# nnwm CHANGELOG

## 0.1.2

### Features

- **Per-monitor workspace names** (`workspaces.names` in `nnwm.monitor()`):
  workspace labels can now be overridden per monitor. The `workspaces` sub-table
  in `nnwm.monitor()` accepts a `names` string array. Priority: monitor-specific
  `workspaces.names` > global `nnwm.opt.workspace_names` > numeric fallback.
- **`current_workspace().name`**: `nnwm.current_workspace()` now returns a `name`
  field containing the resolved effective workspace label for the focused output
  (`nil` if no name is configured for that workspace slot).

### Breaking Changes

- **`nnwm.monitor()` workspace fields restructured**: the flat `workspace_layouts`
  field is now nested under a `workspaces` sub-table. Replace:
  ```lua
  nnwm.monitor({ name = "eDP-1", workspace_layouts = { "htile", "float" } })
  ```
  with:
  ```lua
  nnwm.monitor({ name = "eDP-1", workspaces = { layouts = { "htile", "float" } } })
  ```

### Bug Fixes

- **Workspace switch animation bleeds onto adjacent monitors**: windows sliding
  off one monitor's edge were rendered on the neighbouring monitor by the wlroots
  scene graph. The animation loop now hides a window's scene node whenever its
  interpolated geometry no longer intersects its home output's bounding box,
  matching the behaviour of Hyprland and Niri.
- **Workspace switch animation fires in overview mode**: sliding windows during a
  workspace switch while the overview was open produced a brief, jarring animation.
  The animation block in `workspace::switch_to` is now skipped entirely when the
  output is in overview mode, making workspace navigation instant there.
- **Floating windows invisible in overview mode**: `arrange_windows_impl` only
  enables tiled windows, so floating windows were hidden by the overview's
  hide-all step and never re-enabled. The overview arrange loop now explicitly
  re-enables floating windows for each workspace after `arrange_windows_impl`
  runs.
- **Floating windows invisible after exiting overview**: the exit-overview path
  called `arrange_windows` which only re-enables tiled windows. Floating windows
  hidden by the overview's hide-all step were never restored. A visibility sweep
  now runs before `arrange_windows` in `exit_overview`, re-enabling every
  non-scratchpad window whose workspace matches its output's active workspace.
- **VT switching segfault**: switching to another VT and back could destroy and
  recreate DRM outputs. `output_destroy` freed the `nnwm_output` object but left
  `tl->output` on all toplevels pointing to freed memory. Any subsequent WM action
  that dereferenced `tl->output` (e.g. `toggle_sticky`, workspace switch) segfaulted.
  `output_destroy` now migrates all toplevels from the destroyed output to the
  replacement output before calling `delete output`, eliminating the dangling pointer.

## 0.1.1

### Features

- **Per-monitor workspace names** (`workspaces.names` in `nnwm.monitor()`):
  workspace labels can now be overridden per monitor. The `nnwm.monitor()`
  config table accepts a `workspaces` sub-table with `names` (string array)
  and `layouts` (string array) fields. Priority for names: monitor-specific >
  global `workspace_names` > numeric fallback. `nnwm.current_workspace()` now
  includes a `name` field with the resolved effective label (`nil` if unset).
- **Overview mode window drag-and-drop**: clicking a window thumbnail in the
  overview now focuses it (switching to its workspace if needed) and exits the
  overview. Dragging a window thumbnail to a different workspace slot moves the
  window there while keeping the overview open so you can keep rearranging.
  An amber border highlights the drop target slot during the drag. Motion
  updates use a cheap Cairo-only redraw so dragging stays smooth.
- **Float layout (`"float"`)**: a new layout mode where all windows in the
  workspace are floating. Switching to float layout converts any existing tiled
  windows to floating (centered at half output size if they have no prior
  geometry). `nnwm.layout.toggle_float()` toggles between float and htile on
  the active workspace. `nnwm.layout.set(name)` sets any layout directly by
  name. `nnwm.layout.next()` and `nnwm.layout.prev()` now cycle through float
  as well.
- **Configurable default workspace layouts** (`nnwm.opt.workspace_layouts`):
  a string array that sets the initial layout for each workspace on all
  monitors. Array index corresponds to workspace number; workspaces without
  an entry default to `"htile"`. Example:
  `workspace_layouts = { "htile", "float", "tabbed", "vscroll" }`.
- **Per-monitor workspace layout overrides** (`nnwm.monitor` `workspace_layouts`
  field): overrides the global `workspace_layouts` for the matched monitor.
  Only specified positions are overridden; unspecified workspaces fall back to
  the global default then `"htile"`. Priority: monitor-specific >
  global `workspace_layouts` > `"htile"`.
- **Drag-and-drop between applications**: nnwm now fully handles the Wayland
  `wl_data_device` drag-and-drop protocol. The compositor validates
  `request_start_drag` events and starts pointer drags, renders the drag icon
  surface as a scene node that tracks the cursor, and on a successful drop
  focuses the window under the cursor so keyboard input follows the drop target.
  Cancelled drags (Escape) leave focus unchanged.
- **VSCROLL layout now behaves like Niri**: each window fills the full output
  height (minus outer gaps). Windows are stacked vertically and the viewport
  snaps to the focused window, showing exactly one window at a time. The
  fractional `scroll_row_height` config field is no longer used for VSCROLL.
- Add animation to window fullscreen action
- Add `smart` option to `tabbed` layout, which automatically hides the tab bar when
  only one window is present. The tab bar reappears when a second window is added
  to the workspace.
- Add `overview_mode` which shows all workspaces in a grid layout giving you the overview
  of the workspace per-monitor.
- **Titlebar hidden on fullscreen**: entering fullscreen now hides the server-side
  titlebar and removes its vertical offset so the window occupies the full output
  area. The same applies to fake-fullscreen. Exiting fullscreen restores the
  titlebar through the normal layout path.
- **`workspace_back_and_forth`** config option: when enabled, switching to the
  currently active workspace jumps to the previously visited workspace instead
  of doing nothing. Each output tracks its own previous workspace independently.
- **`show_config_error_overlay`** config option: controls whether the red error
  overlay is displayed when the Lua config fails to load. Defaults to `true`.
  Disabling it is dangerous — config errors will be silently ignored and no
  visual feedback will appear.
- **Lid and tablet-mode hook events**: nnwm now registers `wlr_switch` devices
  (laptop lid sensor, tablet-mode sensor) and fires the following hook events:
  - `"lid_close"` — fired when the laptop lid is closed
  - `"lid_open"` — fired when the laptop lid is opened
  - `"tablet_mode_on"` — fired when the device enters tablet mode
  - `"tablet_mode_off"` — fired when the device leaves tablet mode
- **Optional keybinding description**: `nnwm.key` accepts an optional third
  string argument — a human-readable label stored for future use in a
  keybindings overlay. Example: `nnwm.key({"Super","Return"}, fn, "Launch terminal")`.
- **Customisable workspace names and count**: `workspace_names` in `nnwm.opt`
  accepts a string array whose length implicitly sets the workspace count (1–9).
  Names are shown in the overview grid and sent to clients via `ext-workspace-v1`
  (e.g. waybar). Empty strings fall back to the numeric index. On config hot-reload
  the ext-workspace handles are rebuilt live: removed workspaces receive a
  `removed` event and new workspaces are announced without restarting clients.
- **XKB keymap file**: `keyboard.xkb_file` accepts a path to a compiled XKB
  keymap file (e.g. produced by `xkbcomp`). When set it takes precedence over
  `xkb_rules`/`xkb_layout`/`xkb_variant`/`xkb_options`. Falls back to the
  rules-based path with a log warning if the file cannot be opened or compiled.
- **`nnwm.find_cursor()` cursor attention animation**: calling `nnwm.find_cursor()`
  from a keybinding flashes an animation at the current cursor position to help
  locate it on large or multi-monitor setups. Two styles are available via
  `nnwm.opt.find_cursor_style`:
  - `"rings"` *(default)* — a filled circle shrinks toward the cursor over ~640 ms.
  - `"spotlight"` — the entire output dims and a soft circular cutout reveals the
    area around the cursor, fading in, holding, then fading out over ~1600 ms.

### Bug Fixes

- **Floating windows invisible after workspace switch**: the SLIDE and FADE
  workspace animation loops iterated over floating windows and used stale
  `geo_to_*` values (set during the previous slide-out) to drive the slide-in
  animation, moving them to off-screen coordinates where they remained. Floating
  windows are now excluded from both animation loops; the visibility sweep in
  `switch_to` already shows and hides them correctly.
- **`warp_to_focused_window` no longer fires on pointer motion**: the cursor warp
  was previously triggered by focus-follows-mouse (moving the physical pointer over
  a window), causing the cursor to jump back to the window centre mid-gesture.
  Warping now only happens on programmatic focus changes (keybinding, window map,
  workspace switch), not when the user moves the pointer.
- Fix emacs starting in floating mode due to spurious `xdg_toplevel_request_move`
  events fired by GTK with no button held; the handler now ignores the request
  when `pointer_state.button_count == 0`.
- Overview mode workspace switching not signalling the workspace change to waybar's
  `ext/workspaces` module; `ext_workspace_notify` is now called from `ov_switch_ws`.
- **Scratchpad floating window reverts to tiled on re-show**: floating windows in
  the scratchpad were repositioned by `arrange_scratchpad` on every show,
  overwriting any user resize/float. The layout loops now skip windows with
  `floating = true` and a separate `scratch_count_all` counter is used for the
  empty-scratchpad check, while `scratch_count` (tiled only) drives layout math.
- **Floating scratchpad window invisible after workspace switch**: the workspace
  visibility sweep in `workspace::switch_to` and `ov_switch_ws` was setting
  scratchpad window scene nodes to `enabled = false` when their workspace did not
  match the new active workspace. Scratchpad visibility is owned by the scratchpad
  toggle; the sweep now skips all `in_scratchpad` windows.
- **`move_to_workspace` from scratchpad makes window disappear**: calling
  `nnwm.move_to_workspace()` on a scratchpad window only changed `tl->workspace`
  but left the scene node parented under `scene_scratchpad`. The window disappeared
  when the scratchpad was hidden. The action now clears `in_scratchpad`, reparents
  the scene node to `scene_windows`, and re-arranges the scratchpad before
  proceeding with the normal workspace assignment.
- **Fullscreen window invisible after workspace switch and return**: the workspace
  slide/fade animation loops iterated over fullscreen windows and used stale
  `geo_to_*` values (the last tiled position) to drive the animation, moving the
  window to the wrong coordinates on return. Fullscreen and fake-fullscreen windows
  are now skipped in both animation loops; the visibility sweep already handles
  showing and hiding them correctly.
- **Fullscreen window not focused after workspace switch**: `switch_to` focused
  the result of `ws_first()` which excludes fullscreen windows, so returning to a
  workspace whose only window was fullscreen cleared keyboard focus. The focus
  logic now tries `last_focused[ws]` first (which may hold a fullscreen window),
  then falls back to `ws_first`, then `ws_first_float`.
- **Cursor shape on resize/move**: Super+right-click resize now shows the
  appropriate directional cursor (`nw-resize`, `se-resize`, etc.) based on which
  edges are being dragged. Super+left-click move shows `move`. The cursor is
  restored to `default` when the drag ends. Client cursor-change requests are
  suppressed during compositor-managed move/resize so the shape stays consistent.
- **Cursor warps to resize corner on resize start**: when Super+right-click
  initiates a resize, the cursor is warped to the window corner being dragged
  (bottom-right by default) so the grab point is exact from the first motion event.
- **VT resume segfault**: `output_frame` now checks `session->active` before
  attempting any rendering. Previously a stale DRM frame event arriving while
  the session was transitioning could cause scenefx's overview render pass
  (`wlr_renderer_begin_buffer_pass`) to crash with the GPU context in an
  undefined state. The DRM cursor plane is also re-uploaded on VT resume via
  `wlr_cursor_set_xcursor` in `server_session_active`.


## 0.1.0

### Features

- **Event hooks** (`nnwm.on`): register Lua callbacks for compositor events. All
  callbacks fire with a snapshot table matching the event type. Multiple callbacks
  can be registered for the same event; all are called in registration order.
  Supported events:
  - `"startup"` — fires once on the first event-loop tick, after autostart
    commands and `WAYLAND_DISPLAY` are set. No argument. Timers started inside
    the callback arm correctly because the event loop is already running.
  - `"shutdown"` — fires when the compositor is about to exit, before clients
    are destroyed. No argument.
  - `"window_focus"` — fires when a window receives keyboard focus. Receives an
    `nnwm.Window` snapshot table.
  - `"window_open"` — fires when a window is mapped (first appears on screen).
    Receives an `nnwm.Window` snapshot table.
  - `"window_close"` — fires when a window unmaps (closes). Receives an
    `nnwm.Window` snapshot table.
  - `"workspace_switch"` — fires when the active workspace changes on any output.
    Receives an `nnwm.Workspace` snapshot table.
  - `"output_connect"` — fires when a new monitor is connected. Receives an
    `nnwm.Output` snapshot table.
  ```lua
  nnwm.on("window_focus", function(win)
      print("focused:", win.title, win.app_id)
  end)
  nnwm.on("workspace_switch", function(ws)
      print("workspace", ws.index, "on", ws.output)
  end)
  nnwm.on("startup", function()
      nnwm.spawn_once("waybar")
  end)
  ```
- **Timers** (`nnwm.timer`, `nnwm.interval`): schedule Lua callbacks from the
  Wayland event loop, avoiding external polling or shell scripts.
  - `nnwm.timer(ms, fn)` — run `fn` once after `ms` milliseconds.
  - `nnwm.interval(ms, fn)` — run `fn` every `ms` milliseconds, repeating
    indefinitely until the compositor exits.
  ```lua
  nnwm.interval(30000, function()
      nnwm.spawn("~/.local/bin/update-wallpaper.sh")
  end)
  nnwm.timer(500, function()
      nnwm.spawn("notify-send 'nnwm started'")
  end)
  ```
- **Introspection API**: query the current compositor state from Lua.
  - `nnwm.current_window()` — returns an `nnwm.Window` snapshot for the focused
    window, or `nil` if no window is focused. Fields: `title`, `app_id`,
    `floating`, `fullscreen`, `fake_fullscreen`, `maximized`, `sticky`,
    `workspace`, `x`, `y`, `width`, `height`, `output`.
  - `nnwm.current_workspace()` — returns an `nnwm.Workspace` snapshot for the
    active workspace on the focused output, or `nil`. Fields: `index`, `layout`,
    `master_ratio`, `window_count`, `output`.
  - `nnwm.current_output()` — returns an `nnwm.Output` snapshot for the focused
    output, or `nil`. Fields: `name`, `description`, `width`, `height`, `scale`,
    `x`, `y`, `active_workspace`.
- **`nnwm.move_dir(direction)`**: move the focused window in a geometric direction.
  On the same output, swaps the focused window with the nearest tiled window in
  that direction (center-to-center, ties broken by perpendicular distance). If no
  tiled window exists in that direction, moves the window to the nearest monitor
  in that direction.
  ```lua
  nnwm.key({"Super", "Shift", "h"}, function() nnwm.move_dir("left")  end)
  nnwm.key({"Super", "Shift", "l"}, function() nnwm.move_dir("right") end)
  nnwm.key({"Super", "Shift", "k"}, function() nnwm.move_dir("up")    end)
  nnwm.key({"Super", "Shift", "j"}, function() nnwm.move_dir("down")  end)
  ```
- **`nnwm.focus_dir(direction)`**: geometric directional focus that crosses
  monitor boundaries. `direction` is `"left"`, `"right"`, `"up"`, or `"down"`.
  The nearest window in the requested direction (by center-to-center distance on
  the primary axis, breaking ties by perpendicular distance) on the current
  output is focused. If no window exists in that direction, the nearest monitor
  in that direction is focused instead and its last active window is selected.
  ```lua
  nnwm.key({"Super", "h"}, function() nnwm.focus_dir("left")  end)
  nnwm.key({"Super", "l"}, function() nnwm.focus_dir("right") end)
  nnwm.key({"Super", "k"}, function() nnwm.focus_dir("up")    end)
  nnwm.key({"Super", "j"}, function() nnwm.focus_dir("down")  end)
  ```
- **`nnwm.monitor()` API**: monitor configuration is now done with individual
  `nnwm.monitor({ ... })` function calls instead of the `nnwm.opt.monitors`
  array. Each call registers one output rule in declaration order; the first
  matching rule wins. This makes multi-monitor configs more readable and
  composable, and allows conditional logic between monitor declarations:
  ```lua
  nnwm.monitor({ name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 })
  nnwm.monitor({ description = "HP Inc. HP P24h G5 3CM5031JJC", x = 1536, y = 0, width = 1920, height = 1080 })
  nnwm.monitor({ name = "HDMI-A-2", disabled = true })
  ```
  **Breaking change**: `nnwm.opt.monitors` is no longer read. Configs must be
  migrated to use `nnwm.monitor()` calls.
- **Per-workspace master ratio**: the master split ratio is now tracked per
  workspace per output rather than globally. Each workspace starts at
  `nnwm.opt.layout.master_ratio`. `nnwm.master_ratio_grow()` and
  `nnwm.master_ratio_shrink()` adjust only the focused output's active workspace.
  `nnwm.current_workspace().master_ratio` reflects the per-workspace value.
- **Tabbed layout variants**: Two new configuration options control the
  appearance and position of the tab bar in tabbed layout. `tab_style` (`"normal"`
  or `"minimal"`) determines whether window titles are rendered in the tabs —
  `"minimal"` shows colored strips only. `tab_position` (`"top"`, `"bottom"`,
  `"left"`, `"right"`) controls which edge the tab bar occupies. For left/right
  bars the tab bar is rendered vertically and window titles are rotated 90°.
  Corner radii on both the tab bar and window content adapt automatically so
  rounded corners only appear on the exposed edges. Example:
  ```lua
  nnwm.opt.layout.tabbed.tab_style    = "minimal"
  nnwm.opt.layout.tabbed.tab_position = "bottom"
  ```
- **Scratchpad workspace**: A global overlay workspace that can hold multiple
  windows with full tiling support. `nnwm.move_to_scratchpad()` sends the
  focused window to the scratchpad. `nnwm.scratchpad_toggle()` shows or hides
  the scratchpad on the focused output with a semi-transparent dim background.
  When visible, scratchpad windows tile like a regular workspace (HTILE by
  default). Layout toggle functions (`toggle_vertical_tile`, `layout_next`,
  `layout_prev`) redirect to the scratchpad layout while the overlay is open.
  The scratchpad is global — shared across all outputs and workspaces. Closing
  the last scratchpad window automatically hides the overlay. Example:
  ```lua
  nnwm.key({"super", "s"}, nnwm.scratchpad_toggle)
  nnwm.key({"super", "shift", "s"}, nnwm.move_to_scratchpad)
  ```
- **Fractional scale and viewporter support**: `wp_viewporter` and
  `wp_fractional_scale_v1` are now advertised as Wayland globals. Clients such
  as Firefox use these protocols to discover the output scale factor and render
  at the correct buffer resolution. Without them clients fall back to 1× and the
  compositor upscales the result, producing blurry windows on scaled displays.
- **Touchpad gesture bindings**: `nnwm.gesture(fingers, direction, callback)`
  registers a swipe gesture binding. When a touchpad swipe ends without being
  cancelled the dominant axis and sign of the accumulated displacement determine
  direction (`"up"`, `"down"`, `"left"`, `"right"`), and the callback is called
  if fingers and direction match. Pinch and hold events are forwarded to clients
  via `pointer-gestures-unstable-v1` so applications (e.g. browsers) retain
  native pinch-to-zoom. Example:
  ```lua
  nnwm.gesture(3, "left",  function() nnwm.focus_prev() end)
  nnwm.gesture(3, "right", function() nnwm.focus_next() end)
  nnwm.gesture(4, "up",    function() nnwm.toggle_fullscreen() end)
  ```
- **Urgent window highlighting**: when a client requests activation for a
  background window (via `xdg-activation-v1`), that window's titlebar and tab
  bar tab are immediately redrawn in the configurable urgent colors
  (`nnwm.opt.titlebar.urgent_bg_color`, default dark red; `urgent_text_color`,
  default white). Urgency is cleared the moment the window receives keyboard
  focus.
- **Scroll layout focus tracking**: focusing a window in scroll layout (via
  keybinding, click, or focus-follows-mouse) automatically scrolls the viewport
  to center it, matching the behavior of niri and similar scrolling compositors.
- **Vertical master-stack layout (VTILE)**: `nnwm.layout.vtile.toggle()` switches
  the active workspace between horizontal and vertical master-stack orientation.
  In VTILE mode the master window occupies the full width at the top and the stack
  windows are arranged horizontally below it. `master_ratio` controls the vertical
  split between master and stack (default `0.6`). `nnwm.layout.next()` and
  `nnwm.layout.prev()` also cycle through VTILE in the full layout sequence:
  htile → vtile → tabbed → hscroll → vscroll.
- **Vertical scroll layout (VSCROLL)**: `nnwm.layout.vscroll.toggle()` switches
  the active workspace into a vertical scrolling strip. Windows are stacked
  top-to-bottom, each occupying the full output width and a configurable fraction
  of the output height (`nnwm.opt.layout.scroll_row_height`, default `0.5`). The
  viewport automatically centers the focused window vertically. Borders, titlebars,
  inner and outer gaps all apply per-row. Focusing a window by any means (keybinding,
  click, focus-follows-mouse) scrolls it into view.
- **`nnwm.toggle_maximize()`**: toggles the focused window between its normal tiled
  slot and a maximized state that fills the full usable area of the output (respecting
  panels, gaps, borders, and titlebars). Unlike fullscreen, the window is not
  hidden behind the layer shell and the border/titlebar remain visible. The maximized
  window is raised above all other tiled windows. Calling the action again restores
  the previous layout.
- **Scroll layout**: `nnwm.layout.hscroll.toggle()` switches the active workspace
  between master-stack and a horizontal scrolling layout. Windows are arranged
  in a left-to-right strip, each occupying a configurable fraction of the
  output width (`nnwm.opt.layout.scroll_column_width`, default `0.5`). The
  viewport automatically centers the focused window. Windows off-screen remain
  alive and are scrolled into view when focused. Borders, titlebars, inner and
  outer gaps all apply per-column.
- **Tabbed layout**: `nnwm.layout.tabbed.toggle()` switches the active workspace
  between master-stack and tabbed mode. In tabbed mode all tiled windows occupy
  the same content area; a composite tab bar rendered at the top of the output
  shows each window's title. Clicking a tab or using normal focus actions
  switches the visible window. The tab bar is hidden in tiling mode.
- **Layout cycling**: `nnwm.layout.next()` and `nnwm.layout.prev()` cycle
  through all available layouts for the active workspace, wrapping around.
  Current order: tile → tabbed.
- **Sticky windows**: `nnwm.toggle_sticky()` pins the focused window to all
  workspaces. A sticky tiled window participates in the active workspace's
  layout; a sticky floating window stays visible above all workspaces.
  `nnwm.rule()` also supports `sticky = true` as a window rule action.
- **Window rules**: `nnwm.rule(match, action)` applies actions to windows when
  they first map. `match` fields (`app_id`, `title`) are fnmatch globs matched
  with AND logic. `action` fields: `floating`, `fullscreen`, `sticky`
  (booleans), `workspace` (1–9 integer), `monitor` (output name string). All
  rules are evaluated in order and all matching rules are applied.
- **ext-workspace-v1**: implements the `ext-workspace-v1` staging Wayland
  protocol, creating one workspace group per connected output with 9 workspaces
  each. Allows workspace-aware bars such as waybar's `ext/workspaces` module to
  display and switch per-monitor workspaces.
- **Per-monitor independent workspaces**: each output maintains its own set of
  9 workspaces. Switching workspaces on one monitor does not affect others.
  Moving a window to another monitor assigns it to that monitor's active workspace.
- **Floating window focus cycling**: `nnwm.focus_next_float()` and
  `nnwm.focus_prev_float()` cycle keyboard focus through floating windows.
  `nnwm.focus_mode_toggle()` jumps focus between the tiled and floating layers.
- **`nnwm.swap_master()`**: swaps the focused window with the master window,
  preserving both positions. No-op if the focused window is already master.
- **Server-side titlebars**: configurable via `nnwm.opt.titlebar`. Fields:
  `enabled`, `height`, `font` (Pango description), `text_align` (0=left,
  1=center, 2=right), `bg_color`, `focused_bg_color`, `text_color`,
  `focused_text_color`. Disabled by default.
- **Client decoration control**: `nnwm.opt.client_decorations = true` requests
  clients to draw their own titlebars (CSD); `false` suppresses client
  decoration (default: `false`).
- **Color hex strings**: `nnwm.color` values now accept `"RRGGBB"`,
  `"RRGGBBAA"`, `"#RRGGBB"`, or `"#RRGGBBAA"` strings in addition to
  `{r, g, b, a}` float tables.
- **Sloppy focus improvement**: `focus_follows_mouse` now uses enter/leave
  surface events rather than polling cursor motion, making focus transfers more
  reliable and eliminating spurious refocuses during window rearrangement.
- **Titlebar hidden in tabbed layout**: per-window titlebars are suppressed when
  the workspace is in tabbed mode. The shared tab bar already provides window
  titles; showing individual titlebars was redundant and wasted vertical space.
- **Titlebar hidden in fullscreen**: entering fullscreen now hides the titlebar
  and removes its vertical offset, giving the window the full output area.
  Exiting fullscreen restores the titlebar through the normal layout path.
- **scenefx rounded corners**: when `nnwm.opt.fx.corner_radius` is set, window
  borders are drawn with correct rounded corners via a single full-window
  `border_bg` rect with `corner_radius r`. The four border strips are inset by
  `r` from each corner so they never override the rounding. The window surface
  receives a concentric inner radius (`corner_radius − border_width`, floored at
  0) so content clips cleanly inside the frame; the area revealed behind the
  clipped inner corners shows the `border_bg` color rather than the desktop,
  giving a consistent border width all the way around each corner.
- **scenefx window opacity**: `nnwm.opt.fx.opacity` (default `1.0`) sets the
  composited opacity of all window content. The value is applied recursively to
  every surface buffer in the xdg surface tree (main surface and subsurfaces).
  Borders, titlebars, and shadows are unaffected and remain fully opaque.
- **scenefx background blur**: `nnwm.opt.fx.blur` enables background blur behind
  windows. Global parameters: `enabled`, `passes` (dual-kawase iterations),
  `radius`, `noise`, `brightness`, `contrast`, `saturation`. A `wlr_scene_blur`
  node is created per window and sized/repositioned alongside the window in every
  layout pass. Corner radius is applied to the blur node to match the window frame.
- **Per-window opacity and blur rules**: `nnwm.rule()` action table now accepts
  `opacity` (float 0.0–1.0) and `blur` (boolean) to override the global
  `nnwm.opt.fx` values for individual windows. Per-window values take precedence
  and are applied at map time before scenefx decorations are created. Example:
  `nnwm.rule({ app_id = "foot*" }, { opacity = 0.85, blur = true })`.
- **Animations**: smooth transitions driven by ease-out cubic easing
  (`1 − (1−t)³`), configurable via `nnwm.opt.animations`:
  - **Window open/close**: new windows fade in from 0 opacity and scale up from
    95 % of their final size (grow-from-center). Closed windows fade out to 0
    opacity; the window is held in a "dying" list until the fade completes and
    then destroyed.
  - **Layout transitions**: any geometry change (tiling rearrangement,
    master-ratio adjustment, gap change) smoothly tweens each window's position
    and size from its previous location to the new one.
  - **Workspace switch**: switching workspaces slides the old workspace's windows
    off-screen in the direction of travel while the new workspace's windows slide
    in from the opposite edge.
  - **Focus border crossfade**: the focused and unfocused border colors blend
    smoothly when keyboard focus moves between windows.
  - All animations share a single duration (`duration`, default `250 ms`). Set
    `enabled = false` to disable all animations instantly.
  - Configured under `nnwm.opt.fx.animations`. All animation code is compiled
    only when `USE_SCENEFX=ON`; without scenefx all paths fall back to instant
    layout/color changes.
- **Animation styles and easing curves**: each animation type is independently
  configurable via sub-tables of `nnwm.opt.fx.animations`:
  - **Easing curves** (`easing` field, global or per-type):
    `"ease_out"` (default, cubic), `"ease_in"`, `"ease_in_out"`, `"linear"`,
    `"bounce"` (bounce-out), `"elastic"` (elastic-out).
  - **Open style** (`open.style`): `"fade_scale"` (default — grow from center
    + fade in), `"fade"`, `"scale"`, `"slide_up"`, `"slide_down"`,
    `"slide_left"`, `"slide_right"`, `"none"`.
  - **Close style** (`close.style`): same options as open; default `"fade"`.
  - **Workspace switch style** (`workspace.style`): `"slide"` (default),
    `"fade"`, `"none"`.
  - **Layout transition style** (`layout.style`): `"tween"` (default), `"none"`.
  - **Focus border style** (`focus.style`): `"crossfade"` (default), `"none"`.
  - Each sub-table also accepts `easing` and `duration` to override the global
    defaults for that animation type only.
  - **Per-window overrides via window rules**: `nnwm.rule()` action table now
    accepts `anim_open` (string, open style), `anim_close` (string, close style),
    and `no_anim` (boolean) to override or disable animations for specific
    windows. Example:
    `nnwm.rule({ app_id = "rofi" }, { anim_open = "fade", anim_close = "fade" })`.
- **Per-corner radius in tabbed and titlebar modes**: when `fx.rounding.radius`
  is set, corner rounding is now applied selectively per element rather than
  uniformly:
  - In **tabbed layout** the tab bar receives `corner_radii_top(r)` (top corners
    only), and tiled windows receive `corner_radii_bottom(r)` on the `border_bg`
    and inner content clips — giving a cohesive rounded-rectangle appearance where
    the tab bar and window frame share the same arc.
  - With **titlebars enabled** the titlebar buffer receives `corner_radii_top(r)`
    so its bottom edge meets the content area squarely, while the window surface
    clips get `corner_radii_bottom(inner_r)` — the `border_bg` outer rect stays
    fully rounded and the titlebar's top corners align with it.
  - The tab bar radius also respects `fx.rounding.smart`: with a single tiled
    window the radius collapses to 0 in step with the window corners.
- **Smart corner rounding state desync**: when `fx.rounding.smart = true`, the
  effective radius is 0 with one tiled window and the configured value with
  multiple. Previously `update_borders` always read the raw configured radius
  for the strip-inset calculation, while `apply_fx_decorations` applied the
  smart/fullscreen logic separately — and `apply_fx_decorations` was only
  called for the focused window, leaving all other windows' `border_bg` and
  content clip radii stale whenever ws_count changed. Fixed by introducing
  `effective_corner_radius()` (respects smart and fullscreen) used by both
  `update_borders` and `apply_fx_decorations`, and moving all radius-setting
  (`border_bg`, titlebar, content inner_r) into `update_borders` so every
  window is correctly updated on each layout pass, not just the focused one.
- **Floating window dragged to another monitor tiles on wrong monitor**: dragging
  a floating window across monitors and then tiling it (`toggle_float`) placed it
  in the tiled layout of the original monitor instead of the one it was dropped
  on. `process_cursor_move` now updates `toplevel->output` and
  `toplevel->workspace` whenever the cursor crosses into a different output during
  a drag, so subsequent tiling always targets the correct monitor.
- **Float→tile animation**: toggling a floating window to tiled mode ran a
  geometry tween from the floating position to the tile slot, which was
  disorienting. The tween is now cancelled immediately after `arrange_windows` in
  the float→tile path and the window snaps directly to its tiled position.
- **Crash when calling swap/cycle with a floating window focused**:
  `nnwm.swap.left/right/next/prev`, `nnwm.swap.master`, and `nnwm.cycle` all
  dereferenced the result of `ws_first` / `ws_next` / `ws_prev` without checking
  for null. These helpers only consider tiled windows, so a null return was
  possible whenever the focused window was floating (or no tiled windows existed).
  All six functions now return early if the focused toplevel is floating.
- **Layer-shell popup crash**: clicking waybar modules (e.g. wifi, network)
  that open XDG popups no longer crashes the compositor. Previously
  `server_new_xdg_popup` asserted that the popup parent was an XDG surface;
  layer-shell clients such as waybar use a layer surface as the parent instead.
  Now guards against a null parent, then falls back to
  `wlr_layer_surface_v1_try_from_wlr_surface` and uses the layer surface's
  scene tree as the parent, with a safe no-op if neither lookup succeeds.
- **Move to monitor with same workspace index**: moving a window to a monitor
  that happened to be on the same workspace number (e.g. both on workspace 1)
  was silently ignored due to an `old_ws == new_ws` early return, leaving the
  window's `output` pointer pointing at the source monitor. Subsequent tiling or
  `toggle_float` would then tile the window back on the original monitor. The
  guard is now `dst == src`, which correctly handles the same-index case.
- **SIGINT/SIGTERM crash when running nested**: pressing Ctrl+C on the console
  while nnwm ran inside Sway (or any nested session) terminated the process
  abruptly inside `epoll_wait` with no cleanup. Added `wl_event_loop_add_signal`
  handlers for `SIGINT` and `SIGTERM` that call `wl_display_terminate()`,
  allowing the event loop to exit cleanly and the normal shutdown path to run.
- **Layer-shell popup not appearing (null parent)**: waybar's wifi module creates
  an XDG popup with a null parent (screen-space positioner, allowed in newer
  xdg-shell versions). The null parent caused a crash inside
  `wlr_xdg_surface_try_from_wlr_surface`, and after guarding against null the
  popup was silently discarded. Null-parent popups are now placed under an
  intermediate scene tree offset to the focused output's global origin, so the
  positioner's output-local coordinates map correctly to global screen
  coordinates. Output constraining uses the focused output. All popups also now
  call `wlr_xdg_popup_unconstrain_from_box` before scheduling their initial
  configure, preventing popups from being clipped or pushed off-screen.
- **Null-parent popup wrong position on non-primary monitors**: null-parent
  popups (e.g. waybar wifi module menu) appeared correctly on the primary monitor
  but at the wrong position on other monitors. The intermediate scene tree was
  previously placed at global origin, so output-local positioner coordinates were
  interpreted as primary-monitor coordinates. The scene tree is now positioned at
  the focused output's global offset in the layout, fixing popup placement on all
  monitors.
- **Cursor warps to monitor center on monitor focus change**:
  `nnwm.focus_monitor_next()` and `nnwm.focus_monitor_prev()` now warp the
  cursor to the center of the newly focused monitor so the pointer and keyboard
  focus stay in sync.
- **Stale focus when switching to an empty monitor**: moving keyboard focus to a
  monitor with no windows left the previously focused window on the old monitor
  still holding keyboard focus. Both focus-monitor actions now call
  `wlr_seat_keyboard_clear_focus` when no window is found on the target monitor,
  and the previously focused window's border is also unfocused via
  `unfocus_all_borders`.
- **Config error overlay**: when a config hot-reload fails (syntax error or
  runtime error in `init.lua`), a red error bar is rendered at the top of every
  connected output showing the error message in white text — similar to i3/sway's
  error bar. The bar auto-dismisses after 8 seconds. If the config is fixed and
  successfully reloaded, the bar is hidden immediately.
- **scenefx crash on DRM (SIGSEGV in scene_output_handle_commit)**: nnwm
  crashed immediately on startup from a TTY when built with `USE_SCENEFX=ON`.
  The scenefx `main` branch requires wlroots ≥ 0.20, but nnwm was previously
  selecting wlroots 0.19 when both versions are installed. scenefx was compiled
  against the 0.20 ABI while nnwm linked the 0.19 runtime, causing a struct
  layout mismatch that manifested as a SIGSEGV on the first DRM page-flip
  callback. Fixed by making `USE_SCENEFX=ON` unconditionally select wlroots 0.20.
  Additionally, wlroots 0.20's `wlr/render/color.h` uses C99 `[static N]` static
  array syntax which is invalid C++; the header is now pre-included with `static`
  suppressed (via `pragma push_macro`) before any transitive inclusion fires.
- **scenefx rounded corners not clipping window content**: with `corner_radius > 0`
  the window surface itself still showed square corners. `wlr_scene_xdg_surface_create`
  nests the actual surface buffer inside an intermediate subsurface tree; the previous
  code only iterated direct children, so `wlr_scene_buffer_set_corner_radius` was
  never called on the real buffer. Fixed by replacing the one-level loop with a
  recursive traversal that walks the full subtree and applies the corner radius to
  every buffer node, including those in nested subsurface trees.
- **scenefx shadow not visible**: `shadow_offset_x` and `shadow_offset_y` were
  read from config but never applied to the shadow node's position, leaving the
  shadow directly behind the window with no visible offset. Fixed by calling
  `wlr_scene_node_set_position` on the shadow node at creation and on every
  `update_borders` call (which runs whenever the window is laid out or resized).
- **scenefx crash when running nested (SIGABRT in wlr_scene_output_build_state)**:
  when running inside another Wayland compositor (e.g. for testing), scenefx's
  `fx_renderer_create` succeeded even on the Wayland backend because a GPU render
  node is available, but the renderer was incompatible with the Wayland backend's
  buffer pipeline, triggering an assertion. The fix checks `WAYLAND_DISPLAY` and
  `DISPLAY` at startup: if either is set, the standard `wlr_renderer_autocreate`
  is used instead of the FX renderer, so scenefx effects are only active on real
  DRM hardware.
- **Monitor flickering**: on multi-monitor setups one output could flicker
  continuously. The frame handler was calling `wlr_scene_output_send_frame_done`
  unconditionally even when `wlr_scene_output_commit` returned false (nothing to
  render / no damage). Spurious frame-done callbacks caused clients to submit new
  buffers prematurely, creating a rendering feedback loop. The frame handler now
  returns early when commit fails, so `send_frame_done` is only called when a
  frame was actually presented.
- **Tabbed layout floating window transparency**: floating windows in tabbed
  mode no longer appear transparent or blank. The tab bar is now raised to the
  top after floating windows, so floating windows remain visible above it.
- **Natural scroll not applying to mouse**: `touchpad.natural_scroll` was
  mistakenly applied to all pointer devices including mice. The setting now
  applies only to touchpad devices (devices with `TAP_FINGER_COUNT > 0`).
- **`focus_follows_mouse` config parsing**: the config field was not read
  correctly from the Lua table, leaving the option permanently at its default.
- **Rofi focus**: launching rofi and selecting a window now correctly transfers
  keyboard focus to the chosen window. The compositor now handles the
  `set_focus` request from the activation protocol properly.
- **ext-session-lock-v1**: screen locking support. Clients such as `swaylock`
  and `waylock` can acquire a session lock, covering every output with a lock
  surface that receives all input. Compositor keybindings, window focus, and
  Super+drag are suppressed while locked. The compositor sends `locked` once
  every output has a mapped lock surface. If the lock client crashes the screen
  remains locked rather than silently unlocking. VT switching still works while
  locked.
- **wlr-output-power-management-unstable-v1**: DPMS control for tools such as
  `swayidle` and `wlopm`. Clients can turn individual outputs off or on; the
  compositor applies the change immediately via an output state commit and
  updates the output manager state so `wlr-randr` reflects the new enabled
  status.
- **wlr-screencopy-unstable-v1**: frame capture protocol used by `grim` and
  similar screenshot tools. Backed entirely by wlroots — no custom frame
  handling required.
- **wlr-export-dmabuf-unstable-v1**: DMA-BUF based output capture for
  hardware-accelerated screen recording tools such as `wf-recorder`.
- **ext-image-copy-capture-v1** and **ext-image-capture-source-v1**: modern
  replacements for screencopy, used by `wl-screenrec` and future recording
  clients. Generated from wayland-protocols staging XMLs.
- **wlr-output-management-unstable-v1**: runtime output configuration via
  `wlr-randr`, `kanshi`, and similar tools. Clients can query available outputs,
  test configurations, and apply changes (mode, scale, transform, position,
  enable/disable). The compositor advertises the current state and updates it on
  config hot-reload.
- **xdg-output-unstable-v1**: exposes logical output geometry (position, size)
  to clients. Required by many Wayland-native tools to correctly interpret
  output layouts.
- **Floating windows**: `nnwm.toggle_float()` toggles the focused window
  between floating and tiled mode. When made floating the window is centered on
  screen and raised above all tiled windows. Super+left-click drags a floating
  window; Super+right-click resizes it from the bottom-right corner. Tiling a
  floating window re-enters the master-stack layout automatically. Floating
  windows are always rendered above tiled windows.
- **Fullscreen**: `nnwm.toggle_fullscreen()` makes the focused window cover the
  entire output with no borders or gaps. Exits fullscreen by calling the action
  again, or via the client's own request (e.g. F11 in a browser). Both floating
  and fullscreen windows are excluded from the tiling layout.
- **Focus follows mouse**: `nnwm.focus_follows_mouse = true` transfers keyboard
  focus to whichever window the cursor moves over (default: `false`). Has no
  effect during move/resize drags.
- **XKB options**: `nnwm.xkb_options` accepts a comma-separated XKB options
  string (e.g. `"caps:escape,compose:ralt"`). Applied to all keyboards on
  startup and re-applied on config hot-reload (default: `""`).
- **9-workspace support**: `nnwm.switch_workspace(n)` and
  `nnwm.move_to_workspace(n)` (1–9). Each workspace maintains independent
  tiling and remembers the last focused window.
- **Master ratio keybindings**: `nnwm.master_ratio_grow()` and
  `nnwm.master_ratio_shrink()` adjust the master column width at runtime by
  `nnwm.master_ratio_step`, clamped to `[master_ratio_min, master_ratio_max]`.
- **New window placement**: `nnwm.new_window_master = false` appends new
  windows to the end of the stack instead of promoting them to master
  (default: `true`).
- **Layer-shell exclusive zones (struts)**: tiling layout now respects panels
  that declare an exclusive zone via `zwlr-layer-shell-v1`. The usable area is
  recomputed across all layer surfaces in layer order whenever a panel maps,
  commits, or closes, and `arrange_windows` tiles only within that area.
- **Autostart fix**: `nnwm.spawn()` calls made during config loading are now
  deferred until after the Wayland socket is open and `WAYLAND_DISPLAY` is set,
  so wallpaper managers and bars start correctly on first launch.
- **`nnwm.spawn_once(cmd)`**: spawns a command only once per compositor session.
  Subsequent calls with the same string (e.g. after a config hot-reload) are
  silently ignored. Intended for autostart programs such as bars and wallpaper
  managers that must not have duplicate instances.
- **Multi-monitor support**: each connected output gets an independent workspace.
  The cursor determines the focused output; keyboard actions (focus, swap,
  workspace switching, new windows) operate on the output under the cursor.
  Switching to a workspace already visible on another output swaps the two
  outputs' workspaces instead of hiding windows.
- **Monitor focus and window migration**: `nnwm.focus_monitor_next()` and
  `nnwm.focus_monitor_prev()` shift keyboard focus between outputs, restoring
  the last focused window on the target monitor. `nnwm.move_to_monitor_next()`
  and `nnwm.move_to_monitor_prev()` send the focused window to the adjacent
  output's active workspace; focus follows the window to the destination.
- **Output monitor configuration**: `nnwm.monitors` is an array-of-tables for
  configuring outputs by `name` (connector, e.g. `"DP-1"`) or `description`
  (`"make model serial"` combined string; serial is `"Unknown"` when absent).
  First match wins. Each entry may specify `width`/`height`/`refresh` (mode),
  `x`/`y` (layout position), `scale`, `transform` (rotation string),
  `hdr` (wlroots 0.20+), and `disabled`. Unmatched outputs fall back to their
  preferred mode and auto-layout.
- **`nnwm.host_name()`**: returns the machine hostname as a string. Useful for
  per-host config in a shared init file.
- **Gaps support**: `nnwm.outer_gap` (space between windows and screen edge)
  and `nnwm.inner_gap` (space between adjacent windows) config fields. Both
  default to `0`. Applied in the tiling layout for all window arrangements.
- **Smart gaps**: `nnwm.smart_gaps = true` collapses gaps to zero when only
  one window is on screen (default: `false`).
- **Smart borders**: `nnwm.smart_borders = true` collapses border width to
  zero when only one window is on screen (default: `false`).
- **Lua config with hot-reload**: Lua 5.4 configuration loaded from
  `~/.config/nnwm/init.lua` (or `-c` path). Edits are picked up automatically
  via inotify without restarting the compositor.
- **`nnwm.key()` API**: Keybindings defined as Lua callbacks —
  `nnwm.key({"Super", "h"}, function() nnwm.focus_left() end)`. The combo
  table mixes modifier names (`Super`, `Shift`, `Ctrl`, `Alt`, `Mod2`–`Mod5`,
  `Caps`) and a single XKB key name.
- **Action functions**: `nnwm.quit()`, `nnwm.close()`, `nnwm.spawn(cmd)`,
  `nnwm.focus_left/right/next/prev()`, `nnwm.swap_left/right/next/prev()`,
  `nnwm.cycle()` — callable from keybinding callbacks.
- **Tiling master-stack layout**: first window is master (left column), rest
  are stacked in the right column. Master ratio is configurable.
- **Window borders**: per-window border rects using `wlr_scene_rect`. Focused
  and unfocused colors are configurable.
- **Layer shell support**: background, bottom, top, and overlay layers for
  panels, wallpapers, etc.
- **XDG decoration protocol**: advertises server-side decoration to clients.
- **Cursor theming**: configurable theme and size via `nnwm.cursor_theme` /
  `nnwm.cursor_size`.
- **Input configuration**: libinput touchpad options (tap-to-click, natural
  scroll, disable-while-typing) configurable from Lua.
- **Startup command**: `-s` flag to run a command after the backend starts.
- **Wayland socket**: auto-named socket, `WAYLAND_DISPLAY` set automatically.
- `DISPLAY` is unset so toolkits don't fall back to X11.

### Bug Fixes

- **Floating window invisible borders**: toggling a tiled window to floating no
  longer leaves oversized border rects extending past the window content.
  `toggle_float` now sends a `set_size(0, 0)` configure so the client can
  settle at its preferred dimensions, and the commit handler resizes the border
  rects to match the geometry the client actually commits.
- **Monitor config hot-reload**: editing `nnwm.monitors` in the config file and
  saving now immediately applies mode, scale, transform, position, and
  enable/disable changes to live outputs. Previously these settings were only
  applied at output detection during startup.
- **Scale change rendering**: changing output scale on the fly now correctly
  recomputes the usable area and re-tiles windows. Previously the stale logical
  resolution left empty space after a scale change.
- **Focus on window close**: when the focused window is closed, focus transfers
  to the previously focused window (the one focused before it). Falls back to
  the current workspace master if no history is available.
- **Focus tracking**: `get_focused_toplevel` was always returning the master
  window (first in the tiling list) instead of the actually keyboard-focused
  window. Now resolves the focused toplevel from `seat->keyboard_state.focused_surface`.
  Fixes `nnwm.close()`, `nnwm.focus_right/next/prev()`, and all `nnwm.swap_*()` actions
  when called after a focus change.
- **VT switching**: Ctrl+Alt+F<n> now switches to the corresponding TTY.
  The compositor intercepts `XF86Switch_VT_1..12` keysyms and calls
  `wlr_session_change_vt`; no-op when running nested (session is null).
- Fixed `nnwm_lua_init`: missing `lua_newtable` before `luaL_setfuncs` was
  causing a crash on startup.
- Fixed `push_config_defaults`: was replacing the `nnwm` global table with a
  new empty one on every config load, discarding all registered action
  functions (`nnwm.key`, `nnwm.quit`, etc.). Now updates the existing table
  in place so functions survive config loads and reloads.
- Fixed window destroy: scene tree is now properly destroyed when an XDG
  toplevel is unmapped, preventing stale scene nodes.
- Focus changes update border colors for all windows (focused vs unfocused).
- Config reload re-applies border colors, tiling layout, and keyboard repeat
  settings.
- **Titlebar update on resize**: titlebar is now re-rendered when a window is
  resized via mouse drag, and the resize height calculation correctly subtracts
  the titlebar height (matching `arrange_windows` behavior).
- **VT switch resume**: returning to nnwm from another VT (e.g. sway on a
  different TTY) now correctly re-tiles all outputs. Previously the session
  resume was not handled, leaving window positions stale and crashing on
  interaction with existing windows.
- **Floating window border occlusion during resize**: borders at the growing
  edge of a floating window are no longer hidden by the client surface.
  Border rects are now created above the surface in the scene Z-order, and
  the commit handler re-syncs border sizes to the client's actually committed
  geometry after each frame.
- **`swap_next`/`swap_prev` wrap-around**: swapping the last window forward now
  promotes it to master (not second position), and swapping the master backward
  now demotes it to last (not second-to-last). Wrap-around direction matches
  `focus_next`/`focus_prev`.
- **Lua config crash when `fx.rounding` is absent**: if `nnwm.opt.fx.rounding`
  was not set in `init.lua`, the config parser left a nil value on the Lua stack
  (the result of `lua_getfield(L, -1, "rounding")`) because `lua_pop` was inside
  the `if (lua_istable)` block and was skipped. The subsequent `lua_getfield` for
  `"shadow"` then operated on nil rather than the `fx` table, triggering a Lua
  panic. Fixed by moving `lua_pop` outside the if block so it runs unconditionally
  regardless of whether the field exists.
- **libscenefx shared object not found after install**: running the installed
  `nnwm` binary failed with `libscenefx-0.5.so: cannot open shared object file`
  because the binary's RPATH still pointed to the build-tree location of the
  library. Fixed by setting `BUILD_RPATH` to the scenefx build directory (so the
  development binary still works without installing) and `INSTALL_RPATH` to the
  system library directory (`${CMAKE_INSTALL_FULL_LIBDIR}`). The scenefx `.so` is
  now also installed alongside nnwm by `install(FILES ...)`, so a plain
  `cmake --install` produces a self-contained installation.
- **Popup submenu appears at wrong position**: submenus (popup-of-popup, e.g.
  the waybar wifi module's right-click submenu) were placed at the left edge of
  the monitor instead of next to the parent menu entry. The root cause was that
  `wlr_xdg_popup_unconstrain_from_box` expects the constraint box in the root
  surface's coordinate space (the toplevel or layer surface that anchors the
  entire popup chain), but the constraint was computed relative to the immediate
  parent popup's scene tree. For sub-popups this produced a badly offset
  constraint box that caused the flip/slide algorithm to push the submenu to the
  output's left edge. Fixed by introducing a `root_tree` field on `nnwm_popup`
  (set by walking up the xdg popup ancestor chain to the owning toplevel or layer
  surface) and using `root_tree` for coordinate calculation in
  `wlr_xdg_popup_unconstrain_from_box`. The scene parenting (`parent_tree`)
  is unchanged.
