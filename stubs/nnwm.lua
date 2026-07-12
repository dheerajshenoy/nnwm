---@meta "nnwm"

nnwm = nnwm or {}

---@class GapsOpt
---@field inner number The inner gap size between windows (default: 10)
---@field outer number The outer gap size between windows and the screen edge (default: 10)
---@field smart boolean Whether to enable smart gaps (default: true)

---@class BorderOpt
---@field width number The border width of windows (default: 2)
---@field focused_color string The border color of focused windows (default: "#ff0000")
---@field unfocused_color string The border color of unfocused windows (default: "#000000
---@field smart boolean Whether to enable smart borders (default: true)

---@class nnwm.opt
---@field gaps GapsOpt
---@field border BorderOpt

--- Quit the window manager
nnwm.quit = function () end

--- Close the currently focused window
nnwm.close = function () end

