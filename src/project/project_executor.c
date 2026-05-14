#include "project/nmo_project_executor.h"

#include "document/nmo_document.h"
#include "document/nmo_document_file_state.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "project/nmo_asset_plan.h"
#include "project_internal.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_script_authoring.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *project_executor_strdup(const char *src)
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

static void project_report_name_list_dispose(nmo_project_report_name_list_t *list)
{
    if (!list) {
        return;
    }
    for (size_t i = 0u; i < list->count; ++i) {
        free(list->names[i]);
    }
    free(list->names);
    memset(list, 0, sizeof(*list));
}

static void project_report_diff_dispose(nmo_project_report_diff_t *diff)
{
    if (!diff) {
        return;
    }
    project_report_name_list_dispose(&diff->created);
}

static void project_report_evidence_dispose(nmo_project_report_evidence_t *evidence)
{
    if (!evidence) {
        return;
    }
    for (size_t i = 0u; i < evidence->object_count; ++i) {
        free(evidence->objects[i].name);
    }
    free(evidence->objects);
    for (size_t i = 0u; i < evidence->asset_binding_count; ++i) {
        free(evidence->asset_bindings[i].owner_name);
        free(evidence->asset_bindings[i].asset_name);
        free(evidence->asset_bindings[i].kind);
    }
    free(evidence->asset_bindings);
    for (size_t i = 0u; i < evidence->material_texture_slot_count; ++i) {
        free(evidence->material_texture_slots[i].material_name);
        free(evidence->material_texture_slots[i].texture_name);
        free(evidence->material_texture_slots[i].source_path);
    }
    free(evidence->material_texture_slots);
    for (size_t i = 0u; i < evidence->material_channel_count; ++i) {
        free(evidence->material_channels[i].material_name);
    }
    free(evidence->material_channels);
    for (size_t i = 0u; i < evidence->script_count; ++i) {
        free(evidence->scripts[i].name);
    }
    free(evidence->scripts);
    for (size_t i = 0u; i < evidence->sound_binding_count; ++i) {
        free(evidence->sound_bindings[i].name);
        free(evidence->sound_bindings[i].file);
        free(evidence->sound_bindings[i].attached_object_name);
    }
    free(evidence->sound_bindings);
    for (size_t i = 0u; i < evidence->animation_binding_count; ++i) {
        free(evidence->animation_bindings[i].name);
        free(evidence->animation_bindings[i].target_name);
        free(evidence->animation_bindings[i].controllers);
        free(evidence->animation_bindings[i].morph_keys);
    }
    free(evidence->animation_bindings);
    memset(evidence, 0, sizeof(*evidence));
}

static void project_report_dispose_diffs(nmo_project_report_t *report)
{
    if (!report) {
        return;
    }
    project_report_diff_dispose(&report->document_diff);
    project_report_diff_dispose(&report->scene_diff);
    project_report_diff_dispose(&report->object_diff);
    project_report_diff_dispose(&report->asset_diff);
    project_report_diff_dispose(&report->script_diff);
    project_report_diff_dispose(&report->manager_diff);
}

static nmo_status_t project_report_add_created(
    nmo_project_report_diff_t *diff,
    const char *name)
{
    if (!diff || !name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "report diff and created name are required");
    }

    char *copy = project_executor_strdup(name);
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project report name");
    }

    size_t next_count = diff->created.count + 1u;
    char **next_names =
        (char **)realloc(diff->created.names, next_count * sizeof(*next_names));
    if (!next_names) {
        free(copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project report names");
    }

    diff->created.names = next_names;
    diff->created.names[diff->created.count] = copy;
    diff->created.count = next_count;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_format_asset_name(
    const char *owner_name,
    const char *suffix,
    char *out_name,
    size_t out_size)
{
    int len = snprintf(out_name, out_size, "%s_%s", owner_name, suffix);
    if (len < 0 || (size_t)len >= out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "generated asset evidence name is too long");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_report_format_obj_material_name(
    const char *owner_name,
    const char *obj_material_name,
    const char *suffix,
    char *out_name,
    size_t out_size)
{
    int len = snprintf(out_name,
                       out_size,
                       "%s_%s_%s",
                       owner_name,
                       obj_material_name,
                       suffix);
    if (len < 0 || (size_t)len >= out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "generated OBJ material evidence name is too long");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_report_format_texture_name(
    const char *base_name,
    uint32_t slot,
    char *out_name,
    size_t out_size)
{
    int len = slot == 0u
        ? snprintf(out_name, out_size, "%s_Texture", base_name)
        : snprintf(out_name, out_size, "%s_Texture%u", base_name, slot);
    if (len < 0 || (size_t)len >= out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "generated texture evidence name is too long");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_report_add_object_evidence(
    nmo_project_report_evidence_t *evidence,
    uint32_t plan_handle,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    const char *name)
{
    if (!evidence || !name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "object evidence and name are required");
    }

    char *name_copy = project_executor_strdup(name);
    if (!name_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate object evidence name");
    }

    size_t next_count = evidence->object_count + 1u;
    nmo_project_report_object_evidence_t *next_objects =
        (nmo_project_report_object_evidence_t *)realloc(
            evidence->objects,
            next_count * sizeof(*next_objects));
    if (!next_objects) {
        free(name_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate object evidence");
    }

    evidence->objects = next_objects;
    nmo_project_report_object_evidence_t *item =
        &evidence->objects[evidence->object_count];
    item->plan_handle = plan_handle;
    item->object_id = object_id;
    item->class_id = class_id;
    item->name = name_copy;
    evidence->object_count = next_count;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_add_asset_binding_evidence(
    nmo_project_report_evidence_t *evidence,
    const char *owner_name,
    const char *asset_name,
    const char *kind)
{
    if (!evidence || !owner_name || !asset_name || !kind) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "asset binding evidence fields are required");
    }

    char *owner_copy = project_executor_strdup(owner_name);
    char *asset_copy = project_executor_strdup(asset_name);
    char *kind_copy = project_executor_strdup(kind);
    if (!owner_copy || !asset_copy || !kind_copy) {
        free(owner_copy);
        free(asset_copy);
        free(kind_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate asset binding evidence");
    }

    size_t next_count = evidence->asset_binding_count + 1u;
    nmo_project_report_asset_binding_evidence_t *next_bindings =
        (nmo_project_report_asset_binding_evidence_t *)realloc(
            evidence->asset_bindings,
            next_count * sizeof(*next_bindings));
    if (!next_bindings) {
        free(owner_copy);
        free(asset_copy);
        free(kind_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate asset binding evidence");
    }

    evidence->asset_bindings = next_bindings;
    nmo_project_report_asset_binding_evidence_t *item =
        &evidence->asset_bindings[evidence->asset_binding_count];
    item->owner_name = owner_copy;
    item->asset_name = asset_copy;
    item->kind = kind_copy;
    evidence->asset_binding_count = next_count;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_add_material_texture_slot_evidence(
    nmo_project_report_evidence_t *evidence,
    const char *material_name,
    uint32_t slot,
    const char *texture_name,
    const char *source_path)
{
    if (!evidence || !material_name || !texture_name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "material texture slot evidence fields are required");
    }

    char *material_copy = project_executor_strdup(material_name);
    char *texture_copy = project_executor_strdup(texture_name);
    char *source_copy = source_path ? project_executor_strdup(source_path) : NULL;
    if (!material_copy || !texture_copy || (source_path && !source_copy)) {
        free(material_copy);
        free(texture_copy);
        free(source_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate material texture slot evidence");
    }

    size_t next_count = evidence->material_texture_slot_count + 1u;
    nmo_project_report_material_texture_slot_evidence_t *next_slots =
        (nmo_project_report_material_texture_slot_evidence_t *)realloc(
            evidence->material_texture_slots,
            next_count * sizeof(*next_slots));
    if (!next_slots) {
        free(material_copy);
        free(texture_copy);
        free(source_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate material texture slot evidence");
    }

    evidence->material_texture_slots = next_slots;
    nmo_project_report_material_texture_slot_evidence_t *item =
        &evidence->material_texture_slots[evidence->material_texture_slot_count];
    item->material_name = material_copy;
    item->slot = slot;
    item->texture_name = texture_copy;
    item->source_path = source_copy;
    evidence->material_texture_slot_count = next_count;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_add_material_channel_evidence(
    nmo_project_report_evidence_t *evidence,
    const char *material_name,
    bool has_diffuse,
    bool has_ambient,
    bool has_specular,
    bool has_emissive,
    bool has_specular_power)
{
    if (!evidence || !material_name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "material channel evidence fields are required");
    }

    char *material_copy = project_executor_strdup(material_name);
    if (!material_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate material channel evidence");
    }

    size_t next_count = evidence->material_channel_count + 1u;
    nmo_project_report_material_channel_evidence_t *next_channels =
        (nmo_project_report_material_channel_evidence_t *)realloc(
            evidence->material_channels,
            next_count * sizeof(*next_channels));
    if (!next_channels) {
        free(material_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate material channel evidence");
    }

    evidence->material_channels = next_channels;
    nmo_project_report_material_channel_evidence_t *item =
        &evidence->material_channels[evidence->material_channel_count];
    item->material_name = material_copy;
    item->has_diffuse = has_diffuse;
    item->has_ambient = has_ambient;
    item->has_specular = has_specular;
    item->has_emissive = has_emissive;
    item->has_specular_power = has_specular_power;
    evidence->material_channel_count = next_count;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_add_script_evidence(
    nmo_project_report_evidence_t *evidence,
    const char *name,
    size_t step_count,
    bool validation_ok)
{
    if (!evidence || !name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "script evidence and name are required");
    }

    char *name_copy = project_executor_strdup(name);
    if (!name_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate script evidence name");
    }

    size_t next_count = evidence->script_count + 1u;
    nmo_project_report_script_evidence_t *next_scripts =
        (nmo_project_report_script_evidence_t *)realloc(
            evidence->scripts,
            next_count * sizeof(*next_scripts));
    if (!next_scripts) {
        free(name_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate script evidence");
    }

    evidence->scripts = next_scripts;
    nmo_project_report_script_evidence_t *item =
        &evidence->scripts[evidence->script_count];
    item->name = name_copy;
    item->step_count = step_count;
    item->validation_ok = validation_ok;
    evidence->script_count = next_count;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_add_sound_binding_evidence(
    nmo_project_report_evidence_t *evidence,
    const char *name,
    const char *file,
    const char *attached_object_name)
{
    if (!evidence || !name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "sound binding evidence fields are required");
    }

    char *name_copy = project_executor_strdup(name);
    char *file_copy = file ? project_executor_strdup(file) : NULL;
    char *attached_copy = attached_object_name
        ? project_executor_strdup(attached_object_name)
        : NULL;
    if (!name_copy || (file && !file_copy) ||
        (attached_object_name && !attached_copy)) {
        free(name_copy);
        free(file_copy);
        free(attached_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate sound binding evidence");
    }

    size_t next_count = evidence->sound_binding_count + 1u;
    nmo_project_report_sound_binding_evidence_t *next_bindings =
        (nmo_project_report_sound_binding_evidence_t *)realloc(
            evidence->sound_bindings,
            next_count * sizeof(*next_bindings));
    if (!next_bindings) {
        free(name_copy);
        free(file_copy);
        free(attached_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate sound binding evidence");
    }

    evidence->sound_bindings = next_bindings;
    nmo_project_report_sound_binding_evidence_t *item =
        &evidence->sound_bindings[evidence->sound_binding_count];
    item->name = name_copy;
    item->file = file_copy;
    item->attached_object_name = attached_copy;
    evidence->sound_binding_count = next_count;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_add_animation_binding_evidence(
    nmo_project_report_evidence_t *evidence,
    const char *name,
    const char *target_name,
    uint32_t format,
    bool has_length,
    float length,
    const nmo_objanim_controller_t *controllers,
    size_t controller_count,
    const nmo_objanim_morph_key_t *morph_keys,
    size_t morph_key_count)
{
    if (!evidence || !name || !target_name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "animation binding evidence fields are required");
    }

    char *name_copy = project_executor_strdup(name);
    char *target_copy = project_executor_strdup(target_name);
    nmo_project_report_animation_controller_evidence_t *controller_copy = NULL;
    nmo_project_report_animation_morph_key_evidence_t *morph_key_copy = NULL;
    if (controller_count > 0u) {
        if (!controllers ||
            controller_count > SIZE_MAX / sizeof(*controller_copy)) {
            free(name_copy);
            free(target_copy);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "invalid animation controller evidence");
        }
        controller_copy =
            (nmo_project_report_animation_controller_evidence_t *)calloc(
                controller_count,
                sizeof(*controller_copy));
        if (!controller_copy) {
            free(name_copy);
            free(target_copy);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate animation controller evidence");
        }
        for (size_t i = 0u; i < controller_count; ++i) {
            controller_copy[i].type = controllers[i].type;
            controller_copy[i].data_size = controllers[i].data_size;
        }
    }
    if (morph_key_count > 0u) {
        if (!morph_keys ||
            morph_key_count > SIZE_MAX / sizeof(*morph_key_copy)) {
            free(name_copy);
            free(target_copy);
            free(controller_copy);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "invalid animation morph key evidence");
        }
        morph_key_copy =
            (nmo_project_report_animation_morph_key_evidence_t *)calloc(
                morph_key_count,
                sizeof(*morph_key_copy));
        if (!morph_key_copy) {
            free(name_copy);
            free(target_copy);
            free(controller_copy);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate animation morph key evidence");
        }
        for (size_t i = 0u; i < morph_key_count; ++i) {
            morph_key_copy[i].time_step = morph_keys[i].time_step;
            morph_key_copy[i].data_size = morph_keys[i].data_size;
        }
    }
    if (!name_copy || !target_copy) {
        free(name_copy);
        free(target_copy);
        free(controller_copy);
        free(morph_key_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate animation binding evidence");
    }

    size_t next_count = evidence->animation_binding_count + 1u;
    nmo_project_report_animation_binding_evidence_t *next_bindings =
        (nmo_project_report_animation_binding_evidence_t *)realloc(
            evidence->animation_bindings,
            next_count * sizeof(*next_bindings));
    if (!next_bindings) {
        free(name_copy);
        free(target_copy);
        free(controller_copy);
        free(morph_key_copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate animation binding evidence");
    }

    evidence->animation_bindings = next_bindings;
    nmo_project_report_animation_binding_evidence_t *item =
        &evidence->animation_bindings[evidence->animation_binding_count];
    item->name = name_copy;
    item->target_name = target_copy;
    item->format = format;
    item->has_length = has_length;
    item->length = length;
    item->controllers = controller_copy;
    item->controller_count = controller_count;
    item->morph_keys = morph_key_copy;
    item->morph_key_count = morph_key_count;
    evidence->animation_binding_count = next_count;
    NMO_RETURN_OK();
}

static bool project_report_created_contains(
    const nmo_project_report_diff_t *diff,
    const char *name)
{
    if (!diff || !name) {
        return false;
    }
    for (size_t i = 0u; i < diff->created.count; ++i) {
        if (diff->created.names[i] &&
            strcmp(diff->created.names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool project_report_get_object_by_handle(
    const nmo_project_plan_t *plan,
    uint32_t handle,
    nmo_project_object_desc_t *out_object)
{
    if (!plan || handle == 0u || !out_object) {
        return false;
    }
    for (size_t i = 0u; i < nmo_project_plan_object_count(plan); ++i) {
        nmo_project_object_desc_t object = {0};
        if (nmo_project_plan_get_object(plan, i, &object) != NMO_OK) {
            return false;
        }
        if (object.handle == handle) {
            *out_object = object;
            return true;
        }
    }
    return false;
}

static void project_report_set_generated_object_ids(
    nmo_project_report_t *report,
    const nmo_project_runtime_object_t *objects,
    size_t object_count)
{
    if (!report || !objects) {
        return;
    }
    for (size_t i = 0u; i < report->evidence.object_count; ++i) {
        for (size_t j = 0u; j < object_count; ++j) {
            if (report->evidence.objects[i].plan_handle == objects[j].plan_handle) {
                report->evidence.objects[i].object_id = objects[j].object_id;
                break;
            }
        }
    }
}

static nmo_status_t project_report_reset_for_execute(
    nmo_project_report_t *report)
{
    if (!report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "project report is required");
    }

    report->ok = false;
    report->dry_run = false;
    project_report_dispose_diffs(report);
    project_report_evidence_dispose(&report->evidence);
    nmo_project_validation_report_dispose(&report->validation);
    nmo_project_validation_report_init(&report->validation);
    free(report->output_path);
    report->output_path = NULL;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_populate_diff(
    nmo_project_report_t *report,
    const nmo_project_plan_t *plan)
{
    const char *document_name = nmo_project_plan_document_name(plan);
    if (document_name) {
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->document_diff,
            document_name));
    }

    for (size_t i = 0u; i < nmo_project_plan_scene_count(plan); ++i) {
        nmo_project_scene_desc_t scene = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_scene(plan, i, &scene));
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->scene_diff,
            scene.name));
    }

    for (size_t i = 0u; i < nmo_project_plan_object_count(plan); ++i) {
        nmo_project_object_desc_t object = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, i, &object));
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->object_diff,
            object.name));
        NMO_RETURN_IF_ERROR(project_report_add_object_evidence(
            &report->evidence,
            object.handle,
            0u,
            object.class_id,
            object.name));
        if (object.has_sound) {
            nmo_project_object_desc_t attached = {0};
            const char *attached_name = NULL;
            if (object.has_sound_attached_object &&
                project_report_get_object_by_handle(
                    plan,
                    object.sound_attached_object_handle,
                    &attached)) {
                attached_name = attached.name;
            }
            NMO_RETURN_IF_ERROR(project_report_add_sound_binding_evidence(
                &report->evidence,
                object.name,
                object.sound_file_path,
                attached_name));
        }
        if (object.has_animation) {
            nmo_project_object_desc_t target = {0};
            if (!project_report_get_object_by_handle(
                    plan,
                    object.animation_target_handle,
                    &target)) {
                NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                 "animation target object handle not found");
            }
            NMO_RETURN_IF_ERROR(project_report_add_animation_binding_evidence(
                &report->evidence,
                object.name,
                target.name,
                (uint32_t)object.animation_format,
                object.has_animation_length,
                object.animation_length,
                object.animation_controllers,
                object.animation_controller_count,
                object.animation_morph_keys,
                object.animation_morph_key_count));
        }
    }

    for (size_t i = 0u; i < nmo_project_plan_asset_count(plan); ++i) {
        nmo_project_asset_desc_t asset = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_asset(plan, i, &asset));

        nmo_project_object_desc_t object = {0};
        bool found = false;
        for (size_t j = 0u; j < nmo_project_plan_object_count(plan); ++j) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, j, &object));
            if (object.handle == asset.object_handle) {
                found = true;
                break;
            }
        }
        if (!found || !object.name) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "asset target object handle not found");
        }

        char asset_name[256];
        if (asset.has_primitive_mesh || asset.has_external_mesh) {
            NMO_RETURN_IF_ERROR(project_report_format_asset_name(
                object.name,
                "Mesh",
                asset_name,
                sizeof(asset_name)));
            NMO_RETURN_IF_ERROR(project_report_add_created(
                &report->asset_diff,
                asset_name));
            NMO_RETURN_IF_ERROR(project_report_add_asset_binding_evidence(
                &report->evidence,
                object.name,
                asset_name,
                asset.has_external_mesh ? "external_mesh" : "primitive_mesh"));
        }
        if (asset.has_material_color ||
            asset.has_material_diffuse ||
            asset.has_material_ambient ||
            asset.has_material_specular ||
            asset.has_material_emissive ||
            asset.has_material_specular_power ||
            asset.has_material_render_flags ||
            asset.has_material_texture) {
            NMO_RETURN_IF_ERROR(project_report_format_asset_name(
                object.name,
                "Material",
                asset_name,
                sizeof(asset_name)));
            NMO_RETURN_IF_ERROR(project_report_add_created(
                &report->asset_diff,
                asset_name));
            NMO_RETURN_IF_ERROR(project_report_add_asset_binding_evidence(
                &report->evidence,
                object.name,
                asset_name,
                "material"));
            NMO_RETURN_IF_ERROR(project_report_add_material_channel_evidence(
                &report->evidence,
                asset_name,
                asset.has_material_color || asset.has_material_diffuse,
                asset.has_material_ambient,
                asset.has_material_specular,
                asset.has_material_emissive,
                asset.has_material_specular_power));
        }
        if (asset.has_material_texture) {
            char material_name[256];
            NMO_RETURN_IF_ERROR(project_report_format_asset_name(
                object.name,
                "Material",
                material_name,
                sizeof(material_name)));
            for (uint32_t slot = 0u; slot < 4u; ++slot) {
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
                if (!texture_path) {
                    continue;
                }
                NMO_RETURN_IF_ERROR(project_report_format_texture_name(
                    object.name,
                    slot,
                    asset_name,
                    sizeof(asset_name)));
                NMO_RETURN_IF_ERROR(project_report_add_created(
                    &report->asset_diff,
                    asset_name));
                NMO_RETURN_IF_ERROR(project_report_add_asset_binding_evidence(
                    &report->evidence,
                    object.name,
                    asset_name,
                    "texture"));
                NMO_RETURN_IF_ERROR(project_report_add_material_texture_slot_evidence(
                    &report->evidence,
                    material_name,
                    slot,
                    asset_name,
                    source_path));
            }
        }
        size_t obj_material_count =
            nmo_project_plan_obj_material_count(plan, asset.object_handle);
        for (size_t material_index = 0u;
             material_index < obj_material_count;
             ++material_index) {
            nmo_project_material_spec_t material = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_obj_material(
                plan,
                asset.object_handle,
                material_index,
                &material));
            NMO_RETURN_IF_ERROR(project_report_format_obj_material_name(
                object.name,
                material.obj_material_name,
                "Material",
                asset_name,
                sizeof(asset_name)));
            NMO_RETURN_IF_ERROR(project_report_add_created(
                &report->asset_diff,
                asset_name));
            NMO_RETURN_IF_ERROR(project_report_add_asset_binding_evidence(
                &report->evidence,
                object.name,
                asset_name,
                "obj_material"));
            NMO_RETURN_IF_ERROR(project_report_add_material_channel_evidence(
                &report->evidence,
                asset_name,
                material.has_color || material.has_diffuse,
                material.has_ambient,
                material.has_specular,
                material.has_emissive,
                material.has_specular_power));

            bool material_has_texture = material.has_texture;
            for (uint32_t slot = 0u; slot < 4u; ++slot) {
                material_has_texture =
                    material_has_texture || material.has_texture_slots[slot];
            }
            if (material_has_texture) {
                char material_name[256];
                char texture_base[256];
                NMO_RETURN_IF_ERROR(project_report_format_obj_material_name(
                    object.name,
                    material.obj_material_name,
                    "Material",
                    material_name,
                    sizeof(material_name)));
                int len = snprintf(texture_base,
                                   sizeof(texture_base),
                                   "%s_%s",
                                   object.name,
                                   material.obj_material_name);
                if (len < 0 || (size_t)len >= sizeof(texture_base)) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT,
                                     NMO_SEVERITY_ERROR,
                                     "generated OBJ texture evidence base name is too long");
                }
                for (uint32_t slot = 0u; slot < 4u; ++slot) {
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
                    if (!texture_path) {
                        continue;
                    }
                    NMO_RETURN_IF_ERROR(project_report_format_texture_name(
                        texture_base,
                        slot,
                        asset_name,
                        sizeof(asset_name)));
                    NMO_RETURN_IF_ERROR(project_report_add_created(
                        &report->asset_diff,
                        asset_name));
                    NMO_RETURN_IF_ERROR(project_report_add_asset_binding_evidence(
                        &report->evidence,
                        object.name,
                        asset_name,
                        "obj_material_texture"));
                    NMO_RETURN_IF_ERROR(project_report_add_material_texture_slot_evidence(
                        &report->evidence,
                        material_name,
                        slot,
                        asset_name,
                        source_path));
                }
            }
        }
    }

    for (size_t i = 0u; i < nmo_project_plan_script_count(plan); ++i) {
        nmo_project_script_desc_t script = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_script(plan, i, &script));
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->script_diff,
            script.name));
        NMO_RETURN_IF_ERROR(project_report_add_script_evidence(
            &report->evidence,
            script.name,
            script.step_count,
            report->validation.ok));
    }

    NMO_RETURN_OK();
}

static nmo_status_t project_report_validate_and_populate(
    const nmo_project_plan_t *plan,
    nmo_project_report_t *report)
{
    nmo_status_t status = nmo_project_validate_plan(plan, &report->validation);
    if (status != NMO_OK) {
        return status;
    }
    if (!report->validation.ok) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    return project_report_populate_diff(report, plan);
}

void nmo_project_report_init(nmo_project_report_t *report)
{
    if (!report) {
        return;
    }

    memset(report, 0, sizeof(*report));
    nmo_project_validation_report_init(&report->validation);
}

void nmo_project_report_dispose(nmo_project_report_t *report)
{
    if (!report) {
        return;
    }

    project_report_dispose_diffs(report);
    project_report_evidence_dispose(&report->evidence);
    nmo_project_validation_report_dispose(&report->validation);
    free(report->output_path);
    memset(report, 0, sizeof(*report));
}

bool nmo_project_report_diff_has_created_scene(
    const nmo_project_report_t *report,
    const char *name)
{
    return report && project_report_created_contains(&report->scene_diff, name);
}

bool nmo_project_report_diff_has_created_object(
    const nmo_project_report_t *report,
    const char *name)
{
    return report && project_report_created_contains(&report->object_diff, name);
}

bool nmo_project_report_diff_has_created_asset(
    const nmo_project_report_t *report,
    const char *name)
{
    return report && project_report_created_contains(&report->asset_diff, name);
}

nmo_status_t nmo_project_executor_execute_dry_run(
    const nmo_project_plan_t *plan,
    nmo_project_report_t *report)
{
    if (!plan || !report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and report are required");
    }

    NMO_RETURN_IF_ERROR(project_report_reset_for_execute(report));
    report->dry_run = true;
    nmo_status_t status = project_report_validate_and_populate(plan, report);
    if (status != NMO_OK) {
        return status;
    }
    report->ok = true;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_executor_execute_to_file(
    const nmo_project_plan_t *plan,
    const char *output_path,
    nmo_project_report_t *report)
{
    if (!plan || !output_path || !report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan, output_path, and report are required");
    }

    NMO_RETURN_IF_ERROR(project_report_reset_for_execute(report));
    report->output_path = project_executor_strdup(output_path);
    if (!report->output_path) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to copy project output path");
    }

    nmo_status_t status = project_report_validate_and_populate(plan, report);
    if (status != NMO_OK) {
        return status;
    }

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to create project execution context");
    }

    nmo_document_t *document = nmo_document_create(ctx);
    if (!document) {
        nmo_context_release(ctx);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to create generated document");
    }
    status = nmo_document_set_file_info(
        document,
        &(nmo_file_info_t){
            .file_version = 9u,
        });
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    nmo_workspace_t *workspace = NULL;
    status = nmo_workspace_create(ctx, document, &workspace);
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    nmo_workspace_edit_t *edit = NULL;
    status = nmo_workspace_edit_begin(workspace, "project generation", &edit);
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    nmo_object_id_t root_id = 0;
    nmo_object_create_desc_t root = {
        .class_id = NMO_CID_OBJECT,
        .name = nmo_project_plan_document_name(plan),
        .type_guid = NMO_GUID_NULL,
    };
    status = nmo_object_edit_create(
        edit,
        &root,
        &root_id);
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    nmo_project_runtime_object_t *objects = NULL;
    size_t object_count = 0u;
    status = nmo_project_author_scenes(edit, plan, &objects, &object_count);
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }
    project_report_set_generated_object_ids(report, objects, object_count);

    status = nmo_project_author_assets(edit, plan, objects, object_count);
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        free(objects);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_workspace_edit_commit(edit);
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        free(objects);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_project_author_scripts(workspace, plan, objects, object_count);
    free(objects);
    objects = NULL;
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    size_t output_len = strlen(output_path);
    char *temp_path = (char *)malloc(output_len + 5u);
    if (!temp_path) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project temp output path");
    }
    memcpy(temp_path, output_path, output_len);
    memcpy(temp_path + output_len, ".tmp", 5u);

    remove(temp_path);
    status = nmo_document_save_file(document, temp_path, NULL);
    if (status == NMO_OK) {
        nmo_document_t *loaded = NULL;
        status = nmo_document_load_file(ctx, temp_path, NULL, &loaded);
        nmo_document_destroy(loaded);
    }
    report->evidence.post_load_checked = true;
    report->evidence.post_load_ok = status == NMO_OK;
    report->evidence.post_save_validate_checked = true;
    report->evidence.post_save_validate_ok = status == NMO_OK;
    if (status == NMO_OK) {
        remove(output_path);
        if (rename(temp_path, output_path) != 0) {
            status = NMO_ERR_CANT_WRITE_FILE;
            NMO_SET_LAST_ERROR(status, NMO_SEVERITY_ERROR,
                               "failed to publish generated project output: %s",
                               output_path);
        }
    }
    if (status != NMO_OK) {
        remove(temp_path);
    }
    free(temp_path);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    if (status != NMO_OK) {
        return status;
    }

    report->ok = true;
    NMO_RETURN_OK();
}
