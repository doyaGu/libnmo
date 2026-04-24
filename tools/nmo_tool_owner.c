#include "nmo_tool_owner.h"

#include "../src/runtime/runtime_internal.h"

nmo_object_repository_t *nmo_tool_owner_repository(nmo_workspace_t *workspace) {
    return nmo_workspace_internal_repository(workspace);
}

nmo_object_repository_t *nmo_tool_document_repository(nmo_document_t *document) {
    return nmo_document_get_repository(document);
}

nmo_arena_t *nmo_tool_owner_arena(nmo_workspace_t *workspace) {
    return nmo_workspace_internal_document_arena(workspace);
}

nmo_ref_graph_t *nmo_tool_owner_ref_graph(nmo_workspace_t *workspace) {
    return nmo_workspace_internal_ref_graph(workspace);
}

nmo_behavior_index_t *nmo_tool_owner_behavior_index(nmo_workspace_t *workspace) {
    return nmo_workspace_internal_behavior_index(workspace);
}

nmo_status_t nmo_tool_owner_ensure_behavior_acceleration(nmo_workspace_t *workspace) {
    return nmo_workspace_internal_ensure_behavior_acceleration(workspace);
}

void nmo_tool_owner_behavior_interface_diagnostics(
    nmo_workspace_t *workspace,
    nmo_tool_behavior_interface_diagnostics_t *out_diag)
{
    nmo_workspace_internal_get_behavior_interface_diagnostics(workspace, out_diag);
}

nmo_status_t nmo_tool_owner_create_object(
    nmo_workspace_t *workspace,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id,
    nmo_runtime_report_t *out_report)
{
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    return nmo_session_create_object(
        session, class_id, name, type_guid, out_created_id, out_report);
}

nmo_status_t nmo_tool_owner_copy_objects(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report)
{
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    return nmo_session_copy_objects(session, object_ids, object_count, flags, out_report);
}

nmo_status_t nmo_tool_owner_destroy_objects(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report)
{
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    return nmo_session_destroy_objects(session, object_ids, object_count, flags, out_report);
}

nmo_status_t nmo_tool_owner_preview_destroy(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_expanded_ids,
    size_t *out_expanded_count)
{
    return nmo_workspace_internal_preview_destroy(
        workspace, object_ids, object_count, flags, arena,
        out_expanded_ids, out_expanded_count);
}

nmo_status_t nmo_tool_owner_execute_runtime_request(
    nmo_workspace_t *workspace,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report)
{
    return nmo_workspace_internal_execute_runtime_request(workspace, request, out_report);
}

bool nmo_tool_owner_build_hierarchy(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_object_hierarchy_t *out_hierarchy)
{
    return nmo_object_hierarchy_build(
        ctx, nmo_workspace_internal_session(workspace), out_hierarchy);
}

nmo_status_t nmo_tool_owner_stats_collect(
    nmo_workspace_t *workspace,
    nmo_file_stats_t *out_stats)
{
    nmo_document_t *document = nmo_workspace_get_document(workspace);
    return nmo_document_stats_collect(document, out_stats);
}

bool nmo_tool_owner_chunk_entries(
    nmo_workspace_t *workspace,
    nmo_chunk_index_entry_t **out_entries,
    size_t *out_count,
    size_t *out_object_count)
{
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    return nmo_chunk_index_collect_entries(
        session, out_entries, out_count, out_object_count);
}

void nmo_tool_owner_summary_output_bind(
    nmo_summary_output_t *output,
    nmo_workspace_t *workspace)
{
    if (output != NULL) {
        output->session = nmo_workspace_internal_session(workspace);
    }
}

bool nmo_tool_owner_interface_references_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id)
{
    nmo_interface_view_t view = {0};
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    if (session == NULL || parent_behavior_id == 0u || behavior_id == 0u) {
        return false;
    }
    return nmo_interface_view_find_behavior(
        session, parent_behavior_id, behavior_id, &view) == NMO_OK;
}

nmo_save_options_t nmo_tool_owner_save_options_default(void) {
    return nmo_save_options_default();
}

void nmo_tool_owner_save_options_enable_compressed(nmo_save_options_t *options) {
    if (options != NULL) {
        options->flags |= NMO_SAVE_COMPRESSED;
    }
}

void nmo_tool_owner_save_options_enable_sequential_ids(nmo_save_options_t *options) {
    if (options != NULL) {
        options->flags |= NMO_SAVE_SEQUENTIAL_IDS;
    }
}
