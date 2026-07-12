---@meta "nnwm"

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

---@class nnwm
--- Layout
---@field master_ratio       number  Fraction of screen width for the master column (default: 0.55)
---@field master_ratio_step  number  Step size for master ratio adjustments (default: 0.05)
---@field master_ratio_min   number  Minimum master ratio (default: 0.1)
---@field master_ratio_max   number  Maximum master ratio (default: 0.9)
--- Gaps
---@field inner_gap     integer  Gap in pixels between adjacent windows (default: 0)
---@field outer_gap     integer  Gap in pixels between windows and the screen edge (default: 0)
---@field smart_gaps    boolean  Disable gaps when only one window is on screen (default: false)
---@field smart_borders boolean  Disable borders when only one window is on screen (default: false)
--- Borders
---@field border_width        integer        Border thickness in pixels (default: 2)
---@field focused_color       number[]       RGBA color for the focused window border (default: {0.3, 0.5, 0.8, 1.0})
---@field unfocused_color     number[]       RGBA color for unfocused window borders (default: {0.15, 0.15, 0.15, 1.0})
--- Keyboard
---@field keyboard_repeat_rate  integer  Key repeat rate in keys/sec (default: 25)
---@field keyboard_repeat_delay integer  Delay before repeat starts in ms (default: 600)
--- Cursor
---@field cursor_theme  string   Xcursor theme name (default: "default")
---@field cursor_size   integer  Cursor size in pixels (default: 24)
--- Seat
---@field seat_name  string  Wayland seat name (default: "seat0")
--- Touchpad (libinput)
---@field touchpad_tap_to_click         boolean  Enable tap-to-click (default: true)
---@field touchpad_natural_scroll       boolean  Natural/reverse scroll direction (default: true)
---@field touchpad_disable_while_typing boolean  Disable touchpad while typing (default: true)
nnwm = {}

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

--- Terminate the compositor.
function nnwm.quit() end

--- Close the focused window.
function nnwm.close() end

--- Spawn a command via `/bin/sh -c`.
---@param cmd string  Shell command to execute
function nnwm.spawn(cmd) end

--- Focus the master window (first in the tiling list).
function nnwm.focus_left() end

--- Focus the first stack window (second in the tiling list).
function nnwm.focus_right() end

--- Focus the next window, wrapping around.
function nnwm.focus_next() end

--- Focus the previous window, wrapping around.
function nnwm.focus_prev() end

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
