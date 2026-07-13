#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <ctime>
#include <linux/input-event-codes.h>

/* ---- Cursor / pointer hit-testing ---- */

nnwm_toplevel *
desktop_toplevel_at(nnwm_server *server, double lx, double ly,
                    wlr_surface **surface, double *sx, double *sy)
{
    wlr_scene_node *node
        = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER)
    {
        return nullptr;
    }
    wlr_scene_buffer  *scene_buffer  = wlr_scene_buffer_from_node(node);
    wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface)
    {
        return nullptr;
    }

    *surface = scene_surface->surface;
    wlr_scene_tree *tree = node->parent;
    while (tree != nullptr && tree->node.data == nullptr)
    {
        tree = tree->node.parent;
    }
    if (tree == nullptr)
        return nullptr;
    return static_cast<nnwm_toplevel*>(tree->node.data);
}

/* ---- Cursor mode management ---- */

void
reset_cursor_mode(nnwm_server *server)
{
    server->cursor_mode      = NNWM_CURSOR_PASSTHROUGH;
    server->grabbed_toplevel = nullptr;
}

/* ---- Cursor motion processing ---- */

void
process_cursor_move(nnwm_server *server)
{
    nnwm_toplevel *toplevel = server->grabbed_toplevel;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                server->cursor->x - server->grab_x,
                                server->cursor->y - server->grab_y);
}

void
process_cursor_resize(nnwm_server *server)
{
    nnwm_toplevel *toplevel = server->grabbed_toplevel;
    double border_x           = server->cursor->x - server->grab_x;
    double border_y           = server->cursor->y - server->grab_y;
    int new_left              = server->grab_geobox.x;
    int new_right  = server->grab_geobox.x + server->grab_geobox.width;
    int new_top    = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP)
    {
        new_top = border_y;
        if (new_top >= new_bottom)
        {
            new_top = new_bottom - 1;
        }
    }
    else if (server->resize_edges & WLR_EDGE_BOTTOM)
    {
        new_bottom = border_y;
        if (new_bottom <= new_top)
        {
            new_bottom = new_top + 1;
        }
    }
    if (server->resize_edges & WLR_EDGE_LEFT)
    {
        new_left = border_x;
        if (new_left >= new_right)
        {
            new_left = new_right - 1;
        }
    }
    else if (server->resize_edges & WLR_EDGE_RIGHT)
    {
        new_right = border_x;
        if (new_right <= new_left)
        {
            new_right = new_left + 1;
        }
    }

    wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                new_left - geo_box->x, new_top - geo_box->y);

    int new_width  = new_right - new_left;
    int new_height = new_bottom - new_top;
    int bw = toplevel->server->config->border_width;
    int th = toplevel->server->config->titlebar_height;
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                               new_width - 2 * bw, new_height - 2 * bw - th);
    update_borders(toplevel, new_width, new_height, bw);

    /* Re-render titlebar at the new width */
    wlr_surface *fs = toplevel->server->seat->keyboard_state.focused_surface;
    render_titlebar(toplevel, new_width - 2 * bw,
                    toplevel->xdg_toplevel->base->surface == fs);
}

void
process_cursor_motion(nnwm_server *server, uint32_t time)
{
    if (server->cursor_mode == NNWM_CURSOR_MOVE)
    {
        process_cursor_move(server);
        return;
    }
    else if (server->cursor_mode == NNWM_CURSOR_RESIZE)
    {
        process_cursor_resize(server);
        return;
    }

    /* Track which output the cursor is on */
    {
        nnwm_output *cur_out = output_at_cursor(server);
        if (cur_out)
            server->focused_output = cur_out;
    }

    double sx, sy;
    wlr_seat    *seat    = server->seat;
    wlr_surface *surface = nullptr;
    nnwm_toplevel *toplevel = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (!toplevel)
    {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
    if (surface)
    {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    }
    else
    {
        wlr_seat_pointer_clear_focus(seat);
    }
}

/* ---- Seat and input device handling ---- */

void
server_new_input(wl_listener *listener, void *data)
{
    nnwm_server      *server = wl_container_of(listener, server, new_input);
    wlr_input_device *device = static_cast<wlr_input_device*>(data);
    switch (device->type)
    {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
            break;
        default:
            break;
    }
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards))
    {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

void
seat_request_cursor(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, request_cursor);
    auto *event =
        static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client *focused_client
        = server->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client)
    {
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
                               event->hotspot_y);
    }
}

void
seat_pointer_focus_change(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, pointer_focus_change);
    auto *event =
        static_cast<wlr_seat_pointer_focus_change_event*>(data);
    if (event->new_surface == nullptr)
    {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
}

void
seat_request_set_selection(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, request_set_selection);
    auto *event =
        static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* ---- Cursor event listeners ---- */

void
server_cursor_motion(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, cursor_motion);
    auto *event = static_cast<wlr_pointer_motion_event*>(data);
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
                    event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

void
server_cursor_motion_absolute(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, cursor_motion_absolute);
    auto *event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
                             event->y);
    process_cursor_motion(server, event->time_msec);
}

void
server_cursor_button(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, cursor_button);
    auto *event = static_cast<wlr_pointer_button_event*>(data);
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                   event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
    {
        reset_cursor_mode(server);
        return;
    }

    double sx, sy;
    wlr_surface   *surface  = nullptr;
    nnwm_toplevel *toplevel = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    /* Super + left click -> move, Super + right click -> resize */
    wlr_keyboard *kb   = wlr_seat_get_keyboard(server->seat);
    uint32_t      mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;
    if (!server->session_lock && (mods & WLR_MODIFIER_LOGO) && toplevel)
    {
        if (!toplevel->floating) {
            toplevel->floating = true;
            arrange_windows(server, output_for_workspace(server, toplevel->workspace));
        }
        focus_toplevel(toplevel);
        if (event->button == BTN_LEFT)
            begin_interactive(toplevel, NNWM_CURSOR_MOVE, 0);
        else if (event->button == BTN_RIGHT)
            begin_interactive(toplevel, NNWM_CURSOR_RESIZE,
                              WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        return;
    }

    /* Normal click: focus the window under the cursor (not while locked) */
    if (!server->session_lock)
        focus_toplevel(toplevel);
}

void
server_cursor_axis(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, cursor_axis);
    auto *event = static_cast<wlr_pointer_axis_event*>(data);
    wlr_seat_pointer_notify_axis(
        server->seat, event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source, event->relative_direction);
}

void
server_cursor_frame(wl_listener *listener, void * /*data*/)
{
    nnwm_server *server
        = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}
