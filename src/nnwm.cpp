#include "nnwm.hpp"

#include "nnwm_internal.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
extern "C"
{
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/pass.h>
}

namespace
{

/* ---- Custom CPU-side wlr_buffer for titlebar pixels ---- */

struct nnwm_tbuf
{
    struct wlr_buffer base;
    uint8_t *data;
    int stride;
};

static void
tbuf_destroy(struct wlr_buffer *b)
{
    nnwm_tbuf *tb = wl_container_of(b, tb, base);
    free(tb->data);
    free(tb);
}
static bool
tbuf_begin_data_ptr_access(struct wlr_buffer *b, uint32_t /*flags*/,
                           void **data, uint32_t *format, size_t *stride)
{
    nnwm_tbuf *tb = wl_container_of(b, tb, base);
    *data         = tb->data;
    *format       = DRM_FORMAT_ARGB8888;
    *stride       = (size_t)tb->stride;
    return true;
}
static void
tbuf_end_data_ptr_access(struct wlr_buffer * /*b*/)
{
}

static const wlr_buffer_impl tbuf_impl = {
    tbuf_destroy,
    nullptr,
    nullptr,
    tbuf_begin_data_ptr_access,
    tbuf_end_data_ptr_access,
};

static nnwm_tbuf *
tbuf_create(int w, int h)
{
    auto *tb   = static_cast<nnwm_tbuf *>(calloc(1, sizeof(nnwm_tbuf)));
    tb->stride = w * 4;
    tb->data   = static_cast<uint8_t *>(calloc(h, tb->stride));
    wlr_buffer_init(&tb->base, &tbuf_impl, w, h);
    return tb;
}

} /* anonymous namespace */

/* ---- Titlebar rendering ---- */

void
render_titlebar(nnwm_toplevel *tl, int inner_width, bool focused)
{
    nnwm_config *cfg = tl->server->config;
    int h            = cfg->titlebar.height;
    if (h <= 0 || !tl->titlebar || inner_width <= 0)
        return;

    tl->titlebar_width = inner_width;

    float scale = (tl->output && tl->output->wlr_output)
                      ? tl->output->wlr_output->scale : 1.0f;
    int pw = (int)(inner_width * scale);
    int ph = (int)(h * scale);

    nnwm_tbuf *tb = tbuf_create(pw, ph);

    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        tb->data, CAIRO_FORMAT_ARGB32, pw, ph, tb->stride);
    cairo_t *cr = cairo_create(surf);
    cairo_scale(cr, scale, scale);

    /* Background */
    const float *bg = tl->urgent         ? cfg->titlebar.urgent_bg_color
                      : focused          ? cfg->titlebar.focused_bg_color
                                         : cfg->titlebar.bg_color;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    /* Title text */
    const char *title = tl_title(tl);
    if (title && title[0])
    {
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *fd
            = pango_font_description_from_string(cfg->titlebar.font);
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
        if (cfg->titlebar.text_align == 0)
            align = PANGO_ALIGN_LEFT;
        else if (cfg->titlebar.text_align == 2)
            align = PANGO_ALIGN_RIGHT;
        pango_layout_set_alignment(layout, align);

        int pw, ph;
        pango_layout_get_size(layout, &pw, &ph);
        double ty = (h - ph / (double)PANGO_SCALE) / 2.0;

        const float *tc = tl->urgent        ? cfg->titlebar.urgent_text_color
                          : focused         ? cfg->titlebar.focused_text_color
                                           : cfg->titlebar.text_color;
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

/* Forward declaration for set_opacity_recursive (defined later in scenefx
 * block) */
#ifdef HAVE_SCENEFX
static void
set_opacity_recursive(struct wlr_scene_tree *tree, float opacity);
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
    switch (e)
    {
        case nnwm_easing::LINEAR:
            return t;
        case nnwm_easing::IN:
            return t * t * t;
        default:
        case nnwm_easing::OUT:
        {
            float f = 1.0f - t;
            return 1.0f - f * f * f;
        }
        case nnwm_easing::IN_OUT:
            return t < 0.5f ? 4.0f * t * t * t
                            : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
        case nnwm_easing::BOUNCE:
        {
            const float n1 = 7.5625f, d1 = 2.75f;
            if (t < 1.0f / d1)
                return n1 * t * t;
            if (t < 2.0f / d1)
            {
                t -= 1.5f / d1;
                return n1 * t * t + 0.75f;
            }
            if (t < 2.5f / d1)
            {
                t -= 2.25f / d1;
                return n1 * t * t + 0.9375f;
            }
            {
                t -= 2.625f / d1;
                return n1 * t * t + 0.984375f;
            }
        }
        case nnwm_easing::ELASTIC:
        {
            const float c4 = (2.0f * (float)M_PI) / 3.0f;
            if (t <= 0.0f)
                return 0.0f;
            if (t >= 1.0f)
                return 1.0f;
            return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4)
                   + 1.0f;
        }
    }
}

static float
anim_t(double t0, double now, int duration_ms, nnwm_easing easing)
{
    if (duration_ms <= 0)
        return 1.0f;
    float t = (float)((now - t0) / (duration_ms * 0.001));
    return t >= 1.0f ? 1.0f : apply_easing(easing, t);
}

static float
lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
static int
lerpi(int a, int b, float t)
{
    return a + (int)roundf((float)(b - a) * t);
}
#endif /* HAVE_SCENEFX */

/* ---- Borders and surface placement ---- */

#ifdef HAVE_SCENEFX
static void
set_corner_radii_recursive(struct wlr_scene_tree *tree, struct fx_corner_radii radii)
{
    struct wlr_scene_node *child;
    wl_list_for_each(child, &tree->children, link)
    {
        if (child->type == WLR_SCENE_NODE_BUFFER)
            wlr_scene_buffer_set_corner_radii(
                wlr_scene_buffer_from_node(child), radii);
        else if (child->type == WLR_SCENE_NODE_TREE)
            set_corner_radii_recursive(wlr_scene_tree_from_node(child), radii);
    }
}

static void
set_corner_radius_recursive(struct wlr_scene_tree *tree, int radius)
{
    set_corner_radii_recursive(tree, corner_radii_all(radius));
}

static int
effective_corner_radius(nnwm_toplevel *tl)
{
    nnwm_config *cfg = tl->server->config;
    int r = cfg->fx.rounding.radius;
    if (tl->fullscreen)
        return 0;
    if (cfg->fx.rounding.smart && tl->output)
        return (ws_count(tl->server, tl->output) == 1) ? 0 : r;
    return r;
}
#endif

void
update_borders(nnwm_toplevel *toplevel, int width, int height, int bw)
{
    nnwm_config *cfg = toplevel->server->config;
    int th           = cfg->titlebar.height;
#ifdef HAVE_SCENEFX
    int r = effective_corner_radius(toplevel);
#else
    /* Non-scenefx: four strips form the border frame. */
    int sh = height - 2 * bw;

    wlr_scene_node_set_position(&toplevel->border[0]->node, 0, 0);
    wlr_scene_rect_set_size(toplevel->border[0], width, bw);

    wlr_scene_node_set_position(&toplevel->border[1]->node, 0, height - bw);
    wlr_scene_rect_set_size(toplevel->border[1], width, bw);

    wlr_scene_node_set_position(&toplevel->border[2]->node, 0, bw);
    wlr_scene_rect_set_size(toplevel->border[2], bw, sh);

    wlr_scene_node_set_position(&toplevel->border[3]->node, width - bw, bw);
    wlr_scene_rect_set_size(toplevel->border[3], bw, sh);

    int r = 0;
#endif

    /* In tabbed layout, tiled windows never show their per-window titlebar
     * (the shared tab bar replaces it), and the surface sits at (bw, bw)
     * rather than (bw, bw+th). Check here so every update_borders call path
     * (initial layout, animation frames, open/close anim) is consistent. */
    bool tabbed_tiled = !toplevel->floating && toplevel->output
        && toplevel->output->layout_mode[toplevel->workspace]
               == nnwm_layout_mode::TABBED;
    bool no_titlebar = tabbed_tiled || toplevel->fullscreen
                       || toplevel->fake_fullscreen;

    if (toplevel->titlebar)
    {
        bool enabled = (th > 0) && !no_titlebar;
        wlr_scene_node_set_enabled(&toplevel->titlebar->node, enabled);
        wlr_scene_node_set_position(&toplevel->titlebar->node, bw, bw);
    }

    /* window surface is pushed down by the titlebar (omitted in tabbed/fullscreen) */
    wlr_scene_node_set_position(&toplevel->scene_surface->node,
                                bw, bw + (no_titlebar ? 0 : th));

#ifdef HAVE_SCENEFX
    {
    int inner_r       = r > bw ? r - bw : 0;
    bool titlebar_shown = toplevel->titlebar && (th > 0) && !no_titlebar;
    nnwm_tab_position tab_pos = cfg->layout.tab_position;

    /* Corner radii vary by tab bar position: round the corners that are
     * NOT adjacent to the tab bar. */
    struct fx_corner_radii bg_radii, surf_radii;
    if (tabbed_tiled)
    {
        switch (tab_pos)
        {
            case nnwm_tab_position::BOTTOM:
                bg_radii   = corner_radii_top(r);
                surf_radii = corner_radii_top(inner_r);
                break;
            case nnwm_tab_position::LEFT:
                bg_radii   = corner_radii_right(r);
                surf_radii = corner_radii_right(inner_r);
                break;
            case nnwm_tab_position::RIGHT:
                bg_radii   = corner_radii_left(r);
                surf_radii = corner_radii_left(inner_r);
                break;
            default: /* TOP */
                bg_radii   = corner_radii_bottom(r);
                surf_radii = corner_radii_bottom(inner_r);
                break;
        }
    }
    else
    {
        bg_radii   = corner_radii_all(r);
        surf_radii = titlebar_shown ? corner_radii_bottom(inner_r)
                                    : corner_radii_all(inner_r);
    }

    if (toplevel->border_bg)
    {
        wlr_scene_node_set_position(&toplevel->border_bg->node, 0, 0);
        wlr_scene_rect_set_size(toplevel->border_bg, width, height);
        wlr_scene_rect_set_corner_radii(toplevel->border_bg, bg_radii);
    }
    if (toplevel->titlebar)
    {
        if (titlebar_shown)
            wlr_scene_buffer_set_corner_radii(toplevel->titlebar,
                                              corner_radii_top(inner_r));
        else
            wlr_scene_buffer_set_corner_radius(toplevel->titlebar, 0);
    }
    if (toplevel->scene_surface)
    {
        if (tabbed_tiled || titlebar_shown)
            set_corner_radii_recursive(toplevel->scene_surface, surf_radii);
        else
            set_corner_radius_recursive(toplevel->scene_surface, inner_r);
    }
    }
    if (toplevel->fx_blur)
    {
        wlr_scene_node_set_position(&toplevel->fx_blur->node, 0, 0);
        wlr_scene_blur_set_size(toplevel->fx_blur, width, height);
    }
    if (toplevel->fx_shadow)
    {
        wlr_scene_shadow_set_size(toplevel->fx_shadow, width, height);
        wlr_scene_node_set_position(&toplevel->fx_shadow->node,
                                    (int)cfg->fx.shadow_offset_x,
                                    (int)cfg->fx.shadow_offset_y);
    }
#endif
}

/* ---- Animation functions ---- */

void
tl_set_geometry(nnwm_toplevel *tl, int x, int y, int w, int h, int bw)
{
    nnwm_config *cfg = tl->server->config;
#ifdef HAVE_SCENEFX
    bool do_anim = cfg->fx.animation.enabled && cfg->fx.animation.duration_ms > 0
                   && !(tl->output && tl->output->overview);
    bool first   = (tl->cur_w == 0 && tl->cur_h == 0);
    bool changed = (x != tl->cur_x || y != tl->cur_y || w != tl->cur_w
                    || h != tl->cur_h);

    tl->geo_to_x = x;
    tl->geo_to_y = y;
    tl->geo_to_w = w;
    tl->geo_to_h = h;
    tl->geo_bw   = bw;

    if (do_anim && changed)
    {
        if (first)
        {
            /* First layout: open style determines the from-position.
             * tl_open_anim() will override duration/easing for the open anim;
             * for non-first layouts we use the layout easing/duration. */
            nnwm_open_style open_style
                = tl->rule_no_anim == 1
                      ? nnwm_open_style::NONE
                      : tl->rule_anim_open_style >= 0
                            ? static_cast<nnwm_open_style>(
                                  tl->rule_anim_open_style)
                            : cfg->fx.animation.open_style;
            int sw = (int)(w * 0.95f), sh = (int)(h * 0.95f);
            switch (open_style)
            {
                default:
                case nnwm_open_style::FADE_SCALE:
                case nnwm_open_style::SCALE:
                    tl->geo_from_x = x + (w - sw) / 2;
                    tl->geo_from_y = y + (h - sh) / 2;
                    tl->geo_from_w = sw;
                    tl->geo_from_h = sh;
                    break;
                case nnwm_open_style::SLIDE_UP:
                    tl->geo_from_x = x;
                    tl->geo_from_y = y + h;
                    tl->geo_from_w = w;
                    tl->geo_from_h = h;
                    break;
                case nnwm_open_style::SLIDE_DOWN:
                    tl->geo_from_x = x;
                    tl->geo_from_y = y - h;
                    tl->geo_from_w = w;
                    tl->geo_from_h = h;
                    break;
                case nnwm_open_style::SLIDE_LEFT:
                    tl->geo_from_x = x + w;
                    tl->geo_from_y = y;
                    tl->geo_from_w = w;
                    tl->geo_from_h = h;
                    break;
                case nnwm_open_style::SLIDE_RIGHT:
                    tl->geo_from_x = x - w;
                    tl->geo_from_y = y;
                    tl->geo_from_w = w;
                    tl->geo_from_h = h;
                    break;
                case nnwm_open_style::FADE:
                case nnwm_open_style::NONE:
                    tl->geo_from_x = x;
                    tl->geo_from_y = y;
                    tl->geo_from_w = w;
                    tl->geo_from_h = h;
                    break;
            }
        }
        else
        {
            /* Skip geo animation if layout anim is disabled */
            if (cfg->fx.animation.layout_style == nnwm_layout_anim::NONE)
            {
                tl->geo_anim = false;
                tl->cur_x    = x;
                tl->cur_y    = y;
                tl->cur_w    = w;
                tl->cur_h    = h;
                wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
                update_borders(tl, w, h, bw);
                return;
            }
            tl->geo_from_x = tl->cur_x;
            tl->geo_from_y = tl->cur_y;
            tl->geo_from_w = tl->cur_w;
            tl->geo_from_h = tl->cur_h;
        }
        tl->geo_anim        = true;
        tl->geo_t0          = anim_now();
        tl->geo_then_hide   = false;
        /* Bake layout easing/duration; tl_open_anim() will override for first
         * layout */
        tl->geo_duration_ms = eff_duration(cfg, cfg->fx.animation.layout_duration_ms);
        tl->geo_easing      = eff_easing(cfg, cfg->fx.animation.layout_easing);
        wlr_scene_node_set_position(&tl->scene_tree->node, tl->geo_from_x,
                                    tl->geo_from_y);
        update_borders(tl, tl->geo_from_w, tl->geo_from_h, bw);
        tl->cur_x = tl->geo_from_x;
        tl->cur_y = tl->geo_from_y;
        tl->cur_w = tl->geo_from_w;
        tl->cur_h = tl->geo_from_h;
        return;
    }
    // fall-through: instant apply
    tl->geo_anim = false;
#endif /* HAVE_SCENEFX */
    // instant apply
    tl->cur_x = x;
    tl->cur_y = y;
    tl->cur_w = w;
    tl->cur_h = h;
    wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
    update_borders(tl, w, h, bw);
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland && tl->xwayland_surface)
    {
        /* Compute the inner content area: strip borders and titlebar */
        nnwm_config *cfg2 = tl->server->config;
        bool tabbed2 = !tl->floating && tl->output
            && tl->output->layout_mode[tl->workspace] == nnwm_layout_mode::TABBED;
        bool no_titlebar2 = tabbed2 || tl->fullscreen || tl->fake_fullscreen;
        int eff_bw = bw;
        int eff_th = (no_titlebar2 || cfg2->titlebar.height <= 0) ? 0 : cfg2->titlebar.height;
        int cx = x + eff_bw;
        int cy = y + eff_bw + eff_th;
        int cw = w - 2 * eff_bw;
        int ch = h - 2 * eff_bw - eff_th;
        if (cw < 1) cw = 1;
        if (ch < 1) ch = 1;
        nnwm_xw_configure(tl->xwayland_surface,
                          (int16_t)cx, (int16_t)cy,
                          (uint16_t)cw, (uint16_t)ch);
    }
#endif
}

#ifdef HAVE_SCENEFX
void
tl_start_fade(nnwm_toplevel *tl, float from, float to, int duration_ms,
              nnwm_easing easing)
{
    tl->fade_from        = from;
    tl->fade_to          = to;
    tl->fade_duration_ms = duration_ms;
    tl->fade_easing      = easing;
    if (tl->server->config->fx.animation.enabled && duration_ms > 0)
    {
        tl->fade_anim = true;
        tl->fade_t0   = anim_now();
        set_opacity_recursive(tl->scene_surface, from);
    }
    else
    {
        tl->fade_anim = false;
        set_opacity_recursive(tl->scene_surface, to);
    }
}

void
tl_open_anim(nnwm_toplevel *tl)
{
    nnwm_config *cfg = tl->server->config;
    if (!cfg->fx.animation.enabled || (tl->output && tl->output->overview))
        return;

    nnwm_open_style open_style
        = tl->rule_no_anim == 1
              ? nnwm_open_style::NONE
              : tl->rule_anim_open_style >= 0
                    ? static_cast<nnwm_open_style>(tl->rule_anim_open_style)
                    : cfg->fx.animation.open_style;
    int dur          = eff_duration(cfg, cfg->fx.animation.open_duration_ms);
    nnwm_easing ease = eff_easing(cfg, cfg->fx.animation.open_easing);

    float target_op
        = (tl->rule_opacity >= 0.0f) ? tl->rule_opacity : cfg->fx.opacity;

    bool do_fade = (open_style != nnwm_open_style::SCALE
                    && open_style != nnwm_open_style::NONE);
    bool do_scale = (open_style == nnwm_open_style::FADE_SCALE
                     || open_style == nnwm_open_style::SCALE);
    bool do_slide = (open_style >= nnwm_open_style::SLIDE_UP
                     && open_style <= nnwm_open_style::SLIDE_RIGHT);

    if (do_fade)
        tl_start_fade(tl, 0.0f, target_op, dur, ease);
    else
    {
        /* Set opacity directly to target */
        set_opacity_recursive(tl->scene_surface, target_op);
    }

    if (open_style == nnwm_open_style::NONE)
    {
        tl->geo_anim = false;
        /* Apply target position immediately */
        wlr_scene_node_set_position(&tl->scene_tree->node, tl->geo_to_x,
                                    tl->geo_to_y);
        update_borders(tl, tl->geo_to_w, tl->geo_to_h, tl->geo_bw);
        tl->cur_x = tl->geo_to_x;
        tl->cur_y = tl->geo_to_y;
        tl->cur_w = tl->geo_to_w;
        tl->cur_h = tl->geo_to_h;
        return;
    }
    if (!do_scale && !do_slide)
    {
        /* OPEN_FADE: cancel geo animation */
        tl->geo_anim = false;
        wlr_scene_node_set_position(&tl->scene_tree->node, tl->geo_to_x,
                                    tl->geo_to_y);
        update_borders(tl, tl->geo_to_w, tl->geo_to_h, tl->geo_bw);
        tl->cur_x = tl->geo_to_x;
        tl->cur_y = tl->geo_to_y;
        tl->cur_w = tl->geo_to_w;
        tl->cur_h = tl->geo_to_h;
        return;
    }
    /* For scale + slide, from-position is already set in tl_set_geometry — bake
     * easing */
    if (tl->geo_anim)
    {
        tl->geo_duration_ms = dur;
        tl->geo_easing      = ease;
    }
}

void
tl_close_anim(nnwm_toplevel *tl)
{
    nnwm_config *cfg = tl->server->config;
    if (!cfg->fx.animation.enabled || cfg->fx.animation.duration_ms <= 0)
    {
        tl->dying = false;
        return;
    }

    nnwm_open_style close_style
        = tl->rule_no_anim == 1
              ? nnwm_open_style::NONE
              : tl->rule_anim_close_style >= 0
                    ? static_cast<nnwm_open_style>(tl->rule_anim_close_style)
                    : cfg->fx.animation.close_style;
    int dur          = eff_duration(cfg, cfg->fx.animation.close_duration_ms);
    nnwm_easing ease = eff_easing(cfg, cfg->fx.animation.close_easing);

    float cur_op = (tl->rule_opacity >= 0.0f) ? tl->rule_opacity : cfg->fx.opacity;

    bool do_fade = (close_style != nnwm_open_style::SCALE
                    && close_style != nnwm_open_style::NONE);
    bool do_slide = (close_style >= nnwm_open_style::SLIDE_UP
                     && close_style <= nnwm_open_style::SLIDE_RIGHT);

    if (do_fade)
        tl_start_fade(tl, cur_op, 0.0f, dur, ease);

    if (do_slide)
    {
        int dx = 0, dy = 0;
        int w = tl->cur_w, h = tl->cur_h;
        switch (close_style)
        {
            case nnwm_open_style::SLIDE_UP:
                dy = -h;
                break;
            case nnwm_open_style::SLIDE_DOWN:
                dy = +h;
                break;
            case nnwm_open_style::SLIDE_LEFT:
                dx = -w;
                break;
            case nnwm_open_style::SLIDE_RIGHT:
                dx = +w;
                break;
            default:
                break;
        }
        tl->geo_from_x      = tl->cur_x;
        tl->geo_from_y      = tl->cur_y;
        tl->geo_from_w      = tl->cur_w;
        tl->geo_from_h      = tl->cur_h;
        tl->geo_to_x        = tl->cur_x + dx;
        tl->geo_to_y        = tl->cur_y + dy;
        tl->geo_to_w        = tl->cur_w;
        tl->geo_to_h        = tl->cur_h;
        tl->geo_duration_ms = dur;
        tl->geo_easing      = ease;
        tl->geo_anim        = true;
        tl->geo_t0          = anim_now();
        tl->geo_then_hide   = false;
    }

    if (close_style == nnwm_open_style::NONE)
    {
        tl->dying = false;
    }
    else
    {
        tl->dying = true;
    }
}

void
tl_start_border_color(nnwm_toplevel *tl, const float to[4])
{
    nnwm_config *cfg    = tl->server->config;
    nnwm_focus_style fs = cfg->fx.animation.focus_style;

    if (!cfg->fx.animation.enabled || fs == nnwm_focus_style::NONE
        || (tl->output && tl->output->overview))
    {
        for (int i = 0; i < 4; i++)
            tl->bcol_to[i] = to[i];
        tl->bcol_anim = false;
        if (tl->border_bg)
            wlr_scene_rect_set_color(tl->border_bg, to);
        return;
    }
    const float *cur = tl->border_bg ? tl->border_bg->color : to;
    for (int i = 0; i < 4; i++)
        tl->bcol_from[i] = tl->bcol_anim ? tl->bcol_to[i] : cur[i];
    for (int i = 0; i < 4; i++)
        tl->bcol_to[i] = to[i];
    tl->bcol_duration_ms = eff_duration(cfg, cfg->fx.animation.focus_duration_ms);
    tl->bcol_easing      = eff_easing(cfg, cfg->fx.animation.focus_easing);
    tl->bcol_anim        = true;
    tl->bcol_t0          = anim_now();
}

static bool
animate_step_one(nnwm_server * /*server*/, nnwm_toplevel *tl, double now)
{
    bool active = false;
    if (tl->geo_anim)
    {
        float t = anim_t(tl->geo_t0, now, tl->geo_duration_ms, tl->geo_easing);
        int cx  = lerpi(tl->geo_from_x, tl->geo_to_x, t);
        int cy  = lerpi(tl->geo_from_y, tl->geo_to_y, t);
        int cw  = lerpi(tl->geo_from_w, tl->geo_to_w, t);
        int ch  = lerpi(tl->geo_from_h, tl->geo_to_h, t);
        wlr_scene_node_set_position(&tl->scene_tree->node, cx, cy);
        update_borders(tl, cw, ch, tl->geo_bw);
        tl->cur_x = cx;
        tl->cur_y = cy;
        tl->cur_w = cw;
        tl->cur_h = ch;

        /* Hide when the animated rect leaves the home output entirely.
         * Per-output bleed prevention (partial overlap) is handled in
         * output_frame before each output's commit. */
        if (tl->output)
        {
            wlr_box ob;
            wlr_output_layout_get_box(tl->server->output_layout,
                                      tl->output->wlr_output, &ob);
            bool inside = (cx + cw > ob.x && cx < ob.x + ob.width &&
                           cy + ch > ob.y && cy < ob.y + ob.height);
            wlr_scene_node_set_enabled(&tl->scene_tree->node, inside);
        }

        if (t >= 1.0f)
        {
            tl->geo_anim = false;
            if (tl->geo_then_hide)
            {
                wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
                tl->cur_x = tl->geo_from_x;
                tl->cur_y = tl->geo_from_y;
                tl->cur_w = tl->geo_from_w;
                tl->cur_h = tl->geo_from_h;
            }
            else if (tl->dying && !tl->fade_anim)
            {
                wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
            }
        }
        active = true;
    }
    if (tl->fade_anim)
    {
        float t
            = anim_t(tl->fade_t0, now, tl->fade_duration_ms, tl->fade_easing);
        set_opacity_recursive(tl->scene_surface,
                              lerpf(tl->fade_from, tl->fade_to, t));
        if (t >= 1.0f)
        {
            tl->fade_anim = false;
            if (tl->dying && !tl->geo_anim)
                wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
        }
        active = true;
    }
    if (tl->bcol_anim)
    {
        float t
            = anim_t(tl->bcol_t0, now, tl->bcol_duration_ms, tl->bcol_easing);
        float col[4];
        for (int i = 0; i < 4; i++)
            col[i] = lerpf(tl->bcol_from[i], tl->bcol_to[i], t);
        if (tl->border_bg)
            wlr_scene_rect_set_color(tl->border_bg, col);
        if (t >= 1.0f)
            tl->bcol_anim = false;
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
set_opacity_recursive(struct wlr_scene_tree *tree, float opacity)
{
    struct wlr_scene_node *child;
    wl_list_for_each(child, &tree->children, link)
    {
        if (child->type == WLR_SCENE_NODE_BUFFER)
            wlr_scene_buffer_set_opacity(wlr_scene_buffer_from_node(child),
                                         opacity);
        else if (child->type == WLR_SCENE_NODE_TREE)
            set_opacity_recursive(wlr_scene_tree_from_node(child), opacity);
    }
}
#endif

void
apply_fx_decorations(nnwm_toplevel *toplevel)
{
#ifdef HAVE_SCENEFX
    nnwm_config *cfg = toplevel->server->config;

    int r = effective_corner_radius(toplevel);

    bool tabbed_tiled = !toplevel->floating && toplevel->output
        && toplevel->output->layout_mode[toplevel->workspace]
               == nnwm_layout_mode::TABBED;

    /* border_bg: full-window rect that provides correctly rounded outer
     * corners. The 4 border strips are inset by r (see update_borders).
     * Radius, inner_r, and titlebar radius are all maintained by
     * update_borders (which uses effective_corner_radius). Here we only
     * create the node on first use. */
    if (!toplevel->border_bg)
    {
        toplevel->border_bg = wlr_scene_rect_create(toplevel->scene_tree, 0, 0,
                                                    toplevel->border[0]->color);
        wlr_scene_node_lower_to_bottom(&toplevel->border_bg->node);
        /* border_bg is the sole border element in HAVE_SCENEFX — hide strips */
        for (int i = 0; i < 4; i++)
            wlr_scene_node_set_enabled(&toplevel->border[i]->node, false);
    }

    /* Per-window overrides take precedence over global config values */
    float eff_opacity = (toplevel->rule_opacity >= 0.0f)
                            ? toplevel->rule_opacity
                            : cfg->fx.opacity;
    /* Apply focused / unfocused override if set */
    wlr_surface *focused_surface
        = toplevel->server->seat->keyboard_state.focused_surface;
    bool focused = focused_surface
                   && wlr_xdg_toplevel_try_from_wlr_surface(focused_surface)
                       == toplevel->xdg_toplevel;
    if (focused && cfg->fx.focused_opacity >= 0.0f)
        eff_opacity = cfg->fx.focused_opacity;
    else if (!focused && cfg->fx.unfocused_opacity >= 0.0f)
        eff_opacity = cfg->fx.unfocused_opacity;
    bool eff_blur     = (toplevel->rule_blur >= 0) ? (bool)toplevel->rule_blur
                                                   : cfg->fx.blur_enabled;

    /* Window content opacity */
    if (toplevel->scene_surface)
        set_opacity_recursive(toplevel->scene_surface, eff_opacity);

    /* Background blur */
    if (eff_blur)
    {
        wlr_scene_set_blur_data(toplevel->server->scene, cfg->fx.blur_passes,
                                cfg->fx.blur_radius, cfg->fx.blur_noise,
                                cfg->fx.blur_brightness, cfg->fx.blur_contrast,
                                cfg->fx.blur_saturation);
        if (!toplevel->fx_blur)
        {
            int bw      = cfg->border.width;
            int th      = cfg->titlebar.height;
            int w, h;
#ifdef HAVE_XWAYLAND
            if (toplevel->is_xwayland) {
                w = (toplevel->cur_w > 0) ? toplevel->cur_w : nnwm_xw_width(toplevel->xwayland_surface) + 2 * bw;
                h = (toplevel->cur_h > 0) ? toplevel->cur_h : nnwm_xw_height(toplevel->xwayland_surface) + 2 * bw + th;
            } else
#endif
            {
                wlr_box geo = toplevel->xdg_toplevel->base->geometry;
                w = geo.width + 2 * bw;
                h = geo.height + 2 * bw + th;
            }
            toplevel->fx_blur
                = wlr_scene_blur_create(toplevel->scene_tree, w, h);
            if (tabbed_tiled)
                wlr_scene_blur_set_corner_radii(toplevel->fx_blur,
                                                corner_radii_bottom(r));
            else
                wlr_scene_blur_set_corner_radius(toplevel->fx_blur, r);
            wlr_scene_node_lower_to_bottom(&toplevel->fx_blur->node);
        }
        else
        {
            if (tabbed_tiled)
                wlr_scene_blur_set_corner_radii(toplevel->fx_blur,
                                                corner_radii_bottom(r));
            else
                wlr_scene_blur_set_corner_radius(toplevel->fx_blur, r);
        }
    }
    else if (!eff_blur && toplevel->fx_blur)
    {
        wlr_scene_node_destroy(&toplevel->fx_blur->node);
        toplevel->fx_blur = nullptr;
    }

    /* Shadow node */
    if (cfg->fx.shadow_enabled && !toplevel->fx_shadow)
    {
        int bw              = cfg->border.width;
        int th              = cfg->titlebar.height;
        int w, h;
#ifdef HAVE_XWAYLAND
        if (toplevel->is_xwayland) {
            w = (toplevel->cur_w > 0) ? toplevel->cur_w : nnwm_xw_width(toplevel->xwayland_surface) + 2 * bw;
            h = (toplevel->cur_h > 0) ? toplevel->cur_h : nnwm_xw_height(toplevel->xwayland_surface) + 2 * bw + th;
        } else
#endif
        {
            wlr_box geo = toplevel->xdg_toplevel->base->geometry;
            w           = geo.width + 2 * bw;
            h           = geo.height + 2 * bw + th;
        }
        toplevel->fx_shadow = wlr_scene_shadow_create(
            toplevel->scene_tree, w, h, r,
            cfg->fx.shadow_blur_sigma, cfg->fx.shadow_color);
        wlr_scene_node_set_position(&toplevel->fx_shadow->node,
                                    (int)cfg->fx.shadow_offset_x,
                                    (int)cfg->fx.shadow_offset_y);
        wlr_scene_node_lower_to_bottom(&toplevel->fx_shadow->node);
    }
    else if (toplevel->fx_shadow)
    {
        if (!cfg->fx.shadow_enabled)
        {
            wlr_scene_node_destroy(&toplevel->fx_shadow->node);
            toplevel->fx_shadow = nullptr;
        }
        else
        {
            wlr_scene_shadow_set_corner_radius(toplevel->fx_shadow, r);
            wlr_scene_shadow_set_blur_sigma(toplevel->fx_shadow,
                                            cfg->fx.shadow_blur_sigma);
            wlr_scene_shadow_set_color(toplevel->fx_shadow, cfg->fx.shadow_color);
            wlr_scene_node_set_position(&toplevel->fx_shadow->node,
                                        (int)cfg->fx.shadow_offset_x,
                                        (int)cfg->fx.shadow_offset_y);
        }
    }
#else
    (void)toplevel;
#endif
}

/* ---- Tabbed layout tab bar rendering ---- */

/* Render a single tab's text, centered in (tx, ty, tw, th) of the buffer.
 * For vertical tabs the cairo context must already be set up with the
 * appropriate rotation/translation. */
static void
draw_tab_text(cairo_t *cr, nnwm_config *cfg, const char *title,
              double tx, double ty, double tw, double th,
              const float *tc)
{
    if (!title || !title[0] || tw < 4 || th < 4)
        return;

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(
        cfg->titlebar.font ? cfg->titlebar.font : "Sans 10");
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
    pango_layout_set_width(layout, (int)((tw - 8) * PANGO_SCALE));
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

    int pw, ph;
    pango_layout_get_size(layout, &pw, &ph);
    double text_y = ty + (th - ph / (double)PANGO_SCALE) / 2.0;

    cairo_set_source_rgba(cr, tc[0], tc[1], tc[2], tc[3]);
    cairo_move_to(cr, tx + 4, text_y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

void
render_tab_bar(nnwm_server *server, nnwm_output *out, int width, int height)
{
    if (!out->tab_bar || width <= 0 || height <= 0)
        return;

    nnwm_config *cfg     = server->config;
    int ws               = out->active_workspace;
    nnwm_tab_style style = cfg->layout.tab_style;
    nnwm_tab_position pos = cfg->layout.tab_position;
    bool vertical        = (pos == nnwm_tab_position::LEFT
                             || pos == nnwm_tab_position::RIGHT);

    int n = ws_count(server, out);
    if (n == 0)
    {
        wlr_scene_node_set_enabled(&out->tab_bar->node, false);
        return;
    }

    nnwm_toplevel *active_tl = out->last_focused[ws];
    if (active_tl
        && (active_tl->output != out || active_tl->workspace != ws
            || active_tl->floating || active_tl->fullscreen
            || active_tl->fake_fullscreen))
        active_tl = nullptr;

    float scale = (out->wlr_output) ? out->wlr_output->scale : 1.0f;
    int pw      = (int)(width * scale);
    int ph      = (int)(height * scale);

    nnwm_tbuf *tb         = tbuf_create(pw, ph);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        tb->data, CAIRO_FORMAT_ARGB32, pw, ph, tb->stride);
    cairo_t *cr = cairo_create(surf);
    cairo_scale(cr, scale, scale);

    const float *dflt_bg = cfg->titlebar.bg_color;
    cairo_set_source_rgba(cr, dflt_bg[0], dflt_bg[1], dflt_bg[2], dflt_bg[3]);
    cairo_paint(cr);

    int i = 0;
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (tl->output != out || tl->workspace != ws || tl->floating
            || tl->fullscreen || tl->fake_fullscreen)
            continue;

        bool focused = (tl == active_tl);

        const float *bg = tl->urgent ? cfg->titlebar.urgent_bg_color
                          : focused  ? cfg->titlebar.focused_bg_color
                                     : cfg->titlebar.bg_color;
        const float *tc = tl->urgent ? cfg->titlebar.urgent_text_color
                          : focused  ? cfg->titlebar.focused_text_color
                                     : cfg->titlebar.text_color;

        if (!vertical)
        {
            /* Horizontal bar (TOP / BOTTOM) */
            int x = (int)((long)i * width / n);
            int w = (int)((long)(i + 1) * width / n) - x;

            cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
            cairo_rectangle(cr, x, 0, w, height);
            cairo_fill(cr);

            if (i < n - 1)
            {
                cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
                cairo_rectangle(cr, x + w - 1, 0, 1, height);
                cairo_fill(cr);
            }

            if (style == nnwm_tab_style::NORMAL)
                draw_tab_text(cr, cfg, tl_title(tl),
                              x, 0, w, height, tc);
        }
        else
        {
            /* Vertical bar (LEFT / RIGHT) — tabs stacked top-to-bottom */
            int y = (int)((long)i * height / n);
            int h = (int)((long)(i + 1) * height / n) - y;

            cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
            cairo_rectangle(cr, 0, y, width, h);
            cairo_fill(cr);

            if (i < n - 1)
            {
                cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
                cairo_rectangle(cr, 0, y + h - 1, width, 1);
                cairo_fill(cr);
            }

            if (style == nnwm_tab_style::NORMAL)
            {
                /* Rotate text 90° CCW so it reads bottom-to-top */
                cairo_save(cr);
                cairo_translate(cr, width / 2.0, y + h / 2.0);
                cairo_rotate(cr, -M_PI / 2.0);
                draw_tab_text(cr, cfg, tl_title(tl),
                              -h / 2.0, -width / 2.0, h, width, tc);
                cairo_restore(cr);
            }
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

/* Re-render the tab bar at the correct dimensions for the current config,
 * used by title-change and urgent notification paths. */
void
rerender_tab_bar(nnwm_server *server, nnwm_output *out)
{
    if (!out || out->layout_mode[out->active_workspace] != nnwm_layout_mode::TABBED)
        return;

    nnwm_config *cfg = server->config;
    int ws           = out->active_workspace;
    bool solo        = (ws_count(server, out) == 1);
    if (solo && cfg->layout.tab_smart)
    {
        if (out->tab_bar)
            wlr_scene_node_set_enabled(&out->tab_bar->node, false);
        return;
    }
    int og           = (solo && cfg->gap.smart) ? 0 : cfg->gap.outer;
    int tab_sz       = cfg->layout.tab_bar_height > 0 ? cfg->layout.tab_bar_height : 24;
    const wlr_box &area = out->usable_area;

    int tbw, tbh, tbx, tby;
    switch (cfg->layout.tab_position)
    {
        case nnwm_tab_position::BOTTOM:
            tbw = area.width - 2 * og;
            tbh = tab_sz;
            tbx = area.x + og;
            tby = area.y + area.height - og - tab_sz;
            break;
        case nnwm_tab_position::LEFT:
            tbw = tab_sz;
            tbh = area.height - 2 * og;
            tbx = area.x + og;
            tby = area.y + og;
            break;
        case nnwm_tab_position::RIGHT:
            tbw = tab_sz;
            tbh = area.height - 2 * og;
            tbx = area.x + area.width - og - tab_sz;
            tby = area.y + og;
            break;
        default: /* TOP */
            tbw = area.width - 2 * og;
            tbh = tab_sz;
            tbx = area.x + og;
            tby = area.y + og;
            break;
    }

    render_tab_bar(server, out, tbw, tbh);
    if (out->tab_bar)
        wlr_scene_node_set_position(&out->tab_bar->node, tbx, tby);
}

/* ---- Overview ---- */

static void arrange_windows_impl(nnwm_server *server, nnwm_output *out);

static constexpr int OVERVIEW_COLS  = 3;
static constexpr double OVERVIEW_OUTER = 32.0;
static constexpr double OVERVIEW_INNER = 12.0;

/* Returns the window's layout-space bounding box (including borders/titlebar).
 * For tiled windows this is cur_x/y/w/h; for floating windows that haven't
 * gone through tl_set_geometry yet, it falls back to the scene node position
 * plus the committed XDG geometry. Returns false if the window has no size. */
static bool
tl_layout_box(const nnwm_toplevel *tl, int bw, int th, wlr_box *out)
{
    if (tl->cur_w > 0 && tl->cur_h > 0) {
        *out = {tl->cur_x, tl->cur_y, tl->cur_w, tl->cur_h};
        return true;
    }
    if (!tl->floating) return false;
    int nx, ny;
    if (!wlr_scene_node_coords(
            const_cast<wlr_scene_node *>(&tl->scene_tree->node), &nx, &ny))
        return false;
#ifdef HAVE_XWAYLAND
    if (tl->is_xwayland)
    {
        int w = nnwm_xw_width(tl->xwayland_surface);
        int h = nnwm_xw_height(tl->xwayland_surface);
        if (w <= 0 || h <= 0) return false;
        *out = {nx, ny, w + 2 * bw, h + 2 * bw + th};
        return true;
    }
#endif
    const wlr_box *geo = &tl->xdg_toplevel->base->geometry;
    if (geo->width <= 0 || geo->height <= 0) return false;
    *out = {nx, ny, geo->width + 2 * bw, geo->height + 2 * bw + th};
    return true;
}

struct ov_surf_data {
    wlr_render_pass *pass;
    const pixman_region32_t *clip;
    int orig_x, orig_y;  /* XDG surface origin in buffer pixels */
    double win_scale;    /* logical-to-buffer-pixel scale (s * dpi) */
};

static void
ov_surface_iter(wlr_surface *surface, int sx, int sy, void *ud)
{
    auto *d   = static_cast<ov_surf_data *>(ud);
    wlr_texture *tex = wlr_surface_get_texture(surface);
    if (!tex) return;
    int dx = d->orig_x + (int)std::round(sx * d->win_scale);
    int dy = d->orig_y + (int)std::round(sy * d->win_scale);
    int dw = (int)std::round(surface->current.width  * d->win_scale);
    int dh = (int)std::round(surface->current.height * d->win_scale);
    if (dw <= 0 || dh <= 0) return;
    wlr_render_texture_options opts = {};
    opts.texture     = tex;
    opts.dst_box     = {dx, dy, dw, dh};
    opts.clip        = d->clip;
    opts.filter_mode = WLR_SCALE_FILTER_BILINEAR;
    wlr_render_pass_add_texture(d->pass, &opts);
}

static void render_overview_buffers(nnwm_server *server, nnwm_output *out);

/* Hit-test the overview to find which toplevel (if any) is under output-local
 * cursor position (cx, cy).  Returns null if over empty space.  If a toplevel
 * is found, *out_ws is set to its workspace index. */
nnwm_toplevel *
overview_toplevel_at(nnwm_server *server, nnwm_output *out,
                     double cx, double cy, int *out_ws)
{
    wlr_box ob;
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &ob);
    double W = ob.width, H = ob.height;
    if (W <= 0 || H <= 0) return nullptr;

    int num_ws  = server->config->workspace_count;
    int ov_rows = (num_ws + OVERVIEW_COLS - 1) / OVERVIEW_COLS;
    double slot_w = (W - 2.0 * OVERVIEW_OUTER - (OVERVIEW_COLS - 1) * OVERVIEW_INNER) / OVERVIEW_COLS;
    double slot_h = (H - 2.0 * OVERVIEW_OUTER - (ov_rows - 1) * OVERVIEW_INNER) / ov_rows;

    const wlr_box &ua = out->usable_area;
    double ua_w = ua.width  > 0 ? (double)ua.width  : W;
    double ua_h = ua.height > 0 ? (double)ua.height : H;
    double s    = std::min(slot_w / ua_w, slot_h / ua_h);
    double cx_off = (slot_w - ua_w * s) / 2.0;
    double cy_off = (slot_h - ua_h * s) / 2.0;

    nnwm_config *cfg = server->config;
    int bw = cfg->border.width;
    int th = cfg->titlebar.height;

    for (int ws = 0; ws < num_ws; ws++) {
        int    col = ws % OVERVIEW_COLS;
        int    row = ws / OVERVIEW_COLS;
        double sx  = OVERVIEW_OUTER + col * (slot_w + OVERVIEW_INNER);
        double sy  = OVERVIEW_OUTER + row * (slot_h + OVERVIEW_INNER);

        /* Skip if cursor is outside this slot */
        if (cx < sx || cx >= sx + slot_w || cy < sy || cy >= sy + slot_h)
            continue;

        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link) {
            if (tl->output != out) continue;
            if (tl->workspace != ws && !tl->sticky) continue;
            if (tl->in_scratchpad) continue;

            bool tabbed_tiled = !tl->floating && tl->output->layout_mode[tl->workspace]
                                                    == nnwm_layout_mode::TABBED;
            int eff_th = (tabbed_tiled || th <= 0) ? 0 : th;
            wlr_box lb;
            if (!tl_layout_box(tl, bw, eff_th, &lb)) continue;

            double wx = sx + cx_off + (lb.x - ua.x) * s;
            double wy = sy + cy_off + (lb.y - ua.y) * s;
            double ww = lb.width  * s;
            double wh = lb.height * s;

            if (cx >= wx && cx < wx + ww && cy >= wy && cy < wy + wh) {
                if (out_ws) *out_ws = ws;
                return tl;
            }
        }
        break; /* only one slot can match */
    }
    return nullptr;
}

void
render_overview(nnwm_server *server, nnwm_output *out)
{
    if (!out->overview_buf || !out->overview_labels) return;

    /* Arrange every workspace so all windows have valid cur_x/y/w/h.
     * Direct impl call avoids re-entering arrange_windows → render_overview.
     * All workspaces are arranged (including active) so that window moves in
     * overview immediately reflect correct layouts in both source and target. */
    {
        int saved = out->active_workspace;
        nnwm_toplevel *tl;
        for (int ws = 0; ws < server->config->workspace_count; ws++) {
            out->active_workspace = ws;
            arrange_windows_impl(server, out);
            /* arrange_windows_impl only enables tiled windows; also enable
             * floating windows so their textures are valid for capture below. */
            wl_list_for_each(tl, &server->toplevels, link) {
                if (tl->output != out || tl->in_scratchpad) continue;
                if (tl->floating && (tl->workspace == ws || tl->sticky))
                    wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
            }
        }
        out->active_workspace = saved;
    }

    render_overview_buffers(server, out);

    /* Hide all window scene nodes — the GPU buffer provides the display.
     * Texture capture and frame_done work on surface buffers directly. */
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->output == out && !tl->in_scratchpad)
            wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
    }
}

void
overview_frame_update(nnwm_server *server, nnwm_output *out)
{
    if (!out->overview || !out->overview_buf || !out->overview_labels) return;

#ifdef HAVE_SCENEFX
    if (out->ov_anim && server->config->fx.animation.enabled) {
        double now = anim_now();
        float t = anim_t(out->ov_anim_t0, now, out->ov_anim_duration_ms, nnwm_easing::OUT);
        if (out->ov_anim_exiting) {
            float opacity = lerpf(1.0f, 0.0f, t);
            wlr_scene_buffer_set_opacity(out->overview_buf, opacity);
            wlr_scene_buffer_set_opacity(out->overview_labels, opacity);
            if (t >= 1.0f) {
                out->ov_anim = false;
                exit_overview(server, out);
            }
            return;
        } else {
            render_overview_buffers(server, out);
            float opacity = lerpf(0.0f, 1.0f, t);
            wlr_scene_buffer_set_opacity(out->overview_buf, opacity);
            wlr_scene_buffer_set_opacity(out->overview_labels, opacity);
            if (t >= 1.0f) {
                out->ov_anim = false;
                wlr_scene_buffer_set_opacity(out->overview_buf, 1.0f);
                wlr_scene_buffer_set_opacity(out->overview_labels, 1.0f);
            }
            return;
        }
    }
#endif

    render_overview_buffers(server, out);
}

struct ov_geom {
    wlr_box out_box;
    int W, H, buf_w, buf_h, num_ws;
    double dpi, slot_w, slot_h, s, cx_off, cy_off;
};

static ov_geom
ov_geom_compute(nnwm_server *server, nnwm_output *out)
{
    ov_geom g{};
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &g.out_box);
    g.W = g.out_box.width;
    g.H = g.out_box.height;
    g.dpi   = out->wlr_output->scale;
    g.buf_w = (int)(g.W * g.dpi);
    g.buf_h = (int)(g.H * g.dpi);
    g.num_ws = server->config->workspace_count;
    int ov_rows = (g.num_ws + OVERVIEW_COLS - 1) / OVERVIEW_COLS;
    g.slot_w = (g.W - 2.0 * OVERVIEW_OUTER - (OVERVIEW_COLS - 1) * OVERVIEW_INNER) / OVERVIEW_COLS;
    g.slot_h = (g.H - 2.0 * OVERVIEW_OUTER - (ov_rows - 1) * OVERVIEW_INNER) / ov_rows;
    const wlr_box &ua = out->usable_area;
    double ua_w = ua.width  > 0 ? (double)ua.width  : (double)g.W;
    double ua_h = ua.height > 0 ? (double)ua.height : (double)g.H;
    g.s      = std::min(g.slot_w / ua_w, g.slot_h / ua_h);
    g.cx_off = (g.slot_w - ua_w * g.s) / 2.0;
    g.cy_off = (g.slot_h - ua_h * g.s) / 2.0;
    return g;
}

static void render_overview_labels(nnwm_server *server, nnwm_output *out,
                                   const ov_geom &g, bool gpu_ok);

static void
render_overview_buffers(nnwm_server *server, nnwm_output *out)
{
    ov_geom g = ov_geom_compute(server, out);
    int W = g.W, H = g.H;
    if (W <= 0 || H <= 0) return;
    int buf_w = g.buf_w, buf_h = g.buf_h;
    int num_ws = g.num_ws;
    double dpi = g.dpi, slot_w = g.slot_w, slot_h = g.slot_h;
    double s = g.s, cx_off = g.cx_off, cy_off = g.cy_off;
    wlr_box out_box = g.out_box;

    const wlr_box &ua = out->usable_area;

    nnwm_config *cfg = server->config;
    int bw = cfg->border.width;
    int th = cfg->titlebar.height;

    /* ============================================================
     * GPU RENDER PASS: dark background + wallpaper + window textures
     * ============================================================ */
    const wlr_drm_format_set *fmts =
        wlr_renderer_get_texture_formats(server->renderer,
                                         server->renderer->render_buffer_caps);
    const wlr_drm_format *fmt =
        fmts ? wlr_drm_format_set_get(fmts, DRM_FORMAT_ARGB8888) : nullptr;

    wlr_buffer *gpu_buf = nullptr;
    wlr_render_pass *pass = nullptr;
    if (fmt) {
        gpu_buf = wlr_allocator_create_buffer(server->allocator, buf_w, buf_h, fmt);
        if (gpu_buf)
            pass = wlr_renderer_begin_buffer_pass(server->renderer, gpu_buf, nullptr);
    }

    if (pass) {
        /* Dark background fill */
        {
            float a = 0.93f;
            wlr_render_rect_options bg = {};
            bg.box        = {0, 0, buf_w, buf_h};
            bg.color      = {0.07f * a, 0.07f * a, 0.10f * a, a};
            bg.blend_mode = WLR_RENDER_BLEND_MODE_NONE;
            wlr_render_pass_add_rect(pass, &bg);
        }

        /* Locate background layer surface for this output (wallpaper) */
        wlr_texture *bg_tex = nullptr;
        {
            nnwm_layer_surface *ls;
            wl_list_for_each(ls, &server->layer_surfaces, link) {
                if (ls->wlr_layer_surface->output != out->wlr_output) continue;
                if (ls->wlr_layer_surface->current.layer
                        != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) continue;
                bg_tex = wlr_surface_get_texture(ls->wlr_layer_surface->surface);
                if (bg_tex) break;
            }
        }

        for (int ws = 0; ws < num_ws; ws++) {
            int    col    = ws % OVERVIEW_COLS;
            int    row    = ws / OVERVIEW_COLS;
            double sx     = OVERVIEW_OUTER + col * (slot_w + OVERVIEW_INNER);
            double sy     = OVERVIEW_OUTER + row * (slot_h + OVERVIEW_INNER);
            bool   active = (ws == out->active_workspace);

            int psx = (int)(sx * dpi), psy = (int)(sy * dpi);
            int psw = (int)(slot_w * dpi), psh = (int)(slot_h * dpi);
            if (psw <= 0 || psh <= 0) continue;

            pixman_region32_t clip;
            pixman_region32_init_rect(&clip, psx, psy,
                                      (uint32_t)psw, (uint32_t)psh);

            /* Slot background rect */
            {
                wlr_render_rect_options slot_bg = {};
                slot_bg.box    = {psx, psy, psw, psh};
                slot_bg.color  = active
                    ? wlr_render_color{0.18f, 0.22f, 0.35f, 1.0f}
                    : wlr_render_color{0.12f, 0.12f, 0.18f, 1.0f};
                slot_bg.blend_mode = WLR_RENDER_BLEND_MODE_NONE;
                wlr_render_pass_add_rect(pass, &slot_bg);
            }

            /* Wallpaper texture stretched to fill slot */
            if (bg_tex) {
                wlr_render_texture_options tex = {};
                tex.texture     = bg_tex;
                tex.dst_box     = {psx, psy, psw, psh};
                tex.clip        = &clip;
                tex.filter_mode = WLR_SCALE_FILTER_BILINEAR;
                wlr_render_pass_add_texture(pass, &tex);
            }

            /* Window textures for this workspace */
            double win_scale = s * dpi;
            nnwm_toplevel *tl;
            wl_list_for_each(tl, &server->toplevels, link) {
                if (tl->output != out) continue;
                if (tl->workspace != ws && !tl->sticky) continue;
                if (tl->in_scratchpad) continue;
                bool tabbed_tiled = !tl->floating
                    && tl->output->layout_mode[tl->workspace]
                           == nnwm_layout_mode::TABBED;
                int eff_th = (tabbed_tiled || th <= 0) ? 0 : th;

                wlr_box lb;
                if (!tl_layout_box(tl, bw, eff_th, &lb)) continue;

                /* XDG surface origin in layout space */
                int surf_lx = lb.x + bw;
                int surf_ly = lb.y + bw + eff_th;

                /* Map to buffer pixels via overview slot origin */
                int orig_x = (int)((sx + cx_off + (surf_lx - ua.x) * s) * dpi);
                int orig_y = (int)((sy + cy_off + (surf_ly - ua.y) * s) * dpi);

                ov_surf_data d{pass, &clip, orig_x, orig_y, win_scale};
                wlr_surface_for_each_surface(
                    tl_wlr_surface(tl),
                    ov_surface_iter, &d);
            }

            pixman_region32_fini(&clip);
        }

        wlr_render_pass_submit(pass);
        wlr_scene_buffer_set_buffer(out->overview_buf, gpu_buf);
        wlr_scene_buffer_set_dest_size(out->overview_buf, W, H);
        wlr_buffer_drop(gpu_buf);
        wlr_scene_node_set_position(&out->overview_buf->node, out_box.x, out_box.y);
        wlr_scene_node_set_enabled(&out->overview_buf->node, true);
        wlr_scene_node_raise_to_top(&out->overview_buf->node);
    } else {
        wlr_scene_node_set_enabled(&out->overview_buf->node, false);
    }

    render_overview_labels(server, out, g, pass != nullptr);
}

static void
render_overview_labels(nnwm_server *server, nnwm_output *out,
                       const ov_geom &g, bool gpu_ok)
{
    int W = g.W, H = g.H;
    if (W <= 0 || H <= 0) return;
    int buf_w = g.buf_w, buf_h = g.buf_h;
    int num_ws = g.num_ws;
    double dpi = g.dpi, slot_w = g.slot_w, slot_h = g.slot_h;
    double s = g.s, cx_off = g.cx_off, cy_off = g.cy_off;
    wlr_box out_box = g.out_box;
    const wlr_box &ua = out->usable_area;
    nnwm_config *cfg = server->config;
    int bw = cfg->border.width;
    int th = cfg->titlebar.height;
    bool pass = gpu_ok; /* alias — used where old code tested `pass` directly */

    nnwm_tbuf *tb = tbuf_create(buf_w, buf_h);
    cairo_surface_t *csurf = cairo_image_surface_create_for_data(
        tb->data, CAIRO_FORMAT_ARGB32, buf_w, buf_h, tb->stride);
    cairo_t *cr = cairo_create(csurf);
    cairo_scale(cr, dpi, dpi);

    if (pass) {
        /* Transparent base — GPU content shows through */
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    } else {
        /* Fallback: full schematic dark overlay */
        cairo_set_source_rgba(cr, 0.07, 0.07, 0.10, 0.93);
        cairo_paint(cr);
    }

    /* Determine which slot the cursor is over (for drag-target highlight) */
    int drag_target_ws = -1;
    if (server->overview_drag_toplevel) {
        double dcx = server->cursor->x - out_box.x;
        double dcy = server->cursor->y - out_box.y;
        for (int ws = 0; ws < num_ws; ws++) {
            int    col = ws % OVERVIEW_COLS;
            int    row = ws / OVERVIEW_COLS;
            double sx  = OVERVIEW_OUTER + col * (slot_w + OVERVIEW_INNER);
            double sy  = OVERVIEW_OUTER + row * (slot_h + OVERVIEW_INNER);
            if (dcx >= sx && dcx < sx + slot_w && dcy >= sy && dcy < sy + slot_h) {
                drag_target_ws = ws;
                break;
            }
        }
    }

    for (int ws = 0; ws < num_ws; ws++) {
        int    col    = ws % OVERVIEW_COLS;
        int    row    = ws / OVERVIEW_COLS;
        double sx     = OVERVIEW_OUTER + col * (slot_w + OVERVIEW_INNER);
        double sy     = OVERVIEW_OUTER + row * (slot_h + OVERVIEW_INNER);
        bool   active = (ws == out->active_workspace);
        bool   any    = false;

        if (!pass) {
            /* Fallback: schematic slot bg + window rects */
            if (active)
                cairo_set_source_rgba(cr, 0.18, 0.22, 0.35, 1.0);
            else
                cairo_set_source_rgba(cr, 0.12, 0.12, 0.18, 1.0);
            cairo_rectangle(cr, sx, sy, slot_w, slot_h);
            cairo_fill(cr);

            nnwm_toplevel *tl;
            wl_list_for_each(tl, &server->toplevels, link) {
                if (tl->output != out) continue;
                if (tl->workspace != ws && !tl->sticky) continue;
                if (tl->in_scratchpad) continue;
                bool tabbed_tiled_fb = !tl->floating
                    && tl->output->layout_mode[tl->workspace]
                           == nnwm_layout_mode::TABBED;
                int eff_th_fb = (tabbed_tiled_fb || th <= 0) ? 0 : th;
                wlr_box lb;
                if (!tl_layout_box(tl, bw, eff_th_fb, &lb)) continue;
                any = true;
                bool focused = (tl == out->last_focused[ws]);
                double wx = sx + cx_off + (lb.x - ua.x) * s;
                double wy = sy + cy_off + (lb.y - ua.y) * s;
                double ww = lb.width * s;
                double wh = lb.height * s;
                cairo_set_source_rgba(cr, 0.18, 0.22, 0.40, 0.85);
                cairo_rectangle(cr, wx, wy, ww, wh);
                cairo_fill(cr);
                if (!focused) {
                    cairo_set_line_width(cr, 1.0);
                    cairo_set_source_rgba(cr, 0.35, 0.38, 0.58, 0.8);
                    cairo_rectangle(cr, wx + 0.5, wy + 0.5, ww - 1.0, wh - 1.0);
                    cairo_stroke(cr);
                }
                const char *title = tl_title(tl);
                if (title && title[0] && ww > 18 && wh > 8) {
                    double fs = std::max(6.0, std::min(wh * 0.32, 11.0));
                    cairo_select_font_face(cr, "Sans",
                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, fs);
                    cairo_text_extents_t te;
                    cairo_text_extents(cr, title, &te);
                    double tx = wx + (ww - te.width) / 2.0 - te.x_bearing;
                    double ty = wy + (wh - te.height) / 2.0 - te.y_bearing;
                    if (te.width > ww - 4.0) tx = wx + 2.0 - te.x_bearing;
                    cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, 0.9);
                    cairo_move_to(cr, tx, ty);
                    cairo_show_text(cr, title);
                }
            }
        } else {
            /* GPU path: draw focus border outlines + count any windows */
            nnwm_toplevel *tl;
            wl_list_for_each(tl, &server->toplevels, link) {
                if (tl->output != out) continue;
                if (tl->workspace != ws && !tl->sticky) continue;
                if (tl->in_scratchpad) continue;
                bool tabbed_tiled_ck = !tl->floating
                    && tl->output->layout_mode[tl->workspace]
                           == nnwm_layout_mode::TABBED;
                int eff_th_ck = (tabbed_tiled_ck || th <= 0) ? 0 : th;
                wlr_box lb;
                if (!tl_layout_box(tl, bw, eff_th_ck, &lb)) continue;
                any = true;

                bool focused = (tl == out->last_focused[ws]);
                /* Skip border for the focused window — it adds no information */
                if (focused)
                    continue;
                const float *bc = cfg->border.unfocused_color;
                double wx = sx + cx_off + (lb.x - ua.x) * s;
                double wy = sy + cy_off + (lb.y - ua.y) * s;
                double ww = lb.width * s;
                double wh = lb.height * s;
                cairo_set_line_width(cr, 0.75);
                cairo_set_source_rgba(cr, bc[0], bc[1], bc[2], bc[3]);
                cairo_rectangle(cr, wx + 0.5, wy + 0.5, ww - 1.0, wh - 1.0);
                cairo_stroke(cr);
            }
        }

        /* Slot border — highlight when it's the drag-drop target */
        bool drag_target = (ws == drag_target_ws);
        if (drag_target) {
            cairo_set_line_width(cr, 2.5);
            cairo_set_source_rgba(cr, 0.95, 0.75, 0.20, 1.0); /* amber drop target */
        } else {
            cairo_set_line_width(cr, active ? 2.0 : 1.0);
            if (active)
                cairo_set_source_rgba(cr, 0.45, 0.65, 1.0, 1.0);
            else
                cairo_set_source_rgba(cr, 0.28, 0.28, 0.40, 1.0);
        }
        cairo_rectangle(cr, sx + 0.5, sy + 0.5, slot_w - 1.0, slot_h - 1.0);
        cairo_stroke(cr);

        /* Workspace label (configured name, or fallback to index) */
        char label_buf[32];
        const char *label = out->workspace_names[ws];
        if (!label || label[0] == '\0')
        {
            std::snprintf(label_buf, sizeof(label_buf), "%d", ws + 1);
            label = label_buf;
        }
        double fs_label = any ? std::max(8.0, slot_h * 0.10)
                              : std::max(14.0, slot_h * 0.28);
        fs_label = std::min(fs_label, 40.0);
        cairo_select_font_face(cr, "Sans",
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, fs_label);
        cairo_text_extents_t lte;
        cairo_text_extents(cr, label, &lte);
        double lx, ly;
        if (any) {
            lx = sx + 5.0 - lte.x_bearing;
            ly = sy + 4.0 - lte.y_bearing;
        } else {
            lx = sx + (slot_w - lte.width) / 2.0 - lte.x_bearing;
            ly = sy + (slot_h - lte.height) / 2.0 - lte.y_bearing;
        }
        cairo_set_source_rgba(cr, 0.90, 0.90, 0.95, any ? 0.80 : 0.55);
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, label);
    }

    cairo_surface_flush(csurf);
    cairo_surface_destroy(csurf);
    cairo_destroy(cr);

    wlr_scene_buffer_set_buffer(out->overview_labels, &tb->base);
    wlr_scene_buffer_set_dest_size(out->overview_labels, W, H);
    wlr_buffer_drop(&tb->base);

    wlr_scene_node_set_position(&out->overview_labels->node, out_box.x, out_box.y);
    wlr_scene_node_set_enabled(&out->overview_labels->node, true);
    /* labels must be above the GPU content buffer */
    wlr_scene_node_raise_to_top(&out->overview_labels->node);
}

/* Cheap re-render: only update the Cairo labels overlay (slot borders, labels,
 * drag-target highlight).  Does NOT re-render window textures via the GPU pass.
 * Use this during drag motion to keep things smooth. */
void
overview_update_labels(nnwm_server *server, nnwm_output *out)
{
    if (!out->overview || !out->overview_labels) return;
    render_overview_labels(server, out, ov_geom_compute(server, out), true);
}

void
exit_overview(nnwm_server *server, nnwm_output *out)
{
    out->overview = false;
    if (out->overview_buf)
        wlr_scene_node_set_enabled(&out->overview_buf->node, false);
    if (out->overview_labels)
        wlr_scene_node_set_enabled(&out->overview_labels->node, false);

    /* Restore correct per-window visibility (render_overview hid all nodes;
     * arrange_windows_impl only re-enables tiled windows, so floating windows
     * on the active workspace would stay hidden without this sweep). */
    {
        nnwm_toplevel *t;
        wl_list_for_each(t, &server->toplevels, link)
        {
            if (t->in_scratchpad) continue;
            wlr_scene_node_set_enabled(
                &t->scene_tree->node,
                t->sticky || (t->output && t->output->active_workspace == t->workspace));
        }
    }

    arrange_windows(server, out);

    nnwm_toplevel *tl = ws_first(server, out);
    if (tl)
        focus_toplevel(tl);
    else
        wlr_seat_keyboard_clear_focus(server->seat);
}

/* ---- Output / workspace helpers ---- */

nnwm_output *
output_cycle(nnwm_server *server, nnwm_output *cur, int dir)
{
    nnwm_output *outputs[32];
    int count = 0, cur_idx = 0;
    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link)
    {
        if (count < 32)
        {
            if (o == cur)
                cur_idx = count;
            outputs[count++] = o;
        }
    }
    if (count <= 1)
        return nullptr;
    return outputs[(cur_idx + dir + count) % count];
}

nnwm_output *
output_at_cursor(nnwm_server *server)
{
    wlr_output *wlr_out = wlr_output_layout_output_at(
        server->output_layout, server->cursor->x, server->cursor->y);
    if (!wlr_out)
        return nullptr;
    nnwm_output *o;
    wl_list_for_each(o, &server->outputs,
                     link) if (o->wlr_output == wlr_out) return o;
    return nullptr;
}

/* ---- Workspace tiled-window navigation helpers ---- */

/* A window belongs to the active tiled set if it is on this output and either
 * on the active workspace or sticky (sticky windows tile into every workspace).
 */
#define WS_TILED(t, out)                                                       \
    ((t)->output == (out)                                                      \
     && ((t)->workspace == (out)->active_workspace || (t)->sticky)             \
     && !(t)->floating && !(t)->fullscreen && !(t)->fake_fullscreen            \
     && !(t)->in_scratchpad)

nnwm_toplevel *
ws_first(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels,
                     link) if (WS_TILED(t, out)) return t;
    return nullptr;
}

nnwm_toplevel *
ws_next(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.next; it != &server->toplevels; it = it->next)
    {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_TILED(t, out))
            return t;
    }
    return nullptr;
}

nnwm_toplevel *
ws_prev(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.prev; it != &server->toplevels; it = it->prev)
    {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_TILED(t, out))
            return t;
    }
    return nullptr;
}

#define WS_FLOAT(t, out)                                                       \
    ((t)->output == (out)                                                      \
     && ((t)->workspace == (out)->active_workspace || (t)->sticky)             \
     && (t)->floating)

nnwm_toplevel *
ws_first_float(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels,
                     link) if (WS_FLOAT(t, out)) return t;
    return nullptr;
}

nnwm_toplevel *
ws_last_float(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t, *last = nullptr;
    wl_list_for_each(t, &server->toplevels, link) if (WS_FLOAT(t, out)) last
        = t;
    return last;
}

nnwm_toplevel *
ws_next_float(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.next; it != &server->toplevels; it = it->next)
    {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_FLOAT(t, out))
            return t;
    }
    return nullptr;
}

nnwm_toplevel *
ws_prev_float(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.prev; it != &server->toplevels; it = it->prev)
    {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (WS_FLOAT(t, out))
            return t;
    }
    return nullptr;
}

nnwm_toplevel *
ws_last(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t, *last = nullptr;
    wl_list_for_each(t, &server->toplevels, link) if (WS_TILED(t, out)) last
        = t;
    return last;
}

nnwm_toplevel *
scratch_first(nnwm_server *server)
{
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link) if (t->in_scratchpad) return t;
    return nullptr;
}

nnwm_toplevel *
scratch_last(nnwm_server *server)
{
    nnwm_toplevel *t, *last = nullptr;
    wl_list_for_each(t, &server->toplevels, link) if (t->in_scratchpad) last = t;
    return last;
}

nnwm_toplevel *
scratch_next(nnwm_server *server, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.next; it != &server->toplevels; it = it->next)
    {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (t->in_scratchpad)
            return t;
    }
    return nullptr;
}

nnwm_toplevel *
scratch_prev(nnwm_server *server, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.prev; it != &server->toplevels; it = it->prev)
    {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (t->in_scratchpad)
            return t;
    }
    return nullptr;
}

int
ws_count(nnwm_server *server, nnwm_output *out)
{
    int n = 0;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link) if (WS_TILED(t, out)) n++;
    return n;
}

/* ---- Window arrangement / tiling layout ---- */

void
arrange_windows(nnwm_server *server, nnwm_output *out)
{
    arrange_windows_impl(server, out);
    if (out && out->overview)
        render_overview(server, out);
}

static void
arrange_windows_impl(nnwm_server *server, nnwm_output *out)
{
    if (!out)
        return;

    int ws              = out->active_workspace;
    const wlr_box &area = out->usable_area;
    nnwm_config *cfg    = server->config;
    nnwm_toplevel *tl;

    /* ── Float layout: every tiled window in the workspace becomes floating ──
     */
    if (out->layout_mode[ws] == nnwm_layout_mode::FLOAT)
    {
        wlr_scene_node_set_enabled(&out->tab_bar->node, false);
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->output != out) continue;
            if (tl->workspace != ws && !tl->sticky) continue;
            if (tl->in_scratchpad || tl->fullscreen || tl->fake_fullscreen) continue;
            if (!tl->floating)
            {
                tl->floating = true;
                /* Give newly-floated windows a sensible size if they lack one */
                if (tl->cur_w <= 0 || tl->cur_h <= 0)
                {
                    int fw = area.width  / 2;
                    int fh = area.height / 2;
                    int fx = area.x + (area.width  - fw) / 2;
                    int fy = area.y + (area.height - fh) / 2;
                    tl_set_geometry(tl, fx, fy, fw, fh, cfg->border.width);
                }
            }
            wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
        }
        return;
    }

    /* ── Tabbed layout ───────────────────────────────────────────────────────
     */
    if (out->layout_mode[ws] == nnwm_layout_mode::TABBED)
    {
        int n        = ws_count(server, out);
        bool solo    = (n == 1);
        bool hide_tabs = solo && cfg->layout.tab_smart;
        int bw       = (solo && cfg->border.smart) ? 0 : cfg->border.width;
        int og       = (solo && cfg->gap.smart) ? 0 : cfg->gap.outer;
        int tab_sz   = hide_tabs ? 0
                     : (cfg->layout.tab_bar_height > 0 ? cfg->layout.tab_bar_height : 24);
        nnwm_tab_position tab_pos = cfg->layout.tab_position;

        nnwm_toplevel *active = out->last_focused[ws];
        if (!active || active->output != out || active->workspace != ws
            || active->floating || active->fullscreen || active->fake_fullscreen)
            active = ws_first(server, out);

        /* Content area and tab bar rect, both relative to output origin */
        int cx, cy, cw, ch, tbx, tby, tbw, tbh;
        switch (tab_pos)
        {
            case nnwm_tab_position::BOTTOM:
                cw  = area.width - 2 * og;
                ch  = area.height - 2 * og - tab_sz;
                cx  = area.x + og;
                cy  = area.y + og;
                tbw = cw; tbh = tab_sz;
                tbx = cx; tby = cy + ch;
                break;
            case nnwm_tab_position::LEFT:
                cw  = area.width - 2 * og - tab_sz;
                ch  = area.height - 2 * og;
                cx  = area.x + og + tab_sz;
                cy  = area.y + og;
                tbw = tab_sz; tbh = ch;
                tbx = area.x + og; tby = cy;
                break;
            case nnwm_tab_position::RIGHT:
                cw  = area.width - 2 * og - tab_sz;
                ch  = area.height - 2 * og;
                cx  = area.x + og;
                cy  = area.y + og;
                tbw = tab_sz; tbh = ch;
                tbx = cx + cw; tby = cy;
                break;
            default: /* TOP */
                cw  = area.width - 2 * og;
                ch  = area.height - 2 * og - tab_sz;
                cx  = area.x + og;
                cy  = area.y + og + tab_sz;
                tbw = cw; tbh = tab_sz;
                tbx = cx; tby = area.y + og;
                break;
        }

        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (!WS_TILED(tl, out))
                continue;
            wlr_scene_node_set_enabled(&tl->scene_tree->node, tl == active);
            tl_xdg_set_tiled(tl,
                                       WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                           | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            tl_xdg_set_size(tl, cw - 2 * bw,
                                      ch - 2 * bw);
            tl_set_geometry(tl, cx, cy, cw, ch, bw);
        }

        if (hide_tabs && out->tab_bar)
        {
            wlr_scene_node_set_enabled(&out->tab_bar->node, false);
        }
        else if (n > 0)
        {
            render_tab_bar(server, out, tbw, tbh);
            wlr_scene_node_set_position(&out->tab_bar->node, tbx, tby);
            wlr_scene_node_raise_to_top(&out->tab_bar->node);
#ifdef HAVE_SCENEFX
            {
            int tab_r = cfg->fx.rounding.radius;
            if (cfg->fx.rounding.smart && solo)
                tab_r = 0;
            struct fx_corner_radii tab_cr;
            switch (tab_pos)
            {
                case nnwm_tab_position::BOTTOM: tab_cr = corner_radii_bottom(tab_r); break;
                case nnwm_tab_position::LEFT:   tab_cr = corner_radii_left(tab_r);   break;
                case nnwm_tab_position::RIGHT:  tab_cr = corner_radii_right(tab_r);  break;
                default:                        tab_cr = corner_radii_top(tab_r);    break;
            }
            wlr_scene_buffer_set_corner_radii(out->tab_bar, tab_cr);
            }
#endif
        }
        else if (out->tab_bar)
        {
            wlr_scene_node_set_enabled(&out->tab_bar->node, false);
        }

        /* Floating and fullscreen windows above everything */
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->output == out && (tl->workspace == ws || tl->sticky)
                && (tl->floating || tl->fullscreen || tl->fake_fullscreen))
                wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        }
        return;
    }

    /* ── Scroll layout (horizontal strip) ───────────────────────────────────
     */
    if (out->layout_mode[ws] == nnwm_layout_mode::HSCROLL)
    {
        if (out->tab_bar)
            wlr_scene_node_set_enabled(&out->tab_bar->node, false);

        int og = cfg->gap.outer;
        int ig = cfg->gap.inner;
        int bw = cfg->border.width;
        int th = cfg->titlebar.height;
        float cw_frac
            = cfg->scroll_column_width > 0.0f ? cfg->scroll_column_width : 0.5f;
        int col_w = (int)(area.width * cw_frac);
        int col_h = area.height - 2 * og;

        /* Find focused window index to compute scroll offset */
        nnwm_toplevel *active = out->last_focused[ws];
        if (!active || active->output != out || active->workspace != ws
            || active->floating || active->fullscreen || active->fake_fullscreen)
            active = ws_first(server, out);

        int fi = 0, idx = 0;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (!WS_TILED(tl, out))
                continue;
            if (tl == active)
                fi = idx;
            idx++;
        }

        /* Center the focused column in the viewport */
        int focused_left = og + fi * (col_w + ig);
        int target       = focused_left + col_w / 2 - area.width / 2;
        if (target < 0)
            target = 0;
        out->scroll_offset[ws] = target;

        wlr_surface *focused_surface
            = server->seat->keyboard_state.focused_surface;
        int i = 0;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (!WS_TILED(tl, out))
                continue;
            int tx = area.x + og + i * (col_w + ig) - out->scroll_offset[ws];
            int ty = area.y + og;
            bool focused = (tl_wlr_surface(tl) == focused_surface);
            wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
            tl_xdg_set_tiled(tl,
                                       WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                           | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            tl_xdg_set_size(tl, col_w - 2 * bw,
                                      col_h - 2 * bw - th);
            tl_set_geometry(tl, tx, ty, col_w, col_h, bw);
            render_titlebar(tl, col_w - 2 * bw, focused);
            i++;
        }

        /* Floating and fullscreen windows above tiled */
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->output == out && (tl->workspace == ws || tl->sticky)
                && (tl->floating || tl->fullscreen || tl->fake_fullscreen))
                wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        }
        return;
    }

    /* ── Vertical scroll layout ──────────────────────────────────────────────
     */
    if (out->layout_mode[ws] == nnwm_layout_mode::VSCROLL)
    {
        if (out->tab_bar)
            wlr_scene_node_set_enabled(&out->tab_bar->node, false);

        int og = cfg->gap.outer;
        int ig = cfg->gap.inner;
        int bw = cfg->border.width;
        int th = cfg->titlebar.height;
        int row_h = area.height - 2 * og;
        int row_w = area.width - 2 * og;

        nnwm_toplevel *active = out->last_focused[ws];
        if (!active || active->output != out || active->workspace != ws
            || active->floating || active->fullscreen || active->fake_fullscreen)
            active = ws_first(server, out);

        int fi = 0, idx = 0;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (!WS_TILED(tl, out))
                continue;
            if (tl == active)
                fi = idx;
            idx++;
        }

        /* Snap scroll so focused window is flush with the top of the viewport */
        out->scroll_offset[ws] = fi * (row_h + ig);

        wlr_surface *focused_surface
            = server->seat->keyboard_state.focused_surface;
        int i = 0;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (!WS_TILED(tl, out))
                continue;
            int tx     = area.x + og;
            int ty     = area.y + og + i * (row_h + ig) - out->scroll_offset[ws];
            bool focused = (tl_wlr_surface(tl) == focused_surface);
            wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
            tl_xdg_set_tiled(tl,
                                       WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                           | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            tl_xdg_set_size(tl, row_w - 2 * bw,
                                      row_h - 2 * bw - th);
            tl_set_geometry(tl, tx, ty, row_w, row_h, bw);
            render_titlebar(tl, row_w - 2 * bw, focused);
            i++;
        }

        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->output == out && (tl->workspace == ws || tl->sticky)
                && (tl->floating || tl->fullscreen || tl->fake_fullscreen))
                wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        }
        return;
    }

    /* ── Tile layout (master-stack) ──────────────────────────────────────────
     */

    /* Disable tab bar and re-enable tiled scene trees (from possible tabbed
     * state) */
    if (out->tab_bar)
        wlr_scene_node_set_enabled(&out->tab_bar->node, false);
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (WS_TILED(tl, out))
        {
            wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
        }
    }

    int n = ws_count(server, out);

    bool solo = (n == 1);
    int bw    = (solo && cfg->border.smart) ? 0 : cfg->border.width;
    int ig    = (solo && cfg->gap.smart) ? 0 : cfg->gap.inner;
    int og    = (solo && cfg->gap.smart) ? 0 : cfg->gap.outer;
    int th    = cfg->titlebar.height;

    int x0 = area.x + og;
    int y0 = area.y + og;
    int W  = area.width - 2 * og;
    int H  = area.height - 2 * og;

    wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;

    /* VTILE: master on top, stack arranged horizontally below */
    if (out->layout_mode[ws] == nnwm_layout_mode::VTILE)
    {
        if (n == 1)
        {
            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (!WS_TILED(tl, out))
                    continue;
                tl_xdg_set_tiled(tl,
                                           WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                               | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                tl_xdg_set_size(tl, W - 2 * bw,
                                          H - 2 * bw - th);
                tl_set_geometry(tl, x0, y0, W, H, bw);
                render_titlebar(tl, W - 2 * bw,
                                tl_wlr_surface(tl) == focused_surface);
                break;
            }
        }
        else if (n > 1)
        {
            int mh = (int)(H * out->master_ratio[ws]);
            int sh = H - mh - ig;
            int ns = n - 1;
            int sw = (W - (ns - 1) * ig) / ns;

            int i = 0;
            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (!WS_TILED(tl, out))
                    continue;
                bool focused = (tl_wlr_surface(tl) == focused_surface);
                if (i == 0)
                {
                    tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                              | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                    tl_xdg_set_size(tl, W - 2 * bw,
                                              mh - 2 * bw - th);
                    tl_set_geometry(tl, x0, y0, W, mh, bw);
                    render_titlebar(tl, W - 2 * bw, focused);
                }
                else
                {
                    int sx = x0 + (i - 1) * (sw + ig);
                    int w  = (i < ns) ? sw : W - (i - 1) * (sw + ig);
                    tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                              | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                    tl_xdg_set_size(tl, w - 2 * bw,
                                              sh - 2 * bw - th);
                    tl_set_geometry(tl, sx, y0 + mh + ig, w, sh, bw);
                    render_titlebar(tl, w - 2 * bw, focused);
                }
                ++i;
            }
        }

        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->output == out && (tl->workspace == ws || tl->sticky)
                && (tl->floating || tl->fullscreen || tl->fake_fullscreen))
            {
                tl_xdg_set_tiled(tl, WLR_EDGE_NONE);
                wlr_scene_node_raise_to_top(&tl->scene_tree->node);
            }
        }
        return;
    }

    /* HTILE: master on left, stack in right column */
    if (n == 1)
    {
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (!WS_TILED(tl, out))
                continue;
            tl_xdg_set_tiled(tl,
                                       WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                           | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            tl_xdg_set_size(tl, W - 2 * bw,
                                      H - 2 * bw - th);
            tl_set_geometry(tl, x0, y0, W, H, bw);
            render_titlebar(tl, W - 2 * bw,
                            tl_wlr_surface(tl) == focused_surface);
            break;
        }
    }
    else if (n > 1)
    {
        int mw = (int)(W * out->master_ratio[ws]);
        int sw = W - mw - ig;
        int ns = n - 1;
        int sh = (H - (ns - 1) * ig) / ns;

        int i = 0;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (!WS_TILED(tl, out))
                continue;
            bool focused = (tl_wlr_surface(tl) == focused_surface);
            if (i == 0)
            {
                tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                          | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                tl_xdg_set_size(tl, mw - 2 * bw,
                                          H - 2 * bw - th);
                tl_set_geometry(tl, x0, y0, mw, H, bw);
                render_titlebar(tl, mw - 2 * bw, focused);
            }
            else
            {
                int sy = y0 + (i - 1) * (sh + ig);
                int h  = (i < ns) ? sh : H - (i - 1) * (sh + ig);
                tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                          | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                tl_xdg_set_size(tl, sw - 2 * bw,
                                          h - 2 * bw - th);
                tl_set_geometry(tl, x0 + mw + ig, sy, sw, h, bw);
                render_titlebar(tl, sw - 2 * bw, focused);
            }
            ++i;
        }
    }

    /* Maximize: fill usable area while keeping tiled state */
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (tl->output != out || (tl->workspace != ws && !tl->sticky))
            continue;
        if (!tl->maximize || tl->floating || tl->fullscreen || tl->fake_fullscreen)
            continue;
        tl_xdg_set_tiled(tl,
                                   WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                       | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
        tl_xdg_set_size(tl, W - 2 * bw, H - 2 * bw - th);
        tl_set_geometry(tl, x0, y0, W, H, bw);
        render_titlebar(tl, W - 2 * bw,
                        tl_wlr_surface(tl) == focused_surface);
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
    }

    /* Floating and fullscreen windows must always sit above tiled ones.
     * Clear the tiled state so clients know they can control their own size. */
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (tl->output == out && (tl->workspace == ws || tl->sticky)
            && (tl->floating || tl->fullscreen || tl->fake_fullscreen))
        {
            tl_xdg_set_tiled(tl, WLR_EDGE_NONE);
            wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        }
    }
}

void
arrange_all_outputs(nnwm_server *server)
{
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) arrange_windows(server, out);
}

/* ---- Scratchpad layout ---- */

static int
scratch_count(nnwm_server *server)
{
    int n = 0;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
        if (t->in_scratchpad && !t->floating) n++;
    return n;
}

static int
scratch_count_all(nnwm_server *server)
{
    int n = 0;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
        if (t->in_scratchpad) n++;
    return n;
}

void
arrange_scratchpad(nnwm_server *server)
{
    if (!server->scratchpad_visible)
        return;

    nnwm_output *out = server->focused_output;
    if (!out && !wl_list_empty(&server->outputs))
        out = wl_container_of(server->outputs.next, out, link);
    if (!out)
        return;

    int n = scratch_count(server); /* tiled scratchpad windows only */

    /* Position the dim rect over the focused output */
    wlr_box area;
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &area);
    wlr_scene_rect_set_size(server->scene_scratch_dim, area.width, area.height);
    wlr_scene_node_set_position(&server->scene_scratch_dim->node, area.x, area.y);

    nnwm_config *cfg = server->config;
    bool solo        = (n == 1);
    int bw           = (solo && cfg->border.smart) ? 0 : cfg->border.width;
    int ig           = (solo && cfg->gap.smart) ? 0 : cfg->gap.inner;
    int og           = (solo && cfg->gap.smart) ? 0 : cfg->gap.outer;
    int th           = cfg->titlebar.height;

    int x0 = area.x + og;
    int y0 = area.y + og;
    int W  = area.width - 2 * og;
    int H  = area.height - 2 * og;

    wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;

    nnwm_toplevel *tl;
    if (server->scratchpad_layout == nnwm_layout_mode::HTILE)
    {
        /* HTILE: master on left, stack in right column */
        if (n == 1)
        {
            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (!tl->in_scratchpad || tl->floating) continue;
                tl_xdg_set_tiled(tl,
                                           WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                               | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                tl_xdg_set_size(tl, W - 2 * bw,
                                          H - 2 * bw - th);
                tl_set_geometry(tl, x0, y0, W, H, bw);
                render_titlebar(tl, W - 2 * bw,
                                tl_wlr_surface(tl) == focused_surface);
                break;
            }
        }
        else
        {
            int mw = (int)(W * out->master_ratio[out->active_workspace]);
            int sw = W - mw - ig;
            int ns = n - 1;
            int sh = (H - (ns - 1) * ig) / ns;

            int i = 0;
            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (!tl->in_scratchpad || tl->floating) continue;
                bool focused = (tl_wlr_surface(tl) == focused_surface);
                if (i == 0)
                {
                    tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                              | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                    tl_xdg_set_size(tl, mw - 2 * bw,
                                              H - 2 * bw - th);
                    tl_set_geometry(tl, x0, y0, mw, H, bw);
                    render_titlebar(tl, mw - 2 * bw, focused);
                }
                else
                {
                    int sy = y0 + (i - 1) * (sh + ig);
                    int h  = (i < ns) ? sh : H - (i - 1) * (sh + ig);
                    tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                              | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                    tl_xdg_set_size(tl, sw - 2 * bw,
                                              h - 2 * bw - th);
                    tl_set_geometry(tl, x0 + mw + ig, sy, sw, h, bw);
                    render_titlebar(tl, sw - 2 * bw, focused);
                }
                ++i;
            }
        }
    }
    else /* VTILE: master on top, stack horizontally below */
    {
        if (n == 1)
        {
            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (!tl->in_scratchpad || tl->floating) continue;
                tl_xdg_set_tiled(tl,
                                           WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                               | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                tl_xdg_set_size(tl, W - 2 * bw,
                                          H - 2 * bw - th);
                tl_set_geometry(tl, x0, y0, W, H, bw);
                render_titlebar(tl, W - 2 * bw,
                                tl_wlr_surface(tl) == focused_surface);
                break;
            }
        }
        else
        {
            int mh = (int)(H * out->master_ratio[out->active_workspace]);
            int sh = H - mh - ig;
            int ns = n - 1;
            int sw = (W - (ns - 1) * ig) / ns;

            int i = 0;
            wl_list_for_each(tl, &server->toplevels, link)
            {
                if (!tl->in_scratchpad || tl->floating) continue;
                bool focused = (tl_wlr_surface(tl) == focused_surface);
                if (i == 0)
                {
                    tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                              | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                    tl_xdg_set_size(tl, W - 2 * bw,
                                              mh - 2 * bw - th);
                    tl_set_geometry(tl, x0, y0, W, mh, bw);
                    render_titlebar(tl, W - 2 * bw, focused);
                }
                else
                {
                    int sx = x0 + (i - 1) * (sw + ig);
                    int w  = (i < ns) ? sw : W - (i - 1) * (sw + ig);
                    tl_xdg_set_tiled(tl, WLR_EDGE_TOP | WLR_EDGE_BOTTOM
                                              | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                    tl_xdg_set_size(tl, w - 2 * bw,
                                              sh - 2 * bw - th);
                    tl_set_geometry(tl, sx, y0 + mh + ig, w, sh, bw);
                    render_titlebar(tl, w - 2 * bw, focused);
                }
                ++i;
            }
        }
    }
}

/* ---- Focus management ---- */

void
focus_toplevel(nnwm_toplevel *toplevel, bool warp)
{
    if (toplevel == nullptr)
    {
        return;
    }
    nnwm_server *server       = toplevel->server;
    wlr_seat *seat            = server->seat;
    wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    wlr_surface *surface      = tl_wlr_surface(toplevel);
    if (prev_surface == surface)
    {
        return;
    }
    if (prev_surface)
    {
        nnwm_toplevel *prev_tl_ftl;
        wl_list_for_each(prev_tl_ftl, &server->toplevels, link) {
            if (tl_wlr_surface(prev_tl_ftl) == prev_surface) {
                ftl_set_activated(prev_tl_ftl, false);
                break;
            }
        }

        wlr_xdg_toplevel *prev_toplevel
            = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != nullptr)
        {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
#ifdef HAVE_XWAYLAND
        else
        {
            /* Check if the previously focused surface belonged to an XWayland window */
            nnwm_toplevel *prev_tl;
            wl_list_for_each(prev_tl, &server->toplevels, link)
            {
                if (prev_tl->is_xwayland && tl_wlr_surface(prev_tl) == prev_surface)
                {
                    nnwm_xw_activate(prev_tl->xwayland_surface, 0);
                    break;
                }
            }
        }
#endif
    }
    wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
#ifdef HAVE_XWAYLAND
    if (toplevel->is_xwayland)
        nnwm_xw_activate(toplevel->xwayland_surface, 1);
    else
#endif
        wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    toplevel->urgent = false;
    ftl_set_activated(toplevel, true);

    /* Update border colors, titlebar focus state, and opacity for all windows */
    nnwm_config *cfg = server->config;
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
    {
        bool foc     = (tl == toplevel);
        float *color = foc ? cfg->border.focused_color
                           : cfg->border.unfocused_color;
#ifdef HAVE_SCENEFX
        tl_start_border_color(tl, color);
#else
        for (int b = 0; b < 4; b++)
            wlr_scene_rect_set_color(tl->border[b], color);
#endif
        render_titlebar(tl, tl->titlebar_width, foc);

        /* Apply focused / unfocused opacity */
        float base = (tl->rule_opacity >= 0.0f) ? tl->rule_opacity
                                                 : cfg->fx.opacity;
        float op   = foc ? (cfg->fx.focused_opacity >= 0.0f
                                ? cfg->fx.focused_opacity
                                : base)
                         : (cfg->fx.unfocused_opacity >= 0.0f
                                ? cfg->fx.unfocused_opacity
                                : base);
        if (tl->scene_surface)
            set_opacity_recursive(tl->scene_surface, op);
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
    if (out)
    {
        server->focused_output = out;
        int ws                 = toplevel->workspace;
        if (out->last_focused[ws] != toplevel)
            out->prev_focused[ws] = out->last_focused[ws];
        out->last_focused[ws] = toplevel;

        if (out->layout_mode[ws] == nnwm_layout_mode::TABBED
            || out->layout_mode[ws] == nnwm_layout_mode::HSCROLL
            || out->layout_mode[ws] == nnwm_layout_mode::VSCROLL)
            arrange_windows(server, out);
        else if (out->overview)
            render_overview(server, out);
    }

    if (warp && cfg->mouse.warp_to_focused_window)
    {
        int nx, ny;
        if (wlr_scene_node_coords(&toplevel->scene_tree->node, &nx, &ny))
        {
#ifdef HAVE_XWAYLAND
            if (toplevel->is_xwayland)
            {
                wlr_cursor_warp(server->cursor, nullptr,
                                nx + toplevel->cur_w / 2.0,
                                ny + toplevel->cur_h / 2.0);
            }
            else
#endif
            {
                wlr_box geo = toplevel->xdg_toplevel->base->geometry;
                wlr_cursor_warp(server->cursor, nullptr,
                                nx + geo.x + geo.width / 2.0,
                                ny + geo.y + geo.height / 2.0);
            }
        }
    }
    fire_hook_window(server, "window_focus", toplevel);
}

void
unfocus_all_borders(nnwm_server *server)
{
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
    {
#ifdef HAVE_SCENEFX
        tl_start_border_color(tl, server->config->border.unfocused_color);
#else
        for (int b = 0; b < 4; b++)
            wlr_scene_rect_set_color(tl->border[b],
                                     server->config->border.unfocused_color);
#endif
        render_titlebar(tl, tl->titlebar_width, false);
    }
}

/* ---- Config error overlay ---- */

static int
error_dismiss_cb(void *data)
{
    hide_config_error(static_cast<nnwm_server *>(data));
    return 0;
}

/* ── Cursor attention animation (find_cursor) ────────────────────────────── */

static constexpr int    RING_TICK_MS   = 16;   /* ~60 fps */
static constexpr int    RING_TICKS     = 40;   /* ≈ 640 ms */

/* ---- rings style ---- */
static constexpr int    RINGS_SIZE_PX  = 200;  /* square buffer, logical pixels */
static constexpr float  RINGS_MAX_R    = 90.0f;

/* ---- spotlight style ---- */
static constexpr int    SPOT_TICKS     = 100;  /* ≈ 1600 ms */

/* ---- zoom style ---- */
static constexpr int    ZOOM_TICKS     = 200;  /* ≈ 3200 ms total */
static constexpr int    ZOOM_FACTOR    = 3;    /* cursor_size multiplier */
static constexpr float  ZOOM_HOLD_IN   = 0.15f; /* rise ends (≈ 480 ms) */
static constexpr float  ZOOM_HOLD_OUT  = 0.75f; /* fall begins (≈ 2400 ms) */
static constexpr double SPOT_RADIUS_PX = 120.0; /* cutout radius, logical pixels */
static constexpr double SPOT_DIM       = 0.65;  /* max dim opacity */
static constexpr float  SPOT_HOLD_IN   = 0.25f; /* fade-in ends */
static constexpr float  SPOT_HOLD_OUT  = 0.65f; /* fade-out begins */

void cursor_ring_stop(nnwm_server *server)
{
    if (server->cursor_ring_timer)
    {
        wl_event_source_remove(server->cursor_ring_timer);
        server->cursor_ring_timer = nullptr;
    }
    if (server->cursor_ring_buf)
    {
        wlr_scene_node_destroy(&server->cursor_ring_buf->node);
        server->cursor_ring_buf = nullptr;
    }
    if (server->cursor_zoom_active)
    {
        server->cursor_zoom_active = false;
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
}

void cursor_zoom_update_pos(nnwm_server *server)
{
    if (!server->cursor_zoom_active || !server->cursor_ring_buf)
        return;
    float scale = server->cursor_zoom_scale;
    int pos_x   = (int)(server->cursor->x - server->cursor_zoom_hot_x * scale);
    int pos_y   = (int)(server->cursor->y - server->cursor_zoom_hot_y * scale);
    wlr_scene_node_set_position(&server->cursor_ring_buf->node, pos_x, pos_y);
}

/* Upload spotlight buffer at full opacity; scene node opacity handles the envelope. */
static void spotlight_upload(nnwm_server *server)
{
    int ow = server->cursor_ring_out_w;
    int oh = server->cursor_ring_out_h;
    double scale = server->cursor_ring_out_scale;
    int buf_w = (int)(ow * scale);
    int buf_h = (int)(oh * scale);
    if (buf_w <= 0 || buf_h <= 0)
        return;

    double cx = (server->cursor->x - server->cursor_ring_out_x) * scale;
    double cy = (server->cursor->y - server->cursor_ring_out_y) * scale;
    double sr = SPOT_RADIUS_PX * scale;

    nnwm_tbuf *tb = tbuf_create(buf_w, buf_h);
    cairo_surface_t *csurf = cairo_image_surface_create_for_data(
        tb->data, CAIRO_FORMAT_ARGB32, buf_w, buf_h, tb->stride);
    cairo_t *cr = cairo_create(csurf);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, SPOT_DIM);
    cairo_paint(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_arc(cr, cx, cy, sr, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_surface_flush(csurf);
    cairo_surface_destroy(csurf);
    cairo_destroy(cr);

    wlr_scene_buffer_set_buffer(server->cursor_ring_buf, &tb->base);
    wlr_buffer_drop(&tb->base);
    wlr_scene_buffer_set_dest_size(server->cursor_ring_buf, ow, oh);

    /* Track last rendered position so tick only re-uploads on cursor move */
    server->cursor_ring_x = server->cursor->x;
    server->cursor_ring_y = server->cursor->y;
}

static int cursor_ring_tick(void *data)
{
    auto *server = static_cast<nnwm_server *>(data);
    const char *style = server->config->find_cursor_style;
    bool is_spot = style && std::strcmp(style, "spotlight") == 0;
    bool is_zoom = style && std::strcmp(style, "zoom")      == 0;
    int  ticks   = is_spot ? SPOT_TICKS : is_zoom ? ZOOM_TICKS : RING_TICKS;
    server->cursor_ring_progress += 1.0f / ticks;

    if (server->cursor_ring_progress >= 1.0f)
    {
        cursor_ring_stop(server);
        return 0;
    }

    float t = server->cursor_ring_progress;

    if (is_spot)
    {
        /* ---- spotlight: GPU opacity envelope + lazy Cairo re-upload on cursor move ---- */

        /* Opacity envelope: fade-in → hold → fade-out (executed on GPU via set_opacity) */
        float opacity;
        if (t < SPOT_HOLD_IN)
            opacity = t / SPOT_HOLD_IN;
        else if (t < SPOT_HOLD_OUT)
            opacity = 1.0f;
        else
            opacity = 1.0f - (t - SPOT_HOLD_OUT) / (1.0f - SPOT_HOLD_OUT);
        wlr_scene_buffer_set_opacity(server->cursor_ring_buf, opacity);

        /* Re-upload only when cursor has moved since last render */
        if (server->cursor->x != server->cursor_ring_x ||
            server->cursor->y != server->cursor_ring_y)
            spotlight_upload(server);
    }
    else if (is_zoom)
    {
        /* ---- zoom: rise → hold → fall via GPU dest_size, no re-upload ---- */
        float scale;
        if (t < ZOOM_HOLD_IN)
        {
            float p = t / ZOOM_HOLD_IN;
            scale = 1.0f + (ZOOM_FACTOR - 1.0f) * std::sin(p * (float)(M_PI / 2));
        }
        else if (t < ZOOM_HOLD_OUT)
        {
            scale = (float)ZOOM_FACTOR;
        }
        else
        {
            float p = (t - ZOOM_HOLD_OUT) / (1.0f - ZOOM_HOLD_OUT);
            scale = 1.0f + (ZOOM_FACTOR - 1.0f) * std::cos(p * (float)(M_PI / 2));
        }
        server->cursor_zoom_scale = scale;
        int dest_w = (int)(server->cursor_zoom_img_w * scale);
        int dest_h = (int)(server->cursor_zoom_img_h * scale);
        int pos_x  = (int)(server->cursor->x - server->cursor_zoom_hot_x * scale);
        int pos_y  = (int)(server->cursor->y - server->cursor_zoom_hot_y * scale);
        wlr_scene_buffer_set_dest_size(server->cursor_ring_buf, dest_w, dest_h);
        wlr_scene_node_set_position(&server->cursor_ring_buf->node, pos_x, pos_y);
    }
    else
    {
        /* ---- rings: concentric shrinking filled circle ---- */
        double dpi = 1.0;
        {
            nnwm_output *out;
            wl_list_for_each(out, &server->outputs, link)
            {
                wlr_box ob;
                wlr_output_layout_get_box(server->output_layout, out->wlr_output, &ob);
                if (server->cursor->x >= ob.x && server->cursor->x < ob.x + ob.width &&
                    server->cursor->y >= ob.y && server->cursor->y < ob.y + ob.height)
                {
                    dpi = out->wlr_output->scale;
                    break;
                }
            }
        }
        int buf = (int)(RINGS_SIZE_PX * dpi);
        nnwm_tbuf *tb = tbuf_create(buf, buf);
        cairo_surface_t *csurf = cairo_image_surface_create_for_data(
            tb->data, CAIRO_FORMAT_ARGB32, buf, buf, tb->stride);
        cairo_t *cr = cairo_create(csurf);
        cairo_scale(cr, dpi, dpi);

        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        double cx = RINGS_SIZE_PX / 2.0;
        double cy = RINGS_SIZE_PX / 2.0;

        /* Ease-out: fast shrink at first, slows near cursor */
        double ease = 1.0 - (1.0 - (double)t) * (1.0 - (double)t);
        double r    = RINGS_MAX_R * (1.0 - ease);
        double fill_alpha   = (1.0 - (double)t) * 0.45;
        double border_alpha = (1.0 - (double)t) * 0.90;

        if (r >= 1.0)
        {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, fill_alpha);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            cairo_fill(cr);

            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, border_alpha);
            cairo_set_line_width(cr, 2.5);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            cairo_stroke(cr);
        }

        cairo_surface_flush(csurf);
        cairo_surface_destroy(csurf);
        cairo_destroy(cr);

        wlr_scene_buffer_set_buffer(server->cursor_ring_buf, &tb->base);
        wlr_buffer_drop(&tb->base);
        wlr_scene_buffer_set_dest_size(server->cursor_ring_buf,
                                       RINGS_SIZE_PX, RINGS_SIZE_PX);
        /* Track live cursor position */
        wlr_scene_node_set_position(&server->cursor_ring_buf->node,
                                    (int)(server->cursor->x - RINGS_SIZE_PX / 2.0),
                                    (int)(server->cursor->y - RINGS_SIZE_PX / 2.0));
    }

    wl_event_source_timer_update(server->cursor_ring_timer, RING_TICK_MS);
    return 0;
}

void
cursor_ring_start(nnwm_server *server)
{
    cursor_ring_stop(server);

    server->cursor_ring_x        = server->cursor->x;
    server->cursor_ring_y        = server->cursor->y;
    server->cursor_ring_progress = 0.0f;

    const char *style    = server->config->find_cursor_style;
    bool is_spotlight    = style && std::strcmp(style, "spotlight") == 0;
    bool is_zoom         = style && std::strcmp(style, "zoom")      == 0;

    /* Snapshot the output under the cursor */
    server->cursor_ring_out_x     = 0;
    server->cursor_ring_out_y     = 0;
    server->cursor_ring_out_w     = 800;
    server->cursor_ring_out_h     = 600;
    server->cursor_ring_out_scale = 1.0;
    {
        nnwm_output *out;
        wl_list_for_each(out, &server->outputs, link)
        {
            wlr_box ob;
            wlr_output_layout_get_box(server->output_layout, out->wlr_output, &ob);
            if (server->cursor_ring_x >= ob.x && server->cursor_ring_x < ob.x + ob.width &&
                server->cursor_ring_y >= ob.y && server->cursor_ring_y < ob.y + ob.height)
            {
                server->cursor_ring_out_x     = ob.x;
                server->cursor_ring_out_y     = ob.y;
                server->cursor_ring_out_w     = ob.width;
                server->cursor_ring_out_h     = ob.height;
                server->cursor_ring_out_scale = out->wlr_output->scale;
                break;
            }
        }
    }

    if (is_zoom)
    {
        /* Load cursor at ZOOM_FACTOR × output scale so the buffer has enough
         * physical pixels to stay sharp at peak zoom with no upscaling. */
        float out_scale  = (float)server->cursor_ring_out_scale;
        float load_scale = out_scale * (float)ZOOM_FACTOR;
        wlr_xcursor_manager_load(server->cursor_mgr, load_scale); /* idempotent */
        struct wlr_xcursor *xc = wlr_xcursor_manager_get_xcursor(
            server->cursor_mgr, "default", load_scale);
        if (!xc || xc->image_count == 0)
        {
            /* fallback: native scale */
            wlr_xcursor_manager_load(server->cursor_mgr, out_scale);
            xc = wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", out_scale);
            if (!xc || xc->image_count == 0)
                return;
            load_scale = out_scale;
        }
        struct wlr_xcursor_image *img = xc->images[0];

        /* Convert straight-alpha xcursor pixels → premultiplied ARGB32 for the scene */
        nnwm_tbuf *tb = tbuf_create((int)img->width, (int)img->height);
        const uint32_t *src = (const uint32_t *)img->buffer;
        for (uint32_t y = 0; y < img->height; y++)
        {
            const uint32_t *srow = src + y * img->width;
            uint32_t *drow = (uint32_t *)((uint8_t *)tb->data + (size_t)y * tb->stride);
            for (uint32_t x = 0; x < img->width; x++)
            {
                uint32_t p = srow[x];
                uint8_t  a = (p >> 24) & 0xff;
                uint8_t  r = (uint8_t)(((p >> 16) & 0xff) * a / 255);
                uint8_t  g = (uint8_t)(((p >>  8) & 0xff) * a / 255);
                uint8_t  b = (uint8_t)(((p >>  0) & 0xff) * a / 255);
                drow[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16)
                         | ((uint32_t)g <<  8) | b;
            }
        }

        /* Logical dimensions derived from the high-res load_scale */
        int lw = (int)((float)img->width  / load_scale);
        int lh = (int)((float)img->height / load_scale);
        int hx = (int)((float)img->hotspot_x / load_scale);
        int hy = (int)((float)img->hotspot_y / load_scale);

        server->cursor_zoom_img_w = lw;
        server->cursor_zoom_img_h = lh;
        server->cursor_zoom_hot_x = hx;
        server->cursor_zoom_hot_y = hy;

        server->cursor_ring_buf = wlr_scene_buffer_create(
            server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], nullptr);
        wlr_scene_buffer_set_buffer(server->cursor_ring_buf, &tb->base);
        wlr_buffer_drop(&tb->base);
        wlr_scene_buffer_set_dest_size(server->cursor_ring_buf, lw, lh);
        wlr_scene_node_set_position(&server->cursor_ring_buf->node,
                                    (int)(server->cursor_ring_x - hx),
                                    (int)(server->cursor_ring_y - hy));

        /* Hide the HW cursor; the scene buffer replaces it during the animation */
        wlr_cursor_unset_image(server->cursor);
        server->cursor_zoom_active = true;
        server->cursor_zoom_scale  = 1.0f;
    }
    else
    {
        server->cursor_ring_buf = wlr_scene_buffer_create(
            server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], nullptr);

        if (is_spotlight)
        {
            wlr_scene_node_set_position(&server->cursor_ring_buf->node,
                                        server->cursor_ring_out_x,
                                        server->cursor_ring_out_y);
            /* Pre-render at full opacity; tick animates via wlr_scene_buffer_set_opacity */
            spotlight_upload(server);
            wlr_scene_buffer_set_opacity(server->cursor_ring_buf, 0.0f);
        }
        else
        {
            wlr_scene_node_set_position(&server->cursor_ring_buf->node,
                                        (int)(server->cursor_ring_x - RINGS_SIZE_PX / 2.0),
                                        (int)(server->cursor_ring_y - RINGS_SIZE_PX / 2.0));
        }
    }

    server->cursor_ring_timer = wl_event_loop_add_timer(
        wl_display_get_event_loop(server->wl_display),
        cursor_ring_tick, server);
    wl_event_source_timer_update(server->cursor_ring_timer, 1);
}

void
show_config_error(nnwm_server *server, const char *message)
{
    const int bar_h   = 24;
    const float bg[4] = {0.7f, 0.1f, 0.1f, 1.0f};
    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link)
    {
        if (!out->error_bar)
            continue;
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output,
                                  &area);
        int W = area.width;
        if (W <= 0)
            continue;

        float escale  = out->wlr_output ? out->wlr_output->scale : 1.0f;
        int eW        = (int)(W * escale);
        int ebar_h    = (int)(bar_h * escale);
        nnwm_tbuf *tb         = tbuf_create(eW, ebar_h);
        cairo_surface_t *surf = cairo_image_surface_create_for_data(
            tb->data, CAIRO_FORMAT_ARGB32, eW, ebar_h, tb->stride);
        cairo_t *cr = cairo_create(surf);
        cairo_scale(cr, escale, escale);

        cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
        cairo_paint(cr);

        PangoLayout *layout      = pango_cairo_create_layout(cr);
        const char *font         = server->config->titlebar.font
                                       ? server->config->titlebar.font
                                       : "Sans 10";
        PangoFontDescription *fd = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);

        char text[1024];
        std::snprintf(text, sizeof(text), "Config error: %s", message);
        pango_layout_set_text(layout, text, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, (W - 8) * PANGO_SCALE);

        int lpw, lph;
        pango_layout_get_size(layout, &lpw, &lph);
        double ty = (bar_h - lph / (double)PANGO_SCALE) / 2.0;

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
    if (!server->error_dismiss_timer)
    {
        struct wl_event_loop *loop
            = wl_display_get_event_loop(server->wl_display);
        server->error_dismiss_timer
            = wl_event_loop_add_timer(loop, error_dismiss_cb, server);
    }
    wl_event_source_timer_update(server->error_dismiss_timer, 8000);
}

void
hide_config_error(nnwm_server *server)
{
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link)
    {
        if (out->error_bar)
            wlr_scene_node_set_enabled(&out->error_bar->node, false);
    }
    if (server->error_dismiss_timer)
    {
        wl_event_source_timer_update(server->error_dismiss_timer, 0);
    }
}
