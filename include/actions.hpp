
#ifndef NNWM_ACTIONS_HPP
#define NNWM_ACTIONS_HPP

#ifdef __cplusplus

struct nnwm_server;

namespace nnwm
{

void
quit(nnwm_server *server);
void
close(nnwm_server *server);
void
spawn(nnwm_server *server, const char *cmd);
void
spawn_once(nnwm_server *server, const char *cmd);
void
cycle(nnwm_server *server);
void
flush_autostart(nnwm_server *server);

namespace focus
{
void
left(nnwm_server *server);
void
right(nnwm_server *server);
void
next(nnwm_server *server);
void
prev(nnwm_server *server);
void
next_float(nnwm_server *server);
void
prev_float(nnwm_server *server);
void
mode_toggle(nnwm_server *server);
} // namespace focus

namespace swap
{
void
left(nnwm_server *server);
void
right(nnwm_server *server);
void
next(nnwm_server *server);
void
prev(nnwm_server *server);
void
master(nnwm_server *server);
} // namespace swap

namespace workspace
{
void
switch_to(nnwm_server *server, int ws);
void
move_to(nnwm_server *server, int ws);
} // namespace workspace

namespace monitor
{
void
focus_next(nnwm_server *server);
void
focus_prev(nnwm_server *server);
void
move_to_next(nnwm_server *server);
void
move_to_prev(nnwm_server *server);
} // namespace monitor

namespace layout
{
void
next(nnwm_server *server);
void
prev(nnwm_server *server);
void
toggle_vertical_tile(nnwm_server *server);
void
toggle_tabbed(nnwm_server *server);
void
toggle_horizontal_scroll(nnwm_server *server);
void
toggle_vertical_scroll(nnwm_server *server);
void
master_ratio_grow(nnwm_server *server);
void
master_ratio_shrink(nnwm_server *server);
} // namespace layout

namespace window
{
void
toggle_float(nnwm_server *server);
void
toggle_fullscreen(nnwm_server *server);
void
toggle_fake_fullscreen(nnwm_server *server);
void
toggle_sticky(nnwm_server *server);
} // namespace window

} // namespace nnwm

#endif /* __cplusplus */
#endif /* NNWM_ACTIONS_HPP */
