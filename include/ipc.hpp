#ifndef NNWM_IPC_HPP
#define NNWM_IPC_HPP

#include <wayland-server-core.h>

struct nnwm_server;

/* Create the IPC unix socket and register it on the event loop.
 * Socket path: $XDG_RUNTIME_DIR/nnwm-ipc.sock (0600 perms).
 * Returns 0 on success, -1 on failure. */
int ipc_init(nnwm_server *server, struct wl_event_loop *loop);

/* Tear down the IPC socket. */
void ipc_fini(nnwm_server *server);

#endif
