-- Example nnwm configuration
-- NOTE that this example configuration is not exhaustive in the sense that it doesn not include all the available options.
-- For a complete list of options, please refer to the documentation.

-- Layout
nnwm.master_ratio = 0.55
nnwm.master_ratio_step = 0.05
nnwm.master_ratio_min = 0.1
nnwm.master_ratio_max = 0.9
nnwm.focus_follows_mouse = true
nnwm.new_window_master = false
nnwm.titlebars = true
nnwm.titlebar_height = 20
nnwm.titlebar_text_align = 0
nnwm.titlebar_text_align = 2

nnwm.opt = {

    fx = {
        corner_radius = 10,
        shadow = {
            color = "#444444",
            blur_sigma = 10,
            enabled = true,
            offset_x = 10,
            offset_y = 10,
        },
    },

    keyboard = {
        xkb_options = "ctrl:swapcaps",
    },

    mouse = {
        focus_follows_mouse = true,
    },

    gaps = {
        inner = 20,
        outer = 20,
    },

    client_decorations = false,

    border = {

    },

    monitors = {
        {
            description = "AU Optronics 0xE3AC Unknown",
            x = 0,
            y = 0,
            width = 1920,
            height = 1200,
            scale = 1.25,
        },
        {
            description = "HP Inc. HP P24h G5 3CM5031JJC",
            x = 1536,
            y = 0,
            width = 1920,
            height = 1080,
            scale = 1.0,
        },
        {
            description = "HP Inc. HP 524pu 1H35321M53",
            x = 3456,
            y = 0,
            width = 1920,
            height = 1080,
            scale = 1.0,
        },
    },


}

local mod = "Super"

-- Keybindings
nnwm.key({ mod, "Shift", "c" }, function() nnwm.quit() end)
nnwm.key({ mod, "Shift", "q" }, function() nnwm.close() end)
nnwm.key({ mod, "p" }, function() nnwm.spawn("rofi -show drun") end)
nnwm.key({ mod, "h" }, function() nnwm.master_ratio_shrink() end)
nnwm.key({ mod, "j" }, function() nnwm.focus_next() end)
nnwm.key({ mod, "k" }, function() nnwm.focus_prev() end)
nnwm.key({ mod, "l" }, function() nnwm.master_ratio_grow() end)
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
nnwm.spawn_once("swaybg -i ~/Gits/wallpapers/1920x1080_px_black_geometry_gray_minimalism_Triangle_white-1286005.jpg -m fill")
nnwm.spawn_once("dunst")

nnwm.rule({ app_id = "pcmanfm" }, { floating = true, titlebar = true })
