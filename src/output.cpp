#include "nnwm.hpp"
#include "nnwm_internal.hpp"

#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>

/* Build "make model serial" description string for matching (caller must free)
 */
static char *
output_description(const wlr_output *o)
{
    char buf[512] = {};
    if (o->make && o->make[0])
    {
        if (buf[0])
            strcat(buf, " ");
        strcat(buf, o->make);
    }
    if (o->model && o->model[0])
    {
        if (buf[0])
            strcat(buf, " ");
        strcat(buf, o->model);
    }
    if (buf[0])
        strcat(buf, " ");
    if (o->serial && o->serial[0])
        strcat(buf, o->serial);
    else
        strcat(buf, "Unknown");
    return strdup(buf);
}

/* ---- Output frame and state request ---- */

static void
send_frame_done_cb(wlr_surface *surface, int /*sx*/, int /*sy*/, void *data)
{
    wlr_surface_send_frame_done(surface, static_cast<timespec *>(data));
}

static void
output_frame(wl_listener *listener, void * /*data*/)
{
    nnwm_output *output = wl_container_of(listener, output, frame);

    if (!output->wlr_output->enabled)
        return;

    /* Don't attempt any rendering while the session is inactive (VT switch).
     * The DRM backend may not hold master, and scenefx's renderer can crash
     * if asked to begin a buffer pass without a valid GPU context. */
    nnwm_server *server = output->server;
    if (server->session && !server->session->active)
        return;

    wlr_scene_output *scene_output
        = wlr_scene_get_scene_output(output->server->scene, output->wlr_output);
    if (!scene_output)
        return;

#ifdef HAVE_SCENEFX
    animate_step(output->server);

    /* Before committing this output: temporarily hide any geo-animated window
     * whose home output is a *different* monitor.  Without this, wlroots
     * renders the node on every output whose viewport it overlaps, causing
     * workspace-slide windows to bleed onto adjacent screens. */
    {
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            tl->commit_hidden = false;
            if (!tl->geo_anim || !tl->output || tl->output == output) continue;
            if (!tl->scene_tree->node.enabled) continue;
            wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
            tl->commit_hidden = true;
        }
        wl_list_for_each(tl, &server->dying_toplevels, dying_link)
        {
            tl->commit_hidden = false;
            if (!tl->geo_anim || !tl->output || tl->output == output) continue;
            if (!tl->scene_tree->node.enabled) continue;
            wlr_scene_node_set_enabled(&tl->scene_tree->node, false);
            tl->commit_hidden = true;
        }
    }
#endif

    /* In overview mode, rebuild the GPU buffer every frame so window textures
     * reflect the latest client commits across all workspaces. */
    if (output->overview)
        overview_frame_update(output->server, output);

    wlr_scene_output_state_options commit_opts = {};
    if (output->server->gamma_control_manager)
    {
        wlr_gamma_control_v1 *gc = wlr_gamma_control_manager_v1_get_control(
            output->server->gamma_control_manager, output->wlr_output);
        if (gc)
            commit_opts.color_transform = wlr_gamma_control_v1_get_color_transform(gc);
    }

    bool committed = wlr_scene_output_commit(scene_output, &commit_opts);

#ifdef HAVE_SCENEFX
    /* Restore windows that were hidden solely for this output's commit. */
    {
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->commit_hidden)
            {
                wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
                tl->commit_hidden = false;
            }
        }
        wl_list_for_each(tl, &server->dying_toplevels, dying_link)
        {
            if (tl->commit_hidden)
            {
                wlr_scene_node_set_enabled(&tl->scene_tree->node, true);
                tl->commit_hidden = false;
            }
        }
    }
#endif

    if (!committed)
        return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);

    /* In overview mode, send frame_done to toplevels on inactive workspaces
     * so their clients continue rendering and the overview stays live. */
    if (output->overview) {
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &output->server->toplevels, link) {
            if (tl->output != output) continue;
            if (tl->workspace == output->active_workspace) continue;
            if (tl->in_scratchpad) continue;
            wlr_surface_for_each_surface(
                tl->xdg_toplevel->base->surface,
                send_frame_done_cb, &now);
        }
    }
}

void
output_request_state(wl_listener *listener, void *data)
{
    nnwm_output *output = wl_container_of(listener, output, request_state);
    const auto *event
        = static_cast<const wlr_output_event_request_state *>(data);
    wlr_output_commit_state(output->wlr_output, event->state);
}

void
output_destroy(wl_listener *listener, void * /*data*/)
{
    nnwm_output *output = wl_container_of(listener, output, destroy);
    nnwm_server *server = output->server;

    if (server->focused_output == output)
    {
        server->focused_output = nullptr;
        nnwm_output *o;
        wl_list_for_each(o, &server->outputs, link) if (o != output)
        {
            server->focused_output = o;
            break;
        }
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    if (output->tab_bar)
        wlr_scene_node_destroy(&output->tab_bar->node);
    if (output->error_bar)
        wlr_scene_node_destroy(&output->error_bar->node);
    if (output->overview_buf)
        wlr_scene_node_destroy(&output->overview_buf->node);
    if (output->overview_labels)
        wlr_scene_node_destroy(&output->overview_labels->node);
    for (int i = 0; i < NNWM_NUM_WORKSPACES; i++)
        free(output->workspace_names[i]);

    /* Migrate toplevels that were on this output so no dangling tl->output
     * pointers survive the delete below.  After a VT switch the DRM backend
     * destroys and re-creates outputs; leaving stale pointers would segfault
     * any WM action that dereferences tl->output. */
    {
        nnwm_output *replacement = server->focused_output;
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->output != output)
                continue;
            tl->output = replacement;
        }
        if (replacement)
            arrange_windows(server, replacement);
    }

    delete output;

    output_manager_build_config(server);
}

/* ---- VT resume ---- */

void
server_session_active(wl_listener *listener, void * /*data*/)
{
    nnwm_server *server = wl_container_of(listener, server, session_active);
    if (!server->session || !server->session->active)
        return;
    /* Refresh usable_area from the output layout and re-tile every output.
     * The DRM backend may have applied a new mode or transform while the
     * session was inactive, so we must re-query the layout box. */
    nnwm_output *out;
    wl_list_for_each(out, &server->outputs, link)
        arrange_layers(server, out->wlr_output);
    /* Re-upload the cursor image to the DRM cursor plane, which is lost
     * across VT switches. */
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

/* ---- wlr-output-management ---- */

void
output_manager_build_config(nnwm_server *server)
{
    wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();

    nnwm_output *output;
    wl_list_for_each(output, &server->outputs, link)
    {
        wlr_output_configuration_head_v1 *head
            = wlr_output_configuration_head_v1_create(config,
                                                      output->wlr_output);

        head->state.enabled = output->wlr_output->enabled;
        head->state.mode    = output->wlr_output->current_mode;

        struct wlr_output_layout_output *l_output
            = wlr_output_layout_get(server->output_layout, output->wlr_output);
        if (l_output)
        {
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

    wl_list_for_each(head, &config->heads, link)
    {
        wlr_output *wlr_output = head->state.output;

        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&head->state, &state);

        if (test)
        {
            ok = wlr_output_test_state(wlr_output, &state);
            wlr_output_state_finish(&state);
            if (!ok)
                break;
            continue;
        }

        /* Reposition in the output layout */
        wlr_output_layout_add(server->output_layout, wlr_output, head->state.x,
                              head->state.y);

        ok = wlr_output_commit_state(wlr_output, &state);
        wlr_output_state_finish(&state);

        if (!ok)
            break;

        /* Recompute usable area with the new logical resolution */
        arrange_layers(server, wlr_output);
    }

    if (test)
    {
        wlr_output_configuration_v1_send_succeeded(config);
    }
    else if (ok)
    {
        wlr_output_configuration_v1_send_succeeded(config);
        output_manager_build_config(server);
        arrange_all_outputs(server);
    }
    else
    {
        wlr_output_configuration_v1_send_failed(config);
    }

    wlr_output_configuration_v1_destroy(config);
}

void
output_manager_apply(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, output_manager_apply);
    auto *config = static_cast<wlr_output_configuration_v1 *>(data);
    output_manager_apply_or_test(server, config, false);
}

void
output_manager_test(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, output_manager_test);
    auto *config = static_cast<wlr_output_configuration_v1 *>(data);
    output_manager_apply_or_test(server, config, true);
}

/* ---- Output power management ---- */

void
output_power_set_mode(wl_listener *listener, void *data)
{
    nnwm_server *server
        = wl_container_of(listener, server, output_power_set_mode);
    auto *event = static_cast<wlr_output_power_v1_set_mode_event *>(data);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state,
                                 event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wlr_output_commit_state(event->output, &state);
    wlr_output_state_finish(&state);

    /* Rebuild output manager config so wlr-randr etc. see the new enabled state
     */
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
        wl_list_for_each_safe(output, tmp, &server->outputs, link)
        {
            wlr_output *wlr_output = output->wlr_output;

            /* Find matching monitor config (first match wins) */
            nnwm_monitor_config *mc = nullptr;
            {
                auto *cfg  = server->config;
                char *desc = output_description(wlr_output);
                for (int i = 0; i < cfg->monitor_config_count; i++)
                {
                    auto &c    = cfg->monitor_configs[i];
                    bool match = true;
                    if (c.name
                        && (!wlr_output->name
                            || strcmp(c.name, wlr_output->name) != 0))
                        match = false;
                    if (c.description && strcmp(c.description, desc) != 0)
                        match = false;
                    if (match)
                    {
                        mc = &c;
                        break;
                    }
                }
                free(desc);
            }

            if (!mc)
                continue; /* no config for this output — keep current state */

            wlr_output_state state;
            wlr_output_state_init(&state);

            if (mc->disabled)
            {
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
                wlr_output_state_set_transform(
                    &state, static_cast<wl_output_transform>(mc->transform));

            /* Pick a mode: match by size/refresh if configured */
            if (mc->width > 0 && mc->height > 0)
            {
                int target_w   = mc->width;
                int target_h   = mc->height;
                int target_mhz = mc->refresh * 1000;

                wlr_output_mode *mode = nullptr;
                wlr_output_mode *m;
                wl_list_for_each(m, &wlr_output->modes, link)
                {
                    if (m->width == target_w && m->height == target_h)
                    {
                        if (target_mhz == 0 || m->refresh == target_mhz)
                        {
                            mode = m;
                            break;
                        }
                        if (!mode)
                            mode = m;
                    }
                }
                if (mode)
                    wlr_output_state_set_mode(&state, mode);
            }

            wlr_output_commit_state(wlr_output, &state);
            wlr_output_state_finish(&state);

            /* Reposition in the output layout */
            if (mc->x != INT_MAX && mc->y != INT_MAX)
                wlr_output_layout_add(server->output_layout, wlr_output, mc->x,
                                      mc->y);

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
    wl_list_for_each(tl, &server->toplevels, link)
    {
        float *color = (tl->xdg_toplevel->base->surface == focused_surface)
                           ? server->config->border.focused_color
                           : server->config->border.unfocused_color;
        for (int i = 0; i < 4; i++)
            wlr_scene_rect_set_color(tl->border[i], color);
    }

    /* Re-apply decoration mode (CSD vs SSD) to all live toplevels */
    wl_list_for_each(tl, &server->toplevels, link)
    {
        if (tl->decoration)
            decoration_apply(tl->decoration,
                             server->config->client_decorations);
    }

    /* Re-apply scenefx decorations (corner radius / shadow) to all windows */
    {
        nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link) apply_fx_decorations(tl);
    }

    /* Re-arrange to apply border_width / master_ratio changes */
    arrange_all_outputs(server);

    /* Update keymap and repeat info for all connected keyboards */
    nnwm_keyboard *kb;
    wl_list_for_each(kb, &server->keyboards, link)
    {
        apply_keymap(kb->wlr_keyboard, server->config);
        wlr_keyboard_set_repeat_info(kb->wlr_keyboard,
                                     server->config->keyboard.repeat_rate,
                                     server->config->keyboard.repeat_delay);
    }
}

/* ---- Output creation ---- */

void
server_new_output(wl_listener *listener, void *data)
{
    nnwm_server *server    = wl_container_of(listener, server, new_output);
    wlr_output *wlr_output = static_cast<struct wlr_output *>(data);

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    wlr_log(WLR_INFO, "new output: name='%s' make='%s' model='%s' serial='%s'",
            wlr_output->name ? wlr_output->name : "",
            wlr_output->make ? wlr_output->make : "",
            wlr_output->model ? wlr_output->model : "",
            wlr_output->serial ? wlr_output->serial : "");

    /* Find matching monitor config (first match wins) */
    nnwm_monitor_config *mc = nullptr;
    {
        auto *cfg  = server->config;
        char *desc = output_description(wlr_output);
        for (int i = 0; i < cfg->monitor_config_count; i++)
        {
            auto &c    = cfg->monitor_configs[i];
            bool match = true;
            if (c.name
                && (!wlr_output->name || strcmp(c.name, wlr_output->name) != 0))
                match = false;
            if (c.description && strcmp(c.description, desc) != 0)
                match = false;
            if (match)
            {
                mc = &c;
                break;
            }
        }
        free(desc);
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
        wlr_output_state_set_transform(
            &state, static_cast<wl_output_transform>(mc->transform));

    /* Pick a mode: match by size/refresh if configured, else preferred */
    wlr_output_mode *mode = nullptr;
    if (mc && mc->width > 0 && mc->height > 0)
    {
        int target_w = mc->width;
        int target_h = mc->height;
        int target_mhz
            = mc->refresh * 1000; /* user passes Hz; wlroots uses mHz */

        wlr_output_mode *m;
        wl_list_for_each(m, &wlr_output->modes, link)
        {
            if (m->width == target_w && m->height == target_h)
            {
                if (target_mhz == 0 || m->refresh == target_mhz)
                {
                    mode = m;
                    break;
                }
                if (!mode)
                    mode = m; /* fallback: closest match on size */
            }
        }
        if (!mode)
            std::fprintf(stderr,
                         "nnwm: monitor '%s' has no mode %dx%d, "
                         "falling back to preferred\n",
                         wlr_output->name ? wlr_output->name : "?", target_w,
                         target_h);
    }
    if (!mode)
        mode = wlr_output_preferred_mode(wlr_output);
    if (mode)
        wlr_output_state_set_mode(&state, mode);

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    nnwm_output *output = new nnwm_output{};
    output->wlr_output  = wlr_output;
    output->server      = server;
    wlr_output_layout_get_box(server->output_layout, wlr_output,
                              &output->usable_area);

    output->active_workspace = 0;
    output->prev_workspace   = 0;
    memset(output->last_focused, 0, sizeof(output->last_focused));
    memset(output->prev_focused, 0, sizeof(output->prev_focused));
    for (int i = 0; i < NNWM_NUM_WORKSPACES; i++)
    {
        /* Priority: monitor-specific > global default > htile */
        int mon_dfl = (mc && mc->workspace_layouts[i] >= 0)
                          ? mc->workspace_layouts[i] : -1;
        int glb_dfl = server->config->workspace_default_layouts[i];
        int dfl     = (mon_dfl >= 0) ? mon_dfl : glb_dfl;
        output->layout_mode[i]   = (dfl >= 0)
                                        ? static_cast<nnwm_layout_mode>(dfl)
                                        : nnwm_layout_mode::HTILE;
        output->master_ratio[i]  = server->config->layout.master_ratio;
        output->scroll_offset[i] = 0;
        /* Workspace name: monitor override > global config > nullptr */
        const char *mon_name = (mc && mc->workspace_names[i])
                                   ? mc->workspace_names[i] : nullptr;
        const char *glb_name = server->config->workspace_names[i];
        const char *eff_name = mon_name ? mon_name : glb_name;
        output->workspace_names[i] = eff_name ? strdup(eff_name) : nullptr;
    }
    output->tab_bar = wlr_scene_buffer_create(server->scene_windows, nullptr);
    wlr_scene_node_set_enabled(&output->tab_bar->node, false);
    output->error_bar = wlr_scene_buffer_create(
        server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], nullptr);
    wlr_scene_node_set_enabled(&output->error_bar->node, false);
    output->overview_buf = wlr_scene_buffer_create(
        server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], nullptr);
    wlr_scene_node_set_enabled(&output->overview_buf->node, false);
    output->overview_labels = wlr_scene_buffer_create(
        server->scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], nullptr);
    wlr_scene_node_set_enabled(&output->overview_labels->node, false);
    output->overview = false;
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
        l_output
            = wlr_output_layout_add_auto(server->output_layout, wlr_output);

    wlr_scene_output *scene_output
        = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                       scene_output);

    output_manager_build_config(server);
    fire_hook_output(server, "output_connect", output);
}

void
handle_gamma_control_set_gamma(wl_listener *listener, void *data)
{
    nnwm_server *server = wl_container_of(listener, server, gamma_control_set_gamma);
    auto *event = static_cast<wlr_gamma_control_manager_v1_set_gamma_event *>(data);
    wlr_output_schedule_frame(event->output);
}
