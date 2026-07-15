
#ifndef NNWM_HPP
#define NNWM_HPP

#ifdef __cplusplus
extern "C"
{
#endif
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <wayland-server-core.h>
/* wlr/render/color.h uses C99 [static N] array syntax which is invalid C++.
 * Pre-include it with `static` suppressed so every transitive inclusion is a
 * no-op (include guard already fired). */
#ifdef __cplusplus
#  pragma push_macro("static")
#  define static
#  include <wlr/render/color.h>
#  pragma pop_macro("static")
#else
#  include <wlr/render/color.h>
#endif
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
#include <xkbcommon/xkbcommon.h>
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
#include <wlr/types/wlr_xdg_output_v1.h>
#ifdef __cplusplus
#  pragma push_macro("namespace")
#  define namespace namespace_
#  include <wlr/types/wlr_output_management_v1.h>
#  pragma pop_macro("namespace")
#else
#  include <wlr/types/wlr_output_management_v1.h>
#endif
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <libinput.h>
#ifdef __cplusplus
}
#endif

#ifdef HAVE_SCENEFX
#  ifdef __cplusplus
extern "C" {
#  endif
/* fx_renderer.h has no extern "C" guards — wrap it here */
#  include <scenefx/render/fx_renderer/fx_renderer.h>
/* clipped_region.h only uses wayland/wlroots primitives already included above */
#  include <scenefx/types/fx/clipped_region.h>

/* Forward-declare scenefx scene extension types and functions.
 * We cannot include scenefx/types/wlr_scene.h because it shares the include
 * guard WLR_TYPES_WLR_SCENE_H with wlr/types/wlr_scene.h (already included
 * above) and its own transitive dependencies are not present in the build. */
struct wlr_scene_shadow {
    struct wlr_scene_node node;
    int corner_radius;
};
struct wlr_scene_blur {
    struct wlr_scene_node node;
};
struct wlr_scene_shadow *wlr_scene_shadow_create(struct wlr_scene_tree *parent,
    int width, int height, int corner_radius, float blur_sigma,
    const float color[4]);
void wlr_scene_shadow_set_size(struct wlr_scene_shadow *shadow,
    int width, int height);
void wlr_scene_shadow_set_corner_radius(struct wlr_scene_shadow *shadow,
    int corner_radius);
void wlr_scene_shadow_set_blur_sigma(struct wlr_scene_shadow *shadow,
    float blur_sigma);
void wlr_scene_shadow_set_color(struct wlr_scene_shadow *shadow,
    const float color[4]);
void wlr_scene_rect_set_corner_radius(struct wlr_scene_rect *rect,
    int corner_radius);
void wlr_scene_rect_set_corner_radii(struct wlr_scene_rect *rect,
    struct fx_corner_radii corner_radii);
void wlr_scene_buffer_set_corner_radius(struct wlr_scene_buffer *scene_buffer,
    int corner_radius);
void wlr_scene_buffer_set_opacity(struct wlr_scene_buffer *scene_buffer,
    float opacity);
struct wlr_scene_blur *wlr_scene_blur_create(struct wlr_scene_tree *parent,
    int width, int height);
void wlr_scene_blur_set_size(struct wlr_scene_blur *blur, int width, int height);
void wlr_scene_blur_set_corner_radius(struct wlr_scene_blur *blur,
    int corner_radius);
void wlr_scene_set_blur_data(struct wlr_scene *scene, int num_passes,
    int radius, float noise, float brightness, float contrast, float saturation);
#  ifdef __cplusplus
}
#  endif
#endif

#include "config.hpp"

#define NNWM_NUM_WORKSPACES 9

enum nnwm_layout_mode {
    NNWM_LAYOUT_TILE,
    NNWM_LAYOUT_TABBED,
    NNWM_LAYOUT_SCROLL,
    NNWM_LAYOUT_COUNT,
};

struct lua_State;

struct nnwm_lua_keybinding
{
    uint32_t      mods;
    xkb_keysym_t  keysym;
    int           func_ref;   /* Lua registry reference */
};

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
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    /* Scene sub-trees ordered back-to-front for proper layering */
    struct wlr_scene_tree *scene_layers[4]; /* indexed by zwlr_layer_shell_v1_layer */
    struct wlr_scene_tree *scene_windows;   /* xdg toplevels live here */

    struct wl_list layer_surfaces; /* nnwm_layer_surface::link */

    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

    struct wlr_xdg_decoration_manager_v1 *decoration_manager;
    struct wl_listener new_decoration;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;
#ifdef HAVE_SCENEFX
    struct wl_list dying_toplevels;  /* toplevels fading out after unmap */
#endif

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

    struct wl_listener session_active; /* VT switch resume → re-tile all outputs */

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;

    struct wlr_output_manager_v1 *output_manager;
    struct wl_listener output_manager_apply;
    struct wl_listener output_manager_test;

    struct wlr_xdg_output_manager_v1 *xdg_output_manager;
    struct wlr_screencopy_manager_v1 *screencopy_manager;
    struct wlr_output_power_manager_v1 *output_power_manager;
    struct wl_listener output_power_set_mode;
    struct wlr_session_lock_manager_v1 *lock_manager;
    struct wl_listener new_lock;
    struct nnwm_session_lock *session_lock; /* non-null while screen is locked */
    struct wlr_scene_tree *scene_locks;     /* above overlay — lock surfaces live here */
    struct wlr_export_dmabuf_manager_v1 *export_dmabuf_manager;
    struct wlr_ext_image_copy_capture_manager_v1 *image_copy_capture_manager;
    struct wlr_ext_output_image_capture_source_manager_v1 *output_capture_source_manager;

    struct wl_global *ext_workspace_global;
    struct wl_list ext_workspace_managers; /* nnwm_ext_workspace_manager::link */

    /* Focused output — tracks which output keyboard actions operate on */
    struct nnwm_output *focused_output;

    /* Autostart: commands queued from config before WAYLAND_DISPLAY is ready */
    char   **autostart_cmds;
    int      autostart_count;
    int      autostart_cap;
    bool     wayland_started;

    struct nnwm_config *config;
    char *config_path;
    int config_inotify_fd;
    struct wl_event_source *config_event_source;
    struct wl_event_source *error_dismiss_timer; /* auto-hides config error bar */

    /* spawn_once: commands already launched this session */
    char **spawn_once_cmds;
    int    spawn_once_count;
    int    spawn_once_cap;

    /* Lua state for config and keybinding callbacks */
    struct lua_State *lua;
    struct nnwm_lua_keybinding *lua_keybindings;
    int lua_keybinding_count;
    int lua_keybinding_cap;
};

struct nnwm_output
{
    struct wl_list link;
    struct nnwm_server *server;
    struct wlr_output *wlr_output;
    struct wlr_box usable_area; /* output area minus exclusive-zone struts */
    int active_workspace;
    enum nnwm_layout_mode layout_mode[NNWM_NUM_WORKSPACES];
    int scroll_offset[NNWM_NUM_WORKSPACES]; /* horizontal scroll offset in pixels for NNWM_LAYOUT_SCROLL */
    struct wlr_scene_buffer *tab_bar;
    struct wlr_scene_buffer *error_bar;
    struct nnwm_toplevel *last_focused[NNWM_NUM_WORKSPACES];
    struct nnwm_toplevel *prev_focused[NNWM_NUM_WORKSPACES]; /* focused before last_focused */
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct nnwm_toplevel
{
    struct wl_list link;
    struct nnwm_server *server;
    struct nnwm_output *output;   /* owning output — which monitor this window lives on */
    int workspace;                /* workspace index within output (0..8) */
    bool floating;
    bool fullscreen;
    bool sticky;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_rect *border[4];      /* top, bottom, left, right */
    float rule_opacity;  /* per-window override: <0 = use global cfg->opacity */
    int   rule_blur;     /* per-window override: -1=global, 0=off, 1=on */
#ifdef HAVE_SCENEFX
    struct wlr_scene_rect *border_bg;    /* full-window rect behind content, for rounded corners */
    int   rule_anim_open_style;   /* -1=unset, else nnwm_open_style */
    int   rule_anim_close_style;  /* -1=unset, else nnwm_open_style */
    int   rule_no_anim;           /* -1=unset, 1=disable */
#endif /* HAVE_SCENEFX */
    struct wlr_scene_tree *scene_surface;
    struct nnwm_decoration *decoration; /* pending decoration, applied on initial commit */
    struct wlr_scene_buffer *titlebar;  /* server-side titlebar, nullptr if disabled */
    int                      titlebar_width; /* last rendered inner width (for title-change redraws) */
#ifdef HAVE_SCENEFX
    struct wlr_scene_shadow *fx_shadow; /* scenefx shadow node, nullptr if disabled */
    struct wlr_scene_blur  *fx_blur;    /* scenefx background blur node, nullptr if disabled */
#endif
    /* Current visual geometry — tracks what's actually rendered */
    int cur_x, cur_y, cur_w, cur_h;

#ifdef HAVE_SCENEFX
    /* Geometry animation (position + size tween) */
    bool   geo_anim;
    double geo_t0;
    int    geo_duration_ms;
    nnwm_easing geo_easing;
    int    geo_from_x, geo_from_y, geo_from_w, geo_from_h;
    int    geo_to_x,   geo_to_y,   geo_to_w,   geo_to_h;
    int    geo_bw;
    bool   geo_then_hide;

    /* Fade animation (open/close) */
    bool   fade_anim;
    double fade_t0;
    int    fade_duration_ms;
    nnwm_easing fade_easing;
    float  fade_from, fade_to;

    /* Border color animation (focus change) */
    bool   bcol_anim;
    double bcol_t0;
    int    bcol_duration_ms;
    nnwm_easing bcol_easing;
    float  bcol_from[4], bcol_to[4];

    /* Dying: removed from server->toplevels but still fading out */
    bool           dying;
    struct wl_list dying_link;
#endif /* HAVE_SCENEFX */

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener set_title;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

struct nnwm_popup
{
    struct wlr_xdg_popup  *xdg_popup;
    struct nnwm_server    *server;
    struct wlr_output     *output;       /* output for constraining */
    struct wlr_scene_tree *parent_tree;  /* parent surface scene tree for coord lookup */
    struct wlr_scene_tree *offset_tree;  /* intermediate tree at output origin for null-parent popups */
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct nnwm_session_lock
{
    struct nnwm_server *server;
    struct wlr_session_lock_v1 *wlr_lock;
    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
};

struct nnwm_lock_surface
{
    struct nnwm_server *server;
    struct wlr_session_lock_surface_v1 *wlr_lock_surface;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener map;
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
    struct wl_list link; /* nnwm_server::layer_surfaces */
    struct nnwm_server *server;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

/* ext-workspace-v1 protocol structures */
struct nnwm_ext_workspace_manager; /* forward */

struct nnwm_ext_workspace_group; /* forward */

struct nnwm_ext_workspace {
    struct wl_list link;                      /* in nnwm_ext_workspace_group::workspaces */
    struct wl_resource *resource;
    int index;                                /* 0-based */
    struct nnwm_ext_workspace_group *group;
};

struct nnwm_ext_workspace_group {
    struct wl_list link;                      /* in nnwm_ext_workspace_manager::groups */
    struct wl_resource *resource;
    struct wl_list workspaces;                /* nnwm_ext_workspace::link */
    struct nnwm_ext_workspace_manager *manager;
    struct nnwm_output *output;               /* which output this group represents */
};

struct nnwm_ext_workspace_manager {
    struct wl_list link;                      /* in nnwm_server::ext_workspace_managers */
    struct wl_resource *resource;
    struct wl_list groups;                    /* nnwm_ext_workspace_group::link */
    struct nnwm_server *server;
    bool stopped;
};

#ifdef __cplusplus
extern "C"
{
#endif
void server_apply_config(struct nnwm_server *server);
void server_session_active(struct wl_listener *, void *);
void server_new_output(struct wl_listener *, void *);
void server_new_xdg_toplevel(struct wl_listener *, void *);
void server_new_xdg_popup(struct wl_listener *, void *);
void server_new_layer_surface(struct wl_listener *, void *);
void server_new_decoration(struct wl_listener *, void *);
void server_new_lock(struct wl_listener *, void *);
void output_power_set_mode(struct wl_listener *, void *);
void output_manager_apply(struct wl_listener *, void *);
void output_manager_test(struct wl_listener *, void *);
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

#ifdef __cplusplus
#include "actions.hpp"

namespace nnwm {
void ext_workspace_init(struct nnwm_server *server);
void ext_workspace_notify(struct nnwm_server *server);
} // namespace nnwm
#endif

#endif /* NNWM_HPP */
