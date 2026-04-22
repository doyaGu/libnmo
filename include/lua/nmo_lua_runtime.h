#ifndef NMO_LUA_RUNTIME_H
#define NMO_LUA_RUNTIME_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "lua/nmo_lua_module.h"

#define NMO_LUA_RUNTIME_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LUA_RUNTIME_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_lua_runtime nmo_lua_runtime_t;

/**
 * @brief Create a Lua runtime with the standard libraries opened.
 *
 * @return Owned runtime, or NULL on failure.
 * @ownership owned
 */
NMO_API nmo_lua_runtime_t *nmo_lua_runtime_create(void);

/**
 * @brief Destroy a Lua runtime previously created by nmo_lua_runtime_create().
 */
NMO_API void nmo_lua_runtime_destroy(nmo_lua_runtime_t *runtime);

/**
 * @brief Execute a Lua chunk from a UTF-8 string.
 *
 * This is a minimal Task 3 smoke-test entry point used to verify the embedded
 * runtime can initialize and execute trivial chunks. Later tasks will add
 * structured module registration, traceback capture, and value marshalling.
 *
 * @param runtime Runtime
 * @param chunk Lua source string
 * @return NMO_OK on success, otherwise an error code with last-error text set
 */
NMO_API nmo_status_t nmo_lua_runtime_execute_string(nmo_lua_runtime_t *runtime,
                                                    const char *chunk);

/**
 * @brief Register a preloadable Lua module on this runtime.
 *
 * The module becomes available to `require()` within this runtime via
 * `package.preload`.
 *
 * @param runtime Runtime
 * @param module Module descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_lua_runtime_register_module(nmo_lua_runtime_t *runtime,
                                                     const nmo_lua_module_t *module);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LUA_RUNTIME_H */
