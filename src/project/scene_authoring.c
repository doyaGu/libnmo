#include "project_internal.h"

#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_plan.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>

typedef struct project_authored_scene {
    uint32_t plan_handle;
    nmo_scene_state_t *state;
} project_authored_scene_t;

static nmo_status_t project_authoring_get_created_object(
    nmo_document_t *document,
    nmo_object_id_t object_id,
    nmo_object_t **out_object)
{
    nmo_object_query_t query = {0};
    query.object_id = object_id;
    return nmo_object_query_find_first(document, &query, out_object, NULL);
}

static nmo_scene_state_t *project_authoring_find_scene_state(
    project_authored_scene_t *scenes,
    size_t scene_count,
    uint32_t scene_handle)
{
    for (size_t i = 0; i < scene_count; ++i) {
        if (scenes[i].plan_handle == scene_handle) {
            return scenes[i].state;
        }
    }
    return NULL;
}

static uint32_t project_authoring_scene_object_flags(uint32_t object_flags)
{
    if ((object_flags & NMO_PROJECT_OBJECT_FLAG_ACTIVE) != 0u) {
        return CK_SCENEOBJECT_ACTIVE | CK_SCENEOBJECT_START_ACTIVATE;
    }
    return 0u;
}

nmo_status_t nmo_project_author_scenes(
    nmo_document_t *document,
    const nmo_project_plan_t *plan)
{
    if (!document || !plan) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "document and plan are required");
    }

    size_t scene_count = nmo_project_plan_scene_count(plan);
    project_authored_scene_t *authored_scenes = NULL;
    if (scene_count > 0u) {
        authored_scenes = (project_authored_scene_t *)calloc(
            scene_count,
            sizeof(*authored_scenes));
        if (!authored_scenes) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate authored scene map");
        }
    }

    nmo_status_t status = NMO_OK;
    for (size_t i = 0; i < scene_count; ++i) {
        nmo_project_scene_desc_t scene = {0};
        status = nmo_project_plan_get_scene(plan, i, &scene);
        if (status != NMO_OK) {
            goto cleanup;
        }

        nmo_object_id_t scene_id = 0;
        status = nmo_document_internal_create_object(
            document,
            NMO_CID_SCENE,
            scene.name,
            (nmo_guid_t){0, 0},
            &scene_id);
        if (status != NMO_OK) {
            goto cleanup;
        }

        nmo_object_t *scene_object = NULL;
        status = project_authoring_get_created_object(document, scene_id, &scene_object);
        if (status != NMO_OK) {
            goto cleanup;
        }

        authored_scenes[i].plan_handle = scene.handle;
        authored_scenes[i].state =
            scene_object ? (nmo_scene_state_t *)nmo_object_get_state(scene_object) : NULL;
        if (!authored_scenes[i].state) {
            status = NMO_ERR_INVALID_STATE;
            goto cleanup;
        }
    }

    size_t object_count = nmo_project_plan_object_count(plan);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        status = nmo_project_plan_get_object(plan, i, &object);
        if (status != NMO_OK) {
            goto cleanup;
        }

        nmo_scene_state_t *scene_state = project_authoring_find_scene_state(
            authored_scenes,
            scene_count,
            object.scene_handle);
        if (!scene_state) {
            status = NMO_ERR_INVALID_ARGUMENT;
            goto cleanup;
        }

        nmo_object_id_t object_id = 0;
        status = nmo_document_internal_create_object(
            document,
            object.class_id,
            object.name,
            object.type_guid,
            &object_id);
        if (status != NMO_OK) {
            goto cleanup;
        }

        nmo_scene_object_desc_t scene_desc = {0};
        scene_desc.object_id = object_id;
        scene_desc.flags = project_authoring_scene_object_flags(object.flags);
        status = nmo_array_append(&scene_state->object_descs, &scene_desc);
        if (status != NMO_OK) {
            goto cleanup;
        }
    }

cleanup:
    free(authored_scenes);
    return status;
}
