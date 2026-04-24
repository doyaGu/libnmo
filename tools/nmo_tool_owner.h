#ifndef NMO_TOOL_OWNER_H
#define NMO_TOOL_OWNER_H

#include "nmo_cli_common.h"

#include "behavior/nmo_behavior_query.h"
#include "chunk/nmo_chunk_index.h"
#include "document/nmo_document.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_stats.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_hierarchy.h"
#include "object/nmo_object_summary.h"
#include "runtime/nmo_workspace.h"
#include "format/nmo_interface_view.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_serializer.h"
#include "session/nmo_session_pipeline.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef nmo_session_field_edit_t nmo_tool_field_edit_t;
typedef nmo_session_field_edit_result_t nmo_tool_field_edit_result_t;
typedef nmo_session_plugin_diagnostics_t nmo_tool_plugin_diagnostics_t;
typedef nmo_session_plugin_dependency_status_t nmo_tool_plugin_dependency_status_t;
typedef nmo_session_behavior_interface_diagnostics_t nmo_tool_behavior_interface_diagnostics_t;

#define NMO_TOOL_PLUGIN_DEP_STATUS_MISSING \
    NMO_SESSION_PLUGIN_DEP_STATUS_MISSING
#define NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD \
    NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD
#define NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE \
    NMO_SESSION_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE

nmo_object_repository_t *nmo_tool_owner_repository(nmo_workspace_t *workspace);
nmo_object_repository_t *nmo_tool_document_repository(nmo_document_t *document);
nmo_arena_t *nmo_tool_owner_arena(nmo_workspace_t *workspace);
nmo_ref_graph_t *nmo_tool_owner_ref_graph(nmo_workspace_t *workspace);
nmo_behavior_index_t *nmo_tool_owner_behavior_index(nmo_workspace_t *workspace);
nmo_status_t nmo_tool_owner_ensure_behavior_acceleration(nmo_workspace_t *workspace);
void nmo_tool_owner_behavior_interface_diagnostics(
    nmo_workspace_t *workspace,
    nmo_tool_behavior_interface_diagnostics_t *out_diag);

nmo_status_t nmo_tool_owner_create_object(
    nmo_workspace_t *workspace,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_tool_owner_copy_objects(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_tool_owner_destroy_objects(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_tool_owner_preview_destroy(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_expanded_ids,
    size_t *out_expanded_count);
nmo_status_t nmo_tool_owner_execute_runtime_request(
    nmo_workspace_t *workspace,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report);

bool nmo_tool_owner_build_hierarchy(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_object_hierarchy_t *out_hierarchy);
nmo_status_t nmo_tool_owner_stats_collect(
    nmo_workspace_t *workspace,
    nmo_file_stats_t *out_stats);
bool nmo_tool_owner_chunk_entries(
    nmo_workspace_t *workspace,
    nmo_chunk_index_entry_t **out_entries,
    size_t *out_count,
    size_t *out_object_count);
void nmo_tool_owner_summary_output_bind(
    nmo_summary_output_t *output,
    nmo_workspace_t *workspace);
bool nmo_tool_owner_interface_references_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id);
nmo_save_options_t nmo_tool_owner_save_options_default(void);
void nmo_tool_owner_save_options_enable_compressed(nmo_save_options_t *options);
void nmo_tool_owner_save_options_enable_sequential_ids(nmo_save_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TOOL_OWNER_H */
