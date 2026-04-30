#ifndef NMO_ASSET_EDIT_H
#define NMO_ASSET_EDIT_H

#include "object/nmo_primitive_mesh.h"
#include "runtime/nmo_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_asset_edit_set_material_color(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    float r,
    float g,
    float b,
    float a);

NMO_API nmo_status_t nmo_asset_edit_set_primitive_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    nmo_primitive_mesh_t primitive,
    nmo_object_id_t material_id);

NMO_API nmo_status_t nmo_asset_edit_bind_entity_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t entity_id,
    nmo_object_id_t mesh_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ASSET_EDIT_H */
