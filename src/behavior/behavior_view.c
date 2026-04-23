#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_script_edit_graph.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "../runtime/runtime_internal.h"

#include <string.h>

#define NMO_BEHAVIOR_FLAG_BUILDINGBLOCK 0x00008000u

static void nmo_behavior_view_clear(nmo_behavior_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->edit_graph_status = NMO_ERR_NOT_FOUND;
    view->interface_status = NMO_ERR_NOT_FOUND;
}

static void nmo_behavior_boundary_view_clear(nmo_behavior_boundary_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
}

static nmo_status_t nmo_behavior_view_lookup(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object,
    nmo_behavior_state_t **out_state)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (workspace == NULL || out_object == NULL || out_state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    *out_state = NULL;

    repo = nmo_workspace_internal_repository(workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    object = nmo_object_repository_find_by_id(repo, behavior_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_state = (nmo_behavior_state_t *)nmo_object_get_state(object);
    if (*out_state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    *out_object = object;
    return NMO_OK;
}

nmo_status_t nmo_behavior_view_from_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    nmo_behavior_view_t *out_view)
{
    nmo_object_t *object = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_script_edit_graph_t *edit_graph = NULL;
    nmo_status_t status = NMO_OK;

    if (workspace == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_view_clear(out_view);
    NMO_RETURN_IF_ERROR(nmo_behavior_view_lookup(
        workspace, behavior_id, &object, &state));

    out_view->behavior_id = behavior_id;
    out_view->class_id = nmo_object_get_class_id(object);
    out_view->name = nmo_object_get_name(object);
    out_view->flags = state->flags;
    out_view->is_building_block =
        (state->flags & NMO_BEHAVIOR_FLAG_BUILDINGBLOCK) != 0u;
    out_view->has_target_parameter = state->target_parameter_id != 0u;
    out_view->target_parameter_id = state->target_parameter_id;
    out_view->sub_behavior_count = state->sub_behaviors.count;
    out_view->link_count = state->sub_behavior_links.count;
    out_view->operation_count = state->operations.count;
    out_view->input_count = state->inputs.count;
    out_view->output_count = state->outputs.count;
    out_view->in_parameter_count = state->in_parameters.count;
    out_view->out_parameter_count = state->out_parameters.count;
    out_view->local_parameter_count = state->local_parameters.count;

    status = nmo_workspace_internal_script_edit_graph_build(
        workspace, behavior_id, UINT32_MAX, &edit_graph);
    out_view->edit_graph_status = status;
    if (status == NMO_OK && edit_graph != NULL) {
        out_view->owner_index_available =
            nmo_script_edit_graph_owner_index_available(edit_graph);
        out_view->edit_ready =
            nmo_script_edit_graph_edit_ready(edit_graph);
        nmo_script_edit_graph_destroy(edit_graph);
        edit_graph = NULL;
    }

    out_view->has_interface = state->has_interface;

    if (!state->has_interface) {
        return NMO_OK;
    }

    status = nmo_workspace_internal_interface_view_from_behavior(
        workspace, behavior_id, &out_view->interface_view);
    out_view->interface_status = status;
    out_view->interface_available = status == NMO_OK;
    if (status == NMO_OK || status == NMO_ERR_NOT_FOUND) {
        return NMO_OK;
    }

    return NMO_OK;
}

nmo_status_t nmo_behavior_view_describe_boundary(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    uint32_t max_depth,
    nmo_behavior_boundary_view_t *out_view)
{
    nmo_behavior_boundary_t boundary = {0};
    bool ok = false;
    nmo_status_t status = NMO_OK;

    if (workspace == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_boundary_view_clear(out_view);
    if (nmo_workspace_internal_context(workspace) == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    ok = nmo_behavior_boundary_build(
        workspace, behavior_id, max_depth, &boundary);
    if (!ok) {
        status = nmo_last_error_code();
        return status != NMO_OK ? status : NMO_ERR_INTERNAL;
    }

    out_view->behavior_id = behavior_id;
    out_view->internal_node_count = boundary.internal_node_count;
    out_view->control_in_count = boundary.control_in_count;
    out_view->control_out_count = boundary.control_out_count;
    out_view->parameter_in_count = boundary.parameter_in_count;
    out_view->parameter_out_count = boundary.parameter_out_count;
    out_view->broken_links = boundary.broken_links;
    out_view->missing_nodes = boundary.missing_nodes;

    nmo_behavior_boundary_free(&boundary);
    return NMO_OK;
}
