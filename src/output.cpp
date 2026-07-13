#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>

/* ---- Output frame and state request ---- */

void
output_frame(wl_listener *listener, void * /*data*/)
{
    nnwm_output *output = wl_container_of(listener, output, frame);
    wlr_scene   *scene  = output->server->scene;

    wlr_scene_output *scene_output
        = wlr_scene_get_scene_output(scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, nullptr);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void
output_request_state(wl_listener *listener, void *data)
{
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

    output_manager_build_config(server);
}

/* ---- wlr-output-management ---- */

void
output_manager_build_config(nnwm_server *server)
{
    wlr_output_configuration_v1 *config =
        wlr_output_configuration_v1_create();

    nnwm_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        wlr_output_configuration_head_v1 *head =
            wlr_output_configuration_head_v1_create(config,
                output->wlr_output);

        head->state.enabled = output->wlr_output->enabled;
        head->state.mode    = output->wlr_output->current_mode;

        struct wlr_output_layout_output *l_output =
            wlr_output_layout_get(server->output_layout,
                output->wlr_output);
        if (l_output) {
            head->state.x = l_output->x;
            head->state.y = l_output->y;
        }

        head->state.transform = output->wlr_output->transform;
        head->state.scale     = output->wlr_output->scale;
    }

    wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

static void
output_manager_apply_or_test(nnwm_server *server,
    wlr_output_configuration_v1 *config, bool test)
{
    wlr_output_configuration_head_v1 *head;
    bool ok = true;

    wl_list_for_each(head, &config->heads, link) {
        wlr_output *wlr_output = head->state.output;

        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&head->state, &state);

        if (test) {
            ok = wlr_output_test_state(wlr_output, &state);
            wlr_output_state_finish(&state);
            if (!ok)
                break;
            continue;
        }

        /* Reposition in the output layout */
        wlr_output_layout_add(server->output_layout, wlr_output,
            head->state.x, head->state.y);

        ok = wlr_output_commit_state(wlr_output, &state);
        wlr_output_state_finish(&state);

        if (!ok)
            break;

        /* Recompute usable area with the new logical resolution */
        arrange_layers(server, wlr_output);
    }

    if (test) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
        output_manager_build_config(server);
        arrange_all_outputs(server);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }

    wlr_output_configuration_v1_destroy(config);
}

void
output_manager_apply(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, output_manager_apply);
    auto *config = static_cast<wlr_output_configuration_v1*>(data);
    output_manager_apply_or_test(server, config, false);
}

void
output_manager_test(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, output_manager_test);
    auto *config = static_cast<wlr_output_configuration_v1*>(data);
    output_manager_apply_or_test(server, config, true);
}

/* ---- Output power management ---- */

void
output_power_set_mode(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, output_power_set_mode);
    auto *event = static_cast<wlr_output_power_v1_set_mode_event*>(data);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state,
        event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wlr_output_commit_state(event->output, &state);
    wlr_output_state_finish(&state);

    /* Rebuild output manager config so wlr-randr etc. see the new enabled state */
    output_manager_build_config(server);
}

/* ---- Hot-reload configuration application ---- */

void
server_apply_config(nnwm_server *server)
{
    /* Re-apply monitor configuration (mode, scale, transform, position,
     * enabled/disabled) to all existing outputs.  This mirrors the logic in
     * server_new_output but runs on already-created outputs. */
    {
        nnwm_output *output, *tmp;
        wl_list_for_each_safe(output, tmp, &server->outputs, link) {
            wlr_output *wlr_output = output->wlr_output;

            /* Find matching monitor config (first match wins) */
            nnwm_monitor_config *mc = nullptr;
            {
                auto *cfg = server->config;
                for (int i = 0; i < cfg->monitor_config_count; i++) {
                    auto &c = cfg->monitor_configs[i];
                    bool match = true;
                    if (c.name   && (!wlr_output->name   || strcmp(c.name,   wlr_output->name)   != 0)) match = false;
                    if (c.make   && (!wlr_output->make   || strcmp(c.make,   wlr_output->make)   != 0)) match = false;
                    if (c.model  && (!wlr_output->model  || strcmp(c.model,  wlr_output->model)  != 0)) match = false;
                    if (c.serial && (!wlr_output->serial || strcmp(c.serial, wlr_output->serial) != 0)) match = false;
                    if (match) { mc = &c; break; }
                }
            }

            if (!mc)
                continue;  /* no config for this output — keep current state */

            wlr_output_state state;
            wlr_output_state_init(&state);

            if (mc->disabled) {
                wlr_output_state_set_enabled(&state, false);
                wlr_output_commit_state(wlr_output, &state);
                wlr_output_state_finish(&state);
                continue;
            }

            /* Apply scale */
            if (mc->scale > 0.0f)
                wlr_output_state_set_scale(&state, mc->scale);

            /* Apply transform */
            if (mc->transform >= 0)
                wlr_output_state_set_transform(&state,
                    static_cast<wl_output_transform>(mc->transform));

            /* Pick a mode: match by size/refresh if configured */
            if (mc->width > 0 && mc->height > 0) {
                int target_w   = mc->width;
                int target_h   = mc->height;
                int target_mhz = mc->refresh * 1000;

                wlr_output_mode *mode = nullptr;
                wlr_output_mode *m;
                wl_list_for_each(m, &wlr_output->modes, link) {
                    if (m->width == target_w && m->height == target_h) {
                        if (target_mhz == 0 || m->refresh == target_mhz)
                        { mode = m; break; }
                        if (!mode) mode = m;
                    }
                }
                if (mode)
                    wlr_output_state_set_mode(&state, mode);
            }

            wlr_output_commit_state(wlr_output, &state);
            wlr_output_state_finish(&state);

            /* Reposition in the output layout */
            if (mc->x != INT_MAX && mc->y != INT_MAX)
                wlr_output_layout_add(server->output_layout, wlr_output,
                                      mc->x, mc->y);

            /* Recompute usable area with the new logical resolution and
             * re-tile; arrange_layers reads wlr_output_layout_get_box which
             * now reflects the updated scale/mode. */
            arrange_layers(server, wlr_output);
        }

        /* Update the output manager so wlr-randr / kanshi see the changes */
        output_manager_build_config(server);
    }

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

/* ---- Output creation ---- */

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

    output_manager_build_config(server);
}
