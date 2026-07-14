#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <cstdio>
#include <cstring>
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

/* ---- Borders and surface placement ---- */

void
update_borders(nnwm_toplevel *toplevel, int width, int height, int bw)
{
    int th = toplevel->server->config->titlebar_height;

    /* border[0]: top */
    wlr_scene_node_set_position(&toplevel->border[0]->node, 0, 0);
    wlr_scene_rect_set_size(toplevel->border[0], width, bw);

    /* border[1]: bottom */
    wlr_scene_node_set_position(&toplevel->border[1]->node, 0, height - bw);
    wlr_scene_rect_set_size(toplevel->border[1], width, bw);

    /* border[2]: left */
    wlr_scene_node_set_position(&toplevel->border[2]->node, 0, bw);
    wlr_scene_rect_set_size(toplevel->border[2], bw, height - 2 * bw);

    /* border[3]: right */
    wlr_scene_node_set_position(&toplevel->border[3]->node, width - bw, bw);
    wlr_scene_rect_set_size(toplevel->border[3], bw, height - 2 * bw);

    /* titlebar sits between top border and content */
    if (toplevel->titlebar) {
        bool enabled = (th > 0);
        wlr_scene_node_set_enabled(&toplevel->titlebar->node, enabled);
        wlr_scene_node_set_position(&toplevel->titlebar->node, bw, bw);
    }

    /* window surface is pushed down by the titlebar */
    wlr_scene_node_set_position(&toplevel->scene_surface->node, bw, bw + th);
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
            if (tl->titlebar)
                wlr_scene_node_set_enabled(&tl->titlebar->node, false);
            wlr_scene_node_set_position(&tl->scene_tree->node, cx, cy);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, cw - 2 * bw, ch - 2 * bw);
            update_borders(tl, cw, ch, bw);
            /* Override scene_surface position: no per-window titlebar offset */
            wlr_scene_node_set_position(&tl->scene_surface->node, bw, bw);
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
            wlr_scene_node_set_position(&tl->scene_tree->node, x0, y0);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, W - 2 * bw, H - 2 * bw - th);
            update_borders(tl, W, H, bw);
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
                wlr_scene_node_set_position(&tl->scene_tree->node, x0, y0);
                wlr_xdg_toplevel_set_size(tl->xdg_toplevel, mw - 2 * bw, H - 2 * bw - th);
                update_borders(tl, mw, H, bw);
                render_titlebar(tl, mw - 2 * bw, focused);
            } else {
                int sy = y0 + (i - 1) * (sh + ig);
                int h  = (i < ns) ? sh : H - (i - 1) * (sh + ig);
                wlr_scene_node_set_position(&tl->scene_tree->node, x0 + mw + ig, sy);
                wlr_xdg_toplevel_set_size(tl->xdg_toplevel, sw - 2 * bw, h - 2 * bw - th);
                update_borders(tl, sw, h, bw);
                render_titlebar(tl, sw - 2 * bw, focused);
            }
            ++i;
        }
    }

    /* Floating and fullscreen windows must always sit above tiled ones. */
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->output == out && (tl->workspace == ws || tl->sticky)
                && (tl->floating || tl->fullscreen))
            wlr_scene_node_raise_to_top(&tl->scene_tree->node);
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
        for (int i = 0; i < 4; i++)
            wlr_scene_rect_set_color(tl->border[i], color);
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

        if (out->layout_mode[ws] == NNWM_LAYOUT_TABBED)
            arrange_windows(server, out);
    }
}

void
unfocus_all_borders(nnwm_server *server)
{
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        for (int i = 0; i < 4; i++)
            wlr_scene_rect_set_color(tl->border[i], server->config->unfocused_color);
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
