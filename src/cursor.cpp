#include "nnwm.hpp"
#include "nnwm_internal.hpp"
#include "actions.hpp"

#include <algorithm>
#include <ctime>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_data_device.h>

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
    wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    wlr_scene_surface *scene_surface
        = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface)
    {
        return nullptr;
    }

    *surface             = scene_surface->surface;
    wlr_scene_tree *tree = node->parent;
    while (tree != nullptr && tree->node.data == nullptr)
    {
        tree = tree->node.parent;
    }
    if (tree == nullptr)
        return nullptr;
    return static_cast<nnwm_toplevel *>(tree->node.data);
}

/* ---- Tab bar hit-testing ---- */

nnwm_toplevel *
tab_toplevel_at(nnwm_server *server, double lx, double ly)
{
    nnwm_output *hit = nullptr;
    int hit_tbx = 0, hit_tby = 0, hit_tbw = 0, hit_tbh = 0;

    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link)
    {
        if (!o->tab_bar || !o->tab_bar->node.enabled)
            continue;
        int ws = o->active_workspace;
        if (o->layout_mode[ws] != nnwm_layout_mode::TABBED)
            continue;

        nnwm_config *cfg       = o->server->config;
        int n                  = ws_count(server, o);
        bool solo              = (n == 1);
        bool hide_tabs         = solo && cfg->layout.tab_smart;
        if (hide_tabs)
            continue;

        int og     = (solo && cfg->gap.smart) ? 0 : cfg->gap.outer;
        int tab_sz = cfg->layout.tab_bar_height > 0 ? cfg->layout.tab_bar_height : 24;
        nnwm_tab_position tab_pos = cfg->layout.tab_position;
        const wlr_box &area       = o->usable_area;

        int tbx, tby, tbw, tbh;
        switch (tab_pos)
        {
            case nnwm_tab_position::BOTTOM:
                tbx = area.x + og;
                tby = area.y + area.height - og - tab_sz;
                tbw = area.width - 2 * og;
                tbh = tab_sz;
                break;
            case nnwm_tab_position::LEFT:
                tbx = area.x + og;
                tby = area.y + og;
                tbw = tab_sz;
                tbh = area.height - 2 * og;
                break;
            case nnwm_tab_position::RIGHT:
                tbx = area.x + area.width - og - tab_sz;
                tby = area.y + og;
                tbw = tab_sz;
                tbh = area.height - 2 * og;
                break;
            default: /* TOP */
                tbx = area.x + og;
                tby = area.y + og;
                tbw = area.width - 2 * og;
                tbh = tab_sz;
                break;
        }

        if (lx >= tbx && lx < tbx + tbw && ly >= tby && ly < tby + tbh)
        {
            hit = o;
            hit_tbx = tbx; hit_tby = tby;
            hit_tbw = tbw; hit_tbh = tbh;
            break;
        }
    }
    if (!hit)
        return nullptr;

    int ws = hit->active_workspace;
    int n  = ws_count(server, hit);
    if (n == 0)
        return nullptr;

    nnwm_tab_position tab_pos = hit->server->config->layout.tab_position;
    int tab_idx;
    if (tab_pos == nnwm_tab_position::LEFT || tab_pos == nnwm_tab_position::RIGHT)
    {
        int rel_y = (int)(ly - hit_tby);
        tab_idx   = rel_y * n / hit_tbh;
    }
    else
    {
        int rel_x = (int)(lx - hit_tbx);
        tab_idx   = rel_x * n / hit_tbw;
    }
    if (tab_idx < 0)  tab_idx = 0;
    if (tab_idx >= n) tab_idx = n - 1;

    int i = 0;
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (tl->output != hit || tl->workspace != ws || tl->floating
            || tl->fullscreen || tl->fake_fullscreen)
            continue;
        if (i++ == tab_idx)
            return tl;
    }
    return nullptr;
}

/* ---- Cursor mode management ---- */

static void
tile_drop_border_hide(nnwm_server *server)
{
    for (int i = 0; i < 4; i++)
        wlr_scene_node_set_enabled(&server->tile_drop_border[i]->node, false);
    server->tile_drag_target        = nullptr;
    server->tile_drag_target_output = nullptr;
}

static void
tile_drop_border_show(nnwm_server *server, int x, int y, int w, int h)
{
    int bw = std::max(2, server->config->border.width);
    /* top, bottom, left, right */
    int lx[4] = { x,          x,          x,          x + w - bw };
    int ly[4] = { y,          y + h - bw, y,          y          };
    int lw[4] = { w,          w,          bw,         bw         };
    int lh[4] = { bw,         bw,         h,          h          };
    for (int i = 0; i < 4; i++)
    {
        wlr_scene_rect_set_size(server->tile_drop_border[i], lw[i], lh[i]);
        wlr_scene_node_set_position(&server->tile_drop_border[i]->node, lx[i], ly[i]);
        wlr_scene_node_set_enabled(&server->tile_drop_border[i]->node, true);
        wlr_scene_node_raise_to_top(&server->tile_drop_border[i]->node);
    }
}

void
reset_cursor_mode(nnwm_server *server)
{
    if (server->cursor_mode == nnwm_cursor_mode::TILE_DRAG)
        tile_drop_border_hide(server);
    server->cursor_mode      = nnwm_cursor_mode::PASSTHROUGH;
    server->grabbed_toplevel = nullptr;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

const char *
resize_cursor_name(uint32_t edges)
{
    bool top    = edges & WLR_EDGE_TOP;
    bool bottom = edges & WLR_EDGE_BOTTOM;
    bool left   = edges & WLR_EDGE_LEFT;
    bool right  = edges & WLR_EDGE_RIGHT;
    if (top    && left)  return "nw-resize";
    if (top    && right) return "ne-resize";
    if (bottom && left)  return "sw-resize";
    if (bottom && right) return "se-resize";
    if (top)             return "n-resize";
    if (bottom)          return "s-resize";
    if (left)            return "w-resize";
    if (right)           return "e-resize";
    return "se-resize";
}

/* ---- Cursor motion processing ---- */

void
process_cursor_move(nnwm_server *server)
{
    nnwm_toplevel *toplevel = server->grabbed_toplevel;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                server->cursor->x - server->grab_x,
                                server->cursor->y - server->grab_y);

    /* Keep toplevel->output in sync with whichever monitor the cursor is on
     * so that toggle_float / arrange_windows tile on the correct monitor. */
    nnwm_output *cur_out = output_at_cursor(server);
    if (cur_out && cur_out != toplevel->output)
    {
        toplevel->output    = cur_out;
        toplevel->workspace = cur_out->active_workspace;
    }
}

static void
process_tile_drag_motion(nnwm_server *server)
{
    nnwm_toplevel *grabbed = server->grabbed_toplevel;
    double sx, sy;
    wlr_surface *surface    = nullptr;
    nnwm_toplevel *under    = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (under && under != grabbed && !under->floating)
    {
        server->tile_drag_target        = under;
        server->tile_drag_target_output = nullptr;
        tile_drop_border_show(server, under->cur_x, under->cur_y,
                              under->cur_w, under->cur_h);
    }
    else
    {
        /* No tiled window under cursor — check if cursor is on a different output.
         * If so, show the usable area of that output as a drop zone. */
        nnwm_output *hover_out = output_at_cursor(server);
        if (hover_out && hover_out != grabbed->output)
        {
            server->tile_drag_target        = nullptr;
            server->tile_drag_target_output = hover_out;
            const wlr_box &ua = hover_out->usable_area;
            tile_drop_border_show(server, ua.x, ua.y, ua.width, ua.height);
        }
        else
        {
            tile_drop_border_hide(server);
        }
    }
}

void
process_cursor_resize(nnwm_server *server)
{
    nnwm_toplevel *toplevel = server->grabbed_toplevel;
    double border_x         = server->cursor->x - server->grab_x;
    double border_y         = server->cursor->y - server->grab_y;
    int new_left            = server->grab_geobox.x;
    int new_right           = server->grab_geobox.x + server->grab_geobox.width;
    int new_top             = server->grab_geobox.y;
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

    int new_width  = new_right - new_left;
    int new_height = new_bottom - new_top;
    int bw         = toplevel->server->config->border.width;
    int th         = toplevel->server->config->titlebar.height;

#ifdef HAVE_XWAYLAND
    if (toplevel->is_xwayland)
    {
        wlr_scene_node_set_position(&toplevel->scene_tree->node, new_left, new_top);
        nnwm_xw_configure(toplevel->xwayland_surface,
                          (int16_t)(new_left + bw),
                          (int16_t)(new_top + bw + th),
                          (uint16_t)(new_width > 2*bw ? new_width - 2*bw : 1),
                          (uint16_t)(new_height > 2*bw+th ? new_height - 2*bw - th : 1));
    }
    else
#endif
    {
        wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
        wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                    new_left - geo_box->x, new_top - geo_box->y);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width - 2 * bw,
                                  new_height - 2 * bw - th);
    }
    update_borders(toplevel, new_width, new_height, bw);

    /* Re-render titlebar at the new width */
    wlr_surface *fs = toplevel->server->seat->keyboard_state.focused_surface;
    render_titlebar(toplevel, new_width - 2 * bw,
                    tl_wlr_surface(toplevel) == fs);
}

void
process_cursor_motion(nnwm_server *server, uint32_t time, bool real_motion)
{
    if (server->drag_icon_tree)
    {
        wlr_scene_node_set_position(&server->drag_icon_tree->node,
                                    (int)server->cursor->x,
                                    (int)server->cursor->y);
    }

    /* During overview drag, only update the cheap Cairo overlay (slot highlight).
     * The GPU texture pass is not re-run, so motion stays smooth. */
    if (server->overview_drag_toplevel) {
        nnwm_output *hov = output_at_cursor(server);
        if (hov && hov->overview)
            overview_update_labels(server, hov);
        return;
    }

    if (server->cursor_mode == nnwm_cursor_mode::MOVE)
    {
        process_cursor_move(server);
        return;
    }
    else if (server->cursor_mode == nnwm_cursor_mode::RESIZE)
    {
        process_cursor_resize(server);
        return;
    }
    else if (server->cursor_mode == nnwm_cursor_mode::TILE_DRAG)
    {
        process_tile_drag_motion(server);
        return;
    }

    if (real_motion && server->cursor_hidden_by_typing)
    {
        server->cursor_hidden_by_typing = false;
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }

    /* Track which output the cursor is on. When focus_follows_mouse is off,
     * only update focused_output if no window currently holds keyboard focus
     * — spawned programs should appear on the keyboard-focused output. */
    {
        nnwm_output *cur_out = output_at_cursor(server);
        if (cur_out)
        {
            bool no_focus = !server->seat->keyboard_state.focused_surface
                            || (!wlr_xdg_toplevel_try_from_wlr_surface(
                                    server->seat->keyboard_state.focused_surface)
#ifdef HAVE_XWAYLAND
                                && !nnwm_xw_try_from_surface(
                                    server->seat->keyboard_state.focused_surface)
#endif
                                );
            if (server->config->focus_follows_mouse || no_focus)
                server->focused_output = cur_out;
        }
    }

    double sx, sy;
    wlr_seat *seat          = server->seat;
    wlr_surface *surface    = nullptr;
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
        if (real_motion && toplevel && server->config->focus_follows_mouse)
            focus_toplevel(toplevel, false);
    }
    else
    {
        wlr_seat_pointer_clear_focus(seat);
    }
}

/* ---- Seat and input device handling ---- */

static void
switch_handle_toggle(wl_listener *listener, void *data)
{
    nnwm_switch *sw = wl_container_of(listener, sw, toggle);
    const auto *event = static_cast<wlr_switch_toggle_event *>(data);
    const char *hook = nullptr;
    if (event->switch_type == WLR_SWITCH_TYPE_LID)
        hook = (event->switch_state == WLR_SWITCH_STATE_ON) ? "lid_close" : "lid_open";
    else if (event->switch_type == WLR_SWITCH_TYPE_TABLET_MODE)
        hook = (event->switch_state == WLR_SWITCH_STATE_ON) ? "tablet_mode_on" : "tablet_mode_off";
    if (hook)
        fire_hook_plain(sw->server, hook);
}

static void
switch_handle_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_switch *sw = wl_container_of(listener, sw, destroy);
    wl_list_remove(&sw->toggle.link);
    wl_list_remove(&sw->destroy.link);
    wl_list_remove(&sw->link);
    delete sw;
}

static void
server_new_switch(nnwm_server *server, wlr_input_device *device)
{
    nnwm_switch *sw  = new nnwm_switch{};
    sw->server       = server;
    sw->wlr_switch   = wlr_switch_from_input_device(device);
    sw->toggle.notify  = switch_handle_toggle;
    wl_signal_add(&sw->wlr_switch->events.toggle, &sw->toggle);
    sw->destroy.notify = switch_handle_destroy;
    wl_signal_add(&device->events.destroy, &sw->destroy);
    wl_list_insert(&server->switches, &sw->link);
}

void
server_new_input(wl_listener *listener, void *data)
{
    nnwm_server *server      = wl_container_of(listener, server, new_input);
    wlr_input_device *device = static_cast<wlr_input_device *>(data);
    switch (device->type)
    {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
            break;
        case WLR_INPUT_DEVICE_SWITCH:
            server_new_switch(server, device);
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
    nnwm_server *server = wl_container_of(listener, server, request_cursor);
    /* During compositor-managed move/resize keep the compositor cursor. */
    if (server->cursor_mode != nnwm_cursor_mode::PASSTHROUGH)
        return;
    auto *event
        = static_cast<wlr_seat_pointer_request_set_cursor_event *>(data);
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
    auto *event = static_cast<wlr_seat_pointer_focus_change_event *>(data);
    if (event->new_surface == nullptr)
    {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
}

static void
on_drag_icon_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_server *server    = wl_container_of(listener, server, drag_icon_destroy);
    wl_list_remove(&server->drag_icon_destroy.link);
    wl_list_init(&server->drag_icon_destroy.link);
    server->drag_icon_tree = nullptr;
}

static void
on_drag_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_server *server = wl_container_of(listener, server, drag_destroy);
    bool dropped        = server->current_drag && server->current_drag->dropped;

    wl_list_remove(&server->drag_destroy.link);
    wl_list_init(&server->drag_destroy.link);
    server->current_drag = nullptr;

    if (!dropped)
        return;

    /* Focus the window that received the drop */
    double sx, sy;
    wlr_surface *surface    = nullptr;
    nnwm_toplevel *toplevel = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (toplevel)
        focus_toplevel(toplevel);
}

void
seat_request_start_drag(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, request_start_drag);
    auto *event = static_cast<wlr_seat_request_start_drag_event *>(data);

    if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin,
                                              event->serial))
    {
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
        return;
    }
    wlr_data_source_destroy(event->drag->source);
}

void
seat_start_drag(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, start_drag);
    auto *drag          = static_cast<wlr_drag *>(data);

    server->current_drag         = drag;
    server->drag_destroy.notify  = on_drag_destroy;
    wl_signal_add(&drag->events.destroy, &server->drag_destroy);

    if (!drag->icon)
        return;

    server->drag_icon_tree = wlr_scene_drag_icon_create(
        server->scene_windows, drag->icon);
    wlr_scene_node_set_position(&server->drag_icon_tree->node,
                                (int)server->cursor->x,
                                (int)server->cursor->y);

    server->drag_icon_destroy.notify = on_drag_icon_destroy;
    wl_signal_add(&drag->icon->events.destroy, &server->drag_icon_destroy);
}

void
handle_new_virtual_pointer(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, new_virtual_pointer);
    auto *event = static_cast<wlr_virtual_pointer_v1_new_pointer_event *>(data);
    wlr_cursor_attach_input_device(server->cursor, &event->new_pointer->pointer.base);
}

void
handle_new_virtual_keyboard(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    auto *vk            = static_cast<wlr_virtual_keyboard_v1 *>(data);
    server_new_keyboard(server, &vk->keyboard.base);
}

void
seat_request_set_selection(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, request_set_selection);
    if (!server->config->clipboard_enabled)
        return;
    auto *event = static_cast<wlr_seat_request_set_selection_event *>(data);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* ---- Cursor event listeners ---- */

void
server_cursor_motion(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, cursor_motion);
    auto *event         = static_cast<wlr_pointer_motion_event *>(data);
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
                    event->delta_y);
    process_cursor_motion(server, event->time_msec, true);
}

void
server_cursor_motion_absolute(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, cursor_motion_absolute);
    auto *event = static_cast<wlr_pointer_motion_absolute_event *>(data);
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
                             event->y);
    process_cursor_motion(server, event->time_msec, true);
}

void
server_cursor_button(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, cursor_button);
    auto *event         = static_cast<wlr_pointer_button_event *>(data);

    /* Overview mode: intercept all button events */
    if (!server->session_lock)
    {
        nnwm_output *hov = output_at_cursor(server);
        if (hov && hov->overview)
        {
            wlr_box ob;
            wlr_output_layout_get_box(server->output_layout, hov->wlr_output, &ob);
            double cx = server->cursor->x - ob.x;
            double cy = server->cursor->y - ob.y;

            if (event->state == WL_POINTER_BUTTON_STATE_PRESSED
                && event->button == BTN_LEFT)
            {
                /* Hit-test: window under cursor? Start drag. */
                int hit_ws = -1;
                nnwm_toplevel *hit = overview_toplevel_at(server, hov, cx, cy, &hit_ws);
                if (hit) {
                    server->overview_drag_toplevel = hit;
                    /* Re-render to show drag state */
                    overview_frame_update(server, hov);
                }
                /* else: press on empty space — wait for release to switch workspace */
                return;
            }

            if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
            {
                if (server->overview_drag_toplevel) {
                    /* Drop: find target workspace slot */
                    const double OV_OUTER = 32.0;
                    const double OV_INNER = 12.0;
                    const int    OV_COLS  = 3;
                    const int    num_ws   = server->config->workspace_count;
                    const int    OV_ROWS  = (num_ws + OV_COLS - 1) / OV_COLS;
                    double slot_w = (ob.width  - 2.0 * OV_OUTER - (OV_COLS - 1) * OV_INNER) / OV_COLS;
                    double slot_h = (ob.height - 2.0 * OV_OUTER - (OV_ROWS - 1) * OV_INNER) / OV_ROWS;

                    int target_ws = -1;
                    for (int ws = 0; ws < num_ws; ws++) {
                        int    col = ws % OV_COLS;
                        int    row = ws / OV_COLS;
                        double sx  = OV_OUTER + col * (slot_w + OV_INNER);
                        double sy  = OV_OUTER + row * (slot_h + OV_INNER);
                        if (cx >= sx && cx < sx + slot_w && cy >= sy && cy < sy + slot_h) {
                            target_ws = ws;
                            break;
                        }
                    }

                    nnwm_toplevel *dtl = server->overview_drag_toplevel;
                    server->overview_drag_toplevel = nullptr;

                    if (target_ws >= 0 && target_ws != dtl->workspace) {
                        dtl->workspace = target_ws;
                        /* If the window was last-focused on the old ws, clear that */
                        nnwm_output *out = dtl->output;
                        if (out) {
                            for (int w = 0; w < server->config->workspace_count; w++) {
                                if (out->last_focused[w] == dtl && w != target_ws)
                                    out->last_focused[w] = nullptr;
                                if (out->prev_focused[w] == dtl && w != target_ws)
                                    out->prev_focused[w] = nullptr;
                            }
                        }
                    }

                    /* Re-arrange and re-render overview */
                    render_overview(server, hov);
                } else {
                    /* Click on empty space: switch workspace and exit */
                    const double OV_OUTER = 32.0;
                    const double OV_INNER = 12.0;
                    const int    OV_COLS  = 3;
                    const int    num_ws   = server->config->workspace_count;
                    const int    OV_ROWS  = (num_ws + OV_COLS - 1) / OV_COLS;
                    double slot_w = (ob.width  - 2.0 * OV_OUTER - (OV_COLS - 1) * OV_INNER) / OV_COLS;
                    double slot_h = (ob.height - 2.0 * OV_OUTER - (OV_ROWS - 1) * OV_INNER) / OV_ROWS;

                    int target_ws = -1;
                    for (int ws = 0; ws < num_ws; ws++) {
                        int    col = ws % OV_COLS;
                        int    row = ws / OV_COLS;
                        double sx  = OV_OUTER + col * (slot_w + OV_INNER);
                        double sy  = OV_OUTER + row * (slot_h + OV_INNER);
                        if (cx >= sx && cx < sx + slot_w && cy >= sy && cy < sy + slot_h) {
                            target_ws = ws;
                            break;
                        }
                    }

                    /* Click on a window: focus it in its workspace */
                    int hit_ws = -1;
                    nnwm_toplevel *hit = overview_toplevel_at(server, hov, cx, cy, &hit_ws);

                    server->focused_output = hov;
                    exit_overview(server, hov);
                    if (hit) {
                        if (hit_ws >= 0 && hit_ws != hov->active_workspace)
                            nnwm::workspace::switch_to(server, hit_ws);
                        focus_toplevel(hit);
                    } else if (target_ws >= 0) {
                        nnwm::workspace::switch_to(server, target_ws);
                    }
                }
                return;
            }

            return; /* swallow all other button events in overview */
        }
    }

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
    {
        if (server->cursor_mode == nnwm_cursor_mode::TILE_DRAG)
        {
            nnwm_toplevel *grabbed    = server->grabbed_toplevel;
            nnwm_toplevel *target     = server->tile_drag_target;
            nnwm_output   *target_out = target ? target->output
                                               : server->tile_drag_target_output;
            tile_drop_border_hide(server);
            nnwm_output *grabbed_out = grabbed ? grabbed->output : nullptr;

            if (grabbed && target)
            {
                /* Swap grabbed and target in the toplevel list so they exchange
                 * tile slots on the next arrange. */
                wl_list *before_grabbed = grabbed->link.prev;
                bool b_before_a = (before_grabbed == &target->link);

                wl_list_remove(&grabbed->link);
                wl_list_insert(target->link.prev, &grabbed->link);

                wl_list_remove(&target->link);
                if (b_before_a)
                    wl_list_insert(&grabbed->link, &target->link);
                else
                    wl_list_insert(before_grabbed, &target->link);

                /* Cross-monitor: swap output and workspace assignments and
                 * reset geometry so tl_set_geometry uses first-layout placement
                 * (animation from within the new output's viewport). */
                if (grabbed->output != target->output
                    || grabbed->workspace != target->workspace)
                {
                    nnwm_output *tmp_out = grabbed->output;
                    int          tmp_ws  = grabbed->workspace;
                    grabbed->output    = target->output;
                    grabbed->workspace = target->workspace;
                    target->output     = tmp_out;
                    target->workspace  = tmp_ws;
                    grabbed->cur_w = grabbed->cur_h = 0;
                    target->cur_w  = target->cur_h  = 0;
                }
            }
            else if (grabbed && target_out && target_out != grabbed->output)
            {
                /* Dropped onto an empty area of another output — move there */
                nnwm_output *old_out = grabbed->output;
                grabbed->output    = target_out;
                grabbed->workspace = target_out->active_workspace;
                grabbed_out        = old_out; /* arrange old output too */
            }

            reset_cursor_mode(server);
            if (grabbed)
            {
                /* If grabbed moved to a different output, reset its geometry so
                 * tl_set_geometry treats it as a first-layout placement — the
                 * animation from-position is then computed relative to the
                 * destination on the new output instead of the old output's
                 * coordinates, which would be outside the new viewport. */
                if (grabbed->output != grabbed_out) {
                    grabbed->cur_x = 0;
                    grabbed->cur_y = 0;
                    grabbed->cur_w = 0;
                    grabbed->cur_h = 0;
                }
                focus_toplevel(grabbed);
                arrange_windows(server, grabbed->output);
                if (grabbed_out && grabbed_out != grabbed->output)
                    arrange_windows(server, grabbed_out);
                if (target_out && target_out != grabbed->output
                               && target_out != grabbed_out)
                    arrange_windows(server, target_out);
            }
        }
        else if (server->cursor_mode != nnwm_cursor_mode::PASSTHROUGH)
        {
            /* Ending a compositor move/resize — don't forward to the window;
             * it never received the matching press either. */
            reset_cursor_mode(server);
        }
        else
        {
            wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                           event->button, event->state);
        }
        return;
    }

    /* Tab bar click: focus the clicked window */
    if (!server->session_lock)
    {
        nnwm_toplevel *tab_tl
            = tab_toplevel_at(server, server->cursor->x, server->cursor->y);
        if (tab_tl)
        {
            focus_toplevel(tab_tl);
            wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                           event->button, event->state);
            return;
        }
    }

    double sx, sy;
    wlr_surface *surface    = nullptr;
    nnwm_toplevel *toplevel = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    /* Super + left click -> move/tile-drag, Super + right click -> resize */
    wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    uint32_t mods    = kb ? wlr_keyboard_get_modifiers(kb) : 0;
    if (!server->session_lock && (mods & WLR_MODIFIER_LOGO) && toplevel)
    {
        focus_toplevel(toplevel);
        if (event->button == BTN_LEFT)
        {
            if (toplevel->floating)
                begin_interactive(toplevel, nnwm_cursor_mode::MOVE, 0);
            else
                begin_interactive(toplevel, nnwm_cursor_mode::TILE_DRAG, 0);
        }
        else if (event->button == BTN_RIGHT)
        {
            if (!toplevel->floating)
            {
                toplevel->floating = true;
                arrange_windows(server, toplevel->output);
            }
            begin_interactive(toplevel, nnwm_cursor_mode::RESIZE,
                              WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        }
        return; /* press not forwarded to the window */
    }

    /* Normal click: forward to window and focus it (not while locked) */
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                   event->button, event->state);
    if (!server->session_lock)
        focus_toplevel(toplevel);
}

void
server_cursor_axis(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, cursor_axis);
    auto *event         = static_cast<wlr_pointer_axis_event *>(data);
    float factor        = server->config->touchpad.scroll_factor;
    wlr_seat_pointer_notify_axis(
        server->seat, event->time_msec, event->orientation,
        event->delta * factor,
        (int32_t)(event->delta_discrete * factor),
        event->source, event->relative_direction);
}

void
server_cursor_frame(wl_listener *listener, void * /*data*/)
{
    nnwm_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}
