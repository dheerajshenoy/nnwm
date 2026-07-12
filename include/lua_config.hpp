
#ifndef NNWM_LUA_CONFIG_HPP
#define NNWM_LUA_CONFIG_HPP

struct nnwm_config;

#ifdef __cplusplus
extern "C"
{
#endif

/* Load configuration from a Lua script file.
 * Returns a newly allocated nnwm_config populated with defaults, then
 * overridden by whatever the script sets in the global `config` table.
 * Returns nullptr on error (file not found, syntax error, etc.). */
struct nnwm_config *nnwm_config_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* NNWM_LUA_CONFIG_HPP */
