
#ifndef NNWM_INTERNAL_HPP
#define NNWM_INTERNAL_HPP

#include "nnwm.hpp"

/* ---- Rendering ---- */
void render_titlebar(struct nnwm_toplevel *tl, int inner_width, bool focused);
void render_tab_bar(struct nnwm_server *server, struct nnwm_output *out,
                    int width, int height);
void rerender_tab_bar(struct nnwm_server *server, struct nnwm_output *out);
void update_borders(struct nnwm_toplevel *toplevel, int width, int height, int bw);
void apply_fx_decorations(struct nnwm_toplevel *toplevel);

/* ---- Output / workspace helpers ---- */
struct nnwm_output *output_cycle(struct nnwm_server *server,
                                 struct nnwm_output *cur, int dir);
struct nnwm_output *output_at_cursor(struct nnwm_server *server);

/* ---- Tiled-window navigation helpers ---- */
struct nnwm_toplevel *ws_first(struct nnwm_server *server,
                               struct nnwm_output *out);
struct nnwm_toplevel *ws_next(struct nnwm_server *server,
                              struct nnwm_output *out,
                              struct nnwm_toplevel *cur);
struct nnwm_toplevel *ws_prev(struct nnwm_server *server,
                              struct nnwm_output *out,
                              struct nnwm_toplevel *cur);
struct nnwm_toplevel *ws_last(struct nnwm_server *server,
                              struct nnwm_output *out);
int ws_count(struct nnwm_server *server, struct nnwm_output *out);

/* ---- Floating-window navigation helpers ---- */
struct nnwm_toplevel *ws_first_float(struct nnwm_server *server,
                                     struct nnwm_output *out);
struct nnwm_toplevel *ws_last_float(struct nnwm_server *server,
                                    struct nnwm_output *out);
struct nnwm_toplevel *ws_next_float(struct nnwm_server *server,
                                    struct nnwm_output *out,
                                    struct nnwm_toplevel *cur);
struct nnwm_toplevel *ws_prev_float(struct nnwm_server *server,
                                    struct nnwm_output *out,
                                    struct nnwm_toplevel *cur);

/* ---- Overview ---- */
void render_overview(struct nnwm_server *server, struct nnwm_output *out);
void overview_frame_update(struct nnwm_server *server, struct nnwm_output *out);
void exit_overview(struct nnwm_server *server, struct nnwm_output *out);
struct nnwm_toplevel *overview_toplevel_at(struct nnwm_server *server,
                                            struct nnwm_output *out,
                                            double cx, double cy,
                                            int *out_ws);
void overview_update_labels(struct nnwm_server *server, struct nnwm_output *out);

/* ---- Config error overlay ---- */
void show_config_error(struct nnwm_server *server, const char *message);
void hide_config_error(struct nnwm_server *server);

/* ---- Focus ---- */
void focus_toplevel(struct nnwm_toplevel *toplevel, bool warp = true);
void unfocus_all_borders(struct nnwm_server *server);
struct nnwm_toplevel *get_focused_toplevel(struct nnwm_server *server);

/* ---- Layout ---- */
void arrange_windows(struct nnwm_server *server, struct nnwm_output *out);
void arrange_all_outputs(struct nnwm_server *server);
void arrange_scratchpad(struct nnwm_server *server);

/* ---- Cursor attention ring ---- */
void cursor_ring_start(struct nnwm_server *server);
void cursor_ring_stop(struct nnwm_server *server);

/* ---- Cursor / pointer ---- */
const char *resize_cursor_name(uint32_t edges);
struct nnwm_toplevel *tab_toplevel_at(struct nnwm_server *server,
                                      double lx, double ly);
struct nnwm_toplevel *desktop_toplevel_at(struct nnwm_server *server,
                                          double lx, double ly,
                                          struct wlr_surface **surface,
                                          double *sx, double *sy);
void reset_cursor_mode(struct nnwm_server *server);
void begin_interactive(struct nnwm_toplevel *toplevel,
                       nnwm_cursor_mode mode, uint32_t edges);
void process_cursor_motion(struct nnwm_server *server, uint32_t time,
                           bool real_motion = false);

/* ---- Layer shell ---- */
void arrange_layers(struct nnwm_server *server, struct wlr_output *output);

/* ---- Output management ---- */
void output_manager_build_config(struct nnwm_server *server);

/* ---- Keyboard / input device setup ---- */
void apply_keymap(struct wlr_keyboard *wlr_keyboard,
                  struct nnwm_config *cfg);
void server_new_keyboard(struct nnwm_server *server,
                         struct wlr_input_device *device);
void server_new_pointer(struct nnwm_server *server,
                        struct wlr_input_device *device);

/* ---- Fullscreen toggle (used by actions + xdg request_fullscreen) ---- */
void do_toggle_fullscreen(struct nnwm_toplevel *tl);

/* ---- Fake fullscreen toggle ---- */
void do_toggle_fake_fullscreen(struct nnwm_toplevel *tl);

/* ---- Decoration helpers ---- */
struct nnwm_toplevel *toplevel_from_deco(struct nnwm_decoration *deco);
void decoration_apply(struct nnwm_decoration *deco, bool client_side);

/* ---- XDG toplevel lifecycle ---- */
void xdg_toplevel_map(struct wl_listener *listener, void *data);
void xdg_toplevel_unmap(struct wl_listener *listener, void *data);
void xdg_toplevel_commit(struct wl_listener *listener, void *data);
void handle_xdg_toplevel_destroy(struct wl_listener *listener, void *data);
void xdg_toplevel_request_move(struct wl_listener *listener, void *data);
void xdg_toplevel_request_resize(struct wl_listener *listener, void *data);
void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data);
void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data);

/* ---- XDG popup lifecycle ---- */
void xdg_popup_commit(struct wl_listener *listener, void *data);
void handle_xdg_popup_destroy(struct wl_listener *listener, void *data);

/* ---- Window rules ---- */
void apply_window_rules(struct nnwm_server *server,
                        struct nnwm_toplevel *toplevel);

/* ---- Event hooks ---- */
void fire_hook_plain(struct nnwm_server *server, const char *event);
void fire_hook_window(struct nnwm_server *server, const char *event,
                      struct nnwm_toplevel *tl);
void fire_hook_workspace(struct nnwm_server *server, const char *event,
                         struct nnwm_output *out);
void fire_hook_output(struct nnwm_server *server, const char *event,
                      struct nnwm_output *out);
int  nnwm_timer_cb(void *data);

/* ---- C bridge functions for wlr_xwayland_surface / wlr_xwayland ---- */
#ifdef HAVE_XWAYLAND
extern "C" {
/* Surface field accessors */
struct wlr_surface  *nnwm_xw_surface(const struct wlr_xwayland_surface *s);
const char          *nnwm_xw_class(const struct wlr_xwayland_surface *s);
const char          *nnwm_xw_title(const struct wlr_xwayland_surface *s);
int16_t              nnwm_xw_x(const struct wlr_xwayland_surface *s);
int16_t              nnwm_xw_y(const struct wlr_xwayland_surface *s);
uint16_t             nnwm_xw_width(const struct wlr_xwayland_surface *s);
uint16_t             nnwm_xw_height(const struct wlr_xwayland_surface *s);
int                  nnwm_xw_has_parent(const struct wlr_xwayland_surface *s);
int                  nnwm_xw_override_redirect(const struct wlr_xwayland_surface *s);

/* Surface event signal pointers */
struct wl_signal    *nnwm_xw_events_associate(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_dissociate(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_destroy(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_request_configure(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_set_title(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_request_move(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_request_resize(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_request_maximize(struct wlr_xwayland_surface *s);
struct wl_signal    *nnwm_xw_events_request_fullscreen(struct wlr_xwayland_surface *s);

/* Surface actions */
void                 nnwm_xw_close(struct wlr_xwayland_surface *s);
void                 nnwm_xw_activate(struct wlr_xwayland_surface *s, int activated);
void                 nnwm_xw_configure(struct wlr_xwayland_surface *s,
                                        int16_t x, int16_t y, uint16_t w, uint16_t h);
void                 nnwm_xw_set_fullscreen(struct wlr_xwayland_surface *s, int fullscreen);
int                  nnwm_xw_try_from_surface(struct wlr_surface *s);

/* Event data accessors */
int16_t              nnwm_xw_configure_ev_x(void *ev);
int16_t              nnwm_xw_configure_ev_y(void *ev);
uint16_t             nnwm_xw_configure_ev_width(void *ev);
uint16_t             nnwm_xw_configure_ev_height(void *ev);
uint32_t             nnwm_xw_resize_ev_edges(void *ev);

/* wlr_xwayland (server-level) lifecycle */
struct wlr_xwayland *nnwm_xwl_create(struct wl_display *display,
                                      struct wlr_compositor *compositor, int lazy);
void                 nnwm_xwl_destroy(struct wlr_xwayland *xwl);
void                 nnwm_xwl_set_seat(struct wlr_xwayland *xwl, struct wlr_seat *seat);
struct wl_signal    *nnwm_xwl_events_new_surface(struct wlr_xwayland *xwl);
const char          *nnwm_xwl_display_name(struct wlr_xwayland *xwl);
}
bool nnwm_xwayland_init(struct nnwm_server *server);
void nnwm_xwayland_fini(struct nnwm_server *server);
#endif

/* ---- XWayland / XDG-agnostic toplevel helpers ---- */
static inline struct wlr_surface *tl_wlr_surface(const struct nnwm_toplevel *tl) {
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland) return nnwm_xw_surface(tl->xwayland_surface);
#endif
    return tl->xdg_toplevel->base->surface;
}
static inline const char *tl_app_id(const struct nnwm_toplevel *tl) {
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland) return nnwm_xw_class(tl->xwayland_surface);
#endif
    return tl->xdg_toplevel->app_id;
}
static inline const char *tl_title(const struct nnwm_toplevel *tl) {
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland) return nnwm_xw_title(tl->xwayland_surface);
#endif
    return tl->xdg_toplevel->title;
}
static inline void tl_send_close(struct nnwm_toplevel *tl) {
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland) { nnwm_xw_close(tl->xwayland_surface); return; }
#endif
    wlr_xdg_toplevel_send_close(tl->xdg_toplevel);
}

static inline void tl_xdg_set_size(struct nnwm_toplevel *tl, int w, int h)
{
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland) return;
#endif
    wlr_xdg_toplevel_set_size(tl->xdg_toplevel, w, h);
}
static inline void tl_xdg_set_tiled(struct nnwm_toplevel *tl, uint32_t edges)
{
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland) return;
#endif
    wlr_xdg_toplevel_set_tiled(tl->xdg_toplevel, edges);
}

/* ---- Foreign toplevel management ---- */
void ftl_map(struct nnwm_toplevel *tl);
void ftl_unmap(struct nnwm_toplevel *tl);
void ftl_set_title(struct nnwm_toplevel *tl);
void ftl_set_activated(struct nnwm_toplevel *tl, bool activated);
void ftl_set_fullscreen(struct nnwm_toplevel *tl, bool fullscreen);
void ftl_set_maximized(struct nnwm_toplevel *tl, bool maximized);
void ftl_update_output(struct nnwm_toplevel *tl, struct nnwm_output *old_out);

/* ---- Animation ---- */
void tl_set_geometry(struct nnwm_toplevel *tl, int x, int y, int w, int h, int bw);
#ifdef HAVE_SCENEFX
double anim_now(void);
void animate_step(struct nnwm_server *server);
void tl_start_fade(struct nnwm_toplevel *tl, float from, float to, int duration_ms, nnwm_easing easing);
void tl_start_border_color(struct nnwm_toplevel *tl, const float to[4]);
void tl_open_anim(struct nnwm_toplevel *tl);
void tl_close_anim(struct nnwm_toplevel *tl);

static inline nnwm_easing eff_easing(nnwm_config *cfg, int type_easing) {
    return type_easing >= 0 ? (nnwm_easing)type_easing : cfg->fx.animation.easing;
}
static inline int eff_duration(nnwm_config *cfg, int type_duration) {
    return type_duration > 0 ? type_duration : cfg->fx.animation.duration_ms;
}
#endif /* HAVE_SCENEFX */

#endif /* NNWM_INTERNAL_HPP */
