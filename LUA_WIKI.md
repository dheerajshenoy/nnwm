# nnwm Lua API

nnwm uses Lua 5.4 for its configuration. The config file is loaded from
`~/.config/nnwm/init.lua` (or a path specified with `-c`). Edits are picked
up automatically at runtime via inotify — no restart required.

If a reload fails (syntax error or runtime error), a red error bar is shown on
all outputs with the error message. It auto-dismisses after 8 seconds, or
immediately when a successful reload occurs.

---

## Keybinding API

### `nnwm.key(combo, callback)`

Register a keybinding. `combo` is a table of modifier and key name strings;
`callback` is a Lua function called when the combo is pressed.

```lua
nnwm.key({"Super", "Shift", "c"}, function() nnwm.quit() end)
nnwm.key({"Super", "Return"},     function() nnwm.spawn("kitty") end)
nnwm.key({"Super", "p"},          function() nnwm.spawn("rofi -show drun") end)
```

Modifier names: `Super`, `Shift`, `Ctrl`, `Alt`, `Mod2`, `Mod3`, `Mod5`, `Caps`.
All other strings are treated as XKB key names (e.g. `"Return"`, `"space"`,
`"F1"`, `"apostrophe"`). Exactly one key name is required per combo.

Multiple `nnwm.key()` calls with the same combo are allowed; the last one wins.

---

## Action Functions

### Window management

| Function                     | Description                                              |
|------------------------------|----------------------------------------------------------|
| `nnwm.quit()`                | Terminate the compositor                                 |
| `nnwm.close()`               | Close the focused window                                 |
| `nnwm.spawn(cmd)`            | Run `cmd` via `/bin/sh -c`                               |
| `nnwm.spawn_once(cmd)`       | Like `spawn`, but only once per compositor session       |
| `nnwm.toggle_float()`        | Toggle the focused window between tiled and floating     |
| `nnwm.toggle_fullscreen()`   | Toggle fullscreen for the focused window                 |
| `nnwm.toggle_sticky()`       | Pin the focused window to all workspaces                 |

### Focus

| Function                       | Description                                            |
|--------------------------------|--------------------------------------------------------|
| `nnwm.focus_left()`            | Focus the master window                                |
| `nnwm.focus_right()`           | Focus the first stack window                           |
| `nnwm.focus_next()`            | Focus the next tiled window (wraps)                    |
| `nnwm.focus_prev()`            | Focus the previous tiled window (wraps)                |
| `nnwm.focus_next_float()`      | Focus the next floating window                         |
| `nnwm.focus_prev_float()`      | Focus the previous floating window                     |
| `nnwm.focus_mode_toggle()`     | Jump focus between the tiled and floating layers       |
| `nnwm.focus_dir(dir)`          | Focus nearest window in `"left"`/`"right"`/`"up"`/`"down"` direction, crossing monitors |
| `nnwm.focus_monitor_next()`    | Move keyboard focus to the next monitor                |
| `nnwm.focus_monitor_prev()`    | Move keyboard focus to the previous monitor            |

### Swap / reorder

| Function               | Description                                               |
|------------------------|-----------------------------------------------------------|
| `nnwm.swap_left()`     | Move the focused window to master position                |
| `nnwm.swap_right()`    | Move the focused window to first stack position           |
| `nnwm.swap_next()`     | Swap the focused window with the next                     |
| `nnwm.swap_prev()`     | Swap the focused window with the previous                 |
| `nnwm.swap_master()`   | Swap the focused window with the master window            |
| `nnwm.cycle()`         | Rotate the tiling list (master moves to end of stack)     |

### Master ratio

| Function                      | Description                                           |
|-------------------------------|-------------------------------------------------------|
| `nnwm.master_ratio_grow()`    | Increase master area by `master_ratio_step`           |
| `nnwm.master_ratio_shrink()`  | Decrease master area by `master_ratio_step`           |

### Workspaces

| Function                         | Description                                        |
|----------------------------------|----------------------------------------------------|
| `nnwm.switch_workspace(n)`       | Switch to workspace `n` (1–9) on the active output |
| `nnwm.move_to_workspace(n)`      | Move the focused window to workspace `n`           |
| `nnwm.move_to_monitor_next()`    | Move the focused window to the next monitor        |
| `nnwm.move_to_monitor_prev()`    | Move the focused window to the previous monitor    |

### Layout

| Function                  | Description                                                |
|---------------------------|------------------------------------------------------------|
| `nnwm.layout.next()`      | Cycle to the next layout for the active workspace          |
| `nnwm.layout.prev()`      | Cycle to the previous layout for the active workspace      |
| `nnwm.layout.tabbed.toggle()` | Toggle tabbed mode for the active workspace            |

Available layouts (in cycle order): `tile` → `tabbed`.

### Utility

| Function            | Description                              |
|---------------------|------------------------------------------|
| `nnwm.host_name()` | Return the machine hostname as a string  |

---

## Introspection

These functions return live snapshots of compositor state. All fields are copied
at call time; the tables are plain Lua values with no connection back to the
compositor.

### `nnwm.current_window()`

Returns a snapshot of the currently focused window, or `nil` if no window has
keyboard focus.

| Field           | Type    | Description                                      |
|-----------------|---------|--------------------------------------------------|
| `title`         | string  | Window title (empty string if unset)             |
| `app_id`        | string  | Application ID (empty string if unset)           |
| `floating`      | boolean | Whether the window is floating                   |
| `fullscreen`    | boolean | Whether the window is fullscreen                 |
| `fake_fullscreen` | boolean | Whether fake-fullscreen is active               |
| `maximized`     | boolean | Whether the window is maximized                  |
| `sticky`        | boolean | Whether the window is sticky (all workspaces)    |
| `workspace`     | integer | 1-based workspace index                          |
| `x`, `y`        | integer | Global compositor coordinates                    |
| `width`, `height` | integer | Window dimensions including border             |
| `output`        | string  | Name of the output the window is on, or `nil`    |

```lua
local win = nnwm.current_window()
if win then
    print(win.title, win.app_id, win.workspace)
end
```

### `nnwm.current_workspace()`

Returns a snapshot of the active workspace on the focused output, or `nil`.

| Field          | Type    | Description                                       |
|----------------|---------|---------------------------------------------------|
| `index`        | integer | 1-based workspace index                           |
| `layout`       | string  | Layout name: `"htile"`, `"vtile"`, `"tabbed"`, `"hscroll"`, `"vscroll"` |
| `master_ratio` | number  | Current master split ratio for this workspace     |
| `window_count` | integer | Number of tiled windows on this workspace         |
| `output`       | string  | Name of the output                                |

### `nnwm.current_output()`

Returns a snapshot of the currently focused output, or `nil`.

| Field              | Type    | Description                                   |
|--------------------|---------|-----------------------------------------------|
| `name`             | string  | Connector name, e.g. `"eDP-1"`               |
| `description`      | string  | EDID description string                       |
| `width`, `height`  | integer | Mode resolution in pixels                     |
| `scale`            | number  | Output scale factor                           |
| `x`, `y`           | integer | Global layout position                        |
| `active_workspace` | integer | 1-based active workspace index                |

---

## Event Hooks

### `nnwm.on(event, callback)`

Register a Lua callback for a compositor event. Multiple callbacks can be
registered for the same event; all are called in registration order. Registering
hooks at module load time (outside any callback) is safe — they persist across
config hot-reloads only for the lifetime of the compositor process.

```lua
nnwm.on("window_focus", function(win)
    -- win is an nnwm.Window snapshot table
    print("focused:", win.title)
end)

nnwm.on("workspace_switch", function(ws)
    -- ws is an nnwm.Workspace snapshot table
    print("switched to workspace", ws.index)
end)

nnwm.on("startup", function()
    -- no argument; compositor is fully running
    nnwm.spawn("dunst")
end)
```

### Events

| Event name         | Argument type    | When it fires                                                 |
|--------------------|------------------|---------------------------------------------------------------|
| `"startup"`        | *(none)*         | First event-loop tick — after autostart, after `WAYLAND_DISPLAY` is set |
| `"shutdown"`       | *(none)*         | Compositor is about to exit, before clients are destroyed     |
| `"window_focus"`   | `nnwm.Window`    | A window receives keyboard focus                              |
| `"window_open"`    | `nnwm.Window`    | A window is mapped (first appears on screen)                  |
| `"window_close"`   | `nnwm.Window`    | A window unmaps (closes or hides)                             |
| `"workspace_switch"` | `nnwm.Workspace` | The active workspace changes on any output                  |
| `"output_connect"` | `nnwm.Output`    | A new monitor is connected                                    |

The `nnwm.Window`, `nnwm.Workspace`, and `nnwm.Output` tables passed to
callbacks have the same fields as `nnwm.current_window()`,
`nnwm.current_workspace()`, and `nnwm.current_output()` respectively — they are
plain snapshot tables with no live connection.

---

## Timers

### `nnwm.timer(ms, callback)`

Run `callback` once after `ms` milliseconds. The timer is driven by the Wayland
event loop and fires reliably on the compositor thread — no shell scripts or
external processes required.

```lua
nnwm.timer(500, function()
    nnwm.spawn("notify-send 'nnwm started'")
end)
```

### `nnwm.interval(ms, callback)`

Run `callback` every `ms` milliseconds, repeating indefinitely until the
compositor exits.

```lua
-- Check battery every 60 seconds
nnwm.interval(60000, function()
    nnwm.spawn("~/.local/bin/battery-notify.sh")
end)

-- Rotate wallpaper every 10 minutes
nnwm.interval(600000, function()
    nnwm.spawn("swaybg -i $(shuf -n1 ~/wallpapers)")
end)
```

---

## Configuration

All settings live inside `nnwm.opt = { ... }` (preferred) or directly on the
`nnwm` table for legacy fields.

### `nnwm.opt` structure

```lua
nnwm.opt = {
    gaps = {
        inner = 10,   -- pixels between adjacent windows
        outer = 10,   -- pixels between windows and screen edge
    },

    keyboard = {
        xkb_options    = "caps:escape",  -- XKB options string
        repeat_rate    = 25,             -- keys/sec
        repeat_delay   = 600,            -- ms before repeat starts
    },

    mouse = {
        focus_follows_mouse = false,     -- sloppy focus
    },

    border = {
        width          = 2,
        focused_color  = "#4C7FBF",
        unfocused_color = "#262626",
    },

    titlebar = {
        enabled          = false,
        height           = 20,
        font             = "Sans 10",
        text_align       = 1,            -- 0=left, 1=center, 2=right
        bg_color         = "#333333",
        focused_bg_color = "#3D5A8A",
        text_color       = "#FFFFFF",
        focused_text_color = "#FFFFFF",
    },

    client_decorations = false,          -- request client-side decorations

    cursor = {
        theme = "default",
        size  = 24,
    },

    fx = {
        rounding = {
            radius = 0,     -- corner radius in pixels; 0 = disabled
            smart  = false, -- collapse rounding when only one window is visible
        },
        opacity           = 1.0,  -- base window opacity (0.0–1.0)
        focused_opacity   = -1,   -- <0 = inherit opacity; 0.0–1.0 = override
        unfocused_opacity = -1,   -- <0 = inherit opacity; 0.0–1.0 = override
    },
}

-- Monitor configuration is done with nnwm.monitor() calls (see below)
```

Colors accept `{r, g, b, a}` float tables or hex strings: `"RRGGBB"`,
`"#RRGGBB"`, `"RRGGBBAA"`, `"#RRGGBBAA"`.

### Layout fields (top-level)

| Field                | Type    | Default | Description                              |
|----------------------|---------|---------|------------------------------------------|
| `master_ratio`       | number  | `0.55`  | Fraction of screen width for master area |
| `master_ratio_step`  | number  | `0.05`  | Step for `master_ratio_grow/shrink`      |
| `master_ratio_min`   | number  | `0.1`   | Lower clamp for master ratio             |
| `master_ratio_max`   | number  | `0.9`   | Upper clamp for master ratio             |
| `new_window_master`  | bool    | `true`  | New windows become master; `false` = append to stack |
| `smart_gaps`         | bool    | `false` | Collapse gaps when only one window is visible |
| `smart_borders`      | bool    | `false` | Collapse borders when only one window is visible |

---

## Monitor Configuration

### `nnwm.monitor(config)`

Configure an output. Call once for each monitor that needs non-default settings.
Rules are evaluated in call order; the first matching rule wins. Unmatched
outputs use their preferred mode and auto-layout position.

Match by `name` (exact connector name, e.g. `"eDP-1"`) or `description` (EDID
substring). To find the exact values for your hardware:

```sh
WLR_LOG_LEVEL=info nnwm 2>/tmp/nnwm.log
grep "new output" /tmp/nnwm.log
# new output: name='eDP-1' make='AU Optronics' model='0xE3AC' serial=''
# → description = "AU Optronics 0xE3AC Unknown"
```

### Fields

| Field       | Type    | Description                                                                            |
|-------------|---------|----------------------------------------------------------------------------------------|
| `name`      | string  | Connector name, e.g. `"eDP-1"`, `"DP-1"`, `"HDMI-A-1"`                               |
| `description` | string | EDID description substring to match                                                  |
| `width`     | integer | Mode width in pixels                                                                   |
| `height`    | integer | Mode height in pixels                                                                  |
| `refresh`   | integer | Refresh rate in Hz; `0` = use preferred                                                |
| `x`         | integer | Layout X position (unset = auto-arrange)                                               |
| `y`         | integer | Layout Y position (unset = auto-arrange)                                               |
| `scale`     | number  | Output scale factor (e.g. `1.25` for HiDPI)                                           |
| `transform` | string  | Rotation: `"none"`, `"90"`, `"180"`, `"270"`, `"flipped"`, `"flipped-90"`, `"flipped-180"`, `"flipped-270"` |
| `hdr`       | bool    | Enable HDR — wlroots 0.20+                                                             |
| `disabled`  | bool    | Disable this output entirely                                                            |
| `struts`    | table   | Reserve screen edges: `{ top=N, bottom=N, left=N, right=N }` (pixels)                 |

### Example

```lua
nnwm.monitor({ name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 })

nnwm.monitor({
    description = "HP Inc. HP P24h G5 3CM5031JJC",
    x = 1536, y = 0,
    width = 1920, height = 1080,
    scale = 1.0,
    struts = { top = 32 },   -- reserve space for a panel
})

nnwm.monitor({
    description = "HP Inc. HP 524pu 1H35321M53",
    x = 3456, y = 0,
    width = 1920, height = 1080,
    scale = 1.0,
})

-- Disable an unused output
nnwm.monitor({ name = "HDMI-A-2", disabled = true })
```

---

## Window Rules

### `nnwm.rule(match, action)`

Apply actions to windows when they first map. All `match` fields are tested
with AND logic; all matching rules are applied in order.

**`match` fields:**

| Field    | Type   | Description                                |
|----------|--------|--------------------------------------------|
| `app_id` | string | fnmatch glob matched against the app-id    |
| `title`  | string | fnmatch glob matched against the title     |

**`action` fields:**

| Field        | Type    | Description                                         |
|--------------|---------|-----------------------------------------------------|
| `floating`   | bool    | Make the window floating                            |
| `fullscreen` | bool    | Make the window fullscreen                          |
| `sticky`     | bool    | Pin the window to all workspaces                    |
| `workspace`  | integer | Assign to workspace 1–9                             |
| `monitor`    | string  | Assign to the output with this name (e.g. `"DP-1"`) |

```lua
nnwm.rule({ app_id = "mpv" },      { floating = true })
nnwm.rule({ app_id = "pavucontrol" }, { floating = true, workspace = 9 })
nnwm.rule({ title = "Picture-in-Picture" }, { floating = true, sticky = true })
```

---

## Full Example Config

```lua
nnwm.master_ratio      = 0.55
nnwm.master_ratio_step = 0.05
nnwm.new_window_master = false
nnwm.smart_gaps        = true
nnwm.smart_borders     = true

nnwm.opt = {
    gaps     = { inner = 10, outer = 10 },
    keyboard = { xkb_options = "caps:escape" },
    mouse    = { focus_follows_mouse = true },

    border = {
        width           = 2,
        focused_color   = "#4C7FBF",
        unfocused_color = "#262626",
    },

    titlebar = {
        enabled          = true,
        height           = 22,
        font             = "Sans Bold 10",
        text_align       = 1,
        bg_color         = "#333333",
        focused_bg_color = "#3D5A8A",
    },

    client_decorations = false,

    fx = {
        rounding          = { radius = 6, smart = true },
        opacity           = 1.0,
        focused_opacity   = 1.0,
        unfocused_opacity = 0.8,
    },

}

nnwm.monitor({ name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 })
nnwm.monitor({ description = "HP Inc. HP P24h G5 3CM5031JJC", x = 1536, y = 0, width = 1920, height = 1080 })

local mod = "Super"

nnwm.key({ mod, "Shift", "c" }, function() nnwm.quit() end)
nnwm.key({ mod, "Shift", "q" }, function() nnwm.close() end)
nnwm.key({ mod, "Return" },     function() nnwm.swap_master() end)
nnwm.key({ mod, "Shift", "Return" }, function() nnwm.spawn("kitty") end)
nnwm.key({ mod, "p" },          function() nnwm.spawn("rofi -show drun") end)
nnwm.key({ mod, "t" },          function() nnwm.toggle_float() end)
nnwm.key({ mod, "f" },          function() nnwm.toggle_fullscreen() end)
nnwm.key({ mod, "Space" },      function() nnwm.layout.next() end)

nnwm.key({ mod, "h" }, function() nnwm.master_ratio_shrink() end)
nnwm.key({ mod, "l" }, function() nnwm.master_ratio_grow() end)
nnwm.key({ mod, "j" }, function() nnwm.focus_next() end)
nnwm.key({ mod, "k" }, function() nnwm.focus_prev() end)

nnwm.key({ mod, "Shift", "h" }, function() nnwm.swap_left() end)
nnwm.key({ mod, "Shift", "l" }, function() nnwm.swap_right() end)
nnwm.key({ mod, "Shift", "j" }, function() nnwm.swap_next() end)
nnwm.key({ mod, "Shift", "k" }, function() nnwm.swap_prev() end)

nnwm.key({ mod, "Semicolon" },  function() nnwm.focus_monitor_prev() end)
nnwm.key({ mod, "Apostrophe" }, function() nnwm.focus_monitor_next() end)

for i = 1, 9 do
    nnwm.key({ mod, tostring(i) },         function() nnwm.switch_workspace(i) end)
    nnwm.key({ mod, "Shift", tostring(i) }, function() nnwm.move_to_workspace(i) end)
end

nnwm.rule({ app_id = "mpv" }, { floating = true })

nnwm.spawn_once("waybar")
nnwm.spawn_once("swaybg -i ~/wallpaper.png -m fill")
nnwm.spawn_once("dunst")
```
