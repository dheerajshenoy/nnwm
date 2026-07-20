#ifdef HAVE_XWAYLAND

#include "nnwm.hpp"
#include "nnwm_internal.hpp"

extern "C" {
#include <wlr/types/wlr_layer_shell_v1.h>
}

/* ---- Override-redirect (unmanaged) XWayland windows ---- */

static void
xwayland_or_map(wl_listener *listener, void * /*data*/)
{
    nnwm_xwayland_or *or_win = wl_container_of(listener, or_win, map);
    nnwm_server *server      = or_win->server;
    struct wlr_xwayland_surface *xw = or_win->xwayland_surface;

    if (!nnwm_xw_surface(xw))
        return;

    or_win->scene_tree = wlr_scene_tree_create(
        server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
    if (!or_win->scene_tree)
        return;

    wlr_scene_surface_create(or_win->scene_tree, nnwm_xw_surface(xw));
    wlr_scene_node_set_position(&or_win->scene_tree->node,
                                nnwm_xw_x(xw), nnwm_xw_y(xw));
    wlr_scene_node_set_enabled(&or_win->scene_tree->node, true);
}

static void
xwayland_or_unmap(wl_listener *listener, void * /*data*/)
{
    nnwm_xwayland_or *or_win = wl_container_of(listener, or_win, unmap);
    if (or_win->scene_tree)
    {
        wlr_scene_node_set_enabled(&or_win->scene_tree->node, false);
    }
}

static void
xwayland_or_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_xwayland_or *or_win = wl_container_of(listener, or_win, destroy);

    wl_list_remove(&or_win->map.link);
    wl_list_remove(&or_win->unmap.link);
    wl_list_remove(&or_win->destroy.link);
    wl_list_remove(&or_win->request_configure.link);

    if (or_win->scene_tree)
        wlr_scene_node_destroy(&or_win->scene_tree->node);

    delete or_win;
}

static void
xwayland_or_request_configure(wl_listener *listener, void *data)
{
    nnwm_xwayland_or *or_win = wl_container_of(listener, or_win, request_configure);
    struct wlr_xwayland_surface *xw = or_win->xwayland_surface;

    int16_t ex = nnwm_xw_configure_ev_x(data);
    int16_t ey = nnwm_xw_configure_ev_y(data);
    uint16_t ew = nnwm_xw_configure_ev_width(data);
    uint16_t eh = nnwm_xw_configure_ev_height(data);

    nnwm_xw_configure(xw, ex, ey, ew, eh);
    if (or_win->scene_tree)
        wlr_scene_node_set_position(&or_win->scene_tree->node, ex, ey);
}

/* ---- Managed XWayland windows ---- */

static void
xwayland_surface_map(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    nnwm_server *server     = toplevel->server;
    struct wlr_xwayland_surface *xw = toplevel->xwayland_surface;

    if (!nnwm_xw_surface(xw))
        return;

    /* Create scene tree now that the surface exists */
    toplevel->scene_tree = wlr_scene_tree_create(server->scene_windows);
    if (!toplevel->scene_tree)
        return;
    toplevel->scene_tree->node.data = toplevel;

    toplevel->scene_surface = wlr_scene_tree_create(toplevel->scene_tree);
    if (toplevel->scene_surface)
    {
        wlr_scene_surface_create(toplevel->scene_surface, nnwm_xw_surface(xw));
        toplevel->scene_surface->node.data = toplevel;
    }
    nnwm_xw_surface(xw)->data = toplevel->scene_surface;

    /* Borders */
    for (int i = 0; i < 4; i++)
        toplevel->border[i] = wlr_scene_rect_create(
            toplevel->scene_tree, 0, 0, server->config->border.unfocused_color);

    /* Titlebar */
    toplevel->titlebar = wlr_scene_buffer_create(toplevel->scene_tree, nullptr);
    wlr_scene_node_set_enabled(&toplevel->titlebar->node, false);

#ifdef HAVE_SCENEFX
    toplevel->border_bg = nullptr;
    toplevel->fx_shadow = nullptr;
    toplevel->fx_blur   = nullptr;
#endif

    /* Output and workspace */
    nnwm_output *out    = server->focused_output;
    toplevel->output    = out;
    toplevel->workspace = out ? out->active_workspace : 0;

    /* Auto-float if has parent */
    if (!toplevel->floating && nnwm_xw_has_parent(xw))
        toplevel->floating = true;

    apply_window_rules(server, toplevel);
#ifdef HAVE_SCENEFX
    tl_open_anim(toplevel);
#endif
    apply_fx_decorations(toplevel);

    if (!toplevel->output)
        toplevel->output = server->focused_output;
    out = toplevel->output;

    /* Center new floating windows */
    if (toplevel->floating && out && server->config->center_new_floating)
    {
        int bw = server->config->border.width;
        int th = server->config->titlebar.height;
        wlr_box area = out->usable_area;
        int w = nnwm_xw_width(xw) > 0 ? nnwm_xw_width(xw) : 400;
        int h = nnwm_xw_height(xw) > 0 ? nnwm_xw_height(xw) : 300;
        int fx = area.x + (area.width - w - 2 * bw) / 2;
        int fy = area.y + (area.height - h - 2 * bw - th) / 2;
        if (fx < area.x) fx = area.x;
        if (fy < area.y) fy = area.y;
        nnwm_xw_configure(xw, (int16_t)fx, (int16_t)fy,
                           (uint16_t)w, (uint16_t)h);
        wlr_scene_node_set_position(&toplevel->scene_tree->node, fx, fy);
        update_borders(toplevel, w + 2 * bw, h + 2 * bw + th, bw);
    }
    else
    {
        /* Confirm position */
        nnwm_xw_configure(xw, nnwm_xw_x(xw), nnwm_xw_y(xw),
                           nnwm_xw_width(xw), nnwm_xw_height(xw));
    }

    if (server->config->new_window_master)
        wl_list_insert(&server->toplevels, &toplevel->link);
    else
        wl_list_insert(server->toplevels.prev, &toplevel->link);

    if (server->scratchpad_visible)
    {
        toplevel->in_scratchpad = true;
        toplevel->floating      = false;
        wlr_scene_node_reparent(&toplevel->scene_tree->node,
                                server->scene_scratchpad);
        focus_toplevel(toplevel);
        arrange_scratchpad(server);
    }
    else
    {
        if (toplevel->rule_focused && out)
            nnwm::workspace::switch_to(server, toplevel->workspace);
        focus_toplevel(toplevel);
        arrange_windows(server, out);
    }
    ftl_map(toplevel);
    fire_hook_window(server, "window_open", toplevel);
}

static void
xwayland_surface_unmap(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    nnwm_server *server     = toplevel->server;

    fire_hook_window(server, "window_close", toplevel);
    ftl_unmap(toplevel);

#ifdef HAVE_SCENEFX
    tl_close_anim(toplevel);
#endif

    if (toplevel == server->grabbed_toplevel)
        reset_cursor_mode(server);

    int ws           = toplevel->workspace;
    nnwm_output *out = toplevel->output;

    if (toplevel->in_scratchpad)
    {
        bool was_focused_scratch =
            (server->seat->keyboard_state.focused_surface == tl_wlr_surface(toplevel));

        wl_list_remove(&toplevel->link);
#ifdef HAVE_SCENEFX
        if (toplevel->dying)
            wl_list_insert(&server->dying_toplevels, &toplevel->dying_link);
#endif

        if (was_focused_scratch && server->scratchpad_visible)
        {
            nnwm_toplevel *next = nullptr;
            nnwm_toplevel *t;
            wl_list_for_each(t, &server->toplevels, link)
            {
                if (t->in_scratchpad) { next = t; break; }
            }
            if (next)
                focus_toplevel(next);
            else if (out)
            {
                nnwm_toplevel *wsnext = out->last_focused[out->active_workspace];
                if (!wsnext) wsnext = ws_first(server, out);
                if (wsnext)
                    focus_toplevel(wsnext);
                else
                    wlr_seat_keyboard_clear_focus(server->seat);
            }
        }
        arrange_scratchpad(server);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        process_cursor_motion(server, (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
        return;
    }

    bool was_focused = out && out->last_focused[ws] == toplevel;
    if (out && out->last_focused[ws] == toplevel)
        out->last_focused[ws] = nullptr;
    if (out && out->prev_focused[ws] == toplevel)
        out->prev_focused[ws] = nullptr;

    nnwm_toplevel *stack_next = nullptr;
    if (was_focused && out && !toplevel->floating)
    {
        stack_next = ws_next(server, out, toplevel);
        if (!stack_next)
            stack_next = ws_prev(server, out, toplevel);
    }

    wl_list_remove(&toplevel->link);
#ifdef HAVE_SCENEFX
    if (toplevel->dying)
        wl_list_insert(&server->dying_toplevels, &toplevel->dying_link);
#endif

    /* Destroy scene tree (created in map, so destroy in unmap) */
    if (toplevel->scene_tree)
    {
#ifdef HAVE_SCENEFX
        if (!toplevel->dying)
#endif
        {
            wlr_scene_node_destroy(&toplevel->scene_tree->node);
            toplevel->scene_tree    = nullptr;
            toplevel->scene_surface = nullptr;
        }
    }

    if (out)
    {
        nnwm_toplevel *next = nullptr;
        if (was_focused)
        {
            next = stack_next;
            if (!next) next = ws_first(server, out);
            if (!next) next = ws_first_float(server, out);
        }
        if (next)
            focus_toplevel(next);
        else
            wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, out);
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(server, (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
}

static void
xwayland_surface_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

#ifdef HAVE_SCENEFX
    if (toplevel->dying)
        wl_list_remove(&toplevel->dying_link);
#endif

    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->set_title.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);

    /* Destroy scene tree if unmap didn't already */
    if (toplevel->scene_tree)
    {
        wlr_scene_node_destroy(&toplevel->scene_tree->node);
        toplevel->scene_tree    = nullptr;
        toplevel->scene_surface = nullptr;
    }

    delete toplevel;
}

static void
xwayland_request_configure(wl_listener *listener, void *data)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
    struct wlr_xwayland_surface *xw = toplevel->xwayland_surface;

    int16_t ex = nnwm_xw_configure_ev_x(data);
    int16_t ey = nnwm_xw_configure_ev_y(data);
    uint16_t ew = nnwm_xw_configure_ev_width(data);
    uint16_t eh = nnwm_xw_configure_ev_height(data);

    if (toplevel->floating || !toplevel->scene_tree)
    {
        nnwm_xw_configure(xw, ex, ey, ew, eh);
        if (toplevel->scene_tree)
            wlr_scene_node_set_position(&toplevel->scene_tree->node, ex, ey);
    }
    else
    {
        /* Tiled: keep current position/size */
        nnwm_xw_configure(xw, (int16_t)toplevel->cur_x,
                           (int16_t)toplevel->cur_y,
                           (uint16_t)toplevel->cur_w,
                           (uint16_t)toplevel->cur_h);
    }
}

static void
xwayland_surface_request_fullscreen(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    do_toggle_fullscreen(toplevel);
}

static void
xwayland_surface_request_move(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    if (!toplevel->floating)
    {
        toplevel->floating = true;
        arrange_windows(toplevel->server, toplevel->output);
    }
    begin_interactive(toplevel, nnwm_cursor_mode::MOVE, 0);
}

static void
xwayland_surface_request_resize(wl_listener *listener, void *data)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    uint32_t edges = nnwm_xw_resize_ev_edges(data);
    if (!toplevel->floating)
    {
        toplevel->floating = true;
        arrange_windows(toplevel->server, toplevel->output);
    }
    begin_interactive(toplevel, nnwm_cursor_mode::RESIZE, edges);
}

/* ---- New XWayland surface handler ---- */

void
server_new_xwayland_surface(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xw = (struct wlr_xwayland_surface *)data;

    if (nnwm_xw_override_redirect(xw))
    {
        /* Unmanaged window: tooltip, menu, etc. */
        auto *or_win = new nnwm_xwayland_or{};
        or_win->xwayland_surface = xw;
        or_win->server           = server;
        or_win->scene_tree       = nullptr;

        or_win->map.notify = xwayland_or_map;
        wl_signal_add(nnwm_xw_events_associate(xw), &or_win->map);
        or_win->unmap.notify = xwayland_or_unmap;
        wl_signal_add(nnwm_xw_events_dissociate(xw), &or_win->unmap);
        or_win->destroy.notify = xwayland_or_destroy;
        wl_signal_add(nnwm_xw_events_destroy(xw), &or_win->destroy);
        or_win->request_configure.notify = xwayland_or_request_configure;
        wl_signal_add(nnwm_xw_events_request_configure(xw), &or_win->request_configure);
        return;
    }

    /* Managed window */
    nnwm_toplevel *toplevel = new nnwm_toplevel{};
    toplevel->server           = server;
    toplevel->xdg_toplevel     = nullptr;
    toplevel->is_xwayland      = true;
    toplevel->xwayland_surface = xw;
    toplevel->rule_opacity     = -1.0f;
    toplevel->rule_blur        = -1;
    toplevel->rule_focused     = false;
#ifdef HAVE_SCENEFX
    toplevel->rule_anim_open_style  = -1;
    toplevel->rule_anim_close_style = -1;
    toplevel->rule_no_anim          = -1;
    toplevel->geo_anim = toplevel->fade_anim = toplevel->bcol_anim = false;
    toplevel->geo_duration_ms        = toplevel->fade_duration_ms
        = toplevel->bcol_duration_ms = 0;
    toplevel->geo_easing = toplevel->fade_easing = toplevel->bcol_easing
        = nnwm_easing::OUT;
    toplevel->dying = false;
    wl_list_init(&toplevel->dying_link);
#endif
    toplevel->cur_x = toplevel->cur_y = toplevel->cur_w = toplevel->cur_h = 0;

    /* scene_tree is created in map because surface is NULL here */
    toplevel->scene_tree    = nullptr;
    toplevel->scene_surface = nullptr;

    toplevel->map.notify = xwayland_surface_map;
    wl_signal_add(nnwm_xw_events_associate(xw), &toplevel->map);

    toplevel->unmap.notify = xwayland_surface_unmap;
    wl_signal_add(nnwm_xw_events_dissociate(xw), &toplevel->unmap);

    toplevel->destroy.notify = xwayland_surface_destroy;
    wl_signal_add(nnwm_xw_events_destroy(xw), &toplevel->destroy);

    /* We reuse the commit listener for request_configure */
    toplevel->commit.notify = xwayland_request_configure;
    wl_signal_add(nnwm_xw_events_request_configure(xw), &toplevel->commit);

    toplevel->set_title.notify = [](wl_listener *l, void *)
    {
        nnwm_toplevel *tl   = wl_container_of(l, tl, set_title);
        nnwm_server *server = tl->server;
        nnwm_config *cfg    = server->config;
        if (cfg->titlebar.height > 0 && tl->titlebar_width > 0)
        {
            wlr_surface *fs = server->seat->keyboard_state.focused_surface;
            render_titlebar(tl, tl->titlebar_width, tl_wlr_surface(tl) == fs);
        }
        if (tl->output && !tl->floating
            && tl->output->layout_mode[tl->workspace] == nnwm_layout_mode::TABBED)
        {
            rerender_tab_bar(server, tl->output);
        }
        ftl_set_title(tl);
    };
    wl_signal_add(nnwm_xw_events_set_title(xw), &toplevel->set_title);

    toplevel->request_move.notify = xwayland_surface_request_move;
    wl_signal_add(nnwm_xw_events_request_move(xw), &toplevel->request_move);

    toplevel->request_resize.notify = xwayland_surface_request_resize;
    wl_signal_add(nnwm_xw_events_request_resize(xw), &toplevel->request_resize);

    /* request_maximize reuses listener slot */
    toplevel->request_maximize.notify = [](wl_listener * /*l*/, void * /*data*/) {};
    wl_signal_add(nnwm_xw_events_request_maximize(xw), &toplevel->request_maximize);

    toplevel->request_fullscreen.notify = xwayland_surface_request_fullscreen;
    wl_signal_add(nnwm_xw_events_request_fullscreen(xw), &toplevel->request_fullscreen);
}

/* ---- XWayland init / fini (called from main.cpp) ---- */

bool nnwm_xwayland_init(nnwm_server *server)
{
    server->xwayland = nnwm_xwl_create(server->wl_display,
                                        server->compositor, 1);
    if (!server->xwayland)
        return false;

    server->new_xwayland_surface.notify = server_new_xwayland_surface;
    wl_signal_add(nnwm_xwl_events_new_surface(server->xwayland),
                  &server->new_xwayland_surface);
    nnwm_xwl_set_seat(server->xwayland, server->seat);
    setenv("DISPLAY", nnwm_xwl_display_name(server->xwayland), true);
    return true;
}

void nnwm_xwayland_fini(nnwm_server *server)
{
    if (server->xwayland) {
        wl_list_remove(&server->new_xwayland_surface.link);
        nnwm_xwl_destroy(server->xwayland);
        server->xwayland = nullptr;
    }
}

#endif /* HAVE_XWAYLAND */
