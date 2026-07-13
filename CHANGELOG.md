# nnwm CHANGELOG

## Unreleased

### Features

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
  configuring outputs by name, make, model, or serial (first match wins). Each
  entry may specify `width`/`height`/`refresh` (mode), `x`/`y` (layout
  position), `scale`, `transform` (rotation string), `hdr` (wlroots 0.20+),
  and `disabled`. Unmatched outputs fall back to their preferred mode and
  auto-layout.
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
