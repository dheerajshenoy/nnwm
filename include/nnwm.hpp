
#ifdef __cplusplus
extern "C"
{
#endif
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#ifdef __cplusplus
#  pragma push_macro("static")
#  define static
#  include <wlr/types/wlr_scene.h>
#  pragma pop_macro("static")
#else
#  include <wlr/types/wlr_scene.h>
#endif
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#ifdef __cplusplus
#  pragma push_macro("namespace")
#  define namespace namespace_
#  include <wlr/types/wlr_layer_shell_v1.h>
#  pragma pop_macro("namespace")
#else
#  include <wlr/types/wlr_layer_shell_v1.h>
#endif
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/backend/libinput.h>
#include <wlr/util/log.h>
#include <libinput.h>
#ifdef __cplusplus
}
#endif

#include "config.hpp"

struct nnwm_decoration; /* forward declaration — defined below nnwm_toplevel */

/* For brevity's sake, struct members are annotated where they are used. */
enum nnwm_cursor_mode
{
    NNWM_CURSOR_PASSTHROUGH,
    NNWM_CURSOR_MOVE,
    NNWM_CURSOR_RESIZE,
};

struct nnwm_server
{
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    /* Scene sub-trees ordered back-to-front for proper layering */
    struct wlr_scene_tree *scene_layers[4]; /* indexed by zwlr_layer_shell_v1_layer */
    struct wlr_scene_tree *scene_windows;   /* xdg toplevels live here */

    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

    struct wlr_xdg_decoration_manager_v1 *decoration_manager;
    struct wl_listener new_decoration;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener pointer_focus_change;
    struct wl_listener request_set_selection;
    struct wl_list keyboards;
    enum nnwm_cursor_mode cursor_mode;
    struct nnwm_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;

    struct nnwm_config *config;
    char *config_path;
    int config_inotify_fd;
    struct wl_event_source *config_event_source;
};

struct nnwm_output
{
    struct wl_list link;
    struct nnwm_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct nnwm_toplevel
{
    struct wl_list link;
    struct nnwm_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_rect *border[4];      /* top, bottom, left, right */
    struct wlr_scene_tree *scene_surface;
    struct nnwm_decoration *decoration; /* pending decoration, applied on initial commit */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

struct nnwm_popup
{
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct nnwm_keyboard
{
    struct wl_list link;
    struct nnwm_server *server;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct nnwm_decoration
{
    struct wlr_xdg_toplevel_decoration_v1 *wlr_deco;
    struct wl_listener request_mode;
    struct wl_listener destroy;
};

struct nnwm_layer_surface
{
    struct nnwm_server *server;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

#ifdef __cplusplus
extern "C"
{
#endif
void server_apply_config(struct nnwm_server *server);
void server_new_output(struct wl_listener *, void *);
void server_new_xdg_toplevel(struct wl_listener *, void *);
void server_new_xdg_popup(struct wl_listener *, void *);
void server_new_layer_surface(struct wl_listener *, void *);
void server_new_decoration(struct wl_listener *, void *);
void server_cursor_motion(struct wl_listener *, void *);
void server_cursor_motion_absolute(struct wl_listener *, void *);
void server_cursor_button(struct wl_listener *, void *);
void server_cursor_axis(struct wl_listener *, void *);
void server_cursor_frame(struct wl_listener *, void *);
void server_new_input(struct wl_listener *, void *);
void seat_request_cursor(struct wl_listener *, void *);
void seat_pointer_focus_change(struct wl_listener *, void *);
void seat_request_set_selection(struct wl_listener *, void *);
#ifdef __cplusplus
}
#endif
