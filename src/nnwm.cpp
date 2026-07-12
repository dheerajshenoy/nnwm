#include "nnwm.hpp"
#include <cassert>

namespace {

void
arrange_windows(nnwm_server *server)
{
    if (wl_list_empty(&server->outputs) || wl_list_empty(&server->toplevels))
        return;

    nnwm_output *first = wl_container_of(server->outputs.next, first, link);
    wlr_box area;
    wlr_output_layout_get_box(server->output_layout, first->wlr_output, &area);

    int n = wl_list_length(&server->toplevels);

    nnwm_toplevel *tl;
    if (n == 1) {
        tl = wl_container_of(server->toplevels.next, tl, link);
        wlr_scene_node_set_position(&tl->scene_tree->node, area.x, area.y);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, area.width, area.height);
        return;
    }

    int mw = (int)(area.width * server->master_ratio);
    int sw = area.width - mw;
    int ns = n - 1; /* number of stack windows */
    int sh = area.height / ns;

    int i = 0;
    wl_list_for_each(tl, &server->toplevels, link) {
        if (i == 0) {
            /* master: left column, full height */
            wlr_scene_node_set_position(&tl->scene_tree->node, area.x, area.y);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, mw, area.height);
        } else {
            /* stack: right column, evenly divided rows */
            int sy = area.y + (i - 1) * sh;
            int h  = (i < ns) ? sh : area.height - (i - 1) * sh; /* last gets remainder */
            wlr_scene_node_set_position(&tl->scene_tree->node, area.x + mw, sy);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, sw, h);
        }
        ++i;
    }
}

void
focus_toplevel(nnwm_toplevel *toplevel)
{
    /* Note: this function only deals with keyboard focus. */
    if (toplevel == nullptr)
    {
        return;
    }
    nnwm_server *server        = toplevel->server;
    wlr_seat    *seat          = server->seat;
    wlr_surface *prev_surface  = seat->keyboard_state.focused_surface;
    wlr_surface *surface       = toplevel->xdg_toplevel->base->surface;
    if (prev_surface == surface)
    {
        /* Don't re-focus an already focused surface. */
        return;
    }
    if (prev_surface)
    {
        /*
         * Deactivate the previously focused surface. This lets the client know
         * it no longer has focus and the client will repaint accordingly, e.g.
         * stop displaying a caret.
         */
        wlr_xdg_toplevel *prev_toplevel
            = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != nullptr)
        {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }
    wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    /* Move the toplevel to the front */
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);
    /* Activate the new surface */
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    /*
     * Tell the seat to have the keyboard enter this surface. wlroots will keep
     * track of this and automatically send key events to the appropriate
     * clients without additional work on your part.
     */
    if (keyboard != nullptr)
    {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                       keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }

    arrange_windows(server);
}

void
keyboard_handle_modifiers(wl_listener *listener, void * /*data*/)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    nnwm_keyboard *keyboard
        = wl_container_of(listener, keyboard, modifiers);
    /*
     * A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same seat. You can swap out the underlying wlr_keyboard like this and
     * wlr_seat handles this transparently.
     */
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                       &keyboard->wlr_keyboard->modifiers);
}

bool
handle_keybinding(nnwm_server *server, uint32_t modifiers,
                  xkb_keysym_t sym)
{
    /* Mask out NumLock (Mod2), CapsLock, and other state-only modifiers so
     * they don't interfere with binding matches. */
#define MODS_MASK   (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT | \
                     WLR_MODIFIER_ALT  | WLR_MODIFIER_CTRL)
#define SUPER       WLR_MODIFIER_LOGO
#define SUPER_SHIFT (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT)
#define ALT         WLR_MODIFIER_ALT

    uint32_t mods = modifiers & MODS_MASK;
    /* Normalize to lowercase so Shift doesn't change the keysym we match. */
    xkb_keysym_t key = xkb_keysym_to_lower(sym);

    /* Super+Shift+C: quit compositor */
    if (mods == SUPER_SHIFT && key == XKB_KEY_c)
    {
        wl_display_terminate(server->wl_display);
        return true;
    }

    /* Super+Shift+Q: close focused window */
    if (mods == SUPER_SHIFT && key == XKB_KEY_q)
    {
        if (!wl_list_empty(&server->toplevels))
        {
            nnwm_toplevel *focused =
                wl_container_of(server->toplevels.next, focused, link);
            wlr_xdg_toplevel_send_close(focused->xdg_toplevel);
        }
        return true;
    }

    /* Super+P: launch rofi */
    if (mods == SUPER && key == XKB_KEY_p)
    {
        if (fork() == 0)
            execl("/bin/sh", "/bin/sh", "-c", "rofi -show drun", static_cast<char*>(nullptr));
        return true;
    }

    /* Super+J: focus next window (moves it to master) */
    if (mods == SUPER && key == XKB_KEY_j)
    {
        if (wl_list_length(&server->toplevels) < 2)
            return true;
        nnwm_toplevel *cur  = wl_container_of(server->toplevels.next, cur, link);
        wl_list       *next = cur->link.next == &server->toplevels
                                  ? server->toplevels.next->next
                                  : cur->link.next;
        nnwm_toplevel *tl   = wl_container_of(next, tl, link);
        focus_toplevel(tl);
        return true;
    }

    /* Super+K: focus previous window */
    if (mods == SUPER && key == XKB_KEY_k)
    {
        if (wl_list_length(&server->toplevels) < 2)
            return true;
        nnwm_toplevel *tl = wl_container_of(server->toplevels.prev, tl, link);
        focus_toplevel(tl);
        return true;
    }

    /* Super+H/L: shrink / grow master area */
    if (mods == SUPER && key == XKB_KEY_h)
    {
        server->master_ratio -= 0.05f;
        if (server->master_ratio < 0.1f) server->master_ratio = 0.1f;
        arrange_windows(server);
        return true;
    }
    if (mods == SUPER && key == XKB_KEY_l)
    {
        server->master_ratio += 0.05f;
        if (server->master_ratio > 0.9f) server->master_ratio = 0.9f;
        arrange_windows(server);
        return true;
    }

    /* Alt+F1: cycle windows (legacy binding kept) */
    if (mods == ALT && sym == XKB_KEY_F1)
    {
        if (wl_list_length(&server->toplevels) < 2)
            return true;
        nnwm_toplevel *next_toplevel =
            wl_container_of(server->toplevels.prev, next_toplevel, link);
        focus_toplevel(next_toplevel);
        return true;
    }

#undef MODS_MASK
#undef SUPER
#undef SUPER_SHIFT
#undef ALT

    return false;
}

void
keyboard_handle_key(wl_listener *listener, void *data)
{
    /* This event is raised when a key is pressed or released. */
    nnwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    nnwm_server   *server   = keyboard->server;
    auto          *event    = static_cast<wlr_keyboard_key_event*>(data);
    wlr_seat      *seat     = server->seat;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
                                       keycode, &syms);

    bool     handled   = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    if ((modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
        && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        for (int i = 0; i < nsyms; i++)
        {
            handled = handle_keybinding(server, modifiers, syms[i]);
        }
    }

    if (!handled)
    {
        /* Otherwise, we pass it along to the client. */
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                     event->state);
    }
}

void
keyboard_handle_destroy(wl_listener *listener, void * /*data*/)
{
    /* This event is raised by the keyboard base wlr_input_device to signal
     * the destruction of the wlr_keyboard. It will no longer receive events
     * and should be destroyed.
     */
    nnwm_keyboard *keyboard
        = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    delete keyboard;
}

void
server_new_keyboard(nnwm_server *server, wlr_input_device *device)
{
    wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    nnwm_keyboard *keyboard = new nnwm_keyboard{};
    keyboard->server          = server;
    keyboard->wlr_keyboard    = wlr_keyboard;

    /* We need to prepare an XKB keymap and assign it to the keyboard. This
     * assumes the defaults (e.g. layout = "us"). */
    xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap  *keymap
        = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    /* Here we set up listeners for keyboard events. */
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    /* And add the keyboard to our list of keyboards */
    wl_list_insert(&server->keyboards, &keyboard->link);
}

void
server_new_pointer(nnwm_server *server, wlr_input_device *device)
{
    if (wlr_input_device_is_libinput(device)) {
        libinput_device *li = wlr_libinput_get_device_handle(device);

        /* Tap-to-click */
        if (libinput_device_config_tap_get_finger_count(li) > 0)
            libinput_device_config_tap_set_enabled(li, LIBINPUT_CONFIG_TAP_ENABLED);

        /* Natural scrolling */
        if (libinput_device_config_scroll_has_natural_scroll(li))
            libinput_device_config_scroll_set_natural_scroll_enabled(li, true);

        /* Disable touchpad while typing */
        if (libinput_device_config_dwt_is_available(li))
            libinput_device_config_dwt_set_enabled(li, LIBINPUT_CONFIG_DWT_ENABLED);
    }

    wlr_cursor_attach_input_device(server->cursor, device);
}

nnwm_toplevel *
desktop_toplevel_at(nnwm_server *server, double lx, double ly,
                    wlr_surface **surface, double *sx, double *sy)
{
    /* This returns the topmost node in the scene at the given layout coords.
     * We only care about surface nodes as we are specifically looking for a
     * surface in the surface tree of a nnwm_toplevel. */
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
    /* Find the node corresponding to the nnwm_toplevel at the root of this
     * surface tree, it is the only one for which we set the data field. */
    wlr_scene_tree *tree = node->parent;
    while (tree != nullptr && tree->node.data == nullptr)
    {
        tree = tree->node.parent;
    }
    if (tree == nullptr)
        return nullptr;  /* surface is over a layer shell or other non-toplevel node */
    return static_cast<nnwm_toplevel*>(tree->node.data);
}

void
reset_cursor_mode(nnwm_server *server)
{
    /* Reset the cursor mode to passthrough. */
    server->cursor_mode      = NNWM_CURSOR_PASSTHROUGH;
    server->grabbed_toplevel = nullptr;
}

void
process_cursor_move(nnwm_server *server)
{
    /* Move the grabbed toplevel to the new position. */
    nnwm_toplevel *toplevel = server->grabbed_toplevel;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                server->cursor->x - server->grab_x,
                                server->cursor->y - server->grab_y);
}

void
process_cursor_resize(nnwm_server *server)
{
    /*
     * Resizing the grabbed toplevel can be a little bit complicated, because we
     * could be resizing from any corner or edge. This not only resizes the
     * toplevel on one or two axes, but can also move the toplevel if you resize
     * from the top or left edges (or top-left corner).
     *
     * Note that some shortcuts are taken here. In a more fleshed-out
     * compositor, you'd wait for the client to prepare a buffer at the new
     * size, then commit any movement that was prepared.
     */
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
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

void
process_cursor_motion(nnwm_server *server, uint32_t time)
{
    /* If the mode is non-passthrough, delegate to those functions. */
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

    /* Otherwise, find the toplevel under the pointer and send the event along. */
    double sx, sy;
    wlr_seat    *seat    = server->seat;
    wlr_surface *surface = nullptr;
    nnwm_toplevel *toplevel = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (!toplevel)
    {
        /* If there's no toplevel under the cursor, set the cursor image to a
         * default. This is what makes the cursor image appear when you move it
         * around the screen, not over any toplevels. */
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
    if (surface)
    {
        /*
         * Send pointer enter and motion events.
         *
         * The enter event gives the surface "pointer focus", which is distinct
         * from keyboard focus. You get pointer focus by moving the pointer over
         * a window.
         *
         * Note that wlroots will avoid sending duplicate enter/motion events if
         * the surface has already has pointer focus or if the client is already
         * aware of the coordinates passed.
         */
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    }
    else
    {
        /* Clear pointer focus so future button events and such are not sent to
         * the last client to have the cursor over it. */
        wlr_seat_pointer_clear_focus(seat);
    }
}

void
output_frame(wl_listener *listener, void * /*data*/)
{
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    nnwm_output *output = wl_container_of(listener, output, frame);
    wlr_scene   *scene  = output->server->scene;

    wlr_scene_output *scene_output
        = wlr_scene_get_scene_output(scene, output->wlr_output);

    /* Render the scene if needed and commit the output */
    wlr_scene_output_commit(scene_output, nullptr);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void
output_request_state(wl_listener *listener, void *data)
{
    /* This function is called when the backend requests a new state for
     * the output. For example, Wayland and X11 backends request a new mode
     * when the output window is resized. */
    nnwm_output *output
        = wl_container_of(listener, output, request_state);
    const auto *event =
        static_cast<const wlr_output_event_request_state*>(data);
    wlr_output_commit_state(output->wlr_output, event->state);
}

void
output_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    delete output;
}

void
xdg_toplevel_map(wl_listener *listener, void * /*data*/)
{
    /* Called when the surface is mapped, or ready to display on-screen. */
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);

    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

    focus_toplevel(toplevel);
}

void
xdg_toplevel_unmap(wl_listener *listener, void * /*data*/)
{
    /* Called when the surface is unmapped, and should no longer be shown. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, unmap);

    /* Reset the cursor mode if the grabbed toplevel was unmapped. */
    if (toplevel == toplevel->server->grabbed_toplevel)
    {
        reset_cursor_mode(toplevel->server);
    }

    wl_list_remove(&toplevel->link);
    arrange_windows(toplevel->server);
}

void
decoration_set_csd(nnwm_decoration *deco)
{
    wlr_xdg_toplevel_decoration_v1_set_mode(
        deco->wlr_deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
}

void
decoration_handle_request_mode(wl_listener *listener, void * /*data*/)
{
    nnwm_decoration  *deco    = wl_container_of(listener, deco, request_mode);
    wlr_xdg_toplevel *xdg_tl = deco->wlr_deco->toplevel;

    if (xdg_tl->base->initialized) {
        decoration_set_csd(deco);
        return;
    }
    /* Surface not yet initialized — defer to xdg_toplevel_commit's
     * initial_commit handling where schedule_configure is safe to call. */
    auto *tree = static_cast<wlr_scene_tree*>(xdg_tl->base->data);
    if (tree && tree->node.data)
        static_cast<nnwm_toplevel*>(tree->node.data)->decoration = deco;
}

void
decoration_handle_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_decoration *deco = wl_container_of(listener, deco, destroy);
    wl_list_remove(&deco->request_mode.link);
    wl_list_remove(&deco->destroy.link);
    delete deco;
}

void
xdg_toplevel_commit(wl_listener *listener, void * /*data*/)
{
    /* Called when a new surface state is committed. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, commit);

    if (toplevel->xdg_toplevel->base->initial_commit)
    {
        /* When an xdg_surface performs an initial commit, the compositor must
         * reply with a configure so the client can map the surface. nnwm
         * configures the xdg_toplevel with 0,0 size to let the client pick the
         * dimensions itself. */
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
        /* Apply any decoration mode that was deferred because request_mode
         * arrived before the surface was initialized. */
        if (toplevel->decoration)
            decoration_set_csd(toplevel->decoration);
    }
}

void
handle_xdg_toplevel_destroy(wl_listener *listener, void * /*data*/)
{
    /* Called when the xdg_toplevel is destroyed. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, destroy);

    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);

    delete toplevel;
}

void
begin_interactive(nnwm_toplevel *toplevel,
                  nnwm_cursor_mode mode, uint32_t edges)
{
    /* This function sets up an interactive move or resize operation, where the
     * compositor stops propagating pointer events to clients and instead
     * consumes them itself, to move or resize windows. */
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

void
xdg_toplevel_request_move(wl_listener * /*listener*/, void * /*data*/)
{
    /* Tiling layout owns geometry — interactive move is disabled. */
}

void
xdg_toplevel_request_resize(wl_listener * /*listener*/, void * /*data*/)
{
    /* Tiling layout owns geometry — interactive resize is disabled. */
}

void
xdg_toplevel_request_maximize(wl_listener *listener, void * /*data*/)
{
    /* This event is raised when a client would like to maximize itself,
     * typically because the user clicked on the maximize button on client-side
     * decorations. nnwm doesn't support maximization, but to conform to
     * xdg-shell protocol we still must send a configure.
     * wlr_xdg_surface_schedule_configure() is used to send an empty reply.
     * However, if the request was sent before an initial commit, we don't do
     * anything and let the client finish the initial surface setup. */
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
    /* Just as with request_maximize, we must send a configure here. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_toplevel->base->initialized)
    {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

void
xdg_popup_commit(wl_listener *listener, void * /*data*/)
{
    /* Called when a new surface state is committed. */
    nnwm_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit)
    {
        /* When an xdg_surface performs an initial commit, the compositor must
         * reply with a configure so the client can map the surface.
         * nnwm sends an empty configure. A more sophisticated compositor
         * might change an xdg_popup's geometry to ensure it's not positioned
         * off-screen, for example. */
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

void
handle_xdg_popup_destroy(wl_listener *listener, void * /*data*/)
{
    /* Called when the xdg_popup is destroyed. */
    nnwm_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    delete popup;
}

/* ---- layer shell ---- */

void
arrange_layer_surface(nnwm_layer_surface *ls)
{
    wlr_output *output = ls->wlr_layer_surface->output;
    if (!output)
        return;

    wlr_box full_area;
    wlr_output_layout_get_box(ls->server->output_layout, output, &full_area);

    wlr_box usable = full_area;
    wlr_scene_layer_surface_v1_configure(ls->scene, &full_area, &usable);
}

void
layer_surface_map(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, map);
    arrange_layer_surface(ls);

    if (ls->wlr_layer_surface->current.keyboard_interactive !=
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
    {
        wlr_keyboard *kb = wlr_seat_get_keyboard(ls->server->seat);
        if (kb)
            wlr_seat_keyboard_notify_enter(ls->server->seat,
                ls->wlr_layer_surface->surface,
                kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
}

void
layer_surface_unmap(wl_listener * /*listener*/, void * /*data*/)
{
}

void
layer_surface_commit(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, commit);

    if (ls->wlr_layer_surface->current.committed)
        arrange_layer_surface(ls);

    if (ls->wlr_layer_surface->surface->mapped &&
        ls->wlr_layer_surface->current.keyboard_interactive !=
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
    {
        wlr_keyboard *kb = wlr_seat_get_keyboard(ls->server->seat);
        if (kb)
            wlr_seat_keyboard_notify_enter(ls->server->seat,
                ls->wlr_layer_surface->surface,
                kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
}

void
layer_surface_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    delete ls;
}

} // namespace

void
server_new_input(wl_listener *listener, void *data)
{
    /* This event is raised by the backend when a new input device becomes
     * available. */
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
    /* We need to let the wlr_seat know what our capabilities are, which is
     * communiciated to the client. In TinyWL we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
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
    /* This event is raised by the seat when a client provides a cursor image */
    auto *event =
        static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client *focused_client
        = server->seat->pointer_state.focused_client;
    /* This can be sent by any client, so we check to make sure this one is
     * actually has pointer focus first. */
    if (focused_client == event->seat_client)
    {
        /* Once we've vetted the client, we can tell the cursor to use the
         * provided surface as the cursor image. It will set the hardware cursor
         * on the output that it's currently on and continue to do so as the
         * cursor moves between outputs. */
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
                               event->hotspot_y);
    }
}

void
seat_pointer_focus_change(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, pointer_focus_change);
    /* This event is raised when the pointer focus is changed, including when
     * the client is closed. We set the cursor image to its default if target
     * surface is NULL */
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
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in nnwm we
     * always honor
     */
    nnwm_server *server
        = wl_container_of(listener, server, request_set_selection);
    auto *event =
        static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void
server_cursor_motion(wl_listener *listener, void *data)
{
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_motion);
    auto *event = static_cast<wlr_pointer_motion_event*>(data);
    /* The cursor doesn't move unless we tell it to. The cursor automatically
     * handles constraining the motion to the output layout, as well as any
     * special configuration applied for the specific input device which
     * generated the event. You can pass NULL for the device if you want to move
     * the cursor around without any input. */
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
                    event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

void
server_cursor_motion_absolute(wl_listener *listener, void *data)
{
    /* This event is forwarded by the cursor when a pointer emits an _absolute_
     * motion event, from 0..1 on each axis. This happens, for example, when
     * wlroots is running under a Wayland window rather than KMS+DRM, and you
     * move the mouse over the window. You could enter the window from any edge,
     * so we have to warp the mouse there. There is also some hardware which
     * emits these events. */
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
    /* This event is forwarded by the cursor when a pointer emits a button
     * event. */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_button);
    auto *event = static_cast<wlr_pointer_button_event*>(data);
    /* Notify the client with pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                   event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
    {
        /* If you released any buttons, we exit interactive move/resize mode. */
        reset_cursor_mode(server);
    }
    else
    {
        /* Focus that client if the button was _pressed_ */
        double sx, sy;
        wlr_surface   *surface  = nullptr;
        nnwm_toplevel *toplevel = desktop_toplevel_at(
            server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        focus_toplevel(toplevel);
    }
}

void
server_cursor_axis(wl_listener *listener, void *data)
{
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_axis);
    auto *event = static_cast<wlr_pointer_axis_event*>(data);
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(
        server->seat, event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source, event->relative_direction);
}

void
server_cursor_frame(wl_listener *listener, void * /*data*/)
{
    /* This event is forwarded by the cursor when a pointer emits an frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_frame);
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(server->seat);
}

void
server_new_output(wl_listener *listener, void *data)
{
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    nnwm_server *server
        = wl_container_of(listener, server, new_output);
    wlr_output *wlr_output = static_cast<struct wlr_output*>(data);

    /* Configures the output created by the backend to use our allocator
     * and our renderer. Must be done once, before committing the output */
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* The output may be disabled, switch it on. */
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    /* Some backends don't have modes. DRM+KMS does, and we need to set a mode
     * before we can use the output. The mode is a tuple of (width, height,
     * refresh rate), and each monitor supports only a specific set of modes. We
     * just pick the monitor's preferred mode, a more sophisticated compositor
     * would let the user configure it. */
    wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != nullptr)
    {
        wlr_output_state_set_mode(&state, mode);
    }

    /* Atomically applies the new output state. */
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    /* Allocates and configures our state for this output */
    nnwm_output *output = new nnwm_output{};
    output->wlr_output    = wlr_output;
    output->server        = server;

    /* Sets up a listener for the frame event. */
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    /* Sets up a listener for the state request event. */
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    /* Sets up a listener for the destroy event. */
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    /* Adds this to the output layout. The add_auto function arranges outputs
     * from left-to-right in the order they appear. A more sophisticated
     * compositor would let the user configure the arrangement of outputs in the
     * layout.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    wlr_output_layout_output *l_output
        = wlr_output_layout_add_auto(server->output_layout, wlr_output);
    wlr_scene_output *scene_output
        = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                       scene_output);
}

void
server_new_xdg_toplevel(wl_listener *listener, void *data)
{
    /* This event is raised when a client creates a new toplevel (application
     * window). */
    nnwm_server   *server      = wl_container_of(listener, server, new_xdg_toplevel);
    wlr_xdg_toplevel *xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    /* Allocate a nnwm_toplevel for this surface */
    nnwm_toplevel *toplevel = new nnwm_toplevel{};
    toplevel->server          = server;
    toplevel->xdg_toplevel    = xdg_toplevel;
    toplevel->scene_tree      = wlr_scene_xdg_surface_create(
        toplevel->server->scene_windows, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data        = toplevel->scene_tree;

    /* Listen to the various events it can emit */
    toplevel->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
    toplevel->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
    toplevel->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit,
                  &toplevel->commit);

    toplevel->destroy.notify = handle_xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    /* cotd */
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
    /* This event is raised when a client creates a new popup. */
    wlr_xdg_popup *xdg_popup = static_cast<wlr_xdg_popup*>(data);

    nnwm_popup *popup = new nnwm_popup{};
    popup->xdg_popup    = xdg_popup;

    /* We must add xdg popups to the scene graph so they get rendered. The
     * wlroots scene graph provides a helper for this, but to use it we must
     * provide the proper parent scene node of the xdg popup. To enable this,
     * we always set the user data field of xdg_surfaces to the corresponding
     * scene node. */
    wlr_xdg_surface *parent
        = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != nullptr);
    wlr_scene_tree *parent_tree =
        static_cast<wlr_scene_tree*>(parent->data);
    xdg_popup->base->data
        = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = handle_xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void
server_new_layer_surface(wl_listener *listener, void *data)
{
    nnwm_server        *server = wl_container_of(listener, server, new_layer_surface);
    wlr_layer_surface_v1 *wlr_ls =
        static_cast<wlr_layer_surface_v1*>(data);

    if (!wlr_ls->output && !wl_list_empty(&server->outputs))
    {
        nnwm_output *o = wl_container_of(server->outputs.next, o, link);
        wlr_ls->output = o->wlr_output;
    }

    nnwm_layer_surface *ls = new nnwm_layer_surface{};
    ls->server               = server;
    ls->wlr_layer_surface    = wlr_ls;
    ls->scene                = wlr_scene_layer_surface_v1_create(
        server->scene_layers[wlr_ls->pending.layer], wlr_ls);

    wlr_ls->data = ls;

    ls->map.notify     = layer_surface_map;
    ls->unmap.notify   = layer_surface_unmap;
    ls->commit.notify  = layer_surface_commit;
    ls->destroy.notify = layer_surface_destroy;

    wl_signal_add(&wlr_ls->surface->events.map,    &ls->map);
    wl_signal_add(&wlr_ls->surface->events.unmap,  &ls->unmap);
    wl_signal_add(&wlr_ls->surface->events.commit, &ls->commit);
    wl_signal_add(&wlr_ls->events.destroy,         &ls->destroy);
}

void
server_new_decoration(wl_listener * /*listener*/, void *data)
{
    /* A client requested decoration negotiation. We respond with CSD only
     * when the client later sends request_mode — at which point the surface
     * is initialized and wlr_xdg_surface_schedule_configure is safe to call.
     * Calling set_mode here (before the surface's first commit) would assert. */
    auto *wlr_deco = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);

    auto *deco          = new nnwm_decoration{};
    deco->wlr_deco      = wlr_deco;

    deco->request_mode.notify = decoration_handle_request_mode;
    wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);

    deco->destroy.notify = decoration_handle_destroy;
    wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
}
