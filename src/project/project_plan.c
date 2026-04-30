#include "project/nmo_project_plan.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"

#include <stdlib.h>
#include <string.h>

typedef struct project_scene_record {
    uint32_t handle;
    char *name;
} project_scene_record_t;

typedef struct project_object_record {
    uint32_t handle;
    uint32_t scene_handle;
    uint32_t parent_handle;
    nmo_class_id_t class_id;
    nmo_guid_t type_guid;
    char *name;
    uint32_t flags;
    nmo_session_field_edit_t *fields;
    size_t field_count;
} project_object_record_t;

typedef struct project_asset_record {
    uint32_t object_handle;
    bool has_primitive_mesh;
    nmo_primitive_mesh_t primitive_mesh;
    bool has_material_color;
    float material_color[4];
} project_asset_record_t;

typedef struct project_script_step_record {
    nmo_project_script_step_kind_t kind;
    char *message;
} project_script_step_record_t;

typedef struct project_script_record {
    uint32_t handle;
    uint32_t object_handle;
    char *name;
    project_script_step_record_t *steps;
    size_t step_count;
    size_t step_capacity;
} project_script_record_t;

struct nmo_project_plan {
    char *document_name;
    project_scene_record_t *scenes;
    size_t scene_count;
    size_t scene_capacity;
    project_object_record_t *objects;
    size_t object_count;
    size_t object_capacity;
    project_asset_record_t *assets;
    size_t asset_count;
    size_t asset_capacity;
    project_script_record_t *scripts;
    size_t script_count;
    size_t script_capacity;
    uint32_t next_scene_handle;
    uint32_t next_object_handle;
    uint32_t next_script_handle;
};

static char *project_plan_strdup(const char *src)
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

static void project_plan_free_fields(
    nmo_session_field_edit_t *fields,
    size_t field_count)
{
    if (!fields) {
        return;
    }

    for (size_t i = 0; i < field_count; ++i) {
        free((void *)fields[i].field_name);
        free((void *)fields[i].value_str);
    }
    free(fields);
}

static nmo_status_t project_plan_clone_fields(
    const nmo_session_field_edit_t *src_fields,
    size_t field_count,
    nmo_session_field_edit_t **out_fields)
{
    if (!out_fields) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "out_fields is required");
    }
    *out_fields = NULL;

    if (field_count == 0u) {
        NMO_RETURN_OK();
    }
    if (!src_fields) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "field array is required when field_count is non-zero");
    }

    nmo_session_field_edit_t *fields =
        (nmo_session_field_edit_t *)calloc(field_count, sizeof(*fields));
    if (!fields) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project object field edits");
    }

    for (size_t i = 0; i < field_count; ++i) {
        if (!src_fields[i].field_name || !src_fields[i].value_str) {
            project_plan_free_fields(fields, field_count);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "field edits require field_name and value_str");
        }

        fields[i].field_name = project_plan_strdup(src_fields[i].field_name);
        fields[i].value_str = project_plan_strdup(src_fields[i].value_str);
        if (!fields[i].field_name || !fields[i].value_str) {
            project_plan_free_fields(fields, field_count);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to clone project object field edit");
        }
    }

    *out_fields = fields;
    NMO_RETURN_OK();
}

static void project_plan_free_script_steps(
    project_script_step_record_t *steps,
    size_t step_count)
{
    if (!steps) {
        return;
    }

    for (size_t i = 0u; i < step_count; ++i) {
        free(steps[i].message);
    }
    free(steps);
}

nmo_status_t nmo_project_plan_create(nmo_project_plan_t **out_plan)
{
    if (!out_plan) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "out_plan is required");
    }

    nmo_project_plan_t *plan = (nmo_project_plan_t *)calloc(1, sizeof(*plan));
    if (!plan) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project plan");
    }

    plan->next_scene_handle = 1u;
    plan->next_object_handle = 1u;
    plan->next_script_handle = 1u;
    *out_plan = plan;
    NMO_RETURN_OK();
}

void nmo_project_plan_destroy(nmo_project_plan_t *plan)
{
    if (!plan) {
        return;
    }

    free(plan->document_name);
    for (size_t i = 0; i < plan->scene_count; ++i) {
        free(plan->scenes[i].name);
    }
    for (size_t i = 0; i < plan->object_count; ++i) {
        free(plan->objects[i].name);
        project_plan_free_fields(
            plan->objects[i].fields,
            plan->objects[i].field_count);
    }
    for (size_t i = 0; i < plan->script_count; ++i) {
        free(plan->scripts[i].name);
        project_plan_free_script_steps(
            plan->scripts[i].steps,
            plan->scripts[i].step_count);
    }
    free(plan->scenes);
    free(plan->objects);
    free(plan->assets);
    free(plan->scripts);
    free(plan);
}

nmo_status_t nmo_project_plan_clone(
    const nmo_project_plan_t *plan,
    nmo_project_plan_t **out_clone)
{
    if (!plan || !out_clone) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and out_clone are required");
    }

    nmo_project_plan_t *clone = NULL;
    nmo_status_t status = nmo_project_plan_create(&clone);
    if (status != NMO_OK) {
        return status;
    }

    if (plan->document_name) {
        clone->document_name = project_plan_strdup(plan->document_name);
        if (!clone->document_name) {
            nmo_project_plan_destroy(clone);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to clone document name");
        }
    }
    clone->next_scene_handle = plan->next_scene_handle;
    clone->next_object_handle = plan->next_object_handle;
    clone->next_script_handle = plan->next_script_handle;
    if (plan->scene_count > 0u) {
        clone->scenes = (project_scene_record_t *)calloc(
            plan->scene_count,
            sizeof(*clone->scenes));
        if (!clone->scenes) {
            nmo_project_plan_destroy(clone);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to clone project scenes");
        }

        clone->scene_capacity = plan->scene_count;
        for (size_t i = 0; i < plan->scene_count; ++i) {
            clone->scenes[i].handle = plan->scenes[i].handle;
            clone->scenes[i].name = project_plan_strdup(plan->scenes[i].name);
            if (plan->scenes[i].name && !clone->scenes[i].name) {
                nmo_project_plan_destroy(clone);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "failed to clone project scene name");
            }
            clone->scene_count++;
        }
    }
    if (plan->object_count > 0u) {
        clone->objects = (project_object_record_t *)calloc(
            plan->object_count,
            sizeof(*clone->objects));
        if (!clone->objects) {
            nmo_project_plan_destroy(clone);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to clone project objects");
        }

        clone->object_capacity = plan->object_count;
        for (size_t i = 0; i < plan->object_count; ++i) {
            clone->objects[i] = plan->objects[i];
            clone->objects[i].fields = NULL;
            clone->objects[i].field_count = 0u;
            clone->objects[i].name = project_plan_strdup(plan->objects[i].name);
            if (plan->objects[i].name && !clone->objects[i].name) {
                nmo_project_plan_destroy(clone);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "failed to clone project object name");
            }
            status = project_plan_clone_fields(
                plan->objects[i].fields,
                plan->objects[i].field_count,
                &clone->objects[i].fields);
            if (status != NMO_OK) {
                nmo_project_plan_destroy(clone);
                return status;
            }
            clone->objects[i].field_count = plan->objects[i].field_count;
            clone->object_count++;
        }
    }
    if (plan->asset_count > 0u) {
        clone->assets = (project_asset_record_t *)calloc(
            plan->asset_count,
            sizeof(*clone->assets));
        if (!clone->assets) {
            nmo_project_plan_destroy(clone);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to clone project assets");
        }
        memcpy(
            clone->assets,
            plan->assets,
            plan->asset_count * sizeof(*clone->assets));
        clone->asset_count = plan->asset_count;
        clone->asset_capacity = plan->asset_count;
    }
    if (plan->script_count > 0u) {
        clone->scripts = (project_script_record_t *)calloc(
            plan->script_count,
            sizeof(*clone->scripts));
        if (!clone->scripts) {
            nmo_project_plan_destroy(clone);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to clone project scripts");
        }
        clone->script_capacity = plan->script_count;
        for (size_t i = 0u; i < plan->script_count; ++i) {
            const project_script_record_t *src = &plan->scripts[i];
            project_script_record_t *dst = &clone->scripts[i];
            dst->handle = src->handle;
            dst->object_handle = src->object_handle;
            dst->name = project_plan_strdup(src->name);
            if (src->name && !dst->name) {
                nmo_project_plan_destroy(clone);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "failed to clone project script name");
            }
            if (src->step_count > 0u) {
                dst->steps = (project_script_step_record_t *)calloc(
                    src->step_count,
                    sizeof(*dst->steps));
                if (!dst->steps) {
                    nmo_project_plan_destroy(clone);
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "failed to clone project script steps");
                }
                dst->step_capacity = src->step_count;
                for (size_t j = 0u; j < src->step_count; ++j) {
                    dst->steps[j].kind = src->steps[j].kind;
                    dst->steps[j].message =
                        project_plan_strdup(src->steps[j].message);
                    if (src->steps[j].message && !dst->steps[j].message) {
                        nmo_project_plan_destroy(clone);
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                         "failed to clone project script step");
                    }
                    dst->step_count++;
                }
            }
            clone->script_count++;
        }
    }

    *out_clone = clone;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_set_document_name(
    nmo_project_plan_t *plan,
    const char *name)
{
    if (!plan) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan is required");
    }

    char *copy = project_plan_strdup(name);
    if (name && !copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate document name");
    }

    free(plan->document_name);
    plan->document_name = copy;
    NMO_RETURN_OK();
}

const char *nmo_project_plan_document_name(const nmo_project_plan_t *plan)
{
    return plan ? plan->document_name : NULL;
}

size_t nmo_project_plan_scene_count(const nmo_project_plan_t *plan)
{
    return plan ? plan->scene_count : 0u;
}

nmo_status_t nmo_project_plan_get_scene(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_scene_desc_t *out_scene)
{
    if (!plan || !out_scene) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and out_scene are required");
    }
    if (index >= plan->scene_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "scene index out of bounds");
    }

    out_scene->handle = plan->scenes[index].handle;
    out_scene->name = plan->scenes[index].name;
    NMO_RETURN_OK();
}

size_t nmo_project_plan_object_count(const nmo_project_plan_t *plan)
{
    return plan ? plan->object_count : 0u;
}

nmo_status_t nmo_project_plan_get_object(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_object_desc_t *out_object)
{
    if (!plan || !out_object) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and out_object are required");
    }
    if (index >= plan->object_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "object index out of bounds");
    }

    out_object->handle = plan->objects[index].handle;
    out_object->scene_handle = plan->objects[index].scene_handle;
    out_object->parent_handle = plan->objects[index].parent_handle;
    out_object->class_id = plan->objects[index].class_id;
    out_object->type_guid = plan->objects[index].type_guid;
    out_object->name = plan->objects[index].name;
    out_object->flags = plan->objects[index].flags;
    out_object->fields = plan->objects[index].fields;
    out_object->field_count = plan->objects[index].field_count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_add_scene(
    nmo_project_plan_t *plan,
    const char *name,
    uint32_t *out_scene_handle)
{
    if (!plan || !name || name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and non-empty scene name are required");
    }

    if (plan->scene_count == plan->scene_capacity) {
        size_t new_capacity = plan->scene_capacity ? plan->scene_capacity * 2u : 4u;
        project_scene_record_t *new_scenes =
            (project_scene_record_t *)realloc(
                plan->scenes,
                new_capacity * sizeof(*new_scenes));
        if (!new_scenes) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate project scene");
        }
        memset(new_scenes + plan->scene_capacity,
               0,
               (new_capacity - plan->scene_capacity) * sizeof(*new_scenes));
        plan->scenes = new_scenes;
        plan->scene_capacity = new_capacity;
    }

    char *name_copy = project_plan_strdup(name);
    if (!name_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project scene name");
    }

    uint32_t handle = plan->next_scene_handle++;
    project_scene_record_t *scene = &plan->scenes[plan->scene_count++];
    scene->handle = handle;
    scene->name = name_copy;

    if (out_scene_handle) {
        *out_scene_handle = handle;
    }
    NMO_RETURN_OK();
}

static bool project_plan_has_object_handle(
    const nmo_project_plan_t *plan,
    uint32_t object_handle)
{
    if (!plan || object_handle == 0u) {
        return false;
    }
    for (size_t i = 0; i < plan->object_count; ++i) {
        if (plan->objects[i].handle == object_handle) {
            return true;
        }
    }
    return false;
}

static project_script_record_t *project_plan_find_script(
    nmo_project_plan_t *plan,
    uint32_t script_handle)
{
    if (!plan || script_handle == 0u) {
        return NULL;
    }

    for (size_t i = 0u; i < plan->script_count; ++i) {
        if (plan->scripts[i].handle == script_handle) {
            return &plan->scripts[i];
        }
    }
    return NULL;
}

static const project_script_record_t *project_plan_find_script_const(
    const nmo_project_plan_t *plan,
    uint32_t script_handle)
{
    if (!plan || script_handle == 0u) {
        return NULL;
    }

    for (size_t i = 0u; i < plan->script_count; ++i) {
        if (plan->scripts[i].handle == script_handle) {
            return &plan->scripts[i];
        }
    }
    return NULL;
}

static nmo_status_t project_plan_find_or_add_asset(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    project_asset_record_t **out_asset)
{
    if (!plan || object_handle == 0u || !out_asset) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan, object handle, and out_asset are required");
    }
    *out_asset = NULL;
    if (!project_plan_has_object_handle(plan, object_handle)) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "asset target object handle not found");
    }

    for (size_t i = 0; i < plan->asset_count; ++i) {
        if (plan->assets[i].object_handle == object_handle) {
            *out_asset = &plan->assets[i];
            NMO_RETURN_OK();
        }
    }

    if (plan->asset_count == plan->asset_capacity) {
        size_t new_capacity = plan->asset_capacity ? plan->asset_capacity * 2u : 4u;
        project_asset_record_t *new_assets =
            (project_asset_record_t *)realloc(
                plan->assets,
                new_capacity * sizeof(*new_assets));
        if (!new_assets) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate project asset");
        }
        memset(new_assets + plan->asset_capacity,
               0,
               (new_capacity - plan->asset_capacity) * sizeof(*new_assets));
        plan->assets = new_assets;
        plan->asset_capacity = new_capacity;
    }

    project_asset_record_t *asset = &plan->assets[plan->asset_count++];
    memset(asset, 0, sizeof(*asset));
    asset->object_handle = object_handle;
    *out_asset = asset;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_add_object_script(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const char *name,
    uint32_t *out_script_handle)
{
    if (!plan || object_handle == 0u || !name || name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan, object handle, and non-empty script name are required");
    }
    if (!project_plan_has_object_handle(plan, object_handle)) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "script target object handle not found");
    }

    if (plan->script_count == plan->script_capacity) {
        size_t new_capacity = plan->script_capacity ? plan->script_capacity * 2u : 4u;
        project_script_record_t *new_scripts =
            (project_script_record_t *)realloc(
                plan->scripts,
                new_capacity * sizeof(*new_scripts));
        if (!new_scripts) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate project script");
        }
        memset(new_scripts + plan->script_capacity,
               0,
               (new_capacity - plan->script_capacity) * sizeof(*new_scripts));
        plan->scripts = new_scripts;
        plan->script_capacity = new_capacity;
    }

    char *name_copy = project_plan_strdup(name);
    if (!name_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project script name");
    }

    uint32_t handle = plan->next_script_handle++;
    project_script_record_t *script = &plan->scripts[plan->script_count++];
    script->handle = handle;
    script->object_handle = object_handle;
    script->name = name_copy;

    if (out_script_handle) {
        *out_script_handle = handle;
    }
    NMO_RETURN_OK();
}

size_t nmo_project_plan_script_count(const nmo_project_plan_t *plan)
{
    return plan ? plan->script_count : 0u;
}

nmo_status_t nmo_project_plan_get_script(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_script_desc_t *out_script)
{
    if (!plan || !out_script) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and out_script are required");
    }
    if (index >= plan->script_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "script index out of bounds");
    }

    const project_script_record_t *script = &plan->scripts[index];
    out_script->handle = script->handle;
    out_script->object_handle = script->object_handle;
    out_script->name = script->name;
    out_script->step_count = script->step_count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_get_script_step(
    const nmo_project_plan_t *plan,
    uint32_t script_handle,
    size_t index,
    nmo_project_script_step_desc_t *out_step)
{
    if (!plan || !out_step) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and out_step are required");
    }

    const project_script_record_t *script =
        project_plan_find_script_const(plan, script_handle);
    if (!script) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "script handle not found");
    }
    if (index >= script->step_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "script step index out of bounds");
    }

    out_step->kind = script->steps[index].kind;
    out_step->message = script->steps[index].message;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_script_add_debug_output(
    nmo_project_plan_t *plan,
    uint32_t script_handle,
    const char *message)
{
    if (!plan || script_handle == 0u || !message) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan, script handle, and message are required");
    }

    project_script_record_t *script =
        project_plan_find_script(plan, script_handle);
    if (!script) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "script handle not found");
    }

    if (script->step_count == script->step_capacity) {
        size_t new_capacity = script->step_capacity ? script->step_capacity * 2u : 4u;
        project_script_step_record_t *new_steps =
            (project_script_step_record_t *)realloc(
                script->steps,
                new_capacity * sizeof(*new_steps));
        if (!new_steps) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate project script step");
        }
        memset(new_steps + script->step_capacity,
               0,
               (new_capacity - script->step_capacity) * sizeof(*new_steps));
        script->steps = new_steps;
        script->step_capacity = new_capacity;
    }

    char *message_copy = project_plan_strdup(message);
    if (!message_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project script message");
    }

    project_script_step_record_t *step = &script->steps[script->step_count++];
    step->kind = NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT;
    step->message = message_copy;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_set_primitive_mesh(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    nmo_primitive_mesh_t primitive)
{
    if (primitive != NMO_PRIMITIVE_CUBE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "unsupported primitive mesh");
    }

    project_asset_record_t *asset = NULL;
    NMO_RETURN_IF_ERROR(project_plan_find_or_add_asset(plan, object_handle, &asset));
    asset->has_primitive_mesh = true;
    asset->primitive_mesh = primitive;
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_set_material_color(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    float r,
    float g,
    float b,
    float a)
{
    project_asset_record_t *asset = NULL;
    NMO_RETURN_IF_ERROR(project_plan_find_or_add_asset(plan, object_handle, &asset));
    asset->has_material_color = true;
    asset->material_color[0] = r;
    asset->material_color[1] = g;
    asset->material_color[2] = b;
    asset->material_color[3] = a;
    NMO_RETURN_OK();
}

size_t nmo_project_plan_asset_count(const nmo_project_plan_t *plan)
{
    return plan ? plan->asset_count : 0u;
}

nmo_status_t nmo_project_plan_get_asset(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_asset_desc_t *out_asset)
{
    if (!plan || !out_asset) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and out_asset are required");
    }
    if (index >= plan->asset_count) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR,
                         "asset index out of bounds");
    }

    const project_asset_record_t *asset = &plan->assets[index];
    out_asset->object_handle = asset->object_handle;
    out_asset->has_primitive_mesh = asset->has_primitive_mesh;
    out_asset->primitive_mesh = asset->primitive_mesh;
    out_asset->has_material_color = asset->has_material_color;
    memcpy(out_asset->material_color, asset->material_color, sizeof(out_asset->material_color));
    NMO_RETURN_OK();
}

const char *nmo_project_plan_scene_name(
    const nmo_project_plan_t *plan,
    uint32_t scene_handle)
{
    if (!plan || scene_handle == 0u) {
        return NULL;
    }

    for (size_t i = 0; i < plan->scene_count; ++i) {
        if (plan->scenes[i].handle == scene_handle) {
            return plan->scenes[i].name;
        }
    }

    return NULL;
}

nmo_status_t nmo_project_plan_add_object(
    nmo_project_plan_t *plan,
    const nmo_project_object_spec_t *spec,
    uint32_t *out_object_handle)
{
    if (!plan || !spec || !spec->name || spec->name[0] == '\0' ||
        spec->class_id == 0u || spec->class_id == NMO_CLASS_ID_INVALID) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan, object class, and non-empty object name are required");
    }

    if (plan->object_count == plan->object_capacity) {
        size_t new_capacity = plan->object_capacity ? plan->object_capacity * 2u : 4u;
        project_object_record_t *new_objects =
            (project_object_record_t *)realloc(
                plan->objects,
                new_capacity * sizeof(*new_objects));
        if (!new_objects) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate project object");
        }
        memset(new_objects + plan->object_capacity,
               0,
               (new_capacity - plan->object_capacity) * sizeof(*new_objects));
        plan->objects = new_objects;
        plan->object_capacity = new_capacity;
    }

    char *name_copy = project_plan_strdup(spec->name);
    if (!name_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project object name");
    }

    nmo_session_field_edit_t *fields_copy = NULL;
    nmo_status_t status = project_plan_clone_fields(
        spec->fields,
        spec->field_count,
        &fields_copy);
    if (status != NMO_OK) {
        free(name_copy);
        return status;
    }

    uint32_t handle = plan->next_object_handle++;
    project_object_record_t *object = &plan->objects[plan->object_count++];
    object->handle = handle;
    object->scene_handle = spec->scene_handle;
    object->parent_handle = spec->parent_handle;
    object->class_id = spec->class_id;
    object->type_guid = spec->type_guid;
    object->name = name_copy;
    object->flags = spec->flags;
    object->fields = fields_copy;
    object->field_count = spec->field_count;

    if (out_object_handle) {
        *out_object_handle = handle;
    }
    NMO_RETURN_OK();
}
