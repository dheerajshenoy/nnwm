-- nnwm configuration

-- Layout
local hostname = nnwm.host_name()

if hostname == "archlinux" then
    nnwm.monitor({
        description = "AU Optronics 0xE3AC Unknown",
        x = 0,
        y = 0,
        width = 1920,
        height = 1200,
        scale = 1.25,
        vrr = false,
    })

    nnwm.monitor({
        description = "HP Inc. HP P24h G5 3CM5031JJC",
        x = 1536,
        y = 0,
        width = 1920,
        height = 1080,
        scale = 1.0,
    })

    nnwm.monitor({
        description = "HP Inc. HP 524pu 1H35321M53",
        x = 3456,
        y = 0,
        width = 1920,
        height = 1080,
        scale = 1.0,
    })
elseif hostname == "matrix" then
    nnwm.monitor({
        name = "eDP-1",
        scale = 1.25
    })
end

nnwm.opt = {

    -- monitors = {
    --
    -- }

    fx = {
        rounding = {
            radius = 10,
            smart = true,
        },
        shadow = {
            color = "#444444",
            blur_sigma = 10,
            enabled = true,
            offset_x = 10,
            offset_y = 10,
        },
    },

    layout = {
        master_ratio = 0.55,
        master_ratio_step = 0.05,
        master_ratio_min = 0.1,
        master_ratio_max = 0.9,
        focus_follows_mouse = true,
        new_window_master = false,

        tabbed = {
            tab_position = "left",
            tab_style = "minimal",
            smart = true,
        }
    },

    titlebar = {
        enabled = true,
        height = 30,
        text_align = 0,
    },

    keyboard = {
        xkb_options = "ctrl:swapcaps",
    },

    mouse = {
        warp_to_focused_window = false,
        focus_follows_mouse = true,
        hide_cursor_when_typing = false,
    },

    gaps = {
        inner = 20,
        outer = 20,
        smart = false,
    },

    client_decorations = false,

    border = {
        smart = false,
    },
}

local mod = "Super"

-- Keybindings
nnwm.key({ mod, "Shift", "c" }, function() nnwm.quit() end)
nnwm.key({ mod, "Shift", "q" }, function() nnwm.close() end)
nnwm.key({ mod, "p" }, function() nnwm.spawn("rofi -show drun") end)
-- nnwm.key({ mod, "h" }, function() nnwm.master_ratio_shrink() end)
-- nnwm.key({ mod, "l" }, function() nnwm.master_ratio_grow() end)

-- Logical directional focus
nnwm.key({ mod, "h" }, function() nnwm.focus_dir("left") end)
nnwm.key({ mod, "l" }, function() nnwm.focus_dir("right") end)

-- Logical directional move
nnwm.key({ mod, "Shift", "h" }, function() nnwm.move_dir("left") end)
nnwm.key({ mod, "Shift", "l" }, function() nnwm.move_dir("right") end)

nnwm.key({ mod, "j" }, function() nnwm.focus_next() end)
nnwm.key({ mod, "k" }, function() nnwm.focus_prev() end)
nnwm.key({ mod, "Shift", "h" }, function() nnwm.swap_left() end)
nnwm.key({ mod, "Shift", "j" }, function() nnwm.swap_next() end)
nnwm.key({ mod, "Shift", "k" }, function() nnwm.swap_prev() end)
nnwm.key({ mod, "Shift", "l" }, function() nnwm.swap_right() end)
nnwm.key({ mod, "Shift", "Return" }, function() nnwm.spawn("kitty") end)
nnwm.key({ mod, "Return" }, function() nnwm.swap_master() end)
nnwm.key({ mod, "t" }, function() nnwm.toggle_float() end)
nnwm.key({ mod, "f" }, function() nnwm.toggle_fullscreen() end)
nnwm.key({ mod, "Shift", "f" }, function() nnwm.toggle_fake_fullscreen() end)
nnwm.key({ "XF86AudioMute" }, function() nnwm.spawn("pactl set-sink-mute @DEFAULT_SINK@ toggle") end)
nnwm.key({ "XF86AudioLowerVolume" }, function() nnwm.spawn("pactl set-sink-volume @DEFAULT_SINK@ -5%") end)
nnwm.key({ "XF86AudioRaiseVolume" }, function() nnwm.spawn("pactl set-sink-volume @DEFAULT_SINK@ +5%") end)
nnwm.key({ "XF86AudioMicMute" }, function() nnwm.spawn("pactl set-source-mute @DEFAULT_SOURCE@ toggle") end)
nnwm.key({ "XF86MonBrightnessDown" }, function() nnwm.spawn("brightnessctl set 5%-") end)
nnwm.key({ "XF86MonBrightnessUp" }, function() nnwm.spawn("brightnessctl set 5%+") end)
nnwm.key({ "Super", "Space" }, function() nnwm.layout.next() end)
nnwm.key({ "Super", "Semicolon" }, function() nnwm.focus_monitor_prev() end)
nnwm.key({ "Super", "Apostrophe" }, function() nnwm.focus_monitor_next() end)
nnwm.key({ "Super", "F2" }, function() nnwm.spawn("pcmanfm") end)

nnwm.key({ "Super", "Shift", "Semicolon" }, function() nnwm.move_to_monitor_prev() end)
nnwm.key({ "Super", "Shift", "Apostrophe" }, function() nnwm.move_to_monitor_next() end)

for i = 1, 9 do
    nnwm.key({ mod, string.format("%d", i) }, function() nnwm.switch_workspace(i) end)
    nnwm.key({ mod, "Shift", string.format("%d", i) }, function() nnwm.move_to_workspace(i) end)
end

nnwm.spawn_once("waybar -c ~/.config/waybar/config.jsonc")
nnwm.spawn_once("swaybg -i ~/Gits/wallpapers/11.png -m fill")
nnwm.spawn_once("dunst")
nnwm.spawn_once("/usr/lib/xdg-desktop-portal -r")
nnwm.spawn_once("/usr/lib/xdg-desktop-portal-wlr -r")
nnwm.spawn_once("nm-applet")

-- nnwm.gesture(3, "left",  function() nnwm.move_to_workspace(nnwm.active_workspace() - 1) end)
-- nnwm.gesture(3, "right", function() nnwm.switch_workspace(nnwm.active_workspace() + 1) end)
-- nnwm.gesture(4, "up",    function() nnwm.toggle_fullscreen() end)
-- nnwm.gesture(4, "down",  function() nnwm.toggle_float() end)

nnwm.rule({ app_id = "pcmanfm" }, { floating = true, titlebar = true })
