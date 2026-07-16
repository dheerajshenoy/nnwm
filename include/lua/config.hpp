
#ifndef NNWM_LUA_CONFIG_HPP
#define NNWM_LUA_CONFIG_HPP

#include <cstdint>

struct nnwm_server;
struct nnwm_config;

namespace nnwm {

void lua_init(struct nnwm_server *server);
void lua_fini(struct nnwm_server *server);
void lua_load_config(struct nnwm_server *server, struct nnwm_config *cfg,
                     const char *path);
void lua_reload(struct nnwm_server *server, struct nnwm_config *cfg);
int  lua_handle_keybinding(struct nnwm_server *server,
                            uint32_t mods, unsigned int keysym);
void lua_handle_gesture(struct nnwm_server *server, int fingers,
                        double dx, double dy);

} // namespace nnwm

#endif /* NNWM_LUA_CONFIG_HPP */
