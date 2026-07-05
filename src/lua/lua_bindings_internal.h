#ifndef NMO_LUA_BINDINGS_INTERNAL_H
#define NMO_LUA_BINDINGS_INTERNAL_H

#include "format/nmo_object.h"
#include "format/nmo_interface_view.h"
#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_script_edit.h"
#include "core/nmo_arena.h"
#include "document/nmo_document.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_handles.h"
#include "lua/nmo_lua_module.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "../runtime/runtime_internal.h"

typedef struct nmo_lua_object_handle_data {
    nmo_document_t *document;
    nmo_object_id_t object_id;
} nmo_lua_object_handle_data_t;

typedef struct nmo_lua_script_edit_tx_handle_data {
    nmo_workspace_t *workspace;
    nmo_script_edit_tx_t *tx;
    nmo_edit_plan_t *plan;
    nmo_edit_report_t report;
    bool has_report;
    bool finished;
    bool executed;
    bool dry_run_report;
    uint32_t validation_flags;
} nmo_lua_script_edit_tx_handle_data_t;

extern const nmo_lua_handle_descriptor_t NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_DOCUMENT_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_WORKSPACE_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_SESSION_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_OBJECT_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_RUNTIME_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_SCRIPT_EDIT_TX_HANDLE_DESCRIPTOR;
extern const nmo_lua_handle_descriptor_t NMO_LUA_EDIT_PLAN_HANDLE_DESCRIPTOR;

typedef struct nmo_lua_function_entry {
    const char *name;
    lua_CFunction fn;
} nmo_lua_function_entry_t;

typedef struct nmo_lua_integer_entry {
    const char *name;
    lua_Integer value;
} nmo_lua_integer_entry_t;

void nmo_lua_set_functions(lua_State *state,
                           const nmo_lua_function_entry_t *entries,
                           size_t count);
void nmo_lua_set_integers(lua_State *state,
                          const nmo_lua_integer_entry_t *entries,
                          size_t count);
void nmo_lua_push_integer_table(lua_State *state,
                                const nmo_lua_integer_entry_t *entries,
                                size_t count);
void nmo_lua_set_integer_field(lua_State *state,
                               const char *field_name,
                               lua_Integer value);
void nmo_lua_set_number_field(lua_State *state,
                              const char *field_name,
                              lua_Number value);
void nmo_lua_set_string_field(lua_State *state,
                              const char *field_name,
                              const char *value);
void nmo_lua_set_boolean_field(lua_State *state,
                               const char *field_name,
                               bool value);
void nmo_lua_set_optional_string_field(lua_State *state,
                                       const char *field_name,
                                       const char *value);

int nmo_lua_raise_last_error(lua_State *state,
                             nmo_status_t status,
                             const char *fallback_message);

nmo_status_t nmo_lua_push_context_handle(lua_State *state, nmo_context_t *context);
nmo_status_t nmo_lua_push_document_handle(lua_State *state, nmo_document_t *document);
nmo_status_t nmo_lua_push_workspace_handle(lua_State *state,
                                           nmo_workspace_t *workspace,
                                           nmo_lua_handle_scope_t *document_scope);
nmo_status_t nmo_lua_push_session_handle(lua_State *state, nmo_session_t *session);
nmo_status_t nmo_lua_push_object_handle(lua_State *state,
                                        nmo_document_t *document,
                                        nmo_lua_handle_scope_t *document_scope,
                                        nmo_object_id_t object_id);
nmo_status_t nmo_lua_push_script_edit_tx_handle(lua_State *state,
                                                nmo_workspace_t *workspace,
                                                nmo_edit_plan_t *plan,
                                                nmo_lua_handle_scope_t *workspace_scope);

nmo_status_t nmo_lua_check_context_handle(lua_State *state,
                                          int index,
                                          nmo_context_t **out_context,
                                          nmo_lua_handle_scope_t **out_scope);
nmo_status_t nmo_lua_check_document_handle(lua_State *state,
                                           int index,
                                           nmo_document_t **out_document,
                                           nmo_lua_handle_scope_t **out_scope);
nmo_status_t nmo_lua_check_workspace_handle(lua_State *state,
                                            int index,
                                            nmo_workspace_t **out_workspace,
                                            nmo_lua_handle_scope_t **out_scope,
                                            nmo_lua_handle_scope_t **out_document_scope);
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

nmo_status_t nmo_lua_parse_object_query(lua_State *state,
                                        int index,
                                        nmo_object_query_t *out_query);
void nmo_lua_push_object_query_name_modes(lua_State *state);

void nmo_lua_push_interface_body_view(lua_State *state,
                                      const nmo_interface_body_view_t *body);
void nmo_lua_push_interface_view(lua_State *state,
                                 const nmo_interface_view_t *view);

lua_State *nmo_lua_runtime_state(nmo_lua_runtime_t *runtime);

const char *nmo_lua_edit_op_kind_string(nmo_edit_op_kind_t kind);
const char *nmo_lua_edit_op_result_handle_name(nmo_edit_op_kind_t kind);
void nmo_lua_push_edit_report(lua_State *state, const nmo_edit_report_t *report);
void nmo_lua_push_pending_edit_plan_report(lua_State *state,
                                           const nmo_edit_plan_t *plan);

#endif /* NMO_LUA_BINDINGS_INTERNAL_H */

