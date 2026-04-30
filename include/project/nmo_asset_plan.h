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
    bool has_material_color;
    float material_color[4];
} nmo_project_asset_desc_t;

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
