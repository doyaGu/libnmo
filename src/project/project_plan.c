#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"

#include <stdlib.h>
#include <string.h>

typedef struct project_scene_record {
    uint32_t handle;
    char *name;
} project_scene_record_t;

struct nmo_project_plan {
    char *document_name;
    project_scene_record_t *scenes;
    size_t scene_count;
    size_t scene_capacity;
    uint32_t next_scene_handle;
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
    free(plan->scenes);
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
