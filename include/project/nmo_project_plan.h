#ifndef NMO_PROJECT_PLAN_H
#define NMO_PROJECT_PLAN_H

#include "core/nmo_error.h"
#include "nmo_types.h"

#include <stddef.h>

#define NMO_PROJECT_PLAN_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_PROJECT_PLAN_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_project_plan nmo_project_plan_t;

NMO_API nmo_status_t nmo_project_plan_create(nmo_project_plan_t **out_plan);
NMO_API void nmo_project_plan_destroy(nmo_project_plan_t *plan);

NMO_API nmo_status_t nmo_project_plan_clone(
    const nmo_project_plan_t *plan,
    nmo_project_plan_t **out_clone);

NMO_API nmo_status_t nmo_project_plan_set_document_name(
    nmo_project_plan_t *plan,
    const char *name);

NMO_API const char *nmo_project_plan_document_name(
    const nmo_project_plan_t *plan);

NMO_API size_t nmo_project_plan_scene_count(
    const nmo_project_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PROJECT_PLAN_H */
