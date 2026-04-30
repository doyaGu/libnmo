#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"

#include "object/nmo_class_ids.h"

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
    char *name;
    uint32_t flags;
} project_object_record_t;

struct nmo_project_plan {
    char *document_name;
    project_scene_record_t *scenes;
    size_t scene_count;
    size_t scene_capacity;
    project_object_record_t *objects;
    size_t object_count;
    size_t object_capacity;
    uint32_t next_scene_handle;
    uint32_t next_object_handle;
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
    }
    free(plan->scenes);
    free(plan->objects);
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
            clone->objects[i].name = project_plan_strdup(plan->objects[i].name);
            if (plan->objects[i].name && !clone->objects[i].name) {
                nmo_project_plan_destroy(clone);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "failed to clone project object name");
            }
            clone->object_count++;
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
    out_object->name = plan->objects[index].name;
    out_object->flags = plan->objects[index].flags;
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

static nmo_status_t project_plan_add_object(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    nmo_class_id_t class_id,
    const char *name,
    uint32_t *out_object_handle)
{
    if (!plan || scene_handle == 0u || !name || name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan, scene handle, and non-empty object name are required");
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

    char *name_copy = project_plan_strdup(name);
    if (!name_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project object name");
    }

    uint32_t handle = plan->next_object_handle++;
    project_object_record_t *object = &plan->objects[plan->object_count++];
    object->handle = handle;
    object->scene_handle = scene_handle;
    object->parent_handle = 0u;
    object->class_id = class_id;
    object->name = name_copy;
    object->flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE;

    if (out_object_handle) {
        *out_object_handle = handle;
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_plan_add_camera(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    const char *name,
    uint32_t *out_object_handle)
{
    return project_plan_add_object(
        plan,
        scene_handle,
        NMO_CID_CAMERA,
        name,
        out_object_handle);
}

nmo_status_t nmo_project_plan_add_light(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    const char *name,
    uint32_t *out_object_handle)
{
    return project_plan_add_object(
        plan,
        scene_handle,
        NMO_CID_LIGHT,
        name,
        out_object_handle);
}

nmo_status_t nmo_project_plan_add_3d_entity(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    const char *name,
    uint32_t *out_object_handle)
{
    return project_plan_add_object(
        plan,
        scene_handle,
        NMO_CID_3DENTITY,
        name,
        out_object_handle);
}
