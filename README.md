# nnwm

**No Name Window Manager (nnwm)** is a lightweight, minimalistic tiling Wayland compositor built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots).

Latest Version: 0.1.0

## Why ?

Because [**MangoWM**](https://github.com/mangowm/mango) community downvoted my lua configuration support [request](https://github.com/mangowm/mango/issues/1072), I decided to make my own tiling Wayland compositor with Lua configuration and hot-reload support.

P.S : MangoWM is awesome, and I love it, but I want to have my own compositor with Lua configuration support.

## Features

- Master-stack, tabbed, and scrolling layouts
- 9 independent workspaces per monitor
- Multi-monitor support with per-output layout
- Floating, fullscreen, and sticky windows
- Window rules (`nnwm.rule`) — match by app_id/title, set workspace, float, opacity, etc.
- Lua configuration with hot-reload
- Keybindings as Lua callbacks
- Server-side titlebars and configurable borders
- Inner/outer gaps with smart gaps
- Layer shell (panels, wallpapers, overlays)
- Per-monitor output configuration (mode, scale, transform, position)
- Screen capture, DPMS, libinput touchpad, XKB

## sceneFX (Optional, Experimental)

nnwm optionally integrates with [sceneFX](https://github.com/wlrfx/scenefx), a drop-in wlroots scene-graph replacement that adds GPU-accelerated visual effects. This is **entirely optional** — nnwm builds and runs without it out of the box.

> **Experimental:** sceneFX support is still experimental. It requires wlroots 0.20 and may not be stable on all hardware or driver combinations.

When built with `-DUSE_SCENEFX=ON`, the following extras become available via `nnwm.opt.fx`:

- **Rounded corners** — `corner_radius` clips window borders and surface content with concentric radii
- **Drop shadows** — configurable color, blur sigma, and offset
- **Background blur** — dual-kawase blur behind windows with noise, brightness, contrast, and saturation controls
- **Per-window opacity** — composited opacity for window content
- **Per-window overrides** — `nnwm.rule()` accepts `opacity` and `blur` to override global fx settings per window

### Animations (sceneFX only)

All animations are also compiled in only when `USE_SCENEFX=ON`:

- **Window open/close** — configurable style: `"fade_scale"` (default), `"fade"`, `"scale"`, `"slide_up/down/left/right"`, `"none"`
- **Layout transitions** — smooth position and size tweening when windows are rearranged
- **Workspace switch** — slide or fade between workspaces
- **Focus border crossfade** — border color blends smoothly on focus change
- **Easing curves** — `"ease_out"` (default), `"ease_in"`, `"ease_in_out"`, `"linear"`, `"bounce"`, `"elastic"`
- Each animation type has independent `style`, `easing`, and `duration` overrides
- Per-window animation overrides via `nnwm.rule()`: `anim_open`, `anim_close`, `no_anim`

```lua
nnwm.opt.fx = {
    corner_radius = 10,
    shadow = { enabled = true, blur_sigma = 20, color = "#00000088" },
    blur   = { enabled = true, passes = 3, radius = 5 },
    opacity = 0.95,
}

nnwm.opt.animations = {
    enabled  = true,
    duration = 250,         -- ms
    easing   = "ease_out",
    open      = { style = "fade_scale" },
    close     = { style = "fade" },
    workspace = { style = "slide" },
}
```

### Building with sceneFX

```sh
cmake -B build -DUSE_SCENEFX=ON
cmake --build build
```

## Configuration

Configuration lives at `~/.config/nnwm/init.lua` (or pass `-c <path>`).

Example:

```lua
nnwm.master_ratio = 0.55

nnwm.opt = {
    gaps    = { inner = 10, outer = 10 },
    keyboard = { xkb_options = "caps:escape" },
    mouse   = { focus_follows_mouse = true },

    monitors = {
        { name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 },
        { description = "HP Inc. HP P24h G5 3CM5031JJC", x = 1536, y = 0, width = 1920, height = 1080 },
    },
}

nnwm.key({ "Super", "Return" }, function() nnwm.spawn("kitty") end)
nnwm.key({ "Super", "q" },      function() nnwm.close() end)
nnwm.key({ "Super", "Space" },  function() nnwm.layout.next() end)

for i = 1, 9 do
    nnwm.key({ "Super", tostring(i) },         function() nnwm.switch_workspace(i) end)
    nnwm.key({ "Super", "Shift", tostring(i) }, function() nnwm.move_to_workspace(i) end)
end

nnwm.spawn_once("waybar")
nnwm.spawn_once("swaybg -i ~/wallpaper.png -m fill")
```

### Monitor matching

Monitors are matched by `name` (connector name, e.g. `"eDP-1"`, `"DP-1"`) or `description` (combined `"make model serial"` string — serial is `"Unknown"` when absent). To find the exact values for your outputs, run nnwm once with logging and check for `new output:` lines:

```sh
WLR_LOG_LEVEL=info nnwm 2>/tmp/nnwm.log
grep "new output" /tmp/nnwm.log
```

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

> To enable sceneFX effects and animations, see the [sceneFX section](#scenefx-optional-experimental) above.

## Usage

```sh
./build/nnwm          # auto-selects backend (DRM/KMS when run from TTY)
./build/nnwm -c ~/.config/nnwm/init.lua
./build/nnwm -s "waybar &"   # run a command after startup
```

## Links

- [CHANGELOG](./CHANGELOG.md)
- [TODO](./TODO.md)
