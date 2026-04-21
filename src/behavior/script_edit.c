#include "behavior/nmo_script_edit.h"

#include "behavior/nmo_behavior_index.h"
#include "behavior/nmo_bb_registry.h"
#include "session/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "format/nmo_object.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"

#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct nmo_script_edit_tx {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_session_edit_t *edit;
    nmo_script_edit_report_t report;
    uint32_t session_edit_flags;
    nmo_object_id_t *deferred_destroy_ids;
    size_t deferred_destroy_count;
    size_t deferred_destroy_capacity;
    bool finished;
};

static void script_edit_tx_destroy(nmo_script_edit_tx_t *tx)
{
    if (tx) {
        free(tx->deferred_destroy_ids);
    }
    free(tx);
}

static void script_edit_note_change(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.changed_objects < SIZE_MAX) {
        tx->report.changed_objects++;
    }
}

static void script_edit_note_error(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.errors < SIZE_MAX) {
        tx->report.errors++;
    }
}

static void script_edit_note_create(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.created_objects < SIZE_MAX) {
        tx->report.created_objects++;
    }
}

static void script_edit_note_delete(nmo_script_edit_tx_t *tx)
{
    if (tx && tx->report.deleted_objects < SIZE_MAX) {
        tx->report.deleted_objects++;
    }
}

static nmo_status_t script_edit_append_deferred_destroy(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    if (!tx || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < tx->deferred_destroy_count; ++i) {
        if (tx->deferred_destroy_ids[i] == object_id) {
            return NMO_OK;
        }
    }

    if (tx->deferred_destroy_count == tx->deferred_destroy_capacity) {
        size_t next_capacity =
            tx->deferred_destroy_capacity == 0u
                ? 8u
                : tx->deferred_destroy_capacity * 2u;
        nmo_object_id_t *next_ids =
            (nmo_object_id_t *)realloc(
                tx->deferred_destroy_ids,
                next_capacity * sizeof(*next_ids));
        if (!next_ids) {
            return NMO_ERR_NOMEM;
        }
        tx->deferred_destroy_ids = next_ids;
        tx->deferred_destroy_capacity = next_capacity;
    }

    tx->deferred_destroy_ids[tx->deferred_destroy_count++] = object_id;
    return NMO_OK;
}

static nmo_status_t script_edit_track_created(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || object_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_session_edit_track_created_object(tx->edit, object_id);
    if (rc == NMO_OK) {
        script_edit_note_create(tx);
    }
    return rc;
}

static nmo_behavior_state_t *script_edit_find_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || behavior_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behavior_state_t *)nmo_object_get_state(object);
}

static nmo_behaviorio_state_t *script_edit_find_io_state(
    nmo_session_t *session,
    nmo_object_id_t io_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || io_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, io_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORIO) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behaviorio_state_t *)nmo_object_get_state(object);
}

static nmo_behaviorlink_state_t *script_edit_find_link_state(
    nmo_session_t *session,
    nmo_object_id_t link_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || link_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, link_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_behaviorlink_state_t *)nmo_object_get_state(object);
}

static nmo_parameterin_state_t *script_edit_find_parameterin_state(
    nmo_session_t *session,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || parameter_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_PARAMETERIN) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_parameterin_state_t *)nmo_object_get_state(object);
}

static nmo_parameterout_state_t *script_edit_find_parameterout_state(
    nmo_session_t *session,
    nmo_object_id_t parameter_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || parameter_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_PARAMETEROUT) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_parameterout_state_t *)nmo_object_get_state(object);
}

static nmo_parameteroperation_state_t *script_edit_find_operation_state(
    nmo_session_t *session,
    nmo_object_id_t operation_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object) {
        *out_object = NULL;
    }
    if (!session || operation_id == 0) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, operation_id) : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_PARAMETEROPERATION) {
        return NULL;
    }
    if (out_object) {
        *out_object = object;
    }
    return (nmo_parameteroperation_state_t *)nmo_object_get_state(object);
}

static void script_edit_update_behavior_save_flags(
    nmo_behavior_state_t *state)
{
    if (!state) {
        return;
    }

    state->has_save_flags = true;
    if (state->sub_behaviors.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORSUBBEHAV;
    }
    if (state->sub_behavior_links.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORSUBLINKS;
    }
    if (state->operations.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIOROPERATIONS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIOROPERATIONS;
    }
    if (state->in_parameters.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORINPARAMS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORINPARAMS;
    }
    if (state->out_parameters.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIOROUTPARAMS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIOROUTPARAMS;
    }
    if (state->local_parameters.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORLOCALPARAMS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORLOCALPARAMS;
    }
    if (state->inputs.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIORINPUTS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIORINPUTS;
    }
    if (state->outputs.count > 0u) {
        state->save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;
    } else {
        state->save_flags &= ~CK_STATESAVE_BEHAVIOROUTPUTS;
    }
}

static nmo_status_t script_edit_create_runtime_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_object_id)
{
    nmo_runtime_request_t request;
    nmo_runtime_report_t report;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t object_id = 0;

    if (!tx || !tx->session || !out_object_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(&request, 0, sizeof(request));
    memset(&report, 0, sizeof(report));
    request.kind = NMO_RUNTIME_OP_CREATE;
    request.payload.create.class_id = class_id;
    request.payload.create.name = name;
    request.payload.create.type_guid = type_guid;
    request.payload.create.out_created_id = &object_id;

    rc = nmo_runtime_kernel_execute(tx->session, &request, &report);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_track_created(tx, object_id);
    if (rc != NMO_OK) {
        return rc;
    }

    *out_object_id = object_id;
    return NMO_OK;
}

static nmo_status_t script_edit_create_io_object(
    nmo_script_edit_tx_t *tx,
    const char *name,
    nmo_script_edit_io_kind_t kind,
    nmo_object_id_t *out_io_id)
{
    nmo_behaviorio_state_t *state = NULL;
    nmo_status_t rc = script_edit_create_runtime_object(
        tx, NMO_CID_BEHAVIORIO, name, NMO_GUID_NULL, out_io_id);
    if (rc != NMO_OK) {
        return rc;
    }

    state = script_edit_find_io_state(tx->session, *out_io_id, NULL);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    state->old_flags =
        (kind == NMO_SCRIPT_EDIT_IO_INPUT ? CK_BEHAVIORIO_IN
                                          : CK_BEHAVIORIO_OUT) |
        CK_BEHAVIORIO_ACTIVE;
    state->has_flags = true;
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static nmo_status_t script_edit_create_parameter_object(
    nmo_script_edit_tx_t *tx,
    nmo_class_id_t class_id,
    nmo_object_id_t owner_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *parameter = NULL;
    nmo_parameterin_state_t *input_state = NULL;
    const nmo_type_descriptor_t *type_desc = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_status_t rc = script_edit_create_runtime_object(
        tx, class_id, name, NMO_GUID_NULL, out_parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    repo = nmo_session_get_repository(tx->session);
    object = repo ? nmo_object_repository_find_by_id(repo, *out_parameter_id) : NULL;
    if (!object) {
        return NMO_ERR_INVALID_STATE;
    }

    registry = nmo_context_get_type_registry(tx->ctx);
    if (!registry) {
        return NMO_ERR_INVALID_STATE;
    }
    type_desc = nmo_type_registry_find_by_guid(registry, type_guid);
    if (!type_desc) {
        return NMO_ERR_NOT_FOUND;
    }

    if (class_id == NMO_CID_PARAMETERIN) {
        input_state = (nmo_parameterin_state_t *)nmo_object_get_state(object);
        if (!input_state) {
            return NMO_ERR_INVALID_STATE;
        }
        input_state->type_guid = type_guid;
        input_state->owner_id = owner_id;
        input_state->source_id = 0u;
        input_state->is_shared = 0u;
        input_state->is_disabled = 0u;
    } else {
        parameter = nmo_parameter_get_mutable_state(object);
        if (!parameter) {
            return NMO_ERR_INVALID_STATE;
        }

        parameter->type_guid = type_guid;
        parameter->has_state = true;
        parameter->manager_guid = NMO_GUID_NULL;
        parameter->manager_value = 0;
        parameter->subchunk = NULL;
        parameter->object_id = 0;
        nmo_array_dispose(&parameter->buffer_data);

        if ((type_desc->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0u) {
            parameter->mode = CKPARAM_MODE_OBJECT;
        } else {
            size_t buffer_size = type_desc->size;
            if (buffer_size == 0u) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            parameter->mode = CKPARAM_MODE_BUFFER;
            rc = nmo_array_alloc(&parameter->buffer_data, sizeof(uint8_t),
                                 buffer_size, NULL);
            if (rc != NMO_OK) {
                return rc;
            }
            memset(parameter->buffer_data.data, 0, buffer_size);
        }
    }

    if (class_id == NMO_CID_PARAMETEROUT) {
        nmo_parameterout_state_t *state =
            (nmo_parameterout_state_t *)nmo_object_get_state(object);
        state->owner_id = owner_id;
        state->destination_ids = NULL;
        state->destination_count = 0u;
    } else if (class_id == NMO_CID_PARAMETERLOCAL) {
        nmo_parameterlocal_state_t *state =
            (nmo_parameterlocal_state_t *)nmo_object_get_state(object);
        state->owner_id = owner_id;
        state->is_myself = 0u;
        state->is_setting = 0u;
    }

    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE);
    return NMO_OK;
}

static nmo_status_t script_edit_require_behavior_index(
    nmo_script_edit_tx_t *tx,
    const nmo_behavior_index_t **out_index)
{
    nmo_status_t rc = NMO_OK;
    const nmo_behavior_index_t *index = NULL;

    if (!tx || !tx->session || !out_index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if ((tx->session_edit_flags &
         (NMO_SESSION_EDIT_REFERENCES |
          NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
          NMO_SESSION_EDIT_NAMES |
          NMO_SESSION_EDIT_RESOURCES)) != 0u) {
        rc = nmo_session_apply_edit_flags(
            tx->session,
            tx->session_edit_flags &
                (NMO_SESSION_EDIT_REFERENCES |
                 NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                 NMO_SESSION_EDIT_NAMES |
                 NMO_SESSION_EDIT_RESOURCES));
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_session_ensure_behavior_acceleration(tx->session);
    if (rc != NMO_OK) {
        return rc;
    }

    index = nmo_session_get_behavior_index(tx->session);
    if (!index) {
        return NMO_ERR_INVALID_STATE;
    }
    *out_index = index;
    return NMO_OK;
}

static bool script_edit_is_pending_destroy(
    const nmo_script_edit_tx_t *tx,
    nmo_object_id_t object_id)
{
    if (!tx || object_id == 0u) {
        return false;
    }

    for (size_t i = 0; i < tx->deferred_destroy_count; ++i) {
        if (tx->deferred_destroy_ids[i] == object_id) {
            return true;
        }
    }
    return false;
}

static bool script_edit_io_is_linked(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_id_t io_id)
{
    nmo_behavior_state_t *state = NULL;
    nmo_object_id_t *link_ids = NULL;

    state = script_edit_find_behavior_state(session, behavior_id, NULL);
    if (!state || !state->sub_behavior_links.data) {
        return false;
    }

    link_ids = (nmo_object_id_t *)state->sub_behavior_links.data;
    for (size_t i = 0; i < state->sub_behavior_links.count; ++i) {
        nmo_object_repository_t *repo = nmo_session_get_repository(session);
        nmo_object_t *link_obj =
            repo ? nmo_object_repository_find_by_id(repo, link_ids[i]) : NULL;
        const nmo_behaviorlink_state_t *link_state =
            link_obj ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                     : NULL;
        if (!link_state) {
            continue;
        }
        if (link_state->in_io_id == io_id || link_state->out_io_id == io_id) {
            return true;
        }
    }
    return false;
}

static bool script_edit_behavior_is_direct_graph_member(
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id)
{
    nmo_behavior_state_t *parent = NULL;

    if (!session || parent_behavior_id == 0 || behavior_id == 0) {
        return false;
    }
    if (behavior_id == parent_behavior_id) {
        return true;
    }

    parent = script_edit_find_behavior_state(session, parent_behavior_id, NULL);
    if (!parent) {
        return false;
    }
    return nmo_array_find(&parent->sub_behaviors, &behavior_id, NULL) != 0;
}

static bool script_edit_find_direct_parent_behavior(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_id_t *out_parent_behavior_id)
{
    nmo_object_repository_t *repo = NULL;
    size_t object_count = 0;

    if (out_parent_behavior_id) {
        *out_parent_behavior_id = 0u;
    }
    if (!session || behavior_id == 0u) {
        return false;
    }

    repo = nmo_session_get_repository(session);
    if (!repo) {
        return false;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *state = NULL;
        nmo_object_id_t parent_id = 0u;

        if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
            continue;
        }

        state = (nmo_behavior_state_t *)nmo_object_get_state(object);
        if (!state || state->sub_behaviors.count == 0u) {
            continue;
        }

        parent_id = nmo_object_get_id(object);
        if (nmo_array_find(&state->sub_behaviors, &behavior_id, NULL) != 0) {
            if (out_parent_behavior_id) {
                *out_parent_behavior_id = parent_id;
            }
            return true;
        }
    }

    return false;
}

static bool script_edit_find_parent_graph_io_owner(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!session || parent_behavior_id == 0 || io_id == 0) {
        return false;
    }

    owner = index ? nmo_behavior_index_find(index, io_id) : NULL;
    if (!owner ||
        (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT) ||
        !script_edit_behavior_is_direct_graph_member(session,
                                                     parent_behavior_id,
                                                     owner->owner_id)) {
        return false;
    }

    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static bool script_edit_io_can_source_control(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id)
{
    const nmo_port_owner_t *owner = NULL;

    if (!script_edit_find_parent_graph_io_owner(session, index,
                                                parent_behavior_id, io_id,
                                                &owner)) {
        return false;
    }

    if (owner->owner_id == parent_behavior_id) {
        return owner->kind == NMO_PORT_IO_IN;
    }
    return owner->kind == NMO_PORT_IO_OUT;
}

static bool script_edit_io_can_target_control(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id)
{
    const nmo_port_owner_t *owner = NULL;

    if (!script_edit_find_parent_graph_io_owner(session, index,
                                                parent_behavior_id, io_id,
                                                &owner)) {
        return false;
    }

    if (owner->owner_id == parent_behavior_id) {
        return owner->kind == NMO_PORT_IO_OUT;
    }
    return owner->kind == NMO_PORT_IO_IN;
}

static bool script_edit_is_parameter_owner_kind(nmo_port_kind_t kind)
{
    return kind == NMO_PORT_PARAM_IN ||
           kind == NMO_PORT_PARAM_OUT ||
           kind == NMO_PORT_PARAM_LOCAL;
}

static bool script_edit_find_parameter_owner(
    const nmo_behavior_index_t *index,
    nmo_object_id_t parameter_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!index || parameter_id == 0) {
        return false;
    }

    owner = nmo_behavior_index_find(index, parameter_id);
    if (!owner || !script_edit_is_parameter_owner_kind(owner->kind)) {
        return false;
    }
    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static bool script_edit_parameter_belongs_to_parent_graph(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t parameter_id,
    const nmo_port_owner_t **out_owner)
{
    const nmo_port_owner_t *owner = NULL;

    if (out_owner) {
        *out_owner = NULL;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, &owner)) {
        return false;
    }
    if (!script_edit_behavior_is_direct_graph_member(session,
                                                     parent_behavior_id,
                                                     owner->owner_id)) {
        return false;
    }
    if (out_owner) {
        *out_owner = owner;
    }
    return true;
}

static bool script_edit_resolve_common_parent_graph(
    nmo_session_t *session,
    nmo_object_id_t left_owner_id,
    nmo_object_id_t right_owner_id,
    nmo_object_id_t *out_parent_behavior_id)
{
    nmo_object_id_t left_parent_id = 0u;
    nmo_object_id_t right_parent_id = 0u;

    if (out_parent_behavior_id) {
        *out_parent_behavior_id = 0u;
    }
    if (!session || left_owner_id == 0u || right_owner_id == 0u) {
        return false;
    }

    if (left_owner_id == right_owner_id) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_owner_id;
        }
        return true;
    }

    if (script_edit_behavior_is_direct_graph_member(session,
                                                    right_owner_id,
                                                    left_owner_id)) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = right_owner_id;
        }
        return true;
    }
    if (script_edit_behavior_is_direct_graph_member(session,
                                                    left_owner_id,
                                                    right_owner_id)) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_owner_id;
        }
        return true;
    }

    if (!script_edit_find_direct_parent_behavior(session, left_owner_id,
                                                 &left_parent_id) ||
        !script_edit_find_direct_parent_behavior(session, right_owner_id,
                                                 &right_parent_id)) {
        return false;
    }

    if (left_parent_id != 0u && left_parent_id == right_parent_id) {
        if (out_parent_behavior_id) {
            *out_parent_behavior_id = left_parent_id;
        }
        return true;
    }
    return false;
}

static bool script_edit_is_parameter_reference_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

static bool script_edit_parameter_class_holds_value(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL;
}

static bool script_edit_parameterout_has_destination(
    const nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    if (!state || destination_id == 0u) {
        return false;
    }
    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (state->destination_ids && state->destination_ids[i] == destination_id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t script_edit_parameterout_append_destination(
    nmo_script_edit_tx_t *tx,
    nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    nmo_object_id_t *new_ids = NULL;

    if (!tx || !state || destination_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (script_edit_parameterout_has_destination(state, destination_id)) {
        return NMO_OK;
    }

    new_ids = (nmo_object_id_t *)nmo_arena_alloc(
        nmo_session_get_arena(tx->session),
        (size_t)(state->destination_count + 1u) * sizeof(*new_ids),
        _Alignof(nmo_object_id_t));
    if (!new_ids) {
        return NMO_ERR_NOMEM;
    }

    if (state->destination_count > 0u && state->destination_ids) {
        memcpy(new_ids, state->destination_ids,
               (size_t)state->destination_count * sizeof(*new_ids));
    }
    new_ids[state->destination_count] = destination_id;
    state->destination_ids = new_ids;
    state->destination_count += 1u;
    return NMO_OK;
}

static nmo_status_t script_edit_parameterout_remove_destination(
    nmo_script_edit_tx_t *tx,
    nmo_parameterout_state_t *state,
    nmo_object_id_t destination_id)
{
    nmo_object_id_t *new_ids = NULL;
    uint32_t kept = 0u;

    if (!tx || !state || destination_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (state->destination_count == 0u || !state->destination_ids) {
        return NMO_OK;
    }

    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (state->destination_ids[i] != destination_id) {
            kept++;
        }
    }
    if (kept == state->destination_count) {
        return NMO_OK;
    }
    if (kept > 0u) {
        uint32_t out_index = 0u;
        new_ids = (nmo_object_id_t *)nmo_arena_alloc(
            nmo_session_get_arena(tx->session),
            (size_t)kept * sizeof(*new_ids),
            _Alignof(nmo_object_id_t));
        if (!new_ids) {
            return NMO_ERR_NOMEM;
        }
        for (uint32_t i = 0; i < state->destination_count; ++i) {
            if (state->destination_ids[i] != destination_id) {
                new_ids[out_index++] = state->destination_ids[i];
            }
        }
    }

    state->destination_ids = new_ids;
    state->destination_count = kept;
    return NMO_OK;
}

static nmo_status_t validate_behavior_link_owners(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    size_t object_count = 0;

    if (!repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_behaviorlink_state_t *state = NULL;

        if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
            continue;
        }
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        state = (const nmo_behaviorlink_state_t *)nmo_object_get_state(object);
        if (!state) {
            return NMO_ERR_INVALID_STATE;
        }
        if (!nmo_behavior_index_find(index, nmo_object_get_id(object)) ||
            !nmo_behavior_index_find(index, state->in_io_id) ||
            !nmo_behavior_index_find(index, state->out_io_id)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }

    return NMO_OK;
}

static nmo_status_t validate_parameter_links(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    size_t object_count = 0;

    if (!repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_class_id_t class_id = 0;

        if (!object) {
            continue;
        }

        class_id = nmo_object_get_class_id(object);
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }
        if (class_id == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            nmo_object_t *source = NULL;
            if (!state) {
                return NMO_ERR_INVALID_STATE;
            }
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            if (state->source_id != 0) {
                source = nmo_object_repository_find_by_id(repo, state->source_id);
                if (!source) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
                if (state->is_shared) {
                    if (nmo_object_get_class_id(source) != NMO_CID_PARAMETERIN) {
                        return NMO_ERR_VALIDATION_FAILED;
                    }
                } else if (!script_edit_is_parameter_reference_class(
                               nmo_object_get_class_id(source))) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
            }
        } else if (class_id == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)nmo_object_get_state(object);
            if (!state) {
                return NMO_ERR_INVALID_STATE;
            }
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            for (uint32_t j = 0; j < state->destination_count; ++j) {
                nmo_object_id_t destination_id = state->destination_ids
                    ? state->destination_ids[j]
                    : 0;
                nmo_object_t *destination = NULL;
                if (destination_id == 0) {
                    continue;
                }
                destination = nmo_object_repository_find_by_id(repo, destination_id);
                if (!destination ||
                    !script_edit_is_parameter_reference_class(
                        nmo_object_get_class_id(destination))) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
            }
        }
    }

    return NMO_OK;
}

static nmo_status_t validate_parameter_operations(
    const nmo_script_edit_tx_t *tx,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *index)
{
    size_t object_count = 0;

    if (!repo || !index) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_parameteroperation_state_t *state = NULL;

        if (!object ||
            nmo_object_get_class_id(object) != NMO_CID_PARAMETEROPERATION) {
            continue;
        }
        if (script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        state = (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
        if (!state) {
            return NMO_ERR_INVALID_STATE;
        }
        if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        if (state->has_in1) {
            nmo_object_t *param = NULL;
            if (state->in1_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, state->in1_id);
            if (!param || !script_edit_is_parameter_reference_class(
                              nmo_object_get_class_id(param))) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (state->has_in2) {
            nmo_object_t *param = NULL;
            if (state->in2_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, state->in2_id);
            if (!param || !script_edit_is_parameter_reference_class(
                              nmo_object_get_class_id(param))) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (state->has_out) {
            nmo_object_t *param = NULL;
            if (state->out_id == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            param = nmo_object_repository_find_by_id(repo, state->out_id);
            if (!param || !script_edit_is_parameter_reference_class(
                              nmo_object_get_class_id(param))) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_begin(nmo_context_t *ctx,
                                           nmo_session_t *session,
                                           const char *label,
                                           nmo_script_edit_tx_t **out_tx)
{
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    if (!ctx || !session || !out_tx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_tx = NULL;
    tx = (nmo_script_edit_tx_t *)calloc(1u, sizeof(*tx));
    if (!tx) {
        return NMO_ERR_NOMEM;
    }

    tx->ctx = ctx;
    tx->session = session;
    rc = nmo_session_edit_begin(session, label, &tx->edit);
    if (rc != NMO_OK) {
        script_edit_tx_destroy(tx);
        return rc;
    }

    *out_tx = tx;
    return NMO_OK;
}

NMO_API nmo_session_edit_t *nmo_script_edit_session_edit(
    nmo_script_edit_tx_t *tx)
{
    if (!tx || tx->finished) {
        return NULL;
    }
    return tx->edit;
}

NMO_API void nmo_script_edit_mark(nmo_script_edit_tx_t *tx,
                                  uint32_t session_edit_flags)
{
    if (!tx || tx->finished) {
        return;
    }

    tx->session_edit_flags |= session_edit_flags;
    if (tx->edit) {
        nmo_session_edit_mark(tx->edit, session_edit_flags);
    }
    if (session_edit_flags != 0u) {
        script_edit_note_change(tx);
    }
}

NMO_API const nmo_script_edit_report_t *nmo_script_edit_report(
    const nmo_script_edit_tx_t *tx)
{
    return tx ? &tx->report : NULL;
}

NMO_API nmo_status_t nmo_script_edit_validate(nmo_script_edit_tx_t *tx,
                                              uint32_t validation_flags)
{
    static const uint32_t conservative_refresh_flags =
        NMO_SESSION_EDIT_OBJECT_STATE |
        NMO_SESSION_EDIT_REFERENCES |
        NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
        NMO_SESSION_EDIT_NAMES |
        NMO_SESSION_EDIT_RESOURCES;
    nmo_object_repository_t *repo = NULL;
    nmo_behavior_index_t *index = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    nmo_session_behavior_interface_diagnostics_t interface_diag;
    size_t broken_ref_count = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || tx->finished || !tx->session || !tx->ctx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(&tx->report, 0, sizeof(tx->report));
    repo = nmo_session_get_repository(tx->session);
    if (!repo) {
        script_edit_note_error(tx);
        return NMO_ERR_INVALID_STATE;
    }

    /* The low-level session edit API tracks its own flags internally, but it
     * does not expose them. Validation therefore uses a conservative cache
     * refresh so mixed direct and helper mutations are checked consistently.
     */
    rc = nmo_session_apply_edit_flags(tx->session,
                                      tx->session_edit_flags |
                                          conservative_refresh_flags);
    if (rc != NMO_OK) {
        script_edit_note_error(tx);
        return rc;
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY) != 0u &&
        nmo_session_is_partial_load(tx->session)) {
        script_edit_note_error(tx);
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_ensure_behavior_acceleration(tx->session);
    if (rc != NMO_OK) {
        script_edit_note_error(tx);
        return rc;
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_REFERENCES) != 0u) {
        ref_graph = nmo_session_get_ref_graph(tx->session);
        if (!ref_graph) {
            script_edit_note_error(tx);
            return NMO_ERR_INVALID_STATE;
        }
        rc = nmo_ref_graph_validate(ref_graph, NULL, &broken_ref_count);
        if (rc != NMO_OK || broken_ref_count != 0u) {
            script_edit_note_error(tx);
            return rc != NMO_OK ? rc : NMO_ERR_VALIDATION_FAILED;
        }
    }

    if ((validation_flags &
         (NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
          NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX)) != 0u) {
        index = nmo_session_get_behavior_index(tx->session);
        if (!index) {
            script_edit_note_error(tx);
            return NMO_ERR_INVALID_STATE;
        }

        rc = validate_behavior_link_owners(tx, repo, index);
        if (rc != NMO_OK) {
            script_edit_note_error(tx);
            return rc;
        }

        rc = validate_parameter_links(tx, repo, index);
        if (rc != NMO_OK) {
            script_edit_note_error(tx);
            return rc;
        }

        rc = validate_parameter_operations(tx, repo, index);
        if (rc != NMO_OK) {
            script_edit_note_error(tx);
            return rc;
        }
    }

    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_INTERFACE) != 0u) {
        memset(&interface_diag, 0, sizeof(interface_diag));
        nmo_session_get_behavior_interface_diagnostics(tx->session,
                                                       &interface_diag);
        if (interface_diag.attempted && interface_diag.status != NMO_OK) {
            script_edit_note_error(tx);
            return interface_diag.status;
        }
    }

    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    nmo_object_id_t *out_node_id)
{
    nmo_behavior_state_t *parent_state = NULL;
    nmo_behavior_state_t *node_state = NULL;
    const nmo_bb_proto_t *proto = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t node_id = 0;

    if (!tx || !tx->edit || parent_behavior_id == 0 || nmo_guid_is_null(bb_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent_state = script_edit_find_behavior_state(tx->session,
                                                   parent_behavior_id,
                                                   NULL);
    if (!parent_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, parent_state,
                                         sizeof(*parent_state));
    if (rc != NMO_OK) {
        return rc;
    }

    proto = nmo_bb_registry_find(nmo_context_get_bb_registry(tx->ctx), bb_guid);
    rc = script_edit_create_runtime_object(
        tx,
        NMO_CID_BEHAVIOR,
        (name && name[0] != '\0') ? name : (proto ? proto->name : "Behavior"),
        NMO_GUID_NULL,
        &node_id);
    if (rc != NMO_OK) {
        return rc;
    }

    node_state = script_edit_find_behavior_state(tx->session, node_id, NULL);
    if (!node_state) {
        return NMO_ERR_INVALID_STATE;
    }

    node_state->flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION;
    node_state->flags &= ~CKBEHAVIOR_SCRIPT;
    if (proto) {
        node_state->flags |= proto->behavior_flags;
        node_state->compatible_class_id = proto->compatible_class_id;
        node_state->block_version = proto->version != 0u ? proto->version : 65536u;
    } else {
        node_state->block_version = 65536u;
    }
    node_state->block_guid = bb_guid;
    node_state->priority = 0;
    node_state->owner_id = parent_behavior_id;

    if (proto) {
        for (uint32_t i = 0; i < proto->input_count; ++i) {
            nmo_object_id_t io_id = 0;
            rc = script_edit_create_io_object(tx, proto->inputs[i],
                                              NMO_SCRIPT_EDIT_IO_INPUT,
                                              &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->inputs, &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->output_count; ++i) {
            nmo_object_id_t io_id = 0;
            rc = script_edit_create_io_object(tx, proto->outputs[i],
                                              NMO_SCRIPT_EDIT_IO_OUTPUT,
                                              &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->outputs, &io_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->input_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERIN, node_id,
                proto->input_params[i].name,
                proto->input_params[i].type_guid,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->in_parameters, &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->output_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETEROUT, node_id,
                proto->output_params[i].name,
                proto->output_params[i].type_guid,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->out_parameters, &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
        for (uint32_t i = 0; i < proto->local_param_count; ++i) {
            nmo_object_id_t parameter_id = 0;
            rc = script_edit_create_parameter_object(
                tx, NMO_CID_PARAMETERLOCAL, node_id,
                proto->local_params[i].name,
                proto->local_params[i].type_guid,
                &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
            rc = nmo_array_append(&node_state->local_parameters, &parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    script_edit_update_behavior_save_flags(node_state);
    rc = nmo_array_append(&parent_state->sub_behaviors, &node_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent_state);

    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
            NMO_SESSION_EDIT_NAMES);

    if (out_node_id) {
        *out_node_id = node_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_node(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id,
    uint32_t delete_flags)
{
    nmo_behavior_state_t *parent_state = NULL;
    nmo_behavior_state_t *node_state = NULL;
    nmo_object_id_t *expanded_ids = NULL;
    size_t expanded_count = 0;
    size_t node_index = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parent_behavior_id == 0 || node_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent_state = script_edit_find_behavior_state(tx->session,
                                                   parent_behavior_id,
                                                   NULL);
    node_state = script_edit_find_behavior_state(tx->session, node_id, NULL);
    if (!parent_state || !node_state) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_array_find(&parent_state->sub_behaviors, &node_id, &node_index) == 0) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_preview_destroy(tx->session, &node_id, 1, delete_flags,
                                     nmo_session_get_arena(tx->session),
                                     &expanded_ids, &expanded_count);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, parent_state,
                                         sizeof(*parent_state));
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_array_remove(&parent_state->sub_behaviors, node_index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent_state);

    for (size_t i = 0; i < expanded_count; ++i) {
        rc = script_edit_append_deferred_destroy(tx, expanded_ids[i]);
        if (rc != NMO_OK) {
            return rc;
        }
        script_edit_note_delete(tx);
    }

    /* Fresh BB nodes also own their direct ports and parameters. */
    nmo_array_t *owned_arrays[] = {
        &node_state->inputs,
        &node_state->outputs,
        &node_state->in_parameters,
        &node_state->out_parameters,
        &node_state->local_parameters,
    };
    for (size_t i = 0; i < sizeof(owned_arrays) / sizeof(owned_arrays[0]); ++i) {
        nmo_object_id_t *ids =
            owned_arrays[i]->data ? (nmo_object_id_t *)owned_arrays[i]->data : NULL;
        for (size_t j = 0; ids && j < owned_arrays[i]->count; ++j) {
            rc = script_edit_append_deferred_destroy(tx, ids[j]);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_script_edit_io_kind_t kind,
    const char *name,
    nmo_object_id_t *out_io_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_object_id_t io_id = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || behavior_id == 0 || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state(tx->session, behavior_id, NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_io_object(tx, name, kind, &io_id);
    if (rc != NMO_OK) {
        return rc;
    }

    array = kind == NMO_SCRIPT_EDIT_IO_INPUT ? &behavior->inputs
                                             : &behavior->outputs;
    rc = nmo_array_append(array, &io_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    nmo_session_edit_mark_behavior_interface(tx->edit, behavior_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
            NMO_SESSION_EDIT_NAMES);

    if (out_io_id) {
        *out_io_id = io_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rename_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    const char *name)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || io_id == 0 || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, io_id);
    if (!owner || (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_rename_object(tx->edit, io_id, name);
    if (rc != NMO_OK) {
        return rc;
    }
    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_io(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t io_id,
    bool detach_links)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_status_t rc = NMO_OK;

    (void)detach_links;

    if (!tx || !tx->edit || io_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, io_id);
    if (!owner || (owner->kind != NMO_PORT_IO_IN && owner->kind != NMO_PORT_IO_OUT)) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!detach_links && script_edit_io_is_linked(tx->session, owner->owner_id, io_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    behavior = script_edit_find_behavior_state(tx->session, owner->owner_id, NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    array = owner->kind == NMO_PORT_IO_IN ? &behavior->inputs : &behavior->outputs;
    rc = nmo_array_remove(array, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_deferred_destroy(tx, io_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_note_delete(tx);
    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}

static nmo_guid_t script_edit_parameter_type_guid_from_object(
    const nmo_object_t *object)
{
    if (!object) {
        return NMO_GUID_NULL;
    }
    if (nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *state =
            (const nmo_parameterin_state_t *)nmo_object_get_state(object);
        return state ? state->type_guid : NMO_GUID_NULL;
    }

    {
        const nmo_parameter_state_t *state = nmo_parameter_get_state(object);
        return state ? state->type_guid : NMO_GUID_NULL;
    }
}

static const nmo_type_descriptor_t *script_edit_resolve_parameter_type_desc(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;

    if (!tx || !tx->ctx || !tx->session || parameter_id == 0u) {
        return NULL;
    }

    repo = nmo_session_get_repository(tx->session);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object) {
        return NULL;
    }

    type_guid = script_edit_parameter_type_guid_from_object(object);
    if (nmo_guid_is_null(type_guid)) {
        return NULL;
    }

    registry = nmo_context_get_type_registry(tx->ctx);
    return registry ? nmo_type_registry_find_by_guid(registry, type_guid) : NULL;
}

static bool script_edit_type_matches_operation_guid(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *actual_type,
    nmo_guid_t expected_guid)
{
    nmo_type_id_t actual_id = NMO_TYPE_ID_INVALID;
    nmo_type_id_t expected_id = NMO_TYPE_ID_INVALID;

    if (!actual_type || nmo_guid_is_null(expected_guid)) {
        return false;
    }
    if (nmo_guid_equals(actual_type->guid, expected_guid)) {
        return true;
    }
    if (!registry) {
        return false;
    }

    actual_id = nmo_type_registry_guid_to_type_id(registry, actual_type->guid);
    expected_id = nmo_type_registry_guid_to_type_id(registry, expected_guid);
    if (actual_id == NMO_TYPE_ID_INVALID || expected_id == NMO_TYPE_ID_INVALID) {
        return false;
    }

    return nmo_type_get_derivation_depth((nmo_type_registry_t *)registry,
                                         actual_id, expected_id) >= 0;
}

static bool script_edit_operation_family_matches(
    const nmo_operation_family_t *family,
    const nmo_type_registry_t *type_registry,
    const nmo_type_descriptor_t *in1_type,
    const nmo_type_descriptor_t *in2_type,
    const nmo_type_descriptor_t *out_type)
{
    if (!family) {
        return false;
    }

    for (size_t i = 0; i < family->p1_layers.count; ++i) {
        const nmo_operation_p1_layer_t *p1_layer =
            (const nmo_operation_p1_layer_t *)nmo_arena_array_get(
                (nmo_arena_array_t *)&family->p1_layers, i);
        if (!p1_layer) {
            continue;
        }
        if (in1_type &&
            !script_edit_type_matches_operation_guid(type_registry, in1_type,
                                                     p1_layer->p1_type_guid)) {
            continue;
        }

        for (size_t j = 0; j < p1_layer->p2_layers.count; ++j) {
            const nmo_operation_p2_layer_t *p2_layer =
                (const nmo_operation_p2_layer_t *)nmo_arena_array_get(
                    (nmo_arena_array_t *)&p1_layer->p2_layers, j);
            if (!p2_layer) {
                continue;
            }
            if (in2_type &&
                !script_edit_type_matches_operation_guid(type_registry, in2_type,
                                                         p2_layer->p2_type_guid)) {
                continue;
            }

            for (size_t k = 0; k < p2_layer->cells.count; ++k) {
                const nmo_operation_tree_cell_t *cell =
                    (const nmo_operation_tree_cell_t *)nmo_arena_array_get(
                        (nmo_arena_array_t *)&p2_layer->cells, k);
                if (!cell) {
                    continue;
                }
                if (out_type &&
                    !script_edit_type_matches_operation_guid(type_registry, out_type,
                                                             cell->desc.result_type_guid)) {
                    continue;
                }
                return true;
            }
        }
    }

    return false;
}

static nmo_status_t script_edit_validate_operation_signature(
    nmo_script_edit_tx_t *tx,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    nmo_operation_registry_t *operation_registry = NULL;
    nmo_type_registry_t *type_registry = NULL;
    const nmo_operation_family_t *family = NULL;
    const nmo_type_descriptor_t *in1_type = NULL;
    const nmo_type_descriptor_t *in2_type = NULL;
    const nmo_type_descriptor_t *out_type = NULL;

    if (!tx || !tx->ctx || nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (in1_parameter_id == 0u &&
        in2_parameter_id == 0u &&
        out_parameter_id == 0u) {
        return NMO_OK;
    }

    operation_registry = nmo_context_get_operation_registry(tx->ctx);
    type_registry = nmo_context_get_type_registry(tx->ctx);
    if (!operation_registry || !type_registry) {
        return NMO_ERR_INVALID_STATE;
    }

    family = nmo_operation_registry_get_family(operation_registry, &operation_guid);
    if (!family) {
        return NMO_ERR_NOT_FOUND;
    }

    if (in1_parameter_id != 0u) {
        in1_type = script_edit_resolve_parameter_type_desc(tx, in1_parameter_id);
        if (!in1_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }
    if (in2_parameter_id != 0u) {
        in2_type = script_edit_resolve_parameter_type_desc(tx, in2_parameter_id);
        if (!in2_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }
    if (out_parameter_id != 0u) {
        out_type = script_edit_resolve_parameter_type_desc(tx, out_parameter_id);
        if (!out_type) {
            return NMO_ERR_INVALID_STATE;
        }
    }

    return script_edit_operation_family_matches(family, type_registry,
                                                in1_type, in2_type, out_type)
        ? NMO_OK
        : NMO_ERR_VALIDATION_FAILED;
}

static nmo_status_t script_edit_disconnect_parameter_internal(
    nmo_script_edit_tx_t *tx,
    nmo_parameterin_state_t *target_state,
    nmo_object_id_t target_parameter_id)
{
    nmo_parameterout_state_t *source_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || !target_state || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (target_state->source_id == 0u) {
        return NMO_OK;
    }

    if (target_state->is_shared == 0u) {
        source_state = script_edit_find_parameterout_state(tx->session,
                                                           target_state->source_id,
                                                           NULL);
        if (source_state) {
            rc = nmo_session_edit_snapshot_bytes(tx->edit, source_state,
                                                 sizeof(*source_state));
            if (rc != NMO_OK) {
                return rc;
            }
            rc = script_edit_parameterout_remove_destination(
                tx, source_state, target_parameter_id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, target_state,
                                         sizeof(*target_state));
    if (rc != NMO_OK) {
        return rc;
    }
    target_state->source_id = 0u;
    target_state->is_shared = 0u;
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}

static bool script_edit_parameter_has_live_connections(
    const nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    size_t object_count = 0;

    if (!tx || !tx->session || parameter_id == 0u) {
        return false;
    }

    repo = nmo_session_get_repository(tx->session);
    if (!repo) {
        return false;
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_class_id_t class_id = 0;
        if (!object ||
            script_edit_is_pending_destroy(tx, nmo_object_get_id(object))) {
            continue;
        }

        class_id = nmo_object_get_class_id(object);
        if (class_id == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            if (!state) {
                continue;
            }
            if (nmo_object_get_id(object) == parameter_id && state->source_id != 0u) {
                return true;
            }
            if (state->source_id == parameter_id) {
                return true;
            }
        } else if (class_id == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)nmo_object_get_state(object);
            if (!state) {
                continue;
            }
            if (nmo_object_get_id(object) == parameter_id &&
                state->destination_count > 0u) {
                return true;
            }
            for (uint32_t j = 0; j < state->destination_count; ++j) {
                if (state->destination_ids && state->destination_ids[j] == parameter_id) {
                    return true;
                }
            }
        } else if (class_id == NMO_CID_PARAMETEROPERATION) {
            const nmo_parameteroperation_state_t *state =
                (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
            if (!state) {
                continue;
            }
            if ((state->has_in1 && state->in1_id == parameter_id) ||
                (state->has_in2 && state->in2_id == parameter_id) ||
                (state->has_out && state->out_id == parameter_id)) {
                return true;
            }
        }
    }

    return false;
}

static nmo_status_t script_edit_detach_parameter_references(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    nmo_class_id_t class_id = 0;
    size_t object_count = 0;

    if (!tx || !tx->edit || !tx->session || parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    repo = nmo_session_get_repository(tx->session);
    object = repo ? nmo_object_repository_find_by_id(repo, parameter_id) : NULL;
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    class_id = nmo_object_get_class_id(object);

    if (class_id == NMO_CID_PARAMETERIN) {
        nmo_parameterin_state_t *target_state =
            (nmo_parameterin_state_t *)nmo_object_get_state(object);
        nmo_status_t rc = script_edit_disconnect_parameter_internal(
            tx, target_state, parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *other = nmo_object_repository_get_by_index(repo, i);
        nmo_class_id_t other_class = 0;
        nmo_status_t rc = NMO_OK;

        if (!other || nmo_object_get_id(other) == parameter_id ||
            script_edit_is_pending_destroy(tx, nmo_object_get_id(other))) {
            continue;
        }

        other_class = nmo_object_get_class_id(other);
        if (other_class == NMO_CID_PARAMETERIN) {
            nmo_parameterin_state_t *state =
                (nmo_parameterin_state_t *)nmo_object_get_state(other);
            if (state && state->source_id == parameter_id) {
                rc = nmo_session_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                state->source_id = 0u;
                state->is_shared = 0u;
            }
        } else if (other_class == NMO_CID_PARAMETEROUT) {
            nmo_parameterout_state_t *state =
                (nmo_parameterout_state_t *)nmo_object_get_state(other);
            if (state && script_edit_parameterout_has_destination(state, parameter_id)) {
                rc = nmo_session_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                rc = script_edit_parameterout_remove_destination(tx, state,
                                                                 parameter_id);
                if (rc != NMO_OK) {
                    return rc;
                }
            }
        } else if (other_class == NMO_CID_PARAMETEROPERATION) {
            nmo_parameteroperation_state_t *state =
                (nmo_parameteroperation_state_t *)nmo_object_get_state(other);
            if (!state) {
                continue;
            }
            if ((state->has_in1 && state->in1_id == parameter_id) ||
                (state->has_in2 && state->in2_id == parameter_id) ||
                (state->has_out && state->out_id == parameter_id)) {
                rc = nmo_session_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
                if (rc != NMO_OK) {
                    return rc;
                }
                if (state->has_in1 && state->in1_id == parameter_id) {
                    state->has_in1 = 0u;
                    state->in1_id = 0u;
                }
                if (state->has_in2 && state->in2_id == parameter_id) {
                    state->has_in2 = 0u;
                    state->in2_id = 0u;
                }
                if (state->has_out && state->out_id == parameter_id) {
                    state->has_out = 0u;
                    state->out_id = 0u;
                }
            }
        }
    }

    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t owner_behavior_id,
    nmo_script_edit_parameter_kind_t kind,
    nmo_guid_t type_guid,
    const char *name,
    nmo_object_id_t *out_parameter_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_class_id_t class_id = 0;
    nmo_object_id_t parameter_id = 0;
    nmo_status_t rc = NMO_OK;

    if (out_parameter_id) {
        *out_parameter_id = 0u;
    }
    if (!tx || !tx->edit || owner_behavior_id == 0u ||
        nmo_guid_is_null(type_guid) || !name || name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state(tx->session, owner_behavior_id, NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    switch (kind) {
    case NMO_SCRIPT_EDIT_PARAM_IN:
    case NMO_SCRIPT_EDIT_PARAM_SHARED:
        class_id = NMO_CID_PARAMETERIN;
        array = &behavior->in_parameters;
        break;
    case NMO_SCRIPT_EDIT_PARAM_OUT:
        class_id = NMO_CID_PARAMETEROUT;
        array = &behavior->out_parameters;
        break;
    case NMO_SCRIPT_EDIT_PARAM_LOCAL:
        class_id = NMO_CID_PARAMETERLOCAL;
        array = &behavior->local_parameters;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_parameter_object(tx, class_id, owner_behavior_id, name,
                                             type_guid, &parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    if (kind == NMO_SCRIPT_EDIT_PARAM_SHARED) {
        nmo_parameterin_state_t *state =
            script_edit_find_parameterin_state(tx->session, parameter_id, NULL);
        if (!state) {
            return NMO_ERR_INVALID_STATE;
        }
        state->is_shared = 1u;
    }

    rc = nmo_array_append(array, &parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner_behavior_id);
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES |
                               NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                               NMO_SESSION_EDIT_NAMES);

    if (out_parameter_id) {
        *out_parameter_id = parameter_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_parameter_value(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const char *value_str)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_object_t *object = NULL;
    nmo_parameter_state_t *state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u || !value_str) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    object = nmo_object_repository_find_by_id(nmo_session_get_repository(tx->session),
                                              parameter_id);
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_parameter_class_holds_value(nmo_object_get_class_id(object))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    state = nmo_parameter_get_mutable_state(object);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_edit_set_parameter_value(tx->edit, parameter_id, value_str);
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_parameter_bytes(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_object_t *object = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u ||
        (bytes == NULL && byte_count > 0u)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    object = nmo_object_repository_find_by_id(nmo_session_get_repository(tx->session),
                                              parameter_id);
    if (!object) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_parameter_class_holds_value(nmo_object_get_class_id(object))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_session_edit_set_parameter_bytes(tx->edit, parameter_id, bytes,
                                              byte_count);
    if (rc != NMO_OK) {
        return rc;
    }
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_connect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *source_owner = NULL;
    const nmo_port_owner_t *target_owner = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *source_object = NULL;
    nmo_object_t *target_object = NULL;
    nmo_parameterin_state_t *target_state = NULL;
    nmo_parameterout_state_t *source_state = NULL;
    nmo_guid_t source_type = NMO_GUID_NULL;
    nmo_guid_t target_type = NMO_GUID_NULL;
    nmo_object_id_t common_parent_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || source_parameter_id == 0u || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, source_parameter_id, &source_owner) ||
        !script_edit_find_parameter_owner(index, target_parameter_id, &target_owner)) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_resolve_common_parent_graph(tx->session,
                                                 source_owner->owner_id,
                                                 target_owner->owner_id,
                                                 &common_parent_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    repo = nmo_session_get_repository(tx->session);
    source_object = repo ? nmo_object_repository_find_by_id(repo, source_parameter_id) : NULL;
    target_object = repo ? nmo_object_repository_find_by_id(repo, target_parameter_id) : NULL;
    if (!source_object || !target_object) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!script_edit_is_parameter_reference_class(
            nmo_object_get_class_id(source_object)) ||
        nmo_object_get_class_id(target_object) != NMO_CID_PARAMETERIN) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    source_type = script_edit_parameter_type_guid_from_object(source_object);
    target_type = script_edit_parameter_type_guid_from_object(target_object);
    if (!nmo_guid_equals(source_type, target_type)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    target_state = (nmo_parameterin_state_t *)nmo_object_get_state(target_object);
    if (!target_state) {
        return NMO_ERR_INVALID_STATE;
    }
    if (target_state->source_id != 0u && target_state->source_id != source_parameter_id) {
        rc = script_edit_disconnect_parameter_internal(tx, target_state,
                                                       target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, target_state, sizeof(*target_state));
    if (rc != NMO_OK) {
        return rc;
    }
    target_state->source_id = source_parameter_id;
    target_state->is_shared =
        nmo_object_get_class_id(source_object) == NMO_CID_PARAMETERIN ? 1u : 0u;

    source_state = script_edit_find_parameterout_state(tx->session,
                                                       source_parameter_id,
                                                       NULL);
    if (source_state) {
        rc = nmo_session_edit_snapshot_bytes(tx->edit, source_state,
                                             sizeof(*source_state));
        if (rc != NMO_OK) {
            return rc;
        }
        rc = script_edit_parameterout_append_destination(tx, source_state,
                                                         target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    (void)common_parent_id;
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES);
    tx->report.rewired_parameters++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_disconnect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_parameterin_state_t *target_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || target_parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, target_parameter_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    target_state = script_edit_find_parameterin_state(tx->session,
                                                      target_parameter_id,
                                                      NULL);
    if (!target_state) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_disconnect_parameter_internal(tx, target_state,
                                                   target_parameter_id);
    if (rc == NMO_OK) {
        tx->report.rewired_parameters++;
    }
    return rc;
}

NMO_API nmo_status_t nmo_script_edit_remove_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id,
    bool detach)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_array_t *array = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parameter_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_find_parameter_owner(index, parameter_id, &owner)) {
        return NMO_ERR_NOT_FOUND;
    }

    if (!detach && script_edit_parameter_has_live_connections(tx, parameter_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (detach) {
        rc = script_edit_detach_parameter_references(tx, parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    behavior = script_edit_find_behavior_state(tx->session, owner->owner_id, NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    switch (owner->kind) {
    case NMO_PORT_PARAM_IN:
        array = &behavior->in_parameters;
        break;
    case NMO_PORT_PARAM_OUT:
        array = &behavior->out_parameters;
        break;
    case NMO_PORT_PARAM_LOCAL:
        array = &behavior->local_parameters;
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_array_remove(array, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_deferred_destroy(tx, parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES |
                               NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
    script_edit_note_delete(tx);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id,
    nmo_object_id_t *out_operation_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_operation_family_t *family = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_parameteroperation_state_t *state = NULL;
    nmo_object_id_t operation_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (out_operation_id) {
        *out_operation_id = 0u;
    }
    if (!tx || !tx->edit || parent_behavior_id == 0u ||
        nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state(tx->session, parent_behavior_id, NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    family = nmo_operation_registry_get_family(
        nmo_context_get_operation_registry(tx->ctx), &operation_guid);
    if (!family) {
        return NMO_ERR_NOT_FOUND;
    }

    if (in1_parameter_id != 0u || in2_parameter_id != 0u || out_parameter_id != 0u) {
        rc = script_edit_require_behavior_index(tx, &index);
        if (rc != NMO_OK) {
            return rc;
        }
        if ((in1_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            in1_parameter_id,
                                                            NULL)) ||
            (in2_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            in2_parameter_id,
                                                            NULL)) ||
            (out_parameter_id != 0u &&
             !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                            parent_behavior_id,
                                                            out_parameter_id,
                                                            NULL))) {
            return NMO_ERR_VALIDATION_FAILED;
        }

        rc = script_edit_validate_operation_signature(tx, operation_guid,
                                                      in1_parameter_id,
                                                      in2_parameter_id,
                                                      out_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = script_edit_create_runtime_object(tx, NMO_CID_PARAMETEROPERATION,
                                           family->name ? family->name : "Operation",
                                           NMO_GUID_NULL, &operation_id);
    if (rc != NMO_OK) {
        return rc;
    }

    state = script_edit_find_operation_state(tx->session, operation_id, NULL);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    state->operation_guid = operation_guid;
    state->owner_id = parent_behavior_id;
    state->has_owner = 1u;
    state->has_in1 = in1_parameter_id != 0u;
    state->in1_id = in1_parameter_id;
    state->has_in2 = in2_parameter_id != 0u;
    state->in2_id = in2_parameter_id;
    state->has_out = out_parameter_id != 0u;
    state->out_id = out_parameter_id;
    state->in1_chunk = NULL;
    state->in2_chunk = NULL;
    state->out_chunk = NULL;

    rc = nmo_array_append(&behavior->operations, &operation_id);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);
    (void)nmo_session_edit_mark_behavior_interface(tx->edit, parent_behavior_id);
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES |
                               NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                               NMO_SESSION_EDIT_NAMES);

    if (out_operation_id) {
        *out_operation_id = operation_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rewire_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_parameteroperation_state_t *state = NULL;
    nmo_object_id_t final_in1_parameter_id = 0u;
    nmo_object_id_t final_in2_parameter_id = 0u;
    nmo_object_id_t final_out_parameter_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || operation_id == 0u || slot_flags == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    owner = nmo_behavior_index_find(index, operation_id);
    if (!owner || owner->kind != NMO_PORT_OPERATION) {
        return NMO_ERR_NOT_FOUND;
    }

    if (((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u &&
         in1_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        in1_parameter_id,
                                                        NULL)) ||
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u &&
         in2_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        in2_parameter_id,
                                                        NULL)) ||
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u &&
         out_parameter_id != 0u &&
         !script_edit_parameter_belongs_to_parent_graph(tx->session, index,
                                                        owner->owner_id,
                                                        out_parameter_id,
                                                        NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    state = script_edit_find_operation_state(tx->session, operation_id, NULL);
    if (!state) {
        return NMO_ERR_NOT_FOUND;
    }

    final_in1_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u)
            ? in1_parameter_id
            : (state->has_in1 ? state->in1_id : 0u);
    final_in2_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u)
            ? in2_parameter_id
            : (state->has_in2 ? state->in2_id : 0u);
    final_out_parameter_id =
        ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u)
            ? out_parameter_id
            : (state->has_out ? state->out_id : 0u);

    rc = script_edit_validate_operation_signature(tx, state->operation_guid,
                                                  final_in1_parameter_id,
                                                  final_in2_parameter_id,
                                                  final_out_parameter_id);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, state, sizeof(*state));
    if (rc != NMO_OK) {
        return rc;
    }

    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u) {
        state->has_in1 = in1_parameter_id != 0u;
        state->in1_id = in1_parameter_id;
    }
    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u) {
        state->has_in2 = in2_parameter_id != 0u;
        state->in2_id = in2_parameter_id;
    }
    if ((slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u) {
        state->has_out = out_parameter_id != 0u;
        state->out_id = out_parameter_id;
    }

    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES);
    tx->report.rewired_parameters++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_operation(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behavior_state_t *behavior = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || operation_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    owner = nmo_behavior_index_find(index, operation_id);
    if (!owner || owner->kind != NMO_PORT_OPERATION) {
        return NMO_ERR_NOT_FOUND;
    }

    behavior = script_edit_find_behavior_state(tx->session, owner->owner_id, NULL);
    if (!behavior) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, behavior, sizeof(*behavior));
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_array_remove(&behavior->operations, (size_t)owner->index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(behavior);

    rc = script_edit_append_deferred_destroy(tx, operation_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(tx, NMO_SESSION_EDIT_OBJECT_STATE |
                               NMO_SESSION_EDIT_REFERENCES |
                               NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
    script_edit_note_delete(tx);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_add_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay,
    nmo_object_id_t *out_link_id)
{
    const nmo_behavior_index_t *index = NULL;
    nmo_object_id_t link_id = 0;
    nmo_status_t rc = NMO_OK;

    if (out_link_id) {
        *out_link_id = 0;
    }
    if (!tx || !tx->edit || parent_behavior_id == 0 ||
        from_io_id == 0 || to_io_id == 0 ||
        activation_delay > (uint32_t)INT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!script_edit_find_behavior_state(tx->session, parent_behavior_id, NULL)) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!script_edit_io_can_source_control(tx->session, index,
                                           parent_behavior_id, from_io_id) ||
        !script_edit_io_can_target_control(tx->session, index,
                                           parent_behavior_id, to_io_id)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    rc = nmo_session_edit_add_behavior_link(
        tx->edit,
        parent_behavior_id,
        to_io_id,
        from_io_id,
        (int16_t)activation_delay,
        &link_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, parent_behavior_id);
    tx->report.created_objects++;
    if (out_link_id) {
        *out_link_id = link_id;
    }
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_rewire_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_behaviorlink_state_t *link_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || link_id == 0 ||
        (from_io_id == 0 && to_io_id == 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = script_edit_require_behavior_index(tx, &index);
    if (rc != NMO_OK) {
        return rc;
    }

    owner = nmo_behavior_index_find(index, link_id);
    if (!owner || owner->kind != NMO_PORT_SUB_LINK) {
        return NMO_ERR_NOT_FOUND;
    }
    if ((from_io_id != 0 &&
         !script_edit_io_can_source_control(tx->session, index,
                                            owner->owner_id, from_io_id)) ||
        (to_io_id != 0 &&
         !script_edit_io_can_target_control(tx->session, index,
                                            owner->owner_id, to_io_id))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    link_state = script_edit_find_link_state(tx->session, link_id, NULL);
    if (!link_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, link_state,
                                         sizeof(*link_state));
    if (rc != NMO_OK) {
        return rc;
    }

    /* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
     * and link out_io_id is the target IO.
     */
    if (from_io_id != 0) {
        link_state->in_io_id = from_io_id;
    }
    if (to_io_id != 0) {
        link_state->out_io_id = to_io_id;
    }

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, owner->owner_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
    tx->report.moved_links++;
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_set_behavior_link_delay(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    uint32_t activation_delay)
{
    nmo_behaviorlink_state_t *link_state = NULL;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || link_id == 0 ||
        activation_delay > (uint32_t)INT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    link_state = script_edit_find_link_state(tx->session, link_id, NULL);
    if (!link_state) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, link_state,
                                         sizeof(*link_state));
    if (rc != NMO_OK) {
        return rc;
    }

    link_state->activation_delay = (int16_t)activation_delay;
    link_state->initial_activation_delay = (int16_t)activation_delay;
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_remove_behavior_link(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    nmo_behavior_state_t *parent = NULL;
    size_t link_index = 0;
    nmo_status_t rc = NMO_OK;

    if (!tx || !tx->edit || parent_behavior_id == 0 || link_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    parent = script_edit_find_behavior_state(tx->session, parent_behavior_id, NULL);
    if (!parent) {
        return NMO_ERR_NOT_FOUND;
    }
    if (nmo_array_find(&parent->sub_behavior_links, &link_id, &link_index) == 0) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_session_edit_snapshot_bytes(tx->edit, parent, sizeof(*parent));
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_array_remove(&parent->sub_behavior_links, link_index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }
    script_edit_update_behavior_save_flags(parent);

    rc = script_edit_append_deferred_destroy(tx, link_id);
    if (rc != NMO_OK) {
        return rc;
    }

    (void)nmo_session_edit_mark_behavior_interface(tx->edit, parent_behavior_id);
    nmo_script_edit_mark(
        tx,
        NMO_SESSION_EDIT_OBJECT_STATE |
            NMO_SESSION_EDIT_REFERENCES |
            NMO_SESSION_EDIT_BEHAVIOR_GRAPH);
    script_edit_note_delete(tx);
    return NMO_OK;
}

NMO_API nmo_status_t nmo_script_edit_commit(nmo_script_edit_tx_t *tx)
{
    nmo_status_t rc = NMO_OK;

    if (!tx || tx->finished || !tx->edit) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (tx->deferred_destroy_count > 0u) {
        rc = nmo_session_destroy_objects(tx->session,
                                         tx->deferred_destroy_ids,
                                         tx->deferred_destroy_count,
                                         NMO_RUNTIME_REQUEST_STRICT,
                                         NULL);
        if (rc != NMO_OK) {
            nmo_session_edit_rollback(tx->edit);
            tx->edit = NULL;
            tx->finished = true;
            script_edit_tx_destroy(tx);
            return rc;
        }
    }

    rc = nmo_session_edit_commit(tx->edit);
    tx->edit = NULL;
    tx->finished = true;
    script_edit_tx_destroy(tx);
    return rc;
}

NMO_API void nmo_script_edit_rollback(nmo_script_edit_tx_t *tx)
{
    if (!tx) {
        return;
    }
    if (!tx->finished && tx->edit) {
        nmo_session_edit_rollback(tx->edit);
        tx->edit = NULL;
        tx->finished = true;
    }
    script_edit_tx_destroy(tx);
}
