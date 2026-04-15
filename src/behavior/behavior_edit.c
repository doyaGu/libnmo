/**
 * @file behavior_edit.c
 * @brief Behavior graph mutation: add/remove links
 */

#include "behavior/nmo_behavior_edit.h"

#include "session/nmo_session.h"
#include "session/nmo_context.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"
#include "type/nmo_type_system.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static bool is_behavior(const nmo_type_registry_t *reg, nmo_class_id_t cid)
{
    if (!reg) return false;
    return nmo_type_registry_is_class_derived_from(
        reg, (uint32_t)cid, (uint32_t)NMO_CID_BEHAVIOR);
}

static const nmo_type_registry_t *
session_type_registry(nmo_session_t *session)
{
    nmo_context_t *ctx = nmo_session_get_context(session);
    return ctx ? nmo_context_get_type_registry(ctx) : NULL;
}

/* ------------------------------------------------------------------ */
/* nmo_behavior_add_link                                              */
/* ------------------------------------------------------------------ */

int nmo_behavior_add_link(
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id)
{
    if (!session)
        return NMO_ERR_INVALID_ARGUMENT;

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo)
        return NMO_ERR_INVALID_STATE;

    /* Validate parent is a CKBehavior */
    nmo_object_t *parent_obj =
        nmo_object_repository_find_by_id(repo, parent_behavior_id);
    if (!parent_obj)
        return NMO_ERR_NOT_FOUND;

    const nmo_type_registry_t *reg = session_type_registry(session);
    if (!is_behavior(reg, nmo_object_get_class_id(parent_obj)))
        return NMO_ERR_INVALID_ARGUMENT;

    /* Validate from/to IO objects exist */
    if (!nmo_object_repository_find_by_id(repo, from_io_id))
        return NMO_ERR_NOT_FOUND;
    if (!nmo_object_repository_find_by_id(repo, to_io_id))
        return NMO_ERR_NOT_FOUND;

    /* Create CKBehaviorLink object */
    nmo_object_id_t link_id = 0;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    nmo_guid_t zero_guid;
    memset(&zero_guid, 0, sizeof(zero_guid));

    int rc = nmo_session_create_object(
        session, NMO_CID_BEHAVIORLINK, NULL, zero_guid, &link_id, &report);
    if (rc != NMO_OK)
        return rc;

    /* Configure the link state */
    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (!link_obj)
        return NMO_ERR_INTERNAL;

    nmo_behaviorlink_state_t *link_state =
        (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    if (!link_state)
        return NMO_ERR_INTERNAL;

    link_state->in_io_id                = from_io_id;
    link_state->out_io_id               = to_io_id;
    link_state->activation_delay        = activation_delay;
    link_state->initial_activation_delay = activation_delay;
    link_state->use_new_format          = true;
    link_state->has_format              = true;

    /* Append link ID to parent's sub_behavior_links */
    nmo_behavior_state_t *beh_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    if (!beh_state)
        return NMO_ERR_INTERNAL;

    nmo_status_t append_rc =
        nmo_array_append(&beh_state->sub_behavior_links, &link_id);
    if (append_rc != NMO_OK)
        return (int)append_rc;

    if (out_link_id)
        *out_link_id = link_id;

    return NMO_OK;
}

/* ------------------------------------------------------------------ */
/* nmo_behavior_remove_link                                           */
/* ------------------------------------------------------------------ */

int nmo_behavior_remove_link(
    nmo_session_t *session,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    if (!session)
        return NMO_ERR_INVALID_ARGUMENT;

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo)
        return NMO_ERR_INVALID_STATE;

    /* Validate link exists and is CKBehaviorLink */
    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (!link_obj)
        return NMO_ERR_NOT_FOUND;
    if (nmo_object_get_class_id(link_obj) != NMO_CID_BEHAVIORLINK)
        return NMO_ERR_INVALID_ARGUMENT;

    /* Validate parent exists and is CKBehavior */
    nmo_object_t *parent_obj =
        nmo_object_repository_find_by_id(repo, parent_behavior_id);
    if (!parent_obj)
        return NMO_ERR_NOT_FOUND;

    const nmo_type_registry_t *reg = session_type_registry(session);
    if (!is_behavior(reg, nmo_object_get_class_id(parent_obj)))
        return NMO_ERR_INVALID_ARGUMENT;

    /* Remove link from parent's sub_behavior_links */
    nmo_behavior_state_t *beh_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    if (!beh_state)
        return NMO_ERR_INTERNAL;

    size_t idx;
    if (nmo_array_find(&beh_state->sub_behavior_links, &link_id, &idx) == NMO_OK)
        nmo_array_remove(&beh_state->sub_behavior_links, idx, NULL);

    /* Destroy the link object */
    nmo_session_destroy_objects(session, &link_id, 1, 0, NULL);

    return NMO_OK;
}

/* ------------------------------------------------------------------ */
/* nmo_behavior_find_parameter                                        */
/* ------------------------------------------------------------------ */

nmo_object_t *nmo_behavior_find_parameter(
    nmo_object_repository_t *repo,
    nmo_object_t *behavior,
    const char *name)
{
    if (!repo || !behavior || !name)
        return NULL;

    const nmo_behavior_state_t *bstate =
        (const nmo_behavior_state_t *)nmo_object_get_state(behavior);
    if (!bstate)
        return NULL;

    const nmo_array_t *arrays[] = {
        &bstate->in_parameters,
        &bstate->out_parameters,
        &bstate->local_parameters,
    };

    for (int a = 0; a < 3; a++) {
        const nmo_array_t *arr = arrays[a];
        if (!arr->data || arr->count == 0) continue;
        const nmo_object_id_t *ids = (const nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_t *pobj =
                nmo_object_repository_find_by_id(repo, ids[i]);
            if (!pobj) continue;
            const char *pname = nmo_object_get_name(pobj);
            if (pname && strcmp(pname, name) == 0)
                return pobj;
        }
    }
    return NULL;
}
