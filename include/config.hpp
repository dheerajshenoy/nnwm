
#ifndef NNWM_CONFIG_HPP
#define NNWM_CONFIG_HPP

#ifdef __cplusplus
extern "C"
{
#endif
#include <climits>
#include <cstdint>
#ifdef __cplusplus
}
#endif

struct nnwm_window_rule
{
    /* Match fields — NULL = not used for matching */
    char *app_id; /* fnmatch glob, e.g. "fire*" */
    char *title;  /* fnmatch glob */

    /* Actions — -1 / NULL = unset (don't apply) */
    int   floating;   /* -1=unset, 0=false, 1=true */
    int   fullscreen; /* -1=unset, 0=false, 1=true */
    int   sticky;     /* -1=unset, 0=false, 1=true */
    int   workspace;  /* -1=unset, 0-8 = workspace index */
    char *monitor;    /* NULL=unset, output name e.g. "DP-1" */
};

struct nnwm_monitor_config
{
    /* Match fields (all optional; NULL = don't match on this field) */
    char *name;       /* output name, e.g. "DP-1" */
    char *make;       /* EDID manufacturer */
    char *model;      /* EDID model name */
    char *serial;     /* EDID serial number */

    /* Mode */
    int width, height;/* pixels; 0 = use preferred mode */
    int refresh;      /* Hz; 0 = use preferred mode */

    /* Position */
    int x, y;         /* INT_MAX = unset (auto-position) */

    /* Scale */
    float scale;      /* <= 0 = default (1.0) */

    /* Transform: -1 = default, otherwise wl_output_transform enum value */
    int transform;

    bool hdr;         /* enable HDR (BT.2020 + ST.2084 PQ); wlroots 0.20+ */
    bool disabled;    /* disable this output entirely */
};

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
    float titlebar_bg_color[4];           /* unfocused background RGBA */
    float titlebar_focused_bg_color[4];   /* focused background RGBA */
    float titlebar_text_color[4];         /* unfocused text RGBA */
    float titlebar_focused_text_color[4]; /* focused text RGBA */

    /* Monitor configuration */
    struct nnwm_monitor_config *monitor_configs;
    int monitor_config_count;

    /* Window rules */
    struct nnwm_window_rule *window_rules;
    int window_rule_count;
};

#ifdef __cplusplus
namespace nnwm {
    struct nnwm_config *config_defaults(void);
    void config_free(struct nnwm_config *cfg);
} // namespace nnwm
#else
struct nnwm_config *nnwm_config_defaults(void);
void nnwm_config_free(struct nnwm_config *cfg);
#endif

#endif /* NNWM_CONFIG_HPP */
