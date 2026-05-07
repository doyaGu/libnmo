#ifndef NMO_ASSET_EDIT_H
#define NMO_ASSET_EDIT_H

#include "format/nmo_obj_parser.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_primitive_mesh.h"
#include "runtime/nmo_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_asset_mesh_material_binding {
    const char *name;
    nmo_object_id_t material_id;
} nmo_asset_mesh_material_binding_t;

typedef struct nmo_asset_mesh_import_options {
    nmo_object_id_t default_material_id;
    const nmo_asset_mesh_material_binding_t *materials;
    size_t material_count;
} nmo_asset_mesh_import_options_t;

typedef struct nmo_asset_material_render_flags {
    bool has_texture_blend;
    VXTEXTURE_BLENDMODE texture_blend;
    bool has_source_blend;
    VXBLEND_MODE source_blend;
    bool has_destination_blend;
    VXBLEND_MODE destination_blend;
    bool has_min_filter;
    VXTEXTURE_FILTERMODE min_filter;
    bool has_mag_filter;
    VXTEXTURE_FILTERMODE mag_filter;
    bool has_wrap;
    VXTEXTURE_ADDRESSMODE wrap;
    bool has_alpha_func;
    VXCMPFUNC alpha_func;
} nmo_asset_material_render_flags_t;

typedef struct nmo_asset_material_channels {
    bool has_diffuse;
    float diffuse[4];
    bool has_ambient;
    float ambient[4];
    bool has_specular;
    float specular[4];
    bool has_emissive;
    float emissive[4];
    bool has_specular_power;
    float specular_power;
} nmo_asset_material_channels_t;

NMO_API nmo_status_t nmo_asset_edit_set_material_color(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    float r,
    float g,
    float b,
    float a);

NMO_API nmo_status_t nmo_asset_edit_set_material_channels(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const nmo_asset_material_channels_t *channels);

NMO_API nmo_status_t nmo_asset_edit_set_material_render_flags(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const nmo_asset_material_render_flags_t *flags);

NMO_API nmo_status_t nmo_asset_edit_set_texture_rgba(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t texture_id,
    const void *rgba_pixels,
    uint32_t width,
    uint32_t height);

NMO_API nmo_status_t nmo_asset_edit_set_texture_from_file(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t texture_id,
    const char *path);

NMO_API nmo_status_t nmo_asset_edit_bind_material_texture(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    nmo_object_id_t texture_id,
    uint32_t slot);

NMO_API nmo_status_t nmo_asset_edit_set_primitive_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    nmo_primitive_mesh_t primitive,
    nmo_object_id_t material_id);

NMO_API nmo_status_t nmo_asset_edit_set_obj_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    const nmo_obj_data_t *obj_data,
    const nmo_asset_mesh_import_options_t *options);

NMO_API nmo_status_t nmo_asset_edit_set_obj_mesh_from_file(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    const char *path,
    const nmo_asset_mesh_import_options_t *options);

NMO_API nmo_status_t nmo_asset_edit_bind_entity_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t entity_id,
    nmo_object_id_t mesh_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ASSET_EDIT_H */
