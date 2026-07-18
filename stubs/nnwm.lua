---@meta

-- ── MOD ──────────────────────────────────────────────────────────────────────

---@class MOD
---@field Shift  integer WLR_MODIFIER_SHIFT
---@field Caps   integer WLR_MODIFIER_CAPS
---@field Ctrl   integer WLR_MODIFIER_CTRL
---@field Alt    integer WLR_MODIFIER_ALT
---@field Mod2   integer WLR_MODIFIER_MOD2
---@field Mod3   integer WLR_MODIFIER_MOD3
---@field Super  integer WLR_MODIFIER_LOGO
---@field Mod5   integer WLR_MODIFIER_MOD5
MOD = {}

-- ── nnwm ─────────────────────────────────────────────────────────────────────

---@class nnwm.layout.tabbed
---@field height?       integer                       Tab bar thickness in pixels (default: 24)
---@field tab_style?    "normal"|"minimal"            Tab bar style: `"normal"` renders titles, `"minimal"` renders only colored strips (default: "normal")
---@field tab_position? "top"|"bottom"|"left"|"right" Tab bar edge in tabbed layout (default: "top")
---@field smart?        boolean                       Hide the tab bar when only one tiled window is present (default: false)

---@class nnwm.layout
---@field new_window_master?   boolean            When true new windows become master; when false they append to the stack (default: true)
---@field master_ratio?        number             Fraction of screen width/height for the master pane (default: 0.55)
---@field master_ratio_step?   number             Step size for master ratio adjustments (default: 0.05)
---@field master_ratio_min?    number             Minimum master ratio (default: 0.1)
---@field master_ratio_max?    number             Maximum master ratio (default: 0.9)
---@field scroll_column_width? number             Fraction of output width per column in hscroll layout: 0.0–1.0 (default: 0.5)
---@field scroll_row_height?   number             Fraction of output height per row in vscroll layout: 0.0–1.0 (default: 0.5)
---@field tabbed?              nnwm.layout.tabbed

---@class nnwm.gaps
---@field inner?  integer  Gap in pixels between adjacent windows (default: 0)
---@field outer?  integer  Gap in pixels between windows and the screen edge (default: 0)
---@field smart?  boolean  Disable gaps when only one window is on screen (default: false)

---@alias nnwm.color number[]|string  RGBA as `{r, g, b, a}` floats 0–1, or a hex string: `"RRGGBB"`, `"RRGGBBAA"`, `"#RRGGBB"`, `"#RRGGBBAA"`

---@class nnwm.border
---@field width?           integer     Border thickness in pixels (default: 2)
---@field smart?           boolean     Disable borders when only one window is on screen (default: false)
---@field focused_color?   nnwm.color  RGBA color for the focused window border (default: {0.3, 0.5, 0.8, 1.0})
---@field unfocused_color? nnwm.color  RGBA color for unfocused window borders (default: {0.15, 0.15, 0.15, 1.0})

---@class nnwm.keyboard
---@field repeat_rate?   integer  Key repeat rate in keys/sec (default: 25)
---@field repeat_delay?  integer  Delay before repeat starts in ms (default: 600)
---@field xkb_rules?     string   XKB rules file name, e.g. `"evdev"` (default: system default)
---@field xkb_layout?    string   XKB layout, e.g. `"us"`, `"de"`, `"us,ru"` (default: system default)
---@field xkb_variant?   string   XKB variant, e.g. `"dvorak"`, `"colemak"` (default: system default)
---@field xkb_options?   string   Comma-separated XKB options, e.g. `"caps:escape,compose:ralt"` (default: "")

---@class nnwm.touchpad
---@field enabled?                   boolean                                          Enable the touchpad (default: true)
---@field tap_to_click?              boolean                                          Enable tap-to-click (default: true)
---@field drag?                      boolean                                          Enable tap-and-drag (default: true)
---@field natural_scroll?            boolean                                          Natural/reverse scroll direction (default: true)
---@field disable_while_typing?      boolean                                          Disable touchpad while typing (default: true)
---@field disable_on_external_mouse? boolean                                          Disable touchpad when an external mouse is connected (default: false)
---@field scroll_factor?             number                                           Multiplier applied to scroll delta (default: 1.0)
---@field scroll_method?             "two_finger"|"edge"|"on_button_down"|"no_scroll" Scroll method (default: "two_finger")

---@class nnwm.mouse
---@field focus_follows_mouse?      boolean                       Automatically focus the window under the cursor (default: false)
---@field cursor_theme?             string                        Xcursor theme name (default: "default")
---@field cursor_size?              integer                       Cursor size in pixels (default: 24)
---@field accel_speed?              number                        Pointer acceleration speed: -1.0 (slowest) to 1.0 (fastest) (default: 0.0)
---@field accel_profile?            "adaptive"|"flat"|"none"      Pointer acceleration profile (default: "adaptive")
---@field natural_scroll?           boolean                       Natural/reverse scroll direction (default: false)
---@field disable_while_typing?     boolean                       Disable mouse input while typing (default: false)
---@field hide_cursor_when_typing?  boolean                       Hide cursor on keypress; restores on mouse movement (default: false)
---@field warp_to_focused_window?   boolean                       Warp cursor to the center of a window when it gains focus (default: false)

---@class nnwm.titlebar
---@field enabled?             boolean     Enable the server-side titlebar (default: false)
---@field height?              integer     Titlebar height in pixels, used when enabled (default: 20).
---@field font?                string      Pango font description for the title text, e.g. `"Sans Bold 10"` (default: "Sans 10")
---@field text_align?          integer     Text alignment: 0 = left, 1 = center, 2 = right (default: 1)
---@field bg_color?            nnwm.color  Background color for unfocused windows (default: {0.2, 0.2, 0.2, 1.0})
---@field focused_bg_color?    nnwm.color  Background color for the focused window (default: {0.25, 0.35, 0.55, 1.0})
---@field urgent_bg_color?     nnwm.color  Background color for urgent (attention-requesting) windows (default: {0.7, 0.2, 0.2, 1.0})
---@field text_color?          nnwm.color  Title text color for unfocused windows (default: {1.0, 1.0, 1.0, 1.0})
---@field focused_text_color?  nnwm.color  Title text color for the focused window (default: {1.0, 1.0, 1.0, 1.0})
---@field urgent_text_color?   nnwm.color  Title text color for urgent windows (default: {1.0, 1.0, 1.0, 1.0})

---@class nnwm.fx.shadow
---@field enabled?    boolean     Enable drop shadows (default: false)
---@field blur_sigma? number      Gaussian blur sigma controlling shadow softness (default: 10.0)
---@field offset_x?  number      Horizontal shadow offset in pixels (default: 4.0)
---@field offset_y?  number      Vertical shadow offset in pixels (default: 4.0)
---@field color?     nnwm.color  Shadow color (default: {0, 0, 0, 0.5})

---@class nnwm.fx.blur
---@field enabled?     boolean  Enable background blur behind windows (default: false)
---@field passes?      integer  Number of dual-kawase blur passes; higher = softer (default: 3)
---@field radius?      integer  Blur radius in pixels (default: 5)
---@field noise?       number   Noise factor to reduce banding artifacts (default: 0.0)
---@field brightness?  number   Brightness multiplier for blurred content (default: 1.0)
---@field contrast?    number   Contrast multiplier for blurred content (default: 1.0)
---@field saturation?  number   Saturation multiplier for blurred content (default: 1.0)

---@class nnwm.fx.rounding
---@field radius? integer  Window corner radius in pixels; 0 disables (default: 0)
---@field smart?  boolean  Collapse corner radius when only one window is visible (default: false)

---@class nnwm.fx
---Only active when built with `USE_SCENEFX=ON` and running on real DRM hardware.
---@field rounding?          nnwm.fx.rounding
---@field opacity?           number  Base window content opacity: 0.0–1.0 (default: 1.0)
---@field focused_opacity?   number  Opacity for the focused window; <0 = inherit opacity (default: -1)
---@field unfocused_opacity? number  Opacity for unfocused windows; <0 = inherit opacity (default: -1)
---@field shadow?            nnwm.fx.shadow
---@field blur?              nnwm.fx.blur
---@field animations?        nnwm.animations  Animation settings (sceneFX only)

---@class nnwm_anim_type_config
---@field style?    string   Style for this animation type (depends on type)
---@field easing?   string   Easing override: "ease_out"|"ease_in"|"ease_in_out"|"linear"|"bounce"|"elastic"
---@field duration? integer  Duration override in ms; 0 = inherit global

---@class nnwm.animations
---@field enabled?   boolean  Enable animations (default: true)
---@field duration?  integer  Global default animation duration in ms (default: 250)
---@field easing?    string   Global default easing: "ease_out"|"ease_in"|"ease_in_out"|"linear"|"bounce"|"elastic" (default: "ease_out")
---@field open?      nnwm_anim_type_config  Open style: "fade_scale"|"fade"|"scale"|"slide_up"|"slide_down"|"slide_left"|"slide_right"|"none" (default: "fade_scale")
---@field close?     nnwm_anim_type_config  Close style: same options as open (default: "fade")
---@field layout?    nnwm_anim_type_config  Layout tween style: "tween"|"none" (default: "tween")
---@field workspace? nnwm_anim_type_config  Workspace switch style: "slide"|"fade"|"none" (default: "slide")
---@field focus?     nnwm_anim_type_config  Focus border style: "crossfade"|"none" (default: "crossfade")

---@class nnwm_opts
---@field layout?                    nnwm.layout
---@field gaps?                      nnwm.gaps
---@field border?                    nnwm.border
---@field keyboard?                  nnwm.keyboard
---@field touchpad?                  nnwm.touchpad
---@field mouse?                     nnwm.mouse
---@field titlebar?                  nnwm.titlebar
---@field fx?                        nnwm.fx
---@field clipboard?                 boolean  Enable clipboard (wl_data_device selection); set false to block all clipboard writes (default: true)
---@field client_decorations?        boolean
---@field seat_name?                 string
---@field monitors?                  nnwm_monitor_config[]
---@field workspace_back_and_forth?  boolean   When true, switching to the active workspace jumps to the previously visited workspace instead of doing nothing (default: false)
---@field show_config_error_overlay? boolean   Show a red overlay when the Lua config fails to load. Dangerous to disable — errors will be silently ignored (default: true)
---@field workspace_count?           integer   Number of workspaces (1–9, default: 9)
---@field workspace_names?           string[]  Per-workspace labels shown in the overview and sent via ext-workspace-v1 (e.g. `{"web","code","term"}`). Missing or empty entries fall back to the numeric index.

---@class nnwm
---@field opt nnwm_opts

--- Monitor configuration (array of tables). First match wins.
---@class nnwm.Window
---@field title         string   Window title (empty string if unset)
---@field app_id        string   Application ID (empty string if unset)
---@field floating      boolean  True if the window is floating
---@field fullscreen    boolean  True if the window is fullscreen
---@field fake_fullscreen boolean True if the window is fake-fullscreen
---@field maximized     boolean  True if the window is maximized
---@field sticky        boolean  True if the window is sticky
---@field workspace     integer  Workspace index (1–9)
---@field x             integer  Scene X position in pixels
---@field y             integer  Scene Y position in pixels
---@field width         integer  Current width in pixels
---@field height        integer  Current height in pixels
---@field output        string?  Name of the output the window is on (e.g. `"DP-1"`), or nil

---@class nnwm.Workspace
---@field index         integer  Workspace index (1–9)
---@field layout        "htile"|"vtile"|"tabbed"|"hscroll"|"vscroll"|"unknown"  Active layout
---@field master_ratio  number   Current master ratio for this workspace
---@field window_count  integer  Number of tiled windows on this workspace
---@field output        string   Name of the output this workspace belongs to

---@class nnwm.Output
---@field name              string   Connector name (e.g. `"DP-1"`)
---@field description       string   EDID description string
---@field width             integer  Width in pixels
---@field height            integer  Height in pixels
---@field scale             number   Output scale factor
---@field x                 integer  X position in the output layout
---@field y                 integer  Y position in the output layout
---@field active_workspace  integer  Active workspace index (1–9)

---@class nnwm_window_rule_match
---@field app_id string? fnmatch glob matched against the window's app_id (e.g. `"firefox"`, `"foot*"`)
---@field title  string? fnmatch glob matched against the window title

---@class nnwm_window_rule_action
---@field floating         boolean? Make the window floating (true) or tiled (false)
---@field fullscreen       boolean? Make the window fullscreen
---@field fake_fullscreen  boolean? Make the window fake-fullscreen (fills usable area, not a true fullscreen)
---@field maximize         boolean? Maximize the window to fill the full usable area while remaining tiled
---@field focused          boolean? Switch to the window's workspace and focus it on spawn
---@field sticky           boolean? Make the window sticky (appears on all workspaces; tiles or floats as normal)
---@field workspace   integer? Assign to workspace 1–9
---@field monitor     string?  Assign to the output with this name (e.g. `"DP-1"`)
---@field opacity     number?  Override global opacity for this window: 0.0 (invisible) – 1.0 (opaque). Requires `USE_SCENEFX=ON`.
---@field blur        boolean? Override global blur setting for this window. Requires `USE_SCENEFX=ON`.
---@field anim_open   string?  Open animation style override: "fade_scale"|"fade"|"scale"|"slide_up"|"slide_down"|"slide_left"|"slide_right"|"none"
---@field anim_close  string?  Close animation style override: same options as anim_open
---@field no_anim     boolean? Disable all animations for this window

---@class nnwm_monitor_config
---@field name        string?   Output connector name, e.g. `"DP-1"`, `"eDP-1"`
---@field description string?   Combined EDID string `"make model serial"` (serial is `"Unknown"` if absent), e.g. `"HP Inc. HP P24h G5 3CM5031JJC"`
---@field width     integer?  Mode width in pixels
---@field height    integer?  Mode height in pixels
---@field refresh   integer?  Refresh rate in Hz
---@field x         integer?  Layout X position (unset = auto)
---@field y         integer?  Layout Y position (unset = auto)
---@field scale     number?   Output scale factor (e.g. 2.0)
---@field transform string?   Rotation: "none", "90", "180", "270", "flipped", "flipped-90", "flipped-180", "flipped-270"
---@field hdr       boolean?  Enable HDR (wlroots 0.20+)
---@field disabled  boolean?  Disable this output
---@field struts    { top?: integer, bottom?: integer, left?: integer, right?: integer }?  Reserved pixels on each edge, applied after layer-shell exclusive zones
nnwm = {}
nnwm.opt = {} ---@type nnwm_opts

---Register a keybinding. `combo` is an array of modifier and key name strings;
---`callback` is called when the combo is pressed. The optional `description`
---is a human-readable label stored for future use in a keybindings overlay.
---
---Modifier names: `"Super"`, `"Shift"`, `"Ctrl"`, `"Alt"`, `"Mod2"`, `"Mod3"`, `"Mod5"`, `"Caps"`.
---Exactly one non-modifier entry (an XKB key name) is required.
---
---```lua
---nnwm.key({"Super", "Return"}, function() nnwm.spawn("kitty") end, "Launch terminal")
---nnwm.key({"Super", "Shift", "q"}, function() nnwm.close() end, "Close window")
---```
---@param combo       string[]  Array of modifier names and exactly one key name
---@param callback    fun()     Function to call when the combo is pressed
---@param description string?   Optional human-readable label for the keybindings overlay
function nnwm.key(combo, callback, description) end

---Register a touchpad swipe gesture binding. The callback is fired when a
---swipe with the given number of fingers ends in the given direction.
---Direction is determined by the dominant axis of the total displacement.
---
---```lua
---nnwm.gesture(3, "left",  function() nnwm.switch_workspace(1) end)
---nnwm.gesture(3, "right", function() nnwm.switch_workspace(2) end)
---nnwm.gesture(4, "up",    function() nnwm.toggle_fullscreen() end)
---```
---@param fingers   integer  Number of fingers (e.g. 3 or 4)
---@param direction string   `"up"` | `"down"` | `"left"` | `"right"`
---@param callback  fun()    Function to call when the gesture is recognized
function nnwm.gesture(fingers, direction, callback) end

---Register a window rule. When a new window maps, all rules are tested in
---order; matching rules are applied. All fields in `match` must match (AND
---logic). Both `app_id` and `title` support fnmatch globs (`*`, `?`, `[…]`).
---
---```lua
---nnwm.rule({ app_id = "firefox" }, { workspace = 2 })
---nnwm.rule({ title = "*Picture-in-Picture*" }, { floating = true })
---nnwm.rule({ app_id = "mpv" }, { floating = true, monitor = "DP-1" })
---```
---@param match  nnwm_window_rule_match
---@param action nnwm_window_rule_action
function nnwm.rule(match, action) end

---@class nnwm_monitor_config
---@field name?        string   Connector name, e.g. `"eDP-1"`, `"DP-1"`, `"HDMI-A-1"`
---@field description? string   EDID description substring to match
---@field width?       integer  Mode width in pixels
---@field height?      integer  Mode height in pixels
---@field refresh?     integer  Refresh rate in Hz (0 = preferred)
---@field x?           integer  Layout X position (unset = auto-arrange)
---@field y?           integer  Layout Y position (unset = auto-arrange)
---@field scale?       number   Output scale factor (e.g. `1.25` for HiDPI)
---@field transform?   string   Rotation: `"none"`, `"90"`, `"180"`, `"270"`, `"flipped"`, `"flipped-90"`, `"flipped-180"`, `"flipped-270"`
---@field hdr?         boolean  Enable HDR (wlroots 0.20+)
---@field disabled?    boolean  Disable this output entirely
---@field struts?      { top?: integer, bottom?: integer, left?: integer, right?: integer }

--- Configure a monitor. Call once per output that needs non-default settings.
--- The first matching rule wins; unmatched outputs use their preferred mode.
---
--- Match by `name` (connector name) or `description` (EDID substring). To find
--- the exact values, run nnwm once and check the log:
--- ```
--- WLR_LOG_LEVEL=info nnwm 2>/tmp/nnwm.log
--- grep "new output" /tmp/nnwm.log
--- ```
---
--- ```lua
--- nnwm.monitor({ name = "eDP-1", x = 0, y = 0, width = 1920, height = 1200, scale = 1.25 })
--- nnwm.monitor({ description = "HP Inc. HP P24h G5 3CM5031JJC", x = 1536, y = 0, width = 1920, height = 1080 })
--- nnwm.monitor({ name = "DP-2", disabled = true })
--- ```
---@param config nnwm_monitor_config
function nnwm.monitor(config) end

--- Terminate the compositor.
function nnwm.quit() end

--- Return the hostname of the machine.
---@return string
function nnwm.host_name() end

--- Return a snapshot of the currently focused window, or `nil` if no window is focused.
---@return nnwm.Window?
function nnwm.current_window() end

--- Return a snapshot of the focused output's active workspace, or `nil` if no output is focused.
---@return nnwm.Workspace?
function nnwm.current_workspace() end

--- Return a snapshot of the currently focused output, or `nil` if no output is focused.
---@return nnwm.Output?
function nnwm.current_output() end

---@alias nnwm.EventName
---| "startup"          # Fired once on the first event-loop tick. No argument.
---| "shutdown"         # Fired when the compositor is about to exit. No argument.
---| "window_focus"     # Fired when a window gains keyboard focus. Callback receives `nnwm.Window`.
---| "window_open"      # Fired when a window is mapped (appears on screen). Callback receives `nnwm.Window`.
---| "window_close"     # Fired when a window is unmapped (closes). Callback receives `nnwm.Window`.
---| "workspace_switch" # Fired when the active workspace changes. Callback receives `nnwm.Workspace`.
---| "output_connect"   # Fired when a new output (monitor) is connected. Callback receives `nnwm.Output`.
---| "lid_close"        # Fired when the laptop lid is closed. No argument.
---| "lid_open"         # Fired when the laptop lid is opened. No argument.
---| "tablet_mode_on"   # Fired when the device enters tablet mode. No argument.
---| "tablet_mode_off"  # Fired when the device leaves tablet mode. No argument.

--- Register a callback for a compositor event.
---
--- Multiple callbacks may be registered for the same event.
--- The callback is called with a snapshot table matching the event type.
---
---```lua
---nnwm.on("window_focus", function(win)
---    print("focused:", win.title)
---end)
---nnwm.on("workspace_switch", function(ws)
---    print("workspace", ws.index)
---end)
---```
---@param event   nnwm.EventName  Name of the event to listen for
---@param callback fun(data: nnwm.Window|nnwm.Workspace|nnwm.Output)
function nnwm.on(event, callback) end

--- Run `callback` once after `ms` milliseconds.
---@param ms       integer  Delay in milliseconds
---@param callback fun()    Function to call
function nnwm.timer(ms, callback) end

--- Run `callback` every `ms` milliseconds, repeating indefinitely.
---@param ms       integer  Interval in milliseconds
---@param callback fun()    Function to call
function nnwm.interval(ms, callback) end

--- Close the focused window.
function nnwm.close() end

--- Spawn a command via `/bin/sh -c`.
---@param cmd string  Shell command to execute
function nnwm.spawn(cmd) end

--- Spawn a command only if it has not been launched in this compositor session.
--- Safe to call on every config hot-reload — duplicates are silently ignored.
---@param cmd string  Shell command to execute
function nnwm.spawn_once(cmd) end

--- Increase the master column width by `master_ratio_step`, up to `master_ratio_max`.
function nnwm.master_ratio_grow() end

--- Decrease the master column width by `master_ratio_step`, down to `master_ratio_min`.
function nnwm.master_ratio_shrink() end

--- Focus the master window (first in the tiling list).
function nnwm.focus_left() end

--- Focus the first stack window (second in the tiling list).
function nnwm.focus_right() end

--- Focus the next tiled window, wrapping around.
function nnwm.focus_next() end

--- Focus the previous tiled window, wrapping around.
function nnwm.focus_prev() end

--- Focus the next floating window, wrapping around.
function nnwm.focus_next_float() end

--- Focus the previous floating window, wrapping around.
function nnwm.focus_prev_float() end

--- Toggle focus between the tiling and floating layers.
--- If a tiled window is focused, jumps to the first floating window, and vice versa.
function nnwm.focus_mode_toggle() end

---@alias nnwm.Direction "left"|"right"|"up"|"down"

--- Focus the nearest window in the given direction on the current output.
--- If no window exists in that direction, switches focus to the nearest monitor
--- in that direction (by center-to-center distance) and focuses its last active window.
---
--- Direction is determined geometrically by window center points:
--- - `"left"` / `"right"` — compare horizontal centers
--- - `"up"` / `"down"` — compare vertical centers
---
--- Ties on the primary axis are broken by perpendicular distance.
---
--- ```lua
--- nnwm.key({"Super", "h"}, function() nnwm.focus_dir("left")  end)
--- nnwm.key({"Super", "l"}, function() nnwm.focus_dir("right") end)
--- nnwm.key({"Super", "k"}, function() nnwm.focus_dir("up")    end)
--- nnwm.key({"Super", "j"}, function() nnwm.focus_dir("down")  end)
--- ```
---@param direction nnwm.Direction
function nnwm.focus_dir(direction) end

--- Move the focused window in the given direction.
--- On the same output: swaps the focused window with the nearest tiled window
--- in that direction (by center-to-center distance).
--- If no tiled window exists in that direction, moves the focused window to the
--- nearest monitor in that direction and focuses it there.
---
--- Floating windows are ignored for same-output swapping; cross-monitor move
--- works for all window types.
---
--- ```lua
--- nnwm.key({"Super", "Shift", "h"}, function() nnwm.move_dir("left")  end)
--- nnwm.key({"Super", "Shift", "l"}, function() nnwm.move_dir("right") end)
--- nnwm.key({"Super", "Shift", "k"}, function() nnwm.move_dir("up")    end)
--- nnwm.key({"Super", "Shift", "j"}, function() nnwm.move_dir("down")  end)
--- ```
---@param direction nnwm.Direction
function nnwm.move_dir(direction) end

--- Swap the focused window with the master window, preserving both positions.
--- No-op if the focused window is already master.
function nnwm.swap_master() end

--- Move the focused window to the master position.
function nnwm.swap_left() end

--- Move the focused window to the first stack position.
function nnwm.swap_right() end

--- Swap the focused window with the next window.
function nnwm.swap_next() end

--- Swap the focused window with the previous window.
function nnwm.swap_prev() end

--- Rotate the window list so the last window becomes master.
function nnwm.cycle() end

--- Switch to workspace `n` (1–9). Hides all windows on the current workspace
--- and shows all windows on the target workspace.
---@param n integer  Workspace index (1-indexed)
function nnwm.switch_workspace(n) end

--- Move the focused window to workspace `n` (1–9). The window is hidden
--- immediately and focus transfers to the next available window.
---@param n integer  Workspace index (1-indexed)
function nnwm.move_to_workspace(n) end

--- Toggle the focused window between sticky and normal.
--- A sticky window appears on every workspace: if tiled it participates in the
--- active workspace's layout; if floating it stays above all workspaces.
function nnwm.toggle_sticky() end

--- Toggle overview mode on the focused output.
--- Overview mode zooms out and shows all workspaces in a 3×3 grid.
--- Each workspace slot renders the tiled windows to scale as colored
--- rectangles with their titles. The active workspace is highlighted.
--- Clicking a slot switches to that workspace and exits overview.
--- Clicking outside any slot or pressing the toggle key again exits overview
--- without switching workspaces.
--- While in overview mode keyboard input is not forwarded to clients.
---
--- ```lua
--- nnwm.key({"Super"}, "w", function() nnwm.toggle_overview() end)
--- ```
function nnwm.toggle_overview() end

--- Move the focused window into the scratchpad.
--- The scratchpad is a global overlay workspace that can hold multiple windows
--- and supports tiling layouts. The window is removed from its current
--- workspace and placed in the scratchpad tree. If the scratchpad is currently
--- visible, the window is immediately tiled into it.
function nnwm.move_to_scratchpad() end

--- Toggle the scratchpad overlay on or off.
--- When shown, the scratchpad appears on top of the focused output with a
--- semi-transparent dim background. When hidden, focus returns to the
--- previously focused workspace window. While the scratchpad is visible,
--- layout toggle functions (e.g. `toggle_vertical_tile`, `layout_next`)
--- affect the scratchpad layout (HTILE/VTILE) instead of the active workspace.
function nnwm.scratchpad_toggle() end

--- Toggle the focused window between floating and tiled mode.
--- When made floating the window is centered on screen; when returned to tiled
--- the layout is re-arranged automatically.
function nnwm.toggle_float() end

--- Toggle the focused window between fullscreen and its previous mode.
--- When fullscreen the window covers the entire output with no borders or gaps;
--- exiting fullscreen restores the tiling layout.
function nnwm.toggle_fullscreen() end

--- Toggle the focused window between fake-fullscreen and its previous mode.
--- Like fullscreen, the window covers the entire output with no borders or
--- gaps, but the client is NOT told it is fullscreen (CSD decorations are
--- preserved). Useful for apps like OBS or Steam.
function nnwm.toggle_fake_fullscreen() end

--- Toggle the focused window between maximized and normal.
--- When maximized the window expands to fill the full usable area (respecting
--- borders and the titlebar) and is raised above other tiled windows.
--- Unlike fullscreen, borders and the titlebar remain visible.
function nnwm.toggle_maximize() end

--- Move keyboard focus to the next monitor (output), wrapping around.
--- Restores the last focused window on the target monitor.
function nnwm.focus_monitor_next() end

--- Move keyboard focus to the previous monitor (output), wrapping around.
--- Restores the last focused window on the target monitor.
function nnwm.focus_monitor_prev() end

--- Move the focused window to the next monitor's active workspace.
--- Focus follows the window to the destination monitor.
function nnwm.move_to_monitor_next() end

--- Move the focused window to the previous monitor's active workspace.
--- Focus follows the window to the destination monitor.
function nnwm.move_to_monitor_prev() end

-- ── nnwm.layout ──────────────────────────────────────────────────────────────

nnwm.layout        = {}
nnwm.layout.vtile  = {}
nnwm.layout.tabbed = {}
nnwm.layout.hscroll = {}
nnwm.layout.vscroll = {}

--- Toggle the current workspace between htile (horizontal master-stack, default)
--- and vtile (vertical master-stack: master on top, stack in a row below).
--- Toggling again restores htile.
---
--- ```lua
--- nnwm.key({"Super", "v"}, function() nnwm.layout.vtile.toggle() end)
--- ```
function nnwm.layout.vtile.toggle() end

--- Toggle the current workspace between tiled (master-stack) and tabbed layout.
--- In tabbed mode all tiled windows share the same content area; a composite tab
--- bar at the top shows each window's title and clicking a tab focuses that window.
---
--- ```lua
--- nnwm.key({"Super", "w"}, function() nnwm.layout.tabbed.toggle() end)
--- ```
function nnwm.layout.tabbed.toggle() end

--- Toggle the current workspace into horizontal-scroll layout.
--- Windows are arranged left-to-right, each occupying `scroll_column_width`
--- fraction of the output width. The viewport centers on the focused window.
---
--- ```lua
--- nnwm.key({"Super", "s"}, function() nnwm.layout.hscroll.toggle() end)
--- ```
function nnwm.layout.hscroll.toggle() end

--- Toggle the current workspace into vertical-scroll layout.
--- Windows are arranged top-to-bottom, each occupying `scroll_row_height`
--- fraction of the output height. The viewport centers on the focused window.
---
--- ```lua
--- nnwm.key({"Super", "Shift", "s"}, function() nnwm.layout.vscroll.toggle() end)
--- ```
function nnwm.layout.vscroll.toggle() end

--- Advance to the next layout for the active workspace, wrapping around.
--- Order: htile → vtile → tabbed → hscroll → vscroll → htile → …
---
--- ```lua
--- nnwm.key({"Super", "bracketright"}, function() nnwm.layout.next() end)
--- ```
function nnwm.layout.next() end

--- Go back to the previous layout for the active workspace, wrapping around.
--- Order: htile → vscroll → hscroll → tabbed → vtile → htile → … (reversed)
---
--- ```lua
--- nnwm.key({"Super", "bracketleft"}, function() nnwm.layout.prev() end)
--- ```
function nnwm.layout.prev() end
