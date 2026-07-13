#include "nnwm.hpp"
#include "lua/config.hpp"
#include <cassert>
#include <cstdio>
#include <linux/input-event-codes.h>
#include <drm_fourcc.h>
extern "C" {
#include <wlr/interfaces/wlr_buffer.h>
}
#include <pango/pangocairo.h>

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

/* ---- Titlebar rendering ---- */

static void
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
        /* Add font fallback to ensure characters like tilde (~) render correctly */
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

        const float *tc = cfg->titlebar_text_color;
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

/* ---- Output / workspace helpers ---- */

/* True if any output is currently showing workspace ws. */
static bool
workspace_is_visible(nnwm_server *server, int ws)
{
    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link)
        if (o->active_workspace == ws) return true;
    return false;
}

/* Output currently showing workspace ws, or nullptr. */
static nnwm_output *
output_for_workspace(nnwm_server *server, int ws)
{
    nnwm_output *o;
    wl_list_for_each(o, &server->outputs, link)
        if (o->active_workspace == ws) return o;
    return nullptr;
}

/* Next (dir=+1) or previous (dir=-1) output relative to cur, wrapping. */
static nnwm_output *
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

/* Output whose area contains the cursor, or nullptr. */
static nnwm_output *
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

/* ---- Workspace helpers (scoped to a single output) ---- */

/* First tiled toplevel on out's active workspace, or nullptr. */
static nnwm_toplevel *
ws_first(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
        if (t->workspace == out->active_workspace && !t->floating && !t->fullscreen)
            return t;
    return nullptr;
}

/* Next tiled toplevel on out's workspace after cur, without wrapping. */
static nnwm_toplevel *
ws_next(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.next; it != &server->toplevels; it = it->next) {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (t->workspace == out->active_workspace && !t->floating && !t->fullscreen)
            return t;
    }
    return nullptr;
}

/* Previous tiled toplevel on out's workspace before cur, without wrapping. */
static nnwm_toplevel *
ws_prev(nnwm_server *server, nnwm_output *out, nnwm_toplevel *cur)
{
    for (wl_list *it = cur->link.prev; it != &server->toplevels; it = it->prev) {
        nnwm_toplevel *t = wl_container_of(it, t, link);
        if (t->workspace == out->active_workspace && !t->floating && !t->fullscreen)
            return t;
    }
    return nullptr;
}

/* Last tiled toplevel on out's active workspace, or nullptr. */
static nnwm_toplevel *
ws_last(nnwm_server *server, nnwm_output *out)
{
    nnwm_toplevel *t, *last = nullptr;
    wl_list_for_each(t, &server->toplevels, link)
        if (t->workspace == out->active_workspace && !t->floating && !t->fullscreen)
            last = t;
    return last;
}

/* Count tiled toplevels on out's active workspace. */
static int
ws_count(nnwm_server *server, nnwm_output *out)
{
    int n = 0;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link)
        if (t->workspace == out->active_workspace && !t->floating && !t->fullscreen)
            n++;
    return n;
}

void
arrange_windows(nnwm_server *server, nnwm_output *out)
{
    if (!out)
        return;

    int n = ws_count(server, out);

    const wlr_box &area = out->usable_area;

    bool solo = (n == 1);
    int bw = (solo && server->config->smart_borders) ? 0 : server->config->border_width;
    int ig = (solo && server->config->smart_gaps)    ? 0 : server->config->inner_gap;
    int og = (solo && server->config->smart_gaps)    ? 0 : server->config->outer_gap;
    int th = server->config->titlebar_height;

    int x0 = area.x + og;
    int y0 = area.y + og;
    int W  = area.width  - 2 * og;
    int H  = area.height - 2 * og;

    wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;

    nnwm_toplevel *tl;
    if (n == 1) {
        wl_list_for_each(tl, &server->toplevels, link) {
            if (tl->workspace != out->active_workspace || tl->floating || tl->fullscreen)
                continue;
            wlr_scene_node_set_position(&tl->scene_tree->node, x0, y0);
            wlr_xdg_toplevel_set_size(tl->xdg_toplevel, W - 2 * bw, H - 2 * bw - th);
            update_borders(tl, W, H, bw);
            render_titlebar(tl, W - 2 * bw,
                tl->xdg_toplevel->base->surface == focused_surface);
            break;
        }
    } else if (n > 1) {
        int mw = (int)(W * server->config->master_ratio);
        int sw = W - mw - ig;
        int ns = n - 1;
        int sh = (H - (ns - 1) * ig) / ns;

        int i = 0;
        wl_list_for_each(tl, &server->toplevels, link) {
            if (tl->workspace != out->active_workspace || tl->floating || tl->fullscreen)
                continue;
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
        if (tl->workspace == out->active_workspace && (tl->floating || tl->fullscreen))
            wlr_scene_node_raise_to_top(&tl->scene_tree->node);
    }
}

static void
arrange_all_outputs(nnwm_server *server)
{
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link)
        arrange_windows(server, out);
}

void
focus_toplevel(nnwm_toplevel *toplevel)
{
    /* Note: this function only deals with keyboard focus. */
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
        /* Don't re-focus an already focused surface. */
        return;
    }
    if (prev_surface)
    {
        /*
         * Deactivate the previously focused surface. This lets the client know
         * it no longer has focus and the client will repaint accordingly, e.g.
         * stop displaying a caret.
         */
        wlr_xdg_toplevel *prev_toplevel
            = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != nullptr)
        {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }
    wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    /* Activate the new surface */
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

    nnwm_output *out = output_for_workspace(server, toplevel->workspace);
    if (out) {
        server->focused_output = out;
        int ws = toplevel->workspace;
        if (out->last_focused[ws] != toplevel)
            out->prev_focused[ws] = out->last_focused[ws];
        out->last_focused[ws] = toplevel;
    }
}

} /* namespace */

void
keyboard_handle_modifiers(wl_listener *listener, void * /*data*/)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    nnwm_keyboard *keyboard
        = wl_container_of(listener, keyboard, modifiers);
    /*
     * A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same seat. You can swap out the underlying wlr_keyboard like this and
     * wlr_seat handles this transparently.
     */
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                       &keyboard->wlr_keyboard->modifiers);
}

/* ---- Compositor actions (called from Lua keybinding callbacks) ---- */

static void
do_toggle_fullscreen(nnwm_toplevel *tl)
{
    tl->fullscreen = !tl->fullscreen;
    wlr_xdg_toplevel_set_fullscreen(tl->xdg_toplevel, tl->fullscreen);

    nnwm_server *server = tl->server;
    nnwm_output *out    = output_for_workspace(server, tl->workspace);

    if (tl->fullscreen && out) {
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output, &area);
        wlr_scene_node_set_position(&tl->scene_tree->node, area.x, area.y);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, area.width, area.height);
        update_borders(tl, area.width, area.height, 0);
    }

    arrange_windows(server, out);
}

static nnwm_toplevel *
get_focused_toplevel(nnwm_server *server)
{
    wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    if (!focused)
        return nullptr;
    nnwm_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link) {
        if (t->xdg_toplevel->base->surface == focused)
            return t;
    }
    return nullptr;
}

void
nnwm::action_quit(nnwm_server *server)
{
    wl_display_terminate(server->wl_display);
}

void
nnwm::action_close(nnwm_server *server)
{
    nnwm_toplevel *focused = get_focused_toplevel(server);
    if (focused)
        wlr_xdg_toplevel_send_close(focused->xdg_toplevel);
}

void
nnwm::action_spawn(nnwm_server *server, const char *cmd)
{
    if (!server->wayland_started) {
        /* Queue until WAYLAND_DISPLAY is set and the backend is running */
        if (server->autostart_count >= server->autostart_cap) {
            server->autostart_cap = server->autostart_cap ? server->autostart_cap * 2 : 8;
            server->autostart_cmds = static_cast<char**>(
                std::realloc(server->autostart_cmds,
                             sizeof(char*) * server->autostart_cap));
        }
        server->autostart_cmds[server->autostart_count++] = strdup(cmd);
        return;
    }
    if (fork() == 0)
        execl("/bin/sh", "/bin/sh", "-c", cmd, static_cast<char*>(nullptr));
}

void
nnwm::flush_autostart(nnwm_server *server)
{
    for (int i = 0; i < server->autostart_count; i++) {
        if (fork() == 0)
            execl("/bin/sh", "/bin/sh", "-c", server->autostart_cmds[i],
                  static_cast<char*>(nullptr));
        free(server->autostart_cmds[i]);
    }
    free(server->autostart_cmds);
    server->autostart_cmds  = nullptr;
    server->autostart_count = 0;
    server->autostart_cap   = 0;
}

void
nnwm::action_spawn_once(nnwm_server *server, const char *cmd)
{
    for (int i = 0; i < server->spawn_once_count; i++)
        if (strcmp(server->spawn_once_cmds[i], cmd) == 0)
            return;

    if (server->spawn_once_count >= server->spawn_once_cap) {
        server->spawn_once_cap = server->spawn_once_cap ? server->spawn_once_cap * 2 : 8;
        server->spawn_once_cmds = static_cast<char**>(
            realloc(server->spawn_once_cmds,
                    server->spawn_once_cap * sizeof(char*)));
    }
    server->spawn_once_cmds[server->spawn_once_count++] = strdup(cmd);
    nnwm::action_spawn(server, cmd);
}

void
nnwm::action_focus_left(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *tl = ws_first(server, out);
    if (tl)
        focus_toplevel(tl);
}

void
nnwm::action_focus_right(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (next)
        focus_toplevel(next);
}

void
nnwm::action_focus_next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (!next)
        next = ws_first(server, out);
    if (next)
        focus_toplevel(next);
}

void
nnwm::action_focus_prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *prev = ws_prev(server, out, cur);
    if (!prev)
        prev = ws_last(server, out);
    if (prev)
        focus_toplevel(prev);
}

void
nnwm::action_swap_left(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur   = get_focused_toplevel(server);
    nnwm_toplevel *first = ws_first(server, out);
    if (!cur || cur == first)
        return;
    wl_list_remove(&cur->link);
    wl_list_insert(first->link.prev, &cur->link);
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_swap_right(nnwm_server *server)
{
    nnwm_output *out    = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur    = get_focused_toplevel(server);
    nnwm_toplevel *master = ws_first(server, out);
    if (!cur || !master)
        return;
    if (cur != master) {
        wl_list_remove(&cur->link);
        wl_list_insert(master->link.prev, &cur->link);
    } else {
        nnwm_toplevel *first_stack = ws_next(server, out, master);
        if (!first_stack)
            return;
        wl_list_remove(&master->link);
        wl_list_insert(&first_stack->link, &master->link);
    }
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_swap_next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *next = ws_next(server, out, cur);
    if (!next) return;
    wl_list_remove(&cur->link);
    wl_list_insert(&next->link, &cur->link);
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_swap_prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur = get_focused_toplevel(server);
    if (!cur) return;
    nnwm_toplevel *prev = ws_prev(server, out, cur);
    if (!prev) return;
    wl_list_remove(&cur->link);
    wl_list_insert(prev->link.prev, &cur->link);
    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_swap_master(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_toplevel *cur    = get_focused_toplevel(server);
    nnwm_toplevel *master = ws_first(server, out);
    if (!cur || !master || cur == master) return;

    /* Record the node before cur (master is guaranteed to precede cur). */
    struct wl_list *before_cur = cur->link.prev;
    bool adjacent = (before_cur == &master->link);

    /* Move cur to master's slot. */
    wl_list_remove(&cur->link);
    wl_list_insert(master->link.prev, &cur->link);

    /* Move master to cur's old slot. */
    wl_list_remove(&master->link);
    if (adjacent)
        wl_list_insert(&cur->link, &master->link);   /* directly after cur */
    else
        wl_list_insert(before_cur, &master->link);   /* where cur was */

    focus_toplevel(cur);
    arrange_windows(server, out);
}

void
nnwm::action_cycle(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out || ws_count(server, out) < 2) return;
    nnwm_toplevel *last  = ws_last(server, out);
    nnwm_toplevel *first = ws_first(server, out);
    if (!last || last == first)
        return;
    wl_list_remove(&last->link);
    wl_list_insert(first->link.prev, &last->link);
    focus_toplevel(last);
    arrange_windows(server, out);
}

void
nnwm::action_switch_workspace(nnwm_server *server, int ws)
{
    nnwm_output *out = server->focused_output;
    if (!out || ws < 0 || ws >= NNWM_NUM_WORKSPACES || ws == out->active_workspace)
        return;

    /* If another output already shows ws, swap workspaces between the two */
    nnwm_output *other = nullptr;
    {
        nnwm_output *o;
        wl_list_for_each(o, &server->outputs, link)
            if (o != out && o->active_workspace == ws) { other = o; break; }
    }
    if (other)
        other->active_workspace = out->active_workspace;
    out->active_workspace = ws;

    /* Sync scene visibility */
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link)
        wlr_scene_node_set_enabled(&tl->scene_tree->node,
                                   workspace_is_visible(server, tl->workspace));

    nnwm_toplevel *next = out->last_focused[ws];
    if (!next)
        next = ws_first(server, out);
    if (next)
        focus_toplevel(next);
    else
        wlr_seat_keyboard_clear_focus(server->seat);

    arrange_all_outputs(server);
}

void
nnwm::action_focus_monitor_next(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_output *next = output_cycle(server, out, +1);
    if (!next) return;
    server->focused_output = next;
    int ws = next->active_workspace;
    nnwm_toplevel *tl = next->last_focused[ws];
    if (!tl) tl = ws_first(server, next);
    if (tl) focus_toplevel(tl);
}

void
nnwm::action_focus_monitor_prev(nnwm_server *server)
{
    nnwm_output *out = server->focused_output;
    if (!out) return;
    nnwm_output *next = output_cycle(server, out, -1);
    if (!next) return;
    server->focused_output = next;
    int ws = next->active_workspace;
    nnwm_toplevel *tl = next->last_focused[ws];
    if (!tl) tl = ws_first(server, next);
    if (tl) focus_toplevel(tl);
}

static void
move_to_monitor(nnwm_server *server, int dir)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl) return;
    nnwm_output *src = output_for_workspace(server, tl->workspace);
    if (!src) return;
    nnwm_output *dst = output_cycle(server, src, dir);
    if (!dst) return;

    int old_ws = tl->workspace;
    int new_ws = dst->active_workspace;
    if (old_ws == new_ws) return;

    if (src->last_focused[old_ws] == tl) src->last_focused[old_ws] = nullptr;
    if (src->prev_focused[old_ws] == tl) src->prev_focused[old_ws] = nullptr;

    tl->workspace = new_ws;
    wlr_scene_node_set_enabled(&tl->scene_tree->node,
                               workspace_is_visible(server, new_ws));

    /* Refocus src with the next best window */
    nnwm_toplevel *next = src->prev_focused[old_ws];
    if (!next) next = src->last_focused[old_ws];
    if (!next) next = ws_first(server, src);
    if (next) focus_toplevel(next);
    else      wlr_seat_keyboard_clear_focus(server->seat);

    /* Switch focus to the moved window on dst */
    server->focused_output = dst;
    focus_toplevel(tl);

    arrange_windows(server, src);
    arrange_windows(server, dst);
}

void
nnwm::action_move_to_monitor_next(nnwm_server *server)
{
    move_to_monitor(server, +1);
}

void
nnwm::action_move_to_monitor_prev(nnwm_server *server)
{
    move_to_monitor(server, -1);
}

void
nnwm::action_master_ratio_grow(nnwm_server *server)
{
    nnwm_config *cfg = server->config;
    cfg->master_ratio += cfg->master_ratio_step;
    if (cfg->master_ratio > cfg->master_ratio_max)
        cfg->master_ratio = cfg->master_ratio_max;
    arrange_all_outputs(server);
}

void
nnwm::action_master_ratio_shrink(nnwm_server *server)
{
    nnwm_config *cfg = server->config;
    cfg->master_ratio -= cfg->master_ratio_step;
    if (cfg->master_ratio < cfg->master_ratio_min)
        cfg->master_ratio = cfg->master_ratio_min;
    arrange_all_outputs(server);
}

void
nnwm::action_toggle_float(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl)
        return;

    tl->floating = !tl->floating;
    nnwm_output *out = output_for_workspace(server, tl->workspace);

    if (tl->floating && out) {
        wlr_scene_node_raise_to_top(&tl->scene_tree->node);
        wlr_box area;
        wlr_output_layout_get_box(server->output_layout, out->wlr_output, &area);
        wlr_box *geo = &tl->xdg_toplevel->base->geometry;
        int x = area.x + (area.width  - geo->width)  / 2;
        int y = area.y + (area.height - geo->height) / 2;
        wlr_scene_node_set_position(&tl->scene_tree->node, x, y);
    }

    arrange_windows(server, out);
}

void
nnwm::action_toggle_fullscreen(nnwm_server *server)
{
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (tl)
        do_toggle_fullscreen(tl);
}

void
nnwm::action_move_to_workspace(nnwm_server *server, int ws)
{
    if (ws < 0 || ws >= NNWM_NUM_WORKSPACES)
        return;
    nnwm_toplevel *tl = get_focused_toplevel(server);
    if (!tl || tl->workspace == ws)
        return;

    nnwm_output *src = output_for_workspace(server, tl->workspace);

    if (src && src->last_focused[tl->workspace] == tl)
        src->last_focused[tl->workspace] = nullptr;

    tl->workspace = ws;
    wlr_scene_node_set_enabled(&tl->scene_tree->node,
                               workspace_is_visible(server, ws));

    if (src) {
        nnwm_toplevel *next = src->last_focused[src->active_workspace];
        if (!next) next = ws_first(server, src);
        if (next) focus_toplevel(next);
        else      wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, src);
    }

    nnwm_output *dst = output_for_workspace(server, ws);
    if (dst && dst != src)
        arrange_windows(server, dst);
}

/* ---- Keybinding dispatch (delegates to Lua) ---- */

bool
handle_keybinding(nnwm_server *server, uint32_t modifiers,
                  xkb_keysym_t sym)
{
#define MODS_MASK   (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT | \
                     WLR_MODIFIER_ALT  | WLR_MODIFIER_CTRL)

    uint32_t mods = modifiers & MODS_MASK;
    return nnwm::lua_handle_keybinding(server, mods, (unsigned int)sym);

#undef MODS_MASK
}

void
keyboard_handle_key(wl_listener *listener, void *data)
{
    /* This event is raised when a key is pressed or released. */
    nnwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    nnwm_server   *server   = keyboard->server;
    auto          *event    = static_cast<wlr_keyboard_key_event*>(data);
    wlr_seat      *seat     = server->seat;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
                                       keycode, &syms);

    bool     handled   = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        for (int i = 0; i < nsyms; i++)
        {
            if (syms[i] >= XKB_KEY_XF86Switch_VT_1 &&
                syms[i] <= XKB_KEY_XF86Switch_VT_12)
            {
                if (server->session)
                    wlr_session_change_vt(server->session,
                        syms[i] - XKB_KEY_XF86Switch_VT_1 + 1);
                handled = true;
            }
        }
    }

    if (!handled && (modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
        && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        /* Use the base (level-0) keysym so that e.g. Super+Shift+1 matches
         * a binding registered as {"Super","Shift","1"} rather than "exclam". */
        xkb_layout_index_t layout = xkb_state_key_get_layout(
            keyboard->wlr_keyboard->xkb_state, keycode);
        const xkb_keysym_t *base_syms;
        int n_base = xkb_keymap_key_get_syms_by_level(
            keyboard->wlr_keyboard->keymap, keycode, layout, 0, &base_syms);
        for (int i = 0; i < n_base; i++)
            handled = handle_keybinding(server, modifiers, base_syms[i]) || handled;
    }

    if (!handled)
    {
        /* Otherwise, we pass it along to the client. */
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                     event->state);
    }
}

void
keyboard_handle_destroy(wl_listener *listener, void * /*data*/)
{
    /* This event is raised by the keyboard base wlr_input_device to signal
     * the destruction of the wlr_keyboard. It will no longer receive events
     * and should be destroyed.
     */
    nnwm_keyboard *keyboard
        = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    delete keyboard;
}

static void
apply_keymap(wlr_keyboard *wlr_keyboard, nnwm_config *cfg)
{
    xkb_context    *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names  rules   = {};
    const char     *opts    = cfg->xkb_options;
    rules.options = (opts && opts[0]) ? opts : nullptr;
    xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rules,
                                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
}

void
server_new_keyboard(nnwm_server *server, wlr_input_device *device)
{
    wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    nnwm_keyboard *keyboard = new nnwm_keyboard{};
    keyboard->server          = server;
    keyboard->wlr_keyboard    = wlr_keyboard;

    apply_keymap(wlr_keyboard, server->config);
    wlr_keyboard_set_repeat_info(wlr_keyboard,
                                 server->config->keyboard_repeat_rate,
                                 server->config->keyboard_repeat_delay);

    /* Here we set up listeners for keyboard events. */
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    /* And add the keyboard to our list of keyboards */
    wl_list_insert(&server->keyboards, &keyboard->link);
}

void
server_new_pointer(nnwm_server *server, wlr_input_device *device)
{
    if (wlr_input_device_is_libinput(device)) {
        libinput_device *li = wlr_libinput_get_device_handle(device);

        /* Tap-to-click */
        if (server->config->touchpad_tap_to_click
            && libinput_device_config_tap_get_finger_count(li) > 0)
            libinput_device_config_tap_set_enabled(li, LIBINPUT_CONFIG_TAP_ENABLED);

        /* Natural scrolling */
        if (server->config->touchpad_natural_scroll
            && libinput_device_config_scroll_has_natural_scroll(li))
            libinput_device_config_scroll_set_natural_scroll_enabled(li, true);

        /* Disable touchpad while typing */
        if (server->config->touchpad_disable_while_typing
            && libinput_device_config_dwt_is_available(li))
            libinput_device_config_dwt_set_enabled(li, LIBINPUT_CONFIG_DWT_ENABLED);
    }

    wlr_cursor_attach_input_device(server->cursor, device);
}

nnwm_toplevel *
desktop_toplevel_at(nnwm_server *server, double lx, double ly,
                    wlr_surface **surface, double *sx, double *sy)
{
    /* This returns the topmost node in the scene at the given layout coords.
     * We only care about surface nodes as we are specifically looking for a
     * surface in the surface tree of a nnwm_toplevel. */
    wlr_scene_node *node
        = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER)
    {
        return nullptr;
    }
    wlr_scene_buffer  *scene_buffer  = wlr_scene_buffer_from_node(node);
    wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface)
    {
        return nullptr;
    }

    *surface = scene_surface->surface;
    /* Find the node corresponding to the nnwm_toplevel at the root of this
     * surface tree, it is the only one for which we set the data field. */
    wlr_scene_tree *tree = node->parent;
    while (tree != nullptr && tree->node.data == nullptr)
    {
        tree = tree->node.parent;
    }
    if (tree == nullptr)
        return nullptr;  /* surface is over a layer shell or other non-toplevel node */
    return static_cast<nnwm_toplevel*>(tree->node.data);
}

void
reset_cursor_mode(nnwm_server *server)
{
    /* Reset the cursor mode to passthrough. */
    server->cursor_mode      = NNWM_CURSOR_PASSTHROUGH;
    server->grabbed_toplevel = nullptr;
}

void
process_cursor_move(nnwm_server *server)
{
    /* Move the grabbed toplevel to the new position. */
    nnwm_toplevel *toplevel = server->grabbed_toplevel;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                server->cursor->x - server->grab_x,
                                server->cursor->y - server->grab_y);
}

void
process_cursor_resize(nnwm_server *server)
{
    /*
     * Resizing the grabbed toplevel can be a little bit complicated, because we
     * could be resizing from any corner or edge. This not only resizes the
     * toplevel on one or two axes, but can also move the toplevel if you resize
     * from the top or left edges (or top-left corner).
     *
     * Note that some shortcuts are taken here. In a more fleshed-out
     * compositor, you'd wait for the client to prepare a buffer at the new
     * size, then commit any movement that was prepared.
     */
    nnwm_toplevel *toplevel = server->grabbed_toplevel;
    double border_x           = server->cursor->x - server->grab_x;
    double border_y           = server->cursor->y - server->grab_y;
    int new_left              = server->grab_geobox.x;
    int new_right  = server->grab_geobox.x + server->grab_geobox.width;
    int new_top    = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP)
    {
        new_top = border_y;
        if (new_top >= new_bottom)
        {
            new_top = new_bottom - 1;
        }
    }
    else if (server->resize_edges & WLR_EDGE_BOTTOM)
    {
        new_bottom = border_y;
        if (new_bottom <= new_top)
        {
            new_bottom = new_top + 1;
        }
    }
    if (server->resize_edges & WLR_EDGE_LEFT)
    {
        new_left = border_x;
        if (new_left >= new_right)
        {
            new_left = new_right - 1;
        }
    }
    else if (server->resize_edges & WLR_EDGE_RIGHT)
    {
        new_right = border_x;
        if (new_right <= new_left)
        {
            new_right = new_left + 1;
        }
    }

    wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                new_left - geo_box->x, new_top - geo_box->y);

    int new_width  = new_right - new_left;
    int new_height = new_bottom - new_top;
    int bw = toplevel->server->config->border_width;
    int th = toplevel->server->config->titlebar_height;
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                               new_width - 2 * bw, new_height - 2 * bw - th);
    update_borders(toplevel, new_width, new_height, bw);

    /* Re-render titlebar at the new width */
    wlr_surface *fs = toplevel->server->seat->keyboard_state.focused_surface;
    render_titlebar(toplevel, new_width - 2 * bw,
                    toplevel->xdg_toplevel->base->surface == fs);
}

void
process_cursor_motion(nnwm_server *server, uint32_t time)
{
    /* If the mode is non-passthrough, delegate to those functions. */
    if (server->cursor_mode == NNWM_CURSOR_MOVE)
    {
        process_cursor_move(server);
        return;
    }
    else if (server->cursor_mode == NNWM_CURSOR_RESIZE)
    {
        process_cursor_resize(server);
        return;
    }

    /* Track which output the cursor is on */
    {
        nnwm_output *cur_out = output_at_cursor(server);
        if (cur_out)
            server->focused_output = cur_out;
    }

    /* Otherwise, find the toplevel under the pointer and send the event along. */
    double sx, sy;
    wlr_seat    *seat    = server->seat;
    wlr_surface *surface = nullptr;
    nnwm_toplevel *toplevel = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (!toplevel)
    {
        /* If there's no toplevel under the cursor, set the cursor image to a
         * default. This is what makes the cursor image appear when you move it
         * around the screen, not over any toplevels. */
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
    if (surface)
    {
        /*
         * Send pointer enter and motion events.
         *
         * The enter event gives the surface "pointer focus", which is distinct
         * from keyboard focus. You get pointer focus by moving the pointer over
         * a window.
         *
         * Note that wlroots will avoid sending duplicate enter/motion events if
         * the surface has already has pointer focus or if the client is already
         * aware of the coordinates passed.
         */
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
        if (toplevel && server->config->focus_follows_mouse)
            focus_toplevel(toplevel);
    }
    else
    {
        /* Clear pointer focus so future button events and such are not sent to
         * the last client to have the cursor over it. */
        wlr_seat_pointer_clear_focus(seat);
    }
}

void
output_frame(wl_listener *listener, void * /*data*/)
{
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    nnwm_output *output = wl_container_of(listener, output, frame);
    wlr_scene   *scene  = output->server->scene;

    wlr_scene_output *scene_output
        = wlr_scene_get_scene_output(scene, output->wlr_output);

    /* Render the scene if needed and commit the output */
    wlr_scene_output_commit(scene_output, nullptr);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void
output_request_state(wl_listener *listener, void *data)
{
    /* This function is called when the backend requests a new state for
     * the output. For example, Wayland and X11 backends request a new mode
     * when the output window is resized. */
    nnwm_output *output
        = wl_container_of(listener, output, request_state);
    const auto *event =
        static_cast<const wlr_output_event_request_state*>(data);
    wlr_output_commit_state(output->wlr_output, event->state);
}

void
output_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_output *output = wl_container_of(listener, output, destroy);
    nnwm_server *server = output->server;

    if (server->focused_output == output) {
        server->focused_output = nullptr;
        nnwm_output *o;
        wl_list_for_each(o, &server->outputs, link)
            if (o != output) { server->focused_output = o; break; }
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    delete output;
}

void
xdg_toplevel_map(wl_listener *listener, void * /*data*/)
{
    /* Called when the surface is mapped, or ready to display on-screen. */
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, map);

    nnwm_server *server = toplevel->server;
    nnwm_output *out    = server->focused_output;
    toplevel->workspace = out ? out->active_workspace : 0;
    if (server->config->new_window_master)
        wl_list_insert(&server->toplevels, &toplevel->link);
    else
        wl_list_insert(server->toplevels.prev, &toplevel->link);
    focus_toplevel(toplevel);
    arrange_windows(server, out);
}

void
xdg_toplevel_unmap(wl_listener *listener, void * /*data*/)
{
    /* Called when the surface is unmapped, and should no longer be shown. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, unmap);

    /* Reset the cursor mode if the grabbed toplevel was unmapped. */
    if (toplevel == toplevel->server->grabbed_toplevel)
    {
        reset_cursor_mode(toplevel->server);
    }

    nnwm_server *server = toplevel->server;
    int          ws     = toplevel->workspace;
    nnwm_output *out    = output_for_workspace(server, ws);

    bool was_focused = out && out->last_focused[ws] == toplevel;
    if (out && out->last_focused[ws] == toplevel)
        out->last_focused[ws] = nullptr;
    if (out && out->prev_focused[ws] == toplevel)
        out->prev_focused[ws] = nullptr;

    wl_list_remove(&toplevel->link);

    if (out) {
        nnwm_toplevel *next = nullptr;
        if (was_focused)
            next = out->prev_focused[ws];   /* jump to previously focused window */
        if (!next)
            next = out->last_focused[ws];   /* or the current focus on this ws */
        if (!next)
            next = ws_first(server, out);   /* fallback: new master */
        if (next) focus_toplevel(next);
        else      wlr_seat_keyboard_clear_focus(server->seat);
        arrange_windows(server, out);
    }

    /* Restore pointer focus to whatever is now under the cursor. Without this,
     * dismissing a popup (e.g. rofi) without moving the mouse leaves the seat
     * with no pointer focus and the window underneath never gets enter events. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(server,
        (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
}

static nnwm_toplevel *
toplevel_from_deco(nnwm_decoration *deco)
{
    auto *tree = static_cast<wlr_scene_tree*>(deco->wlr_deco->toplevel->base->data);
    if (tree && tree->node.data)
        return static_cast<nnwm_toplevel*>(tree->node.data);
    return nullptr;
}

static void
decoration_apply(nnwm_decoration *deco, bool client_side)
{
    auto mode = client_side
        ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
        : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_deco, mode);
}

void
decoration_handle_request_mode(wl_listener *listener, void * /*data*/)
{
    nnwm_decoration  *deco    = wl_container_of(listener, deco, request_mode);
    wlr_xdg_toplevel *xdg_tl = deco->wlr_deco->toplevel;
    nnwm_toplevel    *tl      = toplevel_from_deco(deco);

    /* Always keep a back-reference so server_apply_config can re-apply. */
    if (tl) tl->decoration = deco;

    if (xdg_tl->base->initialized) {
        bool csd = tl ? tl->server->config->client_decorations : false;
        decoration_apply(deco, csd);
        return;
    }
    /* Surface not yet initialized — defer to xdg_toplevel_commit's
     * initial_commit handling where schedule_configure is safe to call. */
}

void
decoration_handle_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_decoration *deco = wl_container_of(listener, deco, destroy);
    nnwm_toplevel   *tl   = toplevel_from_deco(deco);
    if (tl && tl->decoration == deco)
        tl->decoration = nullptr;
    wl_list_remove(&deco->request_mode.link);
    wl_list_remove(&deco->destroy.link);
    delete deco;
}

void
xdg_toplevel_commit(wl_listener *listener, void * /*data*/)
{
    /* Called when a new surface state is committed. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, commit);

    if (toplevel->xdg_toplevel->base->initial_commit)
    {
        /* When an xdg_surface performs an initial commit, the compositor must
         * reply with a configure so the client can map the surface. nnwm
         * configures the xdg_toplevel with 0,0 size to let the client pick the
         * dimensions itself. */
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
        /* Apply any decoration mode that was deferred because request_mode
         * arrived before the surface was initialized. */
        if (toplevel->decoration)
            decoration_apply(toplevel->decoration,
                             toplevel->server->config->client_decorations);
    }
}

void
handle_xdg_toplevel_destroy(wl_listener *listener, void * /*data*/)
{
    /* Called when the xdg_toplevel is destroyed. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, destroy);

    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->set_title.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);

    wlr_scene_node_destroy(&toplevel->scene_tree->node);
    delete toplevel;
}

void
begin_interactive(nnwm_toplevel *toplevel,
                  nnwm_cursor_mode mode, uint32_t edges)
{
    /* This function sets up an interactive move or resize operation, where the
     * compositor stops propagating pointer events to clients and instead
     * consumes them itself, to move or resize windows. */
    nnwm_server *server = toplevel->server;

    server->grabbed_toplevel = toplevel;
    server->cursor_mode      = mode;

    if (mode == NNWM_CURSOR_MOVE)
    {
        server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
    }
    else
    {
        wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

        double border_x = (toplevel->scene_tree->node.x + geo_box->x)
                          + ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
        double border_y = (toplevel->scene_tree->node.y + geo_box->y)
                          + ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
        server->grab_x  = server->cursor->x - border_x;
        server->grab_y  = server->cursor->y - border_y;

        server->grab_geobox = *geo_box;
        server->grab_geobox.x += toplevel->scene_tree->node.x;
        server->grab_geobox.y += toplevel->scene_tree->node.y;

        server->resize_edges = edges;
    }
}

void
xdg_toplevel_request_move(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    if (toplevel->floating)
        begin_interactive(toplevel, NNWM_CURSOR_MOVE, 0);
}

void
xdg_toplevel_request_resize(wl_listener *listener, void *data)
{
    nnwm_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    if (toplevel->floating) {
        auto *event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
        begin_interactive(toplevel, NNWM_CURSOR_RESIZE, event->edges);
    }
}

void
xdg_toplevel_request_maximize(wl_listener *listener, void * /*data*/)
{
    /* This event is raised when a client would like to maximize itself,
     * typically because the user clicked on the maximize button on client-side
     * decorations. nnwm doesn't support maximization, but to conform to
     * xdg-shell protocol we still must send a configure.
     * wlr_xdg_surface_schedule_configure() is used to send an empty reply.
     * However, if the request was sent before an initial commit, we don't do
     * anything and let the client finish the initial surface setup. */
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, request_maximize);
    if (toplevel->xdg_toplevel->base->initialized)
    {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

void
xdg_toplevel_request_fullscreen(wl_listener *listener, void * /*data*/)
{
    nnwm_toplevel *toplevel
        = wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_toplevel->base->initialized)
        do_toggle_fullscreen(toplevel);
}

void
xdg_popup_commit(wl_listener *listener, void * /*data*/)
{
    /* Called when a new surface state is committed. */
    nnwm_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit)
    {
        /* When an xdg_surface performs an initial commit, the compositor must
         * reply with a configure so the client can map the surface.
         * nnwm sends an empty configure. A more sophisticated compositor
         * might change an xdg_popup's geometry to ensure it's not positioned
         * off-screen, for example. */
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

void
handle_xdg_popup_destroy(wl_listener *listener, void * /*data*/)
{
    /* Called when the xdg_popup is destroyed. */
    nnwm_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    delete popup;
}

/* ---- layer shell ---- */

/* Recompute usable area for an output by processing all layer surfaces in
 * layer order.  Each surface's exclusive zone shrinks the usable area from
 * the anchored edge.  The result is stored on the output so arrange_windows
 * can pick it up, and arrange_windows is called to re-tile. */
static void
arrange_layers(nnwm_server *server, wlr_output *output)
{
    nnwm_output *out = nullptr;
    {
        nnwm_output *o;
        wl_list_for_each(o, &server->outputs, link) {
            if (o->wlr_output == output) { out = o; break; }
        }
    }
    if (!out)
        return;

    wlr_box full_area;
    wlr_output_layout_get_box(server->output_layout, output, &full_area);
    wlr_box usable = full_area;

    for (int layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
         layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer++)
    {
        nnwm_layer_surface *ls;
        wl_list_for_each(ls, &server->layer_surfaces, link) {
            if (ls->wlr_layer_surface->output != output)
                continue;
            if ((int)ls->wlr_layer_surface->current.layer != layer)
                continue;
            wlr_scene_layer_surface_v1_configure(ls->scene, &full_area, &usable);
        }
    }

    out->usable_area = usable;
    arrange_windows(server, out);
}

void
layer_surface_map(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, map);
    arrange_layers(ls->server, ls->wlr_layer_surface->output);

    if (ls->wlr_layer_surface->current.keyboard_interactive !=
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
    {
        wlr_keyboard *kb = wlr_seat_get_keyboard(ls->server->seat);
        if (kb)
            wlr_seat_keyboard_notify_enter(ls->server->seat,
                ls->wlr_layer_surface->surface,
                kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
}

void
layer_surface_unmap(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, unmap);
    arrange_layers(ls->server, ls->wlr_layer_surface->output);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(ls->server,
        (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
}

void
layer_surface_commit(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, commit);

    if (ls->wlr_layer_surface->current.committed)
        arrange_layers(ls->server, ls->wlr_layer_surface->output);

    if (ls->wlr_layer_surface->surface->mapped &&
        ls->wlr_layer_surface->current.keyboard_interactive !=
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
    {
        wlr_keyboard *kb = wlr_seat_get_keyboard(ls->server->seat);
        if (kb)
            wlr_seat_keyboard_notify_enter(ls->server->seat,
                ls->wlr_layer_surface->surface,
                kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
}

void
layer_surface_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_layer_surface *ls = wl_container_of(listener, ls, destroy);
    nnwm_server *server = ls->server;
    wlr_output  *output = ls->wlr_layer_surface->output;
    wl_list_remove(&ls->link);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    delete ls;
    if (output)
        arrange_layers(server, output);
}

void
server_apply_config(nnwm_server *server)
{
    /* Update border colors using actual keyboard focus */
    wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;
    nnwm_toplevel *tl;
    wl_list_for_each(tl, &server->toplevels, link) {
        float *color = (tl->xdg_toplevel->base->surface == focused_surface)
            ? server->config->focused_color
            : server->config->unfocused_color;
        for (int i = 0; i < 4; i++)
            wlr_scene_rect_set_color(tl->border[i], color);
    }

    /* Re-apply decoration mode (CSD vs SSD) to all live toplevels */
    wl_list_for_each(tl, &server->toplevels, link) {
        if (tl->decoration)
            decoration_apply(tl->decoration, server->config->client_decorations);
    }

    /* Re-arrange to apply border_width / master_ratio changes */
    arrange_all_outputs(server);

    /* Update keymap and repeat info for all connected keyboards */
    nnwm_keyboard *kb;
    wl_list_for_each(kb, &server->keyboards, link) {
        apply_keymap(kb->wlr_keyboard, server->config);
        wlr_keyboard_set_repeat_info(kb->wlr_keyboard,
                                     server->config->keyboard_repeat_rate,
                                     server->config->keyboard_repeat_delay);
    }
}

void
server_new_input(wl_listener *listener, void *data)
{
    /* This event is raised by the backend when a new input device becomes
     * available. */
    nnwm_server      *server = wl_container_of(listener, server, new_input);
    wlr_input_device *device = static_cast<wlr_input_device*>(data);
    switch (device->type)
    {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
            break;
        default:
            break;
    }
    /* We need to let the wlr_seat know what our capabilities are, which is
     * communiciated to the client. In TinyWL we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards))
    {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

void
seat_request_cursor(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, request_cursor);
    /* This event is raised by the seat when a client provides a cursor image */
    auto *event =
        static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client *focused_client
        = server->seat->pointer_state.focused_client;
    /* This can be sent by any client, so we check to make sure this one is
     * actually has pointer focus first. */
    if (focused_client == event->seat_client)
    {
        /* Once we've vetted the client, we can tell the cursor to use the
         * provided surface as the cursor image. It will set the hardware cursor
         * on the output that it's currently on and continue to do so as the
         * cursor moves between outputs. */
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
                               event->hotspot_y);
    }
}

void
seat_pointer_focus_change(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, pointer_focus_change);
    /* This event is raised when the pointer focus is changed, including when
     * the client is closed. We set the cursor image to its default if target
     * surface is NULL */
    auto *event =
        static_cast<wlr_seat_pointer_focus_change_event*>(data);
    if (event->new_surface == nullptr)
    {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
}

void
seat_request_set_selection(wl_listener *listener, void *data)
{
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in nnwm we
     * always honor
     */
    nnwm_server *server
        = wl_container_of(listener, server, request_set_selection);
    auto *event =
        static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void
server_cursor_motion(wl_listener *listener, void *data)
{
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_motion);
    auto *event = static_cast<wlr_pointer_motion_event*>(data);
    /* The cursor doesn't move unless we tell it to. The cursor automatically
     * handles constraining the motion to the output layout, as well as any
     * special configuration applied for the specific input device which
     * generated the event. You can pass NULL for the device if you want to move
     * the cursor around without any input. */
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
                    event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

void
server_cursor_motion_absolute(wl_listener *listener, void *data)
{
    /* This event is forwarded by the cursor when a pointer emits an _absolute_
     * motion event, from 0..1 on each axis. This happens, for example, when
     * wlroots is running under a Wayland window rather than KMS+DRM, and you
     * move the mouse over the window. You could enter the window from any edge,
     * so we have to warp the mouse there. There is also some hardware which
     * emits these events. */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_motion_absolute);
    auto *event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
                             event->y);
    process_cursor_motion(server, event->time_msec);
}

void
server_cursor_button(wl_listener *listener, void *data)
{
    /* This event is forwarded by the cursor when a pointer emits a button
     * event. */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_button);
    auto *event = static_cast<wlr_pointer_button_event*>(data);
    /* Notify the client with pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                   event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
    {
        /* If you released any buttons, we exit interactive move/resize mode. */
        reset_cursor_mode(server);
        return;
    }

    double sx, sy;
    wlr_surface   *surface  = nullptr;
    nnwm_toplevel *toplevel = desktop_toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    /* Super + left click → move, Super + right click → resize */
    wlr_keyboard *kb   = wlr_seat_get_keyboard(server->seat);
    uint32_t      mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;
    if ((mods & WLR_MODIFIER_LOGO) && toplevel)
    {
        if (!toplevel->floating) {
            toplevel->floating = true;
            arrange_windows(server, output_for_workspace(server, toplevel->workspace));
        }
        focus_toplevel(toplevel);
        if (event->button == BTN_LEFT)
            begin_interactive(toplevel, NNWM_CURSOR_MOVE, 0);
        else if (event->button == BTN_RIGHT)
            begin_interactive(toplevel, NNWM_CURSOR_RESIZE,
                              WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        return;
    }

    /* Normal click: focus the window under the cursor */
    focus_toplevel(toplevel);
}

void
server_cursor_axis(wl_listener *listener, void *data)
{
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_axis);
    auto *event = static_cast<wlr_pointer_axis_event*>(data);
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(
        server->seat, event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source, event->relative_direction);
}

void
server_cursor_frame(wl_listener *listener, void * /*data*/)
{
    /* This event is forwarded by the cursor when a pointer emits an frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    nnwm_server *server
        = wl_container_of(listener, server, cursor_frame);
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(server->seat);
}

void
server_new_output(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, new_output);
    wlr_output *wlr_output = static_cast<struct wlr_output*>(data);

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* Find matching monitor config (first match wins) */
    nnwm_monitor_config *mc = nullptr;
    {
        auto *cfg = server->config;
        for (int i = 0; i < cfg->monitor_config_count; i++)
        {
            auto &c = cfg->monitor_configs[i];
            bool match = true;
            if (c.name   && (!wlr_output->name   || strcmp(c.name,   wlr_output->name)   != 0)) match = false;
            if (c.make   && (!wlr_output->make   || strcmp(c.make,   wlr_output->make)   != 0)) match = false;
            if (c.model  && (!wlr_output->model  || strcmp(c.model,  wlr_output->model)  != 0)) match = false;
            if (c.serial && (!wlr_output->serial || strcmp(c.serial, wlr_output->serial) != 0)) match = false;
            if (match) { mc = &c; break; }
        }
    }

    wlr_output_state state;
    wlr_output_state_init(&state);

    if (mc && mc->disabled)
    {
        wlr_output_state_set_enabled(&state, false);
        wlr_output_commit_state(wlr_output, &state);
        wlr_output_state_finish(&state);
        return;
    }

    wlr_output_state_set_enabled(&state, true);

    /* Apply scale if configured */
    if (mc && mc->scale > 0.0f)
        wlr_output_state_set_scale(&state, mc->scale);

    /* Apply transform if configured */
    if (mc && mc->transform >= 0)
        wlr_output_state_set_transform(&state,
            static_cast<wl_output_transform>(mc->transform));

    /* Pick a mode: match by size/refresh if configured, else preferred */
    wlr_output_mode *mode = nullptr;
    if (mc && mc->width > 0 && mc->height > 0)
    {
        int target_w   = mc->width;
        int target_h   = mc->height;
        int target_mhz = mc->refresh * 1000; /* user passes Hz; wlroots uses mHz */

        wlr_output_mode *m;
        wl_list_for_each(m, &wlr_output->modes, link)
        {
            if (m->width == target_w && m->height == target_h)
            {
                if (target_mhz == 0 || m->refresh == target_mhz)
                { mode = m; break; }
                if (!mode) mode = m; /* fallback: closest match on size */
            }
        }
        if (!mode)
            std::fprintf(stderr, "nnwm: monitor '%s' has no mode %dx%d, "
                         "falling back to preferred\n",
                         wlr_output->name ? wlr_output->name : "?",
                         target_w, target_h);
    }
    if (!mode)
        mode = wlr_output_preferred_mode(wlr_output);
    if (mode)
        wlr_output_state_set_mode(&state, mode);

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    nnwm_output *output = new nnwm_output{};
    output->wlr_output    = wlr_output;
    output->server        = server;
    wlr_output_layout_get_box(server->output_layout, wlr_output,
                              &output->usable_area);

    {
        int ws = 0;
        while (ws < NNWM_NUM_WORKSPACES && workspace_is_visible(server, ws))
            ws++;
        output->active_workspace = ws < NNWM_NUM_WORKSPACES ? ws : 0;
    }
    memset(output->last_focused, 0, sizeof(output->last_focused));
    memset(output->prev_focused, 0, sizeof(output->prev_focused));
    if (!server->focused_output)
        server->focused_output = output;

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    /* Place at configured position, or auto-arrange */
    wlr_output_layout_output *l_output;
    if (mc && mc->x != INT_MAX && mc->y != INT_MAX)
        l_output = wlr_output_layout_add(server->output_layout, wlr_output,
                                         mc->x, mc->y);
    else
        l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);

    wlr_scene_output *scene_output
        = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                       scene_output);
}

void
server_new_xdg_toplevel(wl_listener *listener, void *data)
{
    /* This event is raised when a client creates a new toplevel (application
     * window). */
    nnwm_server   *server      = wl_container_of(listener, server, new_xdg_toplevel);
    wlr_xdg_toplevel *xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    /* Allocate a nnwm_toplevel for this surface */
    nnwm_toplevel *toplevel = new nnwm_toplevel{};
    toplevel->server          = server;
    toplevel->xdg_toplevel    = xdg_toplevel;

    /* Create wrapper tree in scene_windows */
    toplevel->scene_tree = wlr_scene_tree_create(server->scene_windows);
    toplevel->scene_tree->node.data = toplevel;

    /* Create four border rects as children of the wrapper */
    for (int i = 0; i < 4; i++)
        toplevel->border[i] = wlr_scene_rect_create(
            toplevel->scene_tree, 0, 0, server->config->unfocused_color);

    /* Titlebar scene buffer (null buffer initially; rendered by arrange_windows) */
    toplevel->titlebar = wlr_scene_buffer_create(toplevel->scene_tree, nullptr);
    wlr_scene_node_set_enabled(&toplevel->titlebar->node, false);

    /* Create xdg surface as child of the wrapper, offset by border width */
    toplevel->scene_surface = wlr_scene_xdg_surface_create(
        toplevel->scene_tree, xdg_toplevel->base);
    toplevel->scene_surface->node.data = toplevel;
    xdg_toplevel->base->data           = toplevel->scene_surface;

    /* Listen to the various events it can emit */
    toplevel->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
    toplevel->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
    toplevel->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit,
                  &toplevel->commit);

    toplevel->destroy.notify = handle_xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    toplevel->set_title.notify = [](wl_listener *listener, void *) {
        nnwm_toplevel *tl = wl_container_of(listener, tl, set_title);
        if (tl->server->config->titlebar_height <= 0 || tl->titlebar_width <= 0)
            return;
        wlr_surface *fs = tl->server->seat->keyboard_state.focused_surface;
        render_titlebar(tl, tl->titlebar_width,
                        tl->xdg_toplevel->base->surface == fs);
    };
    wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);

    /* cotd */
    toplevel->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
    toplevel->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize,
                  &toplevel->request_resize);
    toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize,
                  &toplevel->request_maximize);
    toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen,
                  &toplevel->request_fullscreen);
}

void
server_new_xdg_popup(wl_listener *listener, void *data)
{
    /* This event is raised when a client creates a new popup. */
    wlr_xdg_popup *xdg_popup = static_cast<wlr_xdg_popup*>(data);

    nnwm_popup *popup = new nnwm_popup{};
    popup->xdg_popup    = xdg_popup;

    /* We must add xdg popups to the scene graph so they get rendered. The
     * wlroots scene graph provides a helper for this, but to use it we must
     * provide the proper parent scene node of the xdg popup. To enable this,
     * we always set the user data field of xdg_surfaces to the corresponding
     * scene node. */
    wlr_xdg_surface *parent
        = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != nullptr);
    wlr_scene_tree *parent_tree =
        static_cast<wlr_scene_tree*>(parent->data);
    xdg_popup->base->data
        = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = handle_xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void
server_new_layer_surface(wl_listener *listener, void *data)
{
    nnwm_server        *server = wl_container_of(listener, server, new_layer_surface);
    wlr_layer_surface_v1 *wlr_ls =
        static_cast<wlr_layer_surface_v1*>(data);

    if (!wlr_ls->output && !wl_list_empty(&server->outputs))
    {
        nnwm_output *o = wl_container_of(server->outputs.next, o, link);
        wlr_ls->output = o->wlr_output;
    }

    nnwm_layer_surface *ls = new nnwm_layer_surface{};
    ls->server               = server;
    ls->wlr_layer_surface    = wlr_ls;
    ls->scene                = wlr_scene_layer_surface_v1_create(
        server->scene_layers[wlr_ls->pending.layer], wlr_ls);

    wlr_ls->data = ls;
    wl_list_insert(&server->layer_surfaces, &ls->link);

    ls->map.notify     = layer_surface_map;
    ls->unmap.notify   = layer_surface_unmap;
    ls->commit.notify  = layer_surface_commit;
    ls->destroy.notify = layer_surface_destroy;

    wl_signal_add(&wlr_ls->surface->events.map,    &ls->map);
    wl_signal_add(&wlr_ls->surface->events.unmap,  &ls->unmap);
    wl_signal_add(&wlr_ls->surface->events.commit, &ls->commit);
    wl_signal_add(&wlr_ls->events.destroy,         &ls->destroy);
}

void
server_new_decoration(wl_listener * /*listener*/, void *data)
{
    /* A client requested decoration negotiation. We respond with CSD only
     * when the client later sends request_mode — at which point the surface
     * is initialized and wlr_xdg_surface_schedule_configure is safe to call.
     * Calling set_mode here (before the surface's first commit) would assert. */
    auto *wlr_deco = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);

    auto *deco          = new nnwm_decoration{};
    deco->wlr_deco      = wlr_deco;

    deco->request_mode.notify = decoration_handle_request_mode;
    wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);

    deco->destroy.notify = decoration_handle_destroy;
    wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
}
