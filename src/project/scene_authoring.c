#include "project_internal.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_scene_edit.h"
#include "project/nmo_project_plan.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct project_authored_scene {
    uint32_t plan_handle;
    nmo_object_id_t object_id;
} project_authored_scene_t;

static nmo_object_id_t project_authoring_find_scene_id(
    const project_authored_scene_t *scenes,
    size_t scene_count,
    uint32_t scene_handle)
{
    for (size_t i = 0; i < scene_count; ++i) {
        if (scenes[i].plan_handle == scene_handle) {
            return scenes[i].object_id;
        }
    }
    return 0;
}

static uint32_t project_authoring_scene_membership_flags(uint32_t object_flags)
{
    if ((object_flags & NMO_PROJECT_OBJECT_FLAG_ACTIVE) != 0u) {
        return NMO_SCENE_MEMBERSHIP_ACTIVE | NMO_SCENE_MEMBERSHIP_START_ACTIVE;
    }
    return 0u;
}

static nmo_status_t project_authoring_set_position(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const float position[3])
{
    char matrix_value[256];
    int wrote = snprintf(
        matrix_value,
        sizeof(matrix_value),
        "(1, 0, 0, 0; 0, 1, 0, 0; 0, 0, 1, 0; %.9g, %.9g, %.9g, 1)",
        position[0],
        position[1],
        position[2]);
    if (wrote < 0 || (size_t)wrote >= sizeof(matrix_value)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "position matrix string is too long");
    }

    nmo_session_field_edit_result_t field_result = {0};
    nmo_session_field_edit_t field = {
        .field_name = "world_matrix",
        .value_str = matrix_value,
    };
    NMO_RETURN_IF_ERROR(nmo_object_edit_set_fields(
        edit,
        object_id,
        &field,
        1u,
        &field_result));
    if (field_result.failed > 0u) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "failed to set project object position");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_author_scenes(
    nmo_workspace_edit_t *edit,
    const nmo_project_plan_t *plan,
    nmo_project_runtime_object_t **out_objects,
    size_t *out_object_count)
{
    if (!edit || !plan || !out_objects || !out_object_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "workspace edit, plan, and object map outputs are required");
    }
    *out_objects = NULL;
    *out_object_count = 0u;

    project_authored_scene_t *authored_scenes = NULL;
    nmo_project_runtime_object_t *authored_objects = NULL;
    size_t scene_count = nmo_project_plan_scene_count(plan);
    size_t object_count = nmo_project_plan_object_count(plan);
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
        nmo_object_create_desc_t scene_create = {
            .class_id = NMO_CID_SCENE,
            .name = scene.name,
            .type_guid = NMO_GUID_NULL,
        };
        status = nmo_object_edit_create(
            edit,
            &scene_create,
            &scene_id);
        if (status != NMO_OK) {
            goto cleanup;
        }

        authored_scenes[i].plan_handle = scene.handle;
        authored_scenes[i].object_id = scene_id;
    }

    if (object_count > 0u) {
        authored_objects = (nmo_project_runtime_object_t *)calloc(
            object_count,
            sizeof(*authored_objects));
        if (!authored_objects) {
            status = NMO_ERR_NOMEM;
            goto cleanup;
        }
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        status = nmo_project_plan_get_object(plan, i, &object);
        if (status != NMO_OK) {
            goto cleanup;
        }

        nmo_object_id_t object_id = 0;
        nmo_object_create_desc_t object_create = {
            .class_id = object.class_id,
            .name = object.name,
            .type_guid = object.type_guid,
        };
        status = nmo_object_edit_create(
            edit,
            &object_create,
            &object_id);
        if (status != NMO_OK) {
            goto cleanup;
        }

        authored_objects[i].plan_handle = object.handle;
        authored_objects[i].object_id = object_id;
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        status = nmo_project_plan_get_object(plan, i, &object);
        if (status != NMO_OK) {
            goto cleanup;
        }

        nmo_object_id_t object_id = authored_objects[i].object_id;
        if (object.field_count > 0u) {
            nmo_session_field_edit_result_t field_result = {0};
            status = nmo_object_edit_set_fields(
                edit,
                object_id,
                object.fields,
                object.field_count,
                &field_result);
            if (status != NMO_OK) {
                goto cleanup;
            }
            if (field_result.failed > 0u) {
                status = NMO_ERR_VALIDATION_FAILED;
                goto cleanup;
            }
        }

        if (object.has_position) {
            status = project_authoring_set_position(
                edit,
                object_id,
                object.position);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }

        if (object.scene_handle != 0u) {
            nmo_object_id_t scene_id = project_authoring_find_scene_id(
                authored_scenes,
                scene_count,
                object.scene_handle);
            if (scene_id == 0) {
                status = NMO_ERR_INVALID_ARGUMENT;
                goto cleanup;
            }
            status = nmo_scene_edit_add_object(
                edit,
                scene_id,
                object_id,
                project_authoring_scene_membership_flags(object.flags));
            if (status != NMO_OK) {
                goto cleanup;
            }
        }
    }

cleanup:
    if (status == NMO_OK) {
        *out_objects = authored_objects;
        *out_object_count = object_count;
        authored_objects = NULL;
    }
    free(authored_objects);
    free(authored_scenes);
    return status;
}
