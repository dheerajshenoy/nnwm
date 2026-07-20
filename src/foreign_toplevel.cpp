#include "nnwm.hpp"
#include "nnwm_internal.hpp"

extern "C" {
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
}

/* ---- Request handlers ---- */

static void
foreign_handle_request_activate(wl_listener *listener, void *data)
{
    nnwm_toplevel *tl = wl_container_of(listener, tl, foreign_request_activate);
    if (!tl->output) return;
    nnwm_server *server = tl->server;
    if (tl->workspace != tl->output->active_workspace)
        nnwm::workspace::switch_to(server, tl->workspace);
    focus_toplevel(tl);
}

static void
foreign_handle_request_close(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *tl = wl_container_of(listener, tl, foreign_request_close);
    tl_send_close(tl);
}

/* ---- Public helpers ---- */

void
ftl_map(nnwm_toplevel *tl)
{
    nnwm_server *server = tl->server;
    if (!server->foreign_toplevel_manager) return;

    tl->foreign_handle = wlr_foreign_toplevel_handle_v1_create(
        server->foreign_toplevel_manager);
    if (!tl->foreign_handle) return;

    const char *title  = tl_title(tl);
    const char *app_id = tl_app_id(tl);
    wlr_foreign_toplevel_handle_v1_set_title(tl->foreign_handle,
                                             title  ? title  : "");
    wlr_foreign_toplevel_handle_v1_set_app_id(tl->foreign_handle,
                                              app_id ? app_id : "");

    if (tl->output)
        wlr_foreign_toplevel_handle_v1_output_enter(tl->foreign_handle,
                                                    tl->output->wlr_output);

    wlr_foreign_toplevel_handle_v1_set_activated(tl->foreign_handle, true);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(tl->foreign_handle,
                                                  tl->fullscreen);
    wlr_foreign_toplevel_handle_v1_set_maximized(tl->foreign_handle,
                                                 tl->maximize);

    tl->foreign_request_activate.notify = foreign_handle_request_activate;
    wl_signal_add(&tl->foreign_handle->events.request_activate,
                  &tl->foreign_request_activate);

    tl->foreign_request_close.notify = foreign_handle_request_close;
    wl_signal_add(&tl->foreign_handle->events.request_close,
                  &tl->foreign_request_close);
}

void
ftl_unmap(nnwm_toplevel *tl)
{
    if (!tl->foreign_handle) return;
    wl_list_remove(&tl->foreign_request_activate.link);
    wl_list_remove(&tl->foreign_request_close.link);
    wlr_foreign_toplevel_handle_v1_destroy(tl->foreign_handle);
    tl->foreign_handle = nullptr;
}

void
ftl_set_title(nnwm_toplevel *tl)
{
    if (!tl->foreign_handle) return;
    const char *title = tl_title(tl);
    wlr_foreign_toplevel_handle_v1_set_title(tl->foreign_handle,
                                             title ? title : "");
}

void
ftl_set_activated(nnwm_toplevel *tl, bool activated)
{
    if (!tl->foreign_handle) return;
    wlr_foreign_toplevel_handle_v1_set_activated(tl->foreign_handle, activated);
}

void
ftl_set_fullscreen(nnwm_toplevel *tl, bool fullscreen)
{
    if (!tl->foreign_handle) return;
    wlr_foreign_toplevel_handle_v1_set_fullscreen(tl->foreign_handle, fullscreen);
}

void
ftl_set_maximized(nnwm_toplevel *tl, bool maximized)
{
    if (!tl->foreign_handle) return;
    wlr_foreign_toplevel_handle_v1_set_maximized(tl->foreign_handle, maximized);
}

void
ftl_update_output(nnwm_toplevel *tl, nnwm_output *old_out)
{
    if (!tl->foreign_handle) return;
    if (old_out && old_out != tl->output)
        wlr_foreign_toplevel_handle_v1_output_leave(tl->foreign_handle,
                                                    old_out->wlr_output);
    if (tl->output)
        wlr_foreign_toplevel_handle_v1_output_enter(tl->foreign_handle,
                                                    tl->output->wlr_output);
}
