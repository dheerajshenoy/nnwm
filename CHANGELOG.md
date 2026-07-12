# nnwm CHANGELOG

## Unreleased

### Features

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
