#pragma once
#include <cstdint>

struct nnwm_server;

// Initialize the D-Bus system tray watcher/host. Called once at startup.
void tray_init(nnwm_server *server);

// Tear down the tray. Called at shutdown.
void tray_destroy(nnwm_server *server);

// Measure the total width of visible tray icons for layout pass.
// icon_spacing = pixels between icons.
int tray_measure_width(nnwm_server *server, int bar_h, int icon_spacing);

// Draw tray icons at bar-local x position. cr is cairo_t*.
// Returns the total width drawn (same as tray_measure_width).
int tray_draw(nnwm_server *server, void *cr, int bar_h, int x, int icon_spacing);

// Handle a click at bar-local position bx (over the tray module area).
// bar_origin_x/y are compositor coords of the bar's top-left corner.
// button is a linux BTN_* code. Returns true if consumed.
bool tray_handle_click(nnwm_server *server, double bx, int bar_h, int x_start,
                       int bar_origin_x, int bar_origin_y, uint32_t button);

// Generation counter — increments on any tray content change.
// Include in bar hash so the bar redraws when items change.
uint32_t tray_generation(nnwm_server *server);
