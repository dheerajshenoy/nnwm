#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fnmatch.h>

extern "C" {
#include <wlr/types/wlr_layer_shell_v1.h>
}

/* ---- Window rules application ---- */

void
apply_window_rules(nnwm_server *server, nnwm_toplevel *toplevel)
{
    const char *app_id = toplevel->xdg_toplevel->app_id;
    const char *title  = toplevel->xdg_toplevel->title;
    auto *cfg = server->config;

    for (int i = 0; i < cfg->window_rule_count; i++) {
        const auto &r = cfg->window_rules[i];

        bool match = true;
        if (r.app_id) {
            if (!app_id || fnmatch(r.app_id, app_id, 0) != 0)
                match = false;
        }
        if (match && r.title) {
            if (!title || fnmatch(r.title, title, 0) != 0)
                match = false;
        }
        /* Skip rules with no match criteria */
        if (!r.app_id && !r.title)
            match = false;
        if (!match) continue;

        if (r.floating   >= 0) toplevel->floating   = (bool)r.floating;
        if (r.fullscreen >= 0) toplevel->fullscreen = (bool)r.fullscreen;
        if (r.sticky     >= 0) toplevel->sticky = (bool)r.sticky;
        if (r.workspace  >= 0) toplevel->workspace  = r.workspace;
        if (r.monitor) {
            nnwm_output *out;
            wl_list_for_each(out, &server->outputs, link) {
                if (strcmp(out->wlr_output->name, r.monitor) == 0) {
                    toplevel->output    = out;
                    toplevel->workspace = out->active_workspace;
                    break;
                }
            }
        }
    }
}

/* ---- XDG toplevel lifecycle ---- */

void
xdg_toplevel_map(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);

    nnwm_server *server = toplevel->server;
    nnwm_output *out    = server->focused_output;
    toplevel->output    = out;
    toplevel->workspace = out ? out->active_workspace : 0;
    apply_window_rules(server, toplevel);
    /* After rules, ensure output pointer is still valid */
    if (!toplevel->output)
        toplevel->output = server->focused_output;
    out = toplevel->output;
    if (server->config->new_window_master)
        wl_list_insert(&server->toplevels, &toplevel->link);
    else
        wl_list_insert(server->toplevels.prev, &toplevel->link);
    focus_toplevel(toplevel);
    arrange_windows(server, out);
}

void
xdg_toplevel_unmap(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, unmap);

    if (toplevel == toplevel->server->grabbed_toplevel)
    {
        reset_cursor_mode(toplevel->server);
    }

    nnwm_server *server = toplevel->server;
    int          ws     = toplevel->workspace;
    nnwm_output *out    = toplevel->output;

    bool was_focused = out && out->last_focused[ws] == toplevel;
    if (out && out->last_focused[ws] == toplevel)
        out->last_focused[ws] = nullptr;
    if (out && out->prev_focused[ws] == toplevel)
        out->prev_focused[ws] = nullptr;

    /* For tiled windows, find the adjacent stack neighbor before removal. */
    nnwm_toplevel *stack_next = nullptr;
    if (was_focused && out && !toplevel->floating) {
        stack_next = ws_next(server, out, toplevel);
        if (!stack_next)
            stack_next = ws_prev(server, out, toplevel);
    }

    wl_list_remove(&toplevel->link);

    if (out) {
        nnwm_toplevel *next = nullptr;
        if (was_focused) {
            next = stack_next;
            if (!next) next = out->prev_focused[ws];
            if (!next) next = out->last_focused[ws];
            if (!next) next = ws_first_float(server, out);
        }
        if (next) focus_toplevel(next);
        else      wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, out);
    }

    /* Restore pointer focus to whatever is now under the cursor. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(server,
        (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
}

/* ---- Decoration handling ---- */

nnwm_toplevel *
toplevel_from_deco(nnwm_decoration *deco)
{
    auto *tree = static_cast<wlr_scene_tree*>(deco->wlr_deco->toplevel->base->data);
    if (tree && tree->node.data)
        return static_cast<nnwm_toplevel*>(tree->node.data);
    return nullptr;
}

void
decoration_apply(nnwm_decoration *deco, bool client_side)
{
    auto mode = client_side
        ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
        : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_deco, mode);
}

void
decoration_handle_request_mode(wl_listener *listener, void * /*data*/)
{
    nnwm_decoration  *deco    = wl_container_of(listener, deco, request_mode);
    wlr_xdg_toplevel *xdg_tl = deco->wlr_deco->toplevel;
    nnwm_toplevel    *tl      = toplevel_from_deco(deco);

    /* Always keep a back-reference so server_apply_config can re-apply. */
    if (tl) tl->decoration = deco;

    if (xdg_tl->base->initialized) {
        bool csd = tl ? tl->server->config->client_decorations : false;
        decoration_apply(deco, csd);
        return;
    }
    /* Surface not yet initialized — defer to xdg_toplevel_commit's
     * initial_commit handling where schedule_configure is safe to call. */
}

void
decoration_handle_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_decoration *deco = wl_container_of(listener, deco, destroy);
    nnwm_toplevel   *tl   = toplevel_from_deco(deco);
    if (tl && tl->decoration == deco)
        tl->decoration = nullptr;
    wl_list_remove(&deco->request_mode.link);
    wl_list_remove(&deco->destroy.link);
    delete deco;
}

/* ---- XDG toplevel commit and destroy ---- */

void
xdg_toplevel_commit(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, commit);

    if (toplevel->xdg_toplevel->base->initial_commit)
    {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
        if (toplevel->decoration)
            decoration_apply(toplevel->decoration,
                             toplevel->server->config->client_decorations);
    }
}

void
handle_xdg_toplevel_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, destroy);

    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->set_title.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);

    wlr_scene_node_destroy(&toplevel->scene_tree->node);
    delete toplevel;
}

/* ---- Interactive move / resize ---- */

void
begin_interactive(nnwm_toplevel *toplevel,
                  nnwm_cursor_mode mode, uint32_t edges)
{
    nnwm_server *server = toplevel->server;

    server->grabbed_toplevel = toplevel;
    server->cursor_mode      = mode;

    if (mode == NNWM_CURSOR_MOVE)
    {
        server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
    }
    else
    {
        wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

        double border_x = (toplevel->scene_tree->node.x + geo_box->x)
                          + ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
        double border_y = (toplevel->scene_tree->node.y + geo_box->y)
                          + ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
        server->grab_x  = server->cursor->x - border_x;
        server->grab_y  = server->cursor->y - border_y;

        server->grab_geobox = *geo_box;
        server->grab_geobox.x += toplevel->scene_tree->node.x;
        server->grab_geobox.y += toplevel->scene_tree->node.y;

        server->resize_edges = edges;
    }
}

/* ---- XDG toplevel request handlers ---- */

void
xdg_toplevel_request_move(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    if (toplevel->floating)
        begin_interactive(toplevel, NNWM_CURSOR_MOVE, 0);
}

void
xdg_toplevel_request_resize(wl_listener *listener, void *data)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    if (toplevel->floating) {
        auto *event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
        begin_interactive(toplevel, NNWM_CURSOR_RESIZE, event->edges);
    }
}

void
xdg_toplevel_request_maximize(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, request_maximize);
    if (toplevel->xdg_toplevel->base->initialized)
    {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

void
xdg_toplevel_request_fullscreen(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_toplevel->base->initialized)
        do_toggle_fullscreen(toplevel);
}

/* ---- XDG popup lifecycle ---- */

void
xdg_popup_commit(wl_listener *listener, void * /*data*/)
{
    nnwm_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit)
    {
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

void
handle_xdg_popup_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    delete popup;
}

/* ---- New XDG toplevel / popup / decoration creation ---- */

void
server_new_xdg_toplevel(wl_listener *listener, void *data)
{
    nnwm_server   *server      = wl_container_of(listener, server, new_xdg_toplevel);
    wlr_xdg_toplevel *xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    nnwm_toplevel *toplevel = new nnwm_toplevel{};
    toplevel->server          = server;
    toplevel->xdg_toplevel    = xdg_toplevel;

    toplevel->scene_tree = wlr_scene_tree_create(server->scene_windows);
    toplevel->scene_tree->node.data = toplevel;

    for (int i = 0; i < 4; i++)
        toplevel->border[i] = wlr_scene_rect_create(
            toplevel->scene_tree, 0, 0, server->config->unfocused_color);

    toplevel->titlebar = wlr_scene_buffer_create(toplevel->scene_tree, nullptr);
    wlr_scene_node_set_enabled(&toplevel->titlebar->node, false);

    toplevel->scene_surface = wlr_scene_xdg_surface_create(
        toplevel->scene_tree, xdg_toplevel->base);
    toplevel->scene_surface->node.data = toplevel;
    xdg_toplevel->base->data           = toplevel->scene_surface;

    toplevel->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
    toplevel->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
    toplevel->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit,
                  &toplevel->commit);

    toplevel->destroy.notify = handle_xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    toplevel->set_title.notify = [](wl_listener *listener, void *) {
        nnwm_toplevel *tl = wl_container_of(listener, tl, set_title);
        if (tl->server->config->titlebar_height <= 0 || tl->titlebar_width <= 0)
            return;
        wlr_surface *fs = tl->server->seat->keyboard_state.focused_surface;
        render_titlebar(tl, tl->titlebar_width,
                        tl->xdg_toplevel->base->surface == fs);
    };
    wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);

    toplevel->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
    toplevel->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize,
                  &toplevel->request_resize);
    toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize,
                  &toplevel->request_maximize);
    toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen,
                  &toplevel->request_fullscreen);
}

void
server_new_xdg_popup(wl_listener *listener, void *data)
{
    wlr_xdg_popup *xdg_popup = static_cast<wlr_xdg_popup*>(data);

    nnwm_popup *popup = new nnwm_popup{};
    popup->xdg_popup    = xdg_popup;

    wlr_scene_tree *parent_tree = nullptr;

    wlr_xdg_surface *xdg_parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (xdg_parent) {
        parent_tree = static_cast<wlr_scene_tree*>(xdg_parent->data);
    } else {
        wlr_layer_surface_v1 *layer_parent =
            wlr_layer_surface_v1_try_from_wlr_surface(xdg_popup->parent);
        if (layer_parent) {
            auto *ls = static_cast<nnwm_layer_surface*>(layer_parent->data);
            parent_tree = ls->scene->tree;
        }
    }

    if (!parent_tree) {
        delete popup;
        return;
    }

    xdg_popup->base->data
        = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = handle_xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void
server_new_decoration(wl_listener * /*listener*/, void *data)
{
    auto *wlr_deco = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);

    auto *deco          = new nnwm_decoration{};
    deco->wlr_deco      = wlr_deco;

    deco->request_mode.notify = decoration_handle_request_mode;
    wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);

    deco->destroy.notify = decoration_handle_destroy;
    wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
}
