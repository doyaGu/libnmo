#ifndef NMO_LUA_BINDINGS_INTERNAL_H
#define NMO_LUA_BINDINGS_INTERNAL_H

#include "format/nmo_object.h"
#include "behavior/nmo_script_edit.h"
#include "core/nmo_arena.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_handles.h"
#include "lua/nmo_lua_module.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"

typedef struct nmo_lua_object_handle_data {
    nmo_session_t *session;
    nmo_object_id_t object_id;
} nmo_lua_object_handle_data_t;

typedef struct nmo_lua_script_edit_tx_handle_data {
    nmo_script_edit_tx_t *tx;
    bool finished;
} nmo_lua_script_edit_tx_handle_data_t;

extern const nmo_lua_handle_descriptor_t NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_SESSION_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_OBJECT_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_SCRIPT_EDIT_TX_HANDLE_DESCRIPTOR;

int nmo_lua_raise_last_error(lua_State *state,
                             nmo_status_t status,
                             const char *fallback_message);

nmo_status_t nmo_lua_push_context_handle(lua_State *state, nmo_context_t *context);
nmo_status_t nmo_lua_push_session_handle(lua_State *state, nmo_session_t *session);
nmo_status_t nmo_lua_push_object_handle(lua_State *state,
                                        nmo_session_t *session,
                                        nmo_lua_handle_scope_t *session_scope,
                                        nmo_object_id_t object_id);
nmo_status_t nmo_lua_push_script_edit_tx_handle(lua_State *state,
                                                nmo_script_edit_tx_t *tx,
                                                nmo_lua_handle_scope_t *session_scope);

nmo_status_t nmo_lua_check_context_handle(lua_State *state,
                                          int index,
                                          nmo_context_t **out_context,
                                          nmo_lua_handle_scope_t **out_scope);
nmo_status_t nmo_lua_check_session_handle(lua_State *state,
                                          int index,
                                          nmo_session_t **out_session,
                                          nmo_lua_handle_scope_t **out_scope);
nmo_status_t nmo_lua_check_object_handle(lua_State *state,
                                         int index,
                                         nmo_lua_object_handle_data_t **out_handle,
                                         nmo_object_t **out_object);
nmo_status_t nmo_lua_check_script_edit_tx_handle(lua_State *state,
                                                 int index,
                                                 nmo_lua_script_edit_tx_handle_data_t **out_handle);

nmo_status_t nmo_lua_collect_object_id_array(lua_State *state,
                                             int index,
                                             nmo_session_t *session,
                                             nmo_arena_t *arena,
                                             nmo_object_id_t **out_ids,
                                             size_t *out_count);

nmo_status_t nmo_lua_check_optional_flags_arg(lua_State *state,
                                              int index,
                                              uint32_t default_flags,
                                              uint32_t *out_flags);

#endif /* NMO_LUA_BINDINGS_INTERNAL_H */
