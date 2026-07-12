
#ifndef NNWM_CONFIG_HPP
#define NNWM_CONFIG_HPP

#ifdef __cplusplus
extern "C"
{
#endif
#include <cstdint>
#ifdef __cplusplus
}
#endif

struct nnwm_config
{
    /* Layout */
    float master_ratio;
    float master_ratio_step;
    float master_ratio_min;
    float master_ratio_max;

    /* Gaps */
    int inner_gap;
    int outer_gap;
    bool smart_gaps;
    bool smart_borders;

    /* Borders */
    int border_width;
    float focused_color[4];
    float unfocused_color[4];

    /* Keyboard */
    int keyboard_repeat_rate;
    int keyboard_repeat_delay;
    char *xkb_options;

    /* Cursor */
    char *cursor_theme;
    int cursor_size;

    /* Seat */
    char *seat_name;

    /* Input (libinput) */
    bool touchpad_tap_to_click;
    bool touchpad_natural_scroll;
    bool touchpad_disable_while_typing;

    /* Focus */
    bool focus_follows_mouse;
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
