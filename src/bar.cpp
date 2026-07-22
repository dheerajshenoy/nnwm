#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>

extern "C" {
#include <lauxlib.h>
#include <linux/input-event-codes.h>
#include <lua.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>
}

/* ---- CPU-side wlr_buffer for cairo pixels (mirrors nnwm.cpp::nnwm_tbuf) ---- */
namespace {

struct bar_tbuf {
    struct wlr_buffer base;
    uint8_t *data;
    int stride;
};

static void tbuf_destroy_impl(struct wlr_buffer *b) {
    bar_tbuf *tb = wl_container_of(b, tb, base);
    free(tb->data);
    free(tb);
}
static bool tbuf_data_ptr(struct wlr_buffer *b, uint32_t,
                          void **data, uint32_t *format, size_t *stride) {
    bar_tbuf *tb = wl_container_of(b, tb, base);
    *data = tb->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)tb->stride;
    return true;
}
static void tbuf_data_ptr_end(struct wlr_buffer *) {}

static const wlr_buffer_impl bar_tbuf_impl = {
    tbuf_destroy_impl, nullptr, nullptr, tbuf_data_ptr, tbuf_data_ptr_end,
};

static bar_tbuf *tbuf_new(int w, int h) {
    auto *tb = static_cast<bar_tbuf *>(calloc(1, sizeof(bar_tbuf)));
    tb->stride = w * 4;
    tb->data = static_cast<uint8_t *>(calloc(h, tb->stride));
    wlr_buffer_init(&tb->base, &bar_tbuf_impl, w, h);
    return tb;
}

/* ---- helpers ---- */

static double now_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static const char *layout_mode_name(nnwm_layout_mode m) {
    switch (m) {
        case nnwm_layout_mode::HTILE: return "H";
        case nnwm_layout_mode::VTILE: return "V";
        case nnwm_layout_mode::TABBED: return "T";
        case nnwm_layout_mode::HSCROLL: return "SH";
        case nnwm_layout_mode::VSCROLL: return "SV";
        case nnwm_layout_mode::FLOAT: return "F";
        default: return "?";
    }
}

/* Which output does a bar belong to for widget queries. For a global bar,
 * defaults to the server's focused_output. */
static nnwm_output *bar_target_output(nnwm_bar *bar) {
    if (bar->output) return bar->output;
    return bar->server->focused_output;
}

/* Return the focused toplevel on `out` (or the compositor's focused one for
 * global bars). May return NULL. */
static nnwm_toplevel *bar_focused_toplevel(nnwm_bar *bar) {
    nnwm_output *out = bar_target_output(bar);
    if (!out) return nullptr;
    nnwm_toplevel *tl = out->last_focused[out->active_workspace];
    return tl;
}

/* Draw a text segment with pango, return advance width. If `bg` alpha > 0
 * fills a rectangle behind. Returns the width consumed (text + padding*2). */
static int draw_text_segment(cairo_t *cr, PangoFontDescription *fd,
                             const char *text, int x, int bar_h,
                             const float fg[4], const float bg[4], int pad) {
    if (!text || !text[0]) return 0;

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_text(layout, text, -1);

    int text_w, text_h;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    int w = text_w + 2 * pad;

    if (bg && bg[3] > 0.0f) {
        cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
        cairo_rectangle(cr, x, 0, w, bar_h);
        cairo_fill(cr);
    }

    double ty = (bar_h - text_h) / 2.0;
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    cairo_move_to(cr, x + pad, ty);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    return w;
}

/* Build the text to display for a single module. Returns a heap-allocated
 * string the caller must free. NULL if the module renders nothing. For
 * modules with sub-segments (workspaces), returns NULL and caller uses
 * draw_workspaces_module directly. */
static char *module_render_text(nnwm_bar *bar, nnwm_bar_module &m,
                                double now) {
    nnwm_server *server = bar->server;
    nnwm_config *cfg = server->config;

    switch (m.type) {
        case nnwm_bar_module_type::WORKSPACES:
            return nullptr; /* rendered specially */

        case nnwm_bar_module_type::WINDOW_TITLE: {
            nnwm_toplevel *tl = bar_focused_toplevel(bar);
            if (!tl) return strdup("");
            const char *t = tl_title(tl);
            return strdup(t ? t : "");
        }

        case nnwm_bar_module_type::LAYOUT: {
            nnwm_output *out = bar_target_output(bar);
            if (!out) return strdup("");
            const char *n = layout_mode_name(out->layout_mode[out->active_workspace]);
            return strdup(n);
        }

        case nnwm_bar_module_type::CLOCK: {
            const char *fmt = m.format ? m.format : "%H:%M";
            time_t t = time(nullptr);
            struct tm tmv;
            localtime_r(&t, &tmv);
            char buf[128];
            if (strftime(buf, sizeof(buf), fmt, &tmv) == 0) buf[0] = 0;
            return strdup(buf);
        }

        case nnwm_bar_module_type::CUSTOM: {
            /* Poll only when interval elapsed (or first render). */
            bool need = !m.cached_text
                        || (m.interval_ms > 0
                            && (now - m.cached_ts) * 1000.0 >= m.interval_ms);
            if (!need) return m.cached_text ? strdup(m.cached_text) : strdup("");
            if (m.lua_update_ref < 0 || !server->lua)
                return strdup(m.cached_text ? m.cached_text : "");

            lua_State *L = server->lua;
            lua_rawgeti(L, LUA_REGISTRYINDEX, m.lua_update_ref);
            const char *result = "";
            if (lua_pcall(L, 0, 1, 0) == 0) {
                if (lua_isstring(L, -1)) result = lua_tostring(L, -1);
            } else {
                wlr_log(WLR_ERROR, "bar custom widget error: %s",
                        lua_tostring(L, -1));
            }
            free(m.cached_text);
            m.cached_text = strdup(result);
            m.cached_ts = now;
            lua_pop(L, 1);
            (void)cfg;
            return strdup(m.cached_text);
        }
    }
    return nullptr;
}

/* Pick module-level color if set (alpha >= 0), else the fallback. */
static const float *pick(const float mod[4], const float fallback[4]) {
    return mod[3] >= 0.0f ? mod : fallback;
}

/* Built-in default palettes for modules that render more than plain text.
 * These are used unless the module's `colors` sub-table overrides. */
static const float k_ws_active_bg[4]   = {0.30f, 0.50f, 0.80f, 1.0f};
static const float k_ws_active_fg[4]   = {1.0f,  1.0f,  1.0f,  1.0f};
static const float k_ws_occupied_fg[4] = {0.65f, 0.70f, 0.85f, 1.0f};
static const float k_ws_unocc_fg[4]    = {0.45f, 0.45f, 0.50f, 1.0f};

/* Draw the workspaces module. Returns width consumed. */
static int draw_workspaces_module(cairo_t *cr, PangoFontDescription *fd,
                                  nnwm_bar *bar, nnwm_bar_module &m,
                                  int x, int bar_h) {
    nnwm_server *server = bar->server;
    nnwm_config *cfg = server->config;
    nnwm_output *out = bar_target_output(bar);
    int active_ws = out ? out->active_workspace : 0;
    int count = cfg->workspace_count > 0 ? cfg->workspace_count : 9;
    if (count > NNWM_NUM_WORKSPACES) count = NNWM_NUM_WORKSPACES;

    /* Determine which workspaces are occupied on this output. */
    bool occupied[NNWM_NUM_WORKSPACES] = {false};
    if (out) {
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link) {
            if (tl->output != out) continue;
            if (tl->workspace < 0 || tl->workspace >= NNWM_NUM_WORKSPACES) continue;
            occupied[tl->workspace] = true;
        }
    }

    const float *c_active_bg   = pick(m.ws_active_bg,     k_ws_active_bg);
    const float *c_active_fg   = pick(m.ws_active_fg,     k_ws_active_fg);
    const float *c_occupied_fg = pick(m.ws_occupied_fg,   k_ws_occupied_fg);
    const float *c_unocc_fg    = pick(m.ws_unoccupied_fg, k_ws_unocc_fg);

    int pad = m.padding >= 0 ? m.padding : 8;
    int used = 0;
    for (int i = 0; i < count; i++) {
        const char *label = nullptr;
        if (out && out->workspace_names[i]) label = out->workspace_names[i];
        else if (cfg->workspace_names[i]) label = cfg->workspace_names[i];
        char buf[16];
        if (!label) {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            label = buf;
        }

        const float *fg;
        const float *bg = nullptr;
        if (i == active_ws) {
            fg = c_active_fg;
            bg = c_active_bg;
        } else if (occupied[i]) {
            fg = c_occupied_fg;
        } else {
            fg = c_unocc_fg;
        }
        used += draw_text_segment(cr, fd, label, x + used, bar_h, fg, bg, pad);
    }
    return used;
}

/* ---- Bar rendering ---- */

/* Build a compact string describing everything visible on the bar for the
 * current state. Used to skip cairo work when the composition is unchanged.
 * Also captures state that WORKSPACES/LAYOUT/WINDOW_TITLE would render, since
 * those don't populate cached_text via module_render_text. */
static char *bar_compute_signature(nnwm_bar *bar, double now) {
    nnwm_server *server = bar->server;
    nnwm_config *cfg = server->config;
    nnwm_output *out = bar_target_output(bar);

    /* Rough upper bound; grows as needed via realloc. */
    size_t cap = 256;
    size_t len = 0;
    char *buf = static_cast<char *>(malloc(cap));
    auto append = [&](const char *s) {
        size_t n = strlen(s);
        if (len + n + 1 > cap) {
            while (len + n + 1 > cap) cap *= 2;
            buf = static_cast<char *>(realloc(buf, cap));
        }
        memcpy(buf + len, s, n);
        len += n;
        buf[len] = '\0';
    };
    char tmp[128];

    /* Bar geometry — different size means different layout. */
    snprintf(tmp, sizeof(tmp), "W%dH%d|", bar->width, bar->height);
    append(tmp);

    /* Workspace state (active + occupancy bitmap) — shared across all
     * WORKSPACES modules. */
    if (out) {
        snprintf(tmp, sizeof(tmp), "A%d/", out->active_workspace);
        append(tmp);
        for (int i = 0; i < NNWM_NUM_WORKSPACES; i++) {
            int cnt = 0;
            nnwm_toplevel *tl;
            wl_list_for_each(tl, &server->toplevels, link) {
                if (tl->output == out && tl->workspace == i) cnt++;
            }
            snprintf(tmp, sizeof(tmp), "%d,", cnt);
            append(tmp);
        }
        append("|");
    }

    for (int i = 0; i < cfg->bar.module_count; i++) {
        nnwm_bar_module &m = cfg->bar.modules[i];
        append("[");
        switch (m.type) {
            case nnwm_bar_module_type::WORKSPACES:
                append("ws"); /* covered by shared block above */
                break;
            case nnwm_bar_module_type::LAYOUT:
                if (out) append(layout_mode_name(
                    out->layout_mode[out->active_workspace]));
                break;
            case nnwm_bar_module_type::WINDOW_TITLE: {
                nnwm_toplevel *tl = bar_focused_toplevel(bar);
                const char *t = tl ? tl_title(tl) : nullptr;
                append(t ? t : "");
                break;
            }
            case nnwm_bar_module_type::CLOCK:
            case nnwm_bar_module_type::CUSTOM:
                /* module_render_text handles caching; call it just to
                 * populate cached_text, then read it. */
                {
                    char *t = module_render_text(bar, m, now);
                    if (t) append(t);
                    free(t);
                }
                break;
        }
        append("]");
    }
    return buf;
}

static void bar_redraw(nnwm_bar *bar) {
    if (!bar || !bar->content) return;
    nnwm_server *server = bar->server;
    nnwm_config *cfg = server->config;
    nnwm_output *out = bar_target_output(bar);
    if (!out || !out->wlr_output) return;

    /* Skip rendering when nothing user-visible changed since last frame. */
    double now = now_seconds();
    char *sig = bar_compute_signature(bar, now);
    if (bar->last_signature && strcmp(bar->last_signature, sig) == 0) {
        free(sig);
        return;
    }
    free(bar->last_signature);
    bar->last_signature = sig;
    bar->last_signature_len = (int)strlen(sig);

    float scale = out->wlr_output->scale;
    int pw = (int)(bar->width * scale);
    int ph = (int)(bar->height * scale);
    if (pw <= 0 || ph <= 0) return;

    bar_tbuf *tb = tbuf_new(pw, ph);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        tb->data, CAIRO_FORMAT_ARGB32, pw, ph, tb->stride);
    cairo_t *cr = cairo_create(surf);
    cairo_scale(cr, scale, scale);

    /* Transparent — background rect is a scene_rect drawn separately so
     * scenefx blur can attach to it. Cairo layer only carries text. */

    PangoFontDescription *fd = pango_font_description_from_string(
        cfg->bar.font ? cfg->bar.font : "monospace 11");
    const char *fam = pango_font_description_get_family(fd);
    char family[256];
    snprintf(family, sizeof(family), "%s,DejaVu Sans,Noto Sans,Liberation Sans,Sans",
             fam ? fam : "Sans");
    pango_font_description_set_family(fd, family);

    /* Bar edges are at x=0..bar->width; padding is applied as outer margin
     * in bar_layout, so modules can flush against the bar's own edges. */
    int spacing = cfg->bar.module_spacing;

    /* Two-pass layout: measure left/right widths, then draw. Center is
     * anchored to bar center. */
    struct seg { char *text; int w; nnwm_bar_module *m; bool is_ws; };
    seg *segs = static_cast<seg *>(calloc(cfg->bar.module_count, sizeof(seg)));
    /* First pass: pre-render text and measure. */
    for (int i = 0; i < cfg->bar.module_count; i++) {
        nnwm_bar_module &m = cfg->bar.modules[i];
        segs[i].m = &m;
        if (m.type == nnwm_bar_module_type::WORKSPACES) {
            segs[i].is_ws = true;
            /* Measure by drawing to a scratch cr. Cheap enough. */
            cairo_surface_t *ss = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            cairo_t *sc = cairo_create(ss);
            segs[i].w = draw_workspaces_module(sc, fd, bar, m, 0, bar->height);
            cairo_destroy(sc);
            cairo_surface_destroy(ss);
        } else {
            segs[i].text = module_render_text(bar, m, now);
            if (!segs[i].text || !segs[i].text[0]) { segs[i].w = 0; continue; }
            PangoLayout *layout = pango_cairo_create_layout(cr);
            pango_layout_set_font_description(layout, fd);
            pango_layout_set_text(layout, segs[i].text, -1);
            int tw, th;
            pango_layout_get_pixel_size(layout, &tw, &th);
            g_object_unref(layout);
            int pad = m.padding >= 0 ? m.padding : 6;
            segs[i].w = tw + 2 * pad;
        }
    }

    /* Compute cursors for each alignment group. */
    int left_x = 0;
    int right_total = 0;
    int center_total = 0;
    for (int i = 0; i < cfg->bar.module_count; i++) {
        int w = segs[i].w;
        if (w <= 0) continue;
        switch (segs[i].m->align) {
            case nnwm_bar_align::LEFT: break;
            case nnwm_bar_align::RIGHT:
                right_total += w + (right_total > 0 ? spacing : 0);
                break;
            case nnwm_bar_align::CENTER:
                center_total += w + (center_total > 0 ? spacing : 0);
                break;
        }
    }
    int right_x = bar->width - right_total;
    int center_x = (bar->width - center_total) / 2;
    int cursor[3] = { left_x, center_x, right_x };

    /* Reset every module's rect; only re-populate the ones we actually
     * draw. Modules with rect_w<=0 are excluded from hit-testing. */
    for (int i = 0; i < cfg->bar.module_count; i++) {
        cfg->bar.modules[i].rect_x = 0;
        cfg->bar.modules[i].rect_y = 0;
        cfg->bar.modules[i].rect_w = 0;
        cfg->bar.modules[i].rect_h = bar->height;
    }

    /* Second pass: draw. For window_title in the center, clip to available
     * space between left cursor and right region. */
    int left_cursor = left_x;
    for (int i = 0; i < cfg->bar.module_count; i++) {
        seg &s = segs[i];
        if (s.w <= 0 && !s.is_ws) continue;
        int idx = (int)s.m->align;
        int x = cursor[idx];

        const float *fg = (s.m->fg[3] >= 0.0f) ? s.m->fg : cfg->bar.fg_color;
        const float *bg = (s.m->bg[3] > 0.0f) ? s.m->bg : nullptr;
        int pad = s.m->padding >= 0 ? s.m->padding : 6;
        int drawn = 0;

        if (s.is_ws) {
            drawn = draw_workspaces_module(cr, fd, bar, *s.m, x, bar->height);
        } else if (s.m->type == nnwm_bar_module_type::WINDOW_TITLE
                   && s.m->align == nnwm_bar_align::CENTER) {
            /* Ellipsize to available space between left and right groups. */
            int avail_left = left_cursor + spacing;
            int avail_right = right_x - spacing;
            if (avail_right <= avail_left) { free(s.text); continue; }
            int max_w = avail_right - avail_left;
            if (s.w > max_w) {
                PangoLayout *layout = pango_cairo_create_layout(cr);
                pango_layout_set_font_description(layout, fd);
                pango_layout_set_text(layout, s.text, -1);
                pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
                pango_layout_set_width(layout, (max_w - 2 * pad) * PANGO_SCALE);
                int tw, th;
                pango_layout_get_pixel_size(layout, &tw, &th);
                int rx = avail_left + (max_w - (tw + 2 * pad)) / 2;
                if (bg) {
                    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
                    cairo_rectangle(cr, rx, 0, tw + 2 * pad, bar->height);
                    cairo_fill(cr);
                }
                cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
                cairo_move_to(cr, rx + pad, (bar->height - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                g_object_unref(layout);
                drawn = tw + 2 * pad;
                x = rx;
            } else {
                drawn = draw_text_segment(cr, fd, s.text, x, bar->height, fg, bg, pad);
            }
            free(s.text);
        } else {
            drawn = draw_text_segment(cr, fd, s.text, x, bar->height, fg, bg, pad);
            free(s.text);
        }

        /* Record hit-test rect (bar-local coordinates). */
        if (drawn > 0) {
            s.m->rect_x = x;
            s.m->rect_y = 0;
            s.m->rect_w = drawn;
            s.m->rect_h = bar->height;
        }

        cursor[idx] += drawn + spacing;
        if (s.m->align == nnwm_bar_align::LEFT) left_cursor = cursor[idx];
    }
    free(segs);

    pango_font_description_free(fd);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wlr_scene_buffer_set_buffer(bar->content, &tb->base);
    wlr_scene_buffer_set_dest_size(bar->content, bar->width, bar->height);
    wlr_buffer_drop(&tb->base);
    bar->dirty = false;
}

/* Reposition and resize the bar's scene nodes. `padding` acts as CSS-style
 * outer margin: the visible bar rect shrinks horizontally by left+right and
 * is offset vertically by top (or bottom) from the output edge. */
static void bar_layout(nnwm_bar *bar) {
    nnwm_server *server = bar->server;
    nnwm_config *cfg = server->config;
    nnwm_output *out = bar_target_output(bar);
    if (!out || !out->wlr_output) return;

    wlr_box ob;
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &ob);

    int mt = cfg->bar.padding.top;
    int mr = cfg->bar.padding.right;
    int mb = cfg->bar.padding.bottom;
    int ml = cfg->bar.padding.left;

    bar->width = ob.width - ml - mr;
    if (bar->width < 1) bar->width = 1;
    bar->height = cfg->bar.height;
    bar->position_top = cfg->bar.position_top;
    bar->x = ob.x + ml;
    bar->y = cfg->bar.position_top
        ? ob.y + mt
        : ob.y + ob.height - bar->height - mb;

    wlr_scene_node_set_position(&bar->tree->node, bar->x, bar->y);
    wlr_scene_rect_set_size(bar->bg_rect, bar->width, bar->height);
    wlr_scene_node_set_position(&bar->bg_rect->node, 0, 0);
    if (bar->content)
        wlr_scene_node_set_position(&bar->content->node, 0, 0);
    wlr_scene_rect_set_color(bar->bg_rect, cfg->bar.bg_color);

#ifdef HAVE_SCENEFX
    int r = cfg->bar.fx.corner_radius;
    wlr_scene_rect_set_corner_radius(bar->bg_rect, r);
    if (bar->content)
        wlr_scene_buffer_set_corner_radius(bar->content, r);
    if (bar->fx_shadow) {
        /* scenefx's shadow shader treats the shadow rect as the outer
         * bounding box: the "solid" shape inside is (size - 2*sigma) and
         * the Gaussian falls off between that inner shape and the outer
         * rect. So to get a drop shadow that matches the bar and blooms
         * outward, we expand by 2*sigma on each dimension and shift the
         * node by -sigma so the inner shape aligns with the bar. */
        int sigma = (int)cfg->bar.fx.shadow_blur_sigma;
        int sw = bar->width  + 2 * sigma;
        int sh = bar->height + 2 * sigma;
        wlr_scene_shadow_set_size(bar->fx_shadow, sw, sh);
        wlr_scene_shadow_set_corner_radius(bar->fx_shadow, r);
        wlr_scene_shadow_set_blur_sigma(bar->fx_shadow,
                                        cfg->bar.fx.shadow_blur_sigma);
        wlr_scene_shadow_set_color(bar->fx_shadow, cfg->bar.fx.shadow_color);
        wlr_scene_node_set_position(&bar->fx_shadow->node,
                                    -sigma + (int)cfg->bar.fx.shadow_offset_x,
                                    -sigma + (int)cfg->bar.fx.shadow_offset_y);
    }
    if (bar->fx_blur) {
        wlr_scene_blur_set_size(bar->fx_blur, bar->width, bar->height);
        wlr_scene_blur_set_corner_radius(bar->fx_blur, r);
    }
#endif
}

/* Compute the tick cadence for time-based modules. Returns 0 if none exist,
 * otherwise the minimum interval in ms (clamped to a floor). */
static int bar_tick_interval_ms(nnwm_config *cfg) {
    int best = 0;
    for (int i = 0; i < cfg->bar.module_count; i++) {
        nnwm_bar_module &m = cfg->bar.modules[i];
        int cand = 0;
        if (m.type == nnwm_bar_module_type::CLOCK) cand = 1000;
        else if (m.type == nnwm_bar_module_type::CUSTOM)
            cand = m.interval_ms > 0 ? m.interval_ms : 1000;
        if (cand > 0 && (best == 0 || cand < best)) best = cand;
    }
    /* Don't hammer the event loop faster than 100ms even if the user asks. */
    if (best > 0 && best < 100) best = 100;
    return best;
}

static int bar_tick_cb(void *data) {
    nnwm_bar *bar = static_cast<nnwm_bar *>(data);
    bar_redraw(bar);
    if (bar->tick_timer) {
        int ms = bar_tick_interval_ms(bar->server->config);
        if (ms > 0)
            wl_event_source_timer_update(bar->tick_timer, ms);
    }
    return 0;
}

/* Create a bar attached to `out` (per-output) or to no output (global). */
static nnwm_bar *bar_create(nnwm_server *server, nnwm_output *out) {
    nnwm_config *cfg = server->config;
    if (cfg->bar.height <= 0) return nullptr;

    auto *bar = new nnwm_bar{};
    bar->server = server;
    bar->output = out;
    bar->hover_module_idx = -2;

    /* Place in the TOP layer tree so it renders above tiled/floating windows
     * but below OVERLAY (which hosts DnD icons and unmanaged xwayland). */
    bar->tree = wlr_scene_tree_create(
        server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);

#ifdef HAVE_SCENEFX
    /* Order in the tree (back-to-front): shadow, blur, bg_rect, content.
     * scene children render in insertion order, so create them in that
     * sequence. */
    if (cfg->bar.fx.shadow_enabled) {
        bar->fx_shadow = wlr_scene_shadow_create(
            bar->tree, 1, 1, cfg->bar.fx.corner_radius,
            cfg->bar.fx.shadow_blur_sigma, cfg->bar.fx.shadow_color);
    }
    if (cfg->bar.fx.blur_enabled) {
        bar->fx_blur = wlr_scene_blur_create(bar->tree, 1, 1);
    }
#endif

    bar->bg_rect = wlr_scene_rect_create(bar->tree, 1, 1, cfg->bar.bg_color);
    bar->content = wlr_scene_buffer_create(bar->tree, nullptr);

    bar_layout(bar);
    bar_redraw(bar);

    /* Only run a periodic tick when the bar actually hosts a time-based
     * module. Event-driven modules (workspaces, window_title, layout) are
     * redrawn via bar_notify_* on their triggers instead. */
    int tick_ms = bar_tick_interval_ms(cfg);
    if (tick_ms > 0) {
        bar->tick_timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), bar_tick_cb, bar);
        wl_event_source_timer_update(bar->tick_timer, tick_ms);
    }

    return bar;
}

static void bar_destroy(nnwm_bar *bar) {
    if (!bar) return;
    if (bar->tick_timer) wl_event_source_remove(bar->tick_timer);
    if (bar->tree) wlr_scene_node_destroy(&bar->tree->node);
    free(bar->last_signature);
    delete bar;
}

} /* anonymous namespace */

/* ---- Public API ---- */

void bar_apply_config(nnwm_server *server) {
    nnwm_config *cfg = server->config;

    /* Tear down existing bars — simpler than diffing config. */
    bar_destroy(server->global_bar);
    server->global_bar = nullptr;
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        bar_destroy(out->bar);
        out->bar = nullptr;
    }

    if (!cfg->bar.enabled || cfg->bar.height <= 0) return;

    if (cfg->bar.per_output) {
        wl_list_for_each(out, &server->outputs, link) {
            out->bar = bar_create(server, out);
        }
    } else {
        /* Single bar: attach to the named output if given, else the focused
         * one, else the first available. */
        nnwm_output *target = nullptr;
        if (cfg->bar.output_name && cfg->bar.output_name[0]) {
            wl_list_for_each(out, &server->outputs, link) {
                if (out->wlr_output && out->wlr_output->name
                    && strcmp(out->wlr_output->name, cfg->bar.output_name) == 0) {
                    target = out;
                    break;
                }
            }
        }
        if (!target) target = server->focused_output;
        if (!target && !wl_list_empty(&server->outputs))
            target = wl_container_of(server->outputs.next, target, link);
        if (target) server->global_bar = bar_create(server, target);
    }
}

/* Subtract bar height from usable_area if a bar occupies this output. */
void bar_shrink_usable_area(nnwm_server *server, nnwm_output *out,
                            wlr_box *usable) {
    nnwm_config *cfg = server->config;
    if (!cfg->bar.enabled || cfg->bar.height <= 0) return;

    bool has_bar = false;
    if (cfg->bar.per_output) has_bar = (out->bar != nullptr);
    else has_bar = (server->global_bar
                    && bar_target_output(server->global_bar) == out);
    if (!has_bar) return;

    /* Reserve height + vertical outer margins on the anchored edge so tiled
     * windows don't overlap a floating (padded) bar. */
    int mt = cfg->bar.padding.top;
    int mb = cfg->bar.padding.bottom;
    int reserved = cfg->bar.height + mt + mb;
    if (cfg->bar.position_top) {
        usable->y += reserved;
        usable->height -= reserved;
    } else {
        usable->height -= reserved;
    }
    if (usable->height < 1) usable->height = 1;
}

/* Are any modules present that care about the given trigger? Avoids paying
 * cairo/pango cost for state changes the current bar doesn't display. */
static bool bar_uses_module(nnwm_config *cfg, nnwm_bar_module_type t) {
    for (int i = 0; i < cfg->bar.module_count; i++)
        if (cfg->bar.modules[i].type == t) return true;
    return false;
}

void bar_notify_workspace_change(nnwm_server *server, nnwm_output *out) {
    nnwm_config *cfg = server->config;
    /* Workspace-switching also changes the "focused window" for that output,
     * so window_title-only bars are affected too. */
    if (!bar_uses_module(cfg, nnwm_bar_module_type::WORKSPACES)
        && !bar_uses_module(cfg, nnwm_bar_module_type::WINDOW_TITLE)
        && !bar_uses_module(cfg, nnwm_bar_module_type::LAYOUT))
        return;
    if (out && out->bar) bar_redraw(out->bar);
    if (server->global_bar) bar_redraw(server->global_bar);
}

void bar_notify_focus_change(nnwm_server *server) {
    nnwm_config *cfg = server->config;
    if (!bar_uses_module(cfg, nnwm_bar_module_type::WINDOW_TITLE))
        return;
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->bar) bar_redraw(out->bar);
    }
    if (server->global_bar) bar_redraw(server->global_bar);
}

void bar_notify_windows_changed(nnwm_server *server) {
    nnwm_config *cfg = server->config;
    /* Window open/close affects both workspace-occupancy and current title. */
    if (!bar_uses_module(cfg, nnwm_bar_module_type::WORKSPACES)
        && !bar_uses_module(cfg, nnwm_bar_module_type::WINDOW_TITLE))
        return;
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->bar) bar_redraw(out->bar);
    }
    if (server->global_bar) bar_redraw(server->global_bar);
}

void bar_destroy_all(nnwm_server *server) {
    bar_destroy(server->global_bar);
    server->global_bar = nullptr;
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        bar_destroy(out->bar);
        out->bar = nullptr;
    }
}

void bar_destroy_for_output(nnwm_output *out) {
    if (!out) return;
    bar_destroy(out->bar);
    out->bar = nullptr;
}

/* ---- Cursor event dispatch ---- */

/* Locate a bar the cursor is currently over. Returns the bar and the
 * cursor's bar-local coordinates, or nullptr if none. */
static nnwm_bar *bar_at(nnwm_server *server, double lx, double ly,
                        double *local_x, double *local_y) {
    auto hit = [&](nnwm_bar *bar) -> bool {
        if (!bar || !bar->tree || !bar->tree->node.enabled) return false;
        if (lx < bar->x || lx >= bar->x + bar->width) return false;
        if (ly < bar->y || ly >= bar->y + bar->height) return false;
        *local_x = lx - bar->x;
        *local_y = ly - bar->y;
        return true;
    };
    if (hit(server->global_bar)) return server->global_bar;
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link)
        if (hit(out->bar)) return out->bar;
    return nullptr;
}

/* Find the module rect containing (bx, by) in bar-local coords. Returns
 * the module index, or -1 if none (i.e. cursor is over the bar background). */
static int bar_module_at(nnwm_bar *bar, double bx, double by) {
    nnwm_config *cfg = bar->server->config;
    for (int i = 0; i < cfg->bar.module_count; i++) {
        nnwm_bar_module &m = cfg->bar.modules[i];
        if (m.rect_w <= 0) continue;
        if (bx >= m.rect_x && bx < m.rect_x + m.rect_w
            && by >= m.rect_y && by < m.rect_y + m.rect_h)
            return i;
    }
    return -1;
}

/* Translate a linux input button code into a short Lua-friendly string. */
static const char *button_name(uint32_t button) {
    switch (button) {
        case BTN_LEFT:    return "left";
        case BTN_RIGHT:   return "right";
        case BTN_MIDDLE:  return "middle";
        case BTN_SIDE:    return "side";
        case BTN_EXTRA:   return "extra";
        case BTN_FORWARD: return "forward";
        case BTN_BACK:    return "back";
        default:          return "other";
    }
}

static void fire_hover(nnwm_server *server, int ref, bool entered) {
    if (ref < 0 || !server->lua) return;
    lua_State *L = server->lua;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushboolean(L, entered);
    if (lua_pcall(L, 1, 0, 0) != 0) {
        wlr_log(WLR_ERROR, "bar on_hover: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

bool bar_handle_motion(nnwm_server *server, double lx, double ly) {
    double bx, by;
    nnwm_bar *bar = bar_at(server, lx, ly, &bx, &by);
    nnwm_config *cfg = server->config;

    /* Not on any bar — clear hover state on all bars. */
    if (!bar) {
        auto leave = [&](nnwm_bar *b) {
            if (!b) return;
            if (b->hover_module_idx == -2) return;
            if (b->hover_module_idx >= 0
                && b->hover_module_idx < cfg->bar.module_count) {
                fire_hover(server,
                    cfg->bar.modules[b->hover_module_idx].lua_hover_ref, false);
            } else if (b->hover_module_idx == -1) {
                fire_hover(server, cfg->bar.lua_hover_ref, false);
            }
            b->hover_module_idx = -2;
        };
        leave(server->global_bar);
        nnwm_output *out;
        wl_list_for_each(out, &server->outputs, link) leave(out->bar);
        return false;
    }

    int idx = bar_module_at(bar, bx, by);
    if (idx == bar->hover_module_idx) return true; /* still consuming events */

    /* Leave old */
    if (bar->hover_module_idx >= 0
        && bar->hover_module_idx < cfg->bar.module_count) {
        fire_hover(server,
            cfg->bar.modules[bar->hover_module_idx].lua_hover_ref, false);
    } else if (bar->hover_module_idx == -1) {
        fire_hover(server, cfg->bar.lua_hover_ref, false);
    }
    /* Enter new */
    if (idx >= 0) {
        fire_hover(server, cfg->bar.modules[idx].lua_hover_ref, true);
    } else {
        fire_hover(server, cfg->bar.lua_hover_ref, true);
    }
    bar->hover_module_idx = idx;
    return true;
}

bool bar_handle_button(nnwm_server *server, double lx, double ly,
                       uint32_t button, bool pressed) {
    double bx, by;
    nnwm_bar *bar = bar_at(server, lx, ly, &bx, &by);
    if (!bar) return false;

    /* Only PRESS triggers on_click (release is swallowed too so the client
     * below never sees a stray release without a matching press). */
    if (!pressed) return true;

    nnwm_config *cfg = server->config;
    int idx = bar_module_at(bar, bx, by);
    int click_ref = (idx >= 0)
        ? cfg->bar.modules[idx].lua_click_ref
        : cfg->bar.lua_click_ref;
    if (click_ref < 0 || !server->lua) return true; /* still consume */

    lua_State *L = server->lua;
    lua_rawgeti(L, LUA_REGISTRYINDEX, click_ref);
    lua_pushstring(L, button_name(button));
    lua_pushinteger(L, (lua_Integer)bx);
    lua_pushinteger(L, (lua_Integer)by);
    if (lua_pcall(L, 3, 0, 0) != 0) {
        wlr_log(WLR_ERROR, "bar on_click: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return true;
}

void bar_update_module(nnwm_server *server, const char *name) {
    if (!name || !name[0]) return;
    nnwm_config *cfg = server->config;

    /* Invalidate the cached text for every module matching `name` so the
     * next redraw calls module_render_text (which for CUSTOM re-invokes the
     * Lua update fn regardless of interval). */
    bool any = false;
    for (int i = 0; i < cfg->bar.module_count; i++) {
        nnwm_bar_module &m = cfg->bar.modules[i];
        if (m.name && strcmp(m.name, name) == 0) {
            m.cached_ts = 0;      /* force re-poll on next module_render_text */
            free(m.cached_text);
            m.cached_text = nullptr;
            any = true;
        }
    }
    if (!any) return;

    /* Invalidate every bar's signature so the redraw doesn't short-circuit
     * against the stale composition. */
    if (server->global_bar) {
        free(server->global_bar->last_signature);
        server->global_bar->last_signature = nullptr;
        bar_redraw(server->global_bar);
    }
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (!out->bar) continue;
        free(out->bar->last_signature);
        out->bar->last_signature = nullptr;
        bar_redraw(out->bar);
    }
}
