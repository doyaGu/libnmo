#include "project/nmo_project_plan.h"

#include <stdlib.h>
#include <string.h>

struct nmo_project_plan {
    char *document_name;
    size_t scene_count;
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

    *out_plan = plan;
    NMO_RETURN_OK();
}

void nmo_project_plan_destroy(nmo_project_plan_t *plan)
{
    if (!plan) {
        return;
    }

    free(plan->document_name);
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
    clone->scene_count = plan->scene_count;

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
