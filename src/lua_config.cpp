#include "lua_config.hpp"
#include "config.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <xkbcommon/xkbcommon.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* ---- helpers ---- */

static int
get_int_field(lua_State *L, const char *name, int dflt)
{
    lua_getfield(L, -1, name);
    int v = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : dflt;
    lua_pop(L, 1);
    return v;
}

static float
get_float_field(lua_State *L, const char *name, float dflt)
{
    lua_getfield(L, -1, name);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : dflt;
    lua_pop(L, 1);
    return v;
}

static bool
get_bool_field(lua_State *L, const char *name, bool dflt)
{
    lua_getfield(L, -1, name);
    bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : dflt;
    lua_pop(L, 1);
    return v;
}

static const char *
get_string_field(lua_State *L, const char *name, const char *dflt)
{
    lua_getfield(L, -1, name);
    const char *v = lua_isstring(L, -1) ? lua_tostring(L, -1) : dflt;
    lua_pop(L, 1);
    return v;
}

/* Read a 4-element Lua table {r, g, b, a} into a float[4]. */
static void
get_color_field(lua_State *L, const char *name, float out[4], float dflt[4])
{
    lua_getfield(L, -1, name);
    if (lua_istable(L, -1))
    {
        for (int i = 0; i < 4; i++)
        {
            lua_rawgeti(L, -1, i + 1);
            out[i] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : dflt[i];
            lua_pop(L, 1);
        }
    }
    else
    {
        for (int i = 0; i < 4; i++)
            out[i] = dflt[i];
    }
    lua_pop(L, 1);
}

/* Resolve a key name string to an xkb_keysym_t.
 * Accepts both single characters ("c") and XKB key names ("F1", "Return"). */
static xkb_keysym_t
resolve_keysym(const char *name)
{
    xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
    if (sym == XKB_KEY_NoSymbol)
        sym = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
    return sym;
}

/* Read a keybinding table {mods_int, "key_name"} or {mods_int, keysym_int}. */
static struct nnwm_keybinding
get_keybinding_field(lua_State *L, const char *name,
                     uint32_t dflt_mods, xkb_keysym_t dflt_keysym)
{
    struct nnwm_keybinding kb = { dflt_mods, dflt_keysym };
    lua_getfield(L, -1, name);
    if (lua_istable(L, -1))
    {
        lua_rawgeti(L, -1, 1);
        if (lua_isinteger(L, -1))
            kb.mods = (uint32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_rawgeti(L, -1, 2);
        if (lua_isstring(L, -1))
            kb.keysym = resolve_keysym(lua_tostring(L, -1));
        else if (lua_isinteger(L, -1))
            kb.keysym = (xkb_keysym_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return kb;
}

/* ---- MOD and KEY constant tables ---- */

static const struct { const char *name; int value; } mod_entries[] = {
    { "Shift",  WLR_MODIFIER_SHIFT },
    { "Caps",   WLR_MODIFIER_CAPS },
    { "Ctrl",   WLR_MODIFIER_CTRL },
    { "Alt",    WLR_MODIFIER_ALT },
    { "Mod2",   WLR_MODIFIER_MOD2 },
    { "Mod3",   WLR_MODIFIER_MOD3 },
    { "Super",  WLR_MODIFIER_LOGO },
    { "Mod5",   WLR_MODIFIER_MOD5 },
    { nullptr, 0 },
};

static void
push_mod_table(lua_State *L)
{
    lua_newtable(L);
    for (int i = 0; mod_entries[i].name != nullptr; i++)
    {
        lua_pushinteger(L, mod_entries[i].value);
        lua_setfield(L, -2, mod_entries[i].name);
    }
    lua_setglobal(L, "MOD");
}

/* Push a minimal KEY table with common keys. */
static void
push_key_table(lua_State *L)
{
    lua_newtable(L);

    /* Letters a-z */
    for (char c = 'a'; c <= 'z'; c++)
    {
        char buf[2] = { c, '\0' };
        xkb_keysym_t sym = xkb_keysym_from_name(buf, XKB_KEYSYM_NO_FLAGS);
        if (sym != XKB_KEY_NoSymbol)
        {
            lua_pushinteger(L, sym);
            lua_setfield(L, -2, buf);
        }
    }

    /* Function keys F1-F12 */
    for (int i = 1; i <= 12; i++)
    {
        char name[8];
        std::snprintf(name, sizeof(name), "F%d", i);
        xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
        if (sym != XKB_KEY_NoSymbol)
        {
            lua_pushinteger(L, sym);
            lua_setfield(L, -2, name);
        }
    }

    /* Common special keys */
    struct { const char *name; const char *xkb; } special[] = {
        { "Return",  "Return" },
        { "Space",   "space" },
        { "Tab",     "Tab" },
        { "Escape",  "Escape" },
        { "Backspace", "BackSpace" },
        { "Delete",  "Delete" },
        { "Up",      "Up" },
        { "Down",    "Down" },
        { "Left",    "Left" },
        { "Right",   "Right" },
        { nullptr, nullptr },
    };
    for (int i = 0; special[i].name != nullptr; i++)
    {
        xkb_keysym_t sym = xkb_keysym_from_name(special[i].xkb, XKB_KEYSYM_NO_FLAGS);
        if (sym != XKB_KEY_NoSymbol)
        {
            lua_pushinteger(L, sym);
            lua_setfield(L, -2, special[i].name);
        }
    }

    lua_setglobal(L, "KEY");
}

/* ---- push defaults into the Lua config table ---- */

static void
push_config_defaults(lua_State *L, struct nnwm_config *cfg)
{
    lua_newtable(L);

    lua_pushnumber(L, cfg->master_ratio);
    lua_setfield(L, -2, "master_ratio");

    lua_pushnumber(L, cfg->master_ratio_step);
    lua_setfield(L, -2, "master_ratio_step");

    lua_pushnumber(L, cfg->master_ratio_min);
    lua_setfield(L, -2, "master_ratio_min");

    lua_pushnumber(L, cfg->master_ratio_max);
    lua_setfield(L, -2, "master_ratio_max");

    lua_pushinteger(L, cfg->border_width);
    lua_setfield(L, -2, "border_width");

    /* color arrays */
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->focused_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "focused_color");

    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->unfocused_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "unfocused_color");

    lua_pushinteger(L, cfg->keyboard_repeat_rate);
    lua_setfield(L, -2, "keyboard_repeat_rate");

    lua_pushinteger(L, cfg->keyboard_repeat_delay);
    lua_setfield(L, -2, "keyboard_repeat_delay");

    lua_pushstring(L, cfg->cursor_theme);
    lua_setfield(L, -2, "cursor_theme");

    lua_pushinteger(L, cfg->cursor_size);
    lua_setfield(L, -2, "cursor_size");

    lua_pushstring(L, cfg->seat_name);
    lua_setfield(L, -2, "seat_name");

    lua_pushboolean(L, cfg->touchpad_tap_to_click);
    lua_setfield(L, -2, "touchpad_tap_to_click");

    lua_pushboolean(L, cfg->touchpad_natural_scroll);
    lua_setfield(L, -2, "touchpad_natural_scroll");

    lua_pushboolean(L, cfg->touchpad_disable_while_typing);
    lua_setfield(L, -2, "touchpad_disable_while_typing");

    lua_pushstring(L, cfg->launcher_command);
    lua_setfield(L, -2, "launcher_command");

    /* keybindings as {mods, keysym_or_name} tables */
    auto push_kb = [&](const char *name, struct nnwm_keybinding *kb) {
        lua_newtable(L);
        lua_pushinteger(L, kb->mods);
        lua_rawseti(L, -2, 1);
        lua_pushinteger(L, kb->keysym);
        lua_rawseti(L, -2, 2);
        lua_setfield(L, -2, name);
    };
    push_kb("key_quit",         &cfg->key_quit);
    push_kb("key_close",        &cfg->key_close);
    push_kb("key_launcher",     &cfg->key_launcher);
    push_kb("key_promote_next", &cfg->key_promote_next);
    push_kb("key_promote_prev", &cfg->key_promote_prev);
    push_kb("key_shrink_master", &cfg->key_shrink_master);
    push_kb("key_grow_master",   &cfg->key_grow_master);
    push_kb("key_cycle_windows", &cfg->key_cycle_windows);

    lua_setglobal(L, "config");
}

/* ---- read back from Lua config table into C struct ---- */

static void
read_config_table(lua_State *L, struct nnwm_config *cfg)
{
    lua_getglobal(L, "config");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }

    cfg->master_ratio      = get_float_field(L, "master_ratio", cfg->master_ratio);
    cfg->master_ratio_step = get_float_field(L, "master_ratio_step", cfg->master_ratio_step);
    cfg->master_ratio_min  = get_float_field(L, "master_ratio_min", cfg->master_ratio_min);
    cfg->master_ratio_max  = get_float_field(L, "master_ratio_max", cfg->master_ratio_max);
    cfg->border_width      = get_int_field(L, "border_width", cfg->border_width);

    float dflt_foc[4] = { cfg->focused_color[0], cfg->focused_color[1],
                           cfg->focused_color[2], cfg->focused_color[3] };
    get_color_field(L, "focused_color", cfg->focused_color, dflt_foc);

    float dflt_unf[4] = { cfg->unfocused_color[0], cfg->unfocused_color[1],
                           cfg->unfocused_color[2], cfg->unfocused_color[3] };
    get_color_field(L, "unfocused_color", cfg->unfocused_color, dflt_unf);

    cfg->keyboard_repeat_rate  = get_int_field(L, "keyboard_repeat_rate",
                                               cfg->keyboard_repeat_rate);
    cfg->keyboard_repeat_delay = get_int_field(L, "keyboard_repeat_delay",
                                               cfg->keyboard_repeat_delay);
    cfg->cursor_theme = get_string_field(L, "cursor_theme", cfg->cursor_theme);
    cfg->cursor_size  = get_int_field(L, "cursor_size", cfg->cursor_size);
    cfg->seat_name    = get_string_field(L, "seat_name", cfg->seat_name);

    cfg->touchpad_tap_to_click = get_bool_field(L, "touchpad_tap_to_click",
                                                 cfg->touchpad_tap_to_click);
    cfg->touchpad_natural_scroll = get_bool_field(L, "touchpad_natural_scroll",
                                                   cfg->touchpad_natural_scroll);
    cfg->touchpad_disable_while_typing = get_bool_field(L, "touchpad_disable_while_typing",
                                                         cfg->touchpad_disable_while_typing);
    cfg->launcher_command = get_string_field(L, "launcher_command",
                                             cfg->launcher_command);

    cfg->key_quit         = get_keybinding_field(L, "key_quit",
                                cfg->key_quit.mods, cfg->key_quit.keysym);
    cfg->key_close        = get_keybinding_field(L, "key_close",
                                cfg->key_close.mods, cfg->key_close.keysym);
    cfg->key_launcher     = get_keybinding_field(L, "key_launcher",
                                cfg->key_launcher.mods, cfg->key_launcher.keysym);
    cfg->key_promote_next = get_keybinding_field(L, "key_promote_next",
                                cfg->key_promote_next.mods, cfg->key_promote_next.keysym);
    cfg->key_promote_prev = get_keybinding_field(L, "key_promote_prev",
                                cfg->key_promote_prev.mods, cfg->key_promote_prev.keysym);
    cfg->key_shrink_master = get_keybinding_field(L, "key_shrink_master",
                                cfg->key_shrink_master.mods, cfg->key_shrink_master.keysym);
    cfg->key_grow_master   = get_keybinding_field(L, "key_grow_master",
                                cfg->key_grow_master.mods, cfg->key_grow_master.keysym);
    cfg->key_cycle_windows = get_keybinding_field(L, "key_cycle_windows",
                                cfg->key_cycle_windows.mods, cfg->key_cycle_windows.keysym);

    lua_pop(L, 1);
}

/* ---- public API ---- */

extern "C" struct nnwm_config *
nnwm_config_load(const char *path)
{
    struct nnwm_config *cfg = nnwm_config_defaults();

    lua_State *L = luaL_newstate();
    if (!L)
    {
        std::fprintf(stderr, "nnwm: failed to create Lua state\n");
        return cfg;
    }

    luaL_openlibs(L);

    /* Push MOD and KEY constant tables */
    push_mod_table(L);
    push_key_table(L);

    /* Push the config table pre-populated with defaults */
    push_config_defaults(L, cfg);

    /* Execute the user's config file */
    if (luaL_dofile(L, path) != LUA_OK)
    {
        std::fprintf(stderr, "nnwm: config error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_close(L);
        return cfg;
    }

    /* Read back any values the script changed */
    read_config_table(L, cfg);

    lua_close(L);
    return cfg;
}
