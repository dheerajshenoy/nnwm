# nnwm

**no name Window Manager (nnWM)** is a lightweight, minimalistic tiling Wayland compositor built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots).

Latest Version: 0.1.0

## Features

- **Tiling layout** — master-stack with configurable master ratio
- **Tabbed layout** — all tiled windows share the same area, switched via a tab bar
- **Layout cycling** — `nnwm.layout.next()` / `nnwm.layout.prev()` cycles layouts per workspace
- **Floating windows** — toggle with `nnwm.toggle_float()`; drag with Super+left-click, resize with Super+right-click
- **Fullscreen** — `nnwm.toggle_fullscreen()`
- **Sticky windows** — `nnwm.toggle_sticky()` pins a window across all workspaces
- **9 workspaces per monitor** — independent per output, switch with `nnwm.switch_workspace(n)`
- **Multi-monitor support** — focus monitors with `nnwm.focus_monitor_next/prev()`, move windows with `nnwm.move_to_monitor_next/prev()`
- **Window rules** — `nnwm.rule(match, action)` applies floating/fullscreen/sticky/workspace/monitor actions on map
- **Server-side titlebars** — optional, fully configurable (font, colors, alignment)
- **Window borders** — configurable focused/unfocused colors
- **Gaps** — inner and outer gaps with smart gaps/borders support
- **Lua configuration** with hot-reload via inotify
- **Keybindings** defined as Lua callbacks via `nnwm.key()`
- **Focus follows mouse** — optional sloppy focus
- **Layer shell** — background, bottom, top, overlay layers (panels, wallpapers, etc.)
- **XDG popups** — correct placement on all monitors including null-parent popups (e.g. waybar modules)
- **Output configuration** — per-monitor mode, scale, transform, position, enable/disable via `nnwm.opt.monitors`
- **ext-workspace-v1** — workspace-aware bars (waybar `ext/workspaces`)
- **Session lock** — `ext-session-lock-v1` (swaylock, waylock)
- **Screen capture** — `wlr-screencopy`, `wlr-export-dmabuf`, `ext-image-copy-capture-v1`
- **DPMS** — `wlr-output-power-management-v1` (swayidle, wlopm)
- **Input** — libinput touchpad options (tap, natural scroll, disable-while-typing), XKB options
- **Config error overlay** — displays a red error bar on all outputs when config hot-reload fails, auto-dismisses after 8 seconds

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

- wlroots 0.19
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

## Usage

```sh
./build/nnwm          # auto-selects backend (DRM/KMS when run from TTY)
./build/nnwm -c ~/.config/nnwm/init.lua
./build/nnwm -s "waybar &"   # run a command after startup
```

## Links

- [CHANGELOG](./CHANGELOG.md)
- [TODO](./TODO.md)
