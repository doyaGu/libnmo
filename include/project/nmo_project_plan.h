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

typedef struct nmo_project_scene_desc {
    uint32_t handle;
    const char *name;
} nmo_project_scene_desc_t;

typedef enum nmo_project_object_flags {
    NMO_PROJECT_OBJECT_FLAG_ACTIVE = 1u << 0
} nmo_project_object_flags_t;

typedef struct nmo_project_object_desc {
    uint32_t handle;
    uint32_t scene_handle;
    uint32_t parent_handle;
    nmo_class_id_t class_id;
    const char *name;
    uint32_t flags;
} nmo_project_object_desc_t;

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

NMO_API nmo_status_t nmo_project_plan_get_scene(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_scene_desc_t *out_scene);

NMO_API size_t nmo_project_plan_object_count(
    const nmo_project_plan_t *plan);

NMO_API nmo_status_t nmo_project_plan_get_object(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_object_desc_t *out_object);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PROJECT_PLAN_H */
