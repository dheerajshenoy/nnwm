#include "nnwm.hpp"
#include "lua/config.hpp"
#include <cstdio>

extern "C" {
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <unistd.h>
}

static int
config_file_changed(int /*fd*/, uint32_t /*mask*/, void *data)
{
    auto *server = static_cast<nnwm_server*>(data);

    /* Drain inotify events */
    char buf[sizeof(struct inotify_event) + 256];
    while (read(server->config_inotify_fd, buf, sizeof(buf)) > 0)
        ;

    std::fprintf(stderr, "nnwm: reloading config\n");
    nnwm_lua_reload(server, server->config);
    server_apply_config(server);
    return 0;
}

int
main(int argc, char *argv[])
{
    wlr_log_init(WLR_DEBUG, nullptr);
    char *startup_cmd  = nullptr;
    char *config_path  = nullptr;

    int c;
    while ((c = getopt(argc, argv, "c:s:h")) != -1)
    {
        switch (c)
        {
            case 'c':
                config_path = optarg;
                break;
            case 's':
                startup_cmd = optarg;
                break;
            default:
                std::fprintf(stderr, "Usage: %s [-c config.lua] [-s startup command]\n", argv[0]);
                return 0;
        }
    }
    if (optind < argc)
    {
        std::fprintf(stderr, "Usage: %s [-s startup command]\n", argv[0]);
        return 0;
    }

    nnwm_server server = {};

    /* Initialize Lua state for config and keybinding callbacks */
    server.config_inotify_fd = -1;
    nnwm_lua_init(&server);

    /* Load config: explicit -c path, then ~/.config/nnwm/init.lua, then defaults */
    server.config = nnwm_config_defaults();
    if (config_path)
    {
        nnwm_lua_load_config(&server, server.config, config_path);
        server.config_path = strdup(config_path);
    }
    else
    {
        const char *home = getenv("HOME");
        if (home)
        {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/.config/nnwm/init.lua", home);
            struct stat st;
            if (stat(path, &st) == 0)
            {
                nnwm_lua_load_config(&server, server.config, path);
                server.config_path = strdup(path);
            }
        }
    }
    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, managing Wayland globals, and so on. */
    server.wl_display           = wl_display_create();
    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. */
    server.backend              = wlr_backend_autocreate(
        wl_display_get_event_loop(server.wl_display), &server.session);
    if (server.backend == nullptr)
    {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return 1;
    }

    /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
     * can also specify a renderer using the WLR_RENDERER env var.
     * The renderer is responsible for defining the various pixel formats it
     * supports for shared memory, this configures that for clients. */
    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == nullptr)
    {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return 1;
    }

    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    /* Autocreates an allocator for us.
     * The allocator is the bridge between the renderer and the backend. It
     * handles the buffer creation, allowing wlroots to render onto the
     * screen */
    server.allocator
        = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == nullptr)
    {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return 1;
    }

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces, the subcompositor allows to
     * assign the role of subsurfaces to surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note
     * that the clients cannot set the selection directly without compositor
     * approval, see the handling of the request_set_selection event below.*/
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);

    /* Creates an output layout, which a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    server.output_layout = wlr_output_layout_create(server.wl_display);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&server.outputs);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    /* Create a scene graph. This is a wlroots abstraction that handles all
     * rendering and damage tracking. All the compositor author needs to do
     * is add things that should be rendered to the scene graph at the proper
     * positions and then call wlr_scene_output_commit() to render a frame if
     * necessary.
     */
    server.scene = wlr_scene_create();
    server.scene_layout
        = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    /* Create scene sub-trees in Z-order (back to front):
     *   background → bottom → windows → top → overlay */
    server.scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]
        = wlr_scene_tree_create(&server.scene->tree);
    server.scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]
        = wlr_scene_tree_create(&server.scene->tree);
    server.scene_windows
        = wlr_scene_tree_create(&server.scene->tree);
    server.scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]
        = wlr_scene_tree_create(&server.scene->tree);
    server.scene_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]
        = wlr_scene_tree_create(&server.scene->tree);

    /* Layer shell (needed by rofi, waybar, etc.) */
    server.layer_shell               = wlr_layer_shell_v1_create(server.wl_display, 4);
    server.new_layer_surface.notify  = server_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

    /* XDG decoration: tell clients to use client-side decorations */
    server.decoration_manager       = wlr_xdg_decoration_manager_v1_create(server.wl_display);
    server.new_decoration.notify    = server_new_decoration;
    wl_signal_add(&server.decoration_manager->events.new_toplevel_decoration,
                  &server.new_decoration);

    /* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
     * used for application windows. For more detail on shells, refer to
     * https://drewdevault.com/2018/07/29/Wayland-shells.html.
     */
    wl_list_init(&server.toplevels);
    server.xdg_shell               = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel,
                  &server.new_xdg_toplevel);
    server.new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). */
    server.cursor_mgr = wlr_xcursor_manager_create(
        server.config->cursor_theme, server.config->cursor_size);

    /*
     * wlr_cursor *only* displays an image on screen. It does not move around
     * when the pointer moves. However, we can attach input devices to it, and
     * it will generate aggregate events for all of them. In these events, we
     * can choose how we want to process them, forwarding them to clients and
     * moving the cursor around. More detail on this process is described in
     * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
     *
     * And more comments are sprinkled throughout the notify functions above.
     */
    server.cursor_mode          = NNWM_CURSOR_PASSTHROUGH;
    server.cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute,
                  &server.cursor_motion_absolute);
    server.cursor_button.notify = server_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    server.cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    server.cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_list_init(&server.keyboards);
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    server.seat                  = wlr_seat_create(server.wl_display,
                                                    server.config->seat_name);
    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor,
                  &server.request_cursor);
    server.pointer_focus_change.notify = seat_pointer_focus_change;
    wl_signal_add(&server.seat->pointer_state.events.focus_change,
                  &server.pointer_focus_change);
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection,
                  &server.request_set_selection);

    /* Add a Unix socket to the Wayland display. */
    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket)
    {
        wlr_backend_destroy(server.backend);
        return 1;
    }

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(server.backend))
    {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* Set the WAYLAND_DISPLAY environment variable to our socket and run the
     * startup command if requested. */
    setenv("WAYLAND_DISPLAY", socket, true);
    /* Unset DISPLAY so clients don't try to connect to a non-existent X server.
     * Without this, toolkits like GLFW/SDL that support both X11 and Wayland
     * will attempt X11 first (because $DISPLAY is set from the parent TTY
     * session) and fail with an EGL/GLX error. */
    unsetenv("DISPLAY");
    if (startup_cmd)
    {
        if (fork() == 0)
        {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, static_cast<char*>(nullptr));
        }
    }
    /* Set up inotify watch on config file for hot-reload */
    if (server.config_path)
    {
        server.config_inotify_fd = inotify_init1(IN_NONBLOCK);
        if (server.config_inotify_fd >= 0)
        {
            int wd = inotify_add_watch(server.config_inotify_fd,
                                       server.config_path, IN_MODIFY);
            if (wd >= 0)
            {
                struct wl_event_loop *loop
                    = wl_display_get_event_loop(server.wl_display);
                server.config_event_source = wl_event_loop_add_fd(
                    loop, server.config_inotify_fd, WL_EVENT_READABLE,
                    config_file_changed, &server);
            }
            else
            {
                std::fprintf(stderr, "nnwm: inotify_add_watch failed\n");
                close(server.config_inotify_fd);
                server.config_inotify_fd = -1;
            }
        }
    }

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
            socket);
    wl_display_run(server.wl_display);

    /* Once wl_display_run returns, we destroy all clients then shut down the
     * server. */
    wl_display_destroy_clients(server.wl_display);

    wl_list_remove(&server.new_decoration.link);
    wl_list_remove(&server.new_layer_surface.link);
    wl_list_remove(&server.new_xdg_toplevel.link);
    wl_list_remove(&server.new_xdg_popup.link);

    wl_list_remove(&server.cursor_motion.link);
    wl_list_remove(&server.cursor_motion_absolute.link);
    wl_list_remove(&server.cursor_button.link);
    wl_list_remove(&server.cursor_axis.link);
    wl_list_remove(&server.cursor_frame.link);

    wl_list_remove(&server.new_input.link);
    wl_list_remove(&server.request_cursor.link);
    wl_list_remove(&server.pointer_focus_change.link);
    wl_list_remove(&server.request_set_selection.link);

    wl_list_remove(&server.new_output.link);

    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    if (server.config_event_source)
        wl_event_source_remove(server.config_event_source);
    if (server.config_inotify_fd >= 0)
        close(server.config_inotify_fd);
    free(server.config_path);
    nnwm_config_free(server.config);
    nnwm_lua_fini(&server);
    return 0;
}
