#ifndef NMO_ASSET_PLAN_H
#define NMO_ASSET_PLAN_H

#include "core/nmo_error.h"
#include "object/nmo_primitive_mesh.h"
#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_project_plan nmo_project_plan_t;

typedef struct nmo_project_asset_desc {
    uint32_t object_handle;
    bool has_primitive_mesh;
    nmo_primitive_mesh_t primitive_mesh;
    bool has_external_mesh;
    const char *external_mesh_path;
    const char *external_mesh_source_path;
    bool has_material_color;
    float material_color[4];
    bool has_material_texture;
    const char *material_texture_path;
    const char *material_texture_source_path;
    bool has_material_texture_slots[4];
    const char *material_texture_paths[4];
    const char *material_texture_source_paths[4];
} nmo_project_asset_desc_t;

typedef struct nmo_project_material_spec {
    const char *obj_material_name;
    bool has_color;
    float color[4];
    bool has_texture;
    const char *texture_path;
    const char *source_path;
    const char *texture_source_path;
    bool has_texture_slots[4];
    const char *texture_paths[4];
    const char *texture_source_paths[4];
} nmo_project_material_spec_t;

NMO_API nmo_status_t nmo_project_plan_set_primitive_mesh(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    nmo_primitive_mesh_t primitive);

NMO_API nmo_status_t nmo_project_plan_set_material_color(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    float r,
    float g,
    float b,
    float a);

NMO_API nmo_status_t nmo_project_plan_set_external_mesh(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const char *path);

NMO_API nmo_status_t nmo_project_plan_set_external_mesh_source_path(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const char *source_path);

NMO_API nmo_status_t nmo_project_plan_set_material_texture(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const char *path);

NMO_API nmo_status_t nmo_project_plan_set_material_texture_slot(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    uint32_t slot,
    const char *path);

NMO_API nmo_status_t nmo_project_plan_set_material_texture_source_path(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const char *source_path);

NMO_API nmo_status_t nmo_project_plan_set_material_texture_slot_source_path(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    uint32_t slot,
    const char *source_path);

NMO_API nmo_status_t nmo_project_plan_add_obj_material(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const nmo_project_material_spec_t *spec);

NMO_API size_t nmo_project_plan_obj_material_count(
    const nmo_project_plan_t *plan,
    uint32_t object_handle);

NMO_API nmo_status_t nmo_project_plan_get_obj_material(
    const nmo_project_plan_t *plan,
    uint32_t object_handle,
    size_t index,
    nmo_project_material_spec_t *out_spec);

NMO_API nmo_status_t nmo_project_plan_set_obj_material_texture(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    size_t index,
    const char *path);

NMO_API nmo_status_t nmo_project_plan_set_obj_material_texture_slot(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    size_t index,
    uint32_t slot,
    const char *path);

NMO_API nmo_status_t nmo_project_plan_set_obj_material_source_paths(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    size_t index,
    const char *source_path,
    const char *texture_source_path);

NMO_API size_t nmo_project_plan_asset_count(
    const nmo_project_plan_t *plan);

NMO_API nmo_status_t nmo_project_plan_get_asset(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_asset_desc_t *out_asset);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ASSET_PLAN_H */
