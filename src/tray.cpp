/*
 * tray.cpp — StatusNotifierItem (SNI) D-Bus system tray for nnwm bar.
 *
 * Registers org.kde.StatusNotifierWatcher on the session bus, tracks SNI
 * items, renders their icons into the bar's Cairo surface, and routes click
 * events to the appropriate D-Bus method calls.
 *
 * D-Bus watch/timeout integration uses wl_event_loop fd callbacks — no
 * blocking poll, no separate thread.
 */

#include "tray.hpp"
#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <cairo/cairo.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <climits>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <dbus/dbus.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
}

/* ---- SNI constants ---- */
#define SNI_WATCHER_NAME  "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH  "/StatusNotifierWatcher"
#define SNI_WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define SNI_ITEM_IFACE    "org.kde.StatusNotifierItem"
#define SNI_ITEM_PATH     "/StatusNotifierItem"

/* ---- Data structures ---- */

struct nnwm_tray_item {
    char *service;       /* D-Bus well-known or unique name */
    char *object_path;   /* object path on that service */
    char *title;
    char *icon_name;
    char *status;        /* "Active", "Passive", "NeedsAttention" */

    uint8_t *pixmap_data; /* ARGB network-byte-order from D-Bus, owned */
    int      pixmap_w;
    int      pixmap_h;

    cairo_surface_t *surface;      /* scaled, ready-to-blit */
    int              surface_target_h; /* bar_h for which surface was built */

    int rendered_x; /* bar-local x set during draw pass */

    struct wl_list link;
};

struct tray_watch {
    struct wl_event_source *source;
    DBusWatch              *watch;
    DBusConnection         *conn;
    struct wl_list          link;
};

struct tray_timeout {
    struct wl_event_source *source;
    DBusTimeout            *timeout;
    struct wl_list          link;
};

struct nnwm_tray {
    nnwm_server    *server;
    DBusConnection *conn;

    struct wl_list items;    /* nnwm_tray_item::link */
    struct wl_list watches;  /* tray_watch::link */
    struct wl_list timeouts; /* tray_timeout::link */

    uint32_t generation;

    char host_name[64];
    bool host_registered;
};

/* ---- Forward declarations ---- */
static void item_rebuild_surface(nnwm_tray_item *item, int bar_h);
static void request_item_properties(nnwm_tray *tray, nnwm_tray_item *item);
static void parse_item_properties(nnwm_tray *tray, nnwm_tray_item *item,
                                  DBusMessage *reply);

/* ---- Icon file search ---- */

static char *find_icon_png(const char *name, int preferred_size) {
    if (!name || !name[0]) return nullptr;

    /* Paths to search */
    const char *home = getenv("HOME");
    const char *xdg_data = getenv("XDG_DATA_DIRS");

    /* Themes to try in order */
    static const char *themes[] = {
        "hicolor", "Adwaita", "breeze", "Papirus", "gnome", nullptr
    };
    /* Sizes to try */
    int sizes[8];
    int nsizes = 0;
    sizes[nsizes++] = preferred_size;
    if (preferred_size != 22) sizes[nsizes++] = 22;
    if (preferred_size != 24) sizes[nsizes++] = 24;
    if (preferred_size != 32) sizes[nsizes++] = 32;
    if (preferred_size != 16) sizes[nsizes++] = 16;
    if (preferred_size != 48) sizes[nsizes++] = 48;
    if (preferred_size != 64) sizes[nsizes++] = 64;
    if (preferred_size != 128) sizes[nsizes++] = 128;

    /* Categories */
    static const char *cats[] = {
        "apps", "status", "devices", "actions", "mimetypes", nullptr
    };

    /* Build list of base icon dirs */
    char basedirs[32][512];
    int nbasedirs = 0;

    auto add_dir = [&](const char *d) {
        if (!d || !d[0]) return;
        if (nbasedirs >= 32) return;
        snprintf(basedirs[nbasedirs++], 512, "%s", d);
    };

    if (home) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s/.local/share/icons", home);
        add_dir(tmp);
        snprintf(tmp, sizeof(tmp), "%s/.icons", home);
        add_dir(tmp);
    }

    /* XDG_DATA_DIRS */
    if (xdg_data) {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s", xdg_data);
        char *tok = strtok(buf, ":");
        while (tok) {
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s/icons", tok);
            add_dir(tmp);
            tok = strtok(nullptr, ":");
        }
    } else {
        add_dir("/usr/local/share/icons");
        add_dir("/usr/share/icons");
    }

    char path[1024];

    for (int b = 0; b < nbasedirs; b++) {
        for (int t = 0; themes[t]; t++) {
            for (int s = 0; s < nsizes; s++) {
                for (int c = 0; cats[c]; c++) {
                    snprintf(path, sizeof(path), "%s/%s/%dx%d/%s/%s.png",
                             basedirs[b], themes[t], sizes[s], sizes[s],
                             cats[c], name);
                    if (access(path, R_OK) == 0)
                        return strdup(path);
                    /* Also try scalable */
                    snprintf(path, sizeof(path), "%s/%s/scalable/%s/%s.svg",
                             basedirs[b], themes[t], cats[c], name);
                    /* SVG not supported by cairo directly, skip */
                    (void)path;
                }
            }
        }
    }

    /* /usr/share/pixmaps fallback */
    snprintf(path, sizeof(path), "/usr/share/pixmaps/%s.png", name);
    if (access(path, R_OK) == 0) return strdup(path);

    /* If name ends with "-symbolic", strip that suffix and retry */
    const char *sym = strstr(name, "-symbolic");
    if (sym && sym[9] == '\0' && sym != name) {
        char stripped[256];
        size_t len = (size_t)(sym - name);
        if (len < sizeof(stripped)) {
            memcpy(stripped, name, len);
            stripped[len] = '\0';
            char *result = find_icon_png(stripped, preferred_size);
            if (result) return result;
        }
    }

    return nullptr;
}

static cairo_surface_t *scale_surface(cairo_surface_t *src, int target_h) {
    if (!src) return nullptr;
    int sw = cairo_image_surface_get_width(src);
    int sh = cairo_image_surface_get_height(src);
    if (sw <= 0 || sh <= 0) return nullptr;
    if (sh == target_h) {
        cairo_surface_reference(src);
        return src;
    }
    double scale = (double)target_h / sh;
    int dw = (int)(sw * scale);
    int dh = target_h;
    if (dw <= 0 || dh <= 0) return nullptr;

    cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    cairo_t *cr = cairo_create(dst);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return dst;
}

static cairo_surface_t *load_icon_surface_from_file(const char *path, int target_h) {
    if (!path) return nullptr;
    cairo_surface_t *src = cairo_image_surface_create_from_png(path);
    if (!src) return nullptr;
    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        return nullptr;
    }
    cairo_surface_t *scaled = scale_surface(src, target_h);
    cairo_surface_destroy(src);
    return scaled;
}

static cairo_surface_t *load_icon_surface_from_pixmap(const uint8_t *data,
                                                       int w, int h,
                                                       int target_h) {
    if (!data || w <= 0 || h <= 0) return nullptr;

    cairo_surface_t *src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (!src || cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        if (src) cairo_surface_destroy(src);
        return nullptr;
    }

    cairo_surface_flush(src);
    uint8_t *dst = cairo_image_surface_get_data(src);
    int stride = cairo_image_surface_get_stride(src);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *px = data + (y * w + x) * 4;
            uint8_t A = px[0], R = px[1], G = px[2], B = px[3];
            uint32_t argb = ((uint32_t)A << 24) | ((uint32_t)R << 16)
                          | ((uint32_t)G << 8)  |  (uint32_t)B;
            uint32_t *ddst = (uint32_t *)(dst + y * stride + x * 4);
            *ddst = argb;
        }
    }
    cairo_surface_mark_dirty(src);

    cairo_surface_t *scaled = scale_surface(src, target_h);
    cairo_surface_destroy(src);
    return scaled;
}

/* ---- Item property parsing ---- */

/*
 * parse_pixmap_array — iterate a(iiay) to pick the pixmap closest in height
 * to preferred_size.  On success, *out_data is a malloc'd ARGB byte buffer.
 */
static bool parse_pixmap_array(DBusMessageIter *top_iter, int preferred_size,
                                uint8_t **out_data, int *out_w, int *out_h) {
    *out_data = nullptr;
    *out_w = *out_h = 0;

    if (dbus_message_iter_get_arg_type(top_iter) != DBUS_TYPE_ARRAY)
        return false;

    DBusMessageIter arr;
    dbus_message_iter_recurse(top_iter, &arr);

    int best_diff = INT_MAX;
    uint8_t *best_data = nullptr;
    int best_w = 0, best_h = 0;

    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
        DBusMessageIter st;
        dbus_message_iter_recurse(&arr, &st);

        int w = 0, h = 0;
        if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32) {
            dbus_message_iter_next(&arr); continue;
        }
        dbus_message_iter_get_basic(&st, &w);
        dbus_message_iter_next(&st);

        if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32) {
            dbus_message_iter_next(&arr); continue;
        }
        dbus_message_iter_get_basic(&st, &h);
        dbus_message_iter_next(&st);

        if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_ARRAY) {
            dbus_message_iter_next(&arr); continue;
        }

        DBusMessageIter bytes_iter;
        dbus_message_iter_recurse(&st, &bytes_iter);

        const uint8_t *raw = nullptr;
        int nelem = 0;
        dbus_message_iter_get_fixed_array(&bytes_iter,
                                          (const void **)&raw, &nelem);

        if (raw && w > 0 && h > 0 && nelem == w * h * 4) {
            int diff = abs(h - preferred_size);
            if (diff < best_diff) {
                free(best_data);
                best_data = (uint8_t *)malloc((size_t)(w * h * 4));
                if (best_data) {
                    memcpy(best_data, raw, (size_t)(w * h * 4));
                    best_w = w;
                    best_h = h;
                    best_diff = diff;
                }
            }
        }

        dbus_message_iter_next(&arr);
    }

    if (best_data) {
        *out_data = best_data;
        *out_w = best_w;
        *out_h = best_h;
        return true;
    }
    return false;
}

static void item_rebuild_surface(nnwm_tray_item *item, int bar_h) {
    if (item->surface) {
        cairo_surface_destroy(item->surface);
        item->surface = nullptr;
    }
    item->surface_target_h = bar_h;
    int icon_h = bar_h - 4;
    if (icon_h < 1) icon_h = 1;

    /* Prefer raw pixmap from D-Bus */
    if (item->pixmap_data && item->pixmap_w > 0 && item->pixmap_h > 0) {
        item->surface = load_icon_surface_from_pixmap(
            item->pixmap_data, item->pixmap_w, item->pixmap_h, icon_h);
        if (item->surface) return;
    }

    /* Fall back to icon file search */
    if (item->icon_name && item->icon_name[0]) {
        char *path = find_icon_png(item->icon_name, icon_h);
        if (path) {
            wlr_log(WLR_DEBUG, "tray: icon '%s' -> %s", item->icon_name, path);
            item->surface = load_icon_surface_from_file(path, icon_h);
            free(path);
        } else {
            wlr_log(WLR_INFO, "tray: icon '%s' not found (no PNG)", item->icon_name);
        }
    }
}

static void parse_item_properties(nnwm_tray *tray, nnwm_tray_item *item,
                                  DBusMessage *reply) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter)) return;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) return;

    DBusMessageIter arr;
    dbus_message_iter_recurse(&iter, &arr);

    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&arr, &entry);

        const char *key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);

        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
            dbus_message_iter_next(&arr);
            continue;
        }

        DBusMessageIter var;
        dbus_message_iter_recurse(&entry, &var);

        if (key && strcmp(key, "Title") == 0) {
            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                const char *v = nullptr;
                dbus_message_iter_get_basic(&var, &v);
                free(item->title);
                item->title = v ? strdup(v) : nullptr;
            }
        } else if (key && strcmp(key, "Status") == 0) {
            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                const char *v = nullptr;
                dbus_message_iter_get_basic(&var, &v);
                free(item->status);
                item->status = v ? strdup(v) : nullptr;
            }
        } else if (key && strcmp(key, "IconName") == 0) {
            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                const char *v = nullptr;
                dbus_message_iter_get_basic(&var, &v);
                free(item->icon_name);
                item->icon_name = v ? strdup(v) : nullptr;
                /* Invalidate surface */
                if (item->surface) {
                    cairo_surface_destroy(item->surface);
                    item->surface = nullptr;
                    item->surface_target_h = 0;
                }
            }
        } else if (key && strcmp(key, "IconPixmap") == 0) {
            /* a(iiay) */
            uint8_t *pdata = nullptr;
            int pw = 0, ph = 0;
            if (parse_pixmap_array(&var, 22, &pdata, &pw, &ph)) {
                free(item->pixmap_data);
                item->pixmap_data = pdata;
                item->pixmap_w = pw;
                item->pixmap_h = ph;
                /* Invalidate surface */
                if (item->surface) {
                    cairo_surface_destroy(item->surface);
                    item->surface = nullptr;
                    item->surface_target_h = 0;
                }
            }
        }

        dbus_message_iter_next(&arr);
    }

    wlr_log(WLR_INFO, "tray: item '%s' props: status='%s' icon_name='%s' pixmap=%s",
            item->service,
            item->status    ? item->status    : "(null)",
            item->icon_name ? item->icon_name : "(null)",
            (item->pixmap_data && item->pixmap_w > 0) ? "yes" : "no");

    tray->generation++;
    bar_notify_tray_changed(tray->server);
}

/* ---- Async property fetch ---- */

struct pending_ctx {
    nnwm_tray      *tray;
    nnwm_tray_item *item;
};

static void on_props_reply(DBusPendingCall *pending, void *user_data) {
    pending_ctx *ctx  = (pending_ctx *)user_data;
    nnwm_tray      *tray = ctx->tray;
    nnwm_tray_item *item = ctx->item;
    /* ctx is freed by the free_function (::free) when pending refcount hits 0 */

    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending); /* may call free(ctx) — locals are safe */
    if (!reply) return;

    /* Guard: item may have been freed by unregister_item while we were waiting.
     * Walk the list to confirm it still exists before touching it. */
    nnwm_tray_item *it;
    bool valid = false;
    wl_list_for_each(it, &tray->items, link) {
        if (it == item) { valid = true; break; }
    }

    if (valid && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
        parse_item_properties(tray, item, reply);

    dbus_message_unref(reply);
}

static void request_item_properties(nnwm_tray *tray, nnwm_tray_item *item) {
    DBusMessage *msg = dbus_message_new_method_call(
        item->service,
        item->object_path,
        "org.freedesktop.DBus.Properties",
        "GetAll");
    if (!msg) return;

    const char *iface = SNI_ITEM_IFACE;
    dbus_message_append_args(msg,
        DBUS_TYPE_STRING, &iface,
        DBUS_TYPE_INVALID);

    DBusPendingCall *pending = nullptr;
    if (!dbus_connection_send_with_reply(tray->conn, msg, &pending, 5000)) {
        dbus_message_unref(msg);
        return;
    }
    dbus_message_unref(msg);
    if (!pending) return;

    pending_ctx *ctx = (pending_ctx *)malloc(sizeof(pending_ctx));
    if (!ctx) { dbus_pending_call_unref(pending); return; }
    ctx->tray = tray;
    ctx->item = item;
    /* free_function = ::free — called when pending refcount drops to 0 */
    dbus_pending_call_set_notify(pending, on_props_reply, ctx, ::free);
}

/* ---- Item management ---- */

static nnwm_tray_item *find_item_by_service(nnwm_tray *tray,
                                             const char *service) {
    nnwm_tray_item *item;
    wl_list_for_each(item, &tray->items, link) {
        if (item->service && strcmp(item->service, service) == 0)
            return item;
    }
    return nullptr;
}

static void free_item(nnwm_tray_item *item) {
    free(item->service);
    free(item->object_path);
    free(item->title);
    free(item->icon_name);
    free(item->status);
    free(item->pixmap_data);
    if (item->surface) cairo_surface_destroy(item->surface);
    free(item);
}

static void register_item(nnwm_tray *tray, const char *service_arg,
                           const char *sender) {
    char *service = nullptr;
    char *object_path = nullptr;

    if (service_arg && service_arg[0] == '/') {
        /* Argument is a path: service name is the sender */
        service = strdup(sender ? sender : "");
        object_path = strdup(service_arg);
    } else if (service_arg && strchr(service_arg, '/')) {
        /* "bus.name/path" */
        const char *slash = strchr(service_arg, '/');
        size_t slen = (size_t)(slash - service_arg);
        service = (char *)malloc(slen + 1);
        if (service) { memcpy(service, service_arg, slen); service[slen] = 0; }
        object_path = strdup(slash);
    } else {
        service = strdup(service_arg ? service_arg : "");
        object_path = strdup(SNI_ITEM_PATH);
    }

    if (!service || !object_path) {
        free(service); free(object_path);
        return;
    }

    /* Don't add duplicates */
    if (find_item_by_service(tray, service)) {
        free(service); free(object_path);
        return;
    }

    nnwm_tray_item *item = (nnwm_tray_item *)calloc(1, sizeof(nnwm_tray_item));
    if (!item) { free(service); free(object_path); return; }

    item->service = service;
    item->object_path = object_path;

    wl_list_insert(tray->items.prev, &item->link);
    wlr_log(WLR_INFO, "tray: registered item service='%s' path='%s'",
            item->service, item->object_path);

    /* Subscribe to PropertiesChanged on this service */
    char match[512];
    snprintf(match, sizeof(match),
             "type='signal',"
             "sender='%s',"
             "interface='org.freedesktop.DBus.Properties',"
             "member='PropertiesChanged'",
             service);
    dbus_bus_add_match(tray->conn, match, nullptr);
    dbus_connection_flush(tray->conn);

    request_item_properties(tray, item);

    tray->generation++;
    bar_notify_tray_changed(tray->server);
}

static void unregister_item(nnwm_tray *tray, const char *service) {
    nnwm_tray_item *item, *tmp;
    wl_list_for_each_safe(item, tmp, &tray->items, link) {
        if (item->service && strcmp(item->service, service) == 0) {
            wl_list_remove(&item->link);
            free_item(item);
            tray->generation++;
            bar_notify_tray_changed(tray->server);
            return;
        }
    }
}

/* ---- D-Bus watch/timeout integration with wl_event_loop ---- */

static int watch_dispatch_cb(int /*fd*/, uint32_t mask, void *data) {
    tray_watch *tw = (tray_watch *)data;
    if (!dbus_watch_get_enabled(tw->watch)) return 0;

    unsigned int flags = 0;
    if (mask & WL_EVENT_READABLE) flags |= DBUS_WATCH_READABLE;
    if (mask & WL_EVENT_WRITABLE) flags |= DBUS_WATCH_WRITABLE;
    if (mask & WL_EVENT_ERROR)    flags |= DBUS_WATCH_ERROR;
    if (mask & WL_EVENT_HANGUP)   flags |= DBUS_WATCH_HANGUP;

    if (flags) dbus_watch_handle(tw->watch, flags);

    /* Drain dispatch queue */
    while (dbus_connection_get_dispatch_status(tw->conn)
           == DBUS_DISPATCH_DATA_REMAINS)
        dbus_connection_dispatch(tw->conn);

    return 0;
}

static dbus_bool_t add_watch(DBusWatch *watch, void *data) {
    nnwm_tray *tray = (nnwm_tray *)data;
    if (!dbus_watch_get_enabled(watch)) return TRUE;

    int fd = dbus_watch_get_unix_fd(watch);
    unsigned int dflags = dbus_watch_get_flags(watch);
    uint32_t mask = 0;
    if (dflags & DBUS_WATCH_READABLE) mask |= WL_EVENT_READABLE;
    if (dflags & DBUS_WATCH_WRITABLE) mask |= WL_EVENT_WRITABLE;

    tray_watch *tw = (tray_watch *)calloc(1, sizeof(tray_watch));
    if (!tw) return FALSE;
    tw->watch = watch;
    tw->conn = tray->conn;

    struct wl_event_loop *loop =
        wl_display_get_event_loop(tray->server->wl_display);
    tw->source = wl_event_loop_add_fd(loop, fd, mask, watch_dispatch_cb, tw);
    if (!tw->source) { free(tw); return FALSE; }

    dbus_watch_set_data(watch, tw, nullptr);
    wl_list_insert(tray->watches.prev, &tw->link);
    return TRUE;
}

static void remove_watch(DBusWatch *watch, void *data) {
    nnwm_tray *tray = (nnwm_tray *)data;
    tray_watch *tw = (tray_watch *)dbus_watch_get_data(watch);
    if (!tw) return;
    wl_event_source_remove(tw->source);
    wl_list_remove(&tw->link);
    free(tw);
    dbus_watch_set_data(watch, nullptr, nullptr);
    (void)tray;
}

static void toggle_watch(DBusWatch *watch, void *data) {
    if (dbus_watch_get_enabled(watch)) {
        /* Only add if not already tracked (avoid duplicate fd sources). */
        if (!dbus_watch_get_data(watch))
            add_watch(watch, data);
    } else {
        remove_watch(watch, data);
    }
}

static int timeout_dispatch_cb(void *data) {
    tray_timeout *tt = (tray_timeout *)data;
    if (dbus_timeout_get_enabled(tt->timeout))
        dbus_timeout_handle(tt->timeout);
    return 0;
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data) {
    nnwm_tray *tray = (nnwm_tray *)data;
    if (!dbus_timeout_get_enabled(timeout)) return TRUE;

    tray_timeout *tt = (tray_timeout *)calloc(1, sizeof(tray_timeout));
    if (!tt) return FALSE;
    tt->timeout = timeout;

    struct wl_event_loop *loop =
        wl_display_get_event_loop(tray->server->wl_display);
    tt->source = wl_event_loop_add_timer(loop, timeout_dispatch_cb, tt);
    if (!tt->source) { free(tt); return FALSE; }

    int ms = dbus_timeout_get_interval(timeout);
    wl_event_source_timer_update(tt->source, ms);

    dbus_timeout_set_data(timeout, tt, nullptr);
    wl_list_insert(tray->timeouts.prev, &tt->link);
    return TRUE;
}

static void remove_timeout(DBusTimeout *timeout, void *data) {
    nnwm_tray *tray = (nnwm_tray *)data;
    tray_timeout *tt = (tray_timeout *)dbus_timeout_get_data(timeout);
    if (!tt) return;
    wl_event_source_remove(tt->source);
    wl_list_remove(&tt->link);
    free(tt);
    dbus_timeout_set_data(timeout, nullptr, nullptr);
    (void)tray;
}

static void toggle_timeout(DBusTimeout *timeout, void *data) {
    if (dbus_timeout_get_enabled(timeout))
        add_timeout(timeout, data);
    else
        remove_timeout(timeout, data);
}

/* ---- D-Bus message filter ---- */

static DBusHandlerResult message_filter(DBusConnection *conn,
                                        DBusMessage *msg,
                                        void *data) {
    nnwm_tray *tray = (nnwm_tray *)data;
    (void)conn;

    const char *iface  = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path   = dbus_message_get_path(msg);
    const char *sender = dbus_message_get_sender(msg);

    /* ---- Handle RegisterStatusNotifierItem ---- */
    if (dbus_message_is_method_call(msg, SNI_WATCHER_IFACE,
                                    "RegisterStatusNotifierItem")) {
        const char *service_arg = nullptr;
        DBusError err = DBUS_ERROR_INIT;
        dbus_message_get_args(msg, &err,
                              DBUS_TYPE_STRING, &service_arg,
                              DBUS_TYPE_INVALID);
        if (!dbus_error_is_set(&err) && service_arg)
            register_item(tray, service_arg, sender);
        dbus_error_free(&err);

        /* Send empty reply */
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_connection_send(tray->conn, reply, nullptr);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* ---- Handle RegisterStatusNotifierHost ---- */
    if (dbus_message_is_method_call(msg, SNI_WATCHER_IFACE,
                                    "RegisterStatusNotifierHost")) {
        tray->host_registered = true;
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_connection_send(tray->conn, reply, nullptr);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* ---- Handle GetAll / Introspect on SNI watcher path ---- */
    if (path && strcmp(path, SNI_WATCHER_PATH) == 0) {
        if (iface && strcmp(iface, "org.freedesktop.DBus.Introspectable") == 0
            && member && strcmp(member, "Introspect") == 0) {
            const char *xml =
                "<!DOCTYPE node PUBLIC '-//freedesktop//DTD D-BUS Object Introspection 1.0//EN' "
                "'http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd'>"
                "<node name='/StatusNotifierWatcher'>"
                "<interface name='org.kde.StatusNotifierWatcher'>"
                "<method name='RegisterStatusNotifierItem'>"
                "<arg direction='in' name='service' type='s'/>"
                "</method>"
                "<method name='RegisterStatusNotifierHost'>"
                "<arg direction='in' name='service' type='s'/>"
                "</method>"
                "<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
                "<property name='ProtocolVersion' type='i' access='read'/>"
                "<property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
                "</interface>"
                "</node>";
            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (reply) {
                dbus_message_append_args(reply,
                    DBUS_TYPE_STRING, &xml,
                    DBUS_TYPE_INVALID);
                dbus_connection_send(tray->conn, reply, nullptr);
                dbus_message_unref(reply);
            }
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }

    /* ---- Handle PropertiesChanged signal — re-fetch item props ---- */
    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL
        && iface && strcmp(iface, "org.freedesktop.DBus.Properties") == 0
        && member && strcmp(member, "PropertiesChanged") == 0) {
        /* Find the item by sender */
        if (sender) {
            nnwm_tray_item *item = find_item_by_service(tray, sender);
            if (item) request_item_properties(tray, item);
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    /* ---- Handle NameOwnerChanged — remove vanished items ---- */
    if (dbus_message_is_signal(msg, "org.freedesktop.DBus",
                                "NameOwnerChanged")) {
        const char *name = nullptr, *old_owner = nullptr, *new_owner = nullptr;
        DBusError err = DBUS_ERROR_INIT;
        dbus_message_get_args(msg, &err,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_STRING, &old_owner,
                              DBUS_TYPE_STRING, &new_owner,
                              DBUS_TYPE_INVALID);
        if (!dbus_error_is_set(&err) && name) {
            bool vanished = new_owner && new_owner[0] == '\0';
            if (vanished) {
                unregister_item(tray, name);
                if (old_owner && old_owner[0])
                    unregister_item(tray, old_owner);
            }
        }
        dbus_error_free(&err);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---- Public API ---- */

void tray_init(nnwm_server *server) {
    server->tray = nullptr;

    DBusError err = DBUS_ERROR_INIT;
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err)) {
        wlr_log(WLR_INFO, "tray: cannot connect to session bus: %s",
                dbus_error_is_set(&err) ? err.message : "(null)");
        dbus_error_free(&err);
        return;
    }
    dbus_error_free(&err);

    /* Don't exit on disconnect */
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    nnwm_tray *tray = (nnwm_tray *)calloc(1, sizeof(nnwm_tray));
    if (!tray) { dbus_connection_unref(conn); return; }

    tray->server = server;
    tray->conn   = conn;
    wl_list_init(&tray->items);
    wl_list_init(&tray->watches);
    wl_list_init(&tray->timeouts);

    /* Register watch/timeout integration */
    dbus_connection_set_watch_functions(conn,
        add_watch, remove_watch, toggle_watch, tray, nullptr);
    dbus_connection_set_timeout_functions(conn,
        add_timeout, remove_timeout, toggle_timeout, tray, nullptr);

    /* Add message filter */
    dbus_connection_add_filter(conn, message_filter, tray, nullptr);

    /* Subscribe to NameOwnerChanged */
    dbus_bus_add_match(conn,
        "type='signal',"
        "sender='org.freedesktop.DBus',"
        "interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged'",
        nullptr);
    dbus_connection_flush(conn);

    /* Request org.kde.StatusNotifierWatcher name */
    dbus_error_init(&err);
    int ret = dbus_bus_request_name(conn, SNI_WATCHER_NAME,
                                    DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (dbus_error_is_set(&err)) {
        wlr_log(WLR_INFO, "tray: request_name error: %s", err.message);
        dbus_error_free(&err);
    } else if (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        wlr_log(WLR_INFO, "tray: acquired " SNI_WATCHER_NAME);
    } else {
        wlr_log(WLR_INFO, "tray: could not acquire " SNI_WATCHER_NAME
                " (ret=%d); tray still active as consumer", ret);
    }

    /* Register object path so method calls to /StatusNotifierWatcher reach us */
    static const DBusObjectPathVTable vtable = {
        nullptr, /* unregister */
        [](DBusConnection *, DBusMessage *msg, void *data) -> DBusHandlerResult {
            return message_filter(nullptr, msg, data);
        },
        nullptr, nullptr, nullptr, nullptr
    };
    dbus_connection_register_object_path(conn, SNI_WATCHER_PATH, &vtable, tray);

    /* Register ourselves as a host too */
    snprintf(tray->host_name, sizeof(tray->host_name),
             "org.kde.StatusNotifierHost-%d", getpid());
    dbus_error_init(&err);
    dbus_bus_request_name(conn, tray->host_name,
                          DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    dbus_error_free(&err);

    server->tray = tray;
    wlr_log(WLR_INFO, "tray: initialized");
}

void tray_destroy(nnwm_server *server) {
    if (!server->tray) return;
    nnwm_tray *tray = (nnwm_tray *)server->tray;
    server->tray = nullptr;

    /* Free items */
    nnwm_tray_item *item, *itmp;
    wl_list_for_each_safe(item, itmp, &tray->items, link) {
        wl_list_remove(&item->link);
        free_item(item);
    }

    /* Remove watches */
    tray_watch *tw, *wt;
    wl_list_for_each_safe(tw, wt, &tray->watches, link) {
        wl_event_source_remove(tw->source);
        wl_list_remove(&tw->link);
        free(tw);
    }

    /* Remove timeouts */
    tray_timeout *tt, *tt2;
    wl_list_for_each_safe(tt, tt2, &tray->timeouts, link) {
        wl_event_source_remove(tt->source);
        wl_list_remove(&tt->link);
        free(tt);
    }

    dbus_connection_remove_filter(tray->conn, message_filter, tray);
    dbus_connection_unregister_object_path(tray->conn, SNI_WATCHER_PATH);
    dbus_connection_unref(tray->conn);
    free(tray);
}

uint32_t tray_generation(nnwm_server *server) {
    if (!server->tray) return 0;
    return ((nnwm_tray *)server->tray)->generation;
}

int tray_measure_width(nnwm_server *server, int bar_h, int icon_spacing) {
    if (!server->tray) return 0;
    nnwm_tray *tray = (nnwm_tray *)server->tray;

    int icon_h = bar_h - 4;
    if (icon_h < 1) icon_h = 1;
    int icon_w = icon_h; /* square icons */

    int total = 0;
    int count = 0;
    nnwm_tray_item *item;
    wl_list_for_each(item, &tray->items, link) {
        if (item->status && strcmp(item->status, "Passive") == 0)
            continue;
        if (count > 0) total += icon_spacing;
        total += icon_w;
        count++;
    }
    return total;
}

int tray_draw(nnwm_server *server, void *cr_void, int bar_h, int x,
              int icon_spacing) {
    if (!server->tray) return 0;
    nnwm_tray *tray = (nnwm_tray *)server->tray;
    cairo_t *cr = (cairo_t *)cr_void;

    int icon_h = bar_h - 4;
    if (icon_h < 1) icon_h = 1;
    int icon_w = icon_h;

    int cx = x;
    bool first = true;
    nnwm_tray_item *item;
    wl_list_for_each(item, &tray->items, link) {
        if (item->status && strcmp(item->status, "Passive") == 0)
            continue;

        if (!first) cx += icon_spacing;
        first = false;

        item->rendered_x = cx;

        /* Rebuild surface if needed */
        if (!item->surface || item->surface_target_h != bar_h)
            item_rebuild_surface(item, bar_h);

        double ty = (bar_h - icon_h) / 2.0;

        if (item->surface
            && cairo_surface_status(item->surface) == CAIRO_STATUS_SUCCESS) {
            cairo_save(cr);
            cairo_rectangle(cr, cx, ty, icon_w, icon_h);
            cairo_clip(cr);
            cairo_set_source_surface(cr, item->surface, cx, ty);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            /* Gray placeholder square */
            cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 0.8);
            cairo_rectangle(cr, cx, ty, icon_w, icon_h);
            cairo_fill(cr);
        }

        cx += icon_w;
    }

    return cx - x;
}

bool tray_handle_click(nnwm_server *server, double bx, int bar_h, int x_start,
                       int bar_origin_x, int bar_origin_y, uint32_t button) {
    if (!server->tray) return false;
    nnwm_tray *tray = (nnwm_tray *)server->tray;
    (void)x_start;

    int icon_h = bar_h - 4;
    if (icon_h < 1) icon_h = 1;
    int icon_w = icon_h;

    nnwm_tray_item *item;
    wl_list_for_each(item, &tray->items, link) {
        if (item->status && strcmp(item->status, "Passive") == 0)
            continue;

        if (bx >= item->rendered_x && bx < item->rendered_x + icon_w) {
            /* Map button to SNI method */
            const char *method = nullptr;
            if (button == BTN_LEFT)   method = "Activate";
            else if (button == BTN_RIGHT)  method = "ContextMenu";
            else if (button == BTN_MIDDLE) method = "SecondaryActivate";
            if (!method) return true;

            /* Click coordinates: center of the icon in screen space */
            int icon_screen_x = bar_origin_x + item->rendered_x + icon_w / 2;
            int icon_screen_y = bar_origin_y + bar_h / 2;

            DBusMessage *msg = dbus_message_new_method_call(
                item->service, item->object_path,
                SNI_ITEM_IFACE, method);
            if (msg) {
                dbus_message_append_args(msg,
                    DBUS_TYPE_INT32, &icon_screen_x,
                    DBUS_TYPE_INT32, &icon_screen_y,
                    DBUS_TYPE_INVALID);
                dbus_connection_send(tray->conn, msg, nullptr);
                dbus_connection_flush(tray->conn);
                dbus_message_unref(msg);
            }
            return true;
        }
    }
    return false;
}
