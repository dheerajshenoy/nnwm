#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <cstdio>

extern "C" {
#include "ext-workspace-v1-protocol.h"
#include "ext-workspace-v1-protocol.c"
}

/* ============================================================
 * ext-workspace-v1 protocol implementation
 * ============================================================ */

namespace {

/* ---- helper: find wl_output resource for a client matching a wlr_output ---- */

struct wl_output_find_data {
    struct wlr_output *target;
    struct wl_resource *result;
};

static enum wl_iterator_result
find_wl_output_iterator(struct wl_resource *resource, void *user_data)
{
    auto *data = static_cast<struct wl_output_find_data*>(user_data);
    if (wl_resource_get_interface(resource) == &wl_output_interface) {
        struct wlr_output *o = wlr_output_from_resource(resource);
        if (o == data->target) {
            data->result = resource;
            return WL_ITERATOR_STOP;
        }
    }
    return WL_ITERATOR_CONTINUE;
}

static struct wl_resource *
find_wl_output_for_client(struct wl_client *client, struct wlr_output *wlr_output)
{
    struct wl_output_find_data data = { wlr_output, nullptr };
    wl_client_for_each_resource(client, find_wl_output_iterator, &data);
    return data.result;
}

/* ---- workspace handle request handlers ---- */

static void
workspace_handle_destroy(struct wl_client * /*client*/,
                         struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void
workspace_handle_activate(struct wl_client * /*client*/,
                          struct wl_resource *resource)
{
    auto *ws = static_cast<nnwm_ext_workspace*>(wl_resource_get_user_data(resource));
    if (!ws || ws->removed) return;
    nnwm_server *server = ws->group->manager->server;
    /* Switch workspace on the output that owns this group */
    nnwm_output *out = ws->group->output;
    if (out)
        server->focused_output = out;
    nnwm::workspace::switch_to(server, ws->index);
}

static void
workspace_handle_deactivate(struct wl_client * /*client*/,
                             struct wl_resource * /*resource*/)
{
    /* no-op */
}

static void
workspace_handle_assign(struct wl_client * /*client*/,
                        struct wl_resource * /*resource*/,
                        struct wl_resource * /*group*/)
{
    /* no-op */
}

static void
workspace_handle_remove(struct wl_client * /*client*/,
                        struct wl_resource * /*resource*/)
{
    /* no-op */
}

static const struct ext_workspace_handle_v1_interface workspace_impl = {
    workspace_handle_destroy,
    workspace_handle_activate,
    workspace_handle_deactivate,
    workspace_handle_assign,
    workspace_handle_remove,
};

static void
workspace_resource_destroy(struct wl_resource *resource)
{
    auto *ws = static_cast<nnwm_ext_workspace*>(wl_resource_get_user_data(resource));
    if (!ws) return;
    wl_list_remove(&ws->link);
    delete ws;
}

/* ---- workspace group handle request handlers ---- */

static void
group_handle_create_workspace(struct wl_client * /*client*/,
                              struct wl_resource * /*resource*/,
                              const char * /*name*/)
{
    /* no-op — we don't support dynamic workspace creation */
}

static void
group_handle_destroy(struct wl_client * /*client*/,
                     struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct ext_workspace_group_handle_v1_interface group_impl = {
    group_handle_create_workspace,
    group_handle_destroy,
};

static void
group_resource_destroy(struct wl_resource *resource)
{
    auto *grp = static_cast<nnwm_ext_workspace_group*>(wl_resource_get_user_data(resource));
    if (!grp) return;

    /* Destroy all workspace resources in this group */
    nnwm_ext_workspace *ws, *ws_tmp;
    wl_list_for_each_safe(ws, ws_tmp, &grp->workspaces, link) {
        wl_resource_destroy(ws->resource);
    }

    wl_list_remove(&grp->link);
    delete grp;
}

/* ---- manager request handlers ---- */

static void
manager_handle_commit(struct wl_client * /*client*/,
                      struct wl_resource * /*resource*/)
{
    /* no-op — we apply state immediately */
}

static void
manager_handle_stop(struct wl_client * /*client*/,
                    struct wl_resource *resource)
{
    auto *mgr = static_cast<nnwm_ext_workspace_manager*>(wl_resource_get_user_data(resource));
    if (!mgr) return;
    mgr->stopped = true;
    ext_workspace_manager_v1_send_finished(resource);
    wl_resource_destroy(resource);
}

static const struct ext_workspace_manager_v1_interface manager_impl = {
    manager_handle_commit,
    manager_handle_stop,
};

static void
manager_resource_destroy(struct wl_resource *resource)
{
    auto *mgr = static_cast<nnwm_ext_workspace_manager*>(wl_resource_get_user_data(resource));
    if (!mgr) return;

    /* Destroy all group resources for this manager */
    nnwm_ext_workspace_group *grp, *grp_tmp;
    wl_list_for_each_safe(grp, grp_tmp, &mgr->groups, link) {
        wl_resource_destroy(grp->resource);
    }

    wl_list_remove(&mgr->link);
    delete mgr;
}

static void
ext_workspace_manager_bind(struct wl_client *client, void *data,
                           uint32_t version, uint32_t id)
{
    auto *server = static_cast<nnwm_server*>(data);

    /* Create manager resource */
    auto *mgr = new nnwm_ext_workspace_manager{};
    mgr->server  = server;
    mgr->stopped = false;
    wl_list_init(&mgr->groups);

    struct wl_resource *mgr_res = wl_resource_create(
        client, &ext_workspace_manager_v1_interface, (int)version, id);
    if (!mgr_res) {
        delete mgr;
        wl_client_post_no_memory(client);
        return;
    }
    mgr->resource = mgr_res;
    wl_resource_set_implementation(mgr_res, &manager_impl, mgr, manager_resource_destroy);
    wl_list_insert(&server->ext_workspace_managers, &mgr->link);

    /* Create one workspace group per output */
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        auto *grp = new nnwm_ext_workspace_group{};
        grp->manager = mgr;
        grp->output  = out;
        wl_list_init(&grp->workspaces);

        struct wl_resource *grp_res = wl_resource_create(
            client, &ext_workspace_group_handle_v1_interface, (int)version, 0);
        if (!grp_res) {
            delete grp;
            wl_client_post_no_memory(client);
            return;
        }
        grp->resource = grp_res;
        wl_resource_set_implementation(grp_res, &group_impl, grp, group_resource_destroy);
        wl_list_insert(&mgr->groups, &grp->link);

        /* Announce the group to the client */
        ext_workspace_manager_v1_send_workspace_group(mgr_res, grp_res);
        ext_workspace_group_handle_v1_send_capabilities(grp_res, 0);

        /* Tell the client which output this group belongs to */
        struct wl_resource *out_res = find_wl_output_for_client(client, out->wlr_output);
        if (out_res)
            ext_workspace_group_handle_v1_send_output_enter(grp_res, out_res);

        /* Create workspace handles for configured workspaces on this output */
        int num_ws = server->config->workspace_count;
        for (int i = 0; i < num_ws; i++) {
            auto *ws = new nnwm_ext_workspace{};
            ws->index = i;
            ws->group = grp;

            struct wl_resource *ws_res = wl_resource_create(
                client, &ext_workspace_handle_v1_interface, (int)version, 0);
            if (!ws_res) {
                delete ws;
                wl_client_post_no_memory(client);
                return;
            }
            ws->resource = ws_res;
            wl_resource_set_implementation(ws_res, &workspace_impl, ws, workspace_resource_destroy);
            wl_list_insert(grp->workspaces.prev, &ws->link);

            ext_workspace_manager_v1_send_workspace(mgr_res, ws_res);

            const char *ws_name = server->config->workspace_names[i];
            char name_buf[32];
            if (!ws_name || ws_name[0] == '\0')
            {
                snprintf(name_buf, sizeof(name_buf), "%d", i + 1);
                ws_name = name_buf;
            }
            ext_workspace_handle_v1_send_name(ws_res, ws_name);

            ext_workspace_handle_v1_send_capabilities(
                ws_res, EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);

            uint32_t state = (out->active_workspace == i)
                ? EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE : 0;
            ext_workspace_handle_v1_send_state(ws_res, state);

            ext_workspace_group_handle_v1_send_workspace_enter(grp_res, ws_res);
        }
    }

    ext_workspace_manager_v1_send_done(mgr_res);
}

} // anonymous namespace

/* ---- Public functions ---- */

void
nnwm::ext_workspace_init(struct nnwm_server *server)
{
    wl_list_init(&server->ext_workspace_managers);
    server->ext_workspace_global = wl_global_create(
        server->wl_display,
        &ext_workspace_manager_v1_interface,
        1,
        server,
        ext_workspace_manager_bind);
}

void
nnwm::ext_workspace_notify(struct nnwm_server *server)
{
    nnwm_ext_workspace_manager *mgr;
    wl_list_for_each(mgr, &server->ext_workspace_managers, link) {
        if (mgr->stopped) continue;

        nnwm_ext_workspace_group *grp;
        wl_list_for_each(grp, &mgr->groups, link) {
            nnwm_output *out = grp->output;
            nnwm_ext_workspace *ws;
            wl_list_for_each(ws, &grp->workspaces, link) {
                if (ws->removed) continue;
                uint32_t state = (out && out->active_workspace == ws->index)
                    ? EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE : 0;
                ext_workspace_handle_v1_send_state(ws->resource, state);
            }
        }

        ext_workspace_manager_v1_send_done(mgr->resource);
    }
}

void
nnwm::ext_workspace_rebuild(struct nnwm_server *server)
{
    int num_ws = server->config->workspace_count;

    nnwm_ext_workspace_manager *mgr;
    wl_list_for_each(mgr, &server->ext_workspace_managers, link) {
        if (mgr->stopped) continue;

        uint32_t version = (uint32_t)wl_resource_get_version(mgr->resource);
        struct wl_client *client = wl_resource_get_client(mgr->resource);

        nnwm_ext_workspace_group *grp;
        wl_list_for_each(grp, &mgr->groups, link) {
            nnwm_output *out = grp->output;

            int max_live_index = -1;
            nnwm_ext_workspace *ws;
            wl_list_for_each(ws, &grp->workspaces, link) {
                if (ws->removed) continue;
                if (ws->index >= num_ws) {
                    /* Signal the client to destroy this handle; we do NOT call
                     * wl_resource_destroy here — the client calls destroy after
                     * receiving removed, which triggers workspace_resource_destroy. */
                    ext_workspace_handle_v1_send_removed(ws->resource);
                    ws->removed = true;
                } else {
                    /* Update name in place */
                    const char *ws_name = server->config->workspace_names[ws->index];
                    char name_buf[32];
                    if (!ws_name || ws_name[0] == '\0') {
                        snprintf(name_buf, sizeof(name_buf), "%d", ws->index + 1);
                        ws_name = name_buf;
                    }
                    ext_workspace_handle_v1_send_name(ws->resource, ws_name);
                    if (ws->index > max_live_index)
                        max_live_index = ws->index;
                }
            }

            /* Create handles for any indices not yet announced */
            for (int i = max_live_index + 1; i < num_ws; i++) {
                auto *nws = new nnwm_ext_workspace{};
                nws->index   = i;
                nws->removed = false;
                nws->group   = grp;

                struct wl_resource *ws_res = wl_resource_create(
                    client, &ext_workspace_handle_v1_interface, (int)version, 0);
                if (!ws_res) { delete nws; continue; }
                nws->resource = ws_res;
                wl_resource_set_implementation(ws_res, &workspace_impl, nws,
                                               workspace_resource_destroy);
                wl_list_insert(grp->workspaces.prev, &nws->link);

                ext_workspace_manager_v1_send_workspace(mgr->resource, ws_res);

                const char *ws_name = server->config->workspace_names[i];
                char name_buf[32];
                if (!ws_name || ws_name[0] == '\0') {
                    snprintf(name_buf, sizeof(name_buf), "%d", i + 1);
                    ws_name = name_buf;
                }
                ext_workspace_handle_v1_send_name(ws_res, ws_name);
                ext_workspace_handle_v1_send_capabilities(
                    ws_res, EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
                uint32_t state = (out && out->active_workspace == i)
                    ? EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE : 0;
                ext_workspace_handle_v1_send_state(ws_res, state);
                ext_workspace_group_handle_v1_send_workspace_enter(grp->resource, ws_res);
            }
        }

        ext_workspace_manager_v1_send_done(mgr->resource);
    }
}
