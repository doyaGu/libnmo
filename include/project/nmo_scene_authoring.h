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

NMO_API nmo_status_t nmo_project_plan_set_scene_startup_active(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    bool startup_active);

NMO_API nmo_status_t nmo_project_plan_set_scene_active_camera(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    uint32_t camera_handle);

NMO_API nmo_status_t nmo_project_plan_set_scene_background_color(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    float r,
    float g,
    float b,
    float a);

NMO_API nmo_status_t nmo_project_plan_set_scene_ambient_light(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    float r,
    float g,
    float b,
    float a);

NMO_API nmo_status_t nmo_project_plan_set_scene_fog(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    VXFOG_MODE mode,
    float r,
    float g,
    float b,
    float a,
    float start,
    float end,
    float density);

NMO_API const char *nmo_project_plan_scene_name(
    const nmo_project_plan_t *plan,
    uint32_t scene_handle);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCENE_AUTHORING_H */
