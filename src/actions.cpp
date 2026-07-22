#include "lua/config.hpp"
#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <linux/input-event-codes.h>

extern "C"
{
#include <libinput.h>
#include <wlr/backend/libinput.h>
}

/* ---- Keyboard modifiers ---- */

void
keyboard_handle_modifiers(wl_listener *listener, void * /*data*/)
{
    nnwm_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                       &keyboard->wlr_keyboard->modifiers);
}

/* ---- Compositor actions (called from Lua keybinding callbacks) ---- */

void
do_toggle_fullscreen(nnwm_toplevel *tl)
{
    tl->fullscreen = !tl->fullscreen;
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland)
        nnwm_xw_set_fullscreen(tl->xwayland_surface, tl->fullscreen ? 1 : 0);
    else
#endif
        wlr_xdg_toplevel_set_fullscreen(tl->xdg_toplevel, tl->fullscreen);
    ftl_set_fullscreen(tl, tl->fullscreen);

    nnwm_server *server = tl->server;
    nnwm_output *out    = tl->output;

    if (tl->fullscreen && out)
    {
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output,
                                  &area);

#ifdef HAVE_SCENEFX
        nnwm_config *cfg = server->config;
        bool do_anim = cfg->fx.animation.enabled
                    && cfg->fx.animation.duration_ms > 0
                    && cfg->fx.animation.layout_style != nnwm_layout_anim::NONE
                    && tl->cur_w > 0;
        if (do_anim)
        {
            tl->geo_from_x = tl->cur_x;
            tl->geo_from_y = tl->cur_y;
            tl->geo_from_w = tl->cur_w;
            tl->geo_from_h = tl->cur_h;
            tl->geo_to_x   = area.x;
            tl->geo_to_y   = area.y;
            tl->geo_to_w   = area.width;
            tl->geo_to_h   = area.height;
            tl->geo_bw          = 0;
            tl->geo_anim        = true;
            tl->geo_t0          = anim_now();
            tl->geo_then_hide   = false;
            tl->geo_duration_ms = eff_duration(cfg, cfg->fx.animation.layout_duration_ms);
            tl->geo_easing      = eff_easing(cfg, cfg->fx.animation.layout_easing);
            /* Start visually at the from state */
            wlr_scene_node_set_position(&tl->scene_tree->node,
                                        tl->geo_from_x, tl->geo_from_y);
            update_borders(tl, tl->geo_from_w, tl->geo_from_h, 0);
            tl->cur_x = tl->geo_from_x;
            tl->cur_y = tl->geo_from_y;
            tl->cur_w = tl->geo_from_w;
            tl->cur_h = tl->geo_from_h;
            tl_xdg_set_size(tl, area.width, area.height);
#ifdef HAVE_XWAYLAND
            if (tl->is_xwayland)
                nnwm_xw_configure(tl->xwayland_surface, (int16_t)area.x, (int16_t)area.y,
                                  (uint16_t)area.width, (uint16_t)area.height);
#endif
            apply_fx_decorations(tl);
            arrange_windows(server, out);
            return;
        }
#endif
        wlr_scene_node_set_position(&tl->scene_tree->node, area.x, area.y);
        tl_xdg_set_size(tl, area.width, area.height);
#ifdef HAVE_XWAYLAND
        if (tl->is_xwayland)
            nnwm_xw_configure(tl->xwayland_surface, (int16_t)area.x, (int16_t)area.y,
                              (uint16_t)area.width, (uint16_t)area.height);
#endif
        update_borders(tl, area.width, area.height, 0);
        tl->cur_x = area.x;
        tl->cur_y = area.y;
        tl->cur_w = area.width;
        tl->cur_h = area.height;
    }

    apply_fx_decorations(tl);
    arrange_windows(server, out);
}

void
do_toggle_fake_fullscreen(nnwm_toplevel *tl)
{
    tl->fake_fullscreen = !tl->fake_fullscreen;

    nnwm_server *server = tl->server;
    nnwm_output *out    = tl->output;

    if (tl->fake_fullscreen && out)
    {
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output,
                                  &area);
        wlr_scene_node_set_position(&tl->scene_tree->node, area.x, area.y);
        tl_xdg_set_size(tl, area.width, area.height);
#ifdef HAVE_XWAYLAND
        if (tl->is_xwayland)
            nnwm_xw_configure(tl->xwayland_surface, (int16_t)area.x, (int16_t)area.y,
                              (uint16_t)area.width, (uint16_t)area.height);
#endif
        update_borders(tl, area.width, area.height, 0);
    }

    arrange_windows(server, out);
}

nnwm_toplevel *
get_focused_toplevel(nnwm_server *server)
{
    wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    if (!focused)
        return nullptr;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
    {
        if (tl_wlr_surface(t) == focused)
            return t;
    }
    return nullptr;
}

void
nnwm::quit(nnwm_server *server)
{
    wl_display_terminate(server->wl_display);
}

void
nnwm::close(nnwm_server *server)
{
    nnwm_toplevel *focused = get_focused_toplevel(server);
    if (focused)
        tl_send_close(focused);
}

void
nnwm::spawn(nnwm_server *server, const char *cmd)
{
    if (!server->wayland_started)
    {
        if (server->autostart_count >= server->autostart_cap)
        {
            server->autostart_cap
                = server->autostart_cap ? server->autostart_cap * 2 : 8;
            server->autostart_cmds = static_cast<char **>(
                std::realloc(server->autostart_cmds,
                             sizeof(char *) * server->autostart_cap));
        }
        server->autostart_cmds[server->autostart_count++] = strdup(cmd);
        return;
    }
    if (fork() == 0)
        execl("/bin/sh", "/bin/sh", "-c", cmd, static_cast<char *>(nullptr));
}

void
nnwm::flush_autostart(nnwm_server *server)
{
    for (int i = 0; i < server->autostart_count; i++)
    {
        if (fork() == 0)
            execl("/bin/sh", "/bin/sh", "-c", server->autostart_cmds[i],
                  static_cast<char *>(nullptr));
        free(server->autostart_cmds[i]);
    }
    free(server->autostart_cmds);
    server->autostart_cmds  = nullptr;
    server->autostart_count = 0;
    server->autostart_cap   = 0;
}

void
nnwm::spawn_once(nnwm_server *server, const char *cmd)
{
    for (int i = 0; i < server->spawn_once_count; i++)
        if (strcmp(server->spawn_once_cmds[i], cmd) == 0)
            return;

    if (server->spawn_once_count >= server->spawn_once_cap)
    {
        server->spawn_once_cap
            = server->spawn_once_cap ? server->spawn_once_cap * 2 : 8;
        server->spawn_once_cmds = static_cast<char **>(realloc(
            server->spawn_once_cmds, server->spawn_once_cap * sizeof(char *)));
    }
    server->spawn_once_cmds[server->spawn_once_count++] = strdup(cmd);
    nnwm::spawn(server, cmd);
}

void
nnwm::focus::left(nnwm_server *server)
{
    if (server->scratchpad_visible)
    {
        nnwm_toplevel *tl = scratch_first(server);
        if (tl)
            focus_toplevel(tl);
        return;
    }
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *tl = ws_first(server, out);
    if (tl)
        focus_toplevel(tl);
}

void
nnwm::focus::right(nnwm_server *server)
{
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur)
        return;
    if (server->scratchpad_visible)
    {
        nnwm_toplevel *next = scratch_next(server, cur);
        if (next)
            focus_toplevel(next);
        return;
    }
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (next)
        focus_toplevel(next);
}

void
nnwm::focus::next(nnwm_server *server)
{
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur)
        return;
    if (server->scratchpad_visible)
    {
        nnwm_toplevel *next = scratch_next(server, cur);
        if (!next)
            next = scratch_first(server);
        if (next)
            focus_toplevel(next);
        return;
    }
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (!next)
        next = ws_first(server, out);
    if (next)
        focus_toplevel(next);
}

void
nnwm::focus::prev(nnwm_server *server)
{
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur)
        return;
    if (server->scratchpad_visible)
    {
        nnwm_toplevel *prev = scratch_prev(server, cur);
        if (!prev)
            prev = scratch_last(server);
        if (prev)
            focus_toplevel(prev);
        return;
    }
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *prev = ws_prev(server, out, cur);
    if (!prev)
        prev = ws_last(server, out);
    if (prev)
        focus_toplevel(prev);
}

void
nnwm::focus::mode_toggle(nnwm_server *server)
{
    if (server->scratchpad_visible)
        return;
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (cur && cur->floating)
    {
        nnwm_toplevel *next = ws_first(server, out);
        if (next)
            focus_toplevel(next);
    }
    else
    {
        nnwm_toplevel *next = ws_first_float(server, out);
        if (next)
            focus_toplevel(next);
    }
}

void
nnwm::focus::next_float(nnwm_server *server)
{
    if (server->scratchpad_visible)
        return;
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur  = get_focused_toplevel(server);
    nnwm_toplevel *next = cur ? ws_next_float(server, out, cur) : nullptr;
    if (!next)
        next = ws_first_float(server, out);
    if (next)
        focus_toplevel(next);
}

void
nnwm::focus::prev_float(nnwm_server *server)
{
    if (server->scratchpad_visible)
        return;
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur  = get_focused_toplevel(server);
    nnwm_toplevel *prev = cur ? ws_prev_float(server, out, cur) : nullptr;
    if (!prev)
        prev = ws_last_float(server, out);
    if (prev)
        focus_toplevel(prev);
}

void
nnwm::swap::left(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur || cur->floating)
        return;
    nnwm_toplevel *first = ws_first(server, out);
    if (cur == first)
        return;
    wl_list_remove(&cur->link);
    wl_list_insert(first->link.prev, &cur->link);
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::swap::right(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur || cur->floating)
        return;
    nnwm_toplevel *master = ws_first(server, out);
    if (!master)
        return;
    if (cur != master)
    {
        wl_list_remove(&cur->link);
        wl_list_insert(master->link.prev, &cur->link);
    }
    else
    {
        nnwm_toplevel *first_stack = ws_next(server, out, master);
        if (!first_stack)
            return;
        wl_list_remove(&master->link);
        wl_list_insert(&first_stack->link, &master->link);
    }
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::swap::next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur || cur->floating)
        return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (next)
    {
        wl_list_remove(&cur->link);
        wl_list_insert(&next->link, &cur->link);
    }
    else
    {
        nnwm_toplevel *first = ws_first(server, out);
        if (!first || first == cur)
            return;
        wl_list_remove(&cur->link);
        wl_list_insert(
            first->link.prev,
            &cur->link); /* insert before first → cur becomes master */
    }
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::swap::prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur || cur->floating)
        return;
    nnwm_toplevel *prev = ws_prev(server, out, cur);
    if (prev)
    {
        wl_list_remove(&cur->link);
        wl_list_insert(prev->link.prev, &cur->link);
    }
    else
    {
        nnwm_toplevel *last = ws_last(server, out);
        if (!last || last == cur)
            return;
        wl_list_remove(&cur->link);
        wl_list_insert(&last->link,
                       &cur->link); /* insert after last → cur becomes last */
    }
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::swap::master(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur || cur->floating)
        return;
    nnwm_toplevel *master = ws_first(server, out);
    if (!master || cur == master)
        return;

    struct wl_list *before_cur = cur->link.prev;
    bool adjacent              = (before_cur == &master->link);

    wl_list_remove(&cur->link);
    wl_list_insert(master->link.prev, &cur->link);

    wl_list_remove(&master->link);
    if (adjacent)
        wl_list_insert(&cur->link, &master->link);
    else
        wl_list_insert(before_cur, &master->link);

    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::cycle(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out || ws_count(server, out) < 2)
        return;
    nnwm_toplevel *focused = get_focused_toplevel(server);
    if (focused && focused->floating)
        return;
    nnwm_toplevel *last  = ws_last(server, out);
    nnwm_toplevel *first = ws_first(server, out);
    if (!last || last == first)
        return;
    wl_list_remove(&last->link);
    wl_list_insert(first->link.prev, &last->link);
    focus_toplevel(last);
    arrange_windows(server, out);
}

void
nnwm::workspace::switch_to(nnwm_server *server, int ws)
{
    nnwm_output *out = server->focused_output;
    if (!out || ws < 0 || ws >= server->config->workspace_count)
        return;

    if (ws == out->active_workspace)
    {
        if (server->config->workspace_back_and_forth && out->prev_workspace != ws)
            ws = out->prev_workspace;
        else
            return;
    }

    int old_ws            = out->active_workspace;
    out->prev_workspace   = old_ws;
    out->active_workspace = ws;

    /* Sync scene visibility: sticky windows always remain visible.
     * Scratchpad windows are skipped — their visibility is owned by the
     * scratchpad toggle and must not be disturbed by workspace changes. */
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (tl->in_scratchpad) continue;
        wlr_scene_node_set_enabled(
            &tl->scene_tree->node,
            tl->sticky
                || (tl->output && tl->output->active_workspace == tl->workspace));
    }

    /* Prefer the last-focused window on the target workspace (may be fullscreen
     * or floating), fall back to first tiled, then first floating. */
    nnwm_toplevel *next = out->last_focused[ws];
    if (!next || next->output != out || next->workspace != ws)
        next = ws_first(server, out);
    if (!next)
        next = ws_first_float(server, out);
    if (next)
        focus_toplevel(next);
    else
        wlr_seat_keyboard_clear_focus(server->seat);

    arrange_windows(server, out);

#ifdef HAVE_SCENEFX
    /* Workspace animation — suppressed in overview (layout is static there) */
    if (!out->overview
        && server->config->fx.animation.enabled
        && server->config->fx.animation.duration_ms > 0)
    {
        nnwm_config *cfg       = server->config;
        nnwm_ws_style ws_style = cfg->fx.animation.ws_style;
        int ws_dur = eff_duration(cfg, cfg->fx.animation.ws_duration_ms);
        nnwm_easing ws_ease = eff_easing(cfg, cfg->fx.animation.ws_easing);

        if (ws_style == nnwm_ws_style::NONE)
        {
            /* No animation — nothing extra to do */
        }
        else if (ws_style == nnwm_ws_style::FADE)
        {
            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (tl->output != out || tl->sticky)
                    continue;
                /* Fullscreen and floating windows are handled by the
                 * visibility sweep; skip geo/fade animation for them. */
                if (tl->fullscreen || tl->fake_fullscreen || tl->floating)
                    continue;
                float op = (tl->rule_opacity >= 0.0f) ? tl->rule_opacity
                                                      : cfg->fx.opacity;
                if (tl->workspace == old_ws)
                {
                    tl_start_fade(tl, op, 0.0f, ws_dur, ws_ease);
                    wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
                    tl->geo_then_hide = true;
                }
                else if (tl->workspace == ws)
                {
                    tl_start_fade(tl, 0.0f, op, ws_dur, ws_ease);
                }
            }
        }
        else
        {
            /* nnwm_ws_style::SLIDE — default behavior */
            wlr_box area;
            wlr_output_layout_get_box(server->output_layout, out->wlr_output,
                                      &area);
            int slide = (ws > old_ws) ? -area.width : area.width;

            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (tl->output != out || tl->sticky)
                    continue;
                /* Fullscreen and floating windows are handled by the
                 * visibility sweep; skip the geo animation for them.
                 * Floating windows have no stable geo_to_x (it gets polluted
                 * by slide-out), so animating them would move them off-screen. */
                if (tl->fullscreen || tl->fake_fullscreen || tl->floating)
                    continue;

                if (tl->workspace == old_ws)
                {
                    /* Slide out: keep visible, animate to off-screen, then hide
                     */
                    wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
                    tl->geo_from_x      = tl->cur_x;
                    tl->geo_from_y      = tl->cur_y;
                    tl->geo_from_w      = tl->cur_w;
                    tl->geo_from_h      = tl->cur_h;
                    tl->geo_to_x        = tl->cur_x + slide;
                    tl->geo_to_y        = tl->cur_y;
                    tl->geo_to_w        = tl->cur_w;
                    tl->geo_to_h        = tl->cur_h;
                    tl->geo_duration_ms = ws_dur;
                    tl->geo_easing      = ws_ease;
                    tl->geo_anim        = true;
                    tl->geo_t0          = anim_now();
                    tl->geo_then_hide   = true;
                }
                else if (tl->workspace == ws)
                {
                    /* Slide in: override from position to off-screen opposite
                     * side */
                    tl->geo_from_x      = tl->geo_to_x - slide;
                    tl->geo_from_y      = tl->geo_to_y;
                    tl->geo_from_w      = tl->geo_to_w;
                    tl->geo_from_h      = tl->geo_to_h;
                    tl->geo_duration_ms = ws_dur;
                    tl->geo_easing      = ws_ease;
                    tl->geo_anim        = true;
                    tl->geo_t0          = anim_now();
                    tl->geo_then_hide   = false;
                    wlr_scene_node_set_position(&tl->scene_tree->node,
                                                tl->geo_from_x, tl->geo_from_y);
                    update_borders(tl, tl->geo_from_w, tl->geo_from_h,
                                   tl->geo_bw);
                    tl->cur_x = tl->geo_from_x;
                    tl->cur_y = tl->geo_from_y;
                }
            }
        }
    }
#endif /* HAVE_SCENEFX */

    nnwm::ext_workspace_notify(server);
    bar_notify_workspace_change(server, out);
    fire_hook_workspace(server, "workspace_switch", out);
}

static void
warp_cursor_to_output(nnwm_server *server, nnwm_output *out)
{
    wlr_box area;
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &area);
    wlr_cursor_warp(server->cursor, nullptr, area.x + area.width / 2.0,
                    area.y + area.height / 2.0);
}

void
nnwm::monitor::focus_next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_output *next = output_cycle(server, out, +1);
    if (!next)
        return;
    server->focused_output = next;
    nnwm_toplevel *tl      = ws_first(server, next);
    if (tl)
        focus_toplevel(tl);
    else
    {
        wlr_seat_keyboard_clear_focus(server->seat);
        unfocus_all_borders(server);
    }
    warp_cursor_to_output(server, next);
}

void
nnwm::monitor::focus_prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_output *next = output_cycle(server, out, -1);
    if (!next)
        return;
    server->focused_output = next;
    nnwm_toplevel *tl      = ws_first(server, next);
    if (tl)
        focus_toplevel(tl);
    else
    {
        wlr_seat_keyboard_clear_focus(server->seat);
        unfocus_all_borders(server);
    }
    warp_cursor_to_output(server, next);
}

/* ---- Overview-grid geometry helpers ---- */

static constexpr int OV_COLS = 3;

/* Switch active workspace in-place (visibility + focused_output) without
 * triggering the full workspace::switch_to animation/arrange path. */
static void
ov_switch_ws(nnwm_server *server, nnwm_output *out, int ws)
{
    if (out->active_workspace == ws) return;
    out->active_workspace = ws;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
    {
        if (t->in_scratchpad) continue;
        wlr_scene_node_set_enabled(
            &t->scene_tree->node,
            t->sticky || (t->output && t->output->active_workspace == t->workspace));
    }
    nnwm::ext_workspace_notify(server);
}

void
nnwm::focus::dir(nnwm_server *server, const char *direction)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;

    bool is_left  = strcmp(direction, "left")  == 0;
    bool is_right = strcmp(direction, "right") == 0;
    bool is_up    = strcmp(direction, "up")    == 0;
    bool is_down  = strcmp(direction, "down")  == 0;
    if (!is_left && !is_right && !is_up && !is_down) return;

    /* ---- Overview mode: navigate between workspace slots in the grid ---- */
    if (out->overview)
    {
        int cur = out->active_workspace;
        int cur_col = cur % OV_COLS;
        int cur_row = cur / OV_COLS;
        int target = -1;

        int num_ws = server->config->workspace_count;
        if (is_left  && cur_col > 0)                  target = cur - 1;
        if (is_right && cur_col < OV_COLS - 1
                     && cur + 1 < num_ws)              target = cur + 1;
        if (is_up    && cur_row > 0)                   target = cur - OV_COLS;
        if (is_down  && cur + OV_COLS < num_ws)        target = cur + OV_COLS;

        if (target < 0) return;
        ov_switch_ws(server, out, target);

        nnwm_toplevel *next = ws_first(server, out);
        if (next)
            focus_toplevel(next);
        else
        {
            wlr_seat_keyboard_clear_focus(server->seat);
            unfocus_all_borders(server);
        }
        render_overview(server, out);
        return;
    }

    nnwm_toplevel *focused = get_focused_toplevel(server);

    /* ---- Scratchpad mode: navigate only within scratchpad windows ---- */
    if (server->scratchpad_visible)
    {
        bool go_prev = is_left || is_up;
        if (go_prev)
        {
            nnwm_toplevel *prev = focused ? scratch_prev(server, focused) : nullptr;
            if (!prev)
                prev = scratch_last(server);
            if (prev)
                focus_toplevel(prev);
        }
        else
        {
            nnwm_toplevel *next = focused ? scratch_next(server, focused) : nullptr;
            if (!next)
                next = scratch_first(server);
            if (next)
                focus_toplevel(next);
        }
        return;
    }

    int ws = out->active_workspace;

    /* ---- Tabbed mode: cycle to the adjacent tab ---- */
    bool skip_on_output_search = false;
    if (out->layout_mode[ws] == nnwm_layout_mode::TABBED) {
        bool go_prev = is_left || is_up;

        /* Find the tiled window immediately before or after focused in list order */
        nnwm_toplevel *tab_target = nullptr;
        if (go_prev) {
            nnwm_toplevel *tl, *prev = nullptr;
            wl_list_for_each(tl, &server->toplevels, link) {
                if (tl->output != out || tl->floating) continue;
                if (tl->workspace != ws && !tl->sticky) continue;
                if (tl == focused) { tab_target = prev; break; }
                prev = tl;
            }
        } else {
            nnwm_toplevel *tl;
            bool found = false;
            wl_list_for_each(tl, &server->toplevels, link) {
                if (found) {
                    if (tl->output != out || tl->floating) continue;
                    if (tl->workspace != ws && !tl->sticky) continue;
                    tab_target = tl;
                    break;
                }
                if (tl == focused) found = true;
            }
        }
        if (tab_target) {
            focus_toplevel(tab_target);
            return;
        }
        skip_on_output_search = true; /* at edge — try cross-monitor below */
    }

    /* Reference point: center of focused window, or center of usable area */
    int fcx = out->usable_area.x + out->usable_area.width  / 2;
    int fcy = out->usable_area.y + out->usable_area.height / 2;
    if (focused && focused->output == out)
    {
        fcx = focused->cur_x + focused->cur_w / 2;
        fcy = focused->cur_y + focused->cur_h / 2;
    }

    /* Find the nearest window in the requested direction on the current output */
    nnwm_toplevel *best     = nullptr;
    int            best_pri = INT_MAX; /* primary-axis distance (smaller = closer) */
    int            best_sec = INT_MAX; /* secondary-axis distance (tiebreak)       */

    if (!skip_on_output_search) {
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl == focused) continue;
            if (tl->output != out) continue;
            if (tl->workspace != ws && !tl->sticky) continue;
            if (tl->in_scratchpad) continue;

            int cx = tl->cur_x + tl->cur_w / 2;
            int cy = tl->cur_y + tl->cur_h / 2;

            int pri, sec;
            bool valid = false;

            if (is_left  && cx < fcx) { pri = fcx - cx; sec = abs(cy - fcy); valid = true; }
            if (is_right && cx > fcx) { pri = cx - fcx; sec = abs(cy - fcy); valid = true; }
            if (is_up    && cy < fcy) { pri = fcy - cy; sec = abs(cx - fcx); valid = true; }
            if (is_down  && cy > fcy) { pri = cy - fcy; sec = abs(cx - fcx); valid = true; }

            if (valid && (pri < best_pri || (pri == best_pri && sec < best_sec)))
            {
                best     = tl;
                best_pri = pri;
                best_sec = sec;
            }
        }
    }

    if (best)
    {
        focus_toplevel(best);
        return;
    }

    /* No window in that direction — look for an adjacent monitor */
    wlr_box cur_box;
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &cur_box);
    int cur_cx = cur_box.x + cur_box.width  / 2;
    int cur_cy = cur_box.y + cur_box.height / 2;

    nnwm_output *best_out     = nullptr;
    int          best_out_pri = INT_MAX;
    int          best_out_sec = INT_MAX;

    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link)
    {
        if (o == out) continue;
        wlr_box ob;
        wlr_output_layout_get_box(server->output_layout, o->wlr_output, &ob);
        int ocx = ob.x + ob.width  / 2;
        int ocy = ob.y + ob.height / 2;

        int pri, sec;
        bool valid = false;

        if (is_left  && ocx < cur_cx) { pri = cur_cx - ocx; sec = abs(ocy - cur_cy); valid = true; }
        if (is_right && ocx > cur_cx) { pri = ocx - cur_cx; sec = abs(ocy - cur_cy); valid = true; }
        if (is_up    && ocy < cur_cy) { pri = cur_cy - ocy; sec = abs(ocx - cur_cx); valid = true; }
        if (is_down  && ocy > cur_cy) { pri = ocy - cur_cy; sec = abs(ocx - cur_cx); valid = true; }

        if (valid && (pri < best_out_pri || (pri == best_out_pri && sec < best_out_sec)))
        {
            best_out     = o;
            best_out_pri = pri;
            best_out_sec = sec;
        }
    }

    if (!best_out) return;

    server->focused_output = best_out;

    /* Prefer the last-focused window on the target output; fall back to the
     * direction-appropriate end: last window when entering from the right/bottom,
     * first when entering from the left/top. */
    int best_ws = best_out->active_workspace;
    nnwm_toplevel *hist = best_out->last_focused[best_ws];
    if (hist && hist->output == best_out && hist->workspace == best_ws
        && !hist->floating)
        ; /* use it — fullscreen windows are valid focus targets */
    else
        hist = nullptr;

    nnwm_toplevel *next = hist
        ? hist
        : ((is_left || is_up) ? ws_last(server, best_out)
                               : ws_first(server, best_out));

    /* ws_first/ws_last skip fullscreen windows (WS_TILED macro); find one
     * explicitly if nothing else was returned. */
    if (!next)
    {
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->output == best_out && tl->workspace == best_ws
                && (tl->fullscreen || tl->fake_fullscreen))
            {
                next = tl;
                break;
            }
        }
    }

    if (next)
        focus_toplevel(next);
    else
    {
        wlr_seat_keyboard_clear_focus(server->seat);
        unfocus_all_borders(server);
    }
    warp_cursor_to_output(server, best_out);
}

void
nnwm::focus::move_dir(nnwm_server *server, const char *direction)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;

    bool is_left  = strcmp(direction, "left")  == 0;
    bool is_right = strcmp(direction, "right") == 0;
    bool is_up    = strcmp(direction, "up")    == 0;
    bool is_down  = strcmp(direction, "down")  == 0;
    if (!is_left && !is_right && !is_up && !is_down) return;

    nnwm_toplevel *focused = get_focused_toplevel(server);
    if (!focused || focused->floating) return;

    /* ---- Overview mode: move window to adjacent workspace slot in the grid ---- */
    if (out->overview)
    {
        int cur = focused->workspace;
        int cur_col = cur % OV_COLS;
        int cur_row = cur / OV_COLS;
        int target = -1;

        int num_ws = server->config->workspace_count;
        if (is_left  && cur_col > 0)                  target = cur - 1;
        if (is_right && cur_col < OV_COLS - 1
                     && cur + 1 < num_ws)              target = cur + 1;
        if (is_up    && cur_row > 0)                   target = cur - OV_COLS;
        if (is_down  && cur + OV_COLS < num_ws)        target = cur + OV_COLS;

        if (target < 0) return;

        int old_ws = focused->workspace;
        if (out->last_focused[old_ws] == focused) out->last_focused[old_ws] = nullptr;
        if (out->prev_focused[old_ws] == focused) out->prev_focused[old_ws] = nullptr;
        focused->workspace = target;
        ov_switch_ws(server, out, target);
        wlr_scene_node_set_enabled(&focused->scene_tree->node, true);
        focus_toplevel(focused);
        arrange_windows(server, out);
        return;
    }

    int ws = out->active_workspace;
    int fcx = focused->cur_x + focused->cur_w / 2;
    int fcy = focused->cur_y + focused->cur_h / 2;

    /* Find the nearest tiled window in the requested direction */
    nnwm_toplevel *best     = nullptr;
    int            best_pri = INT_MAX;
    int            best_sec = INT_MAX;

    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (tl == focused) continue;
        if (tl->output != out) continue;
        if (tl->workspace != ws && !tl->sticky) continue;
        if (tl->floating) continue;
        if (tl->in_scratchpad) continue;

        int cx = tl->cur_x + tl->cur_w / 2;
        int cy = tl->cur_y + tl->cur_h / 2;

        int pri, sec;
        bool valid = false;

        if (is_left  && cx < fcx) { pri = fcx - cx; sec = abs(cy - fcy); valid = true; }
        if (is_right && cx > fcx) { pri = cx - fcx; sec = abs(cy - fcy); valid = true; }
        if (is_up    && cy < fcy) { pri = fcy - cy; sec = abs(cx - fcx); valid = true; }
        if (is_down  && cy > fcy) { pri = cy - fcy; sec = abs(cx - fcx); valid = true; }

        if (valid && (pri < best_pri || (pri == best_pri && sec < best_sec)))
        {
            best     = tl;
            best_pri = pri;
            best_sec = sec;
        }
    }

    if (best)
    {
        /* Swap the two windows' positions in the global toplevel list */
        struct wl_list *fp = focused->link.prev;
        struct wl_list *bp = best->link.prev;
        if (fp == &best->link)
        {
            /* best is immediately before focused */
            wl_list_remove(&best->link);
            wl_list_insert(&focused->link, &best->link);
        }
        else if (bp == &focused->link)
        {
            /* focused is immediately before best */
            wl_list_remove(&focused->link);
            wl_list_insert(&best->link, &focused->link);
        }
        else
        {
            wl_list_remove(&focused->link);
            wl_list_remove(&best->link);
            wl_list_insert(fp, &best->link);
            wl_list_insert(bp, &focused->link);
        }
        arrange_windows(server, out);
        return;
    }

    /* No tiled window in that direction — move to the adjacent monitor */
    wlr_box cur_box;
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &cur_box);
    int cur_cx = cur_box.x + cur_box.width  / 2;
    int cur_cy = cur_box.y + cur_box.height / 2;

    nnwm_output *best_out     = nullptr;
    int          best_out_pri = INT_MAX;
    int          best_out_sec = INT_MAX;

    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link)
    {
        if (o == out) continue;
        wlr_box ob;
        wlr_output_layout_get_box(server->output_layout, o->wlr_output, &ob);
        int ocx = ob.x + ob.width  / 2;
        int ocy = ob.y + ob.height / 2;

        int pri, sec;
        bool valid = false;

        if (is_left  && ocx < cur_cx) { pri = cur_cx - ocx; sec = abs(ocy - cur_cy); valid = true; }
        if (is_right && ocx > cur_cx) { pri = ocx - cur_cx; sec = abs(ocy - cur_cy); valid = true; }
        if (is_up    && ocy < cur_cy) { pri = cur_cy - ocy; sec = abs(ocx - cur_cx); valid = true; }
        if (is_down  && ocy > cur_cy) { pri = ocy - cur_cy; sec = abs(ocx - cur_cx); valid = true; }

        if (valid && (pri < best_out_pri || (pri == best_out_pri && sec < best_out_sec)))
        {
            best_out     = o;
            best_out_pri = pri;
            best_out_sec = sec;
        }
    }

    if (!best_out) return;

    int old_ws = focused->workspace;
    int new_ws = best_out->active_workspace;

    if (out->last_focused[old_ws] == focused)
        out->last_focused[old_ws] = nullptr;
    if (out->prev_focused[old_ws] == focused)
        out->prev_focused[old_ws] = nullptr;

    focused->output    = best_out;
    focused->workspace = new_ws;
    wlr_scene_node_set_enabled(&focused->scene_tree->node,
                               focused->output->active_workspace == focused->workspace);

    nnwm_toplevel *next = ws_first(server, out);
    if (next)
        focus_toplevel(next);
    else
        wlr_seat_keyboard_clear_focus(server->seat);

    server->focused_output = best_out;
    focus_toplevel(focused);

    arrange_windows(server, out);
    arrange_windows(server, best_out);
}

static void
move_to_monitor(nnwm_server *server, int dir)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl)
        return;
    nnwm_output *src = tl->output;
    if (!src)
        return;
    nnwm_output *dst = output_cycle(server, src, dir);
    if (!dst || dst == src)
        return;

    int old_ws = tl->workspace;
    int new_ws = dst->active_workspace;

    if (src->last_focused[old_ws] == tl)
        src->last_focused[old_ws] = nullptr;
    if (src->prev_focused[old_ws] == tl)
        src->prev_focused[old_ws] = nullptr;

    tl->output    = dst;
    tl->workspace = new_ws;
    wlr_scene_node_set_enabled(&tl->scene_tree->node,
                               tl->output->active_workspace == tl->workspace);

    nnwm_toplevel *next = ws_first(server, src);
    if (next)
        focus_toplevel(next);
    else
        wlr_seat_keyboard_clear_focus(server->seat);

    server->focused_output = dst;
    focus_toplevel(tl);

    arrange_windows(server, src);
    arrange_windows(server, dst);
}

void
nnwm::monitor::move_to_next(nnwm_server *server)
{
    move_to_monitor(server, +1);
}

void
nnwm::monitor::move_to_prev(nnwm_server *server)
{
    move_to_monitor(server, -1);
}

void
nnwm::layout::master_ratio_grow(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_config *cfg = server->config;
    int ws = out->active_workspace;
    out->master_ratio[ws] += cfg->layout.master_ratio_step;
    if (out->master_ratio[ws] > cfg->layout.master_ratio_max)
        out->master_ratio[ws] = cfg->layout.master_ratio_max;
    arrange_windows(server, out);
}

void
nnwm::layout::master_ratio_shrink(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    nnwm_config *cfg = server->config;
    int ws = out->active_workspace;
    out->master_ratio[ws] -= cfg->layout.master_ratio_step;
    if (out->master_ratio[ws] < cfg->layout.master_ratio_min)
        out->master_ratio[ws] = cfg->layout.master_ratio_min;
    arrange_windows(server, out);
}

void
nnwm::window::toggle_float(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl)
        return;

    bool was_floating = tl->floating;
    tl->floating      = !tl->floating;
    nnwm_output *out  = tl->output;

    if (tl->floating && out)
    {
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output,
                                  &area);
        int gw, gh;
#ifdef HAVE_XWAYLAND
        if (tl->is_xwayland) {
            int xww = (int)nnwm_xw_width(tl->xwayland_surface);
            int xwh = (int)nnwm_xw_height(tl->xwayland_surface);
            gw = xww > 0 ? xww : tl->cur_w;
            gh = xwh > 0 ? xwh : tl->cur_h;
        } else
#endif
        {
            wlr_box *geo = &tl->xdg_toplevel->base->geometry;
            gw = geo->width;
            gh = geo->height;
        }
        int x = area.x + (area.width - gw) / 2;
        int y = area.y + (area.height - gh) / 2;

#ifdef HAVE_SCENEFX
        nnwm_config *cfg = server->config;
        if (cfg->fx.animation.enabled && cfg->fx.animation.layout_duration_ms > 0
            && !(out && out->overview))
        {
            /* Animate from tiled slot to centered float position */
            int bw           = cfg->border.width;
            int to_w         = gw + 2 * bw;
            int to_h         = gh + 2 * bw;
            tl->geo_from_x   = tl->cur_x;
            tl->geo_from_y   = tl->cur_y;
            tl->geo_from_w   = tl->cur_w;
            tl->geo_from_h   = tl->cur_h;
            tl->geo_to_x     = x;
            tl->geo_to_y     = y;
            tl->geo_to_w     = to_w;
            tl->geo_to_h     = to_h;
            tl->geo_bw       = bw;
            tl->geo_then_hide = false;
            tl->geo_anim     = true;
            tl->geo_t0       = anim_now();
            tl->geo_duration_ms = eff_duration(cfg, cfg->fx.animation.layout_duration_ms);
            tl->geo_easing      = eff_easing(cfg, cfg->fx.animation.layout_easing);
            wlr_scene_node_set_position(&tl->scene_tree->node, tl->geo_from_x,
                                        tl->geo_from_y);
            update_borders(tl, tl->geo_from_w, tl->geo_from_h, bw);
        }
        else
#endif
        {
            wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
        }
    }

    arrange_windows(server, out);
}

void
nnwm::window::toggle_fullscreen(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (tl)
        do_toggle_fullscreen(tl);
}

void
nnwm::window::toggle_fake_fullscreen(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (tl)
        do_toggle_fake_fullscreen(tl);
}

void
nnwm::window::toggle_maximize(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl)
        return;
    tl->maximize = !tl->maximize;
    ftl_set_maximized(tl, tl->maximize);
    if (tl->output)
        arrange_windows(server, tl->output);
}

void
nnwm::window::toggle_sticky(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl)
        return;
    tl->sticky = !tl->sticky;
    arrange_windows(server, tl->output);
}

/* ---- Scratchpad ---- */

void
nnwm::scratchpad::move_to(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl || tl->in_scratchpad)
        return;

    nnwm_output *out = tl->output;

    /* Clear last-focused tracking */
    if (out)
    {
        if (out->last_focused[tl->workspace] == tl)
            out->last_focused[tl->workspace] = nullptr;
        if (out->prev_focused[tl->workspace] == tl)
            out->prev_focused[tl->workspace] = nullptr;
    }

    tl->in_scratchpad = true;
    tl->floating      = false;

    /* Reparent scene tree to scratchpad tree */
    wlr_scene_node_reparent(&tl->scene_tree->node, server->scene_scratchpad);
    wlr_scene_node_set_enabled(&tl->scene_tree->node, server->scratchpad_visible);

    /* Focus next window on current workspace */
    if (out)
    {
        nnwm_toplevel *next = out->last_focused[out->active_workspace];
        if (!next)
            next = ws_first(server, out);
        if (next)
            focus_toplevel(next);
        else
            wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, out);
    }

    if (server->scratchpad_visible)
        arrange_scratchpad(server);
}

void
nnwm::scratchpad::toggle(nnwm_server *server)
{
    server->scratchpad_visible = !server->scratchpad_visible;

    nnwm_output *out = server->focused_output;
    if (!out && !wl_list_empty(&server->outputs))
        out = wl_container_of(server->outputs.next, out, link);

    if (server->scratchpad_visible)
    {
        /* Show: enable scene nodes and arrange scratchpad windows */
        wlr_scene_node_set_enabled(&server->scene_scratch_dim->node, true);
        wlr_scene_node_set_enabled(&server->scene_scratchpad->node, true);

        /* Enable global scratchpad window nodes (not named scratchpad windows) */
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->in_scratchpad && tl->scratchpad_name.empty())
                wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
        }

        arrange_scratchpad(server);

        /* Focus first global scratchpad window */
        nnwm_toplevel *first = nullptr;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->in_scratchpad && tl->scratchpad_name.empty())
            {
                first = tl;
                break;
            }
        }
        if (first)
            focus_toplevel(first);
    }
    else
    {
        /* Hide: disable scene nodes */
        wlr_scene_node_set_enabled(&server->scene_scratch_dim->node, false);
        wlr_scene_node_set_enabled(&server->scene_scratchpad->node, false);

        /* Re-focus last active window on focused output */
        if (out)
        {
            nnwm_toplevel *next = out->last_focused[out->active_workspace];
            if (!next)
                next = ws_first(server, out);
            if (next)
                focus_toplevel(next);
            else
                wlr_seat_keyboard_clear_focus(server->seat);
        }
    }
}

/* ---- Named Scratchpad ---- */

void
nnwm::named_scratchpad::move_to(nnwm_server *server, const char *name)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl || tl->in_scratchpad)
        return;

    nnwm_named_scratchpad *nsp = get_or_create_named_scratchpad(server, name);
    nnwm_output *out = tl->output;

    if (out)
    {
        if (out->last_focused[tl->workspace] == tl)
            out->last_focused[tl->workspace] = nullptr;
        if (out->prev_focused[tl->workspace] == tl)
            out->prev_focused[tl->workspace] = nullptr;
    }

    tl->in_scratchpad    = true;
    tl->scratchpad_name  = name;
    tl->floating         = false;

    wlr_scene_node_reparent(&tl->scene_tree->node, nsp->scene_tree);
    wlr_scene_node_set_enabled(&tl->scene_tree->node, nsp->visible);

    if (out)
    {
        nnwm_toplevel *next = out->last_focused[out->active_workspace];
        if (!next)
            next = ws_first(server, out);
        if (next)
            focus_toplevel(next);
        else
            wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, out);
    }

    if (nsp->visible)
        arrange_named_scratchpad(server, nsp);
}

void
nnwm::named_scratchpad::toggle(nnwm_server *server, const char *name)
{
    nnwm_named_scratchpad *nsp = get_or_create_named_scratchpad(server, name);
    nsp->visible = !nsp->visible;

    nnwm_output *out = server->focused_output;
    if (!out && !wl_list_empty(&server->outputs))
        out = wl_container_of(server->outputs.next, out, link);

    if (nsp->visible)
    {
        wlr_scene_node_set_enabled(&nsp->dim_rect->node, true);
        wlr_scene_node_set_enabled(&nsp->scene_tree->node, true);

        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->in_scratchpad && tl->scratchpad_name == name)
                wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
        }

        arrange_named_scratchpad(server, nsp);

        nnwm_toplevel *first = named_scratch_first(server, name);
        if (first)
            focus_toplevel(first);
    }
    else
    {
        wlr_scene_node_set_enabled(&nsp->dim_rect->node, false);
        wlr_scene_node_set_enabled(&nsp->scene_tree->node, false);

        if (out)
        {
            nnwm_toplevel *next = out->last_focused[out->active_workspace];
            if (!next)
                next = ws_first(server, out);
            if (next)
                focus_toplevel(next);
            else
                wlr_seat_keyboard_clear_focus(server->seat);
        }
    }
}

void
nnwm::layout::toggle_vertical_tile(nnwm_server *server)
{
    if (server->scratchpad_visible)
    {
        server->scratchpad_layout
            = (server->scratchpad_layout == nnwm_layout_mode::VTILE)
                  ? nnwm_layout_mode::HTILE
                  : nnwm_layout_mode::VTILE;
        arrange_scratchpad(server);
        return;
    }
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws               = out->active_workspace;
    out->layout_mode[ws] = (out->layout_mode[ws] == nnwm_layout_mode::VTILE)
                               ? nnwm_layout_mode::HTILE
                               : nnwm_layout_mode::VTILE;
    arrange_windows(server, out);
}

void
nnwm::layout::toggle_tabbed(nnwm_server *server)
{
    if (server->scratchpad_visible)
        return;
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws               = out->active_workspace;
    out->layout_mode[ws] = (out->layout_mode[ws] == nnwm_layout_mode::TABBED)
                               ? out->layout_mode[ws]
                               : nnwm_layout_mode::TABBED;
    arrange_windows(server, out);
}

void
nnwm::layout::toggle_horizontal_scroll(nnwm_server *server)
{
    if (server->scratchpad_visible)
        return;
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws               = out->active_workspace;
    out->layout_mode[ws] = (out->layout_mode[ws] == nnwm_layout_mode::HSCROLL)
                               ? out->layout_mode[ws]
                               : nnwm_layout_mode::HSCROLL;
    arrange_windows(server, out);
}

void
nnwm::layout::toggle_vertical_scroll(nnwm_server *server)
{
    if (server->scratchpad_visible)
        return;
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws               = out->active_workspace;
    out->layout_mode[ws] = (out->layout_mode[ws] == nnwm_layout_mode::VSCROLL)
                               ? out->layout_mode[ws]
                               : nnwm_layout_mode::VSCROLL;
    arrange_windows(server, out);
}

void
nnwm::layout::toggle_float_layout(nnwm_server *server)
{
    if (server->scratchpad_visible)
        return;
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws = out->active_workspace;
    out->layout_mode[ws] = (out->layout_mode[ws] == nnwm_layout_mode::FLOAT)
                               ? nnwm_layout_mode::HTILE
                               : nnwm_layout_mode::FLOAT;
    arrange_windows(server, out);
}

void
nnwm::layout::set_layout(nnwm_server *server, nnwm_layout_mode mode)
{
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws = out->active_workspace;
    out->layout_mode[ws] = mode;
    arrange_windows(server, out);
}

void
nnwm::layout::next(nnwm_server *server)
{
    if (server->scratchpad_visible)
    {
        server->scratchpad_layout
            = (server->scratchpad_layout == nnwm_layout_mode::HTILE)
                  ? nnwm_layout_mode::VTILE
                  : nnwm_layout_mode::HTILE;
        arrange_scratchpad(server);
        return;
    }
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws               = out->active_workspace;
    out->layout_mode[ws] = static_cast<nnwm_layout_mode>(
        ((int)(out->layout_mode[ws]) + 1) % (int)(nnwm_layout_mode::COUNT));
    arrange_windows(server, out);
}

void
nnwm::layout::prev(nnwm_server *server)
{
    if (server->scratchpad_visible)
    {
        server->scratchpad_layout
            = (server->scratchpad_layout == nnwm_layout_mode::HTILE)
                  ? nnwm_layout_mode::VTILE
                  : nnwm_layout_mode::HTILE;
        arrange_scratchpad(server);
        return;
    }
    nnwm_output *out = server->focused_output;
    if (!out)
        return;
    int ws               = out->active_workspace;
    out->layout_mode[ws] = static_cast<nnwm_layout_mode>(
        ((int)(out->layout_mode[ws]) + int(nnwm_layout_mode::COUNT) - 1)
        % (int)(nnwm_layout_mode::COUNT));
    arrange_windows(server, out);
}

void
nnwm::workspace::move_to(nnwm_server *server, int ws)
{
    if (ws < 0 || ws >= server->config->workspace_count)
        return;
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl)
        return;

    nnwm_output *out = tl->output;

    /* Pull out of scratchpad if needed */
    if (tl->in_scratchpad)
    {
        tl->in_scratchpad = false;
        wlr_scene_node_reparent(&tl->scene_tree->node, server->scene_windows);
        if (server->scratchpad_visible)
            arrange_scratchpad(server);
    }
    else if (tl->workspace == ws)
        return;

    if (out && out->last_focused[tl->workspace] == tl)
        out->last_focused[tl->workspace] = nullptr;

    tl->workspace = ws;
    if (out)
        wlr_scene_node_set_enabled(&tl->scene_tree->node,
                                   out->active_workspace == ws);

    if (out)
    {
        nnwm_toplevel *next = out->last_focused[out->active_workspace];
        if (!next)
            next = ws_first(server, out);
        if (next)
            focus_toplevel(next);
        else
            wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, out);
    }
}

/* ---- Keybinding dispatch (delegates to Lua) ---- */

bool
handle_keybinding(nnwm_server *server, uint32_t modifiers, xkb_keysym_t sym)
{
#define MODS_MASK                                                              \
    (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT                 \
     | WLR_MODIFIER_CTRL)

    uint32_t mods = modifiers & MODS_MASK;
    return nnwm::lua_handle_keybinding(server, mods, (unsigned int)sym);

#undef MODS_MASK
}

/* ---- Overview ---- */

static void
begin_exit_overview(nnwm_server *server, nnwm_output *out)
{
#ifdef HAVE_SCENEFX
    if (server->config->fx.animation.enabled && !out->ov_anim_exiting) {
        nnwm_config *cfg = server->config;
        out->ov_anim           = true;
        out->ov_anim_t0        = anim_now();
        out->ov_anim_duration_ms = eff_duration(cfg, cfg->fx.animation.close_duration_ms);
        out->ov_anim_exiting   = true;
        return;
    }
#endif
    exit_overview(server, out);
}

void
nnwm::toggle_overview(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;

    if (out->overview) {
        begin_exit_overview(server, out);
    } else {
        out->overview = true;
        render_overview(server, out);
#ifdef HAVE_SCENEFX
        if (server->config->fx.animation.enabled) {
            nnwm_config *cfg = server->config;
            wlr_scene_buffer_set_opacity(out->overview_buf, 0.0f);
            wlr_scene_buffer_set_opacity(out->overview_labels, 0.0f);
            out->ov_anim             = true;
            out->ov_anim_t0          = anim_now();
            out->ov_anim_duration_ms = eff_duration(cfg, cfg->fx.animation.open_duration_ms);
            out->ov_anim_exiting     = false;
        }
#endif
    }
}

/* ---- Keyboard event handling ---- */

void
keyboard_handle_key(wl_listener *listener, void *data)
{
    nnwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    nnwm_server *server     = keyboard->server;
    auto *event             = static_cast<wlr_keyboard_key_event *>(data);
    wlr_seat *seat          = server->seat;

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
                                       keycode, &syms);

    bool handled       = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED)
        wl_event_source_timer_update(keyboard->repeat_timer, 0);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        for (int i = 0; i < nsyms; i++)
        {
            if (syms[i] >= XKB_KEY_XF86Switch_VT_1
                && syms[i] <= XKB_KEY_XF86Switch_VT_12)
            {
                if (server->session)
                    wlr_session_change_vt(
                        server->session, syms[i] - XKB_KEY_XF86Switch_VT_1 + 1);
                handled = true;
            }
        }
    }

    /* In overview mode, Escape goes back to the focused workspace and exits */
    if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED
        && server->focused_output && server->focused_output->overview)
    {
        for (int i = 0; i < nsyms; i++)
        {
            if (syms[i] == XKB_KEY_Escape)
            {
                begin_exit_overview(server, server->focused_output);
                handled = true;
                break;
            }
        }
    }

    if (!handled && !server->session_lock
        && (modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
        && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        xkb_layout_index_t layout = xkb_state_key_get_layout(
            keyboard->wlr_keyboard->xkb_state, keycode);
        const xkb_keysym_t *base_syms;
        int n_base = xkb_keymap_key_get_syms_by_level(
            keyboard->wlr_keyboard->keymap, keycode, layout, 0, &base_syms);
        for (int i = 0; i < n_base; i++)
            handled
                = handle_keybinding(server, modifiers, base_syms[i]) || handled;
        if (handled && n_base > 0)
        {
            nnwm_config *cfg         = server->config;
            keyboard->repeat_modifiers = modifiers;
            keyboard->repeat_sym       = base_syms[0];
            keyboard->repeat_started   = false;
            if (cfg->keyboard.repeat_delay > 0)
                wl_event_source_timer_update(keyboard->repeat_timer,
                                             cfg->keyboard.repeat_delay);
        }
    }

    if (!handled && !server->session_lock
        && !(modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
        && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        for (int i = 0; i < nsyms; i++)
            handled = handle_keybinding(server, modifiers, syms[i]) || handled;
        if (handled && nsyms > 0)
        {
            nnwm_config *cfg         = server->config;
            keyboard->repeat_modifiers = modifiers;
            keyboard->repeat_sym       = syms[0];
            keyboard->repeat_started   = false;
            if (cfg->keyboard.repeat_delay > 0)
                wl_event_source_timer_update(keyboard->repeat_timer,
                                             cfg->keyboard.repeat_delay);
        }
    }

    if (!handled)
    {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                     event->state);
    }

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED
        && server->config->mouse.hide_cursor_when_typing
        && !server->cursor_hidden_by_typing)
    {
        wlr_cursor_unset_image(server->cursor);
        server->cursor_hidden_by_typing = true;
    }
}

static int
keyboard_repeat_cb(void *data)
{
    nnwm_keyboard *keyboard = static_cast<nnwm_keyboard *>(data);
    nnwm_server *server     = keyboard->server;
    nnwm_config *cfg        = server->config;

    handle_keybinding(server, keyboard->repeat_modifiers, keyboard->repeat_sym);

    if (!keyboard->repeat_started)
    {
        keyboard->repeat_started = true;
        if (cfg->keyboard.repeat_rate > 0)
            wl_event_source_timer_update(keyboard->repeat_timer,
                                         1000 / cfg->keyboard.repeat_rate);
    }
    return 0;
}

void
keyboard_handle_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    if (keyboard->repeat_timer)
        wl_event_source_remove(keyboard->repeat_timer);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    delete keyboard;
}

/* ---- Keyboard / input device setup ---- */

void
apply_keymap(wlr_keyboard *wlr_keyboard, nnwm_config *cfg)
{
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap   = nullptr;

    if (cfg->keyboard.xkb_file && cfg->keyboard.xkb_file[0])
    {
        FILE *f = fopen(cfg->keyboard.xkb_file, "r");
        if (f)
        {
            keymap = xkb_keymap_new_from_file(context, f,
                         XKB_KEYMAP_FORMAT_TEXT_V1,
                         XKB_KEYMAP_COMPILE_NO_FLAGS);
            fclose(f);
            if (!keymap)
                std::fprintf(stderr,
                    "nnwm: failed to load XKB keymap from '%s', "
                    "falling back to rules\n", cfg->keyboard.xkb_file);
        }
        else
        {
            std::fprintf(stderr,
                "nnwm: cannot open XKB keymap file '%s': %s, "
                "falling back to rules\n",
                cfg->keyboard.xkb_file, strerror(errno));
        }
    }

    if (!keymap)
    {
        xkb_rule_names names = {};
        auto nonempty        = [](const char *s) -> const char * {
            return (s && s[0]) ? s : nullptr;
        };
        names.rules   = nonempty(cfg->keyboard.xkb_rules);
        names.layout  = nonempty(cfg->keyboard.xkb_layout);
        names.variant = nonempty(cfg->keyboard.xkb_variant);
        names.options = nonempty(cfg->keyboard.xkb_options);
        keymap = xkb_keymap_new_from_names(context, &names,
                     XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(wlr_keyboard, cfg->keyboard.repeat_rate,
                                 cfg->keyboard.repeat_delay);
}

void
server_new_keyboard(nnwm_server *server, wlr_input_device *device)
{
    nnwm_keyboard *keyboard = new nnwm_keyboard{};
    keyboard->server        = server;
    keyboard->wlr_keyboard  = wlr_keyboard_from_input_device(device);

    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    keyboard->repeat_timer = wl_event_loop_add_timer(loop, keyboard_repeat_cb,
                                                     keyboard);

    apply_keymap(keyboard->wlr_keyboard, server->config);

    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    /* Set up listeners */
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&keyboard->wlr_keyboard->events.modifiers,
                  &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&keyboard->wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wl_list_insert(&server->keyboards, &keyboard->link);
}

void
server_new_pointer(nnwm_server *server, wlr_input_device *device)
{
    if (wlr_input_device_is_libinput(device))
    {
        libinput_device *li     = wlr_libinput_get_device_handle(device);
        bool is_touchpad        = libinput_device_config_tap_get_finger_count(li) > 0;

        if (is_touchpad)
        {
            const auto &tp = server->config->touchpad;

            libinput_device_config_tap_set_enabled(
                li, tp.tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED
                                    : LIBINPUT_CONFIG_TAP_DISABLED);
            libinput_device_config_tap_set_drag_enabled(
                li, tp.drag ? LIBINPUT_CONFIG_DRAG_ENABLED
                            : LIBINPUT_CONFIG_DRAG_DISABLED);

            if (libinput_device_config_scroll_has_natural_scroll(li))
                libinput_device_config_scroll_set_natural_scroll_enabled(
                    li, tp.natural_scroll);

            if (libinput_device_config_dwt_is_available(li))
                libinput_device_config_dwt_set_enabled(
                    li, tp.disable_while_typing ? LIBINPUT_CONFIG_DWT_ENABLED
                                                : LIBINPUT_CONFIG_DWT_DISABLED);

            {
                uint32_t available
                    = libinput_device_config_send_events_get_modes(li);
                uint32_t mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
                if (!tp.enabled)
                    mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
                else if (tp.disable_on_external_mouse
                         && (available
                             & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE))
                    mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
                libinput_device_config_send_events_set_mode(li, mode);
            }

            {
                static const libinput_config_scroll_method methods[] = {
                    LIBINPUT_CONFIG_SCROLL_NO_SCROLL,
                    LIBINPUT_CONFIG_SCROLL_2FG,
                    LIBINPUT_CONFIG_SCROLL_EDGE,
                    LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN,
                };
                int idx = tp.scroll_method;
                if (idx >= 0 && idx <= 3)
                {
                    uint32_t available
                        = libinput_device_config_scroll_get_methods(li);
                    if (available & methods[idx])
                        libinput_device_config_scroll_set_method(li,
                                                                 methods[idx]);
                }
            }
        }
        else
        {
            const auto &m = server->config->mouse;

            if (libinput_device_config_accel_is_available(li))
            {
                static const libinput_config_accel_profile profiles[] = {
                    LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
                    LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT,
                    LIBINPUT_CONFIG_ACCEL_PROFILE_NONE,
                };
                int idx = m.accel_profile;
                if (idx >= 0 && idx <= 2)
                    libinput_device_config_accel_set_profile(li, profiles[idx]);
                libinput_device_config_accel_set_speed(li, m.accel_speed);
            }

            if (libinput_device_config_scroll_has_natural_scroll(li))
                libinput_device_config_scroll_set_natural_scroll_enabled(
                    li, m.natural_scroll);

            if (libinput_device_config_dwt_is_available(li))
                libinput_device_config_dwt_set_enabled(
                    li, m.disable_while_typing ? LIBINPUT_CONFIG_DWT_ENABLED
                                              : LIBINPUT_CONFIG_DWT_DISABLED);
        }
    }

    wlr_cursor_attach_input_device(server->cursor, device);
}
