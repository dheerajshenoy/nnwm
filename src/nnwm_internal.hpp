
#ifndef NNWM_INTERNAL_HPP
#define NNWM_INTERNAL_HPP

#include "nnwm.hpp"

/* ---- Rendering ---- */
void render_titlebar(struct nnwm_toplevel *tl, int inner_width, bool focused);
void render_tab_bar(struct nnwm_server *server, struct nnwm_output *out,
                    int width, int height);
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

/* ---- Config error overlay ---- */
void show_config_error(struct nnwm_server *server, const char *message);
void hide_config_error(struct nnwm_server *server);

/* ---- Focus ---- */
void focus_toplevel(struct nnwm_toplevel *toplevel);
void unfocus_all_borders(struct nnwm_server *server);
struct nnwm_toplevel *get_focused_toplevel(struct nnwm_server *server);

/* ---- Layout ---- */
void arrange_windows(struct nnwm_server *server, struct nnwm_output *out);
void arrange_all_outputs(struct nnwm_server *server);
void arrange_scratchpad(struct nnwm_server *server);

/* ---- Cursor / pointer ---- */
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
