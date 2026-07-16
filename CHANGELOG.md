# nnwm CHANGELOG

## 0.1

### Features

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
- **Scroll layout**: `nnwm.layout.scroll.toggle()` switches the active workspace
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

### Bug Fixes

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
