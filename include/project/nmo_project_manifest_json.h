#ifndef NMO_PROJECT_MANIFEST_JSON_H
#define NMO_PROJECT_MANIFEST_JSON_H

#include "core/nmo_error.h"
#include "project/nmo_project_plan.h"

#include <stddef.h>

#define NMO_PROJECT_MANIFEST_JSON_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_PROJECT_MANIFEST_JSON_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_project_manifest {
    nmo_project_plan_t *plan;
    char *output_path;
} nmo_project_manifest_t;

NMO_API void nmo_project_manifest_init(nmo_project_manifest_t *manifest);
NMO_API void nmo_project_manifest_dispose(nmo_project_manifest_t *manifest);

NMO_API nmo_status_t nmo_project_manifest_json_read_manifest(
    const char *json,
    size_t json_size,
    nmo_project_manifest_t *out_manifest);

NMO_API nmo_status_t nmo_project_manifest_json_read(
    const char *json,
    size_t json_size,
    nmo_project_plan_t **out_plan);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PROJECT_MANIFEST_JSON_H */
