<p align="center">
<img src="./resources/logo/banner.png" alt="nnwm logo" />
</p>

**NNWM (No Name Window Manager)** is a tiling Wayland compositor built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) with Lua configuration support.

Latest Version: 0.1.0

## Why ?

Because [**MangoWM**](https://github.com/mangowm/mango) community downvoted my lua configuration support [request](https://github.com/mangowm/mango/issues/1072), I decided to make my own tiling Wayland compositor with Lua configuration and hot-reload support.

P.S : MangoWM is awesome, and I love it, but I want to have my own compositor with Lua configuration support.

## Features

- Master-stack, tabbed, and scrolling layouts with per-workspace master ratio
- 9 independent workspaces per monitor
- Multi-monitor support with per-output layout and per-output struts
- Floating, fullscreen, fake-fullscreen, maximized, and sticky windows
- Window rules (`nnwm.rule`) — match by app_id/title, set workspace, opacity, effects, and more
- Lua configuration with hot-reload
- Keybindings as Lua callbacks with key repeat
- Event hooks (`nnwm.on`) — react to window focus/open/close, workspace switches, monitor connect, startup, and shutdown
- Timers (`nnwm.timer`, `nnwm.interval`) — schedule callbacks from the compositor event loop
- Introspection API (`nnwm.current_window/workspace/output`) — query live compositor state from Lua
- Server-side titlebars and configurable borders (HiDPI-aware)
- Inner/outer gaps with smart gaps
- Layer shell (panels, wallpapers, overlays)
- Per-monitor output configuration (mode, scale, transform, position, struts)
- Screen capture, DPMS, XDG desktop portal (`XDG_CURRENT_DESKTOP=sway` exported automatically)
- libinput touchpad and mouse configuration
- XKB keyboard configuration

## Dependencies

- wlroots 0.19 (or 0.20 when building with sceneFX)
- wayland-server
- xkbcommon
- libinput
- pixman
- cairo + pango (titlebars and error overlay)
- Lua 5.4

## Building

```sh
cmake -B build
cmake --build build
```

> To enable sceneFX effects and animations, see the [sceneFX section](#scenefx-optional-experimental) below.

## Installation

```sh
sudo cmake --install build
```

This installs the `nnwm` binary to `/usr/local/bin/` and the `nnwm.desktop` session file to `/usr/local/share/wayland-sessions/`, making nnwm appear in the session list of login managers (GDM, SDDM, LightDM, etc.).

To install under `/usr` instead:

```sh
sudo cmake --install build --prefix /usr
```

## Usage

```sh
./build/nnwm          # auto-selects backend (DRM/KMS when run from TTY)
./build/nnwm -c ~/.config/nnwm/init.lua
./build/nnwm -s "waybar &"   # run a command after startup
```

## Configuration

Configuration lives at `~/.config/nnwm/init.lua` (or pass `-c <path>`).

Example:

```lua
nnwm.opt = {
    gaps    = { inner = 10, outer = 10 },

    keyboard = {
        xkb_layout  = "us",
        xkb_variant = "intl",
        xkb_options = "caps:escape",
        repeat_rate  = 25,
        repeat_delay = 300,
    },

    touchpad = {
        tap_to_click  = true,
        natural_scroll = true,
        scroll_factor  = 1.0,
    },

    mouse = {
        natural_scroll          = false,
        warp_to_focused_window  = true,
        hide_cursor_when_typing = true,
    },

}

nnwm.monitor({ name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 })
nnwm.monitor({
    description = "HP Inc. HP P24h G5 3CM5031JJC",
    x = 1536, y = 0, width = 1920, height = 1080,
    struts = { top = 32 },   -- reserve space for a panel
})

nnwm.key({ "Super", "Return" }, function() nnwm.spawn("kitty") end)
nnwm.key({ "Super", "q" },      function() nnwm.close() end)
nnwm.key({ "Super", "Space" },  function() nnwm.layout.next() end)

for i = 1, 9 do
    nnwm.key({ "Super", tostring(i) },          function() nnwm.switch_workspace(i) end)
    nnwm.key({ "Super", "Shift", tostring(i) }, function() nnwm.move_to_workspace(i) end)
end

nnwm.spawn_once("waybar")
nnwm.spawn_once("swaybg -i ~/wallpaper.png -m fill")
```

### Layouts

```lua
nnwm.opt.layout = {
    master_ratio = 0.55,

    tabbed = {
        tab_style    = "normal",   -- "normal" | "minimal"
        tab_position = "top",      -- "top" | "bottom" | "left" | "right"
        height       = 28,         -- tab bar thickness in pixels
    },

    scroll_column_width = 0.5,     -- hscroll: fraction of output width per column
    scroll_row_height   = 0.5,     -- vscroll: fraction of output height per row
    new_window_master   = false,   -- true = new window becomes master
}
```

### Monitor configuration

Call `nnwm.monitor({ ... })` once per output that needs non-default settings. Rules are matched in call order; the first match wins. Unmatched outputs use their preferred mode and auto-layout.

```lua
nnwm.monitor({ name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 })
nnwm.monitor({
    description = "HP Inc. HP P24h G5 3CM5031JJC",
    x = 1536, y = 0, width = 1920, height = 1080,
    struts = { top = 32 },   -- reserve space for a panel
})
nnwm.monitor({ name = "HDMI-A-2", disabled = true })
```

Match by `name` (connector name, e.g. `"eDP-1"`) or `description` (EDID substring). To find the exact values for your outputs, run nnwm once and check the log:

```sh
WLR_LOG_LEVEL=info nnwm 2>/tmp/nnwm.log
grep "new output" /tmp/nnwm.log
```

### Window rules

Match windows by `app_id` or `title` (both support glob patterns) and apply actions:

```lua
nnwm.rule(
    { app_id = "firefox" },
    {
        workspace = 2,
        focused   = true,   -- switch to workspace and focus on spawn
    }
)

nnwm.rule(
    { app_id = "mpv" },
    {
        floating    = true,
        fake_fullscreen = true,
    }
)

-- sceneFX per-window overrides
nnwm.rule(
    { app_id = "kitty" },
    {
        opacity           = 0.95,
        focused_opacity   = 1.0,
        unfocused_opacity = 0.8,
        corner_radius     = 12,
        shadow            = true,
        blur              = true,
    }
)
```

Available actions:

| Field | Type | Description |
|---|---|---|
| `floating` | boolean | Float or tile the window |
| `fullscreen` | boolean | Start fullscreen |
| `fake_fullscreen` | boolean | Fill usable area without a true fullscreen |
| `maximize` | boolean | Maximize within tiled layout |
| `focused` | boolean | Switch to workspace and focus on spawn |
| `sticky` | boolean | Appear on all workspaces |
| `workspace` | integer | Assign to workspace 1–9 |
| `monitor` | string | Assign to output by name |
| `opacity` | number | Base opacity override (0.0–1.0) |
| `focused_opacity` | number | Opacity when focused (sceneFX) |
| `unfocused_opacity` | number | Opacity when unfocused (sceneFX) |
| `blur` | boolean | Background blur override (sceneFX) |
| `shadow` | boolean | Drop shadow override (sceneFX) |
| `corner_radius` | integer | Corner radius in pixels (sceneFX) |
| `anim_open` | string | Open animation style (sceneFX) |
| `anim_close` | string | Close animation style (sceneFX) |
| `no_anim` | boolean | Disable animations for this window (sceneFX) |

### Event hooks

React to compositor events with Lua callbacks:

```lua
-- Fire once the compositor is ready
nnwm.on("startup", function()
    nnwm.spawn_once("dunst")
end)

-- Print the title of every newly focused window
nnwm.on("window_focus", function(win)
    print("focused:", win.title, "workspace:", win.workspace)
end)

-- Notify on workspace switch
nnwm.on("workspace_switch", function(ws)
    nnwm.spawn("notify-send 'Workspace " .. ws.index .. "'")
end)
```

Available events: `startup`, `shutdown`, `window_focus`, `window_open`, `window_close`, `workspace_switch`, `output_connect`. See [LUA_WIKI.md](LUA_WIKI.md) for full documentation.

### Timers

```lua
-- One-shot: notify 500 ms after startup
nnwm.timer(500, function()
    nnwm.spawn("notify-send 'nnwm is running'")
end)

-- Repeating: rotate wallpaper every 10 minutes
nnwm.interval(600000, function()
    nnwm.spawn("swaybg -i $(shuf -n1 ~/wallpapers)")
end)
```

## sceneFX (Optional, Experimental)

nnwm optionally integrates with [sceneFX](https://github.com/wlrfx/scenefx), a drop-in wlroots scene-graph replacement that adds GPU-accelerated visual effects. This is **entirely optional** — nnwm builds and runs without it out of the box.

> **Experimental:** sceneFX support is still experimental. It requires wlroots 0.20 and may not be stable on all hardware or driver combinations.

### Building with sceneFX

```sh
cmake -B build -DUSE_SCENEFX=ON
cmake --build build
```

### Effects

When built with `-DUSE_SCENEFX=ON`, the following extras become available via `nnwm.opt.fx`:

- **Rounded corners** — clips window borders and surfaces with configurable radii
- **Drop shadows** — configurable color, blur sigma, and offset
- **Background blur** — dual-kawase blur with noise, brightness, contrast, and saturation controls
- **Per-window opacity** — base, focused, and unfocused opacity
- **Per-window overrides** — all fx settings are overridable per window via `nnwm.rule()`

```lua
nnwm.opt.fx = {
    corner_radius = 10,
    opacity           = 1.0,
    focused_opacity   = 1.0,
    unfocused_opacity = 0.85,

    shadow = {
        enabled    = true,
        blur_sigma = 20,
        offset_x   = 4,
        offset_y   = 4,
        color      = { 0, 0, 0, 0.5 },
    },

    blur = {
        enabled    = true,
        passes     = 3,
        radius     = 5,
        noise      = 0.0,
        brightness = 1.0,
        contrast   = 1.0,
        saturation = 1.0,
    },
}
```

### Animations (sceneFX only)

- **Window open/close** — `"fade_scale"` (default), `"fade"`, `"scale"`, `"slide_up/down/left/right"`, `"none"`
- **Layout transitions** — smooth position and size tweening
- **Workspace switch** — slide or fade between workspaces
- **Focus border crossfade** — border color blends on focus change
- **Easing curves** — `"ease_out"` (default), `"ease_in"`, `"ease_in_out"`, `"linear"`, `"bounce"`, `"elastic"`
- Per-window overrides via `nnwm.rule()`: `anim_open`, `anim_close`, `no_anim`

```lua
nnwm.opt.fx.animations = {
    enabled  = true,
    duration = 250,       -- ms
    easing   = "ease_out",
    open      = { style = "fade_scale" },
    close     = { style = "fade" },
    workspace = { style = "slide" },
}
```

## Links

- [CHANGELOG](./CHANGELOG.md)
- [TODO](./TODO.md)
