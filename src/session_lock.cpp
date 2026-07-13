#include "nnwm.hpp"
#include "nnwm_internal.hpp"

/* ---- session lock ---- */

namespace {

void
lock_surface_map(wl_listener *listener, void * /*data*/)
{
    nnwm_lock_surface *ls = wl_container_of(listener, ls, map);
    nnwm_server       *server = ls->server;
    nnwm_session_lock *lock   = server->session_lock;
    if (!lock)
        return;

    /* Focus keyboard to this lock surface */
    wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(server->seat,
            ls->wlr_lock_surface->surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);

    /* Send locked once every output has a mapped lock surface */
    if (lock->wlr_lock->WLR_PRIVATE.locked_sent)
        return;
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        bool found = false;
        wlr_session_lock_surface_v1 *wls;
        wl_list_for_each(wls, &lock->wlr_lock->surfaces, link) {
            if (wls->output == out->wlr_output && wls->surface->mapped) {
                found = true;
                break;
            }
        }
        if (!found)
            return;
    }
    wlr_session_lock_v1_send_locked(lock->wlr_lock);
}

void
lock_surface_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_lock_surface *ls = wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->destroy.link);
    delete ls;
}

void
session_lock_new_surface(wl_listener *listener, void *data)
{
    nnwm_session_lock *lock = wl_container_of(listener, lock, new_surface);
    nnwm_server       *server = lock->server;
    auto *wls = static_cast<wlr_session_lock_surface_v1*>(data);

    /* Size the surface to the full output */
    wlr_box box;
    wlr_output_layout_get_box(server->output_layout, wls->output, &box);
    wlr_session_lock_surface_v1_configure(wls, box.width, box.height);

    auto *ls = new nnwm_lock_surface{};
    ls->server          = server;
    ls->wlr_lock_surface = wls;
    ls->scene_tree = wlr_scene_subsurface_tree_create(server->scene_locks, wls->surface);
    wlr_scene_node_set_position(&ls->scene_tree->node, box.x, box.y);
    wls->data = ls;

    ls->map.notify     = lock_surface_map;
    ls->destroy.notify = lock_surface_destroy;
    wl_signal_add(&wls->surface->events.map,  &ls->map);
    wl_signal_add(&wls->events.destroy,        &ls->destroy);
}

void
session_lock_unlock(wl_listener *listener, void * /*data*/)
{
    nnwm_session_lock *lock   = wl_container_of(listener, lock, unlock);
    nnwm_server       *server = lock->server;

    /* Hide all lock surfaces before destroying — prevents flicker */
    wlr_session_lock_surface_v1 *wls;
    wl_list_for_each(wls, &lock->wlr_lock->surfaces, link) {
        if (wls->data)
            wlr_scene_node_set_enabled(
                &static_cast<nnwm_lock_surface*>(wls->data)->scene_tree->node, false);
    }

    wlr_session_lock_v1_destroy(lock->wlr_lock);
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);
    delete lock;
    server->session_lock = nullptr;

    /* Restore keyboard focus to the previously focused toplevel */
    nnwm_output *out = server->focused_output;
    if (out) {
        int ws = out->active_workspace;
        nnwm_toplevel *tl = out->last_focused[ws];
        if (tl)
            focus_toplevel(tl);
    }
}

void
session_lock_destroy(wl_listener *listener, void * /*data*/)
{
    /* Called if the lock client crashes without sending unlock.
     * We keep the screen locked — do NOT clear session_lock here, as that
     * would leave the compositor in an unlocked state with no lock client. */
    nnwm_session_lock *lock = wl_container_of(listener, lock, destroy);
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);
    /* lock->server->session_lock stays non-null: screen remains locked */
    delete lock;
}

} /* anonymous namespace */

void
server_new_lock(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, new_lock);

    /* Reject a second lock while one is already active */
    if (server->session_lock) {
        wlr_session_lock_v1_destroy(static_cast<wlr_session_lock_v1*>(data));
        return;
    }

    auto *wlr_lock = static_cast<wlr_session_lock_v1*>(data);
    auto *lock     = new nnwm_session_lock{};
    lock->server   = server;
    lock->wlr_lock = wlr_lock;

    lock->new_surface.notify = session_lock_new_surface;
    lock->unlock.notify      = session_lock_unlock;
    lock->destroy.notify     = session_lock_destroy;
    wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);
    wl_signal_add(&wlr_lock->events.unlock,       &lock->unlock);
    wl_signal_add(&wlr_lock->events.destroy,      &lock->destroy);

    server->session_lock = lock;
}
