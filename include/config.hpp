
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

enum class nnwm_tab_style
{
    NORMAL = 0,
    MINIMAL
};

enum class nnwm_tab_position
{
    TOP = 0,
    BOTTOM,
    LEFT,
    RIGHT,
};

#ifdef HAVE_SCENEFX
enum class nnwm_easing
{
    OUT = 0,
    LINEAR,
    IN,
    IN_OUT,
    BOUNCE,
    ELASTIC,
};

enum class nnwm_open_style
{
    FADE_SCALE = 0,
    FADE,
    SCALE,
    SLIDE_UP,
    SLIDE_DOWN,
    SLIDE_LEFT,
    SLIDE_RIGHT,
    NONE,
};

enum class nnwm_ws_style
{
    SLIDE = 0,
    FADE,
    NONE,
};

enum class nnwm_layout_anim
{
    TWEEN = 0,
    NONE,
};

enum class nnwm_focus_style
{
    CROSSFADE = 0,
    NONE,
};
#endif /* HAVE_SCENEFX */

struct nnwm_window_rule
{
    /* Match fields — NULL = not used for matching */
    char *app_id; /* fnmatch glob, e.g. "fire*" */
    char *title;  /* fnmatch glob */

    /* Actions — -1 / NULL = unset (don't apply) */
    int floating;        /* -1=unset, 0=false, 1=true */
    int fullscreen;      /* -1=unset, 0=false, 1=true */
    int fake_fullscreen; /* -1=unset, 0=false, 1=true */
    int maximize;        /* -1=unset, 0=false, 1=true */
    int focused;         /* -1=unset, 1=focus+switch workspace on map */
    int sticky;          /* -1=unset, 0=false, 1=true */
    int workspace;       /* -1=unset, 0-8 = workspace index */
    char *monitor;       /* NULL=unset, output name e.g. "DP-1" */
    float opacity;       /* <0 = unset (use global); 0.0–1.0 = override */
    int blur;            /* -1=unset, 0=disable, 1=enable */
#ifdef HAVE_SCENEFX
    int anim_open_style;  /* -1=unset, else nnwm_open_style */
    int anim_close_style; /* -1=unset, else nnwm_open_style */
    int no_anim;          /* -1=unset, 1=disable animations for this window */
#endif                    /* HAVE_SCENEFX */
};

struct nnwm_monitor_config
{
    /* Match fields (all optional; NULL = don't match on this field) */
    char *name;        /* output name, e.g. "DP-1" */
    char *description; /* "make model serial" combined string */

    /* Mode */
    int width, height; /* pixels; 0 = use preferred mode */
    int refresh;       /* Hz; 0 = use preferred mode */

    /* Position */
    int x, y; /* INT_MAX = unset (auto-position) */

    /* Scale */
    float scale; /* <= 0 = default (1.0) */

    /* Transform: -1 = default, otherwise wl_output_transform enum value */
    int transform;

    bool hdr;      /* enable HDR (BT.2020 + ST.2084 PQ); wlroots 0.20+ */
    bool disabled; /* disable this output entirely */

    /* Reserved pixels on each edge, applied after layer-shell exclusive zones */
    int strut_top, strut_bottom, strut_left, strut_right;
};

struct nnwm_config
{
    /* Layout */
    struct layout
    {
        float master_ratio;
        float master_ratio_step;
        float master_ratio_min;
        float master_ratio_max;
        nnwm_tab_style tab_style;       /* tabbed layout: normal or minimal */
        nnwm_tab_position tab_position; /* tabbed layout: tab bar placement */
        int tab_bar_height;             /* tabbed layout: bar thickness in pixels */
    } layout;

    /* Gaps */
    struct gap
    {
        int inner;
        int outer;
        bool smart; /* true = disable gaps when only one window is visible */
    } gap;

    /* Borders */
    struct border
    {
        int width;
        float focused_color[4];
        float unfocused_color[4];
        bool smart;
    } border;

    /* Keyboard */
    struct keyboard
    {
        int repeat_rate;
        int repeat_delay;
        char *xkb_rules;
        char *xkb_layout;
        char *xkb_variant;
        char *xkb_options;
    } keyboard;

    /* Cursor */
    char *cursor_theme;
    int cursor_size;

    /* Seat */
    char *seat_name;

    /* Input (libinput) */
    struct touchpad
    {
        bool enabled;
        bool tap_to_click;
        bool drag;
        bool natural_scroll;
        bool disable_while_typing;
        bool disable_on_external_mouse;
        float scroll_factor;
        int scroll_method; /* 0=no_scroll, 1=two_finger, 2=edge, 3=on_button */
    } touchpad;

    struct mouse
    {
        float accel_speed;              /* pointer acceleration speed: -1.0–1.0 */
        int accel_profile;              /* 0=adaptive, 1=flat, 2=none */
        bool natural_scroll;
        bool disable_while_typing;
        bool hide_cursor_when_typing;
        bool warp_to_focused_window;
    } mouse;

    /* Focus */
    bool focus_follows_mouse;

    /* Layout behaviour */
    bool new_window_master;    /* true = new window becomes master, false =
                                  appended to stack */
    float scroll_column_width; /* fraction of output width per column in hscroll
                                  layout (0.0–1.0) */
    float scroll_row_height;   /* fraction of output height per row in vscroll
                                  layout (0.0–1.0) */
    bool center_new_floating;  /* center new floating windows on the output when
                                  they first map */

    /* Decorations */
    bool clipboard_enabled;
    bool client_decorations; /* true = CSD (client draws titlebar), false = SSD
                                (no titlebar) */

    /* Titlebar (server-side, drawn by compositor) */
    struct titlebar
    {
        int height;
        char *font;
        int text_align;              /* 0 = left, 1 = center, 2 = right */
        float bg_color[4];           /* unfocused background RGBA */
        float focused_bg_color[4];   /* focused background RGBA */
        float urgent_bg_color[4];    /* urgent window background RGBA */
        float text_color[4];         /* unfocused text RGBA */
        float focused_text_color[4]; /* focused text RGBA */
        float urgent_text_color[4];  /* urgent window text RGBA */
    } titlebar;

    struct fx
    {
        /* scenefx: corner radius, shadows, blur, opacity (requires HAVE_SCENEFX
         * build flag) */
        struct rounding
        {
            int radius; /* pixels; 0 = disabled */
            bool smart; /* true = collapse corner radius when only one window
                           is visible */
        } rounding;
        bool shadow_enabled;
        float shadow_blur_sigma; /* Gaussian sigma; controls softness */
        float shadow_color[4];   /* RGBA */
        float shadow_offset_x;   /* pixels */
        float shadow_offset_y;   /* pixels */
        float opacity; /* window content opacity: 0.0–1.0 (default: 1.0) */
        float focused_opacity;   /* <0 = inherit opacity; 0.0–1.0 = override */
        float unfocused_opacity; /* <0 = inherit opacity; 0.0–1.0 = override */
        bool blur_enabled;
        int blur_passes;       /* number of dual-kawase passes (default: 3) */
        int blur_radius;       /* blur radius in pixels (default: 5) */
        float blur_noise;      /* noise to reduce banding (default: 0.0) */
        float blur_brightness; /* brightness adjustment (default: 1.0) */
        float blur_contrast;   /* contrast adjustment (default: 1.0) */
        float blur_saturation; /* saturation adjustment (default: 1.0) */

#ifdef HAVE_SCENEFX
        /* Animation */
        struct animation
        {
            bool enabled;
            int duration_ms;
            nnwm_easing easing; /* global default easing */
            nnwm_open_style open_style;
            nnwm_open_style close_style;
            nnwm_ws_style ws_style;
            nnwm_layout_anim layout_style;
            nnwm_focus_style focus_style;

            /* Per-type easing (-1 = inherit global) */
            int open_easing;
            int close_easing;
            int ws_easing;
            int layout_easing;
            int focus_easing;

            /* Per-type duration in ms (0 = inherit global) */
            int open_duration_ms;
            int close_duration_ms;
            int ws_duration_ms;
            int layout_duration_ms;
            int focus_duration_ms;
        } animation;
    } fx;
#else
    } fx;
#endif /* HAVE_SCENEFX */

    /* Monitor configuration */
    nnwm_monitor_config *monitor_configs;
    int monitor_config_count;

    /* Window rules */
    nnwm_window_rule *window_rules;
    int window_rule_count;
};

#ifdef __cplusplus
namespace nnwm
{
struct nnwm_config *
config_defaults(void);
void
config_free(struct nnwm_config *cfg);
} // namespace nnwm
#else
struct nnwm_config *
nnwm_config_defaults(void);
void
nnwm_config_free(struct nnwm_config *cfg);
#endif

#endif /* NNWM_CONFIG_HPP */
