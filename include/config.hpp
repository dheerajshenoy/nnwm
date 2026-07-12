
#ifndef NNWM_CONFIG_HPP
#define NNWM_CONFIG_HPP

#ifdef __cplusplus
extern "C"
{
#endif
#include <cstdint>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_seat.h>
#ifdef __cplusplus
}
#endif

struct nnwm_keybinding
{
    uint32_t    mods;   /* WLR_MODIFIER_* mask */
    xkb_keysym_t keysym;
};

struct nnwm_config
{
    /* Layout */
    float master_ratio;
    float master_ratio_step;
    float master_ratio_min;
    float master_ratio_max;

    /* Borders */
    int   border_width;
    float focused_color[4];
    float unfocused_color[4];

    /* Keyboard */
    int keyboard_repeat_rate;
    int keyboard_repeat_delay;

    /* Cursor */
    char *cursor_theme;
    int   cursor_size;

    /* Seat */
    char *seat_name;

    /* Input (libinput) */
    bool touchpad_tap_to_click;
    bool touchpad_natural_scroll;
    bool touchpad_disable_while_typing;

    /* Launcher */
    char *launcher_command;

    /* Keybindings */
    struct nnwm_keybinding key_quit;         /* Super+Shift+C */
    struct nnwm_keybinding key_close;        /* Super+Shift+Q */
    struct nnwm_keybinding key_launcher;     /* Super+P */
    struct nnwm_keybinding key_promote_next; /* Super+J */
    struct nnwm_keybinding key_promote_prev; /* Super+K */
    struct nnwm_keybinding key_shrink_master; /* Super+H */
    struct nnwm_keybinding key_grow_master;   /* Super+L */
    struct nnwm_keybinding key_cycle_windows; /* Alt+F1 */
};

#ifdef __cplusplus
extern "C"
{
#endif
struct nnwm_config *nnwm_config_defaults(void);
void nnwm_config_free(struct nnwm_config *cfg);
#ifdef __cplusplus
}
#endif

#endif /* NNWM_CONFIG_HPP */
