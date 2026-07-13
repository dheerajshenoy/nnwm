
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

    /* Layout behaviour */
    bool new_window_master; /* true = new window becomes master, false = appended to stack */

    /* Decorations */
    bool client_decorations; /* true = CSD (client draws titlebar), false = SSD (no titlebar) */

    /* Titlebar (server-side, drawn by compositor) */
    int   titlebar_height;             /* pixels; 0 = disabled */
    char *titlebar_font;               /* pango font description, e.g. "Sans Bold 10" */
    int   titlebar_text_align;         /* 0 = left, 1 = center, 2 = right */
    float titlebar_bg_color[4];        /* unfocused background RGBA */
    float titlebar_focused_bg_color[4];/* focused background RGBA */
    float titlebar_text_color[4];      /* text RGBA */
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
