#include "project/nmo_project_validator.h"

#include "core/nmo_arena.h"
#include "format/nmo_obj_parser.h"
#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_script_authoring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *project_validation_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }

    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1u);
    return copy;
}

static void project_validation_clear(nmo_project_validation_report_t *report)
{
    if (!report) {
        return;
    }

    for (size_t i = 0; i < report->issue_count; ++i) {
        free(report->issues[i].code);
        free(report->issues[i].message);
        free(report->issues[i].subject_kind);
        free(report->issues[i].subject_name);
        free(report->issues[i].source_path);
    }
    free(report->issues);
    report->issues = NULL;
    report->issue_count = 0u;
    report->issue_capacity = 0u;
    report->ok = false;
}

static nmo_status_t project_validation_add_issue_ex(
    nmo_project_validation_report_t *report,
    const char *code,
    const char *message,
    const char *subject_kind,
    const char *subject_name,
    const char *source_path);

static nmo_status_t project_validation_add_issue(
    nmo_project_validation_report_t *report,
    const char *code,
    const char *message)
{
    return project_validation_add_issue_ex(
        report,
        code,
        message,
        NULL,
        NULL,
        NULL);
}

static nmo_status_t project_validation_add_issue_ex(
    nmo_project_validation_report_t *report,
    const char *code,
    const char *message,
    const char *subject_kind,
    const char *subject_name,
    const char *source_path)
{
    if (!report || !code) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "validation report and issue code are required");
    }

    if (report->issue_count == report->issue_capacity) {
        size_t new_capacity = report->issue_capacity ? report->issue_capacity * 2u : 4u;
        nmo_project_validation_issue_t *new_issues =
            (nmo_project_validation_issue_t *)realloc(
                report->issues,
                new_capacity * sizeof(*new_issues));
        if (!new_issues) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate project validation issue");
        }
        memset(new_issues + report->issue_capacity,
               0,
               (new_capacity - report->issue_capacity) * sizeof(*new_issues));
        report->issues = new_issues;
        report->issue_capacity = new_capacity;
    }

    nmo_project_validation_issue_t *issue = &report->issues[report->issue_count];
    issue->code = project_validation_strdup(code);
    issue->message = project_validation_strdup(message ? message : code);
    issue->subject_kind = project_validation_strdup(subject_kind);
    issue->subject_name = project_validation_strdup(subject_name);
    issue->source_path = project_validation_strdup(source_path);
    if (!issue->code || !issue->message ||
        (subject_kind && !issue->subject_kind) ||
        (subject_name && !issue->subject_name) ||
        (source_path && !issue->source_path)) {
        free(issue->code);
        free(issue->message);
        free(issue->subject_kind);
        free(issue->subject_name);
        free(issue->source_path);
        memset(issue, 0, sizeof(*issue));
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to copy project validation issue");
    }

    report->issue_count++;
    NMO_RETURN_OK();
}

void nmo_project_validation_report_init(nmo_project_validation_report_t *report)
{
    if (!report) {
        return;
    }
    memset(report, 0, sizeof(*report));
}

void nmo_project_validation_report_dispose(nmo_project_validation_report_t *report)
{
    if (!report) {
        return;
    }
    project_validation_clear(report);
}

bool nmo_project_validation_contains(
    const nmo_project_validation_report_t *report,
    const char *code)
{
    if (!report || !code) {
        return false;
    }

    for (size_t i = 0; i < report->issue_count; ++i) {
        if (report->issues[i].code && strcmp(report->issues[i].code, code) == 0) {
            return true;
        }
    }

    return false;
}

static bool project_validation_has_scene_handle(
    const nmo_project_plan_t *plan,
    uint32_t handle)
{
    size_t scene_count = nmo_project_plan_scene_count(plan);
    for (size_t i = 0; i < scene_count; ++i) {
        nmo_project_scene_desc_t scene = {0};
        if (nmo_project_plan_get_scene(plan, i, &scene) == NMO_OK &&
            scene.handle == handle) {
            return true;
        }
    }
    return false;
}

static bool project_validation_has_object_handle(
    const nmo_project_plan_t *plan,
    uint32_t handle)
{
    size_t object_count = nmo_project_plan_object_count(plan);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        if (nmo_project_plan_get_object(plan, i, &object) == NMO_OK &&
            object.handle == handle) {
            return true;
        }
    }
    return false;
}

static bool project_validation_get_object_by_handle(
    const nmo_project_plan_t *plan,
    uint32_t handle,
    nmo_project_object_desc_t *out_object)
{
    size_t object_count = nmo_project_plan_object_count(plan);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        if (nmo_project_plan_get_object(plan, i, &object) == NMO_OK &&
            object.handle == handle) {
            if (out_object) {
                *out_object = object;
            }
            return true;
        }
    }
    return false;
}

static bool project_validation_file_exists(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);
    return true;
}

static nmo_status_t project_validation_read_file(
    const char *path,
    uint8_t **out_data,
    size_t *out_size)
{
    if (!path || !out_data || !out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "file read arguments are required");
    }
    *out_data = NULL;
    *out_size = 0u;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_OPEN_FILE, NMO_SEVERITY_ERROR,
                         "failed to open project asset file");
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "failed to seek project asset file");
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "failed to size project asset file");
    }
    rewind(fp);
    uint8_t *data = (uint8_t *)malloc((size_t)size + 1u);
    if (!data) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project asset file buffer");
    }
    if ((size_t)size > 0u &&
        fread(data, 1u, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "failed to read project asset file");
    }
    fclose(fp);
    data[size] = 0u;
    *out_data = data;
    *out_size = (size_t)size;
    NMO_RETURN_OK();
}

static bool project_validation_class_is_entity(nmo_class_id_t class_id)
{
    switch (class_id) {
    case NMO_CID_3DENTITY:
    case NMO_CID_3DOBJECT:
    case NMO_CID_CAMERA:
    case NMO_CID_TARGETCAMERA:
    case NMO_CID_LIGHT:
    case NMO_CID_TARGETLIGHT:
    case NMO_CID_CHARACTER:
    case NMO_CID_SPRITE3D:
    case NMO_CID_CURVE:
    case NMO_CID_CURVEPOINT:
    case NMO_CID_BODYPART:
        return true;
    default:
        return false;
    }
}

static bool project_validation_class_is_camera(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_CAMERA || class_id == NMO_CID_TARGETCAMERA;
}

static bool project_validation_class_is_light(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_LIGHT || class_id == NMO_CID_TARGETLIGHT;
}

static bool project_validation_class_is_wavesound(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_WAVESOUND;
}

static bool project_validation_class_is_objectanimation(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_OBJECTANIMATION;
}

static nmo_status_t project_validation_check_duplicate_scenes(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    size_t scene_count = nmo_project_plan_scene_count(plan);
    for (size_t i = 0; i < scene_count; ++i) {
        nmo_project_scene_desc_t lhs = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_scene(plan, i, &lhs));
        for (size_t j = i + 1u; j < scene_count; ++j) {
            nmo_project_scene_desc_t rhs = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_scene(plan, j, &rhs));
            if (lhs.handle == rhs.handle) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue(
                    report,
                    "duplicate_scene_handle",
                    "Project scene handles must be unique"));
            }
        }
        if (lhs.active_camera_handle != 0u) {
            nmo_project_object_desc_t camera = {0};
            if (!project_validation_get_object_by_handle(
                    plan,
                    lhs.active_camera_handle,
                    &camera)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "missing_active_camera",
                    "Project scene active camera references a missing object",
                    "scene",
                    lhs.name,
                    lhs.active_camera_source_path));
            } else {
                if (camera.scene_handle != lhs.handle) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "active_camera_outside_scene",
                        "Project scene active camera must belong to the scene",
                        "scene",
                        lhs.name,
                        lhs.active_camera_source_path));
                }
                if (!project_validation_class_is_camera(camera.class_id)) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "invalid_active_camera_class",
                        "Project scene active camera must be camera-compatible",
                        "scene",
                        lhs.name,
                        lhs.active_camera_source_path));
                }
            }
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_validation_check_objects(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    size_t object_count = nmo_project_plan_object_count(plan);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, i, &object));

        if (object.class_id == 0u ||
            object.class_id == NMO_CLASS_ID_INVALID ||
            object.class_id > NMO_CID_MAXCLASSID) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "invalid_object_class",
                "Project object class must be a valid CKObject class ID"));
        }
        if (object.scene_handle != 0u &&
            !project_validation_has_scene_handle(plan, object.scene_handle)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "missing_scene",
                "Project object references a missing scene handle"));
        }
        if (object.parent_handle != 0u &&
            !project_validation_has_object_handle(plan, object.parent_handle)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "missing_parent",
                "Project object references a missing parent object handle"));
        }
        if (object.parent_handle != 0u) {
            nmo_project_object_desc_t parent = {0};
            if (project_validation_get_object_by_handle(
                    plan,
                    object.parent_handle,
                    &parent) &&
                !project_validation_class_is_entity(parent.class_id)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue(
                    report,
                    "invalid_parent_class",
                    "Project object parent must be a 3D entity-compatible object"));
            }
        }
        if ((object.has_position ||
             object.has_rotation_euler_deg ||
             object.has_scale) &&
            !project_validation_class_is_entity(object.class_id)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "invalid_transform_target",
                "Project object transform requires a 3D entity-compatible class"));
        }
        if (object.has_camera && !project_validation_class_is_camera(object.class_id)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                report,
                "invalid_camera_target",
                "Project camera settings require CKCamera or CKTargetCamera",
                "object",
                object.name,
                object.source_path));
        }
        if (object.has_camera_target) {
            nmo_project_object_desc_t target = {0};
            if (object.class_id != NMO_CID_TARGETCAMERA) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_camera_target_binding_class",
                    "Project camera target requires CKTargetCamera",
                    "object",
                    object.name,
                    object.source_path));
            }
            if (!project_validation_get_object_by_handle(
                    plan,
                    object.camera_target_handle,
                    &target)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "missing_camera_target",
                    "Project camera target references a missing object",
                    "object",
                    object.name,
                    object.source_path));
            } else if (!project_validation_class_is_entity(target.class_id)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_camera_target_object",
                    "Project camera target must be entity-compatible",
                    "object",
                    object.name,
                    object.source_path));
            }
        }
        if (object.has_light && !project_validation_class_is_light(object.class_id)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                report,
                "invalid_light_target",
                "Project light settings require CKLight or CKTargetLight",
                "object",
                object.name,
                object.source_path));
        }
        if (object.has_light_target) {
            nmo_project_object_desc_t target = {0};
            if (object.class_id != NMO_CID_TARGETLIGHT) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_light_target_binding_class",
                    "Project light target requires CKTargetLight",
                    "object",
                    object.name,
                    object.source_path));
            }
            if (!project_validation_get_object_by_handle(
                    plan,
                    object.light_target_handle,
                    &target)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "missing_light_target",
                    "Project light target references a missing object",
                    "object",
                    object.name,
                    object.source_path));
            } else if (!project_validation_class_is_entity(target.class_id)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_light_target_object",
                    "Project light target must be entity-compatible",
                    "object",
                    object.name,
                    object.source_path));
            }
        }

        for (size_t j = i + 1u; j < object_count; ++j) {
            nmo_project_object_desc_t other = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, j, &other));
            if (object.handle == other.handle) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue(
                    report,
                    "duplicate_object_handle",
                    "Project object handles must be unique"));
            }
        }
        if (object.has_sound) {
            if (!project_validation_class_is_wavesound(object.class_id)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_sound_class",
                    "Project sound settings require CKWaveSound",
                    "object",
                    object.name,
                    object.source_path));
            }
            if (!object.sound_file_path || object.sound_file_path[0] == '\0') {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "missing_sound_file",
                    "Project sound requires a non-empty file path",
                    "object",
                    object.name,
                    object.sound_file_source_path ? object.sound_file_source_path : object.source_path));
            } else if (!project_validation_file_exists(object.sound_file_path)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "missing_sound_file",
                    "Project sound file does not exist",
                    "object",
                    object.name,
                    object.sound_file_source_path ? object.sound_file_source_path : object.source_path));
            }
            if (object.has_sound_attached_object) {
                nmo_project_object_desc_t attached = {0};
                if (!project_validation_get_object_by_handle(
                        plan,
                        object.sound_attached_object_handle,
                        &attached)) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "missing_sound_attached_object",
                        "Project sound attached object references a missing object",
                        "object",
                        object.name,
                        object.source_path));
                } else if (!project_validation_class_is_entity(attached.class_id)) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "invalid_sound_attached_object",
                        "Project sound attached object must be entity-compatible",
                        "object",
                        object.name,
                        object.source_path));
                }
            }
        }
        if (object.has_animation) {
            nmo_project_object_desc_t target = {0};
            if (!project_validation_class_is_objectanimation(object.class_id)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_animation_class",
                    "Project animation settings require CKObjectAnimation",
                    "object",
                    object.name,
                    object.source_path));
            }
            if (object.animation_format != CKOBJANIM_FORMAT_CONTROLLERS) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "unsupported_animation_format",
                    "Project animation only supports controllers format",
                    "object",
                    object.name,
                    object.source_path));
            }
            if (!project_validation_get_object_by_handle(
                    plan,
                    object.animation_target_handle,
                    &target)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "missing_animation_target",
                    "Project animation target references a missing object",
                    "object",
                    object.name,
                    object.source_path));
            } else if (!project_validation_class_is_entity(target.class_id)) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_animation_target_object",
                    "Project animation target must be entity-compatible",
                    "object",
                    object.name,
                    object.source_path));
            }
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_validation_check_assets(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    size_t asset_count = nmo_project_plan_asset_count(plan);
    for (size_t i = 0u; i < asset_count; ++i) {
        nmo_project_asset_desc_t asset = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_asset(plan, i, &asset));
        nmo_project_object_desc_t asset_object = {0};
        bool has_asset_object = project_validation_get_object_by_handle(
            plan,
            asset.object_handle,
            &asset_object);
        if (asset.object_handle == 0u ||
            !has_asset_object) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "missing_asset_object",
                "Project asset references a missing object handle"));
        }
        if (asset.has_external_mesh &&
            !project_validation_file_exists(asset.external_mesh_path)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                report,
                "missing_external_mesh_file",
                "Project external OBJ mesh path must exist",
                "object",
                has_asset_object ? asset_object.name : NULL,
                asset.external_mesh_source_path));
        }
        if (asset.has_material_texture) {
            for (size_t slot = 0u; slot < 4u; ++slot) {
                const char *texture_path = asset.has_material_texture_slots[slot]
                    ? asset.material_texture_paths[slot]
                    : NULL;
                const char *source_path = asset.has_material_texture_slots[slot]
                    ? asset.material_texture_source_paths[slot]
                    : NULL;
                if (!texture_path && slot == 0u && asset.material_texture_path) {
                    texture_path = asset.material_texture_path;
                    source_path = asset.material_texture_source_path;
                }
                if (texture_path &&
                    !project_validation_file_exists(texture_path)) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "missing_material_texture_file",
                        "Project material texture path must exist",
                        "object",
                        has_asset_object ? asset_object.name : NULL,
                        source_path));
                }
            }
        }

        size_t obj_material_count =
            nmo_project_plan_obj_material_count(plan, asset.object_handle);
        if (obj_material_count > 0u && !asset.has_external_mesh) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "obj_material_without_external_mesh",
                "Project OBJ material bindings require an external OBJ mesh"));
        }
        for (size_t material_index = 0u;
             material_index < obj_material_count;
             ++material_index) {
            nmo_project_material_spec_t material = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_obj_material(
                plan,
                asset.object_handle,
                material_index,
                &material));
            for (size_t slot = 0u; slot < 4u; ++slot) {
                const char *texture_path = material.has_texture_slots[slot]
                    ? material.texture_paths[slot]
                    : NULL;
                const char *source_path = material.has_texture_slots[slot]
                    ? material.texture_source_paths[slot]
                    : NULL;
                if (!texture_path && slot == 0u && material.texture_path) {
                    texture_path = material.texture_path;
                    source_path = material.texture_source_path;
                }
                if (texture_path &&
                    !project_validation_file_exists(texture_path)) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "missing_obj_material_texture_file",
                        "Project OBJ material texture path must exist",
                        "object",
                        has_asset_object ? asset_object.name : NULL,
                        source_path));
                }
            }
            for (size_t other_index = material_index + 1u;
                 other_index < obj_material_count;
                 ++other_index) {
                nmo_project_material_spec_t other = {0};
                NMO_RETURN_IF_ERROR(nmo_project_plan_get_obj_material(
                    plan,
                    asset.object_handle,
                    other_index,
                    &other));
                if (material.obj_material_name &&
                    other.obj_material_name &&
                    strcmp(material.obj_material_name, other.obj_material_name) == 0) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "duplicate_obj_material",
                        "Project OBJ material names must be unique per object",
                        "object",
                        has_asset_object ? asset_object.name : NULL,
                        material.source_path));
                }
            }
        }
        if (asset.has_external_mesh &&
            project_validation_file_exists(asset.external_mesh_path) &&
            obj_material_count > 0u &&
            !asset.has_material_color &&
            !asset.has_material_diffuse &&
            !asset.has_material_ambient &&
            !asset.has_material_specular &&
            !asset.has_material_emissive &&
            !asset.has_material_specular_power &&
            !asset.has_material_texture) {
            uint8_t *obj_bytes = NULL;
            size_t obj_size = 0u;
            nmo_status_t read_status = project_validation_read_file(
                asset.external_mesh_path,
                &obj_bytes,
                &obj_size);
            if (read_status != NMO_OK) {
                return read_status;
            }
            nmo_arena_t *arena = nmo_arena_create(NULL, obj_size + 4096u);
            if (!arena) {
                free(obj_bytes);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "failed to allocate OBJ validation arena");
            }
            nmo_obj_data_t obj_data = {0};
            nmo_status_t parse_status =
                nmo_obj_parse(arena, (const char *)obj_bytes, obj_size, &obj_data);
            free(obj_bytes);
            if (parse_status != NMO_OK) {
                nmo_arena_destroy(arena);
                NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                    report,
                    "invalid_external_mesh_file",
                    "Project external OBJ mesh path must parse",
                    "object",
                    has_asset_object ? asset_object.name : NULL,
                    asset.external_mesh_source_path));
                continue;
            }

            for (size_t face_index = 0u; face_index < obj_data.face_count; ++face_index) {
                if (obj_data.faces[face_index].material_group == NMO_OBJ_NO_MATERIAL) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "unbound_obj_material",
                        "Project OBJ has unassigned material faces and no default material",
                        "object",
                        has_asset_object ? asset_object.name : NULL,
                        asset.external_mesh_source_path));
                    break;
                }
            }
            for (size_t name_index = 0u;
                 name_index < obj_data.material_name_count;
                 ++name_index) {
                const char *obj_name = obj_data.material_names
                    ? obj_data.material_names[name_index]
                    : NULL;
                bool bound = false;
                for (size_t material_index = 0u;
                     material_index < obj_material_count;
                     ++material_index) {
                    nmo_project_material_spec_t material = {0};
                    NMO_RETURN_IF_ERROR(nmo_project_plan_get_obj_material(
                        plan,
                        asset.object_handle,
                        material_index,
                        &material));
                    if (obj_name && material.obj_material_name &&
                        strcmp(obj_name, material.obj_material_name) == 0) {
                        bound = true;
                        break;
                    }
                }
                if (!bound) {
                    NMO_RETURN_IF_ERROR(project_validation_add_issue_ex(
                        report,
                        "unbound_obj_material",
                        "Project OBJ material group has no binding and no default material",
                        "object",
                        has_asset_object ? asset_object.name : NULL,
                        asset.external_mesh_source_path));
                }
            }
            nmo_arena_destroy(arena);
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_validation_check_scripts(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    size_t script_count = nmo_project_plan_script_count(plan);
    for (size_t i = 0u; i < script_count; ++i) {
        nmo_project_script_desc_t script = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_script(plan, i, &script));

        if (script.object_handle == 0u ||
            !project_validation_has_object_handle(plan, script.object_handle)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "missing_script_object",
                "Project script references a missing object handle"));
        }
        if (!script.name || script.name[0] == '\0') {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "missing_script_name",
                "Project script requires a non-empty name"));
        }
        for (size_t step_index = 0u; step_index < script.step_count; ++step_index) {
            nmo_project_script_step_desc_t step = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_script_step(
                plan,
                script.handle,
                step_index,
                &step));
            if (step.kind != NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT &&
                step.kind != NMO_PROJECT_SCRIPT_STEP_ON_START_DEBUG_OUTPUT &&
                step.kind !=
                    NMO_PROJECT_SCRIPT_STEP_SCENE_ON_START_DEBUG_OUTPUT &&
                step.kind != NMO_PROJECT_SCRIPT_STEP_TIMER_DEBUG_OUTPUT &&
                step.kind != NMO_PROJECT_SCRIPT_STEP_INPUT_KEY_DEBUG_OUTPUT &&
                step.kind !=
                    NMO_PROJECT_SCRIPT_STEP_OBJECT_TRIGGER_DEBUG_OUTPUT &&
                step.kind !=
                    NMO_PROJECT_SCRIPT_STEP_SCENE_START_THEN_TIMER_DEBUG_OUTPUT) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue(
                    report,
                    "unsupported_script_template",
                    "Project script step kind is not supported"));
            }
        }

        for (size_t j = i + 1u; j < script_count; ++j) {
            nmo_project_script_desc_t other = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_script(plan, j, &other));
            if (script.handle == other.handle) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue(
                    report,
                    "duplicate_script_handle",
                    "Project script handles must be unique"));
            }
        }
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_validate_plan(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    if (!plan || !report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and validation report are required");
    }

    project_validation_clear(report);

    const char *document_name = nmo_project_plan_document_name(plan);
    if (!document_name || document_name[0] == '\0') {
        NMO_RETURN_IF_ERROR(project_validation_add_issue(
            report,
            "missing_document_name",
            "Project plan requires a document name"));
    }

    NMO_RETURN_IF_ERROR(project_validation_check_duplicate_scenes(plan, report));
    NMO_RETURN_IF_ERROR(project_validation_check_objects(plan, report));
    NMO_RETURN_IF_ERROR(project_validation_check_assets(plan, report));
    NMO_RETURN_IF_ERROR(project_validation_check_scripts(plan, report));

    report->ok = report->issue_count == 0u;
    NMO_RETURN_OK();
}
