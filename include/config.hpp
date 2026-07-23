
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

enum class nnwm_bar_module_type
{
    WORKSPACES = 0,
    WINDOW_TITLE,
    CLOCK,
    LAYOUT,
    CUSTOM,
    TRAY,
};

enum class nnwm_bar_align
{
    LEFT = 0,
    CENTER,
    RIGHT,
};

struct nnwm_bar_module
{
    nnwm_bar_module_type type;
    nnwm_bar_align align;
    char *name;           /* owned; non-empty when registered via
                             nnwm.bar.module(name, def) or when the inline
                             table sets a `name` field. Used to look up the
                             module from nnwm.bar.update(name). */
    char *format;         /* CLOCK: strftime; CUSTOM: unused */
    int lua_update_ref;   /* CUSTOM: Lua function returning string; -1 = unset */
    int interval_ms;      /* CUSTOM: poll interval; <=0 = event-only */
    float fg[4];          /* module-level foreground; a<0 = inherit bar fg */
    float bg[4];          /* module-level background; a<0 = transparent */
    int padding;          /* horizontal padding inside module; <0 = inherit */

    /* Per-module workspace palette (WORKSPACES only). Alpha<0 = inherit
     * from the bar-level color of the same role. */
    float ws_active_bg[4];
    float ws_active_fg[4];
    float ws_occupied_fg[4];
    float ws_unoccupied_fg[4];

    /* Lua handlers (LUA_REGISTRYINDEX refs; -1 = unset). */
    int lua_click_ref;    /* on_click = fn(button:string, lx:int, ly:int) */
    int lua_hover_ref;    /* on_hover = fn(entered:bool) */

    /* Per-module font overrides. Any field left null/0 inherits from the
     * bar-level font. If `font` is set, it fully replaces the bar font
     * description (parsed via pango_font_description_from_string); the
     * three atomic fields (style/weight/size) are then applied on top of
     * whichever base was chosen. */
    char *font;    /* full Pango font description, e.g. "Berkeley Mono Bold 12" */
    char *style;   /* "normal" | "italic" | "oblique" */
    char *weight;  /* "normal" | "bold" | "light" | numeric string 100-1000 */
    int   size;    /* point size; <=0 = inherit */

    /* Runtime cache — not part of config semantics, updated during render. */
    char *cached_text;    /* last text; owned */
    double cached_ts;     /* CLOCK: last strftime time; CUSTOM: last poll time */

    /* Last rendered bounding box in bar-local coords. w<=0 means the
     * module rendered nothing this frame. Refreshed in bar_redraw. */
    int rect_x, rect_y, rect_w, rect_h;
};

struct nnwm_bar_config
{
    bool enabled;
    bool position_top;    /* true = top, false = bottom */
    int height;           /* pixels */
    bool per_output;      /* true = one bar per output; false = single bar */
    char *output_name;    /* per_output=false: which output; NULL = focused */
    float bg_color[4];          /* bar background */
    float fg_color[4];          /* default text color for all modules */
    char *font;           /* pango font description, e.g. "monospace 11" */
    /* Uniform opacity multiplier for the whole bar (background, text,
     * shadow). Multiplies into each color's alpha; text uses
     * wlr_scene_buffer_set_opacity when built with scenefx. Range
     * 0.0-1.0; values <0 mean "unset/1.0". */
    float opacity;
    /* Outer margin around the bar (CSS-style). Creates a floating panel
     * when non-zero: the bar's rendered rect shrinks by left+right and its
     * vertical position is offset by top/bottom. usable_area is reduced by
     * top + height + bottom on the anchored edge. */
    struct { int top, right, bottom, left; } padding;
    int module_spacing;   /* pixels between adjacent modules */

    nnwm_bar_module *modules;
    int module_count;

    /* Bar-level Lua handlers (fired when the cursor is over the bar but
     * not over any module). LUA_REGISTRYINDEX refs; -1 = unset. */
    int lua_click_ref;
    int lua_hover_ref;

    /* scenefx effects for the bar. Only take effect when built with
     * USE_SCENEFX=ON. Everything defaults to off/0 so behavior is
     * unchanged unless the user opts in. */
    struct {
        int corner_radius;        /* pixels; 0 = square corners */
        bool shadow_enabled;
        float shadow_blur_sigma;
        float shadow_offset_x;
        float shadow_offset_y;
        float shadow_color[4];
        bool blur_enabled;        /* backdrop blur behind the bar */
    } fx;
};

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

    /* Per-workspace default layout for this monitor; -1 = inherit global */
    int workspace_layouts[9];

    /* Per-workspace name override for this monitor; NULL = inherit global */
    char *workspace_names[9];
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
        bool tab_smart;                 /* tabbed layout: hide tab bar when only one window */

        /* Layouts included in the layout::next/prev cycle, in order. Values
         * are nnwm_layout_mode ints (cast to int for storage). NULL/0 = use
         * the built-in default (all six layouts in enum order). set_layout()
         * ignores this and can jump to any layout regardless. */
        int *enabled_layouts;
        int enabled_layouts_count;
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
        float urgent_color[4];
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
        char *xkb_file; /* path to a compiled XKB keymap file; overrides rules/layout/variant/options */
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
    bool focus_on_activate;
    bool workspace_back_and_forth; /* switching to the active workspace jumps
                                      to the previously visited one instead */

    /* Layout behaviour */
    bool new_window_master;    /* true = new window becomes master, false =
                                  appended to stack */
    float scroll_column_width; /* fraction of output width per column in hscroll
                                  layout (0.0–1.0) */
    float scroll_row_height;   /* fraction of output height per row in vscroll
                                  layout (0.0–1.0) */
    bool center_new_floating;  /* center new floating windows on the output when
                                  they first map */

    /* Config error overlay (dangerous to disable — you won't see load errors) */
    bool show_config_error_overlay;

    /* find_cursor animation style: "rings" (concentric shrinking rings) or
     * "spotlight" (full-screen dim with circular cutout) */
    char *find_cursor_style;

    /* Workspaces — count is inferred from the length of workspace_names */
    int workspace_count;            /* 1–NNWM_NUM_WORKSPACES; default 9 */
    char *workspace_names[9];       /* per-workspace label; NULL = use numeric index */
    int workspace_default_layouts[9]; /* default layout per workspace; -1 = htile */

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

    /* Compositor-drawn status bar */
    nnwm_bar_config bar;
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
