#ifndef NMO_LUA_SCRIPT_H
#define NMO_LUA_SCRIPT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include "lua.h"

#define NMO_LUA_SCRIPT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LUA_SCRIPT_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace nmo_workspace_t;

/**
 * @brief Push the script model of a workspace's document as a Lua table.
 *
 * The table is what the nmo.script module's model() returns: a snapshot of
 * the script model and the script index as plain tables that refer to each
 * other (nodes, IOs, parameters with their values, operations, links, data
 * edges, and message, array, and script uses), with the classes of
 * nmo.script as metatables. It copies everything it needs, so it outlives
 * the workspace.
 *
 * @return NMO_OK with the table pushed; on failure nothing is pushed.
 */
NMO_API nmo_status_t nmo_lua_push_script_model(lua_State *state, nmo_workspace_t *workspace);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LUA_SCRIPT_H */
