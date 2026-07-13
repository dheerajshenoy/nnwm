#include "lua/config.hpp"

#include "config.hpp"
#include "nnwm.hpp"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <xkbcommon/xkbcommon.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fnmatch.h>
#include <unistd.h>

/* ---- helpers ---- */

static struct nnwm_server *
get_server(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "_nnwm_server");
    auto *server = static_cast<struct nnwm_server *>(lua_touserdata(L, -1));
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
    char *r       = v ? strdup(v) : nullptr;
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

static const struct
{
    const char *name;
    int value;
} mod_entries[] = {
    {"Shift", WLR_MODIFIER_SHIFT},
    {"Caps", WLR_MODIFIER_CAPS},
    {"Ctrl", WLR_MODIFIER_CTRL},
    {"Alt", WLR_MODIFIER_ALT},
    {"Mod2", WLR_MODIFIER_MOD2},
    {"Mod3", WLR_MODIFIER_MOD3},
    {"Super", WLR_MODIFIER_LOGO},
    {"Mod5", WLR_MODIFIER_MOD5},
    {nullptr, 0},
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
        char buf[2]      = {c, '\0'};
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

    struct
    {
        const char *name;
        const char *xkb;
    } special[] = {
        {"Return", "Return"}, {"Space", "space"},         {"Tab", "Tab"},
        {"Escape", "Escape"}, {"Backspace", "BackSpace"}, {"Delete", "Delete"},
        {"Up", "Up"},         {"Down", "Down"},           {"Left", "Left"},
        {"Right", "Right"},   {nullptr, nullptr},
    };
    for (int i = 0; special[i].name != nullptr; i++)
    {
        xkb_keysym_t sym
            = xkb_keysym_from_name(special[i].xkb, XKB_KEYSYM_NO_FLAGS);
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

    uint32_t mods       = 0;
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;

    int len = static_cast<int>(lua_rawlen(L, 1));
    for (int i = 1; i <= len; i++)
    {
        lua_rawgeti(L, 1, i);
        const char *name = lua_tostring(L, -1);
        if (!name)
        {
            lua_pop(L, 1);
            return luaL_error(L,
                              "nnwm.key: combo table entries must be strings");
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
        server->lua_keybinding_cap
            = server->lua_keybinding_cap ? server->lua_keybinding_cap * 2 : 16;
        server->lua_keybindings
            = static_cast<struct nnwm_lua_keybinding *>(std::realloc(
                server->lua_keybindings, sizeof(struct nnwm_lua_keybinding)
                                             * server->lua_keybinding_cap));
    }

    auto &kb    = server->lua_keybindings[server->lua_keybinding_count++];
    kb.mods     = mods;
    kb.keysym   = keysym;
    kb.func_ref = func_ref;

    return 0;
}

/* ---- Lua action wrappers ---- */

static int
l_nnwm_quit(lua_State *L)
{
    nnwm::action_quit(get_server(L));
    return 0;
}

static int
l_nnwm_close(lua_State *L)
{
    nnwm::action_close(get_server(L));
    return 0;
}

static int
l_nnwm_spawn(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    nnwm::action_spawn(get_server(L), cmd);
    return 0;
}

static int
l_nnwm_spawn_once(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    nnwm::action_spawn_once(get_server(L), cmd);
    return 0;
}

static int
l_nnwm_focus_left(lua_State *L)
{
    nnwm::action_focus_left(get_server(L));
    return 0;
}

static int
l_nnwm_focus_right(lua_State *L)
{
    nnwm::action_focus_right(get_server(L));
    return 0;
}

static int
l_nnwm_focus_next(lua_State *L)
{
    nnwm::action_focus_next(get_server(L));
    return 0;
}

static int
l_nnwm_focus_prev(lua_State *L)
{
    nnwm::action_focus_prev(get_server(L));
    return 0;
}

static int
l_nnwm_swap_left(lua_State *L)
{
    nnwm::action_swap_left(get_server(L));
    return 0;
}

static int
l_nnwm_swap_right(lua_State *L)
{
    nnwm::action_swap_right(get_server(L));
    return 0;
}

static int
l_nnwm_swap_next(lua_State *L)
{
    nnwm::action_swap_next(get_server(L));
    return 0;
}

static int
l_nnwm_swap_prev(lua_State *L)
{
    nnwm::action_swap_prev(get_server(L));
    return 0;
}

static int
l_nnwm_cycle(lua_State *L)
{
    nnwm::action_cycle(get_server(L));
    return 0;
}

static int
l_nnwm_swap_master(lua_State *L)
{
    nnwm::action_swap_master(get_server(L));
    return 0;
}

static int
l_nnwm_switch_workspace(lua_State *L)
{
    int ws = (int)luaL_checkinteger(L, 1);
    if (ws < 1 || ws > NNWM_NUM_WORKSPACES)
        return luaL_error(L, "nnwm.switch_workspace: index must be 1-%d", NNWM_NUM_WORKSPACES);
    nnwm::action_switch_workspace(get_server(L), ws - 1);
    return 0;
}

static int
l_nnwm_move_to_workspace(lua_State *L)
{
    int ws = (int)luaL_checkinteger(L, 1);
    if (ws < 1 || ws > NNWM_NUM_WORKSPACES)
        return luaL_error(L, "nnwm.move_to_workspace: index must be 1-%d", NNWM_NUM_WORKSPACES);
    nnwm::action_move_to_workspace(get_server(L), ws - 1);
    return 0;
}

static int
l_nnwm_master_ratio_grow(lua_State *L)
{
    nnwm::action_master_ratio_grow(get_server(L));
    return 0;
}

static int
l_nnwm_master_ratio_shrink(lua_State *L)
{
    nnwm::action_master_ratio_shrink(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_float(lua_State *L)
{
    nnwm::action_toggle_float(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_fullscreen(lua_State *L)
{
    nnwm::action_toggle_fullscreen(get_server(L));
    return 0;
}

static int
l_nnwm_focus_monitor_next(lua_State *L)
{
    nnwm::action_focus_monitor_next(get_server(L));
    return 0;
}

static int
l_nnwm_focus_monitor_prev(lua_State *L)
{
    nnwm::action_focus_monitor_prev(get_server(L));
    return 0;
}

static int
l_nnwm_move_to_monitor_next(lua_State *L)
{
    nnwm::action_move_to_monitor_next(get_server(L));
    return 0;
}

static int
l_nnwm_move_to_monitor_prev(lua_State *L)
{
    nnwm::action_move_to_monitor_prev(get_server(L));
    return 0;
}

static int
l_nnwm_host_name(lua_State *L)
{
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0)
        buf[sizeof(buf) - 1] = '\0';
    else
        buf[0] = '\0';
    lua_pushstring(L, buf);
    return 1;
}

static int
l_nnwm_rule(lua_State *L)
{
    if (!lua_istable(L, 1) || !lua_istable(L, 2))
        return luaL_error(L, "nnwm.rule: expected two table arguments");

    auto *server = get_server(L);
    auto *cfg    = server->config;

    cfg->window_rules = static_cast<nnwm_window_rule*>(
        realloc(cfg->window_rules,
                (size_t)(cfg->window_rule_count + 1) * sizeof(nnwm_window_rule)));
    auto &r = cfg->window_rules[cfg->window_rule_count++];
    memset(&r, 0, sizeof(r));
    r.floating   = -1;
    r.fullscreen = -1;
    r.workspace  = -1;

    /* Match criteria */
    lua_getfield(L, 1, "app_id");
    if (lua_isstring(L, -1)) r.app_id = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, 1, "title");
    if (lua_isstring(L, -1)) r.title = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);

    /* Actions */
    lua_getfield(L, 2, "floating");
    if (lua_isboolean(L, -1)) r.floating = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "fullscreen");
    if (lua_isboolean(L, -1)) r.fullscreen = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "workspace");
    if (lua_isinteger(L, -1)) {
        int ws = (int)lua_tointeger(L, -1) - 1; /* Lua 1-9 → internal 0-8 */
        if (ws >= 0 && ws < NNWM_NUM_WORKSPACES) r.workspace = ws;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "monitor");
    if (lua_isstring(L, -1)) r.monitor = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);

    return 0;
}

static const struct luaL_Reg nnwm_funcs[] = {
    {"key", l_nnwm_key},
    {"quit", l_nnwm_quit},
    {"close", l_nnwm_close},
    {"spawn", l_nnwm_spawn},
    {"spawn_once", l_nnwm_spawn_once},
    {"focus_left", l_nnwm_focus_left},
    {"focus_right", l_nnwm_focus_right},
    {"focus_next", l_nnwm_focus_next},
    {"focus_prev", l_nnwm_focus_prev},
    {"swap_left", l_nnwm_swap_left},
    {"swap_right", l_nnwm_swap_right},
    {"swap_next", l_nnwm_swap_next},
    {"swap_prev", l_nnwm_swap_prev},
    {"swap_master", l_nnwm_swap_master},
    {"cycle", l_nnwm_cycle},
    {"switch_workspace", l_nnwm_switch_workspace},
    {"move_to_workspace", l_nnwm_move_to_workspace},
    {"master_ratio_grow", l_nnwm_master_ratio_grow},
    {"master_ratio_shrink", l_nnwm_master_ratio_shrink},
    {"toggle_float", l_nnwm_toggle_float},
    {"toggle_fullscreen", l_nnwm_toggle_fullscreen},
    {"focus_monitor_next", l_nnwm_focus_monitor_next},
    {"focus_monitor_prev", l_nnwm_focus_monitor_prev},
    {"move_to_monitor_next", l_nnwm_move_to_monitor_next},
    {"move_to_monitor_prev", l_nnwm_move_to_monitor_prev},
    {"host_name", l_nnwm_host_name},
    {"rule", l_nnwm_rule},
    {nullptr, nullptr},
};

/* ---- push config defaults into the Lua nnwm table ---- */

static void
push_config_defaults(lua_State *L, struct nnwm_config *cfg)
{
    lua_getglobal(L, "nnwm");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "nnwm");
    }

    /* layout sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->new_window_master);
    lua_setfield(L, -2, "new_window_master");
    lua_pushnumber(L, cfg->master_ratio);
    lua_setfield(L, -2, "master_ratio");
    lua_pushnumber(L, cfg->master_ratio_step);
    lua_setfield(L, -2, "master_ratio_step");
    lua_pushnumber(L, cfg->master_ratio_min);
    lua_setfield(L, -2, "master_ratio_min");
    lua_pushnumber(L, cfg->master_ratio_max);
    lua_setfield(L, -2, "master_ratio_max");
    lua_setfield(L, -2, "layout");

    /* gaps sub-table */
    lua_newtable(L);
    lua_pushinteger(L, cfg->inner_gap);
    lua_setfield(L, -2, "inner");
    lua_pushinteger(L, cfg->outer_gap);
    lua_setfield(L, -2, "outer");
    lua_pushboolean(L, cfg->smart_gaps);
    lua_setfield(L, -2, "smart");
    lua_pushboolean(L, cfg->smart_borders);
    lua_setfield(L, -2, "smart_borders");
    lua_setfield(L, -2, "gaps");

    /* border sub-table */
    lua_newtable(L);
    lua_pushinteger(L, cfg->border_width);
    lua_setfield(L, -2, "width");
    lua_newtable(L);
    for (int i = 0; i < 4; i++) { lua_pushnumber(L, cfg->focused_color[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "focused_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++) { lua_pushnumber(L, cfg->unfocused_color[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "unfocused_color");
    lua_setfield(L, -2, "border");

    /* keyboard sub-table */
    lua_newtable(L);
    lua_pushinteger(L, cfg->keyboard_repeat_rate);
    lua_setfield(L, -2, "repeat_rate");
    lua_pushinteger(L, cfg->keyboard_repeat_delay);
    lua_setfield(L, -2, "repeat_delay");
    lua_pushstring(L, cfg->xkb_options ? cfg->xkb_options : "");
    lua_setfield(L, -2, "xkb_options");
    lua_setfield(L, -2, "keyboard");

    lua_pushstring(L, cfg->seat_name);
    lua_setfield(L, -2, "seat_name");

    /* touchpad sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->touchpad_tap_to_click);
    lua_setfield(L, -2, "tap_to_click");
    lua_pushboolean(L, cfg->touchpad_natural_scroll);
    lua_setfield(L, -2, "natural_scroll");
    lua_pushboolean(L, cfg->touchpad_disable_while_typing);
    lua_setfield(L, -2, "disable_while_typing");
    lua_setfield(L, -2, "touchpad");

    /* mouse sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->focus_follows_mouse);
    lua_setfield(L, -2, "focus_follows_mouse");
    lua_pushstring(L, cfg->cursor_theme);
    lua_setfield(L, -2, "cursor_theme");
    lua_pushinteger(L, cfg->cursor_size);
    lua_setfield(L, -2, "cursor_size");
    lua_setfield(L, -2, "mouse");

    lua_pushboolean(L, cfg->client_decorations);
    lua_setfield(L, -2, "client_decorations");

    /* titlebar sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->titlebar_height > 0);
    lua_setfield(L, -2, "enabled");
    lua_pushinteger(L, cfg->titlebar_height > 0 ? cfg->titlebar_height : 20);
    lua_setfield(L, -2, "height");
    lua_pushstring(L, cfg->titlebar_font ? cfg->titlebar_font : "Sans 10");
    lua_setfield(L, -2, "font");
    lua_pushinteger(L, cfg->titlebar_text_align);
    lua_setfield(L, -2, "text_align");
    lua_newtable(L);
    for (int i = 0; i < 4; i++) { lua_pushnumber(L, cfg->titlebar_bg_color[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "bg_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++) { lua_pushnumber(L, cfg->titlebar_focused_bg_color[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "focused_bg_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++) { lua_pushnumber(L, cfg->titlebar_text_color[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "text_color");
    lua_setfield(L, -2, "titlebar");

    /* monitors: empty table (user populates in config file) */
    lua_newtable(L);
    lua_setfield(L, -2, "monitors");

    lua_pop(L, 1);
}

static void
free_window_rules(struct nnwm_config *cfg)
{
    for (int i = 0; i < cfg->window_rule_count; i++) {
        auto &r = cfg->window_rules[i];
        free(r.app_id);
        free(r.title);
        free(r.monitor);
    }
    free(cfg->window_rules);
    cfg->window_rules     = nullptr;
    cfg->window_rule_count = 0;
}

/* ---- read monitor configuration from Lua ---- */

static void
free_monitor_configs(struct nnwm_config *cfg)
{
    for (int i = 0; i < cfg->monitor_config_count; i++)
    {
        auto &mc = cfg->monitor_configs[i];
        free(mc.name);
        free(mc.make);
        free(mc.model);
        free(mc.serial);
    }
    free(cfg->monitor_configs);
    cfg->monitor_configs     = nullptr;
    cfg->monitor_config_count = 0;
}

static void
read_monitor_configs(lua_State *L, struct nnwm_config *cfg)
{
    free_monitor_configs(cfg);

    lua_getglobal(L, "nnwm");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "monitors");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    /* Count entries */
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) { count++; lua_pop(L, 1); }
    if (count == 0) { lua_pop(L, 2); return; }

    cfg->monitor_configs = static_cast<nnwm_monitor_config*>(
        calloc(count, sizeof(nnwm_monitor_config)));
    cfg->monitor_config_count = count;

    int idx = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0 && idx < count)
    {
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

        auto &mc = cfg->monitor_configs[idx];
        memset(&mc, 0, sizeof(mc));
        mc.x = INT_MAX;
        mc.y = INT_MAX;
        mc.transform = -1;

        mc.name   = get_string_field(L, "name", nullptr);
        mc.make   = get_string_field(L, "make", nullptr);
        mc.model  = get_string_field(L, "model", nullptr);
        mc.serial = get_string_field(L, "serial", nullptr);

        mc.width   = get_int_field(L, "width", 0);
        mc.height  = get_int_field(L, "height", 0);
        mc.refresh = get_int_field(L, "refresh", 0);

        lua_getfield(L, -1, "x");
        if (lua_isinteger(L, -1)) mc.x = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "y");
        if (lua_isinteger(L, -1)) mc.y = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        mc.scale = get_float_field(L, "scale", 0.0f);

        /* transform: string name -> wl_output_transform int */
        {
            char *ts = get_string_field(L, "transform", nullptr);
            if (ts)
            {
                if      (strcmp(ts, "none") == 0)          mc.transform = 0; /* WL_OUTPUT_TRANSFORM_NORMAL */
                else if (strcmp(ts, "90") == 0)            mc.transform = 1;
                else if (strcmp(ts, "180") == 0)           mc.transform = 2;
                else if (strcmp(ts, "270") == 0)           mc.transform = 3;
                else if (strcmp(ts, "flipped") == 0)       mc.transform = 4;
                else if (strcmp(ts, "flipped-90") == 0)    mc.transform = 5;
                else if (strcmp(ts, "flipped-180") == 0)   mc.transform = 6;
                else if (strcmp(ts, "flipped-270") == 0)   mc.transform = 7;
                else mc.transform = -1;
                free(ts);
            }
        }

        mc.hdr      = get_bool_field(L, "hdr", false);
        mc.disabled = get_bool_field(L, "disabled", false);

        lua_pop(L, 1);
        idx++;
    }

    lua_pop(L, 2); /* pop monitors table and nnwm table */
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

    lua_getfield(L, -1, "layout");
    if (lua_istable(L, -1)) {
        cfg->new_window_master  = get_bool_field(L, "new_window_master", cfg->new_window_master);
        cfg->master_ratio       = get_float_field(L, "master_ratio", cfg->master_ratio);
        cfg->master_ratio_step  = get_float_field(L, "master_ratio_step", cfg->master_ratio_step);
        cfg->master_ratio_min   = get_float_field(L, "master_ratio_min", cfg->master_ratio_min);
        cfg->master_ratio_max   = get_float_field(L, "master_ratio_max", cfg->master_ratio_max);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "gaps");
    if (lua_istable(L, -1)) {
        cfg->inner_gap     = get_int_field(L, "inner", cfg->inner_gap);
        cfg->outer_gap     = get_int_field(L, "outer", cfg->outer_gap);
        cfg->smart_gaps    = get_bool_field(L, "smart", cfg->smart_gaps);
        cfg->smart_borders = get_bool_field(L, "smart_borders", cfg->smart_borders);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "border");
    if (lua_istable(L, -1)) {
        cfg->border_width = get_int_field(L, "width", cfg->border_width);
        float dflt_foc[4] = {cfg->focused_color[0], cfg->focused_color[1],
                             cfg->focused_color[2], cfg->focused_color[3]};
        get_color_field(L, "focused_color", cfg->focused_color, dflt_foc);
        float dflt_unf[4] = {cfg->unfocused_color[0], cfg->unfocused_color[1],
                             cfg->unfocused_color[2], cfg->unfocused_color[3]};
        get_color_field(L, "unfocused_color", cfg->unfocused_color, dflt_unf);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "keyboard");
    if (lua_istable(L, -1)) {
        cfg->keyboard_repeat_rate  = get_int_field(L, "repeat_rate", cfg->keyboard_repeat_rate);
        cfg->keyboard_repeat_delay = get_int_field(L, "repeat_delay", cfg->keyboard_repeat_delay);
        char *s = get_string_field(L, "xkb_options", cfg->xkb_options);
        free(cfg->xkb_options); cfg->xkb_options = s;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "touchpad");
    if (lua_istable(L, -1)) {
        cfg->touchpad_tap_to_click = get_bool_field(L, "tap_to_click",
                                                    cfg->touchpad_tap_to_click);
        cfg->touchpad_natural_scroll = get_bool_field(L, "natural_scroll",
                                                      cfg->touchpad_natural_scroll);
        cfg->touchpad_disable_while_typing = get_bool_field(L, "disable_while_typing",
                                                            cfg->touchpad_disable_while_typing);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "mouse");
    if (lua_istable(L, -1)) {
        cfg->focus_follows_mouse = get_bool_field(L, "focus_follows_mouse",
                                                  cfg->focus_follows_mouse);
        char *ct = get_string_field(L, "cursor_theme", cfg->cursor_theme);
        free(cfg->cursor_theme); cfg->cursor_theme = ct;
        cfg->cursor_size = get_int_field(L, "cursor_size", cfg->cursor_size);
    }
    lua_pop(L, 1);

    cfg->client_decorations = get_bool_field(L, "client_decorations", cfg->client_decorations);

    lua_getfield(L, -1, "titlebar");
    if (lua_istable(L, -1)) {
        bool tb_enabled = get_bool_field(L, "enabled", cfg->titlebar_height > 0);
        if (!tb_enabled) {
            cfg->titlebar_height = 0;
        } else {
            int h = get_int_field(L, "height", cfg->titlebar_height > 0 ? cfg->titlebar_height : 20);
            cfg->titlebar_height = h > 0 ? h : 20;
        }
        cfg->titlebar_text_align = get_int_field(L, "text_align", cfg->titlebar_text_align);
        char *tf = get_string_field(L, "font", cfg->titlebar_font);
        free(cfg->titlebar_font); cfg->titlebar_font = tf;
        float dflt_tbg[4]  = {cfg->titlebar_bg_color[0], cfg->titlebar_bg_color[1],
                               cfg->titlebar_bg_color[2], cfg->titlebar_bg_color[3]};
        get_color_field(L, "bg_color", cfg->titlebar_bg_color, dflt_tbg);
        float dflt_tfbg[4] = {cfg->titlebar_focused_bg_color[0], cfg->titlebar_focused_bg_color[1],
                               cfg->titlebar_focused_bg_color[2], cfg->titlebar_focused_bg_color[3]};
        get_color_field(L, "focused_bg_color", cfg->titlebar_focused_bg_color, dflt_tfbg);
        float dflt_ttc[4]  = {cfg->titlebar_text_color[0], cfg->titlebar_text_color[1],
                               cfg->titlebar_text_color[2], cfg->titlebar_text_color[3]};
        get_color_field(L, "text_color", cfg->titlebar_text_color, dflt_ttc);
    }
    lua_pop(L, 1);

    char *s = get_string_field(L, "seat_name", cfg->seat_name);
    free(cfg->seat_name);
    cfg->seat_name = s;

    lua_pop(L, 1);

    read_monitor_configs(L, cfg);
}

/* ---- public API ---- */

void
nnwm::lua_init(struct nnwm_server *server)
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
    server->lua_keybindings      = nullptr;
    server->lua_keybinding_count = 0;
    server->lua_keybinding_cap   = 0;
}

void
nnwm::lua_fini(struct nnwm_server *server)
{
    if (!server->lua)
        return;

    /* Release all Lua function references */
    for (int i = 0; i < server->lua_keybinding_count; i++)
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_keybindings[i].func_ref);
    std::free(server->lua_keybindings);
    server->lua_keybindings      = nullptr;
    server->lua_keybinding_count = 0;
    server->lua_keybinding_cap   = 0;

    lua_close(server->lua);
    server->lua = nullptr;
}

void
nnwm::lua_load_config(struct nnwm_server *server, struct nnwm_config *cfg,
                     const char *path)
{
    if (!server->lua)
        return;

    free_window_rules(cfg);

    /* Re-push config defaults into the nnwm table so read_config_table works */
    push_config_defaults(server->lua, cfg);

    if (luaL_dofile(server->lua, path) != LUA_OK)
    {
        std::fprintf(stderr, "nnwm: config error: %s\n",
                     lua_tostring(server->lua, -1));
        lua_pop(server->lua, 1);
    }

    read_config_table(server->lua, cfg);

    std::fprintf(stderr, "nnwm: loaded config from %s (%d keybindings)\n", path,
                 server->lua_keybinding_count);
}

void
nnwm::lua_reload(struct nnwm_server *server, struct nnwm_config *cfg)
{
    if (!server->lua || !server->config_path)
        return;

    /* Clear existing keybinding registrations */
    for (int i = 0; i < server->lua_keybinding_count; i++)
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_keybindings[i].func_ref);
    server->lua_keybinding_count = 0;

    /* Clear existing window rules */
    free_window_rules(cfg);

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

int
nnwm::lua_handle_keybinding(struct nnwm_server *server, uint32_t mods,
                           unsigned int keysym)
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

struct nnwm_config *
nnwm::config_defaults(void)
{
    auto *cfg = new nnwm_config{};

    cfg->master_ratio      = 0.55f;
    cfg->master_ratio_step = 0.05f;
    cfg->master_ratio_min  = 0.1f;
    cfg->master_ratio_max  = 0.9f;

    cfg->inner_gap     = 0;
    cfg->outer_gap     = 0;
    cfg->smart_gaps    = false;
    cfg->smart_borders = false;

    cfg->border_width       = 2;
    cfg->focused_color[0]   = 0.3f;
    cfg->focused_color[1]   = 0.5f;
    cfg->focused_color[2]   = 0.8f;
    cfg->focused_color[3]   = 1.0f;
    cfg->unfocused_color[0] = 0.15f;
    cfg->unfocused_color[1] = 0.15f;
    cfg->unfocused_color[2] = 0.15f;
    cfg->unfocused_color[3] = 1.0f;

    cfg->keyboard_repeat_rate  = 25;
    cfg->keyboard_repeat_delay = 600;
    cfg->xkb_options           = strdup("");

    cfg->cursor_theme = strdup("default");
    cfg->cursor_size  = 24;
    cfg->seat_name    = strdup("seat0");

    cfg->touchpad_tap_to_click         = true;
    cfg->touchpad_natural_scroll       = true;
    cfg->touchpad_disable_while_typing = true;

    cfg->focus_follows_mouse = false;
    cfg->new_window_master   = true;
    cfg->client_decorations  = false;

    cfg->titlebar_height         = 0;
    cfg->titlebar_font           = strdup("Sans 10");
    cfg->titlebar_text_align     = 1; /* center */
    cfg->titlebar_bg_color[0]    = 0.2f; cfg->titlebar_bg_color[1]    = 0.2f;
    cfg->titlebar_bg_color[2]    = 0.2f; cfg->titlebar_bg_color[3]    = 1.0f;
    cfg->titlebar_focused_bg_color[0] = 0.25f; cfg->titlebar_focused_bg_color[1] = 0.35f;
    cfg->titlebar_focused_bg_color[2] = 0.55f; cfg->titlebar_focused_bg_color[3] = 1.0f;
    cfg->titlebar_text_color[0]  = 1.0f; cfg->titlebar_text_color[1]  = 1.0f;
    cfg->titlebar_text_color[2]  = 1.0f; cfg->titlebar_text_color[3]  = 1.0f;

    cfg->monitor_configs     = nullptr;
    cfg->monitor_config_count = 0;

    cfg->window_rules     = nullptr;
    cfg->window_rule_count = 0;

    return cfg;
}

void
nnwm::config_free(struct nnwm_config *cfg)
{
    if (!cfg)
        return;
    free(cfg->cursor_theme);
    free(cfg->seat_name);
    free(cfg->xkb_options);
    free(cfg->titlebar_font);
    for (int i = 0; i < cfg->monitor_config_count; i++)
    {
        auto &mc = cfg->monitor_configs[i];
        free(mc.name);
        free(mc.make);
        free(mc.model);
        free(mc.serial);
    }
    free(cfg->monitor_configs);
    free_window_rules(cfg);
    delete cfg;
}
