/**
 * @file script_edit_interface.c
 * @brief Script edit interface-chunk validation and policy application.
 */

#include "script_edit_internal.h"

#include "behavior/nmo_behavior_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "format/nmo_interface_chunk.h"

#include <string.h>

static bool script_edit_interface_owner_matches(const nmo_port_owner_t *owner,
                                                nmo_object_id_t owner_id,
                                                nmo_port_kind_t kind)
{
    return owner && owner->owner_id == owner_id && owner->kind == kind;
}

static bool script_edit_behavior_array_contains(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    const char *field_name,
    nmo_object_id_t object_id)
{
    nmo_behavior_state_t *state = script_edit_find_behavior_state(
        session, behavior_id, NULL);
    if (!state || !field_name || object_id == NMO_OBJECT_ID_NONE) return false;
    const nmo_array_t *array = NULL;
    if (strcmp(field_name, "operations") == 0) {
        array = &state->operations;
    } else if (strcmp(field_name, "sub_behavior_links") == 0) {
        array = &state->sub_behavior_links;
    } else {
        return false;
    }
    return nmo_behavior_ref_array_find(array, object_id, NULL);
}

typedef enum script_edit_interface_object_kind {
    SCRIPT_EDIT_INTERFACE_OBJECT_ANY = 0,
    SCRIPT_EDIT_INTERFACE_OBJECT_BEHAVIOR,
    SCRIPT_EDIT_INTERFACE_OBJECT_LINK,
    SCRIPT_EDIT_INTERFACE_OBJECT_OPERATION,
    SCRIPT_EDIT_INTERFACE_OBJECT_PARAMETER,
} script_edit_interface_object_kind_t;

static bool script_edit_interface_object_matches(
    const nmo_type_registry_t *registry,
    nmo_object_t *object,
    script_edit_interface_object_kind_t kind)
{
    if (!object) {
        return false;
    }
    switch (kind) {
        case SCRIPT_EDIT_INTERFACE_OBJECT_ANY:
            return true;
        case SCRIPT_EDIT_INTERFACE_OBJECT_BEHAVIOR:
            return script_edit_get_object_state(
                registry, object, NMO_CID_BEHAVIOR, CKPGUID_BEHAVIOR) != NULL;
        case SCRIPT_EDIT_INTERFACE_OBJECT_LINK:
            return script_edit_get_object_state(
                registry,
                object,
                NMO_CID_BEHAVIORLINK,
                CKPGUID_BEHAVIORLINK) != NULL;
        case SCRIPT_EDIT_INTERFACE_OBJECT_OPERATION:
            return script_edit_get_object_state(
                registry,
                object,
                NMO_CID_PARAMETEROPERATION,
                CKPGUID_PARAMETEROPERATION) != NULL;
        case SCRIPT_EDIT_INTERFACE_OBJECT_PARAMETER:
            return script_edit_get_object_state(
                registry, object, NMO_CID_PARAMETER, CKPGUID_PARAMETER) != NULL;
    }
    return false;
}

static nmo_object_t *script_edit_find_interface_object(
    nmo_session_t *session,
    nmo_object_id_t interface_id,
    bool interface_ids_are_runtime,
    script_edit_interface_object_kind_t kind)
{
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_object_t *object = NULL;

    if (!session || interface_id == 0u) {
        return NULL;
    }

    repo = nmo_session_get_repository(session);
    registry = nmo_context_get_type_registry(nmo_session_get_context(session));
    if (!repo || !registry) {
        return NULL;
    }

    if (interface_ids_are_runtime) {
        object = nmo_object_repository_find_by_id(repo, interface_id);
        if (script_edit_interface_object_matches(registry, object, kind)) {
            return object;
        }
        object = nmo_object_repository_find_by_file_id(repo, interface_id);
        if (script_edit_interface_object_matches(registry, object, kind)) {
            return object;
        }
    } else {
        object = nmo_object_repository_find_by_file_id(repo, interface_id);
        if (script_edit_interface_object_matches(registry, object, kind)) {
            return object;
        }
        object = nmo_object_repository_find_by_id(repo, interface_id);
        if (script_edit_interface_object_matches(registry, object, kind)) {
            return object;
        }
    }

    return NULL;
}

static nmo_object_id_t script_edit_resolve_interface_object_id(
    nmo_session_t *session,
    nmo_object_id_t interface_id,
    bool interface_ids_are_runtime,
    script_edit_interface_object_kind_t kind,
    nmo_object_t **out_object)
{
    nmo_object_t *object = script_edit_find_interface_object(session,
                                                             interface_id,
                                                             interface_ids_are_runtime,
                                                             kind);
    if (out_object) {
        *out_object = object;
    }
    return object ? object->id : 0u;
}

static nmo_behavior_state_t *script_edit_resolve_interface_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t interface_behavior_id,
    bool interface_ids_are_runtime,
    nmo_object_id_t *out_runtime_behavior_id)
{
    nmo_object_t *object = NULL;
    nmo_object_id_t runtime_behavior_id = script_edit_resolve_interface_object_id(
        session,
        interface_behavior_id,
        interface_ids_are_runtime,
        SCRIPT_EDIT_INTERFACE_OBJECT_BEHAVIOR,
        &object);

    if (out_runtime_behavior_id) {
        *out_runtime_behavior_id = runtime_behavior_id;
    }
    return (nmo_behavior_state_t *)script_edit_get_object_state(
        nmo_context_get_type_registry(nmo_session_get_context(session)),
        object,
        NMO_CID_BEHAVIOR,
        CKPGUID_BEHAVIOR);
}

static bool script_edit_interface_endpoint_exists(
    nmo_session_t *session,
    nmo_object_id_t object_id,
    bool interface_ids_are_runtime)
{
    return object_id == 0u ||
           script_edit_find_interface_object(session,
                                             object_id,
                                             interface_ids_are_runtime,
                                             SCRIPT_EDIT_INTERFACE_OBJECT_ANY) != NULL;
}

static bool script_edit_interface_link_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_link_t *link,
    bool interface_ids_are_runtime)
{
    nmo_object_id_t link_id = 0u;
    const nmo_port_owner_t *owner = NULL;

    if (!link) {
        return false;
    }
    if (link->type == NMO_INTERFACE_LINK_PARAMETER) {
        return script_edit_interface_endpoint_exists(session,
                                                     link->start.id,
                                                     interface_ids_are_runtime) &&
               script_edit_interface_endpoint_exists(session,
                                                     link->end.id,
                                                     interface_ids_are_runtime);
    }
    link_id = script_edit_resolve_interface_object_id(session,
                                                      link->link_id,
                                                      interface_ids_are_runtime,
                                                      SCRIPT_EDIT_INTERFACE_OBJECT_LINK,
                                                      NULL);
    owner = index ? nmo_behavior_index_find(index, link_id) : NULL;
    if (!script_edit_interface_owner_matches(owner, owner_id, NMO_PORT_SUB_LINK)) {
        return false;
    }
    if (!script_edit_behavior_array_contains(
            session, owner_id, "sub_behavior_links", link_id)) {
        return false;
    }
    return script_edit_interface_endpoint_exists(session,
                                                 link->start.id,
                                                 interface_ids_are_runtime) &&
           script_edit_interface_endpoint_exists(session,
                                                 link->end.id,
                                                 interface_ids_are_runtime) &&
           link_id != 0u;
}

static bool script_edit_interface_operation_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_operation_t *op,
    bool interface_ids_are_runtime)
{
    nmo_object_id_t operation_id = 0u;
    const nmo_port_owner_t *owner = NULL;

    if (!op) {
        return false;
    }
    operation_id = script_edit_resolve_interface_object_id(
        session,
        op->id,
        interface_ids_are_runtime,
        SCRIPT_EDIT_INTERFACE_OBJECT_OPERATION,
        NULL);
    owner = index ? nmo_behavior_index_find(index, operation_id) : NULL;
    if (!script_edit_interface_owner_matches(owner, owner_id, NMO_PORT_OPERATION)) {
        return false;
    }
    return script_edit_behavior_array_contains(
        session, owner_id, "operations", operation_id);
}

static bool script_edit_interface_shared_param_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_param_t *param,
    bool interface_ids_are_runtime)
{
    nmo_object_t *object = NULL;
    nmo_object_id_t source_id = 0u;
    const nmo_port_owner_t *owner = NULL;

    if (!param || param->source_id == 0u) {
        return false;
    }
    source_id = script_edit_resolve_interface_object_id(
        session,
        param->source_id,
        interface_ids_are_runtime,
        SCRIPT_EDIT_INTERFACE_OBJECT_PARAMETER,
        &object);
    owner = index ? nmo_behavior_index_find(index, source_id) : NULL;
    if (!owner || owner->owner_id != owner_id) {
        return false;
    }
    return object != NULL;
}

static bool script_edit_interface_graph_io_is_valid(
    const nmo_behavior_state_t *owner_state,
    const nmo_interface_graph_io_t *graph_io)
{
    const int32_t *sets[4];
    size_t counts[4];

    if (!owner_state || !graph_io) {
        return false;
    }

    sets[0] = graph_io->inward_inputs;
    sets[1] = graph_io->outward_inputs;
    sets[2] = graph_io->inward_outputs;
    sets[3] = graph_io->outward_outputs;
    counts[0] = graph_io->inward_input_count;
    counts[1] = graph_io->outward_input_count;
    counts[2] = graph_io->inward_output_count;
    counts[3] = graph_io->outward_output_count;

    for (size_t set_index = 0; set_index < 4u; ++set_index) {
        for (size_t i = 0; i < counts[set_index]; ++i) {
            if (sets[set_index][i] < 0) {
                return false;
            }
        }
    }
    return true;
}

static bool script_edit_interface_graph_io_is_owner_relative(
    const nmo_behavior_state_t *owner_state,
    const nmo_interface_graph_io_t *graph_io)
{
    const int32_t *sets[4];
    size_t counts[4];
    size_t limits[4];

    if (!owner_state || !graph_io) {
        return false;
    }

    sets[0] = graph_io->inward_inputs;
    sets[1] = graph_io->outward_inputs;
    sets[2] = graph_io->inward_outputs;
    sets[3] = graph_io->outward_outputs;
    counts[0] = graph_io->inward_input_count;
    counts[1] = graph_io->outward_input_count;
    counts[2] = graph_io->inward_output_count;
    counts[3] = graph_io->outward_output_count;
    limits[0] = owner_state->inputs.count;
    limits[1] = owner_state->inputs.count;
    limits[2] = owner_state->outputs.count;
    limits[3] = owner_state->outputs.count;

    for (size_t set_index = 0; set_index < 4u; ++set_index) {
        for (size_t i = 0; i < counts[set_index]; ++i) {
            if (sets[set_index][i] < 0 ||
                (size_t)sets[set_index][i] >= limits[set_index]) {
                return false;
            }
        }
    }
    return true;
}

static bool script_edit_interface_body_is_valid(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    const nmo_interface_body_t *body,
    bool interface_ids_are_runtime)
{
    nmo_behavior_state_t *owner_state = NULL;

    if (!body || !body->has_body) {
        return true;
    }
    owner_state = script_edit_find_behavior_state(session, owner_id, NULL);
    if (!owner_state) {
        return false;
    }

    for (size_t i = 0; i < body->link_count; ++i) {
        if (!script_edit_interface_link_is_valid(session,
                                                 index,
                                                 owner_id,
                                                 &body->links[i],
                                                 interface_ids_are_runtime)) {
            return false;
        }
    }
    for (size_t i = 0; i < body->operation_count; ++i) {
        if (!script_edit_interface_operation_is_valid(session,
                                                      index,
                                                      owner_id,
                                                      &body->operations[i],
                                                      interface_ids_are_runtime)) {
            return false;
        }
    }
    if (body->has_params) {
        if (body->params.local_count > owner_state->local_parameters.count) {
            return false;
        }
        for (size_t i = 0; i < body->params.shared_count; ++i) {
            if (!script_edit_interface_shared_param_is_valid(session,
                                                             index,
                                                             owner_id,
                                                             &body->params.shared[i],
                                                             interface_ids_are_runtime)) {
                return false;
            }
        }
    }
    if (body->has_graph_io && body->graph_io &&
        !script_edit_interface_graph_io_is_valid(owner_state, body->graph_io)) {
        return false;
    }
    return true;
}

NMO_API nmo_status_t nmo_script_edit_validate_interface_refs(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_behavior_state_t *script_behavior = NULL;
    const nmo_behavior_index_t *index = NULL;
    nmo_object_id_t script_behavior_id = 0u;
    bool interface_ids_are_runtime = false;

    if (!tx || !tx->session || behavior_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!behavior->interface_data) {
        return NMO_OK;
    }
    interface_ids_are_runtime = behavior->interface_ids_are_runtime;
    script_behavior_id = behavior->interface_data->script.behavior_id != 0u
        ? behavior->interface_data->script.behavior_id
        : behavior_id;
    index = nmo_workspace_internal_behavior_index(tx->workspace);
    script_behavior = script_edit_resolve_interface_behavior_state(
        tx->session,
        script_behavior_id,
        interface_ids_are_runtime,
        &script_behavior_id);
    if (!script_behavior) {
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                           NMO_SEVERITY_WARNING,
                           "Interface root behavior %u could not resolve in %s ID space",
                           behavior->interface_data->script.behavior_id,
                           interface_ids_are_runtime ? "runtime" : "raw");
        return NMO_ERR_VALIDATION_FAILED;
    }

    for (size_t i = 0; i < behavior->interface_data->sub_count; ++i) {
        const nmo_interface_behavior_t *sub = &behavior->interface_data->subs[i];
        nmo_object_id_t sub_behavior_id = 0u;
        nmo_behavior_state_t *sub_behavior = script_edit_resolve_interface_behavior_state(
            tx->session,
            sub->behavior_id,
            interface_ids_are_runtime,
            &sub_behavior_id);

        if (!script_edit_behavior_is_graph_member(tx->session,
                                                  script_behavior_id,
                                                  sub_behavior_id) ||
            !sub_behavior) {
            NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                               NMO_SEVERITY_WARNING,
                               "Interface sub[%zu] behavior %u resolved=%u is not in root graph %u",
                               i,
                               sub->behavior_id,
                               sub_behavior_id,
                               script_behavior_id);
            return NMO_ERR_VALIDATION_FAILED;
        }
        if (!script_edit_interface_body_is_valid(tx->session,
                                                 index,
                                                 sub_behavior_id,
                                                 &sub->body,
                                                 interface_ids_are_runtime)) {
            NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                               NMO_SEVERITY_WARNING,
                               "Interface sub[%zu] body for behavior %u resolved=%u is invalid",
                               i,
                               sub->behavior_id,
                               sub_behavior_id);
            return NMO_ERR_VALIDATION_FAILED;
        }
    }

    if (!script_edit_interface_body_is_valid(tx->session, index,
                                             script_behavior_id,
                                             &behavior->interface_data->script.body,
                                             interface_ids_are_runtime)) {
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED,
                           NMO_SEVERITY_WARNING,
                           "Interface script body for behavior %u resolved=%u is invalid",
                           behavior->interface_data->script.behavior_id,
                           script_behavior_id);
        return NMO_ERR_VALIDATION_FAILED;
    }

    return NMO_OK;
}

static bool script_edit_filter_graph_io_indices(int32_t *items,
                                                size_t *count,
                                                size_t limit)
{
    size_t write_index = 0u;
    bool changed = false;

    if (!count) {
        return false;
    }
    for (size_t read_index = 0; read_index < *count; ++read_index) {
        if (!items || items[read_index] < 0 ||
            (size_t)items[read_index] >= limit) {
            changed = true;
            continue;
        }
        if (items && write_index != read_index) {
            items[write_index] = items[read_index];
        }
        ++write_index;
    }
    *count = write_index;
    return changed;
}

static bool script_edit_rewrite_removed_graph_io_index(
    int32_t *items,
    size_t *count,
    size_t removed_index)
{
    size_t write_index = 0u;
    bool changed = false;

    if (!count) {
        return false;
    }

    for (size_t read_index = 0; read_index < *count; ++read_index) {
        int32_t value = items ? items[read_index] : -1;
        if (!items || value < 0) {
            changed = true;
            continue;
        }
        if ((size_t)value == removed_index) {
            changed = true;
            continue;
        }
        if (write_index != read_index) {
            changed = true;
        }
        items[write_index++] = value;
    }

    *count = write_index;
    return changed;
}

static bool script_edit_rewrite_interface_body_removed_io(
    nmo_interface_body_t *body,
    nmo_port_kind_t kind,
    size_t removed_index)
{
    bool changed = false;

    if (!body || !body->has_body || !body->has_graph_io || !body->graph_io) {
        return false;
    }

    if (kind == NMO_PORT_IO_IN) {
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->inward_inputs,
            &body->graph_io->inward_input_count,
            removed_index);
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->outward_inputs,
            &body->graph_io->outward_input_count,
            removed_index);
    } else if (kind == NMO_PORT_IO_OUT) {
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->inward_outputs,
            &body->graph_io->inward_output_count,
            removed_index);
        changed |= script_edit_rewrite_removed_graph_io_index(
            body->graph_io->outward_outputs,
            &body->graph_io->outward_output_count,
            removed_index);
    }

    return changed;
}

static bool script_edit_apply_removed_io_refs_to_behavior(
    nmo_script_edit_tx_t *tx,
    nmo_interface_data_t *idata,
    nmo_object_id_t root_behavior_id)
{
    bool changed = false;

    if (!tx || !idata || root_behavior_id == 0u) {
        return false;
    }

    for (size_t i = 0; i < tx->removed_io_ref_count; ++i) {
        const script_edit_removed_io_ref_t *ref = &tx->removed_io_refs[i];
        nmo_interface_body_t *body = NULL;

        if (idata->script.behavior_id == ref->owner_behavior_id ||
            root_behavior_id == ref->owner_behavior_id) {
            body = &idata->script.body;
        } else {
            for (size_t j = 0; j < idata->sub_count; ++j) {
                if (idata->subs[j].behavior_id == ref->owner_behavior_id) {
                    if (script_edit_rewrite_interface_body_removed_io(
                            &idata->subs[j].body,
                            ref->kind,
                            ref->removed_index)) {
                        changed = true;
                    }
                }
            }
        }

        if (body != NULL &&
            script_edit_rewrite_interface_body_removed_io(body,
                                                          ref->kind,
                                                          ref->removed_index)) {
            changed = true;
        }
    }

    return changed;
}

static bool script_edit_rewrite_interface_endpoint_to_runtime(
    nmo_session_t *session,
    nmo_interface_endpoint_t *endpoint,
    bool interface_ids_are_runtime)
{
    nmo_object_id_t runtime_id = 0u;

    if (!endpoint || endpoint->id == 0u) {
        return false;
    }

    runtime_id = script_edit_resolve_interface_object_id(
        session,
        endpoint->id,
        interface_ids_are_runtime,
        SCRIPT_EDIT_INTERFACE_OBJECT_ANY,
        NULL);
    if (runtime_id == 0u || runtime_id == endpoint->id) {
        return false;
    }

    endpoint->id = runtime_id;
    return true;
}

static bool script_edit_rewrite_interface_body_to_runtime(
    nmo_session_t *session,
    nmo_interface_body_t *body,
    bool interface_ids_are_runtime)
{
    bool changed = false;

    if (!body || !body->has_body) {
        return false;
    }

    for (size_t i = 0; i < body->link_count; ++i) {
        nmo_object_id_t runtime_link_id = script_edit_resolve_interface_object_id(
            session,
            body->links[i].link_id,
            interface_ids_are_runtime,
            SCRIPT_EDIT_INTERFACE_OBJECT_LINK,
            NULL);
        if (runtime_link_id != 0u && runtime_link_id != body->links[i].link_id) {
            body->links[i].link_id = runtime_link_id;
            changed = true;
        }
        changed |= script_edit_rewrite_interface_endpoint_to_runtime(
            session,
            &body->links[i].start,
            interface_ids_are_runtime);
        changed |= script_edit_rewrite_interface_endpoint_to_runtime(
            session,
            &body->links[i].end,
            interface_ids_are_runtime);
    }

    for (size_t i = 0; i < body->operation_count; ++i) {
        nmo_object_id_t runtime_operation_id = script_edit_resolve_interface_object_id(
            session,
            body->operations[i].id,
            interface_ids_are_runtime,
            SCRIPT_EDIT_INTERFACE_OBJECT_OPERATION,
            NULL);
        if (runtime_operation_id != 0u &&
            runtime_operation_id != body->operations[i].id) {
            body->operations[i].id = runtime_operation_id;
            changed = true;
        }
    }

    if (body->has_params) {
        for (size_t i = 0; i < body->params.shared_count; ++i) {
            nmo_object_id_t runtime_source_id = script_edit_resolve_interface_object_id(
                session,
                body->params.shared[i].source_id,
                interface_ids_are_runtime,
                SCRIPT_EDIT_INTERFACE_OBJECT_PARAMETER,
                NULL);
            if (runtime_source_id != 0u &&
                runtime_source_id != body->params.shared[i].source_id) {
                body->params.shared[i].source_id = runtime_source_id;
                changed = true;
            }
        }
    }

    return changed;
}

static bool script_edit_rewrite_interface_data_to_runtime(
    nmo_session_t *session,
    nmo_interface_data_t *idata,
    bool interface_ids_are_runtime)
{
    bool changed = false;

    if (!session || !idata) {
        return false;
    }

    if (idata->script.behavior_id != 0u) {
        nmo_object_id_t runtime_script_id = script_edit_resolve_interface_object_id(
            session,
            idata->script.behavior_id,
            interface_ids_are_runtime,
            SCRIPT_EDIT_INTERFACE_OBJECT_BEHAVIOR,
            NULL);
        if (runtime_script_id != 0u && runtime_script_id != idata->script.behavior_id) {
            idata->script.behavior_id = runtime_script_id;
            changed = true;
        }
    }
    changed |= script_edit_rewrite_interface_body_to_runtime(session,
                                                             &idata->script.body,
                                                             interface_ids_are_runtime);

    for (size_t i = 0; i < idata->sub_count; ++i) {
        nmo_object_id_t runtime_behavior_id = script_edit_resolve_interface_object_id(
            session,
            idata->subs[i].behavior_id,
            interface_ids_are_runtime,
            SCRIPT_EDIT_INTERFACE_OBJECT_BEHAVIOR,
            NULL);
        if (runtime_behavior_id != 0u &&
            runtime_behavior_id != idata->subs[i].behavior_id) {
            idata->subs[i].behavior_id = runtime_behavior_id;
            changed = true;
        }
        changed |= script_edit_rewrite_interface_body_to_runtime(session,
                                                                 &idata->subs[i].body,
                                                                 interface_ids_are_runtime);
    }

    return changed;
}

static bool script_edit_canonicalize_interface_body(
    nmo_session_t *session,
    const nmo_behavior_index_t *index,
    nmo_object_id_t owner_id,
    nmo_interface_body_t *body,
    bool interface_ids_are_runtime)
{
    nmo_behavior_state_t *owner_state = NULL;
    size_t write_index = 0u;
    bool changed = false;

    if (!body || !body->has_body) {
        return false;
    }
    owner_state = script_edit_find_behavior_state(session, owner_id, NULL);
    if (!owner_state) {
        return false;
    }

    for (size_t read_index = 0; read_index < body->link_count; ++read_index) {
        if (!script_edit_interface_link_is_valid(session,
                                                 index,
                                                 owner_id,
                                                 &body->links[read_index],
                                                 interface_ids_are_runtime)) {
            changed = true;
            continue;
        }
        if (write_index != read_index) {
            body->links[write_index] = body->links[read_index];
        }
        ++write_index;
    }
    body->link_count = write_index;

    write_index = 0u;
    for (size_t read_index = 0; read_index < body->operation_count; ++read_index) {
        if (!script_edit_interface_operation_is_valid(session,
                                                      index,
                                                      owner_id,
                                                      &body->operations[read_index],
                                                      interface_ids_are_runtime)) {
            changed = true;
            continue;
        }
        if (write_index != read_index) {
            body->operations[write_index] = body->operations[read_index];
        }
        ++write_index;
    }
    body->operation_count = write_index;

    if (body->has_params) {
        if (body->params.local_count > owner_state->local_parameters.count) {
            body->params.local_count = owner_state->local_parameters.count;
            changed = true;
        }
        write_index = 0u;
        for (size_t read_index = 0; read_index < body->params.shared_count; ++read_index) {
            if (!script_edit_interface_shared_param_is_valid(
                    session,
                    index,
                    owner_id,
                    &body->params.shared[read_index],
                    interface_ids_are_runtime)) {
                changed = true;
                continue;
            }
            if (write_index != read_index) {
                body->params.shared[write_index] = body->params.shared[read_index];
            }
            ++write_index;
        }
        body->params.shared_count = write_index;
    }

    if (body->has_graph_io && body->graph_io &&
        script_edit_interface_graph_io_is_owner_relative(owner_state,
                                                         body->graph_io)) {
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->inward_inputs,
            &body->graph_io->inward_input_count,
            owner_state->inputs.count);
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->outward_inputs,
            &body->graph_io->outward_input_count,
            owner_state->inputs.count);
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->inward_outputs,
            &body->graph_io->inward_output_count,
            owner_state->outputs.count);
        changed |= script_edit_filter_graph_io_indices(
            body->graph_io->outward_outputs,
            &body->graph_io->outward_output_count,
            owner_state->outputs.count);
    }

    return changed;
}

NMO_API nmo_status_t nmo_script_edit_apply_interface_policy(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id,
    nmo_script_edit_interface_mode_t mode)
{
    nmo_behavior_state_t *behavior = NULL;
    nmo_interface_data_t *idata = NULL;
    const nmo_behavior_index_t *index = NULL;
    nmo_object_id_t script_behavior_id = 0u;
    size_t write_index = 0u;
    nmo_status_t rc = NMO_OK;
    bool interface_ids_are_runtime = false;

    if (!tx || !tx->edit || behavior_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (mode == NMO_SCRIPT_EDIT_INTERFACE_PRESERVE) {
        return nmo_script_edit_validate_interface_refs(tx, behavior_id);
    }

    behavior = script_edit_find_behavior_state_in_repo(
        nmo_workspace_internal_type_registry(tx->workspace),
        nmo_workspace_internal_repository(tx->workspace),
        behavior_id,
        NULL);
    if (!behavior) {
        return NMO_ERR_NOT_FOUND;
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(tx->edit, behavior);
    if (rc != NMO_OK) {
        return rc;
    }

    if (mode == NMO_SCRIPT_EDIT_INTERFACE_REMOVE) {
        if (!behavior->has_interface &&
            !behavior->interface_chunk &&
            !behavior->interface_data) {
            return NMO_OK;
        }
        behavior->has_interface = false;
        behavior->interface_chunk = NULL;
        behavior->interface_data = NULL;
        behavior->interface_ids_are_runtime = false;
        (void)nmo_behavior_edit_mark_interface(tx->edit, behavior_id);
        tx->report.interface_changes++;
        nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
        return NMO_OK;
    }

    idata = behavior->interface_data;
    if (!idata) {
        return NMO_OK;
    }
    interface_ids_are_runtime = behavior->interface_ids_are_runtime;
    if (!interface_ids_are_runtime) {
        if (script_edit_rewrite_interface_data_to_runtime(tx->session,
                                                          idata,
                                                          false)) {
            tx->report.interface_changes++;
        }
        behavior->interface_ids_are_runtime = true;
        interface_ids_are_runtime = true;
    }
    script_behavior_id = idata->script.behavior_id != 0u
        ? idata->script.behavior_id
        : behavior_id;
    index = nmo_workspace_internal_behavior_index(tx->workspace);

    for (size_t read_index = 0; read_index < idata->sub_count; ++read_index) {
        nmo_interface_behavior_t *sub = &idata->subs[read_index];
        if (!script_edit_behavior_is_graph_member(tx->session,
                                                  script_behavior_id,
                                                  sub->behavior_id)) {
            tx->report.interface_changes++;
            continue;
        }
        if (write_index != read_index) {
            idata->subs[write_index] = *sub;
        }
        if (script_edit_canonicalize_interface_body(tx->session,
                                                    index,
                                                    sub->behavior_id,
                                                    &idata->subs[write_index].body,
                                                    interface_ids_are_runtime)) {
            tx->report.interface_changes++;
        }
        ++write_index;
    }
    idata->sub_count = write_index;
    if (script_edit_canonicalize_interface_body(tx->session,
                                                index,
                                                script_behavior_id,
                                                &idata->script.body,
                                                interface_ids_are_runtime)) {
        tx->report.interface_changes++;
    }
    if (script_edit_apply_removed_io_refs_to_behavior(tx, idata, behavior_id)) {
        tx->report.interface_changes++;
    }

    rc = nmo_script_edit_validate_interface_refs(tx, behavior_id);
    if (rc != NMO_OK) {
        return rc;
    }

    if (tx->report.interface_changes > 0u) {
        (void)nmo_behavior_edit_mark_interface(tx->edit, behavior_id);
        nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    }
    return NMO_OK;
}
