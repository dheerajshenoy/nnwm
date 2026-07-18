#include <wlr/xwayland/xwayland.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>

/* ---- wlr_xwayland_surface field accessors ---- */

struct wlr_surface *nnwm_xw_surface(const struct wlr_xwayland_surface *s)
{
    return s->surface;
}

const char *nnwm_xw_class(const struct wlr_xwayland_surface *s)
{
    return s->class;
}

const char *nnwm_xw_title(const struct wlr_xwayland_surface *s)
{
    return s->title;
}

int16_t nnwm_xw_x(const struct wlr_xwayland_surface *s) { return s->x; }
int16_t nnwm_xw_y(const struct wlr_xwayland_surface *s) { return s->y; }
uint16_t nnwm_xw_width(const struct wlr_xwayland_surface *s) { return s->width; }
uint16_t nnwm_xw_height(const struct wlr_xwayland_surface *s) { return s->height; }
int nnwm_xw_has_parent(const struct wlr_xwayland_surface *s) { return s->parent != NULL; }
int nnwm_xw_override_redirect(const struct wlr_xwayland_surface *s) { return s->override_redirect; }

/* ---- wlr_xwayland_surface event signals ---- */

struct wl_signal *nnwm_xw_events_associate(struct wlr_xwayland_surface *s)
{
    return &s->events.associate;
}

struct wl_signal *nnwm_xw_events_dissociate(struct wlr_xwayland_surface *s)
{
    return &s->events.dissociate;
}

struct wl_signal *nnwm_xw_events_destroy(struct wlr_xwayland_surface *s)
{
    return &s->events.destroy;
}

struct wl_signal *nnwm_xw_events_request_configure(struct wlr_xwayland_surface *s)
{
    return &s->events.request_configure;
}

struct wl_signal *nnwm_xw_events_set_title(struct wlr_xwayland_surface *s)
{
    return &s->events.set_title;
}

struct wl_signal *nnwm_xw_events_request_move(struct wlr_xwayland_surface *s)
{
    return &s->events.request_move;
}

struct wl_signal *nnwm_xw_events_request_resize(struct wlr_xwayland_surface *s)
{
    return &s->events.request_resize;
}

struct wl_signal *nnwm_xw_events_request_maximize(struct wlr_xwayland_surface *s)
{
    return &s->events.request_maximize;
}

struct wl_signal *nnwm_xw_events_request_fullscreen(struct wlr_xwayland_surface *s)
{
    return &s->events.request_fullscreen;
}

/* ---- wlr_xwayland_surface actions ---- */

void nnwm_xw_close(struct wlr_xwayland_surface *s)
{
    wlr_xwayland_surface_close(s);
}

void nnwm_xw_activate(struct wlr_xwayland_surface *s, int activated)
{
    wlr_xwayland_surface_activate(s, activated ? true : false);
}

void nnwm_xw_configure(struct wlr_xwayland_surface *s,
                        int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    wlr_xwayland_surface_configure(s, x, y, w, h);
}

void nnwm_xw_set_fullscreen(struct wlr_xwayland_surface *s, int fullscreen)
{
    wlr_xwayland_surface_set_fullscreen(s, fullscreen ? true : false);
}

int nnwm_xw_try_from_surface(struct wlr_surface *s)
{
    return wlr_xwayland_surface_try_from_wlr_surface(s) != NULL;
}

/* ---- Event data accessors ---- */

int16_t nnwm_xw_configure_ev_x(void *ev) {
    return ((struct wlr_xwayland_surface_configure_event *)ev)->x;
}
int16_t nnwm_xw_configure_ev_y(void *ev) {
    return ((struct wlr_xwayland_surface_configure_event *)ev)->y;
}
uint16_t nnwm_xw_configure_ev_width(void *ev) {
    return ((struct wlr_xwayland_surface_configure_event *)ev)->width;
}
uint16_t nnwm_xw_configure_ev_height(void *ev) {
    return ((struct wlr_xwayland_surface_configure_event *)ev)->height;
}
uint32_t nnwm_xw_resize_ev_edges(void *ev) {
    return ((struct wlr_xwayland_resize_event *)ev)->edges;
}

/* ---- wlr_xwayland (server-level) lifecycle ---- */

struct wlr_xwayland *nnwm_xwl_create(struct wl_display *display,
                                      struct wlr_compositor *compositor,
                                      int lazy)
{
    return wlr_xwayland_create(display, compositor, lazy);
}

void nnwm_xwl_destroy(struct wlr_xwayland *xwl)
{
    wlr_xwayland_destroy(xwl);
}

void nnwm_xwl_set_seat(struct wlr_xwayland *xwl, struct wlr_seat *seat)
{
    wlr_xwayland_set_seat(xwl, seat);
}

struct wl_signal *nnwm_xwl_events_new_surface(struct wlr_xwayland *xwl)
{
    return &xwl->events.new_surface;
}

const char *nnwm_xwl_display_name(struct wlr_xwayland *xwl)
{
    return xwl->display_name;
}
