# nnwm CHANGELOG

## 0.1

### Features

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

### Bug Fixes

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
- **`swap_next`/`swap_prev` wrap-around**: swapping the last window forward now
  promotes it to master (not second position), and swapping the master backward
  now demotes it to last (not second-to-last). Wrap-around direction matches
  `focus_next`/`focus_prev`.
