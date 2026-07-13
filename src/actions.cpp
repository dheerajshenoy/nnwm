#include "nnwm.hpp"
#include "nnwm_internal.hpp"
#include "lua/config.hpp"

#include <cstdio>
#include <cstring>
#include <linux/input-event-codes.h>

extern "C" {
#include <wlr/backend/libinput.h>
#include <libinput.h>
}

/* ---- Keyboard modifiers ---- */

void
keyboard_handle_modifiers(wl_listener *listener, void * /*data*/)
{
    nnwm_keyboard *keyboard
        = wl_container_of(listener, keyboard, modifiers);

    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                       &keyboard->wlr_keyboard->modifiers);
}

/* ---- Compositor actions (called from Lua keybinding callbacks) ---- */

void
do_toggle_fullscreen(nnwm_toplevel *tl)
{
    tl->fullscreen = !tl->fullscreen;
    wlr_xdg_toplevel_set_fullscreen(tl->xdg_toplevel, tl->fullscreen);

    nnwm_server *server = tl->server;
    nnwm_output *out    = tl->output;

    if (tl->fullscreen && out) {
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output, &area);
        wlr_scene_node_set_position(&tl->scene_tree->node, area.x, area.y);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, area.width, area.height);
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
    wl_list_for_each(t, &server->toplevels, link) {
        if (t->xdg_toplevel->base->surface == focused)
            return t;
    }
    return nullptr;
}

void
nnwm::action_quit(nnwm_server *server)
{
    wl_display_terminate(server->wl_display);
}

void
nnwm::action_close(nnwm_server *server)
{
    nnwm_toplevel *focused = get_focused_toplevel(server);
    if (focused)
        wlr_xdg_toplevel_send_close(focused->xdg_toplevel);
}

void
nnwm::action_spawn(nnwm_server *server, const char *cmd)
{
    if (!server->wayland_started) {
        if (server->autostart_count >= server->autostart_cap) {
            server->autostart_cap = server->autostart_cap ? server->autostart_cap * 2 : 8;
            server->autostart_cmds = static_cast<char**>(
                std::realloc(server->autostart_cmds,
                             sizeof(char*) * server->autostart_cap));
        }
        server->autostart_cmds[server->autostart_count++] = strdup(cmd);
        return;
    }
    if (fork() == 0)
        execl("/bin/sh", "/bin/sh", "-c", cmd, static_cast<char*>(nullptr));
}

void
nnwm::flush_autostart(nnwm_server *server)
{
    for (int i = 0; i < server->autostart_count; i++) {
        if (fork() == 0)
            execl("/bin/sh", "/bin/sh", "-c", server->autostart_cmds[i],
                  static_cast<char*>(nullptr));
        free(server->autostart_cmds[i]);
    }
    free(server->autostart_cmds);
    server->autostart_cmds  = nullptr;
    server->autostart_count = 0;
    server->autostart_cap   = 0;
}

void
nnwm::action_spawn_once(nnwm_server *server, const char *cmd)
{
    for (int i = 0; i < server->spawn_once_count; i++)
        if (strcmp(server->spawn_once_cmds[i], cmd) == 0)
            return;

    if (server->spawn_once_count >= server->spawn_once_cap) {
        server->spawn_once_cap = server->spawn_once_cap ? server->spawn_once_cap * 2 : 8;
        server->spawn_once_cmds = static_cast<char**>(
            realloc(server->spawn_once_cmds,
                    server->spawn_once_cap * sizeof(char*)));
    }
    server->spawn_once_cmds[server->spawn_once_count++] = strdup(cmd);
    nnwm::action_spawn(server, cmd);
}

void
nnwm::action_focus_left(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *tl = ws_first(server, out);
    if (tl)
        focus_toplevel(tl);
}

void
nnwm::action_focus_right(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (next)
        focus_toplevel(next);
}

void
nnwm::action_focus_next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (!next)
        next = ws_first(server, out);
    if (next)
        focus_toplevel(next);
}

void
nnwm::action_focus_prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *prev = ws_prev(server, out, cur);
    if (!prev)
        prev = ws_last(server, out);
    if (prev)
        focus_toplevel(prev);
}

void
nnwm::action_focus_mode_toggle(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (cur && cur->floating) {
        nnwm_toplevel *next = ws_first(server, out);
        if (next) focus_toplevel(next);
    } else {
        nnwm_toplevel *next = ws_first_float(server, out);
        if (next) focus_toplevel(next);
    }
}

void
nnwm::action_focus_next_float(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    nnwm_toplevel *next = cur ? ws_next_float(server, out, cur) : nullptr;
    if (!next)
        next = ws_first_float(server, out);
    if (next)
        focus_toplevel(next);
}

void
nnwm::action_focus_prev_float(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    nnwm_toplevel *prev = cur ? ws_prev_float(server, out, cur) : nullptr;
    if (!prev)
        prev = ws_last_float(server, out);
    if (prev)
        focus_toplevel(prev);
}

void
nnwm::action_swap_left(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur   = get_focused_toplevel(server);
    nnwm_toplevel *first = ws_first(server, out);
    if (!cur || cur == first)
        return;
    wl_list_remove(&cur->link);
    wl_list_insert(first->link.prev, &cur->link);
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_swap_right(nnwm_server *server)
{
    nnwm_output *out    = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur    = get_focused_toplevel(server);
    nnwm_toplevel *master = ws_first(server, out);
    if (!cur || !master)
        return;
    if (cur != master) {
        wl_list_remove(&cur->link);
        wl_list_insert(master->link.prev, &cur->link);
    } else {
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
nnwm::action_swap_next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (!next) return;
    wl_list_remove(&cur->link);
    wl_list_insert(&next->link, &cur->link);
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_swap_prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *prev = ws_prev(server, out, cur);
    if (!prev) return;
    wl_list_remove(&cur->link);
    wl_list_insert(prev->link.prev, &cur->link);
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_swap_master(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur    = get_focused_toplevel(server);
    nnwm_toplevel *master = ws_first(server, out);
    if (!cur || !master || cur == master) return;

    struct wl_list *before_cur = cur->link.prev;
    bool adjacent = (before_cur == &master->link);

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
nnwm::action_cycle(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out || ws_count(server, out) < 2) return;
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
nnwm::action_switch_workspace(nnwm_server *server, int ws)
{
    nnwm_output *out = server->focused_output;
    if (!out || ws < 0 || ws >= NNWM_NUM_WORKSPACES || ws == out->active_workspace)
        return;

    out->active_workspace = ws;

    /* Sync scene visibility for all toplevels on this output */
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
        wlr_scene_node_set_enabled(&tl->scene_tree->node,
                                   tl->output && tl->output->active_workspace == tl->workspace);

    nnwm_toplevel *next = out->last_focused[ws];
    if (!next)
        next = ws_first(server, out);
    if (next)
        focus_toplevel(next);
    else
        wlr_seat_keyboard_clear_focus(server->seat);

    arrange_windows(server, out);
    ext_workspace_notify(server);
}

void
nnwm::action_focus_monitor_next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_output *next = output_cycle(server, out, +1);
    if (!next) return;
    server->focused_output = next;
    int ws = next->active_workspace;
    nnwm_toplevel *tl = next->last_focused[ws];
    if (!tl) tl = ws_first(server, next);
    if (tl) focus_toplevel(tl);
}

void
nnwm::action_focus_monitor_prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_output *next = output_cycle(server, out, -1);
    if (!next) return;
    server->focused_output = next;
    int ws = next->active_workspace;
    nnwm_toplevel *tl = next->last_focused[ws];
    if (!tl) tl = ws_first(server, next);
    if (tl) focus_toplevel(tl);
}

static void
move_to_monitor(nnwm_server *server, int dir)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl) return;
    nnwm_output *src = tl->output;
    if (!src) return;
    nnwm_output *dst = output_cycle(server, src, dir);
    if (!dst) return;

    int old_ws = tl->workspace;
    int new_ws = dst->active_workspace;
    if (old_ws == new_ws) return;

    if (src->last_focused[old_ws] == tl) src->last_focused[old_ws] = nullptr;
    if (src->prev_focused[old_ws] == tl) src->prev_focused[old_ws] = nullptr;

    tl->output   = dst;
    tl->workspace = new_ws;
    wlr_scene_node_set_enabled(&tl->scene_tree->node,
                               tl->output->active_workspace == tl->workspace);

    nnwm_toplevel *next = src->prev_focused[old_ws];
    if (!next) next = src->last_focused[old_ws];
    if (!next) next = ws_first(server, src);
    if (next) focus_toplevel(next);
    else      wlr_seat_keyboard_clear_focus(server->seat);

    server->focused_output = dst;
    focus_toplevel(tl);

    arrange_windows(server, src);
    arrange_windows(server, dst);
}

void
nnwm::action_move_to_monitor_next(nnwm_server *server)
{
    move_to_monitor(server, +1);
}

void
nnwm::action_move_to_monitor_prev(nnwm_server *server)
{
    move_to_monitor(server, -1);
}

void
nnwm::action_master_ratio_grow(nnwm_server *server)
{
    nnwm_config *cfg = server->config;
    cfg->master_ratio += cfg->master_ratio_step;
    if (cfg->master_ratio > cfg->master_ratio_max)
        cfg->master_ratio = cfg->master_ratio_max;
    arrange_all_outputs(server);
}

void
nnwm::action_master_ratio_shrink(nnwm_server *server)
{
    nnwm_config *cfg = server->config;
    cfg->master_ratio -= cfg->master_ratio_step;
    if (cfg->master_ratio < cfg->master_ratio_min)
        cfg->master_ratio = cfg->master_ratio_min;
    arrange_all_outputs(server);
}

void
nnwm::action_toggle_float(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl)
        return;

    tl->floating = !tl->floating;
    nnwm_output *out = tl->output;

    if (tl->floating && out) {
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output, &area);
        wlr_box *geo = &tl->xdg_toplevel->base->geometry;
        int x = area.x + (area.width  - geo->width)  / 2;
        int y = area.y + (area.height - geo->height) / 2;
        wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
    }

    arrange_windows(server, out);
}

void
nnwm::action_toggle_fullscreen(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (tl)
        do_toggle_fullscreen(tl);
}

void
nnwm::action_move_to_workspace(nnwm_server *server, int ws)
{
    if (ws < 0 || ws >= NNWM_NUM_WORKSPACES)
        return;
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl || tl->workspace == ws)
        return;

    nnwm_output *out = tl->output;

    if (out && out->last_focused[tl->workspace] == tl)
        out->last_focused[tl->workspace] = nullptr;

    tl->workspace = ws;
    if (out)
        wlr_scene_node_set_enabled(&tl->scene_tree->node,
                                   out->active_workspace == ws);

    if (out) {
        nnwm_toplevel *next = out->last_focused[out->active_workspace];
        if (!next) next = ws_first(server, out);
        if (next) focus_toplevel(next);
        else      wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, out);
    }
}

/* ---- Keybinding dispatch (delegates to Lua) ---- */

bool
handle_keybinding(nnwm_server *server, uint32_t modifiers,
                  xkb_keysym_t sym)
{
#define MODS_MASK   (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT | \
                     WLR_MODIFIER_ALT  | WLR_MODIFIER_CTRL)

    uint32_t mods = modifiers & MODS_MASK;
    return nnwm::lua_handle_keybinding(server, mods, (unsigned int)sym);

#undef MODS_MASK
}

/* ---- Keyboard event handling ---- */

void
keyboard_handle_key(wl_listener *listener, void *data)
{
    nnwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    nnwm_server   *server   = keyboard->server;
    auto          *event    = static_cast<wlr_keyboard_key_event*>(data);
    wlr_seat      *seat     = server->seat;

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
                                       keycode, &syms);

    bool     handled   = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        for (int i = 0; i < nsyms; i++)
        {
            if (syms[i] >= XKB_KEY_XF86Switch_VT_1 &&
                syms[i] <= XKB_KEY_XF86Switch_VT_12)
            {
                if (server->session)
                    wlr_session_change_vt(server->session,
                        syms[i] - XKB_KEY_XF86Switch_VT_1 + 1);
                handled = true;
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
            handled = handle_keybinding(server, modifiers, base_syms[i]) || handled;
    }

    if (!handled && !server->session_lock
        && !(modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
        && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        for (int i = 0; i < nsyms; i++)
            handled = handle_keybinding(server, modifiers, syms[i]) || handled;
    }

    if (!handled)
    {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                     event->state);
    }
}

void
keyboard_handle_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_keyboard *keyboard
        = wl_container_of(listener, keyboard, destroy);
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
    struct xkb_keymap  *keymap  = xkb_keymap_new_from_names(
        context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (cfg->xkb_options && cfg->xkb_options[0])
    {
        /* Apply XKB options from config */
        xkb_rule_names names = {};
        names.options = cfg->xkb_options;
        keymap = xkb_keymap_new_from_names(context, &names,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(wlr_keyboard,
                                 cfg->keyboard_repeat_rate,
                                 cfg->keyboard_repeat_delay);
}

void
server_new_keyboard(nnwm_server *server, wlr_input_device *device)
{
    nnwm_keyboard *keyboard = new nnwm_keyboard{};
    keyboard->server = server;
    keyboard->wlr_keyboard
        = wlr_keyboard_from_input_device(device);

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
    if (wlr_input_device_is_libinput(device)) {
        libinput_device *li = wlr_libinput_get_device_handle(device);

        if (server->config->touchpad_tap_to_click
            && libinput_device_config_tap_get_finger_count(li) > 0)
            libinput_device_config_tap_set_enabled(li, LIBINPUT_CONFIG_TAP_ENABLED);

        if (server->config->touchpad_natural_scroll
            && libinput_device_config_scroll_has_natural_scroll(li))
            libinput_device_config_scroll_set_natural_scroll_enabled(li, true);

        if (server->config->touchpad_disable_while_typing
            && libinput_device_config_dwt_is_available(li))
            libinput_device_config_dwt_set_enabled(li, LIBINPUT_CONFIG_DWT_ENABLED);
    }

    wlr_cursor_attach_input_device(server->cursor, device);
}
