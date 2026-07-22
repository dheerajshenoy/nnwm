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
            fg = cfg->bar.active_ws_fg;
            bg = cfg->bar.active_ws_bg;
        } else if (occupied[i]) {
            fg = cfg->bar.occupied_ws_fg;
        } else {
            fg = cfg->bar.fg_color;
        }
        used += draw_text_segment(cr, fd, label, x + used, bar_h, fg, bg, pad);
    }
    return used;
}

/* ---- Bar rendering ---- */

static void bar_redraw(nnwm_bar *bar) {
    if (!bar || !bar->content) return;
    nnwm_server *server = bar->server;
    nnwm_config *cfg = server->config;
    nnwm_output *out = bar_target_output(bar);
    if (!out || !out->wlr_output) return;

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

    double now = now_seconds();
    int edge_pad = cfg->bar.padding;
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
    int left_x = edge_pad;
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
    int right_x = bar->width - edge_pad - right_total;
    int center_x = (bar->width - center_total) / 2;
    int cursor[3] = { left_x, center_x, right_x };

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

/* Reposition and resize the bar's scene nodes. */
static void bar_layout(nnwm_bar *bar) {
    nnwm_server *server = bar->server;
    nnwm_config *cfg = server->config;
    nnwm_output *out = bar_target_output(bar);
    if (!out || !out->wlr_output) return;

    wlr_box ob;
    wlr_output_layout_get_box(server->output_layout, out->wlr_output, &ob);

    bar->width = ob.width;
    bar->height = cfg->bar.height;
    bar->position_top = cfg->bar.position_top;
    bar->x = ob.x;
    bar->y = cfg->bar.position_top ? ob.y : ob.y + ob.height - bar->height;

    wlr_scene_node_set_position(&bar->tree->node, bar->x, bar->y);
    wlr_scene_rect_set_size(bar->bg_rect, bar->width, bar->height);
    wlr_scene_node_set_position(&bar->bg_rect->node, 0, 0);
    if (bar->content)
        wlr_scene_node_set_position(&bar->content->node, 0, 0);
    wlr_scene_rect_set_color(bar->bg_rect, cfg->bar.bg_color);
}

static int bar_tick_cb(void *data) {
    nnwm_bar *bar = static_cast<nnwm_bar *>(data);
    bar_redraw(bar);
    if (bar->tick_timer)
        wl_event_source_timer_update(bar->tick_timer, 1000);
    return 0;
}

/* Create a bar attached to `out` (per-output) or to no output (global). */
static nnwm_bar *bar_create(nnwm_server *server, nnwm_output *out) {
    nnwm_config *cfg = server->config;
    if (cfg->bar.height <= 0) return nullptr;

    auto *bar = new nnwm_bar{};
    bar->server = server;
    bar->output = out;

    /* Place in the TOP layer tree so it renders above tiled/floating windows
     * but below OVERLAY (which hosts DnD icons and unmanaged xwayland). */
    bar->tree = wlr_scene_tree_create(
        server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);
    bar->bg_rect = wlr_scene_rect_create(bar->tree, 1, 1, cfg->bar.bg_color);
    bar->content = wlr_scene_buffer_create(bar->tree, nullptr);

    bar_layout(bar);
    bar_redraw(bar);

    /* Periodic tick for CLOCK and CUSTOM modules. 1s cadence is fine for
     * everything the built-in modules render. */
    bar->tick_timer = wl_event_loop_add_timer(
        wl_display_get_event_loop(server->wl_display), bar_tick_cb, bar);
    wl_event_source_timer_update(bar->tick_timer, 1000);

    return bar;
}

static void bar_destroy(nnwm_bar *bar) {
    if (!bar) return;
    if (bar->tick_timer) wl_event_source_remove(bar->tick_timer);
    if (bar->tree) wlr_scene_node_destroy(&bar->tree->node);
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

    int h = cfg->bar.height;
    if (cfg->bar.position_top) {
        usable->y += h;
        usable->height -= h;
    } else {
        usable->height -= h;
    }
    if (usable->height < 1) usable->height = 1;
}

void bar_notify_workspace_change(nnwm_server *server, nnwm_output *out) {
    if (out && out->bar) bar_redraw(out->bar);
    if (server->global_bar) bar_redraw(server->global_bar);
}

void bar_notify_focus_change(nnwm_server *server) {
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->bar) bar_redraw(out->bar);
    }
    if (server->global_bar) bar_redraw(server->global_bar);
}

void bar_notify_windows_changed(nnwm_server *server) {
    bar_notify_focus_change(server);
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
