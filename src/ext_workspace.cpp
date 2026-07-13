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
    if (!ws) return;
    nnwm_server *server = ws->group->manager->server;
    nnwm::action_switch_workspace(server, ws->index);
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

    /* Create the single workspace group (server-allocated ID = 0) */
    auto *grp = new nnwm_ext_workspace_group{};
    grp->manager = mgr;
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
    ext_workspace_group_handle_v1_send_capabilities(grp_res, 0 /* no create_workspace */);

    /* Create workspace handles for workspaces 0..8 */
    for (int i = 0; i < NNWM_NUM_WORKSPACES; i++) {
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
        wl_list_insert(grp->workspaces.prev, &ws->link); /* append */

        /* Announce workspace to the client */
        ext_workspace_manager_v1_send_workspace(mgr_res, ws_res);

        /* Name: "1".."9" */
        char name[4];
        snprintf(name, sizeof(name), "%d", i + 1);
        ext_workspace_handle_v1_send_name(ws_res, name);

        /* Capabilities */
        ext_workspace_handle_v1_send_capabilities(
            ws_res, EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);

        /* State: active if any output is on this workspace */
        uint32_t state = 0;
        {
            nnwm_output *out;
            wl_list_for_each(out, &server->outputs, link) {
                if (out->active_workspace == i) {
                    state = EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE;
                    break;
                }
            }
        }
        ext_workspace_handle_v1_send_state(ws_res, state);

        /* Tell group the workspace belongs here */
        ext_workspace_group_handle_v1_send_workspace_enter(grp_res, ws_res);
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
            nnwm_ext_workspace *ws;
            wl_list_for_each(ws, &grp->workspaces, link) {
                uint32_t state = 0;
                nnwm_output *out;
                wl_list_for_each(out, &server->outputs, link) {
                    if (out->active_workspace == ws->index) {
                        state = EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE;
                        break;
                    }
                }
                ext_workspace_handle_v1_send_state(ws->resource, state);
            }
        }

        ext_workspace_manager_v1_send_done(mgr->resource);
    }
}
