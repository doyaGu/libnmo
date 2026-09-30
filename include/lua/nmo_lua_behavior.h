#ifndef NMO_LUA_BEHAVIOR_H
#define NMO_LUA_BEHAVIOR_H

#include "edit/nmo_behavior_execute.h"
#include "lua/nmo_lua_runtime.h"

#define NMO_LUA_BEHAVIOR_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LUA_BEHAVIOR_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the Lua runtime of a behavior execution.
 *
 * The runtime is created on first use, with the platform bindings registered,
 * and lives until the execution ends. It used to be created before the action
 * callback ran; it is now created here, so a failure is reported as NULL.
 *
 * @return Runtime owned by the execution, or NULL if it cannot be created.
 * @ownership borrowed
 */
NMO_API nmo_lua_runtime_t *nmo_behavior_execution_lua_runtime(
    nmo_behavior_execution_t *execution);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LUA_BEHAVIOR_H */
