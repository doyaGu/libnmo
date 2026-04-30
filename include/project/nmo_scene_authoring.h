#ifndef NMO_SCENE_AUTHORING_H
#define NMO_SCENE_AUTHORING_H

#include "project/nmo_project_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_project_plan_add_scene(
    nmo_project_plan_t *plan,
    const char *name,
    uint32_t *out_scene_handle);

NMO_API const char *nmo_project_plan_scene_name(
    const nmo_project_plan_t *plan,
    uint32_t scene_handle);

NMO_API nmo_status_t nmo_project_plan_add_camera(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    const char *name,
    uint32_t *out_object_handle);

NMO_API nmo_status_t nmo_project_plan_add_light(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    const char *name,
    uint32_t *out_object_handle);

NMO_API nmo_status_t nmo_project_plan_add_3d_entity(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    const char *name,
    uint32_t *out_object_handle);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCENE_AUTHORING_H */
