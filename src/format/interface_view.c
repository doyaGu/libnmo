#include "format/nmo_interface_view.h"

#include "../runtime/runtime_internal.h"
#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"

#include <string.h>

static void nmo_interface_view_clear(nmo_interface_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
}

static void nmo_interface_body_view_fill(
    const nmo_interface_body_t *body,
    nmo_interface_body_view_t *out_view)
{
    memset(out_view, 0, sizeof(*out_view));
    if (body == NULL) {
        return;
    }

    out_view->has_body = body->has_body;
    if (!body->has_body) {
        return;
    }

    out_view->link_count = body->link_count;
    out_view->operation_count = body->operation_count;
    out_view->comment_count = body->comment_count;
    out_view->local_param_count = body->params.local_count;
    out_view->shared_param_count = body->params.shared_count;
    out_view->has_params = body->has_params;
    out_view->has_graph_io = body->has_graph_io && body->graph_io != NULL;
    out_view->has_links_section = body->has_links_section;
    out_view->has_operations_section = body->has_operations_section;
    out_view->has_comments_section = body->has_comments_section;
    out_view->has_unknown_flag_section = body->has_unknown_flag_section;
    out_view->unknown_flag = body->unknown_flag;
    if (out_view->has_graph_io) {
        out_view->inward_input_count = body->graph_io->inward_input_count;
        out_view->outward_input_count = body->graph_io->outward_input_count;
        out_view->inward_output_count = body->graph_io->inward_output_count;
        out_view->outward_output_count = body->graph_io->outward_output_count;
    }
}

static nmo_status_t nmo_interface_view_get_data(
    nmo_session_t *session,
    nmo_object_id_t owner_behavior_id,
    const nmo_interface_data_t **out_data)
{
    nmo_object_repository_t *repository = NULL;
    nmo_object_t *object = NULL;
    const nmo_behavior_state_t *state = NULL;
    nmo_session_behavior_interface_diagnostics_t diagnostics = {0};
    nmo_status_t status = NMO_OK;

    if (session == NULL || out_data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_data = NULL;

    status = nmo_session_ensure_behavior_acceleration(session);
    if (status != NMO_OK) {
        return status;
    }

    repository = nmo_session_get_repository(session);
    if (repository == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    object = nmo_object_repository_find_by_id(repository, owner_behavior_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    state = (const nmo_behavior_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (!state->has_interface) {
        return NMO_ERR_NOT_FOUND;
    }
    if (state->interface_data == NULL) {
        nmo_session_get_behavior_interface_diagnostics(session, &diagnostics);
        if (diagnostics.attempted && diagnostics.status != NMO_OK) {
            return diagnostics.status;
        }
        return NMO_ERR_NOT_FOUND;
    }

    *out_data = state->interface_data;
    return NMO_OK;
}

static void nmo_interface_view_fill_common(
    const nmo_interface_data_t *data,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view)
{
    out_view->owner_behavior_id = owner_behavior_id;
    out_view->version = data->version;
    out_view->format_flags = data->format_flags;
    out_view->sub_behavior_count = data->sub_count;
    out_view->extra_present = data->extra.present;
    out_view->extra_entry_count = data->extra.entry_count;
}

nmo_status_t nmo_interface_view_from_behavior(
    nmo_session_t *session,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view)
{
    const nmo_interface_data_t *data = NULL;

    if (session == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_interface_view_clear(out_view);

    NMO_RETURN_IF_ERROR(nmo_interface_view_get_data(
        session, owner_behavior_id, &data));

    nmo_interface_view_fill_common(data, owner_behavior_id, out_view);
    out_view->behavior_id = data->script.behavior_id;
    out_view->is_root = true;
    out_view->flags = data->script.flags;
    out_view->has_snapshot = data->script.has_snapshot;
    nmo_interface_body_view_fill(&data->script.body, &out_view->body);
    return NMO_OK;
}

nmo_status_t nmo_interface_view_find_behavior(
    nmo_session_t *session,
    nmo_object_id_t owner_behavior_id,
    nmo_object_id_t behavior_id,
    nmo_interface_view_t *out_view)
{
    const nmo_interface_data_t *data = NULL;
    size_t i = 0;

    if (session == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_interface_view_clear(out_view);

    NMO_RETURN_IF_ERROR(nmo_interface_view_get_data(
        session, owner_behavior_id, &data));

    if (behavior_id == owner_behavior_id || behavior_id == data->script.behavior_id) {
        return nmo_interface_view_from_behavior(session, owner_behavior_id, out_view);
    }

    for (i = 0; i < data->sub_count; ++i) {
        const nmo_interface_behavior_t *sub = &data->subs[i];
        if (sub->behavior_id != behavior_id) {
            continue;
        }

        nmo_interface_view_fill_common(data, owner_behavior_id, out_view);
        out_view->behavior_id = sub->behavior_id;
        out_view->is_root = false;
        out_view->flags = sub->flags;
        out_view->depth = sub->depth;
        nmo_interface_body_view_fill(&sub->body, &out_view->body);
        return NMO_OK;
    }

    return NMO_ERR_NOT_FOUND;
}
