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

-- ── KEY ──────────────────────────────────────────────────────────────────────

---@class KEY
---@field a integer
---@field b integer
---@field c integer
---@field d integer
---@field e integer
---@field f integer
---@field g integer
---@field h integer
---@field i integer
---@field j integer
---@field k integer
---@field l integer
---@field m integer
---@field n integer
---@field o integer
---@field p integer
---@field q integer
---@field r integer
---@field s integer
---@field t integer
---@field u integer
---@field v integer
---@field w integer
---@field x integer
---@field y integer
---@field z integer
---@field F1  integer
---@field F2  integer
---@field F3  integer
---@field F4  integer
---@field F5  integer
---@field F6  integer
---@field F7  integer
---@field F8  integer
---@field F9  integer
---@field F10 integer
---@field F11 integer
---@field F12 integer
---@field Return    integer
---@field Space     integer
---@field Tab       integer
---@field Escape    integer
---@field Backspace integer
---@field Delete    integer
---@field Up        integer
---@field Down      integer
---@field Left      integer
---@field Right     integer
KEY = {}

-- ── nnwm ─────────────────────────────────────────────────────────────────────

---@class nnwm.layout
---@field new_window_master?  boolean  When true new windows become master; when false they append to the stack (default: true)
---@field master_ratio?       number   Fraction of screen width for the master column (default: 0.55)
---@field master_ratio_step?  number   Step size for master ratio adjustments (default: 0.05)
---@field master_ratio_min?   number   Minimum master ratio (default: 0.1)
---@field master_ratio_max?   number   Maximum master ratio (default: 0.9)

---@class nnwm.gaps
---@field inner?  integer  Gap in pixels between adjacent windows (default: 0)
---@field outer?  integer  Gap in pixels between windows and the screen edge (default: 0)
---@field smart?  boolean  Disable gaps when only one window is on screen (default: false)

---@class nnwm.border
---@field width?           integer   Border thickness in pixels (default: 2)
---@field smart?           boolean   Disable borders when only one window is on screen (default: false)
---@field focused_color?   number[]  RGBA color for the focused window border (default: {0.3, 0.5, 0.8, 1.0})
---@field unfocused_color? number[]  RGBA color for unfocused window borders (default: {0.15, 0.15, 0.15, 1.0})

---@class nnwm.keyboard
---@field repeat_rate?   integer  Key repeat rate in keys/sec (default: 25)
---@field repeat_delay?  integer  Delay before repeat starts in ms (default: 600)
---@field xkb_options?   string   Comma-separated XKB options, e.g. `"caps:escape,compose:ralt"` (default: "")

---@class nnwm.touchpad
---@field tap_to_click?         boolean  Enable tap-to-click (default: true)
---@field natural_scroll?       boolean  Natural/reverse scroll direction (default: true)
---@field disable_while_typing? boolean  Disable touchpad while typing (default: true)

---@class nnwm.mouse
---@field focus_follows_mouse?  boolean  Automatically focus the window under the cursor (default: false)
---@field cursor_theme?         string   Xcursor theme name (default: "default")
---@field cursor_size?          integer  Cursor size in pixels (default: 24)

---@class nnwm.titlebar
---@field enabled?          boolean   Enable the server-side titlebar (default: false)
---@field height?           integer   Titlebar height in pixels, used when enabled (default: 20)
---@field font?             string    Pango font description for the title text, e.g. `"Sans Bold 10"` (default: "Sans 10")
---@field text_align?       integer   Text alignment: 0 = left, 1 = center, 2 = right (default: 1)
---@field bg_color?         number[]  RGBA background color for unfocused windows (default: {0.2, 0.2, 0.2, 1.0})
---@field focused_bg_color? number[]  RGBA background color for the focused window (default: {0.25, 0.35, 0.55, 1.0})
---@field text_color?       number[]  RGBA color for the title text (default: {1.0, 1.0, 1.0, 1.0})

---@class nnwm_opts
---@field layout?             nnwm.layout
---@field gaps?               nnwm.gaps
---@field border?             nnwm.border
---@field keyboard?           nnwm.keyboard
---@field touchpad?           nnwm.touchpad
---@field mouse?              nnwm.mouse
---@field titlebar?           nnwm.titlebar
---@field client_decorations? boolean
---@field seat_name?          string
---@field monitors?           nnwm_monitor_config[]

---@class nnwm
---@field opt nnwm_opts

--- Monitor configuration (array of tables). First match wins.
---@class nnwm_window_rule_match
---@field app_id string? fnmatch glob matched against the window's app_id (e.g. `"firefox"`, `"foot*"`)
---@field title  string? fnmatch glob matched against the window title

---@class nnwm_window_rule_action
---@field floating   boolean? Make the window floating (true) or tiled (false)
---@field fullscreen boolean? Make the window fullscreen
---@field workspace  integer? Assign to workspace 1–9
---@field monitor    string?  Assign to the output with this name (e.g. `"DP-1"`)

---@class nnwm_monitor_config
---@field name      string   Output name, e.g. "DP-1"
---@field make      string   Manufacturer string
---@field model     string   Model string
---@field serial    string   Serial number string
---@field width     integer  Mode width in pixels
---@field height    integer  Mode height in pixels
---@field refresh   integer  Refresh rate in Hz
---@field x         integer  Layout X position (unset = auto)
---@field y         integer  Layout Y position (unset = auto)
---@field scale     number   Output scale factor (e.g. 2.0)
---@field transform string   Rotation: "none", "90", "180", "270", "flipped", "flipped-90", "flipped-180", "flipped-270"
---@field hdr       boolean  Enable HDR (wlroots 0.20+)
---@field disabled  boolean  Disable this output
nnwm = {}
nnwm.opt = {} ---@type nnwm_opts

---Register a keybinding. `combo` is an array of modifier and key name strings;
---`callback` is called when the combo is pressed.
---
---Modifier names: `"Super"`, `"Shift"`, `"Ctrl"`, `"Alt"`, `"Mod2"`, `"Mod3"`, `"Mod5"`, `"Caps"`.
---Exactly one non-modifier entry (an XKB key name) is required.
---
---```lua
---nnwm.key({"Super", "Return"}, function() nnwm.spawn("kitty") end)
---nnwm.key({"Super", "Shift", "q"}, function() nnwm.close() end)
---```
---@param combo    string[]  Array of modifier names and exactly one key name
---@param callback fun()     Function to call when the combo is pressed
function nnwm.key(combo, callback) end

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

--- Terminate the compositor.
function nnwm.quit() end

--- Return the hostname of the machine.
---@return string
function nnwm.host_name() end

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

--- Focus the next window, wrapping around.
function nnwm.focus_next() end

--- Focus the previous window, wrapping around.
function nnwm.focus_prev() end

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

--- Toggle the focused window between floating and tiled mode.
--- When made floating the window is centered on screen; when returned to tiled
--- the layout is re-arranged automatically.
function nnwm.toggle_float() end

--- Toggle the focused window between fullscreen and its previous mode.
--- When fullscreen the window covers the entire output with no borders or gaps;
--- exiting fullscreen restores the tiling layout.
function nnwm.toggle_fullscreen() end

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
