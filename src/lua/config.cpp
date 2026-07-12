#include "lua/config.hpp"
#include "config.hpp"
#include "nnwm.hpp"

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

static struct nnwm_server *
get_server(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "_nnwm_server");
    auto *server = static_cast<struct nnwm_server*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return server;
}

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

static char *
get_string_field(lua_State *L, const char *name, const char *dflt)
{
    lua_getfield(L, -1, name);
    const char *v = lua_isstring(L, -1) ? lua_tostring(L, -1) : dflt;
    char *r = v ? strdup(v) : nullptr;
    lua_pop(L, 1);
    return r;
}

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

static xkb_keysym_t
resolve_keysym(const char *name)
{
    xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
    if (sym == XKB_KEY_NoSymbol)
        sym = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
    return sym;
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

static bool
is_modifier_name(const char *name, uint32_t *out_mod)
{
    for (int i = 0; mod_entries[i].name != nullptr; i++)
    {
        if (strcmp(name, mod_entries[i].name) == 0)
        {
            *out_mod = (uint32_t)mod_entries[i].value;
            return true;
        }
    }
    return false;
}

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

static void
push_key_table(lua_State *L)
{
    lua_newtable(L);

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

/* ---- nnwm.key() C function ---- */

static int
l_nnwm_key(lua_State *L)
{
    if (lua_gettop(L) != 2 || !lua_istable(L, 1) || !lua_isfunction(L, 2))
        return luaL_error(L, "nnwm.key(combo_table, callback) expected");

    struct nnwm_server *server = get_server(L);

    uint32_t      mods  = 0;
    xkb_keysym_t  keysym = XKB_KEY_NoSymbol;

    int len = static_cast<int>(lua_rawlen(L, 1));
    for (int i = 1; i <= len; i++)
    {
        lua_rawgeti(L, 1, i);
        const char *name = lua_tostring(L, -1);
        if (!name)
        {
            lua_pop(L, 1);
            return luaL_error(L, "nnwm.key: combo table entries must be strings");
        }

        uint32_t mod;
        if (is_modifier_name(name, &mod))
        {
            mods |= mod;
        }
        else
        {
            if (keysym != XKB_KEY_NoSymbol)
            {
                lua_pop(L, 1);
                return luaL_error(L, "nnwm.key: multiple key names in combo");
            }
            keysym = resolve_keysym(name);
            if (keysym == XKB_KEY_NoSymbol)
            {
                lua_pop(L, 1);
                return luaL_error(L, "nnwm.key: unknown key '%s'", name);
            }
        }
        lua_pop(L, 1);
    }

    if (keysym == XKB_KEY_NoSymbol)
        return luaL_error(L, "nnwm.key: no key name in combo");

    /* Store callback in Lua registry */
    lua_pushvalue(L, 2);
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* Grow keybinding array if needed */
    if (server->lua_keybinding_count >= server->lua_keybinding_cap)
    {
        server->lua_keybinding_cap = server->lua_keybinding_cap
            ? server->lua_keybinding_cap * 2 : 16;
        server->lua_keybindings = static_cast<struct nnwm_lua_keybinding*>(
            std::realloc(server->lua_keybindings,
                         sizeof(struct nnwm_lua_keybinding)
                         * server->lua_keybinding_cap));
    }

    auto &kb = server->lua_keybindings[server->lua_keybinding_count++];
    kb.mods    = mods;
    kb.keysym  = keysym;
    kb.func_ref = func_ref;

    return 0;
}

/* ---- Lua action wrappers ---- */

static int
l_nnwm_quit(lua_State *L)
{
    nnwm_action_quit(get_server(L));
    return 0;
}

static int
l_nnwm_close(lua_State *L)
{
    nnwm_action_close(get_server(L));
    return 0;
}

static int
l_nnwm_spawn(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    nnwm_action_spawn(cmd);
    return 0;
}

static int
l_nnwm_focus_left(lua_State *L)
{
    nnwm_action_focus_left(get_server(L));
    return 0;
}

static int
l_nnwm_focus_right(lua_State *L)
{
    nnwm_action_focus_right(get_server(L));
    return 0;
}

static int
l_nnwm_focus_next(lua_State *L)
{
    nnwm_action_focus_next(get_server(L));
    return 0;
}

static int
l_nnwm_focus_prev(lua_State *L)
{
    nnwm_action_focus_prev(get_server(L));
    return 0;
}

static int
l_nnwm_swap_left(lua_State *L)
{
    nnwm_action_swap_left(get_server(L));
    return 0;
}

static int
l_nnwm_swap_right(lua_State *L)
{
    nnwm_action_swap_right(get_server(L));
    return 0;
}

static int
l_nnwm_swap_next(lua_State *L)
{
    nnwm_action_swap_next(get_server(L));
    return 0;
}

static int
l_nnwm_swap_prev(lua_State *L)
{
    nnwm_action_swap_prev(get_server(L));
    return 0;
}

static int
l_nnwm_cycle(lua_State *L)
{
    nnwm_action_cycle(get_server(L));
    return 0;
}

static const struct luaL_Reg nnwm_funcs[] = {
    { "key",          l_nnwm_key },
    { "quit",         l_nnwm_quit },
    { "close",        l_nnwm_close },
    { "spawn",        l_nnwm_spawn },
    { "focus_left",   l_nnwm_focus_left },
    { "focus_right",  l_nnwm_focus_right },
    { "focus_next",   l_nnwm_focus_next },
    { "focus_prev",   l_nnwm_focus_prev },
    { "swap_left",    l_nnwm_swap_left },
    { "swap_right",   l_nnwm_swap_right },
    { "swap_next",    l_nnwm_swap_next },
    { "swap_prev",    l_nnwm_swap_prev },
    { "cycle",        l_nnwm_cycle },
    { nullptr, nullptr },
};

/* ---- push config defaults into the Lua nnwm table ---- */

static void
push_config_defaults(lua_State *L, struct nnwm_config *cfg)
{
    lua_getglobal(L, "nnwm");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "nnwm");
    }

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

    lua_pop(L, 1);
}

/* ---- read back non-keybinding settings from Lua ---- */

static void
read_config_table(lua_State *L, struct nnwm_config *cfg)
{
    lua_getglobal(L, "nnwm");
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
    cfg->cursor_size  = get_int_field(L, "cursor_size", cfg->cursor_size);
    cfg->touchpad_tap_to_click = get_bool_field(L, "touchpad_tap_to_click",
                                                 cfg->touchpad_tap_to_click);
    cfg->touchpad_natural_scroll = get_bool_field(L, "touchpad_natural_scroll",
                                                   cfg->touchpad_natural_scroll);
    cfg->touchpad_disable_while_typing = get_bool_field(L, "touchpad_disable_while_typing",
                                                         cfg->touchpad_disable_while_typing);

    char *s;
    s = get_string_field(L, "cursor_theme", cfg->cursor_theme);
    free(cfg->cursor_theme);
    cfg->cursor_theme = s;

    s = get_string_field(L, "seat_name", cfg->seat_name);
    free(cfg->seat_name);
    cfg->seat_name = s;

    s = get_string_field(L, "launcher_command", cfg->launcher_command);
    free(cfg->launcher_command);
    cfg->launcher_command = s;

    lua_pop(L, 1);
}

/* ---- public API ---- */

extern "C" void
nnwm_lua_init(struct nnwm_server *server)
{
    server->lua = luaL_newstate();
    if (!server->lua)
    {
        std::fprintf(stderr, "nnwm: failed to create Lua state\n");
        return;
    }

    luaL_openlibs(server->lua);

    /* Store server pointer in Lua registry for action functions */
    lua_pushlightuserdata(server->lua, server);
    lua_setfield(server->lua, LUA_REGISTRYINDEX, "_nnwm_server");

    /* Register MOD and KEY tables */
    push_mod_table(server->lua);
    push_key_table(server->lua);

    /* Register nnwm table with key() and action functions */
    lua_newtable(server->lua);
    luaL_setfuncs(server->lua, nnwm_funcs, 0);
    lua_setglobal(server->lua, "nnwm");

    /* Initialize keybinding registry */
    server->lua_keybindings     = nullptr;
    server->lua_keybinding_count = 0;
    server->lua_keybinding_cap   = 0;
}

extern "C" void
nnwm_lua_fini(struct nnwm_server *server)
{
    if (!server->lua)
        return;

    /* Release all Lua function references */
    for (int i = 0; i < server->lua_keybinding_count; i++)
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_keybindings[i].func_ref);
    std::free(server->lua_keybindings);
    server->lua_keybindings     = nullptr;
    server->lua_keybinding_count = 0;
    server->lua_keybinding_cap   = 0;

    lua_close(server->lua);
    server->lua = nullptr;
}

extern "C" void
nnwm_lua_load_config(struct nnwm_server *server, struct nnwm_config *cfg,
                      const char *path)
{
    if (!server->lua)
        return;

    /* Re-push config defaults into the nnwm table so read_config_table works */
    push_config_defaults(server->lua, cfg);

    if (luaL_dofile(server->lua, path) != LUA_OK)
    {
        std::fprintf(stderr, "nnwm: config error: %s\n",
                     lua_tostring(server->lua, -1));
        lua_pop(server->lua, 1);
    }

    read_config_table(server->lua, cfg);

    std::fprintf(stderr, "nnwm: loaded config from %s (%d keybindings)\n",
                 path, server->lua_keybinding_count);
}

extern "C" void
nnwm_lua_reload(struct nnwm_server *server, struct nnwm_config *cfg)
{
    if (!server->lua || !server->config_path)
        return;

    /* Clear existing keybinding registrations */
    for (int i = 0; i < server->lua_keybinding_count; i++)
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_keybindings[i].func_ref);
    server->lua_keybinding_count = 0;

    /* Re-push defaults and re-run config */
    push_config_defaults(server->lua, cfg);

    if (luaL_dofile(server->lua, server->config_path) != LUA_OK)
    {
        std::fprintf(stderr, "nnwm: config error: %s\n",
                     lua_tostring(server->lua, -1));
        lua_pop(server->lua, 1);
    }

    read_config_table(server->lua, cfg);

    std::fprintf(stderr, "nnwm: reloaded config (%d keybindings)\n",
                 server->lua_keybinding_count);
}

extern "C" int
nnwm_lua_handle_keybinding(struct nnwm_server *server,
                            uint32_t mods, unsigned int keysym)
{
    if (!server->lua)
        return 0;

    /* Normalize to lowercase so Shift doesn't change the keysym */
    xkb_keysym_t key = xkb_keysym_to_lower(static_cast<xkb_keysym_t>(keysym));

    for (int i = 0; i < server->lua_keybinding_count; i++)
    {
        auto &kb = server->lua_keybindings[i];
        if (kb.mods == mods && kb.keysym == key)
        {
            lua_rawgeti(server->lua, LUA_REGISTRYINDEX, kb.func_ref);
            if (lua_pcall(server->lua, 0, 0, 0) != LUA_OK)
            {
                std::fprintf(stderr, "nnwm: keybinding error: %s\n",
                             lua_tostring(server->lua, -1));
                lua_pop(server->lua, 1);
            }
            return 1;
        }
    }
    return 0;
}

/* ---- non-keybinding config management ---- */

extern "C" struct nnwm_config *
nnwm_config_defaults(void)
{
    auto *cfg = new nnwm_config{};

    cfg->master_ratio      = 0.55f;
    cfg->master_ratio_step = 0.05f;
    cfg->master_ratio_min  = 0.1f;
    cfg->master_ratio_max  = 0.9f;

    cfg->border_width = 2;
    cfg->focused_color[0]   = 0.3f;  cfg->focused_color[1]   = 0.5f;
    cfg->focused_color[2]   = 0.8f;  cfg->focused_color[3]   = 1.0f;
    cfg->unfocused_color[0] = 0.15f; cfg->unfocused_color[1] = 0.15f;
    cfg->unfocused_color[2] = 0.15f; cfg->unfocused_color[3] = 1.0f;

    cfg->keyboard_repeat_rate  = 25;
    cfg->keyboard_repeat_delay = 600;

    cfg->cursor_theme    = strdup("default");
    cfg->cursor_size     = 24;
    cfg->seat_name       = strdup("seat0");
    cfg->launcher_command = strdup("rofi -show drun");

    cfg->touchpad_tap_to_click       = true;
    cfg->touchpad_natural_scroll     = true;
    cfg->touchpad_disable_while_typing = true;

    return cfg;
}

extern "C" void
nnwm_config_free(struct nnwm_config *cfg)
{
    if (!cfg)
        return;
    free(cfg->cursor_theme);
    free(cfg->seat_name);
    free(cfg->launcher_command);
    delete cfg;
}
