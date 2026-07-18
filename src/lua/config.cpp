#include "lua/config.hpp"

#include "../nnwm_internal.hpp"
#include "config.hpp"
#include "nnwm.hpp"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <xkbcommon/xkbcommon.h>
}

#include <cctype>
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

static bool
parse_hex_color(const char *s, float out[4])
{
    /* Accept optional '#' prefix, then RRGGBB or RRGGBBAA. */
    if (!s)
        return false;
    if (*s == '#')
        s++;
    size_t len = strlen(s);
    if (len != 6 && len != 8)
        return false;
    for (size_t i = 0; i < len; i++)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    unsigned int r, g, b, a = 255;
    sscanf(s, "%2x%2x%2x", &r, &g, &b);
    if (len == 8)
        sscanf(s + 6, "%2x", &a);
    out[0] = r / 255.0f;
    out[1] = g / 255.0f;
    out[2] = b / 255.0f;
    out[3] = a / 255.0f;
    return true;
}

static void
get_color_field(lua_State *L, const char *name, float out[4], float dflt[4])
{
    lua_getfield(L, -1, name);
    if (lua_isstring(L, -1) && !lua_isnumber(L, -1))
    {
        if (!parse_hex_color(lua_tostring(L, -1), out))
            for (int i = 0; i < 4; i++)
                out[i] = dflt[i];
    }
    else if (lua_istable(L, -1))
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

#ifdef HAVE_SCENEFX
static nnwm_easing
parse_easing(const char *s, nnwm_easing dflt)
{
    if (!s)
        return dflt;
    if (strcmp(s, "linear") == 0)
        return nnwm_easing::LINEAR;
    if (strcmp(s, "ease_in") == 0)
        return nnwm_easing::IN;
    if (strcmp(s, "ease_out") == 0)
        return nnwm_easing::OUT;
    if (strcmp(s, "ease_in_out") == 0)
        return nnwm_easing::IN_OUT;
    if (strcmp(s, "bounce") == 0)
        return nnwm_easing::BOUNCE;
    if (strcmp(s, "elastic") == 0)
        return nnwm_easing::ELASTIC;
    return dflt;
}

static nnwm_open_style
parse_open_style(const char *s, nnwm_open_style dflt)
{
    if (!s)
        return dflt;
    if (strcmp(s, "fade_scale") == 0)
        return nnwm_open_style::FADE_SCALE;
    if (strcmp(s, "fade") == 0)
        return nnwm_open_style::FADE;
    if (strcmp(s, "scale") == 0)
        return nnwm_open_style::SCALE;
    if (strcmp(s, "slide_up") == 0)
        return nnwm_open_style::SLIDE_UP;
    if (strcmp(s, "slide_down") == 0)
        return nnwm_open_style::SLIDE_DOWN;
    if (strcmp(s, "slide_left") == 0)
        return nnwm_open_style::SLIDE_LEFT;
    if (strcmp(s, "slide_right") == 0)
        return nnwm_open_style::SLIDE_RIGHT;
    if (strcmp(s, "none") == 0)
        return nnwm_open_style::NONE;
    return dflt;
}
#endif /* HAVE_SCENEFX */

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
    int argc = lua_gettop(L);
    if (argc < 2 || argc > 3 || !lua_istable(L, 1) || !lua_isfunction(L, 2))
        return luaL_error(L, "nnwm.key(combo_table, callback[, description]) expected");
    if (argc == 3 && !lua_isstring(L, 3))
        return luaL_error(L, "nnwm.key: description must be a string");

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

    auto &kb        = server->lua_keybindings[server->lua_keybinding_count++];
    kb.mods         = mods;
    kb.keysym       = keysym;
    kb.func_ref     = func_ref;
    kb.description  = (argc == 3) ? strdup(lua_tostring(L, 3)) : nullptr;

    return 0;
}

/* ---- nnwm.gesture() C function ---- */

static int
l_nnwm_gesture(lua_State *L)
{
    if (!lua_isinteger(L, 1) || !lua_isstring(L, 2) || !lua_isfunction(L, 3))
        return luaL_error(
            L, "nnwm.gesture(fingers, direction, callback) expected");

    nnwm_server *server = get_server(L);
    int fingers         = (int)lua_tointeger(L, 1);
    const char *dir_str = lua_tostring(L, 2);

    nnwm_gesture_dir dir;
    if (strcmp(dir_str, "up") == 0)
        dir = nnwm_gesture_dir::UP;
    else if (strcmp(dir_str, "down") == 0)
        dir = nnwm_gesture_dir::DOWN;
    else if (strcmp(dir_str, "left") == 0)
        dir = nnwm_gesture_dir::LEFT;
    else if (strcmp(dir_str, "right") == 0)
        dir = nnwm_gesture_dir::RIGHT;
    else
        return luaL_error(L,
                          "nnwm.gesture: direction must be \"up\", \"down\","
                          " \"left\", or \"right\"");

    lua_pushvalue(L, 3);
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (server->lua_gesture_count >= server->lua_gesture_cap)
    {
        server->lua_gesture_cap
            = server->lua_gesture_cap ? server->lua_gesture_cap * 2 : 16;
        server->lua_gestures = static_cast<struct nnwm_lua_gesture *>(
            std::realloc(server->lua_gestures,
                         sizeof(struct nnwm_lua_gesture)
                             * server->lua_gesture_cap));
    }

    auto &g    = server->lua_gestures[server->lua_gesture_count++];
    g.fingers  = fingers;
    g.dir      = dir;
    g.func_ref = func_ref;
    return 0;
}

/* ---- Lua action wrappers ---- */

static int
l_nnwm_quit(lua_State *L)
{
    nnwm::quit(get_server(L));
    return 0;
}

static int
l_nnwm_close(lua_State *L)
{
    nnwm::close(get_server(L));
    return 0;
}

static int
l_nnwm_spawn(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    nnwm::spawn(get_server(L), cmd);
    return 0;
}

static int
l_nnwm_spawn_once(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    nnwm::spawn_once(get_server(L), cmd);
    return 0;
}

static int
l_nnwm_focus_left(lua_State *L)
{
    nnwm::focus::left(get_server(L));
    return 0;
}

static int
l_nnwm_focus_right(lua_State *L)
{
    nnwm::focus::right(get_server(L));
    return 0;
}

static int
l_nnwm_focus_next(lua_State *L)
{
    nnwm::focus::next(get_server(L));
    return 0;
}

static int
l_nnwm_focus_prev(lua_State *L)
{
    nnwm::focus::prev(get_server(L));
    return 0;
}

static int
l_nnwm_focus_dir(lua_State *L)
{
    const char *dir = luaL_checkstring(L, 1);
    nnwm::focus::dir(get_server(L), dir);
    return 0;
}

static int
l_nnwm_move_dir(lua_State *L)
{
    const char *dir = luaL_checkstring(L, 1);
    nnwm::focus::move_dir(get_server(L), dir);
    return 0;
}

static int
l_nnwm_focus_mode_toggle(lua_State *L)
{
    nnwm::focus::mode_toggle(get_server(L));
    return 0;
}

static int
l_nnwm_focus_next_float(lua_State *L)
{
    nnwm::focus::next_float(get_server(L));
    return 0;
}

static int
l_nnwm_focus_prev_float(lua_State *L)
{
    nnwm::focus::prev_float(get_server(L));
    return 0;
}

static int
l_nnwm_swap_left(lua_State *L)
{
    nnwm::swap::left(get_server(L));
    return 0;
}

static int
l_nnwm_swap_right(lua_State *L)
{
    nnwm::swap::right(get_server(L));
    return 0;
}

static int
l_nnwm_swap_next(lua_State *L)
{
    nnwm::swap::next(get_server(L));
    return 0;
}

static int
l_nnwm_swap_prev(lua_State *L)
{
    nnwm::swap::prev(get_server(L));
    return 0;
}

static int
l_nnwm_cycle(lua_State *L)
{
    nnwm::cycle(get_server(L));
    return 0;
}

static int
l_nnwm_swap_master(lua_State *L)
{
    nnwm::swap::master(get_server(L));
    return 0;
}

static int
l_nnwm_switch_workspace(lua_State *L)
{
    struct nnwm_server *server = get_server(L);
    int ws = (int)luaL_checkinteger(L, 1);
    int max = server->config->workspace_count;
    if (ws < 1 || ws > max)
        return luaL_error(L, "nnwm.switch_workspace: index must be 1-%d", max);
    nnwm::workspace::switch_to(server, ws - 1);
    return 0;
}

static int
l_nnwm_move_to_workspace(lua_State *L)
{
    struct nnwm_server *server = get_server(L);
    int ws = (int)luaL_checkinteger(L, 1);
    int max = server->config->workspace_count;
    if (ws < 1 || ws > max)
        return luaL_error(L, "nnwm.move_to_workspace: index must be 1-%d", max);
    nnwm::workspace::move_to(server, ws - 1);
    return 0;
}

static const char *
layout_mode_str(nnwm_layout_mode m)
{
    switch (m)
    {
        case nnwm_layout_mode::HTILE:   return "htile";
        case nnwm_layout_mode::VTILE:   return "vtile";
        case nnwm_layout_mode::TABBED:  return "tabbed";
        case nnwm_layout_mode::HSCROLL: return "hscroll";
        case nnwm_layout_mode::VSCROLL: return "vscroll";
        default:                        return "unknown";
    }
}

static int
l_nnwm_current_window(lua_State *L)
{
    auto *server = get_server(L);
    auto *tl     = get_focused_toplevel(server);
    if (!tl)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    lua_pushstring(L, tl->xdg_toplevel->title
                          ? tl->xdg_toplevel->title : "");
    lua_setfield(L, -2, "title");

    lua_pushstring(L, tl->xdg_toplevel->app_id
                          ? tl->xdg_toplevel->app_id : "");
    lua_setfield(L, -2, "app_id");

    lua_pushboolean(L, tl->floating);
    lua_setfield(L, -2, "floating");

    lua_pushboolean(L, tl->fullscreen);
    lua_setfield(L, -2, "fullscreen");

    lua_pushboolean(L, tl->fake_fullscreen);
    lua_setfield(L, -2, "fake_fullscreen");

    lua_pushboolean(L, tl->maximize);
    lua_setfield(L, -2, "maximized");

    lua_pushboolean(L, tl->sticky);
    lua_setfield(L, -2, "sticky");

    lua_pushinteger(L, tl->workspace + 1);
    lua_setfield(L, -2, "workspace");

    lua_pushinteger(L, tl->cur_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, tl->cur_y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, tl->cur_w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, tl->cur_h);
    lua_setfield(L, -2, "height");

    if (tl->output)
    {
        lua_pushstring(L, tl->output->wlr_output->name
                              ? tl->output->wlr_output->name : "");
        lua_setfield(L, -2, "output");
    }
    else
    {
        lua_pushnil(L);
        lua_setfield(L, -2, "output");
    }

    return 1;
}

static int
l_nnwm_current_workspace(lua_State *L)
{
    auto *server = get_server(L);
    auto *out    = server->focused_output;
    if (!out)
    {
        lua_pushnil(L);
        return 1;
    }

    int ws = out->active_workspace;

    lua_newtable(L);

    lua_pushinteger(L, ws + 1);
    lua_setfield(L, -2, "index");

    lua_pushstring(L, layout_mode_str(out->layout_mode[ws]));
    lua_setfield(L, -2, "layout");

    lua_pushnumber(L, (double)out->master_ratio[ws]);
    lua_setfield(L, -2, "master_ratio");

    lua_pushinteger(L, ws_count(server, out));
    lua_setfield(L, -2, "window_count");

    lua_pushstring(L, out->wlr_output->name ? out->wlr_output->name : "");
    lua_setfield(L, -2, "output");

    return 1;
}

static int
l_nnwm_current_output(lua_State *L)
{
    auto *server = get_server(L);
    auto *out    = server->focused_output;
    if (!out)
    {
        lua_pushnil(L);
        return 1;
    }

    struct wlr_output *wlr = out->wlr_output;

    lua_newtable(L);

    lua_pushstring(L, wlr->name ? wlr->name : "");
    lua_setfield(L, -2, "name");

    lua_pushstring(L, wlr->description ? wlr->description : "");
    lua_setfield(L, -2, "description");

    lua_pushinteger(L, wlr->width);
    lua_setfield(L, -2, "width");

    lua_pushinteger(L, wlr->height);
    lua_setfield(L, -2, "height");

    lua_pushnumber(L, (double)wlr->scale);
    lua_setfield(L, -2, "scale");

    {
        wlr_box box;
        wlr_output_layout_get_box(server->output_layout, wlr, &box);
        lua_pushinteger(L, box.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, box.y);
        lua_setfield(L, -2, "y");
    }

    lua_pushinteger(L, out->active_workspace + 1);
    lua_setfield(L, -2, "active_workspace");

    return 1;
}

static int
l_nnwm_master_ratio_grow(lua_State *L)
{
    nnwm::layout::master_ratio_grow(get_server(L));
    return 0;
}

static int
l_nnwm_master_ratio_shrink(lua_State *L)
{
    nnwm::layout::master_ratio_shrink(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_float(lua_State *L)
{
    nnwm::window::toggle_float(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_fullscreen(lua_State *L)
{
    nnwm::window::toggle_fullscreen(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_fake_fullscreen(lua_State *L)
{
    nnwm::window::toggle_fake_fullscreen(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_maximize(lua_State *L)
{
    nnwm::window::toggle_maximize(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_tabbed(lua_State *L)
{
    nnwm::layout::toggle_tabbed(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_horizontal_scroll(lua_State *L)
{
    nnwm::layout::toggle_horizontal_scroll(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_vertical_scroll(lua_State *L)
{
    nnwm::layout::toggle_vertical_scroll(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_vertical_tile(lua_State *L)
{
    nnwm::layout::toggle_vertical_tile(get_server(L));
    return 0;
}

static int
l_nnwm_layout_next(lua_State *L)
{
    nnwm::layout::next(get_server(L));
    return 0;
}

static int
l_nnwm_layout_prev(lua_State *L)
{
    nnwm::layout::prev(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_sticky(lua_State *L)
{
    nnwm::window::toggle_sticky(get_server(L));
    return 0;
}

static int
l_nnwm_toggle_overview(lua_State *L)
{
    nnwm::toggle_overview(get_server(L));
    return 0;
}

static int
l_nnwm_move_to_scratchpad(lua_State *L)
{
    nnwm::scratchpad::move_to(get_server(L));
    return 0;
}

static int
l_nnwm_scratchpad_toggle(lua_State *L)
{
    nnwm::scratchpad::toggle(get_server(L));
    return 0;
}

static int
l_nnwm_focus_monitor_next(lua_State *L)
{
    nnwm::monitor::focus_next(get_server(L));
    return 0;
}

static int
l_nnwm_focus_monitor_prev(lua_State *L)
{
    nnwm::monitor::focus_prev(get_server(L));
    return 0;
}

static int
l_nnwm_move_to_monitor_next(lua_State *L)
{
    nnwm::monitor::move_to_next(get_server(L));
    return 0;
}

static int
l_nnwm_move_to_monitor_prev(lua_State *L)
{
    nnwm::monitor::move_to_prev(get_server(L));
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

    cfg->window_rules = static_cast<nnwm_window_rule *>(
        realloc(cfg->window_rules, (size_t)(cfg->window_rule_count + 1)
                                       * sizeof(nnwm_window_rule)));
    auto &r = cfg->window_rules[cfg->window_rule_count++];
    memset(&r, 0, sizeof(r));
    r.floating        = -1;
    r.fullscreen      = -1;
    r.fake_fullscreen = -1;
    r.maximize        = -1;
    r.focused         = -1;
    r.sticky          = -1;
    r.workspace  = -1;
    r.opacity    = -1.0f;
    r.blur       = -1;
#ifdef HAVE_SCENEFX
    r.anim_open_style  = -1;
    r.anim_close_style = -1;
    r.no_anim          = -1;
#endif

    /* Match criteria */
    lua_getfield(L, 1, "app_id");
    if (lua_isstring(L, -1))
        r.app_id = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, 1, "title");
    if (lua_isstring(L, -1))
        r.title = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);

    /* Actions */
    lua_getfield(L, 2, "floating");
    if (lua_isboolean(L, -1))
        r.floating = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "fullscreen");
    if (lua_isboolean(L, -1))
        r.fullscreen = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "fake_fullscreen");
    if (lua_isboolean(L, -1))
        r.fake_fullscreen = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "maximize");
    if (lua_isboolean(L, -1))
        r.maximize = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "focused");
    if (lua_isboolean(L, -1))
        r.focused = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "sticky");
    if (lua_isboolean(L, -1))
        r.sticky = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

    lua_getfield(L, 2, "workspace");
    if (lua_isinteger(L, -1))
    {
        int ws = (int)lua_tointeger(L, -1) - 1; /* Lua 1-N → internal 0-(N-1) */
        if (ws >= 0 && ws < get_server(L)->config->workspace_count)
            r.workspace = ws;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "monitor");
    if (lua_isstring(L, -1))
        r.monitor = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, 2, "opacity");
    if (lua_isnumber(L, -1))
    {
        float v = (float)lua_tonumber(L, -1);
        if (v >= 0.0f && v <= 1.0f)
            r.opacity = v;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "blur");
    if (lua_isboolean(L, -1))
        r.blur = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);

#ifdef HAVE_SCENEFX
    lua_getfield(L, 2, "anim_open");
    if (lua_isstring(L, -1))
    {
        nnwm_open_style st
            = parse_open_style(lua_tostring(L, -1), (nnwm_open_style)-1);
        if ((int)st >= 0)
            r.anim_open_style = (int)st;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "anim_close");
    if (lua_isstring(L, -1))
    {
        nnwm_open_style st
            = parse_open_style(lua_tostring(L, -1), (nnwm_open_style)-1);
        if ((int)st >= 0)
            r.anim_close_style = (int)st;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "no_anim");
    if (lua_isboolean(L, -1))
        r.no_anim = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
#endif /* HAVE_SCENEFX */

    return 0;
}

/* ---- Event hooks and timers ---- */

static void
push_window_table(lua_State *L, nnwm_server *server, nnwm_toplevel *tl)
{
    lua_newtable(L);
    lua_pushstring(L, tl->xdg_toplevel->title ? tl->xdg_toplevel->title : "");
    lua_setfield(L, -2, "title");
    lua_pushstring(L, tl->xdg_toplevel->app_id ? tl->xdg_toplevel->app_id : "");
    lua_setfield(L, -2, "app_id");
    lua_pushboolean(L, tl->floating);
    lua_setfield(L, -2, "floating");
    lua_pushboolean(L, tl->fullscreen);
    lua_setfield(L, -2, "fullscreen");
    lua_pushboolean(L, tl->fake_fullscreen);
    lua_setfield(L, -2, "fake_fullscreen");
    lua_pushboolean(L, tl->maximize);
    lua_setfield(L, -2, "maximized");
    lua_pushboolean(L, tl->sticky);
    lua_setfield(L, -2, "sticky");
    lua_pushinteger(L, tl->workspace + 1);
    lua_setfield(L, -2, "workspace");
    lua_pushinteger(L, tl->cur_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, tl->cur_y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, tl->cur_w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, tl->cur_h);
    lua_setfield(L, -2, "height");
    if (tl->output)
    {
        lua_pushstring(L, tl->output->wlr_output->name
                              ? tl->output->wlr_output->name : "");
        lua_setfield(L, -2, "output");
    }
    else
    {
        lua_pushnil(L);
        lua_setfield(L, -2, "output");
    }
    (void)server;
}

static void
push_workspace_table(lua_State *L, nnwm_server *server, nnwm_output *out)
{
    int ws = out->active_workspace;
    lua_newtable(L);
    lua_pushinteger(L, ws + 1);
    lua_setfield(L, -2, "index");
    lua_pushstring(L, layout_mode_str(out->layout_mode[ws]));
    lua_setfield(L, -2, "layout");
    lua_pushnumber(L, (double)out->master_ratio[ws]);
    lua_setfield(L, -2, "master_ratio");
    lua_pushinteger(L, ws_count(server, out));
    lua_setfield(L, -2, "window_count");
    lua_pushstring(L, out->wlr_output->name ? out->wlr_output->name : "");
    lua_setfield(L, -2, "output");
}

static void
push_output_table(lua_State *L, nnwm_server *server, nnwm_output *out)
{
    struct wlr_output *wlr = out->wlr_output;
    lua_newtable(L);
    lua_pushstring(L, wlr->name ? wlr->name : "");
    lua_setfield(L, -2, "name");
    lua_pushstring(L, wlr->description ? wlr->description : "");
    lua_setfield(L, -2, "description");
    lua_pushinteger(L, wlr->width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, wlr->height);
    lua_setfield(L, -2, "height");
    lua_pushnumber(L, (double)wlr->scale);
    lua_setfield(L, -2, "scale");
    {
        wlr_box box;
        wlr_output_layout_get_box(server->output_layout, wlr, &box);
        lua_pushinteger(L, box.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, box.y);
        lua_setfield(L, -2, "y");
    }
    lua_pushinteger(L, out->active_workspace + 1);
    lua_setfield(L, -2, "active_workspace");
}

void
fire_hook_plain(nnwm_server *server, const char *event)
{
    lua_State *L = server->lua;
    if (!L) return;
    nnwm_hook *h;
    wl_list_for_each(h, &server->hooks, link)
    {
        if (strcmp(h->event, event) != 0) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, h->func_ref);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        {
            wlr_log(WLR_ERROR, "hook '%s': %s", event,
                    lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}

void
fire_hook_window(nnwm_server *server, const char *event, nnwm_toplevel *tl)
{
    lua_State *L = server->lua;
    if (!L || !tl) return;
    nnwm_hook *h;
    wl_list_for_each(h, &server->hooks, link)
    {
        if (strcmp(h->event, event) != 0) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, h->func_ref);
        push_window_table(L, server, tl);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            wlr_log(WLR_ERROR, "hook '%s': %s", event,
                    lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}

void
fire_hook_workspace(nnwm_server *server, const char *event, nnwm_output *out)
{
    lua_State *L = server->lua;
    if (!L || !out) return;
    nnwm_hook *h;
    wl_list_for_each(h, &server->hooks, link)
    {
        if (strcmp(h->event, event) != 0) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, h->func_ref);
        push_workspace_table(L, server, out);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            wlr_log(WLR_ERROR, "hook '%s': %s", event,
                    lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}

void
fire_hook_output(nnwm_server *server, const char *event, nnwm_output *out)
{
    lua_State *L = server->lua;
    if (!L || !out) return;
    nnwm_hook *h;
    wl_list_for_each(h, &server->hooks, link)
    {
        if (strcmp(h->event, event) != 0) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, h->func_ref);
        push_output_table(L, server, out);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            wlr_log(WLR_ERROR, "hook '%s': %s", event,
                    lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}

int
nnwm_timer_cb(void *data)
{
    nnwm_timer *t = static_cast<nnwm_timer *>(data);
    if (t->dead) return 0;
    nnwm_server *server = t->server;
    lua_State *L = server->lua;
    lua_rawgeti(L, LUA_REGISTRYINDEX, t->func_ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        wlr_log(WLR_ERROR, "timer cb: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    if (t->interval_ms > 0)
    {
        wl_event_source_timer_update(t->source, t->interval_ms);
    }
    else
    {
        t->dead = true;
        wl_list_remove(&t->link);
        luaL_unref(L, LUA_REGISTRYINDEX, t->func_ref);
        wl_event_source_remove(t->source);
        delete t;
    }
    return 0;
}

static int
l_nnwm_on(lua_State *L)
{
    const char *event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    nnwm_server *server = get_server(L);
    nnwm_hook *h = new nnwm_hook{};
    h->event    = strdup(event);
    lua_pushvalue(L, 2);
    h->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    wl_list_insert(server->hooks.prev, &h->link);
    return 0;
}

static int
l_nnwm_timer(lua_State *L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    nnwm_server *server = get_server(L);
    nnwm_timer *t = new nnwm_timer{};
    t->server      = server;
    t->interval_ms = 0;
    t->dead        = false;
    lua_pushvalue(L, 2);
    t->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    t->source = wl_event_loop_add_timer(loop, nnwm_timer_cb, t);
    wl_event_source_timer_update(t->source, ms);
    wl_list_insert(server->timers.prev, &t->link);
    return 0;
}

static int
l_nnwm_interval(lua_State *L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    nnwm_server *server = get_server(L);
    nnwm_timer *t = new nnwm_timer{};
    t->server      = server;
    t->interval_ms = ms;
    t->dead        = false;
    lua_pushvalue(L, 2);
    t->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    t->source = wl_event_loop_add_timer(loop, nnwm_timer_cb, t);
    wl_event_source_timer_update(t->source, ms);
    wl_list_insert(server->timers.prev, &t->link);
    return 0;
}

/* ---- monitor configuration ---- */

static void
free_monitor_configs(struct nnwm_config *cfg)
{
    for (int i = 0; i < cfg->monitor_config_count; i++)
    {
        auto &mc = cfg->monitor_configs[i];
        free(mc.name);
        free(mc.description);
    }
    free(cfg->monitor_configs);
    cfg->monitor_configs      = nullptr;
    cfg->monitor_config_count = 0;
}

static int
l_nnwm_monitor(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    nnwm_server *server = get_server(L);
    nnwm_config *cfg    = server->config;

    int idx = cfg->monitor_config_count;
    cfg->monitor_configs = static_cast<nnwm_monitor_config *>(
        realloc(cfg->monitor_configs, (idx + 1) * sizeof(nnwm_monitor_config)));
    cfg->monitor_config_count = idx + 1;

    auto &mc = cfg->monitor_configs[idx];
    memset(&mc, 0, sizeof(mc));
    mc.x         = INT_MAX;
    mc.y         = INT_MAX;
    mc.transform = -1;

    lua_pushvalue(L, 1); /* push table to top so get_*_field helpers see it */

    mc.name        = get_string_field(L, "name", nullptr);
    mc.description = get_string_field(L, "description", nullptr);

    mc.width   = get_int_field(L, "width", 0);
    mc.height  = get_int_field(L, "height", 0);
    mc.refresh = get_int_field(L, "refresh", 0);

    lua_getfield(L, -1, "x");
    if (lua_isinteger(L, -1))
        mc.x = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, -1, "y");
    if (lua_isinteger(L, -1))
        mc.y = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    mc.scale = get_float_field(L, "scale", 0.0f);

    {
        char *ts = get_string_field(L, "transform", nullptr);
        if (ts)
        {
            if (strcmp(ts, "none") == 0)             mc.transform = 0;
            else if (strcmp(ts, "90") == 0)          mc.transform = 1;
            else if (strcmp(ts, "180") == 0)         mc.transform = 2;
            else if (strcmp(ts, "270") == 0)         mc.transform = 3;
            else if (strcmp(ts, "flipped") == 0)     mc.transform = 4;
            else if (strcmp(ts, "flipped-90") == 0)  mc.transform = 5;
            else if (strcmp(ts, "flipped-180") == 0) mc.transform = 6;
            else if (strcmp(ts, "flipped-270") == 0) mc.transform = 7;
            else                                     mc.transform = -1;
            free(ts);
        }
    }

    mc.hdr      = get_bool_field(L, "hdr", false);
    mc.disabled = get_bool_field(L, "disabled", false);

    lua_getfield(L, -1, "struts");
    if (lua_istable(L, -1))
    {
        mc.strut_top    = get_int_field(L, "top",    0);
        mc.strut_bottom = get_int_field(L, "bottom", 0);
        mc.strut_left   = get_int_field(L, "left",   0);
        mc.strut_right  = get_int_field(L, "right",  0);
    }
    lua_pop(L, 1); /* pop struts */

    lua_pop(L, 1); /* pop table copy */
    return 0;
}

static const struct luaL_Reg nnwm_funcs[] = {
    {"key", l_nnwm_key},
    {"gesture", l_nnwm_gesture},
    {"monitor", l_nnwm_monitor},
    {"quit", l_nnwm_quit},
    {"close", l_nnwm_close},
    {"spawn", l_nnwm_spawn},
    {"spawn_once", l_nnwm_spawn_once},
    {"focus_left", l_nnwm_focus_left},
    {"focus_right", l_nnwm_focus_right},
    {"focus_next", l_nnwm_focus_next},
    {"focus_prev", l_nnwm_focus_prev},
    {"focus_next_float", l_nnwm_focus_next_float},
    {"focus_prev_float", l_nnwm_focus_prev_float},
    {"focus_mode_toggle", l_nnwm_focus_mode_toggle},
    {"focus_dir", l_nnwm_focus_dir},
    {"move_dir", l_nnwm_move_dir},
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
    {"toggle_fake_fullscreen", l_nnwm_toggle_fake_fullscreen},
    {"toggle_maximize", l_nnwm_toggle_maximize},
    {"toggle_sticky", l_nnwm_toggle_sticky},
    {"toggle_overview", l_nnwm_toggle_overview},
    {"move_to_scratchpad", l_nnwm_move_to_scratchpad},
    {"scratchpad_toggle", l_nnwm_scratchpad_toggle},
    {"focus_monitor_next", l_nnwm_focus_monitor_next},
    {"focus_monitor_prev", l_nnwm_focus_monitor_prev},
    {"move_to_monitor_next", l_nnwm_move_to_monitor_next},
    {"move_to_monitor_prev", l_nnwm_move_to_monitor_prev},
    {"host_name", l_nnwm_host_name},
    {"rule", l_nnwm_rule},
    {"current_window",    l_nnwm_current_window},
    {"current_workspace", l_nnwm_current_workspace},
    {"current_output",    l_nnwm_current_output},
    {"on",                l_nnwm_on},
    {"timer",             l_nnwm_timer},
    {"interval",          l_nnwm_interval},
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

    /* get or create nnwm.opt */
    lua_getfield(L, -1, "opt");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "opt");
    }
    /* stack: nnwm (-2), nnwm.opt (-1) */

    /* layout sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->new_window_master);
    lua_setfield(L, -2, "new_window_master");
    lua_pushboolean(L, cfg->center_new_floating);
    lua_setfield(L, -2, "center_new_floating");
    lua_pushnumber(L, cfg->layout.master_ratio);
    lua_setfield(L, -2, "master_ratio");
    lua_pushnumber(L, cfg->layout.master_ratio_step);
    lua_setfield(L, -2, "master_ratio_step");
    lua_pushnumber(L, cfg->layout.master_ratio_min);
    lua_setfield(L, -2, "master_ratio_min");
    lua_pushnumber(L, cfg->layout.master_ratio_max);
    lua_setfield(L, -2, "master_ratio_max");
    lua_pushnumber(L, cfg->scroll_column_width);
    lua_setfield(L, -2, "scroll_column_width");
    lua_pushnumber(L, cfg->scroll_row_height);
    lua_setfield(L, -2, "scroll_row_height");
    /* layout.tabbed sub-table */
    lua_newtable(L);
    lua_pushstring(L, cfg->layout.tab_style == nnwm_tab_style::MINIMAL
                          ? "minimal" : "normal");
    lua_setfield(L, -2, "tab_style");
    {
        const char *tp = "top";
        switch (cfg->layout.tab_position)
        {
            case nnwm_tab_position::BOTTOM: tp = "bottom"; break;
            case nnwm_tab_position::LEFT:   tp = "left";   break;
            case nnwm_tab_position::RIGHT:  tp = "right";  break;
            default: break;
        }
        lua_pushstring(L, tp);
        lua_setfield(L, -2, "tab_position");
    }
    lua_pushinteger(L, cfg->layout.tab_bar_height);
    lua_setfield(L, -2, "height");
    lua_pushboolean(L, cfg->layout.tab_smart);
    lua_setfield(L, -2, "smart");
    lua_setfield(L, -2, "tabbed");
    lua_setfield(L, -2, "layout");

    /* gaps sub-table */
    lua_newtable(L);
    lua_pushinteger(L, cfg->gap.inner);
    lua_setfield(L, -2, "inner");
    lua_pushinteger(L, cfg->gap.outer);
    lua_setfield(L, -2, "outer");
    lua_pushboolean(L, cfg->gap.smart);
    lua_setfield(L, -2, "smart");
    lua_setfield(L, -2, "gaps");

    /* border sub-table */
    lua_newtable(L);
    lua_pushinteger(L, cfg->border.width);
    lua_setfield(L, -2, "width");
    lua_pushboolean(L, cfg->border.smart);
    lua_setfield(L, -2, "smart");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->border.focused_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "focused_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->border.unfocused_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "unfocused_color");
    lua_setfield(L, -2, "border");

    /* keyboard sub-table */
    lua_newtable(L);
    lua_pushinteger(L, cfg->keyboard.repeat_rate);
    lua_setfield(L, -2, "repeat_rate");
    lua_pushinteger(L, cfg->keyboard.repeat_delay);
    lua_setfield(L, -2, "repeat_delay");
    lua_pushstring(L, cfg->keyboard.xkb_rules ? cfg->keyboard.xkb_rules : "");
    lua_setfield(L, -2, "xkb_rules");
    lua_pushstring(L, cfg->keyboard.xkb_layout ? cfg->keyboard.xkb_layout : "");
    lua_setfield(L, -2, "xkb_layout");
    lua_pushstring(L, cfg->keyboard.xkb_variant ? cfg->keyboard.xkb_variant : "");
    lua_setfield(L, -2, "xkb_variant");
    lua_pushstring(L, cfg->keyboard.xkb_options ? cfg->keyboard.xkb_options : "");
    lua_setfield(L, -2, "xkb_options");
    lua_setfield(L, -2, "keyboard");

    lua_pushstring(L, cfg->seat_name);
    lua_setfield(L, -2, "seat_name");

    /* touchpad sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->touchpad.enabled);
    lua_setfield(L, -2, "enabled");
    lua_pushboolean(L, cfg->touchpad.tap_to_click);
    lua_setfield(L, -2, "tap_to_click");
    lua_pushboolean(L, cfg->touchpad.natural_scroll);
    lua_setfield(L, -2, "natural_scroll");
    lua_pushboolean(L, cfg->touchpad.disable_while_typing);
    lua_setfield(L, -2, "disable_while_typing");
    lua_pushboolean(L, cfg->touchpad.disable_on_external_mouse);
    lua_setfield(L, -2, "disable_on_external_mouse");
    lua_pushboolean(L, cfg->touchpad.drag);
    lua_setfield(L, -2, "drag");
    lua_pushnumber(L, cfg->touchpad.scroll_factor);
    lua_setfield(L, -2, "scroll_factor");
    {
        const char *sm = "two_finger";
        switch (cfg->touchpad.scroll_method)
        {
            case 0: sm = "no_scroll";      break;
            case 2: sm = "edge";           break;
            case 3: sm = "on_button_down"; break;
            default: break;
        }
        lua_pushstring(L, sm);
        lua_setfield(L, -2, "scroll_method");
    }
    lua_setfield(L, -2, "touchpad");

    /* mouse sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->focus_follows_mouse);
    lua_setfield(L, -2, "focus_follows_mouse");
    lua_pushboolean(L, cfg->workspace_back_and_forth);
    lua_setfield(L, -2, "workspace_back_and_forth");
    lua_pushstring(L, cfg->cursor_theme);
    lua_setfield(L, -2, "cursor_theme");
    lua_pushinteger(L, cfg->cursor_size);
    lua_setfield(L, -2, "cursor_size");
    lua_pushnumber(L, cfg->mouse.accel_speed);
    lua_setfield(L, -2, "accel_speed");
    {
        const char *ap = "adaptive";
        switch (cfg->mouse.accel_profile)
        {
            case 1: ap = "flat"; break;
            case 2: ap = "none"; break;
            default: break;
        }
        lua_pushstring(L, ap);
        lua_setfield(L, -2, "accel_profile");
    }
    lua_pushboolean(L, cfg->mouse.natural_scroll);
    lua_setfield(L, -2, "natural_scroll");
    lua_pushboolean(L, cfg->mouse.disable_while_typing);
    lua_setfield(L, -2, "disable_while_typing");
    lua_pushboolean(L, cfg->mouse.hide_cursor_when_typing);
    lua_setfield(L, -2, "hide_cursor_when_typing");
    lua_pushboolean(L, cfg->mouse.warp_to_focused_window);
    lua_setfield(L, -2, "warp_to_focused_window");
    lua_setfield(L, -2, "mouse");

    lua_pushboolean(L, cfg->clipboard_enabled);
    lua_setfield(L, -2, "clipboard");
    lua_pushboolean(L, cfg->client_decorations);
    lua_setfield(L, -2, "client_decorations");

    lua_pushinteger(L, cfg->workspace_count);
    lua_setfield(L, -2, "workspace_count");
    lua_newtable(L);
    for (int i = 0; i < cfg->workspace_count; i++)
    {
        if (cfg->workspace_names[i])
            lua_pushstring(L, cfg->workspace_names[i]);
        else
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", i + 1);
            lua_pushstring(L, buf);
        }
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "workspace_names");

    /* titlebar sub-table */
    lua_newtable(L);
    lua_pushboolean(L, cfg->titlebar.height > 0);
    lua_setfield(L, -2, "enabled");
    lua_pushinteger(L, cfg->titlebar.height > 0 ? cfg->titlebar.height : 20);
    lua_setfield(L, -2, "height");
    lua_pushstring(L, cfg->titlebar.font ? cfg->titlebar.font : "Sans 10");
    lua_setfield(L, -2, "font");
    lua_pushinteger(L, cfg->titlebar.text_align);
    lua_setfield(L, -2, "text_align");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->titlebar.bg_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "bg_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->titlebar.focused_bg_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "focused_bg_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->titlebar.urgent_bg_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "urgent_bg_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->titlebar.text_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "text_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->titlebar.focused_text_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "focused_text_color");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->titlebar.urgent_text_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "urgent_text_color");
    lua_setfield(L, -2, "titlebar");

    /* fx sub-table (scenefx: corner radius and shadows) */
    lua_newtable(L);
    lua_newtable(L);
    lua_pushinteger(L, cfg->fx.rounding.radius);
    lua_setfield(L, -2, "radius");
    lua_pushboolean(L, cfg->fx.rounding.smart);
    lua_setfield(L, -2, "smart");
    lua_setfield(L, -2, "rounding");
    lua_newtable(L);
    lua_pushboolean(L, cfg->fx.shadow_enabled);
    lua_setfield(L, -2, "enabled");
    lua_pushnumber(L, cfg->fx.shadow_blur_sigma);
    lua_setfield(L, -2, "blur_sigma");
    lua_pushnumber(L, cfg->fx.shadow_offset_x);
    lua_setfield(L, -2, "offset_x");
    lua_pushnumber(L, cfg->fx.shadow_offset_y);
    lua_setfield(L, -2, "offset_y");
    lua_newtable(L);
    for (int i = 0; i < 4; i++)
    {
        lua_pushnumber(L, cfg->fx.shadow_color[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "color");
    lua_setfield(L, -2, "shadow");
    lua_pushnumber(L, cfg->fx.opacity);
    lua_setfield(L, -2, "opacity");
    lua_pushnumber(L, cfg->fx.focused_opacity);
    lua_setfield(L, -2, "focused_opacity");
    lua_pushnumber(L, cfg->fx.unfocused_opacity);
    lua_setfield(L, -2, "unfocused_opacity");
    lua_newtable(L);
    lua_pushboolean(L, cfg->fx.blur_enabled);
    lua_setfield(L, -2, "enabled");
    lua_pushinteger(L, cfg->fx.blur_passes);
    lua_setfield(L, -2, "passes");
    lua_pushinteger(L, cfg->fx.blur_radius);
    lua_setfield(L, -2, "radius");
    lua_pushnumber(L, cfg->fx.blur_noise);
    lua_setfield(L, -2, "noise");
    lua_pushnumber(L, cfg->fx.blur_brightness);
    lua_setfield(L, -2, "brightness");
    lua_pushnumber(L, cfg->fx.blur_contrast);
    lua_setfield(L, -2, "contrast");
    lua_pushnumber(L, cfg->fx.blur_saturation);
    lua_setfield(L, -2, "saturation");
    lua_setfield(L, -2, "blur");
#ifdef HAVE_SCENEFX
    /* animations sub-table (nested inside fx) */
    lua_newtable(L);
    lua_pushboolean(L, cfg->fx.animation.enabled);
    lua_setfield(L, -2, "enabled");
    lua_pushinteger(L, cfg->fx.animation.duration_ms);
    lua_setfield(L, -2, "duration");
    /* global easing string */
    {
        const char *easing_names[] = {"ease_out",    "linear", "ease_in",
                                      "ease_in_out", "bounce", "elastic"};
        lua_pushstring(L, easing_names[static_cast<int>(cfg->fx.animation.easing)]);
        lua_setfield(L, -2, "easing");
    }
    /* open sub-table */
    lua_newtable(L);
    {
        const char *open_names[]
            = {"fade_scale", "fade",       "scale",       "slide_up",
               "slide_down", "slide_left", "slide_right", "none"};
        lua_pushstring(L, open_names[static_cast<int>(cfg->fx.animation.open_style)]);
        lua_setfield(L, -2, "style");
    }
    lua_pushinteger(L, cfg->fx.animation.open_duration_ms);
    lua_setfield(L, -2, "duration");
    if (cfg->fx.animation.open_easing >= 0)
    {
        const char *easing_names[] = {"ease_out",    "linear", "ease_in",
                                      "ease_in_out", "bounce", "elastic"};
        lua_pushstring(L, easing_names[cfg->fx.animation.open_easing]);
    }
    else
    {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "easing");
    lua_setfield(L, -2, "open");
    /* close sub-table */
    lua_newtable(L);
    {
        const char *open_names[]
            = {"fade_scale", "fade",       "scale",       "slide_up",
               "slide_down", "slide_left", "slide_right", "none"};
        lua_pushstring(L, open_names[static_cast<int>(cfg->fx.animation.close_style)]);
        lua_setfield(L, -2, "style");
    }
    lua_pushinteger(L, cfg->fx.animation.close_duration_ms);
    lua_setfield(L, -2, "duration");
    if (cfg->fx.animation.close_easing >= 0)
    {
        const char *easing_names[] = {"ease_out",    "linear", "ease_in",
                                      "ease_in_out", "bounce", "elastic"};
        lua_pushstring(L, easing_names[cfg->fx.animation.close_easing]);
    }
    else
    {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "easing");
    lua_setfield(L, -2, "close");
    /* workspace sub-table */
    lua_newtable(L);
    {
        const char *ws_names[] = {"slide", "fade", "none"};
        lua_pushstring(L, ws_names[static_cast<int>(cfg->fx.animation.ws_style)]);
        lua_setfield(L, -2, "style");
    }
    lua_pushinteger(L, cfg->fx.animation.ws_duration_ms);
    lua_setfield(L, -2, "duration");
    if (cfg->fx.animation.ws_easing >= 0)
    {
        const char *easing_names[] = {"ease_out",    "linear", "ease_in",
                                      "ease_in_out", "bounce", "elastic"};
        lua_pushstring(L, easing_names[cfg->fx.animation.ws_easing]);
    }
    else
    {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "easing");
    lua_setfield(L, -2, "workspace");
    /* layout sub-table */
    lua_newtable(L);
    lua_pushstring(L, cfg->fx.animation.layout_style == nnwm_layout_anim::TWEEN
                          ? "tween"
                          : "none");
    lua_setfield(L, -2, "style");
    lua_pushinteger(L, cfg->fx.animation.layout_duration_ms);
    lua_setfield(L, -2, "duration");
    if (cfg->fx.animation.layout_easing >= 0)
    {
        const char *easing_names[] = {"ease_out",    "linear", "ease_in",
                                      "ease_in_out", "bounce", "elastic"};
        lua_pushstring(L, easing_names[cfg->fx.animation.layout_easing]);
    }
    else
    {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "easing");
    lua_setfield(L, -2, "layout");
    /* focus sub-table */
    lua_newtable(L);
    lua_pushstring(L, cfg->fx.animation.focus_style == nnwm_focus_style::CROSSFADE
                          ? "crossfade"
                          : "none");
    lua_setfield(L, -2, "style");
    lua_pushinteger(L, cfg->fx.animation.focus_duration_ms);
    lua_setfield(L, -2, "duration");
    if (cfg->fx.animation.focus_easing >= 0)
    {
        const char *easing_names[] = {"ease_out",    "linear", "ease_in",
                                      "ease_in_out", "bounce", "elastic"};
        lua_pushstring(L, easing_names[cfg->fx.animation.focus_easing]);
    }
    else
    {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "easing");
    lua_setfield(L, -2, "focus");
    lua_setfield(L, -2, "animations");
#endif /* HAVE_SCENEFX */
    lua_setfield(L, -2, "fx");

    lua_pop(L, 2); /* pop opt and nnwm */
}

static void
free_window_rules(struct nnwm_config *cfg)
{
    for (int i = 0; i < cfg->window_rule_count; i++)
    {
        auto &r = cfg->window_rules[i];
        free(r.app_id);
        free(r.title);
        free(r.monitor);
    }
    free(cfg->window_rules);
    cfg->window_rules      = nullptr;
    cfg->window_rule_count = 0;
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

    lua_getfield(L, -1, "opt");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 2);
        return;
    }
    /* stack: nnwm (-2), nnwm.opt (-1) */

    lua_getfield(L, -1, "layout");
    if (lua_istable(L, -1))
    {
        cfg->new_window_master
            = get_bool_field(L, "new_window_master", cfg->new_window_master);
        cfg->center_new_floating = get_bool_field(L, "center_new_floating",
                                                  cfg->center_new_floating);
        cfg->layout.master_ratio
            = get_float_field(L, "master_ratio", cfg->layout.master_ratio);
        cfg->layout.master_ratio_step
            = get_float_field(L, "master_ratio_step", cfg->layout.master_ratio_step);
        cfg->layout.master_ratio_min
            = get_float_field(L, "master_ratio_min", cfg->layout.master_ratio_min);
        cfg->layout.master_ratio_max
            = get_float_field(L, "master_ratio_max", cfg->layout.master_ratio_max);
        cfg->scroll_column_width = get_float_field(L, "scroll_column_width",
                                                   cfg->scroll_column_width);
        cfg->scroll_row_height = get_float_field(L, "scroll_row_height",
                                                 cfg->scroll_row_height);
        lua_getfield(L, -1, "tabbed");
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "tab_style");
            if (lua_isstring(L, -1))
            {
                const char *ts = lua_tostring(L, -1);
                cfg->layout.tab_style = (std::strcmp(ts, "minimal") == 0)
                                            ? nnwm_tab_style::MINIMAL
                                            : nnwm_tab_style::NORMAL;
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "tab_position");
            if (lua_isstring(L, -1))
            {
                const char *tp = lua_tostring(L, -1);
                if (std::strcmp(tp, "bottom") == 0)
                    cfg->layout.tab_position = nnwm_tab_position::BOTTOM;
                else if (std::strcmp(tp, "left") == 0)
                    cfg->layout.tab_position = nnwm_tab_position::LEFT;
                else if (std::strcmp(tp, "right") == 0)
                    cfg->layout.tab_position = nnwm_tab_position::RIGHT;
                else
                    cfg->layout.tab_position = nnwm_tab_position::TOP;
            }
            lua_pop(L, 1);

            cfg->layout.tab_bar_height
                = get_int_field(L, "height", cfg->layout.tab_bar_height);
            cfg->layout.tab_smart
                = get_bool_field(L, "smart", cfg->layout.tab_smart);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "gaps");
    if (lua_istable(L, -1))
    {
        cfg->gap.inner  = get_int_field(L, "inner", cfg->gap.inner);
        cfg->gap.outer  = get_int_field(L, "outer", cfg->gap.outer);
        cfg->gap.smart = get_bool_field(L, "smart", cfg->gap.smart);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "border");
    if (lua_istable(L, -1))
    {
        cfg->border.width  = get_int_field(L, "width", cfg->border.width);
        cfg->border.smart = get_bool_field(L, "smart", cfg->border.smart);
        float dflt_foc[4]  = {cfg->border.focused_color[0], cfg->border.focused_color[1],
                              cfg->border.focused_color[2], cfg->border.focused_color[3]};
        get_color_field(L, "focused_color", cfg->border.focused_color, dflt_foc);
        float dflt_unf[4] = {cfg->border.unfocused_color[0], cfg->border.unfocused_color[1],
                             cfg->border.unfocused_color[2], cfg->border.unfocused_color[3]};
        get_color_field(L, "unfocused_color", cfg->border.unfocused_color, dflt_unf);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "keyboard");
    if (lua_istable(L, -1))
    {
        cfg->keyboard.repeat_rate
            = get_int_field(L, "repeat_rate", cfg->keyboard.repeat_rate);
        cfg->keyboard.repeat_delay
            = get_int_field(L, "repeat_delay", cfg->keyboard.repeat_delay);
        char *s = get_string_field(L, "xkb_rules", cfg->keyboard.xkb_rules);
        free(cfg->keyboard.xkb_rules);
        cfg->keyboard.xkb_rules = s;
        s = get_string_field(L, "xkb_layout", cfg->keyboard.xkb_layout);
        free(cfg->keyboard.xkb_layout);
        cfg->keyboard.xkb_layout = s;
        s = get_string_field(L, "xkb_variant", cfg->keyboard.xkb_variant);
        free(cfg->keyboard.xkb_variant);
        cfg->keyboard.xkb_variant = s;
        s = get_string_field(L, "xkb_options", cfg->keyboard.xkb_options);
        free(cfg->keyboard.xkb_options);
        cfg->keyboard.xkb_options = s;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "touchpad");
    if (lua_istable(L, -1))
    {
        cfg->touchpad.enabled
            = get_bool_field(L, "enabled", cfg->touchpad.enabled);
        cfg->touchpad.tap_to_click
            = get_bool_field(L, "tap_to_click", cfg->touchpad.tap_to_click);
        cfg->touchpad.natural_scroll
            = get_bool_field(L, "natural_scroll", cfg->touchpad.natural_scroll);
        cfg->touchpad.disable_while_typing = get_bool_field(
            L, "disable_while_typing", cfg->touchpad.disable_while_typing);
        cfg->touchpad.disable_on_external_mouse = get_bool_field(
            L, "disable_on_external_mouse",
            cfg->touchpad.disable_on_external_mouse);
        cfg->touchpad.drag
            = get_bool_field(L, "drag", cfg->touchpad.drag);
        cfg->touchpad.scroll_factor
            = get_float_field(L, "scroll_factor", cfg->touchpad.scroll_factor);
        lua_getfield(L, -1, "scroll_method");
        if (lua_isstring(L, -1))
        {
            const char *sm = lua_tostring(L, -1);
            if (std::strcmp(sm, "no_scroll") == 0)
                cfg->touchpad.scroll_method = 0;
            else if (std::strcmp(sm, "edge") == 0)
                cfg->touchpad.scroll_method = 2;
            else if (std::strcmp(sm, "on_button_down") == 0)
                cfg->touchpad.scroll_method = 3;
            else
                cfg->touchpad.scroll_method = 1; /* two_finger */
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "mouse");
    if (lua_istable(L, -1))
    {
        cfg->focus_follows_mouse = get_bool_field(L, "focus_follows_mouse",
                                                  cfg->focus_follows_mouse);
        cfg->workspace_back_and_forth = get_bool_field(
            L, "workspace_back_and_forth", cfg->workspace_back_and_forth);
        cfg->show_config_error_overlay = get_bool_field(
            L, "show_config_error_overlay", cfg->show_config_error_overlay);
        char *ct = get_string_field(L, "cursor_theme", cfg->cursor_theme);
        free(cfg->cursor_theme);
        cfg->cursor_theme = ct;
        cfg->cursor_size  = get_int_field(L, "cursor_size", cfg->cursor_size);
        cfg->mouse.accel_speed
            = get_float_field(L, "accel_speed", cfg->mouse.accel_speed);
        lua_getfield(L, -1, "accel_profile");
        if (lua_isstring(L, -1))
        {
            const char *ap = lua_tostring(L, -1);
            if (std::strcmp(ap, "flat") == 0)
                cfg->mouse.accel_profile = 1;
            else if (std::strcmp(ap, "none") == 0)
                cfg->mouse.accel_profile = 2;
            else
                cfg->mouse.accel_profile = 0;
        }
        lua_pop(L, 1);
        cfg->mouse.natural_scroll
            = get_bool_field(L, "natural_scroll", cfg->mouse.natural_scroll);
        cfg->mouse.disable_while_typing = get_bool_field(
            L, "disable_while_typing", cfg->mouse.disable_while_typing);
        cfg->mouse.hide_cursor_when_typing = get_bool_field(
            L, "hide_cursor_when_typing", cfg->mouse.hide_cursor_when_typing);
        cfg->mouse.warp_to_focused_window = get_bool_field(
            L, "warp_to_focused_window", cfg->mouse.warp_to_focused_window);
    }
    lua_pop(L, 1);

    cfg->clipboard_enabled
        = get_bool_field(L, "clipboard", cfg->clipboard_enabled);
    cfg->client_decorations
        = get_bool_field(L, "client_decorations", cfg->client_decorations);

    {
        lua_getfield(L, -1, "workspace_count");
        if (lua_isnumber(L, -1))
        {
            int n = (int)lua_tointeger(L, -1);
            if (n >= 1 && n <= NNWM_NUM_WORKSPACES)
                cfg->workspace_count = n;
            else
                luaL_error(L, "workspace_count must be 1–%d", NNWM_NUM_WORKSPACES);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "workspace_names");
        if (lua_istable(L, -1))
        {
            for (int i = 0; i < NNWM_NUM_WORKSPACES; i++)
            {
                free(cfg->workspace_names[i]);
                cfg->workspace_names[i] = nullptr;
                lua_rawgeti(L, -1, i + 1);
                if (lua_isstring(L, -1))
                    cfg->workspace_names[i] = strdup(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    lua_getfield(L, -1, "titlebar");
    if (lua_istable(L, -1))
    {
        bool tb_enabled
            = get_bool_field(L, "enabled", cfg->titlebar.height > 0);
        if (!tb_enabled)
        {
            cfg->titlebar.height = 0;
        }
        else
        {
            int h = get_int_field(
                L, "height",
                cfg->titlebar.height > 0 ? cfg->titlebar.height : 20);
            cfg->titlebar.height = h > 0 ? h : 20;
        }
        cfg->titlebar.text_align
            = get_int_field(L, "text_align", cfg->titlebar.text_align);
        char *tf = get_string_field(L, "font", cfg->titlebar.font);
        free(cfg->titlebar.font);
        cfg->titlebar.font = tf;
        float dflt_tbg[4]
            = {cfg->titlebar.bg_color[0], cfg->titlebar.bg_color[1],
               cfg->titlebar.bg_color[2], cfg->titlebar.bg_color[3]};
        get_color_field(L, "bg_color", cfg->titlebar.bg_color, dflt_tbg);
        float dflt_tfbg[4] = {cfg->titlebar.focused_bg_color[0],
                              cfg->titlebar.focused_bg_color[1],
                              cfg->titlebar.focused_bg_color[2],
                              cfg->titlebar.focused_bg_color[3]};
        get_color_field(L, "focused_bg_color", cfg->titlebar.focused_bg_color,
                        dflt_tfbg);
        float dflt_tubg[4] = {cfg->titlebar.urgent_bg_color[0],
                              cfg->titlebar.urgent_bg_color[1],
                              cfg->titlebar.urgent_bg_color[2],
                              cfg->titlebar.urgent_bg_color[3]};
        get_color_field(L, "urgent_bg_color", cfg->titlebar.urgent_bg_color,
                        dflt_tubg);
        float dflt_ttc[4]
            = {cfg->titlebar.text_color[0], cfg->titlebar.text_color[1],
               cfg->titlebar.text_color[2], cfg->titlebar.text_color[3]};
        get_color_field(L, "text_color", cfg->titlebar.text_color, dflt_ttc);
        float dflt_tftc[4] = {cfg->titlebar.focused_text_color[0],
                              cfg->titlebar.focused_text_color[1],
                              cfg->titlebar.focused_text_color[2],
                              cfg->titlebar.focused_text_color[3]};
        get_color_field(L, "focused_text_color",
                        cfg->titlebar.focused_text_color, dflt_tftc);
        float dflt_tutc[4] = {cfg->titlebar.urgent_text_color[0],
                              cfg->titlebar.urgent_text_color[1],
                              cfg->titlebar.urgent_text_color[2],
                              cfg->titlebar.urgent_text_color[3]};
        get_color_field(L, "urgent_text_color", cfg->titlebar.urgent_text_color,
                        dflt_tutc);

    }
    lua_pop(L, 1);

    char *s = get_string_field(L, "seat_name", cfg->seat_name);
    free(cfg->seat_name);
    cfg->seat_name = s;

    lua_getfield(L, -1, "fx");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "rounding");
        if (lua_istable(L, -1))
        {
            cfg->fx.rounding.radius
                = get_int_field(L, "radius", cfg->fx.rounding.radius);
            cfg->fx.rounding.smart
                = get_bool_field(L, "smart", cfg->fx.rounding.smart);
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "shadow");
        if (lua_istable(L, -1))
        {
            cfg->fx.shadow_enabled
                = get_bool_field(L, "enabled", cfg->fx.shadow_enabled);
            cfg->fx.shadow_blur_sigma
                = get_float_field(L, "blur_sigma", cfg->fx.shadow_blur_sigma);
            cfg->fx.shadow_offset_x
                = get_float_field(L, "offset_x", cfg->fx.shadow_offset_x);
            cfg->fx.shadow_offset_y
                = get_float_field(L, "offset_y", cfg->fx.shadow_offset_y);
            float dflt[4] = {cfg->fx.shadow_color[0], cfg->fx.shadow_color[1],
                             cfg->fx.shadow_color[2], cfg->fx.shadow_color[3]};
            get_color_field(L, "color", cfg->fx.shadow_color, dflt);
        }
        lua_pop(L, 1);
        cfg->fx.opacity = get_float_field(L, "opacity", cfg->fx.opacity);
        cfg->fx.focused_opacity
            = get_float_field(L, "focused_opacity", cfg->fx.focused_opacity);
        cfg->fx.unfocused_opacity
            = get_float_field(L, "unfocused_opacity", cfg->fx.unfocused_opacity);
        lua_getfield(L, -1, "blur");
        if (lua_istable(L, -1))
        {
            cfg->fx.blur_enabled = get_bool_field(L, "enabled", cfg->fx.blur_enabled);
            cfg->fx.blur_passes  = get_int_field(L, "passes", cfg->fx.blur_passes);
            cfg->fx.blur_radius  = get_int_field(L, "radius", cfg->fx.blur_radius);
            cfg->fx.blur_noise   = get_float_field(L, "noise", cfg->fx.blur_noise);
            cfg->fx.blur_brightness
                = get_float_field(L, "brightness", cfg->fx.blur_brightness);
            cfg->fx.blur_contrast
                = get_float_field(L, "contrast", cfg->fx.blur_contrast);
            cfg->fx.blur_saturation
                = get_float_field(L, "saturation", cfg->fx.blur_saturation);
        }
        lua_pop(L, 1);
#ifdef HAVE_SCENEFX
        lua_getfield(L, -1, "animations");
        if (lua_istable(L, -1))
        {
            cfg->fx.animation.enabled = get_bool_field(L, "enabled", cfg->fx.animation.enabled);
            cfg->fx.animation.duration_ms
                = get_int_field(L, "duration", cfg->fx.animation.duration_ms);
            /* global easing */
            {
                char *s = get_string_field(L, "easing", nullptr);
                if (s)
                {
                    cfg->fx.animation.easing = parse_easing(s, cfg->fx.animation.easing);
                    free(s);
                }
            }
            /* open sub-table */
            lua_getfield(L, -1, "open");
            if (lua_istable(L, -1))
            {
                char *s = get_string_field(L, "style", nullptr);
                if (s)
                {
                    cfg->fx.animation.open_style
                        = parse_open_style(s, cfg->fx.animation.open_style);
                    free(s);
                }
                cfg->fx.animation.open_duration_ms
                    = get_int_field(L, "duration", cfg->fx.animation.open_duration_ms);
                char *e = get_string_field(L, "easing", nullptr);
                if (e)
                {
                    cfg->fx.animation.open_easing
                        = static_cast<int>(parse_easing(e, cfg->fx.animation.easing));
                    free(e);
                }
            }
            lua_pop(L, 1);
            /* close sub-table */
            lua_getfield(L, -1, "close");
            if (lua_istable(L, -1))
            {
                char *s = get_string_field(L, "style", nullptr);
                if (s)
                {
                    cfg->fx.animation.close_style
                        = parse_open_style(s, cfg->fx.animation.close_style);
                    free(s);
                }
                cfg->fx.animation.close_duration_ms
                    = get_int_field(L, "duration", cfg->fx.animation.close_duration_ms);
                char *e = get_string_field(L, "easing", nullptr);
                if (e)
                {
                    cfg->fx.animation.close_easing
                        = static_cast<int>(parse_easing(e, cfg->fx.animation.easing));
                    free(e);
                }
            }
            lua_pop(L, 1);
            /* workspace sub-table */
            lua_getfield(L, -1, "workspace");
            if (lua_istable(L, -1))
            {
                char *s = get_string_field(L, "style", nullptr);
                if (s)
                {
                    if (strcmp(s, "slide") == 0)
                        cfg->fx.animation.ws_style = nnwm_ws_style::SLIDE;
                    else if (strcmp(s, "fade") == 0)
                        cfg->fx.animation.ws_style = nnwm_ws_style::FADE;
                    else if (strcmp(s, "none") == 0)
                        cfg->fx.animation.ws_style = nnwm_ws_style::NONE;
                    free(s);
                }
                cfg->fx.animation.ws_duration_ms
                    = get_int_field(L, "duration", cfg->fx.animation.ws_duration_ms);
                char *e = get_string_field(L, "easing", nullptr);
                if (e)
                {
                    cfg->fx.animation.ws_easing
                        = static_cast<int>(parse_easing(e, cfg->fx.animation.easing));
                    free(e);
                }
            }
            lua_pop(L, 1);
            /* layout sub-table */
            lua_getfield(L, -1, "layout");
            if (lua_istable(L, -1))
            {
                char *s = get_string_field(L, "style", nullptr);
                if (s)
                {
                    if (strcmp(s, "tween") == 0)
                        cfg->fx.animation.layout_style = nnwm_layout_anim::TWEEN;
                    else if (strcmp(s, "none") == 0)
                        cfg->fx.animation.layout_style = nnwm_layout_anim::NONE;
                    free(s);
                }
                cfg->fx.animation.layout_duration_ms = get_int_field(
                    L, "duration", cfg->fx.animation.layout_duration_ms);
                char *e = get_string_field(L, "easing", nullptr);
                if (e)
                {
                    cfg->fx.animation.layout_easing
                        = static_cast<int>(parse_easing(e, cfg->fx.animation.easing));
                    free(e);
                }
            }
            lua_pop(L, 1);
            /* focus sub-table */
            lua_getfield(L, -1, "focus");
            if (lua_istable(L, -1))
            {
                char *s = get_string_field(L, "style", nullptr);
                if (s)
                {
                    if (strcmp(s, "crossfade") == 0)
                        cfg->fx.animation.focus_style = nnwm_focus_style::CROSSFADE;
                    else if (strcmp(s, "none") == 0)
                        cfg->fx.animation.focus_style = nnwm_focus_style::NONE;
                    free(s);
                }
                cfg->fx.animation.focus_duration_ms
                    = get_int_field(L, "duration", cfg->fx.animation.focus_duration_ms);
                char *e = get_string_field(L, "easing", nullptr);
                if (e)
                {
                    cfg->fx.animation.focus_easing
                        = static_cast<int>(parse_easing(e, cfg->fx.animation.easing));
                    free(e);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1); /* pop animations or nil */
#endif                 /* HAVE_SCENEFX */
    }
    lua_pop(L, 1); /* pop fx or nil */

    lua_pop(L, 2); /* pop opt and nnwm */
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

    /* nnwm.layout sub-table */
    lua_newtable(server->lua); /* nnwm.layout        */

    lua_newtable(server->lua); /* nnwm.layout.vtile  */
    lua_pushcfunction(server->lua, l_nnwm_toggle_vertical_tile);
    lua_setfield(server->lua, -2, "toggle");
    lua_setfield(server->lua, -2, "vtile");

    lua_newtable(server->lua); /* nnwm.layout.tabbed */
    lua_pushcfunction(server->lua, l_nnwm_toggle_tabbed);
    lua_setfield(server->lua, -2, "toggle");
    lua_setfield(server->lua, -2, "tabbed");

    lua_newtable(server->lua); /* nnwm.layout.hscroll */
    lua_pushcfunction(server->lua, l_nnwm_toggle_horizontal_scroll);
    lua_setfield(server->lua, -2, "toggle");
    lua_setfield(server->lua, -2, "hscroll");

    lua_newtable(server->lua); /* nnwm.layout.vscroll */
    lua_pushcfunction(server->lua, l_nnwm_toggle_vertical_scroll);
    lua_setfield(server->lua, -2, "toggle");
    lua_setfield(server->lua, -2, "vscroll");

    lua_pushcfunction(server->lua, l_nnwm_layout_next);
    lua_setfield(server->lua, -2, "next");

    lua_pushcfunction(server->lua, l_nnwm_layout_prev);
    lua_setfield(server->lua, -2, "prev");
    lua_setfield(server->lua, -2, "layout");

    lua_setglobal(server->lua, "nnwm");

    /* Initialize keybinding registry */
    server->lua_keybindings      = nullptr;
    server->lua_keybinding_count = 0;
    server->lua_keybinding_cap   = 0;

    /* Initialize gesture registry */
    server->lua_gestures      = nullptr;
    server->lua_gesture_count = 0;
    server->lua_gesture_cap   = 0;
}

void
nnwm::lua_fini(struct nnwm_server *server)
{
    if (!server->lua)
        return;

    /* Release all Lua function references */
    for (int i = 0; i < server->lua_keybinding_count; i++)
    {
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_keybindings[i].func_ref);
        std::free(server->lua_keybindings[i].description);
    }
    std::free(server->lua_keybindings);
    server->lua_keybindings      = nullptr;
    server->lua_keybinding_count = 0;
    server->lua_keybinding_cap   = 0;

    for (int i = 0; i < server->lua_gesture_count; i++)
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_gestures[i].func_ref);
    std::free(server->lua_gestures);
    server->lua_gestures      = nullptr;
    server->lua_gesture_count = 0;
    server->lua_gesture_cap   = 0;

    {
        nnwm_hook *h, *h_tmp;
        wl_list_for_each_safe(h, h_tmp, &server->hooks, link)
        {
            luaL_unref(server->lua, LUA_REGISTRYINDEX, h->func_ref);
            free(h->event);
            wl_list_remove(&h->link);
            delete h;
        }
    }
    {
        nnwm_timer *t, *t_tmp;
        wl_list_for_each_safe(t, t_tmp, &server->timers, link)
        {
            if (!t->dead)
            {
                wl_event_source_remove(t->source);
                luaL_unref(server->lua, LUA_REGISTRYINDEX, t->func_ref);
            }
            wl_list_remove(&t->link);
            delete t;
        }
    }

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
    free_monitor_configs(cfg);

    /* Re-push config defaults into the nnwm table so read_config_table works */
    push_config_defaults(server->lua, cfg);

    if (luaL_dofile(server->lua, path) != LUA_OK)
    {
        const char *err = lua_tostring(server->lua, -1);
        std::fprintf(stderr, "nnwm: config error: %s\n", err);
        if (server->wayland_started && cfg->show_config_error_overlay)
            show_config_error(server, err);
        lua_pop(server->lua, 1);
    }
    else if (server->wayland_started)
    {
        hide_config_error(server);
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
    {
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_keybindings[i].func_ref);
        std::free(server->lua_keybindings[i].description);
    }
    server->lua_keybinding_count = 0;

    /* Clear existing gesture registrations */
    for (int i = 0; i < server->lua_gesture_count; i++)
        luaL_unref(server->lua, LUA_REGISTRYINDEX,
                   server->lua_gestures[i].func_ref);
    server->lua_gesture_count = 0;

    /* Clear existing window rules and monitor configs */
    free_window_rules(cfg);
    free_monitor_configs(cfg);

    /* Re-push defaults and re-run config */
    push_config_defaults(server->lua, cfg);

    if (luaL_dofile(server->lua, server->config_path) != LUA_OK)
    {
        const char *err = lua_tostring(server->lua, -1);
        std::fprintf(stderr, "nnwm: config error: %s\n", err);
        if (cfg->show_config_error_overlay)
            show_config_error(server, err);
        lua_pop(server->lua, 1);
    }
    else
    {
        hide_config_error(server);
    }

    read_config_table(server->lua, cfg);

    /* Clamp windows and output state to the new workspace count */
    {
        int max_ws = cfg->workspace_count - 1;
        struct nnwm_toplevel *tl;
        wl_list_for_each(tl, &server->toplevels, link)
        {
            if (tl->workspace > max_ws)
                tl->workspace = max_ws;
        }
        struct nnwm_output *out;
        wl_list_for_each(out, &server->outputs, link)
        {
            if (out->active_workspace > max_ws)
                out->active_workspace = max_ws;
            if (out->prev_workspace > max_ws)
                out->prev_workspace = max_ws;
        }
    }

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

void
nnwm::lua_handle_gesture(struct nnwm_server *server, int fingers,
                         double dx, double dy)
{
    if (!server->lua || server->lua_gesture_count == 0)
        return;

    nnwm_gesture_dir dir;
    if (std::fabs(dx) >= std::fabs(dy))
        dir = (dx >= 0) ? nnwm_gesture_dir::RIGHT : nnwm_gesture_dir::LEFT;
    else
        dir = (dy >= 0) ? nnwm_gesture_dir::DOWN : nnwm_gesture_dir::UP;

    for (int i = 0; i < server->lua_gesture_count; i++)
    {
        auto &g = server->lua_gestures[i];
        if (g.fingers == fingers && g.dir == dir)
        {
            lua_rawgeti(server->lua, LUA_REGISTRYINDEX, g.func_ref);
            if (lua_pcall(server->lua, 0, 0, 0) != LUA_OK)
            {
                std::fprintf(stderr, "nnwm: gesture error: %s\n",
                             lua_tostring(server->lua, -1));
                lua_pop(server->lua, 1);
            }
            return;
        }
    }
}

/* ---- non-keybinding config management ---- */

struct nnwm_config *
nnwm::config_defaults(void)
{
    auto *cfg = new nnwm_config{};

    cfg->layout.master_ratio        = 0.55f;
    cfg->layout.master_ratio_step   = 0.05f;
    cfg->layout.master_ratio_min    = 0.1f;
    cfg->layout.master_ratio_max    = 0.9f;
    cfg->scroll_column_width = 0.5f;
    cfg->scroll_row_height   = 0.5f;

    cfg->fx.rounding.radius = 0;
    cfg->fx.rounding.smart  = false;
    cfg->fx.shadow_enabled    = false;
    cfg->fx.shadow_blur_sigma = 10.0f;
    cfg->fx.shadow_color[0]   = 0.0f;
    cfg->fx.shadow_color[1]   = 0.0f;
    cfg->fx.shadow_color[2]   = 0.0f;
    cfg->fx.shadow_color[3]   = 0.5f;
    cfg->fx.shadow_offset_x   = 4.0f;
    cfg->fx.shadow_offset_y   = 4.0f;
    cfg->fx.opacity           = 1.0f;
    cfg->fx.focused_opacity   = -1.0f;
    cfg->fx.unfocused_opacity = -1.0f;
    cfg->fx.blur_enabled      = false;
    cfg->fx.blur_passes       = 3;
    cfg->fx.blur_radius       = 5;
    cfg->fx.blur_noise        = 0.0f;
    cfg->fx.blur_brightness   = 1.0f;
    cfg->fx.blur_contrast     = 1.0f;
    cfg->fx.blur_saturation   = 1.0f;

    cfg->gap.inner     = 0;
    cfg->gap.outer     = 0;
    cfg->gap.smart    = false;
    cfg->border.smart = false;

    cfg->border.width       = 2;
    cfg->border.focused_color[0]   = 0.3f;
    cfg->border.focused_color[1]   = 0.5f;
    cfg->border.focused_color[2]   = 0.8f;
    cfg->border.focused_color[3]   = 1.0f;
    cfg->border.unfocused_color[0] = 0.15f;
    cfg->border.unfocused_color[1] = 0.15f;
    cfg->border.unfocused_color[2] = 0.15f;
    cfg->border.unfocused_color[3] = 1.0f;

    cfg->keyboard.repeat_rate  = 25;
    cfg->keyboard.repeat_delay = 600;
    cfg->keyboard.xkb_rules    = strdup("");
    cfg->keyboard.xkb_layout   = strdup("");
    cfg->keyboard.xkb_variant  = strdup("");
    cfg->keyboard.xkb_options  = strdup("");

    cfg->cursor_theme = strdup("default");
    cfg->cursor_size  = 24;
    cfg->seat_name    = strdup("seat0");

    cfg->touchpad.enabled              = true;
    cfg->touchpad.tap_to_click         = true;
    cfg->touchpad.natural_scroll       = true;
    cfg->touchpad.disable_while_typing     = true;
    cfg->touchpad.disable_on_external_mouse = false;
    cfg->touchpad.drag                     = true;
    cfg->touchpad.scroll_factor        = 1.0f;
    cfg->touchpad.scroll_method        = 1; /* two_finger */

    cfg->focus_follows_mouse              = false;
    cfg->workspace_back_and_forth         = false;
    cfg->show_config_error_overlay        = true;
    cfg->mouse.accel_speed                = 0.0f;
    cfg->mouse.accel_profile              = 0; /* adaptive */
    cfg->mouse.natural_scroll             = false;
    cfg->mouse.disable_while_typing       = false;
    cfg->mouse.hide_cursor_when_typing    = false;
    cfg->mouse.warp_to_focused_window     = false;
    cfg->new_window_master   = true;
    cfg->center_new_floating = true;
    cfg->clipboard_enabled   = true;
    cfg->client_decorations  = false;

    cfg->titlebar.height                = 0;
    cfg->titlebar.font                  = strdup("Sans 10");
    cfg->titlebar.text_align            = 1; /* center */
    cfg->titlebar.bg_color[0]           = 0.2f;
    cfg->titlebar.bg_color[1]           = 0.2f;
    cfg->titlebar.bg_color[2]           = 0.2f;
    cfg->titlebar.bg_color[3]           = 1.0f;
    cfg->titlebar.focused_bg_color[0]   = 0.25f;
    cfg->titlebar.focused_bg_color[1]   = 0.35f;
    cfg->titlebar.focused_bg_color[2]   = 0.55f;
    cfg->titlebar.focused_bg_color[3]   = 1.0f;
    cfg->titlebar.urgent_bg_color[0]    = 0.7f;
    cfg->titlebar.urgent_bg_color[1]    = 0.2f;
    cfg->titlebar.urgent_bg_color[2]    = 0.2f;
    cfg->titlebar.urgent_bg_color[3]    = 1.0f;
    cfg->titlebar.text_color[0]         = 1.0f;
    cfg->titlebar.text_color[1]         = 1.0f;
    cfg->titlebar.text_color[2]         = 1.0f;
    cfg->titlebar.text_color[3]         = 1.0f;
    cfg->titlebar.focused_text_color[0] = 1.0f;
    cfg->titlebar.focused_text_color[1] = 1.0f;
    cfg->titlebar.focused_text_color[2] = 1.0f;
    cfg->titlebar.focused_text_color[3] = 1.0f;
    cfg->titlebar.urgent_text_color[0]  = 1.0f;
    cfg->titlebar.urgent_text_color[1]  = 1.0f;
    cfg->titlebar.urgent_text_color[2]  = 1.0f;
    cfg->titlebar.urgent_text_color[3]  = 1.0f;
    cfg->layout.tab_style             = nnwm_tab_style::NORMAL;
    cfg->layout.tab_position          = nnwm_tab_position::TOP;
    cfg->layout.tab_bar_height        = 24;
    cfg->layout.tab_smart             = false;

#ifdef HAVE_SCENEFX
    cfg->fx.animation.enabled            = true;
    cfg->fx.animation.duration_ms        = 250;
    cfg->fx.animation.easing             = nnwm_easing::OUT;
    cfg->fx.animation.open_style         = nnwm_open_style::FADE_SCALE;
    cfg->fx.animation.close_style        = nnwm_open_style::FADE;
    cfg->fx.animation.ws_style           = nnwm_ws_style::SLIDE;
    cfg->fx.animation.layout_style       = nnwm_layout_anim::TWEEN;
    cfg->fx.animation.focus_style        = nnwm_focus_style::CROSSFADE;
    cfg->fx.animation.open_easing        = -1; /* inherit global */
    cfg->fx.animation.close_easing       = -1;
    cfg->fx.animation.ws_easing          = -1;
    cfg->fx.animation.layout_easing      = -1;
    cfg->fx.animation.focus_easing       = -1;
    cfg->fx.animation.open_duration_ms   = 0; /* inherit global */
    cfg->fx.animation.close_duration_ms  = 0;
    cfg->fx.animation.ws_duration_ms     = 0;
    cfg->fx.animation.layout_duration_ms = 0;
    cfg->fx.animation.focus_duration_ms  = 0;
#endif /* HAVE_SCENEFX */

    cfg->monitor_configs      = nullptr;
    cfg->monitor_config_count = 0;

    cfg->window_rules      = nullptr;
    cfg->window_rule_count = 0;

    cfg->workspace_count = NNWM_NUM_WORKSPACES;
    for (int i = 0; i < NNWM_NUM_WORKSPACES; i++)
        cfg->workspace_names[i] = nullptr;

    return cfg;
}

void
nnwm::config_free(struct nnwm_config *cfg)
{
    if (!cfg)
        return;
    free(cfg->cursor_theme);
    free(cfg->seat_name);
    free(cfg->keyboard.xkb_rules);
    free(cfg->keyboard.xkb_layout);
    free(cfg->keyboard.xkb_variant);
    free(cfg->keyboard.xkb_options);
    free(cfg->titlebar.font);
    for (int i = 0; i < cfg->monitor_config_count; i++)
    {
        auto &mc = cfg->monitor_configs[i];
        free(mc.name);
        free(mc.description);
    }
    free(cfg->monitor_configs);
    free_window_rules(cfg);
    for (int i = 0; i < NNWM_NUM_WORKSPACES; i++)
        free(cfg->workspace_names[i]);
    delete cfg;
}
