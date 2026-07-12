
#ifndef NNWM_LUA_CONFIG_HPP
#define NNWM_LUA_CONFIG_HPP

#include <cstdint>

struct nnwm_server;
struct nnwm_config;

#ifdef __cplusplus
extern "C"
{
#endif

/* Initialize the Lua state, register nnwm.key() and action functions. */
void nnwm_lua_init(struct nnwm_server *server);

/* Destroy the Lua state and free all keybinding references. */
void nnwm_lua_fini(struct nnwm_server *server);

/* Load a Lua config file into the persistent state.
 * Registers keybindings via nnwm.key() and populates cfg with
 * non-keybinding settings (colors, ratios, etc.). */
void nnwm_lua_load_config(struct nnwm_server *server, struct nnwm_config *cfg,
                           const char *path);

/* Reload config from the server's config_path. Clears existing
 * keybinding registrations and re-executes the config file. */
void nnwm_lua_reload(struct nnwm_server *server, struct nnwm_config *cfg);

/* Look up a keybinding and call its Lua callback.
 * Returns 1 if handled, 0 otherwise. */
int nnwm_lua_handle_keybinding(struct nnwm_server *server,
                                uint32_t mods, unsigned int keysym);

/* Non-keybinding config management */
struct nnwm_config *nnwm_config_defaults(void);
void nnwm_config_free(struct nnwm_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* NNWM_LUA_CONFIG_HPP */
