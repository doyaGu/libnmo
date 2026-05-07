#include "project_internal.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_entity_edit.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_scene_edit.h"
#include "project/nmo_project_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

static nmo_object_id_t project_authoring_find_object_id(
    const nmo_project_runtime_object_t *objects,
    size_t object_count,
    uint32_t object_handle)
{
    for (size_t i = 0; i < object_count; ++i) {
        if (objects[i].plan_handle == object_handle) {
            return objects[i].object_id;
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

static void project_authoring_compose_matrix(
    const nmo_project_object_desc_t *object,
    float matrix[16])
{
    const float position[3] = {
        object->has_position ? object->position[0] : 0.0f,
        object->has_position ? object->position[1] : 0.0f,
        object->has_position ? object->position[2] : 0.0f,
    };
    const float scale[3] = {
        object->has_scale ? object->scale[0] : 1.0f,
        object->has_scale ? object->scale[1] : 1.0f,
        object->has_scale ? object->scale[2] : 1.0f,
    };
    const float rotation[3] = {
        object->has_rotation_euler_deg ? object->rotation_euler_deg[0] : 0.0f,
        object->has_rotation_euler_deg ? object->rotation_euler_deg[1] : 0.0f,
        object->has_rotation_euler_deg ? object->rotation_euler_deg[2] : 0.0f,
    };
    const float deg_to_rad = 0.017453292519943295769f;
    float rx = rotation[0] * deg_to_rad;
    float ry = rotation[1] * deg_to_rad;
    float rz = rotation[2] * deg_to_rad;
    float cx = cosf(rx);
    float sx = sinf(rx);
    float cy = cosf(ry);
    float sy = sinf(ry);
    float cz = cosf(rz);
    float sz = sinf(rz);

    float r00 = cy * cz;
    float r01 = cy * sz;
    float r02 = -sy;
    float r10 = sx * sy * cz - cx * sz;
    float r11 = sx * sy * sz + cx * cz;
    float r12 = sx * cy;
    float r20 = cx * sy * cz + sx * sz;
    float r21 = cx * sy * sz - sx * cz;
    float r22 = cx * cy;

    matrix[0] = scale[0] * r00;
    matrix[1] = scale[0] * r01;
    matrix[2] = scale[0] * r02;
    matrix[3] = 0.0f;
    matrix[4] = scale[1] * r10;
    matrix[5] = scale[1] * r11;
    matrix[6] = scale[1] * r12;
    matrix[7] = 0.0f;
    matrix[8] = scale[2] * r20;
    matrix[9] = scale[2] * r21;
    matrix[10] = scale[2] * r22;
    matrix[11] = 0.0f;
    matrix[12] = position[0];
    matrix[13] = position[1];
    matrix[14] = position[2];
    matrix[15] = 1.0f;
}

static nmo_status_t project_authoring_set_transform(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_project_object_desc_t *object)
{
    float matrix[16];
    project_authoring_compose_matrix(object, matrix);
    return nmo_entity_edit_set_world_matrix(edit, object_id, matrix);
}

static nmo_status_t project_authoring_set_parent(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_object_id_t parent_id)
{
    return nmo_entity_edit_set_parent(edit, object_id, parent_id);
}

static nmo_status_t project_authoring_set_scene_active_camera(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    nmo_object_id_t camera_id)
{
    return nmo_scene_edit_set_active_camera(edit, scene_id, camera_id);
}

static nmo_status_t project_authoring_format_float(
    float value,
    char *out_value,
    size_t out_size)
{
    int wrote = snprintf(out_value, out_size, "%.9g", value);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "scene float string is too long");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_format_quoted_string(
    const char *value,
    char *out_value,
    size_t out_size)
{
    if (!value || !out_value || out_size < 3u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "string and output buffer are required");
    }
    size_t out = 0u;
    out_value[out++] = '"';
    for (const char *p = value; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\\') {
            if (out + 2u >= out_size) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "quoted string is too long");
            }
            out_value[out++] = '\\';
            out_value[out++] = *p;
        } else {
            if (out + 1u >= out_size) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "quoted string is too long");
            }
            out_value[out++] = *p;
        }
    }
    if (out + 1u >= out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "quoted string is too long");
    }
    out_value[out++] = '"';
    out_value[out] = '\0';
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_format_vector3(
    const float value[3],
    char *out_value,
    size_t out_size)
{
    int wrote = snprintf(
        out_value,
        out_size,
        "(%.9g, %.9g, %.9g)",
        value[0],
        value[1],
        value[2]);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "vector string is too long");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_set_wavesound(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_project_object_desc_t *object,
    const nmo_project_runtime_object_t *objects,
    size_t object_count)
{
    nmo_session_field_edit_t fields[20] = {0};
    char file_value[512];
    char gain_value[32];
    char pan_value[32];
    char pitch_value[32];
    char attached_value[32];
    char position_value[96];
    char direction_value[96];
    size_t field_count = 0u;

    if (object->class_id == NMO_CID_SOUND) {
        if (object->sound_file_path) {
            NMO_RETURN_IF_ERROR(project_authoring_format_quoted_string(
                object->sound_file_path,
                file_value,
                sizeof(file_value)));
            fields[field_count++] = (nmo_session_field_edit_t){
                .field_name = "save_options",
                .value_str = "1",
            };
            fields[field_count++] = (nmo_session_field_edit_t){
                .field_name = "file_name",
                .value_str = file_value,
            };
        }
        goto apply_fields;
    }

    if (object->sound_file_path) {
        NMO_RETURN_IF_ERROR(project_authoring_format_quoted_string(
            object->sound_file_path,
            file_value,
            sizeof(file_value)));
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "has_wave_file_name",
            .value_str = "true",
        };
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "wave_file_name",
            .value_str = file_value,
        };
    }

    if (object->has_sound_gain ||
        object->has_sound_pan ||
        object->has_sound_pitch ||
        object->has_sound_attached_object ||
        object->has_sound_position ||
        object->has_sound_direction) {
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "has_data2",
            .value_str = "true",
        };
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "gain",
            .value_str = gain_value,
        };
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "pan",
            .value_str = pan_value,
        };
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "pitch",
            .value_str = pitch_value,
        };
        NMO_RETURN_IF_ERROR(project_authoring_format_float(
            object->has_sound_gain ? object->sound_gain : 1.0f,
            gain_value,
            sizeof(gain_value)));
        NMO_RETURN_IF_ERROR(project_authoring_format_float(
            object->has_sound_pan ? object->sound_pan : 0.0f,
            pan_value,
            sizeof(pan_value)));
        NMO_RETURN_IF_ERROR(project_authoring_format_float(
            object->has_sound_pitch ? object->sound_pitch : 1.0f,
            pitch_value,
            sizeof(pitch_value)));
    }

    if (object->has_sound_attached_object) {
        nmo_object_id_t attached_id = project_authoring_find_object_id(
            objects,
            object_count,
            object->sound_attached_object_handle);
        if (attached_id == 0u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "project sound attached object was not authored");
        }
        int wrote = snprintf(attached_value, sizeof(attached_value), "%u", attached_id);
        if (wrote < 0 || (size_t)wrote >= sizeof(attached_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "attached object id string is too long");
        }
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "attached_object_id",
            .value_str = attached_value,
        };
    }
    if (object->has_sound_position) {
        NMO_RETURN_IF_ERROR(project_authoring_format_vector3(
            object->sound_position,
            position_value,
            sizeof(position_value)));
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "position",
            .value_str = position_value,
        };
    }
    if (object->has_sound_direction) {
        NMO_RETURN_IF_ERROR(project_authoring_format_vector3(
            object->sound_direction,
            direction_value,
            sizeof(direction_value)));
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "direction",
            .value_str = direction_value,
        };
    }

apply_fields:
    if (field_count == 0u) {
        NMO_RETURN_OK();
    }
    nmo_session_field_edit_result_t field_result = {0};
    NMO_RETURN_IF_ERROR(nmo_object_edit_set_fields(
        edit,
        object_id,
        fields,
        field_count,
        &field_result));
    if (field_result.failed > 0u) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "failed to set project wavesound fields");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_set_object_animation(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_project_object_desc_t *object,
    const nmo_project_runtime_object_t *objects,
    size_t object_count)
{
    nmo_session_field_edit_t fields[6] = {0};
    char format_value[32];
    char entity_value[32];
    char root_position_value[96];
    char flags_value[32];
    char length_value[32];
    size_t field_count = 0u;

    nmo_object_id_t target_id = project_authoring_find_object_id(
        objects,
        object_count,
        object->animation_target_handle);
    if (target_id == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "project animation target was not authored");
    }

    int wrote = snprintf(
        format_value,
        sizeof(format_value),
        "%u",
        (unsigned)object->animation_format);
    if (wrote < 0 || (size_t)wrote >= sizeof(format_value)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "animation format string is too long");
    }
    wrote = snprintf(entity_value, sizeof(entity_value), "%u", target_id);
    if (wrote < 0 || (size_t)wrote >= sizeof(entity_value)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "animation target id string is too long");
    }
    fields[field_count++] = (nmo_session_field_edit_t){
        .field_name = "format",
        .value_str = format_value,
    };
    fields[field_count++] = (nmo_session_field_edit_t){
        .field_name = "entity_id",
        .value_str = entity_value,
    };

    if (object->has_animation_root_position) {
        NMO_RETURN_IF_ERROR(project_authoring_format_vector3(
            object->animation_root_position,
            root_position_value,
            sizeof(root_position_value)));
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "root_pos",
            .value_str = root_position_value,
        };
    }
    if (object->has_animation_flags) {
        wrote = snprintf(
            flags_value,
            sizeof(flags_value),
            "%u",
            object->animation_flags);
        if (wrote < 0 || (size_t)wrote >= sizeof(flags_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "animation flags string is too long");
        }
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "flags",
            .value_str = flags_value,
        };
    }
    if (object->has_animation_length) {
        NMO_RETURN_IF_ERROR(project_authoring_format_float(
            object->animation_length,
            length_value,
            sizeof(length_value)));
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "has_length",
            .value_str = "1",
        };
        fields[field_count++] = (nmo_session_field_edit_t){
            .field_name = "length",
            .value_str = length_value,
        };
    }

    nmo_session_field_edit_result_t field_result = {0};
    NMO_RETURN_IF_ERROR(nmo_object_edit_set_fields(
        edit,
        object_id,
        fields,
        field_count,
        &field_result));
    if (field_result.failed > 0u) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "failed to set project object animation fields");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_set_scene_environment(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t scene_id,
    const nmo_project_scene_desc_t *scene)
{
    if (!scene->has_background_color &&
        !scene->has_ambient_light &&
        !scene->has_fog) {
        NMO_RETURN_OK();
    }
    return nmo_scene_edit_set_environment(
        edit,
        scene_id,
        &(nmo_scene_environment_settings_t){
            .has_background_color = scene->has_background_color,
            .background_color = {
                scene->background_color[0],
                scene->background_color[1],
                scene->background_color[2],
                scene->background_color[3],
            },
            .has_ambient_light = scene->has_ambient_light,
            .ambient_light = {
                scene->ambient_light[0],
                scene->ambient_light[1],
                scene->ambient_light[2],
                scene->ambient_light[3],
            },
            .has_fog = scene->has_fog,
            .fog_mode = scene->fog_mode,
            .fog_color = {
                scene->fog_color[0],
                scene->fog_color[1],
                scene->fog_color[2],
                scene->fog_color[3],
            },
            .fog_start = scene->fog_start,
            .fog_end = scene->fog_end,
            .fog_density = scene->fog_density,
        });
}

static nmo_status_t project_authoring_set_camera(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_project_object_desc_t *object)
{
    nmo_entity_camera_settings_t settings = {
        .fov = object->camera_fov,
        .near_plane = object->camera_near,
        .far_plane = object->camera_far,
    };
    return nmo_entity_edit_set_camera_settings(edit, object_id, &settings);
}

static nmo_status_t project_authoring_set_light(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_project_object_desc_t *object)
{
    nmo_entity_light_settings_t settings = {
        .diffuse = {
            object->light_diffuse[0],
            object->light_diffuse[1],
            object->light_diffuse[2],
            object->light_diffuse[3],
        },
        .range = object->light_range,
        .type = object->light_type,
    };
    return nmo_entity_edit_set_light_settings(edit, object_id, &settings);
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

        status = project_authoring_set_scene_environment(
            edit,
            scene_id,
            &scene);
        if (status != NMO_OK) {
            goto cleanup;
        }
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

    for (size_t i = 0; i < scene_count; ++i) {
        nmo_project_scene_desc_t scene = {0};
        status = nmo_project_plan_get_scene(plan, i, &scene);
        if (status != NMO_OK) {
            goto cleanup;
        }
        if (scene.active_camera_handle == 0u) {
            continue;
        }
        nmo_object_id_t scene_id = authored_scenes[i].object_id;
        nmo_object_id_t camera_id = project_authoring_find_object_id(
            authored_objects,
            object_count,
            scene.active_camera_handle);
        if (scene_id == 0u || camera_id == 0u) {
            status = NMO_ERR_INVALID_ARGUMENT;
            goto cleanup;
        }
        status = project_authoring_set_scene_active_camera(
            edit,
            scene_id,
            camera_id);
        if (status != NMO_OK) {
            goto cleanup;
        }
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

        if (object.parent_handle != 0u) {
            nmo_object_id_t parent_id = project_authoring_find_object_id(
                authored_objects,
                object_count,
                object.parent_handle);
            if (parent_id == 0) {
                status = NMO_ERR_INVALID_ARGUMENT;
                goto cleanup;
            }
            status = project_authoring_set_parent(
                edit,
                object_id,
                parent_id);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }

        if (object.has_position ||
            object.has_rotation_euler_deg ||
            object.has_scale) {
            status = project_authoring_set_transform(
                edit,
                object_id,
                &object);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }

        if (object.has_camera) {
            status = project_authoring_set_camera(edit, object_id, &object);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }
        if (object.has_camera_target) {
            nmo_object_id_t target_id = project_authoring_find_object_id(
                authored_objects,
                object_count,
                object.camera_target_handle);
            if (target_id == 0u) {
                status = NMO_ERR_INVALID_ARGUMENT;
                goto cleanup;
            }
            status = nmo_entity_edit_set_camera_target(edit, object_id, target_id);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }

        if (object.has_light) {
            status = project_authoring_set_light(edit, object_id, &object);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }
        if (object.has_light_target) {
            nmo_object_id_t target_id = project_authoring_find_object_id(
                authored_objects,
                object_count,
                object.light_target_handle);
            if (target_id == 0u) {
                status = NMO_ERR_INVALID_ARGUMENT;
                goto cleanup;
            }
            status = nmo_entity_edit_set_light_target(edit, object_id, target_id);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }

        if (object.has_sound) {
            status = project_authoring_set_wavesound(
                edit,
                object_id,
                &object,
                authored_objects,
                object_count);
            if (status != NMO_OK) {
                goto cleanup;
            }
        }

        if (object.has_animation) {
            status = project_authoring_set_object_animation(
                edit,
                object_id,
                &object,
                authored_objects,
                object_count);
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
