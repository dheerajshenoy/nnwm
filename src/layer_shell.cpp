#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <ctime>

/* ---- layer shell ---- */

/* Recompute usable area for an output by processing all layer surfaces in
 * layer order.  Each surface's exclusive zone shrinks the usable area from
 * the anchored edge.  The result is stored on the output so arrange_windows
 * can pick it up, and arrange_windows is called to re-tile. */
void
arrange_layers(nnwm_server *server, wlr_output *output)
{
    nnwm_output *out = nullptr;
    {
        nnwm_output *o;
        wl_list_for_each(o, &server->outputs, link) {
            if (o->wlr_output == output) { out = o; break; }
        }
    }
    if (!out)
        return;

    wlr_box full_area;
    wlr_output_layout_get_box(server->output_layout, output, &full_area);
    wlr_box usable = full_area;

    for (int layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
         layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer++)
    {
        nnwm_layer_surface *ls;
        wl_list_for_each(ls, &server->layer_surfaces, link) {
            if (ls->wlr_layer_surface->output != output)
                continue;
            if ((int)ls->wlr_layer_surface->current.layer != layer)
                continue;
            wlr_scene_layer_surface_v1_configure(ls->scene, &full_area, &usable);
        }
    }

    out->usable_area = usable;
    arrange_windows(server, out);
}

void
layer_surface_map(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, map);
    arrange_layers(ls->server, ls->wlr_layer_surface->output);

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
layer_surface_unmap(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, unmap);
    arrange_layers(ls->server, ls->wlr_layer_surface->output);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(ls->server,
        (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
}

void
layer_surface_commit(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, commit);

    if (ls->wlr_layer_surface->current.committed)
        arrange_layers(ls->server, ls->wlr_layer_surface->output);

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
    nnwm_server *server = ls->server;
    wlr_output  *output = ls->wlr_layer_surface->output;
    wl_list_remove(&ls->link);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    delete ls;
    if (output)
        arrange_layers(server, output);
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
    wl_list_insert(&server->layer_surfaces, &ls->link);

    ls->map.notify     = layer_surface_map;
    ls->unmap.notify   = layer_surface_unmap;
    ls->commit.notify  = layer_surface_commit;
    ls->destroy.notify = layer_surface_destroy;

    wl_signal_add(&wlr_ls->surface->events.map,    &ls->map);
    wl_signal_add(&wlr_ls->surface->events.unmap,  &ls->unmap);
    wl_signal_add(&wlr_ls->surface->events.commit, &ls->commit);
    wl_signal_add(&wlr_ls->events.destroy,         &ls->destroy);
}
