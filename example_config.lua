-- nnwm example configuration
-- Covers every Lua API surface. Copy what you need; delete what you don't.

-- ── Helpers ──────────────────────────────────────────────────────────────────

local mod = "Super"           -- primary modifier
local hostname = nnwm.host_name()

nnwm.log.info("nnwm", nnwm.version(), "starting on", hostname)

-- ── Monitor layout ───────────────────────────────────────────────────────────
-- Match by connector name OR EDID description string.
-- First matching rule wins; unmatched outputs use their preferred mode.

if hostname == "my-desktop" then
    nnwm.monitor({ name = "DP-1",  x = 0,    y = 0, width = 2560, height = 1440, refresh = 144 })
    nnwm.monitor({ name = "HDMI-1", x = 2560, y = 0, width = 1920, height = 1080 })
    nnwm.monitor({ name = "DP-2",  disabled = true })
elseif hostname == "my-laptop" then
    nnwm.monitor({ name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 })
    nnwm.monitor({
        description = "HP Inc. HP P24h G5 3CM5031JJC",
        x = 1536, y = 0, width = 1920, height = 1080, scale = 1.0,
        struts = { top = 0, bottom = 32 },   -- reserve 32 px at bottom for an external bar
    })
end

-- ── Core options ─────────────────────────────────────────────────────────────

nnwm.opt = {

    -- Workspace labels; length also sets the workspace count (1–9).
    workspace_names = { "1", "2", "3", "4", "5", "6", "7", "8", "9" },

    -- Switch to the previous workspace when pressing the active one again.
    workspace_back_and_forth = true,

    -- Follow xdg-activation token focus requests (e.g. from app launchers).
    focus_on_activate = false,

    -- Show a red overlay when init.lua fails to parse.
    show_config_error_overlay = true,

    -- Disable clipboard sync (blocks all wl_data_device writes when false).
    clipboard = true,

    -- ── Layout ───────────────────────────────────────────────────────────────
    layout = {
        master_ratio         = 0.55,    -- master pane width fraction
        master_ratio_step    = 0.05,
        master_ratio_min     = 0.1,
        master_ratio_max     = 0.9,
        new_window_master    = false,   -- new windows go to stack, not master
        center_new_floating  = true,    -- floating windows open centered
        scroll_column_width  = 0.5,     -- hscroll: fraction of output per column
        scroll_row_height    = 0.5,     -- vscroll: fraction of output per row

        -- Subset and order for layout.next() / layout.prev().
        enabled_layouts = { "htile", "vtile", "tabbed", "float" },

        tabbed = {
            tab_position = "top",       -- "top" | "bottom" | "left" | "right"
            tab_style    = "normal",    -- "normal" | "minimal"
            smart        = true,        -- hide tab bar with only one window
            height       = 24,
        },
    },

    -- ── Gaps ─────────────────────────────────────────────────────────────────
    gaps = {
        inner = 6,    -- between windows
        outer = 6,    -- between windows and screen edge
        smart = true, -- collapse gaps when only one window is present
    },

    -- ── Borders ──────────────────────────────────────────────────────────────
    border = {
        width           = 2,
        smart           = true,  -- no border when only one window
        focused_color   = "#5294e2",
        unfocused_color = "#333333",
    },

    -- ── Keyboard ─────────────────────────────────────────────────────────────
    keyboard = {
        repeat_rate  = 30,
        repeat_delay = 250,
        xkb_layout   = "us",
        xkb_options  = "ctrl:swapcaps",
        -- xkb_variant = "dvorak",
        -- xkb_file    = "/path/to/keymap.xkb",  -- overrides all xkb_* fields
    },

    -- ── Touchpad ─────────────────────────────────────────────────────────────
    touchpad = {
        enabled                   = true,
        tap_to_click              = true,
        drag                      = true,
        natural_scroll            = true,
        disable_while_typing      = true,
        disable_on_external_mouse = false,
        scroll_factor             = 1.0,
        scroll_method             = "two_finger",
    },

    -- ── Mouse / pointer ──────────────────────────────────────────────────────
    mouse = {
        focus_follows_mouse     = true,
        cursor_theme            = "Bibata-Modern-Classic",
        cursor_size             = 24,
        accel_speed             = 0.0,
        accel_profile           = "adaptive",
        natural_scroll          = false,
        hide_cursor_when_typing = true,
        warp_to_focused_window  = false,
    },

    -- ── Server-side titlebar ─────────────────────────────────────────────────
    titlebar = {
        enabled           = false,
        height            = 24,
        font              = "Sans Bold 10",
        text_align        = 1,             -- 0=left 1=center 2=right
        bg_color          = "#1e1e2e",
        focused_bg_color  = "#313244",
        urgent_bg_color   = "#f38ba8",
        text_color        = "#cdd6f4",
        focused_text_color = "#cdd6f4",
        urgent_text_color  = "#1e1e2e",
    },

    -- ── Find-cursor animation ─────────────────────────────────────────────────
    find_cursor_style = "rings",   -- "rings" | "spotlight" | "zoom"

    -- ── Idle detection ───────────────────────────────────────────────────────
    idle_timeout = 300,   -- seconds; 0 = disabled

    -- ── Hot corners ──────────────────────────────────────────────────────────
    hot_corners = {
        size = 4,    -- corner zone in pixels
        top_left     = { action = function() nnwm.toggle_overview() end,  delay = 300 },
        top_right    = { action = function() nnwm.find_cursor() end,      delay = 200 },
        bottom_left  = { action = function() nnwm.spawn("rofi -show drun") end, delay = 400 },
        bottom_right = { action = function() nnwm.lock() end,             delay = 500 },
        monitors = {
            ["DP-1"] = {
                top_left = { action = function() nnwm.toggle_overview() end },
            },
        },
    },

    -- ── scenefx effects (requires USE_SCENEFX=ON build) ──────────────────────
    fx = {
        rounding = { radius = 10, smart = true },
        opacity           = 1.0,   -- base window opacity
        focused_opacity   = 1.0,   -- override for focused window; <0 = inherit
        unfocused_opacity = 0.92,  -- override for unfocused windows
        shadow = {
            enabled    = true,
            blur_sigma = 10.0,
            offset_x   = 4.0,
            offset_y   = 4.0,
            color      = { 0, 0, 0, 0.5 },
        },
        blur = {
            enabled    = true,
            passes     = 3,
            radius     = 5,
            noise      = 0.02,
            brightness = 1.0,
            contrast   = 1.0,
            saturation = 1.0,
        },
        animations = {
            enabled  = true,
            duration = 250,
            easing   = "ease_out",
            open      = { style = "fade_scale", easing = "ease_out" },
            close     = { style = "fade",       duration = 150 },
            layout    = { style = "tween" },
            workspace = { style = "slide" },
            focus     = { style = "crossfade" },
        },
    },

}

-- ── Status bar ───────────────────────────────────────────────────────────────

-- Register a named custom module (survives hot-reloads).
nnwm.bar.module("battery", {
    type     = "custom",
    interval = 10000,
    update   = function()
        local f = io.popen("cat /sys/class/power_supply/BAT0/capacity 2>/dev/null")
        if not f then return "" end
        local pct = f:read("*l"); f:close()
        return pct and ("BAT " .. pct .. "%") or ""
    end,
    on_click = function(button)
        if button == "left" then nnwm.spawn("gnome-power-statistics") end
    end,
})

nnwm.bar.module("cpu", {
    type     = "custom",
    interval = 3000,
    update   = function()
        local f = io.popen("grep -o '^[^ ]*' /proc/loadavg")
        if not f then return "" end
        local load = f:read("*l"); f:close()
        return load and ("CPU " .. load) or ""
    end,
})

nnwm.opt.bar = {
    enabled        = true,
    position       = "top",
    height         = 28,
    per_output     = true,    -- one bar per monitor
    font           = "monospace 11",
    padding        = 0,       -- or {top, right, bottom, left} for floating bar
    module_spacing = 8,
    smart_workspaces = false, -- hide empty workspaces in the workspaces module
    opacity        = 1.0,

    colors = {
        background = { 0.08, 0.09, 0.12, 0.95 },
        foreground = { 0.85, 0.85, 0.85, 1.0 },
    },

    modules = {
        left = {
            {
                type   = "workspaces",
                colors = {
                    active_bg     = "#5294e2",
                    active_fg     = "#ffffff",
                    occupied_fg   = "#cdd6f4",
                    unoccupied_fg = "#555577",
                },
            },
            { type = "layout", fg = "#a6e3a1" },
        },
        center = {
            { type = "window_title", fg = "#cdd6f4" },
        },
        right = {
            "battery",
            "cpu",
            { type = "tray" },
            {
                type   = "clock",
                format = "%a %b %d  %H:%M",
                fg     = "#cdd6f4",
                on_click = function(button)
                    if button == "left" then nnwm.spawn("gnome-calendar") end
                end,
            },
        },
    },

    -- scenefx bar effects (USE_SCENEFX=ON only)
    fx = {
        corner_radius = 8,
        blur          = true,
        shadow = {
            enabled    = true,
            blur_sigma = 6.0,
            offset_y   = 2.0,
            color      = { 0, 0, 0, 0.4 },
        },
    },

    on_click = function(button, x, y)
        if button == "right" then nnwm.spawn("rofi -show drun") end
    end,
}

-- ── Window rules (static) ────────────────────────────────────────────────────
-- All fields in `match` must match (AND). Supports fnmatch globs.

nnwm.rule({ app_id = "firefox" },            { workspace = 2 })
nnwm.rule({ app_id = "thunderbird" },        { workspace = 3 })
nnwm.rule({ app_id = "discord" },            { workspace = 4 })
nnwm.rule({ app_id = "steam" },              { floating = true })
nnwm.rule({ title  = "*Picture-in-Picture*" }, { floating = true, sticky = true })
nnwm.rule({ app_id = "pavucontrol" },        { floating = true })
nnwm.rule({ app_id = "mpv" },               { floating = true, monitor = "DP-1" })
nnwm.rule({ app_id = "gimp" },              { maximize = true, workspace = 5 })
nnwm.rule({ app_id = "obs" },               { fake_fullscreen = true })

-- ── Window rules (dynamic) ───────────────────────────────────────────────────
-- nnwm.add_rule returns an ID so the rule can be removed later.

local scratch_rule = nnwm.add_rule({ app_id = "foot-float" }, function(win)
    win:set_floating(true)
    win:set_workspace(1)
end)

-- Remove the rule after use (uncomment when needed):
-- nnwm.remove_rule(scratch_rule)

-- ── Scratchpad ───────────────────────────────────────────────────────────────

nnwm.key({ mod, "minus" },       function() nnwm.scratchpad_toggle() end,          "Toggle scratchpad")
nnwm.key({ mod, "Shift", "minus" }, function() nnwm.move_to_scratchpad() end,      "Send to scratchpad")

-- Named scratchpads — each name is an independent overlay.
nnwm.key({ mod, "apostrophe" },  function() nnwm.scratchpad_toggle("term") end,    "Toggle terminal scratchpad")
nnwm.key({ mod, "Shift", "apostrophe" }, function() nnwm.move_to_scratchpad("term") end, "Send to terminal scratchpad")

-- ── Core keybindings ─────────────────────────────────────────────────────────

nnwm.key({ mod, "Shift", "c" }, function() nnwm.quit() end,  { description = "Quit compositor" })
nnwm.key({ mod, "Shift", "q" }, function() nnwm.close() end, { description = "Close window" })

-- Launchers
nnwm.key({ mod, "Return" },       function() nnwm.spawn("kitty") end,          { description = "Terminal" })
nnwm.key({ mod, "d" },            function() nnwm.spawn("rofi -show drun") end, { description = "App launcher" })
nnwm.key({ mod, "Shift", "d" },   function() nnwm.spawn("rofi -show run") end,  { description = "Run command" })
nnwm.key({ mod, "e" },            function() nnwm.spawn("thunar") end,          { description = "File manager" })

-- Focus — directional
nnwm.key({ mod, "h" }, function() nnwm.focus_dir("left") end,  { description = "Focus left" })
nnwm.key({ mod, "l" }, function() nnwm.focus_dir("right") end, { description = "Focus right" })
nnwm.key({ mod, "k" }, function() nnwm.focus_dir("up") end,    { description = "Focus up" })
nnwm.key({ mod, "j" }, function() nnwm.focus_dir("down") end,  { description = "Focus down" })

-- Focus — sequential
nnwm.key({ mod, "Tab" },       function() nnwm.focus_next() end,         { description = "Focus next" })
nnwm.key({ mod, "Shift", "Tab" }, function() nnwm.focus_prev() end,      { description = "Focus prev" })

-- Focus — floating layer
nnwm.key({ mod, "F1" },        function() nnwm.focus_next_float() end,   { description = "Focus next float" })
nnwm.key({ mod, "F2" },        function() nnwm.focus_prev_float() end,   { description = "Focus prev float" })
nnwm.key({ mod, "grave" },     function() nnwm.focus_mode_toggle() end,  { description = "Toggle tiled/float focus" })

-- Move / swap — directional
nnwm.key({ mod, "Shift", "h" }, function() nnwm.move_dir("left") end,   { description = "Move window left" })
nnwm.key({ mod, "Shift", "l" }, function() nnwm.move_dir("right") end,  { description = "Move window right" })
nnwm.key({ mod, "Shift", "k" }, function() nnwm.move_dir("up") end,     { description = "Move window up" })
nnwm.key({ mod, "Shift", "j" }, function() nnwm.move_dir("down") end,   { description = "Move window down" })

-- Swap — sequential
nnwm.key({ mod, "Shift", "n" }, function() nnwm.swap_next() end,        { description = "Swap with next" })
nnwm.key({ mod, "Shift", "p" }, function() nnwm.swap_prev() end,        { description = "Swap with prev" })
nnwm.key({ mod, "Shift", "m" }, function() nnwm.swap_master() end,      { description = "Swap with master" })
nnwm.key({ mod, "Shift", "Return" }, function() nnwm.swap_left() end,   { description = "Promote to master" })

-- Cycle the window list (last → master)
nnwm.key({ mod, "c" }, function() nnwm.cycle() end, { description = "Cycle windows" })

-- Window state
nnwm.key({ mod, "t" },          function() nnwm.toggle_float() end,         { description = "Toggle float" })
nnwm.key({ mod, "f" },          function() nnwm.toggle_fullscreen() end,    { description = "Toggle fullscreen" })
nnwm.key({ mod, "Shift", "f" }, function() nnwm.toggle_fake_fullscreen() end, { description = "Toggle fake-fullscreen" })
nnwm.key({ mod, "m" },          function() nnwm.toggle_maximize() end,      { description = "Toggle maximize" })
nnwm.key({ mod, "s" },          function() nnwm.toggle_sticky() end,        { description = "Toggle sticky" })

-- Master ratio
nnwm.key({ mod, "equal" }, function() nnwm.master_ratio_grow() end,   { description = "Grow master" })
nnwm.key({ mod, "minus" }, function() nnwm.master_ratio_shrink() end, { description = "Shrink master" })

-- ── Layout cycling ────────────────────────────────────────────────────────────

nnwm.key({ mod, "bracketright" }, function() nnwm.layout.next() end,             { repeat = false, description = "Next layout" })
nnwm.key({ mod, "bracketleft" },  function() nnwm.layout.prev() end,             { repeat = false, description = "Prev layout" })
nnwm.key({ mod, "F3" },           function() nnwm.layout.set("htile") end,       { description = "Layout: htile" })
nnwm.key({ mod, "F4" },           function() nnwm.layout.set("vtile") end,       { description = "Layout: vtile" })
nnwm.key({ mod, "F5" },           function() nnwm.layout.set("tabbed") end,      { description = "Layout: tabbed" })
nnwm.key({ mod, "F6" },           function() nnwm.layout.set("float") end,       { description = "Layout: float" })
nnwm.key({ mod, "F7" },           function() nnwm.layout.set("hscroll") end,     { description = "Layout: hscroll" })
nnwm.key({ mod, "F8" },           function() nnwm.layout.set("vscroll") end,     { description = "Layout: vscroll" })
nnwm.key({ mod, "F9" },           function() nnwm.layout.toggle_float() end,     { description = "Toggle float layout" })

-- ── Workspace switching ───────────────────────────────────────────────────────

for i = 1, 9 do
    nnwm.key({ mod, tostring(i) }, function()
        nnwm.switch_workspace(i)
    end, { repeat = false, description = "Workspace " .. i })

    nnwm.key({ mod, "Shift", tostring(i) }, function()
        nnwm.move_to_workspace(i)
    end, { repeat = false, description = "Move to workspace " .. i })
end

-- ── Monitor navigation ────────────────────────────────────────────────────────

nnwm.key({ mod, "comma" },        function() nnwm.focus_monitor_prev() end,      { description = "Focus prev monitor" })
nnwm.key({ mod, "period" },       function() nnwm.focus_monitor_next() end,      { description = "Focus next monitor" })
nnwm.key({ mod, "Shift", "comma" },  function() nnwm.move_to_monitor_prev() end, { description = "Move to prev monitor" })
nnwm.key({ mod, "Shift", "period" }, function() nnwm.move_to_monitor_next() end, { description = "Move to next monitor" })

-- ── Overlays ──────────────────────────────────────────────────────────────────

nnwm.key({ mod, "w" }, function() nnwm.toggle_overview() end,
    { repeat = false, description = "Toggle overview" })

nnwm.key({ mod, "Shift", "slash" }, function() nnwm.toggle_keybind_overlay() end,
    { repeat = false, description = "Show keybindings" })

-- ── Cursor utilities ──────────────────────────────────────────────────────────

nnwm.key({ "Ctrl", "Ctrl" }, function() nnwm.find_cursor() end,
    { description = "Find cursor" })

-- Example: warp cursor to center of focused output
nnwm.key({ mod, "Shift", "w" }, function()
    local out = nnwm.current_output()
    if out then
        nnwm.cursor.warp(out.x + out.width / 2, out.y + out.height / 2)
    end
end, { description = "Warp cursor to output center" })

-- ── Media / brightness keys ───────────────────────────────────────────────────

nnwm.key({ "XF86AudioMute" },        function() nnwm.spawn("pactl set-sink-mute @DEFAULT_SINK@ toggle") end)
nnwm.key({ "XF86AudioLowerVolume" }, function() nnwm.spawn("pactl set-sink-volume @DEFAULT_SINK@ -5%") end)
nnwm.key({ "XF86AudioRaiseVolume" }, function() nnwm.spawn("pactl set-sink-volume @DEFAULT_SINK@ +5%") end)
nnwm.key({ "XF86AudioMicMute" },     function() nnwm.spawn("pactl set-source-mute @DEFAULT_SOURCE@ toggle") end)
nnwm.key({ "XF86MonBrightnessDown" }, function() nnwm.spawn("brightnessctl set 5%-") end)
nnwm.key({ "XF86MonBrightnessUp" },  function() nnwm.spawn("brightnessctl set 5%+") end)
nnwm.key({ "XF86AudioPlay" },        function() nnwm.spawn("playerctl play-pause") end)
nnwm.key({ "XF86AudioNext" },        function() nnwm.spawn("playerctl next") end)
nnwm.key({ "XF86AudioPrev" },        function() nnwm.spawn("playerctl previous") end)

-- Screenshot
nnwm.key({ "", "Print" },        function() nnwm.spawn("grim ~/Pictures/$(date +%F_%T).png") end, { description = "Screenshot" })
nnwm.key({ "Shift", "Print" },   function() nnwm.spawn("grim -g \"$(slurp)\" ~/Pictures/$(date +%F_%T).png") end, { description = "Screenshot region" })

-- ── Touchpad gestures ─────────────────────────────────────────────────────────

nnwm.gesture(3, "left",  function() nnwm.focus_monitor_next() end)
nnwm.gesture(3, "right", function() nnwm.focus_monitor_prev() end)
nnwm.gesture(3, "up",    function() nnwm.toggle_overview() end)
nnwm.gesture(3, "down",  function() nnwm.toggle_overview() end)
nnwm.gesture(4, "up",    function() nnwm.toggle_fullscreen() end)
nnwm.gesture(4, "down",  function() nnwm.toggle_float() end)

-- ── Event hooks ───────────────────────────────────────────────────────────────

nnwm.on("startup", function()
    nnwm.log.info("compositor ready")
    nnwm.bar.update("battery")  -- force immediate first paint of custom modules
end)

nnwm.on("shutdown", function()
    nnwm.log.info("compositor exiting")
end)

nnwm.on("window_open", function(win)
    nnwm.log.info("open:", win.app_id, win.title)
end)

nnwm.on("window_close", function(win)
    nnwm.log.info("close:", win.app_id)
end)

nnwm.on("window_focus", function(win)
    -- Example: force-redraw a module when focus changes
    nnwm.bar.update("cpu")
end)

nnwm.on("window_urgent", function(win)
    nnwm.log.warn("urgent:", win.app_id, win.title)
    -- Optionally auto-focus urgent windows:
    -- nnwm.switch_workspace(win.workspace)
end)

nnwm.on("workspace_switch", function(ws)
    nnwm.log.info("workspace →", ws.index, ws.layout)
end)

nnwm.on("output_connect", function(out)
    nnwm.log.info("output connected:", out.name, out.description)
end)

nnwm.on("output_disconnect", function(out)
    nnwm.log.info("output disconnected:", out.name)
end)

nnwm.on("idle", function()
    nnwm.log.info("system idle — locking screen")
    nnwm.spawn("swaylock")
end)

nnwm.on("resume", function()
    nnwm.log.info("activity resumed")
end)

nnwm.on("lid_close", function()
    nnwm.log.info("lid closed")
    nnwm.spawn("swaylock")
end)

nnwm.on("lid_open", function()
    nnwm.log.info("lid opened")
end)

nnwm.on("tablet_mode_on", function()
    nnwm.log.info("tablet mode on")
end)

nnwm.on("tablet_mode_off", function()
    nnwm.log.info("tablet mode off")
end)

-- ── Timers ────────────────────────────────────────────────────────────────────

-- Repeating: refresh the battery module every 30 seconds.
local _battery_timer = nnwm.timer(30000, function()
    nnwm.bar.update("battery")
end)

-- One-shot: spawn a welcome notification after 2 seconds.
nnwm.timer(2000, function()
    nnwm.spawn("notify-send 'nnwm' 'Compositor ready'")
end, { once = true })

-- Timer handle usage:
local poll = nnwm.timer(5000, function()
    nnwm.bar.update("cpu")
end)

nnwm.on("idle", function()
    poll:pause()   -- stop polling when idle
end)

nnwm.on("resume", function()
    poll:resume()  -- restart from remaining time on wake
end)

-- ── Autostart ─────────────────────────────────────────────────────────────────
-- spawn_once is safe to call on every hot-reload — duplicates are ignored.

nnwm.spawn_once("swaybg -i ~/Pictures/wallpaper.png -m fill")
nnwm.spawn_once("dunst")
nnwm.spawn_once("nm-applet --indicator")
nnwm.spawn_once("/usr/lib/xdg-desktop-portal -r")
nnwm.spawn_once("/usr/lib/xdg-desktop-portal-wlr -r")
