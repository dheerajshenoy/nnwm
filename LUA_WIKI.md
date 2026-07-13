# nnwm Lua API

nnwm uses Lua 5.4 for its configuration. The config file is loaded from
`~/.config/nnwm/init.lua` (or a path specified with `-c`).

## Global Tables

### `nnwm`

The main configuration table. All settings and keybindings are assigned to it.

### `MOD`

Modifier key constants for use with `nnwm.key()`:

| Name    | Value              |
|---------|--------------------|
| `Shift` | `WLR_MODIFIER_SHIFT` |
| `Caps`  | `WLR_MODIFIER_CAPS`  |
| `Ctrl`  | `WLR_MODIFIER_CTRL`  |
| `Alt`   | `WLR_MODIFIER_ALT`   |
| `Mod2`  | `WLR_MODIFIER_MOD2`  |
| `Mod3`  | `WLR_MODIFIER_MOD3`  |
| `Super` | `WLR_MODIFIER_LOGO`  |
| `Mod5`  | `WLR_MODIFIER_MOD5`  |

### `KEY`

Key name constants. Includes `a`-`z`, `F1`-`F12`, and special keys:

| Name        | XKB name     |
|-------------|--------------|
| `Return`    | `Return`     |
| `Space`     | `space`      |
| `Tab`       | `Tab`        |
| `Escape`    | `Escape`     |
| `Backspace` | `BackSpace`  |
| `Delete`    | `Delete`     |
| `Up`        | `Up`         |
| `Down`      | `Down`       |
| `Left`      | `Left`       |
| `Right`     | `Right`      |

## Keybinding API

### `nnwm.key(combo, callback)`

Register a keybinding. `combo` is a table of modifier and key name strings.
`callback` is a function invoked when the combo is pressed.

```lua
nnwm.key({"Super", "Shift", "c"}, function() nnwm.quit() end)
nnwm.key({"Super", "h"}, function() nnwm.focus_left() end)
nnwm.key({"Super", "p"}, function() nnwm.spawn("rofi -show drun") end)
```

The combo table mixes modifier names and a single key name. Entries are
resolved as follows:

- If the string matches a modifier name (`Super`, `Shift`, `Ctrl`, `Alt`,
  `Mod2`, `Mod3`, `Mod5`, `Caps`), it is added to the modifier mask.
- Otherwise it is treated as an XKB key name (resolved via
  `xkb_keysym_from_name`). Exactly one key entry is required.

Multiple `nnwm.key()` calls may bind the same combo; the last one wins.

### Action Functions

These functions are available inside `nnwm.key()` callbacks:

| Function              | Description                                  |
|-----------------------|----------------------------------------------|
| `nnwm.quit()`        | Terminate the compositor                     |
| `nnwm.close()`       | Close the focused window                     |
| `nnwm.spawn(cmd)`    | Run `cmd` via `/bin/sh -c`                   |
| `nnwm.host_name()`   | Return the machine hostname as a string      |
| `nnwm.focus_left()`  | Focus the master window (first in list)      |
| `nnwm.focus_right()` | Focus the first stack window                 |
| `nnwm.focus_next()`  | Focus the next window (wraps)                |
| `nnwm.focus_prev()`  | Focus the previous window (wraps)            |
| `nnwm.swap_left()`   | Move the focused window to master position   |
| `nnwm.swap_right()`  | Move the focused window to first stack       |
| `nnwm.swap_next()`   | Swap the focused window with the next        |
| `nnwm.swap_prev()`   | Swap the focused window with the previous    |
| `nnwm.cycle()`       | Rotate the window list (master becomes last) |

## Configuration Fields

All fields are set directly on the `nnwm` table.

### Layout

| Field                | Type    | Default | Description                        |
|----------------------|---------|---------|------------------------------------|
| `master_ratio`       | number  | `0.55`  | Fraction of screen for master area |
| `master_ratio_step`  | number  | `0.05`  | Step for master ratio adjustment   |
| `master_ratio_min`   | number  | `0.1`   | Minimum master ratio               |
| `master_ratio_max`   | number  | `0.9`   | Maximum master ratio               |

### Borders

| Field             | Type           | Default                  | Description               |
|-------------------|----------------|--------------------------|---------------------------|
| `border_width`    | integer        | `2`                      | Border width in pixels    |
| `focused_color`   | {r,g,b,a}     | `{0.3, 0.5, 0.8, 1.0}`  | Focused border color      |
| `unfocused_color` | {r,g,b,a}     | `{0.15, 0.15, 0.15, 1.0}`| Unfocused border color   |

### Keyboard

| Field                  | Type    | Default | Description                      |
|------------------------|---------|---------|----------------------------------|
| `keyboard_repeat_rate` | integer | `25`    | Key repeat rate (keys/sec)       |
| `keyboard_repeat_delay`| integer | `600`   | Delay before repeat starts (ms)  |

### Cursor

| Field          | Type    | Default     | Description            |
|----------------|---------|-------------|------------------------|
| `cursor_theme` | string  | `"default"` | Xcursor theme name     |
| `cursor_size`  | integer | `24`        | Cursor size in pixels  |

### Seat

| Field      | Type   | Default    | Description       |
|------------|--------|------------|-------------------|
| `seat_name`| string | `"seat0"`  | Wayland seat name |

### Input (libinput)

| Field                        | Type | Default | Description                    |
|------------------------------|------|---------|--------------------------------|
| `touchpad_tap_to_click`      | bool | `true`  | Enable tap-to-click            |
| `touchpad_natural_scroll`    | bool | `true`  | Natural (reverse) scroll       |
| `touchpad_disable_while_typing`| bool | `true`| Disable touchpad while typing  |

### Titlebar

| Field                        | Type           | Default                     | Description                                |
|------------------------------|----------------|-----------------------------|--------------------------------------------|
| `titlebar_height`            | integer        | `0`                         | Titlebar height in pixels; 0 = disabled    |
| `titlebar_font`              | string         | `"Sans 10"`                 | Pango font description                     |
| `titlebar_text_align`        | integer        | `1`                         | 0 = left, 1 = center, 2 = right           |
| `titlebar_bg_color`          | {r,g,b,a}     | `{0.2, 0.2, 0.2, 1.0}`     | Background color (unfocused windows)       |
| `titlebar_focused_bg_color`  | {r,g,b,a}     | `{0.25, 0.35, 0.55, 1.0}`  | Background color (focused window)          |
| `titlebar_text_color`        | {r,g,b,a}     | `{1.0, 1.0, 1.0, 1.0}`     | Text color                                 |

### Monitors

Array of tables to configure outputs. Each entry is a table; all fields are
optional. Entries are matched against connected outputs by name, make, model,
and/or serial (AND logic). The first matching entry is applied; unmatched
outputs use their preferred mode and auto-layout position.

| Field        | Type              | Description                                           |
|--------------|-------------------|-------------------------------------------------------|
| `name`       | string            | Output name, e.g. `"DP-1"`                           |
| `make`       | string            | Manufacturer string                                   |
| `model`      | string            | Model string                                          |
| `serial`     | string            | Serial number string                                  |
| `width`      | integer           | Mode width in pixels                                  |
| `height`     | integer           | Mode height in pixels                                 |
| `refresh`    | integer           | Refresh rate in Hz                                    |
| `x`          | integer           | Layout X position (if unset, auto-arranged)           |
| `y`          | integer           | Layout Y position (if unset, auto-arranged)           |
| `scale`      | number            | Output scale factor (e.g. `2.0` for HiDPI)           |
| `transform`  | string            | Rotation: `"none"`, `"90"`, `"180"`, `"270"`, `"flipped"`, `"flipped-90"`, `"flipped-180"`, `"flipped-270"` |
| `hdr`        | bool              | Enable HDR (wlroots 0.20+)                            |
| `disabled`   | bool              | Disable this output                                   |

## Example Config

```lua
-- Layout
nnwm.master_ratio = 0.6
nnwm.border_width = 3
nnwm.focused_color = {0.3, 0.5, 0.8, 1.0}
nnwm.unfocused_color = {0.15, 0.15, 0.15, 1.0}

-- Cursor
nnwm.cursor_theme = "default"
nnwm.cursor_size = 24

-- Keybindings
nnwm.key({"Super", "Shift", "c"}, function() nnwm.quit() end)
nnwm.key({"Super", "Shift", "q"}, function() nnwm.close() end)
nnwm.key({"Super", "p"},         function() nnwm.spawn("rofi -show drun") end)
nnwm.key({"Super", "Return"},    function() nnwm.spawn("kitty") end)

-- Focus (master-stack)
nnwm.key({"Super", "h"}, function() nnwm.focus_left() end)
nnwm.key({"Super", "l"}, function() nnwm.focus_right() end)
nnwm.key({"Super", "j"}, function() nnwm.focus_next() end)
nnwm.key({"Super", "k"}, function() nnwm.focus_prev() end)

-- Swap
nnwm.key({"Super", "Shift", "h"}, function() nnwm.swap_left() end)
nnwm.key({"Super", "Shift", "l"}, function() nnwm.swap_right() end)
nnwm.key({"Super", "Shift", "j"}, function() nnwm.swap_next() end)
nnwm.key({"Super", "Shift", "k"}, function() nnwm.swap_prev() end)

-- Cycle
nnwm.key({"Alt", "F1"}, function() nnwm.cycle() end)
```

## Hot Reload

Editing the config file while the compositor is running triggers an automatic
reload. No restart is required. The reload clears all existing keybindings and
re-executes the config file.
