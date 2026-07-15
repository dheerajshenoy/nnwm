#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
extern "C" {
#include <wlr/interfaces/wlr_buffer.h>
}

namespace {

/* ---- Custom CPU-side wlr_buffer for titlebar pixels ---- */

struct nnwm_tbuf {
    struct wlr_buffer base;
    uint8_t          *data;
    int               stride;
};

static void tbuf_destroy(struct wlr_buffer *b) {
    nnwm_tbuf *tb = wl_container_of(b, tb, base);
    free(tb->data);
    free(tb);
}
static bool tbuf_begin_data_ptr_access(struct wlr_buffer *b, uint32_t /*flags*/,
    void **data, uint32_t *format, size_t *stride) {
    nnwm_tbuf *tb = wl_container_of(b, tb, base);
    *data   = tb->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)tb->stride;
    return true;
}
static void tbuf_end_data_ptr_access(struct wlr_buffer * /*b*/) {}

static const wlr_buffer_impl tbuf_impl = {
    tbuf_destroy, nullptr, nullptr, tbuf_begin_data_ptr_access, tbuf_end_data_ptr_access,
};

static nnwm_tbuf *tbuf_create(int w, int h) {
    auto *tb   = static_cast<nnwm_tbuf*>(calloc(1, sizeof(nnwm_tbuf)));
    tb->stride = w * 4;
    tb->data   = static_cast<uint8_t*>(calloc(h, tb->stride));
    wlr_buffer_init(&tb->base, &tbuf_impl, w, h);
    return tb;
}

} /* anonymous namespace */

/* ---- Titlebar rendering ---- */

void
render_titlebar(nnwm_toplevel *tl, int inner_width, bool focused)
{
    nnwm_config *cfg = tl->server->config;
    int h = cfg->titlebar_height;
    if (h <= 0 || !tl->titlebar || inner_width <= 0) return;

    tl->titlebar_width = inner_width;

    nnwm_tbuf *tb = tbuf_create(inner_width, h);

    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        tb->data, CAIRO_FORMAT_ARGB32, inner_width, h, tb->stride);
    cairo_t *cr = cairo_create(surf);

    /* Background */
    const float *bg = focused ? cfg->titlebar_focused_bg_color : cfg->titlebar_bg_color;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    /* Title text */
    const char *title = tl->xdg_toplevel->title;
    if (title && title[0]) {
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *fd = pango_font_description_from_string(cfg->titlebar_font);
        const char *existing_family = pango_font_description_get_family(fd);
        char fallback_family[256];
        snprintf(fallback_family, sizeof(fallback_family),
                 "%s,DejaVu Sans,Noto Sans,Liberation Sans,Arial",
                 existing_family ? existing_family : "Sans");
        pango_font_description_set_family(fd, fallback_family);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, title, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

        int pad = 4;
        pango_layout_set_width(layout, (inner_width - 2 * pad) * PANGO_SCALE);

        PangoAlignment align = PANGO_ALIGN_CENTER;
        if (cfg->titlebar_text_align == 0) align = PANGO_ALIGN_LEFT;
        else if (cfg->titlebar_text_align == 2) align = PANGO_ALIGN_RIGHT;
        pango_layout_set_alignment(layout, align);

        int pw, ph;
        pango_layout_get_size(layout, &pw, &ph);
        double ty = (h - ph / (double)PANGO_SCALE) / 2.0;

        const float *tc = focused ? cfg->titlebar_focused_text_color : cfg->titlebar_text_color;
        cairo_set_source_rgba(cr, tc[0], tc[1], tc[2], tc[3]);
        cairo_move_to(cr, pad, ty);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wlr_scene_buffer_set_buffer(tl->titlebar, &tb->base);
    wlr_scene_buffer_set_dest_size(tl->titlebar, inner_width, h);
    wlr_buffer_drop(&tb->base);
}

/* Forward declaration for set_opacity_recursive (defined later in scenefx block) */
#ifdef HAVE_SCENEFX
static void set_opacity_recursive(struct wlr_scene_tree *tree, float opacity);
#endif

/* ---- Animation helpers ---- */

#ifdef HAVE_SCENEFX
double
anim_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static float
apply_easing(nnwm_easing e, float t)
{
    switch (e) {
    case NNWM_EASE_LINEAR:  return t;
    case NNWM_EASE_IN:      return t * t * t;
    default:
    case NNWM_EASE_OUT: {
        float f = 1.0f - t;
        return 1.0f - f * f * f;
    }
    case NNWM_EASE_IN_OUT:
        return t < 0.5f
            ? 4.0f * t * t * t
            : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    case NNWM_EASE_BOUNCE: {
        const float n1 = 7.5625f, d1 = 2.75f;
        if (t < 1.0f / d1)         return n1 * t * t;
        if (t < 2.0f / d1)         { t -= 1.5f  / d1; return n1 * t * t + 0.75f;    }
        if (t < 2.5f / d1)         { t -= 2.25f / d1; return n1 * t * t + 0.9375f;  }
                                    { t -= 2.625f/ d1; return n1 * t * t + 0.984375f; }
    }
    case NNWM_EASE_ELASTIC: {
        const float c4 = (2.0f * (float)M_PI) / 3.0f;
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
    }
}

static float
anim_t(double t0, double now, int duration_ms, nnwm_easing easing)
{
    if (duration_ms <= 0) return 1.0f;
    float t = (float)((now - t0) / (duration_ms * 0.001));
    return t >= 1.0f ? 1.0f : apply_easing(easing, t);
}


static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static int   lerpi(int   a, int   b, float t) { return a + (int)roundf((float)(b - a) * t); }
#endif /* HAVE_SCENEFX */

/* ---- Borders and surface placement ---- */

void
update_borders(nnwm_toplevel *toplevel, int width, int height, int bw)
{
    nnwm_config *cfg = toplevel->server->config;
    int th = cfg->titlebar_height;
#ifdef HAVE_SCENEFX
    /* Inset the 4 strips by corner_radius so they don't override the
     * rounded corners provided by border_bg. */
    int r  = cfg->corner_radius;
    int tw = width  - 2 * r; if (tw < 0) tw = 0;
    int sh = height - 2 * r; if (sh < 0) sh = 0;
#else
    int r = 0, tw = width, sh = height - 2 * bw;
#endif

    /* border[0]: top */
    wlr_scene_node_set_position(&toplevel->border[0]->node, r, 0);
    wlr_scene_rect_set_size(toplevel->border[0], tw, bw);

    /* border[1]: bottom */
    wlr_scene_node_set_position(&toplevel->border[1]->node, r, height - bw);
    wlr_scene_rect_set_size(toplevel->border[1], tw, bw);

    /* border[2]: left */
    wlr_scene_node_set_position(&toplevel->border[2]->node, 0, r);
    wlr_scene_rect_set_size(toplevel->border[2], bw, sh);

    /* border[3]: right */
    wlr_scene_node_set_position(&toplevel->border[3]->node, width - bw, r);
    wlr_scene_rect_set_size(toplevel->border[3], bw, sh);

    /* titlebar sits between top border and content */
    if (toplevel->titlebar) {
        bool enabled = (th > 0);
        wlr_scene_node_set_enabled(&toplevel->titlebar->node, enabled);
        wlr_scene_node_set_position(&toplevel->titlebar->node, bw, bw);
    }

    /* window surface is pushed down by the titlebar */
    wlr_scene_node_set_position(&toplevel->scene_surface->node, bw, bw + th);

#ifdef HAVE_SCENEFX
    if (toplevel->border_bg) {
        wlr_scene_node_set_position(&toplevel->border_bg->node, 0, 0);
        wlr_scene_rect_set_size(toplevel->border_bg, width, height);
    }
    if (toplevel->fx_blur) {
        wlr_scene_node_set_position(&toplevel->fx_blur->node, 0, 0);
        wlr_scene_blur_set_size(toplevel->fx_blur, width, height);
    }
    if (toplevel->fx_shadow) {
        wlr_scene_shadow_set_size(toplevel->fx_shadow, width, height);
        wlr_scene_node_set_position(&toplevel->fx_shadow->node,
            (int)cfg->shadow_offset_x, (int)cfg->shadow_offset_y);
    }
#endif
}

/* ---- Animation functions ---- */

void
tl_set_geometry(nnwm_toplevel *tl, int x, int y, int w, int h, int bw)
{
    nnwm_config *cfg = tl->server->config;
#ifdef HAVE_SCENEFX
    bool do_anim = cfg->anim_enabled && cfg->anim_duration_ms > 0;
    bool first   = (tl->cur_w == 0 && tl->cur_h == 0);
    bool changed = (x != tl->cur_x || y != tl->cur_y ||
                    w != tl->cur_w || h != tl->cur_h);

    tl->geo_to_x = x; tl->geo_to_y = y;
    tl->geo_to_w = w; tl->geo_to_h = h;
    tl->geo_bw   = bw;

    if (do_anim && changed) {
        if (first) {
            /* First layout: open style determines the from-position.
             * tl_open_anim() will override duration/easing for the open anim;
             * for non-first layouts we use the layout easing/duration. */
            int open_style = tl->rule_no_anim == 1 ? NNWM_OPEN_NONE
                           : tl->rule_anim_open_style >= 0 ? tl->rule_anim_open_style
                           : (int)cfg->anim_open_style;
            int sw = (int)(w * 0.95f), sh = (int)(h * 0.95f);
            switch ((nnwm_open_style)open_style) {
            default:
            case NNWM_OPEN_FADE_SCALE:
            case NNWM_OPEN_SCALE:
                tl->geo_from_x = x + (w - sw) / 2;
                tl->geo_from_y = y + (h - sh) / 2;
                tl->geo_from_w = sw;
                tl->geo_from_h = sh;
                break;
            case NNWM_OPEN_SLIDE_UP:
                tl->geo_from_x = x; tl->geo_from_y = y + h;
                tl->geo_from_w = w; tl->geo_from_h = h;
                break;
            case NNWM_OPEN_SLIDE_DOWN:
                tl->geo_from_x = x; tl->geo_from_y = y - h;
                tl->geo_from_w = w; tl->geo_from_h = h;
                break;
            case NNWM_OPEN_SLIDE_LEFT:
                tl->geo_from_x = x + w; tl->geo_from_y = y;
                tl->geo_from_w = w; tl->geo_from_h = h;
                break;
            case NNWM_OPEN_SLIDE_RIGHT:
                tl->geo_from_x = x - w; tl->geo_from_y = y;
                tl->geo_from_w = w; tl->geo_from_h = h;
                break;
            case NNWM_OPEN_FADE:
            case NNWM_OPEN_NONE:
                tl->geo_from_x = x; tl->geo_from_y = y;
                tl->geo_from_w = w; tl->geo_from_h = h;
                break;
            }
        } else {
            /* Skip geo animation if layout anim is disabled */
            if (cfg->anim_layout_style == NNWM_LAYOUT_ANIM_NONE) {
                tl->geo_anim = false;
                tl->cur_x = x; tl->cur_y = y;
                tl->cur_w = w; tl->cur_h = h;
                wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
                update_borders(tl, w, h, bw);
                return;
            }
            tl->geo_from_x = tl->cur_x;
            tl->geo_from_y = tl->cur_y;
            tl->geo_from_w = tl->cur_w;
            tl->geo_from_h = tl->cur_h;
        }
        tl->geo_anim      = true;
        tl->geo_t0        = anim_now();
        tl->geo_then_hide = false;
        /* Bake layout easing/duration; tl_open_anim() will override for first layout */
        tl->geo_duration_ms = eff_duration(cfg, cfg->anim_layout_duration_ms);
        tl->geo_easing      = eff_easing(cfg, cfg->anim_layout_easing);
        wlr_scene_node_set_position(&tl->scene_tree->node, tl->geo_from_x, tl->geo_from_y);
        update_borders(tl, tl->geo_from_w, tl->geo_from_h, bw);
        tl->cur_x = tl->geo_from_x; tl->cur_y = tl->geo_from_y;
        tl->cur_w = tl->geo_from_w; tl->cur_h = tl->geo_from_h;
        return;
    }
    // fall-through: instant apply
    tl->geo_anim = false;
#endif /* HAVE_SCENEFX */
    // instant apply
    tl->cur_x = x; tl->cur_y = y;
    tl->cur_w = w; tl->cur_h = h;
    wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
    update_borders(tl, w, h, bw);
}

#ifdef HAVE_SCENEFX
void
tl_start_fade(nnwm_toplevel *tl, float from, float to, int duration_ms, nnwm_easing easing)
{
    tl->fade_from        = from;
    tl->fade_to          = to;
    tl->fade_duration_ms = duration_ms;
    tl->fade_easing      = easing;
    if (tl->server->config->anim_enabled && duration_ms > 0) {
        tl->fade_anim = true;
        tl->fade_t0   = anim_now();
        set_opacity_recursive(tl->scene_surface, from);
    } else {
        tl->fade_anim = false;
        set_opacity_recursive(tl->scene_surface, to);
    }
}

void
tl_open_anim(nnwm_toplevel *tl)
{
    nnwm_config *cfg = tl->server->config;
    if (!cfg->anim_enabled) return;

    int open_style = tl->rule_no_anim == 1 ? NNWM_OPEN_NONE
                   : tl->rule_anim_open_style >= 0 ? tl->rule_anim_open_style
                   : (int)cfg->anim_open_style;
    int dur = eff_duration(cfg, cfg->anim_open_duration_ms);
    nnwm_easing ease = eff_easing(cfg, cfg->anim_open_easing);

    float target_op = (tl->rule_opacity >= 0.0f) ? tl->rule_opacity : cfg->opacity;

    bool do_fade  = (open_style != NNWM_OPEN_SCALE && open_style != NNWM_OPEN_NONE);
    bool do_scale = (open_style == NNWM_OPEN_FADE_SCALE || open_style == NNWM_OPEN_SCALE);
    bool do_slide = (open_style >= NNWM_OPEN_SLIDE_UP && open_style <= NNWM_OPEN_SLIDE_RIGHT);

    if (do_fade)
        tl_start_fade(tl, 0.0f, target_op, dur, ease);
    else {
        /* Set opacity directly to target */
        set_opacity_recursive(tl->scene_surface, target_op);
    }

    if (open_style == NNWM_OPEN_NONE) {
        tl->geo_anim = false;
        /* Apply target position immediately */
        wlr_scene_node_set_position(&tl->scene_tree->node, tl->geo_to_x, tl->geo_to_y);
        update_borders(tl, tl->geo_to_w, tl->geo_to_h, tl->geo_bw);
        tl->cur_x = tl->geo_to_x; tl->cur_y = tl->geo_to_y;
        tl->cur_w = tl->geo_to_w; tl->cur_h = tl->geo_to_h;
        return;
    }
    if (!do_scale && !do_slide) {
        /* OPEN_FADE: cancel geo animation */
        tl->geo_anim = false;
        wlr_scene_node_set_position(&tl->scene_tree->node, tl->geo_to_x, tl->geo_to_y);
        update_borders(tl, tl->geo_to_w, tl->geo_to_h, tl->geo_bw);
        tl->cur_x = tl->geo_to_x; tl->cur_y = tl->geo_to_y;
        tl->cur_w = tl->geo_to_w; tl->cur_h = tl->geo_to_h;
        return;
    }
    /* For scale + slide, from-position is already set in tl_set_geometry — bake easing */
    if (tl->geo_anim) {
        tl->geo_duration_ms = dur;
        tl->geo_easing      = ease;
    }
}

void
tl_close_anim(nnwm_toplevel *tl)
{
    nnwm_config *cfg = tl->server->config;
    if (!cfg->anim_enabled || cfg->anim_duration_ms <= 0) {
        tl->dying = false;
        return;
    }

    int close_style = tl->rule_no_anim == 1 ? NNWM_OPEN_NONE
                    : tl->rule_anim_close_style >= 0 ? tl->rule_anim_close_style
                    : (int)cfg->anim_close_style;
    int dur = eff_duration(cfg, cfg->anim_close_duration_ms);
    nnwm_easing ease = eff_easing(cfg, cfg->anim_close_easing);

    float cur_op = (tl->rule_opacity >= 0.0f) ? tl->rule_opacity : cfg->opacity;

    bool do_fade  = (close_style != NNWM_OPEN_SCALE && close_style != NNWM_OPEN_NONE);
    bool do_slide = (close_style >= NNWM_OPEN_SLIDE_UP && close_style <= NNWM_OPEN_SLIDE_RIGHT);

    if (do_fade)
        tl_start_fade(tl, cur_op, 0.0f, dur, ease);

    if (do_slide) {
        int dx = 0, dy = 0;
        int w = tl->cur_w, h = tl->cur_h;
        switch ((nnwm_open_style)close_style) {
        case NNWM_OPEN_SLIDE_UP:    dy = -h; break;
        case NNWM_OPEN_SLIDE_DOWN:  dy = +h; break;
        case NNWM_OPEN_SLIDE_LEFT:  dx = -w; break;
        case NNWM_OPEN_SLIDE_RIGHT: dx = +w; break;
        default: break;
        }
        tl->geo_from_x = tl->cur_x;
        tl->geo_from_y = tl->cur_y;
        tl->geo_from_w = tl->cur_w;
        tl->geo_from_h = tl->cur_h;
        tl->geo_to_x   = tl->cur_x + dx;
        tl->geo_to_y   = tl->cur_y + dy;
        tl->geo_to_w   = tl->cur_w;
        tl->geo_to_h   = tl->cur_h;
        tl->geo_duration_ms = dur;
        tl->geo_easing      = ease;
        tl->geo_anim        = true;
        tl->geo_t0          = anim_now();
        tl->geo_then_hide   = false;
    }

    if (close_style == NNWM_OPEN_NONE) {
        tl->dying = false;
    } else {
        tl->dying = true;
    }
}

void
tl_start_border_color(nnwm_toplevel *tl, const float to[4])
{
    nnwm_config *cfg = tl->server->config;
    nnwm_focus_style fs = cfg->anim_focus_style;

    if (!cfg->anim_enabled || fs == NNWM_FOCUS_NONE) {
        for (int i = 0; i < 4; i++) tl->bcol_to[i] = to[i];
        tl->bcol_anim = false;
        for (int b = 0; b < 4; b++) wlr_scene_rect_set_color(tl->border[b], to);
        if (tl->border_bg) wlr_scene_rect_set_color(tl->border_bg, to);
        return;
    }
    for (int i = 0; i < 4; i++)
        tl->bcol_from[i] = tl->bcol_anim ? tl->bcol_to[i] : tl->border[0]->color[i];
    for (int i = 0; i < 4; i++) tl->bcol_to[i] = to[i];
    tl->bcol_duration_ms = eff_duration(cfg, cfg->anim_focus_duration_ms);
    tl->bcol_easing      = eff_easing(cfg, cfg->anim_focus_easing);
    tl->bcol_anim = true;
    tl->bcol_t0   = anim_now();
}

static bool
animate_step_one(nnwm_server * /*server*/, nnwm_toplevel *tl, double now)
{
    bool active = false;
    if (tl->geo_anim) {
        float t  = anim_t(tl->geo_t0, now, tl->geo_duration_ms, tl->geo_easing);
        int   cx = lerpi(tl->geo_from_x, tl->geo_to_x, t);
        int   cy = lerpi(tl->geo_from_y, tl->geo_to_y, t);
        int   cw = lerpi(tl->geo_from_w, tl->geo_to_w, t);
        int   ch = lerpi(tl->geo_from_h, tl->geo_to_h, t);
        wlr_scene_node_set_position(&tl->scene_tree->node, cx, cy);
        update_borders(tl, cw, ch, tl->geo_bw);
        tl->cur_x = cx; tl->cur_y = cy;
        tl->cur_w = cw; tl->cur_h = ch;
        if (t >= 1.0f) {
            tl->geo_anim = false;
            if (tl->geo_then_hide) {
                wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
                tl->cur_x = tl->geo_from_x; tl->cur_y = tl->geo_from_y;
                tl->cur_w = tl->geo_from_w; tl->cur_h = tl->geo_from_h;
            } else if (tl->dying && !tl->fade_anim) {
                wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
            }
        }
        active = true;
    }
    if (tl->fade_anim) {
        float t = anim_t(tl->fade_t0, now, tl->fade_duration_ms, tl->fade_easing);
        set_opacity_recursive(tl->scene_surface, lerpf(tl->fade_from, tl->fade_to, t));
        if (t >= 1.0f) {
            tl->fade_anim = false;
            if (tl->dying && !tl->geo_anim)
                wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
        }
        active = true;
    }
    if (tl->bcol_anim) {
        float t = anim_t(tl->bcol_t0, now, tl->bcol_duration_ms, tl->bcol_easing);
        float col[4];
        for (int i = 0; i < 4; i++)
            col[i] = lerpf(tl->bcol_from[i], tl->bcol_to[i], t);
        for (int i = 0; i < 4; i++)
            wlr_scene_rect_set_color(tl->border[i], col);
        if (tl->border_bg) wlr_scene_rect_set_color(tl->border_bg, col);
        if (t >= 1.0f) tl->bcol_anim = false;
        active = true;
    }
    return active;
}

void
animate_step(nnwm_server *server)
{
    double now = anim_now();

    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
        animate_step_one(server, tl, now);
    wl_list_for_each(tl, &server->dying_toplevels, dying_link)
        animate_step_one(server, tl, now);
}
#endif /* HAVE_SCENEFX */


/* ---- scenefx per-window decorations ---- */

#ifdef HAVE_SCENEFX
static void
set_corner_radius_recursive(struct wlr_scene_tree *tree, int radius)
{
    struct wlr_scene_node *child;
    wl_list_for_each(child, &tree->children, link) {
        if (child->type == WLR_SCENE_NODE_BUFFER)
            wlr_scene_buffer_set_corner_radius(
                wlr_scene_buffer_from_node(child), radius);
        else if (child->type == WLR_SCENE_NODE_TREE)
            set_corner_radius_recursive(
                wlr_scene_tree_from_node(child), radius);
    }
}

static void
set_opacity_recursive(struct wlr_scene_tree *tree, float opacity)
{
    struct wlr_scene_node *child;
    wl_list_for_each(child, &tree->children, link) {
        if (child->type == WLR_SCENE_NODE_BUFFER)
            wlr_scene_buffer_set_opacity(
                wlr_scene_buffer_from_node(child), opacity);
        else if (child->type == WLR_SCENE_NODE_TREE)
            set_opacity_recursive(
                wlr_scene_tree_from_node(child), opacity);
    }
}
#endif

void
apply_fx_decorations(nnwm_toplevel *toplevel)
{
#ifdef HAVE_SCENEFX
    nnwm_config *cfg = toplevel->server->config;

    /* Per-corner radii on the 4 border rects:
     * top rect rounds its own two top corners; bottom rect rounds its two
     * bottom corners; left/right strips have no corners to round (they fit
     * flush between top and bottom). */
    int r = cfg->corner_radius;

    /* border_bg: full-window rect that provides the correctly rounded outer
     * corners. The 4 border strips are inset by r (see update_borders) so
     * they never cover the corner areas that border_bg rounds. */
    if (!toplevel->border_bg) {
        toplevel->border_bg = wlr_scene_rect_create(
            toplevel->scene_tree, 0, 0, toplevel->border[0]->color);
        wlr_scene_node_lower_to_bottom(&toplevel->border_bg->node);
    }
    wlr_scene_rect_set_corner_radius(toplevel->border_bg, r);

    /* Corner radius on titlebar buffer */
    if (toplevel->titlebar)
        wlr_scene_buffer_set_corner_radius(toplevel->titlebar, cfg->corner_radius);

    /* Inner corner radius: surface sits inset by border_width, so its corners
     * need a smaller radius to remain concentric with the outer border corners. */
    int inner_r = cfg->corner_radius > cfg->border_width
                  ? cfg->corner_radius - cfg->border_width : 0;
    if (toplevel->scene_surface)
        set_corner_radius_recursive(toplevel->scene_surface, inner_r);

    /* Per-window overrides take precedence over global config values */
    float eff_opacity = (toplevel->rule_opacity >= 0.0f)
                        ? toplevel->rule_opacity : cfg->opacity;
    bool  eff_blur    = (toplevel->rule_blur    >= 0)
                        ? (bool)toplevel->rule_blur : cfg->blur_enabled;

    /* Window content opacity */
    if (toplevel->scene_surface)
        set_opacity_recursive(toplevel->scene_surface, eff_opacity);

    /* Background blur */
    if (eff_blur) {
        wlr_scene_set_blur_data(toplevel->server->scene,
            cfg->blur_passes, cfg->blur_radius,
            cfg->blur_noise, cfg->blur_brightness,
            cfg->blur_contrast, cfg->blur_saturation);
        if (!toplevel->fx_blur) {
            wlr_box geo = toplevel->xdg_toplevel->base->geometry;
            int bw = cfg->border_width;
            int th = cfg->titlebar_height;
            int w  = geo.width  + 2 * bw;
            int h  = geo.height + 2 * bw + th;
            toplevel->fx_blur = wlr_scene_blur_create(toplevel->scene_tree, w, h);
            wlr_scene_blur_set_corner_radius(toplevel->fx_blur, cfg->corner_radius);
            wlr_scene_node_lower_to_bottom(&toplevel->fx_blur->node);
        } else {
            wlr_scene_blur_set_corner_radius(toplevel->fx_blur, cfg->corner_radius);
        }
    } else if (!eff_blur && toplevel->fx_blur) {
        wlr_scene_node_destroy(&toplevel->fx_blur->node);
        toplevel->fx_blur = nullptr;
    }

    /* Shadow node */
    if (cfg->shadow_enabled && !toplevel->fx_shadow) {
        wlr_box geo = toplevel->xdg_toplevel->base->geometry;
        int bw = cfg->border_width;
        int th = cfg->titlebar_height;
        int w  = geo.width  + 2 * bw;
        int h  = geo.height + 2 * bw + th;
        toplevel->fx_shadow = wlr_scene_shadow_create(
            toplevel->scene_tree, w, h,
            cfg->corner_radius, cfg->shadow_blur_sigma,
            cfg->shadow_color);
        wlr_scene_node_set_position(&toplevel->fx_shadow->node,
            (int)cfg->shadow_offset_x, (int)cfg->shadow_offset_y);
        wlr_scene_node_lower_to_bottom(&toplevel->fx_shadow->node);
    } else if (toplevel->fx_shadow) {
        if (!cfg->shadow_enabled) {
            wlr_scene_node_destroy(&toplevel->fx_shadow->node);
            toplevel->fx_shadow = nullptr;
        } else {
            wlr_scene_shadow_set_corner_radius(toplevel->fx_shadow, cfg->corner_radius);
            wlr_scene_shadow_set_blur_sigma(toplevel->fx_shadow, cfg->shadow_blur_sigma);
            wlr_scene_shadow_set_color(toplevel->fx_shadow, cfg->shadow_color);
            wlr_scene_node_set_position(&toplevel->fx_shadow->node,
                (int)cfg->shadow_offset_x, (int)cfg->shadow_offset_y);
        }
    }
#else
    (void)toplevel;
#endif
}

/* ---- Tabbed layout tab bar rendering ---- */

void
render_tab_bar(nnwm_server *server, nnwm_output *out, int width, int height)
{
    if (!out->tab_bar || width <= 0 || height <= 0) return;

    nnwm_config *cfg = server->config;
    int ws = out->active_workspace;

    int n = ws_count(server, out);
    if (n == 0) {
        wlr_scene_node_set_enabled(&out->tab_bar->node, false);
        return;
    }

    /* Use last_focused[ws] so the tab stays highlighted even when a layer-shell
     * surface (e.g. rofi) temporarily holds keyboard focus. Validate it is still
     * a tiled window (it may have become floating since last focus). */
    nnwm_toplevel *active_tl = out->last_focused[ws];
    if (active_tl && (active_tl->output != out || active_tl->workspace != ws
                      || active_tl->floating || active_tl->fullscreen))
        active_tl = nullptr;

    nnwm_tbuf *tb = tbuf_create(width, height);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        tb->data, CAIRO_FORMAT_ARGB32, width, height, tb->stride);
    cairo_t *cr = cairo_create(surf);

    const float *dflt_bg = cfg->titlebar_bg_color;
    cairo_set_source_rgba(cr, dflt_bg[0], dflt_bg[1], dflt_bg[2], dflt_bg[3]);
    cairo_paint(cr);

    int i = 0;
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->output != out || tl->workspace != ws || tl->floating || tl->fullscreen)
            continue;

        bool focused = (tl == active_tl);
        int x = (int)((long)i * width / n);
        int w = (int)((long)(i + 1) * width / n) - x;

        const float *bg = focused ? cfg->titlebar_focused_bg_color : cfg->titlebar_bg_color;
        cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
        cairo_rectangle(cr, x, 0, w, height);
        cairo_fill(cr);

        /* Tab separator */
        if (i < n - 1) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
            cairo_rectangle(cr, x + w - 1, 0, 1, height);
            cairo_fill(cr);
        }

        const char *title = tl->xdg_toplevel->title;
        if (title && title[0]) {
            PangoLayout *layout = pango_cairo_create_layout(cr);
            PangoFontDescription *fd = pango_font_description_from_string(
                cfg->titlebar_font ? cfg->titlebar_font : "Sans 10");
            const char *fam = pango_font_description_get_family(fd);
            char fallback[256];
            std::snprintf(fallback, sizeof(fallback),
                          "%s,DejaVu Sans,Noto Sans,Liberation Sans,Arial",
                          fam ? fam : "Sans");
            pango_font_description_set_family(fd, fallback);
            pango_layout_set_font_description(layout, fd);
            pango_font_description_free(fd);
            pango_layout_set_text(layout, title, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

            int pad = 4;
            pango_layout_set_width(layout, (w - 2 * pad) * PANGO_SCALE);
            pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

            int pw, ph;
            pango_layout_get_size(layout, &pw, &ph);
            double ty = (height - ph / (double)PANGO_SCALE) / 2.0;

            const float *tc = focused ? cfg->titlebar_focused_text_color : cfg->titlebar_text_color;
            cairo_set_source_rgba(cr, tc[0], tc[1], tc[2], tc[3]);
            cairo_move_to(cr, x + pad, ty);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }
        i++;
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wlr_scene_buffer_set_buffer(out->tab_bar, &tb->base);
    wlr_scene_buffer_set_dest_size(out->tab_bar, width, height);
    wlr_buffer_drop(&tb->base);
    wlr_scene_node_set_enabled(&out->tab_bar->node, true);
    wlr_scene_node_raise_to_top(&out->tab_bar->node);
}

/* ---- Output / workspace helpers ---- */

nnwm_output *
output_cycle(nnwm_server *server, nnwm_output *cur, int dir)
{
    nnwm_output *outputs[32];
    int count = 0, cur_idx = 0;
    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (count < 32) {
            if (o == cur) cur_idx = count;
            outputs[count++] = o;
        }
    }
    if (count <= 1) return nullptr;
    return outputs[(cur_idx + dir + count) % count];
}

nnwm_output *
output_at_cursor(nnwm_server *server)
{
    wlr_output *wlr_out = wlr_output_layout_output_at(
        server->output_layout, server->cursor->x, server->cursor->y);
    if (!wlr_out) return nullptr;
    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link)
        if (o->wlr_output == wlr_out) return o;
    return nullptr;
}

/* ---- Workspace tiled-window navigation helpers ---- */

/* A window belongs to the active tiled set if it is on this output and either
 * on the active workspace or sticky (sticky windows tile into every workspace). */
#define WS_TILED(t, out) \
    ((t)->output == (out) && \
     ((t)->workspace == (out)->active_workspace || (t)->sticky) && \
     !(t)->floating && !(t)->fullscreen)

nnwm_toplevel *
ws_first(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
        if (WS_TILED(t, out)) return t;
    return nullptr;
}

nnwm_toplevel *
ws_next(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.next; it != &server->toplevels; it = it->next) {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_TILED(t, out)) return t;
    }
    return nullptr;
}

nnwm_toplevel *
ws_prev(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.prev; it != &server->toplevels; it = it->prev) {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_TILED(t, out)) return t;
    }
    return nullptr;
}

#define WS_FLOAT(t, out) \
    ((t)->output == (out) && \
     ((t)->workspace == (out)->active_workspace || (t)->sticky) && \
     (t)->floating)

nnwm_toplevel *
ws_first_float(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
        if (WS_FLOAT(t, out)) return t;
    return nullptr;
}

nnwm_toplevel *
ws_last_float(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t, *last = nullptr;
    wl_list_for_each(t, &server->toplevels, link)
        if (WS_FLOAT(t, out)) last = t;
    return last;
}

nnwm_toplevel *
ws_next_float(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.next; it != &server->toplevels; it = it->next) {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_FLOAT(t, out)) return t;
    }
    return nullptr;
}

nnwm_toplevel *
ws_prev_float(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.prev; it != &server->toplevels; it = it->prev) {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_FLOAT(t, out)) return t;
    }
    return nullptr;
}

nnwm_toplevel *
ws_last(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t, *last = nullptr;
    wl_list_for_each(t, &server->toplevels, link)
        if (WS_TILED(t, out)) last = t;
    return last;
}

int
ws_count(nnwm_server *server, nnwm_output *out)
{
    int n = 0;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
        if (WS_TILED(t, out)) n++;
    return n;
}

/* ---- Window arrangement / tiling layout ---- */

void
arrange_windows(nnwm_server *server, nnwm_output *out)
{
    if (!out)
        return;

    int ws = out->active_workspace;
    const wlr_box &area = out->usable_area;
    nnwm_config *cfg = server->config;
    nnwm_toplevel *tl;

    /* ── Tabbed layout ─────────────────────────────────────────────────────── */
    if (out->layout_mode[ws] == NNWM_LAYOUT_TABBED) {
        int n     = ws_count(server, out);
        bool solo = (n == 1);
        int bw    = (solo && cfg->smart_borders) ? 0 : cfg->border_width;
        int og    = (solo && cfg->smart_gaps)    ? 0 : cfg->outer_gap;
        int tab_h = cfg->titlebar_height > 0 ? cfg->titlebar_height : 24;

        /* Use last_focused[ws] so the visible window survives layer-shell focus
         * steals (e.g. rofi). Validate it is still a tiled window on this
         * workspace (it may have become floating/fullscreen since last focus). */
        nnwm_toplevel *active = out->last_focused[ws];
        if (!active || active->output != out || active->workspace != ws
                    || active->floating || active->fullscreen)
            active = ws_first(server, out);

        int cx = area.x + og;
        int cy = area.y + og + tab_h;
        int cw = area.width  - 2 * og;
        int ch = area.height - 2 * og - tab_h;

        wl_list_for_each(tl, &server->toplevels, link) {
            if (!WS_TILED(tl, out)) continue;
            wlr_scene_node_set_enabled(&tl->scene_tree->node, tl == active);
            wlr_xdg_toplevel_set_tiled(tl->xdg_toplevel,
                WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, cw - 2 * bw, ch - 2 * bw);
            tl_set_geometry(tl, cx, cy, cw, ch, bw);
            /* Override scene_surface position: no per-window titlebar offset.
             * Must happen after tl_set_geometry which re-enables the titlebar. */
            wlr_scene_node_set_position(&tl->scene_surface->node, bw, bw);
            if (tl->titlebar)
                wlr_scene_node_set_enabled(&tl->titlebar->node, false);
        }

        if (n > 0) {
            render_tab_bar(server, out, cw, tab_h);
            wlr_scene_node_set_position(&out->tab_bar->node, cx, area.y + og);
            wlr_scene_node_raise_to_top(&out->tab_bar->node);
        } else if (out->tab_bar) {
            wlr_scene_node_set_enabled(&out->tab_bar->node, false);
        }

        /* Floating and fullscreen windows above everything */
        wl_list_for_each(tl, &server->toplevels, link) {
            if (tl->output == out && (tl->workspace == ws || tl->sticky)
                    && (tl->floating || tl->fullscreen))
                wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        }
        return;
    }

    /* ── Scroll layout (horizontal strip) ─────────────────────────────────── */
    if (out->layout_mode[ws] == NNWM_LAYOUT_SCROLL) {
        if (out->tab_bar)
            wlr_scene_node_set_enabled(&out->tab_bar->node, false);

        int og    = cfg->outer_gap;
        int ig    = cfg->inner_gap;
        int bw    = cfg->border_width;
        int th    = cfg->titlebar_height;
        float cw_frac = cfg->scroll_column_width > 0.0f ? cfg->scroll_column_width : 0.5f;
        int col_w = (int)(area.width * cw_frac);
        int col_h = area.height - 2 * og;

        /* Find focused window index to compute scroll offset */
        nnwm_toplevel *active = out->last_focused[ws];
        if (!active || active->output != out || active->workspace != ws
                    || active->floating || active->fullscreen)
            active = ws_first(server, out);

        int fi = 0, idx = 0;
        wl_list_for_each(tl, &server->toplevels, link) {
            if (!WS_TILED(tl, out)) continue;
            if (tl == active) fi = idx;
            idx++;
        }

        /* Center the focused column in the viewport */
        int focused_left = og + fi * (col_w + ig);
        int target = focused_left + col_w / 2 - area.width / 2;
        if (target < 0) target = 0;
        out->scroll_offset[ws] = target;

        wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;
        int i = 0;
        wl_list_for_each(tl, &server->toplevels, link) {
            if (!WS_TILED(tl, out)) continue;
            int tx = area.x + og + i * (col_w + ig) - out->scroll_offset[ws];
            int ty = area.y + og;
            bool focused = (tl->xdg_toplevel->base->surface == focused_surface);
            wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
            wlr_xdg_toplevel_set_tiled(tl->xdg_toplevel,
                WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, col_w - 2 * bw, col_h - 2 * bw - th);
            tl_set_geometry(tl, tx, ty, col_w, col_h, bw);
            render_titlebar(tl, col_w - 2 * bw, focused);
            i++;
        }

        /* Floating and fullscreen windows above tiled */
        wl_list_for_each(tl, &server->toplevels, link) {
            if (tl->output == out && (tl->workspace == ws || tl->sticky)
                    && (tl->floating || tl->fullscreen))
                wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        }
        return;
    }

    /* ── Tile layout (master-stack) ────────────────────────────────────────── */

    /* Disable tab bar and re-enable tiled scene trees (from possible tabbed state) */
    if (out->tab_bar)
        wlr_scene_node_set_enabled(&out->tab_bar->node, false);
    wl_list_for_each(tl, &server->toplevels, link) {
        if (WS_TILED(tl, out))
            wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
    }

    int n = ws_count(server, out);

    bool solo = (n == 1);
    int bw = (solo && cfg->smart_borders) ? 0 : cfg->border_width;
    int ig = (solo && cfg->smart_gaps)    ? 0 : cfg->inner_gap;
    int og = (solo && cfg->smart_gaps)    ? 0 : cfg->outer_gap;
    int th = cfg->titlebar_height;

    int x0 = area.x + og;
    int y0 = area.y + og;
    int W  = area.width  - 2 * og;
    int H  = area.height - 2 * og;

    wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;

    if (n == 1) {
        wl_list_for_each(tl, &server->toplevels, link) {
            if (!WS_TILED(tl, out)) continue;
            wlr_xdg_toplevel_set_tiled(tl->xdg_toplevel,
                WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, W - 2 * bw, H - 2 * bw - th);
            tl_set_geometry(tl, x0, y0, W, H, bw);
            render_titlebar(tl, W - 2 * bw,
                tl->xdg_toplevel->base->surface == focused_surface);
            break;
        }
    } else if (n > 1) {
        int mw = (int)(W * cfg->master_ratio);
        int sw = W - mw - ig;
        int ns = n - 1;
        int sh = (H - (ns - 1) * ig) / ns;

        int i = 0;
        wl_list_for_each(tl, &server->toplevels, link) {
            if (!WS_TILED(tl, out)) continue;
            bool focused = (tl->xdg_toplevel->base->surface == focused_surface);
            if (i == 0) {
                wlr_xdg_toplevel_set_tiled(tl->xdg_toplevel,
                    WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                wlr_xdg_toplevel_set_size(tl->xdg_toplevel, mw - 2 * bw, H - 2 * bw - th);
                tl_set_geometry(tl, x0, y0, mw, H, bw);
                render_titlebar(tl, mw - 2 * bw, focused);
            } else {
                int sy = y0 + (i - 1) * (sh + ig);
                int h  = (i < ns) ? sh : H - (i - 1) * (sh + ig);
                wlr_xdg_toplevel_set_tiled(tl->xdg_toplevel,
                    WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                wlr_xdg_toplevel_set_size(tl->xdg_toplevel, sw - 2 * bw, h - 2 * bw - th);
                tl_set_geometry(tl, x0 + mw + ig, sy, sw, h, bw);
                render_titlebar(tl, sw - 2 * bw, focused);
            }
            ++i;
        }
    }

    /* Floating and fullscreen windows must always sit above tiled ones.
     * Clear the tiled state so clients know they can control their own size. */
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->output == out && (tl->workspace == ws || tl->sticky)
                && (tl->floating || tl->fullscreen)) {
            wlr_xdg_toplevel_set_tiled(tl->xdg_toplevel, WLR_EDGE_NONE);
            wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        }
    }
}

void
arrange_all_outputs(nnwm_server *server)
{
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link)
        arrange_windows(server, out);
}

/* ---- Focus management ---- */

void
focus_toplevel(nnwm_toplevel *toplevel)
{
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
        return;
    }
    if (prev_surface)
    {
        wlr_xdg_toplevel *prev_toplevel
            = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != nullptr)
        {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }
    wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    /* Update border colors and titlebar focus state for all windows */
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        bool foc = (tl == toplevel);
        float *color = foc ? server->config->focused_color
                           : server->config->unfocused_color;
#ifdef HAVE_SCENEFX
        tl_start_border_color(tl, color);
#else
        for (int b = 0; b < 4; b++) wlr_scene_rect_set_color(tl->border[b], color);
#endif
        render_titlebar(tl, tl->titlebar_width, foc);
    }

    if (keyboard != nullptr)
    {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                       keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
    if (toplevel->floating)
        wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

    nnwm_output *out = toplevel->output;
    if (out) {
        server->focused_output = out;
        int ws = toplevel->workspace;
        if (out->last_focused[ws] != toplevel)
            out->prev_focused[ws] = out->last_focused[ws];
        out->last_focused[ws] = toplevel;

        if (out->layout_mode[ws] == NNWM_LAYOUT_TABBED ||
            out->layout_mode[ws] == NNWM_LAYOUT_SCROLL)
            arrange_windows(server, out);
    }
}

void
unfocus_all_borders(nnwm_server *server)
{
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
#ifdef HAVE_SCENEFX
        tl_start_border_color(tl, server->config->unfocused_color);
#else
        for (int b = 0; b < 4; b++)
            wlr_scene_rect_set_color(tl->border[b], server->config->unfocused_color);
#endif
        render_titlebar(tl, tl->titlebar_width, false);
    }
}

/* ---- Config error overlay ---- */

static int
error_dismiss_cb(void *data)
{
    hide_config_error(static_cast<nnwm_server*>(data));
    return 0;
}

void
show_config_error(nnwm_server *server, const char *message)
{
    const int bar_h = 24;
    const float bg[4] = { 0.7f, 0.1f, 0.1f, 1.0f };
    const float fg[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (!out->error_bar) continue;
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output, &area);
        int W = area.width;
        if (W <= 0) continue;

        nnwm_tbuf *tb = tbuf_create(W, bar_h);
        cairo_surface_t *surf = cairo_image_surface_create_for_data(
            tb->data, CAIRO_FORMAT_ARGB32, W, bar_h, tb->stride);
        cairo_t *cr = cairo_create(surf);

        cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
        cairo_paint(cr);

        PangoLayout *layout = pango_cairo_create_layout(cr);
        const char *font = server->config->titlebar_font
                         ? server->config->titlebar_font : "Sans 10";
        PangoFontDescription *fd = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);

        char text[1024];
        std::snprintf(text, sizeof(text), "Config error: %s", message);
        pango_layout_set_text(layout, text, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, (W - 8) * PANGO_SCALE);

        int pw, ph;
        pango_layout_get_size(layout, &pw, &ph);
        double ty = (bar_h - ph / (double)PANGO_SCALE) / 2.0;

        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
        cairo_move_to(cr, 8, ty);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        cairo_destroy(cr);
        cairo_surface_destroy(surf);

        wlr_scene_buffer_set_buffer(out->error_bar, &tb->base);
        wlr_scene_buffer_set_dest_size(out->error_bar, W, bar_h);
        wlr_buffer_drop(&tb->base);
        wlr_scene_node_set_position(&out->error_bar->node, area.x, area.y);
        wlr_scene_node_set_enabled(&out->error_bar->node, true);
        wlr_scene_node_raise_to_top(&out->error_bar->node);
    }

    /* (Re-)arm the auto-dismiss timer for 8 seconds */
    if (!server->error_dismiss_timer) {
        struct wl_event_loop *loop =
            wl_display_get_event_loop(server->wl_display);
        server->error_dismiss_timer =
            wl_event_loop_add_timer(loop, error_dismiss_cb, server);
    }
    wl_event_source_timer_update(server->error_dismiss_timer, 8000);
}

void
hide_config_error(nnwm_server *server)
{
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->error_bar)
            wlr_scene_node_set_enabled(&out->error_bar->node, false);
    }
    if (server->error_dismiss_timer) {
        wl_event_source_timer_update(server->error_dismiss_timer, 0);
    }
}
