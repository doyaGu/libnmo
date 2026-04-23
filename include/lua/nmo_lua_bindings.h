#ifndef NMO_LUA_BINDINGS_H
#define NMO_LUA_BINDINGS_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "lua/nmo_lua_runtime.h"

#define NMO_LUA_BINDINGS_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LUA_BINDINGS_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_lua_register_core_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_context_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_document_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_workspace_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_session_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_runtime_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_object_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_type_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_behavior_bindings(nmo_lua_runtime_t *runtime);
NMO_API nmo_status_t nmo_lua_register_format_bindings(nmo_lua_runtime_t *runtime);

NMO_API nmo_status_t nmo_lua_register_platform_bindings(nmo_lua_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LUA_BINDINGS_H */
