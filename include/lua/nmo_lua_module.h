#ifndef NMO_LUA_MODULE_H
#define NMO_LUA_MODULE_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include "lua.h"

#define NMO_LUA_MODULE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LUA_MODULE_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_lua_module {
    const char *name;
    lua_CFunction open_fn;
} nmo_lua_module_t;

/**
 * @brief Register a Lua module into `package.preload`.
 *
 * This is the neutral module-registration hook used by subsystem binders.
 *
 * @param state Lua state
 * @param module Module descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_lua_module_register(lua_State *state,
                                             const nmo_lua_module_t *module);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LUA_MODULE_H */
