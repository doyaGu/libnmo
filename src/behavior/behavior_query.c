#include "behavior/nmo_behavior_query.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "../runtime/runtime_internal.h"
#include "type/nmo_type_system.h"

#include <string.h>

static void nmo_behavior_script_view_clear(nmo_behavior_script_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
}

static bool nmo_behavior_query_is_script_owner(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (registry == NULL) {
        return false;
    }

    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEOBJECT);
}

static nmo_status_t nmo_behavior_query_lookup(
    nmo_document_t *document,
    nmo_object_id_t target_script_id,
    size_t target_index,
    bool use_index,
    nmo_behavior_script_view_t *out_view,
    size_t *out_count)
{
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    size_t script_index = 0;
    nmo_type_registry_t *registry = NULL;
    nmo_object_repository_t *repo = NULL;

    if (document == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (use_index) {
        if (out_view == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_behavior_script_view_clear(out_view);
    } else if (target_script_id != 0) {
        if (out_view == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_behavior_script_view_clear(out_view);
    } else if (out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_document_internal_get_objects(document, &objects, &object_count) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }

    registry = (nmo_type_registry_t *)nmo_document_internal_type_registry(document);
    repo = nmo_document_internal_repository(document);
    if (registry == NULL || repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *owner = objects[i];
        const nmo_beobject_state_t *be_state = NULL;
        size_t script_count = 0;

        if (owner == NULL) {
            continue;
        }
        if (!nmo_behavior_query_is_script_owner(
                registry, nmo_object_get_class_id(owner))) {
            continue;
        }

        be_state = (const nmo_beobject_state_t *)nmo_object_get_state(owner);
        if (be_state == NULL) {
            continue;
        }

        script_count = be_state->scripts.count;
        for (size_t s = 0; s < script_count; ++s) {
            nmo_object_id_t script_id = nmo_beobject_script_array_get_id(
                &be_state->scripts, s);
            if (script_id == NMO_OBJECT_ID_NONE) continue;
            nmo_object_t *script = NULL;

            if (script_id == 0) {
                continue;
            }

            if (out_count != NULL) {
                (*out_count)++;
                continue;
            }

            if (use_index && script_index++ != target_index) {
                continue;
            }
            if (!use_index && target_script_id != 0 && script_id != target_script_id) {
                continue;
            }

            script = repo ? nmo_object_repository_find_by_id(repo, script_id) : NULL;

            out_view->script_id = script_id;
            out_view->owner_id = nmo_object_get_id(owner);
            out_view->script_name = script ? nmo_object_get_name(script) : NULL;
            out_view->owner_name = nmo_object_get_name(owner);
            out_view->owner_class_id = nmo_object_get_class_id(owner);
            return NMO_OK;
        }
    }

    return NMO_ERR_NOT_FOUND;
}

nmo_status_t nmo_behavior_query_count_scripts(
    nmo_document_t *document,
    size_t *out_count)
{
    nmo_status_t status = NMO_OK;

    if (document == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_count = 0;
    status = nmo_behavior_query_lookup(
        document, 0, 0, false, NULL, out_count);
    if (status == NMO_ERR_NOT_FOUND) {
        return NMO_OK;
    }
    return status;
}

nmo_status_t nmo_behavior_query_collect_scripts(
    nmo_document_t *document,
    nmo_array_t *out_scripts)
{
    size_t count = 0;
    nmo_status_t rc = NMO_OK;

    if (document == NULL || out_scripts == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_behavior_query_count_scripts(document, &count);
    if (rc != NMO_OK) {
        return rc;
    }

    for (size_t i = 0; i < count; ++i) {
        nmo_behavior_script_view_t view = {0};
        rc = nmo_behavior_query_script_at(document, i, &view);
        if (rc != NMO_OK) {
            return rc;
        }
        rc = nmo_array_append(out_scripts, &view);
        if (rc != NMO_OK) {
            return rc;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_behavior_query_script_at(
    nmo_document_t *document,
    size_t index,
    nmo_behavior_script_view_t *out_view)
{
    return nmo_behavior_query_lookup(
        document, 0, index, true, out_view, NULL);
}

nmo_status_t nmo_behavior_query_script_from_script_id(
    nmo_document_t *document,
    nmo_object_id_t script_id,
    nmo_behavior_script_view_t *out_view)
{
    if (script_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_behavior_query_lookup(
        document, script_id, 0, false, out_view, NULL);
}

