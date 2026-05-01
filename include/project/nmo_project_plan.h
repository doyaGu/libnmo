#ifndef NMO_PROJECT_PLAN_H
#define NMO_PROJECT_PLAN_H

#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_enum_defs.h"
#include "nmo_types.h"

#include <stdbool.h>
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
    bool startup_active;
    uint32_t active_camera_handle;
} nmo_project_scene_desc_t;

typedef enum nmo_project_object_flags {
    NMO_PROJECT_OBJECT_FLAG_ACTIVE = 1u << 0
} nmo_project_object_flags_t;

typedef struct nmo_project_object_spec {
    uint32_t scene_handle;
    uint32_t parent_handle;
    nmo_class_id_t class_id;
    nmo_guid_t type_guid;
    const char *name;
    uint32_t flags;
    const nmo_session_field_edit_t *fields;
    size_t field_count;
    bool has_position;
    float position[3];
    bool has_rotation_euler_deg;
    float rotation_euler_deg[3];
    bool has_scale;
    float scale[3];
    bool has_camera;
    float camera_fov;
    float camera_near;
    float camera_far;
    bool has_light;
    float light_diffuse[4];
    float light_range;
    VXLIGHT_TYPE light_type;
} nmo_project_object_spec_t;

typedef struct nmo_project_object_desc {
    uint32_t handle;
    uint32_t scene_handle;
    uint32_t parent_handle;
    nmo_class_id_t class_id;
    nmo_guid_t type_guid;
    const char *name;
    const char *source_path;
    uint32_t flags;
    const nmo_session_field_edit_t *fields;
    size_t field_count;
    bool has_position;
    float position[3];
    bool has_rotation_euler_deg;
    float rotation_euler_deg[3];
    bool has_scale;
    float scale[3];
    bool has_camera;
    float camera_fov;
    float camera_near;
    float camera_far;
    bool has_light;
    float light_diffuse[4];
    float light_range;
    VXLIGHT_TYPE light_type;
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

NMO_API nmo_status_t nmo_project_plan_add_object(
    nmo_project_plan_t *plan,
    const nmo_project_object_spec_t *spec,
    uint32_t *out_object_handle);

NMO_API nmo_status_t nmo_project_plan_set_object_parent(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    uint32_t parent_handle);

NMO_API nmo_status_t nmo_project_plan_set_object_source_path(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const char *source_path);

NMO_API nmo_status_t nmo_project_plan_set_object_position(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    float x,
    float y,
    float z);

NMO_API nmo_status_t nmo_project_plan_set_object_rotation_euler_deg(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    float x,
    float y,
    float z);

NMO_API nmo_status_t nmo_project_plan_set_object_scale(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    float x,
    float y,
    float z);

NMO_API nmo_status_t nmo_project_plan_set_camera_settings(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    float fov,
    float near_plane,
    float far_plane);

NMO_API nmo_status_t nmo_project_plan_set_light_settings(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    float diffuse_r,
    float diffuse_g,
    float diffuse_b,
    float diffuse_a,
    float range,
    VXLIGHT_TYPE type);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PROJECT_PLAN_H */
