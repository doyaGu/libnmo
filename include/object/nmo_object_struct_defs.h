/**
 * @file nmo_object_struct_defs.h
 * @brief Struct definitions shared by object state schemas
 */

#ifndef NMO_OBJECT_STRUCT_DEFS_H
#define NMO_OBJECT_STRUCT_DEFS_H

#include "nmo_types.h"
#include "core/nmo_color.h"
#include "core/nmo_guid.h"
#include "core/nmo_math.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;

/* ============================================================================
 * CK3dEntity Skin Helpers
 * ============================================================================ */

typedef struct nmo_3dentity_skin_vertex {
    uint32_t bone_count;
    nmo_vector_t initial_pos;
    uint32_t *bone_indices;
    float *bone_weights;
} nmo_3dentity_skin_vertex_t;

typedef struct nmo_3dentity_skin_bone {
    nmo_object_id_t bone_id;
    uint32_t bone_flags;
    nmo_matrix_t inverse_bind_matrix;
} nmo_3dentity_skin_bone_t;

typedef struct nmo_3dentity_skin {
    nmo_matrix_t object_init_matrix;
    uint32_t bone_count;
    nmo_3dentity_skin_bone_t *bones;
    uint32_t vertex_count;
    nmo_3dentity_skin_vertex_t *vertices;
    uint32_t normal_count;
    nmo_vector_t *normals;
    uint8_t normals_present;
    uint8_t normals_have_count;
} nmo_3dentity_skin_t;

/* ============================================================================
 * CKMesh Helpers
 * ============================================================================ */

typedef struct nmo_vertex {
    nmo_vector_t position;
    nmo_vector_t normal;
    nmo_vector2_t uv;
} nmo_vertex_t;

typedef struct nmo_face {
    nmo_vector_t normal;
    uint16_t material_group_idx;
    uint16_t channel_mask;
} nmo_face_t;

typedef struct nmo_material_channel {
    nmo_ref_t material;
    uint32_t flags;
    uint32_t source_blend;
    uint32_t dest_blend;
    uint32_t uv_count;
    nmo_vector2_t *uv_coords;
} nmo_material_channel_t;

typedef struct nmo_material_group {
    nmo_ref_t material;
    int32_t padding;
} nmo_material_group_t;

/* ============================================================================
 * CKPatchMesh Helpers
 * ============================================================================ */

typedef struct nmo_patchmesh_patch {
    uint32_t type;
    uint32_t smoothing_group;
    uint8_t data[40];
} nmo_patchmesh_patch_t;

typedef struct nmo_patchmesh_channel {
    nmo_ref_t material;
    uint32_t flags;
    uint32_t type;
    uint32_t subtype;
    uint32_t patch_count;
    uint8_t *patches_raw;
    uint32_t uv_count;
    nmo_vector2_t *uvs;
} nmo_patchmesh_channel_t;

/* ============================================================================
 * CKTexture Helpers
 * ============================================================================ */

typedef struct nmo_texture_format {
    uint32_t width;
    uint32_t height;
    uint32_t bits_per_pixel;
    uint32_t bytes_per_line;
    uint32_t image_size;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t alpha_mask;
} nmo_texture_format_t;

typedef struct nmo_mipmap_level {
    uint32_t width;
    uint32_t height;
    uint32_t size;
    uint8_t *data;
} nmo_mipmap_level_t;

typedef struct nmo_texture_reader_slot {
    uint32_t format_type;
    uint32_t extension;
    nmo_guid_t reader_guid;
    uint32_t data_size;
    uint8_t *data;
    uint32_t alpha_count;
    uint32_t alpha_value;
    uint32_t alpha_plane_size;
    uint8_t *alpha_plane;
} nmo_texture_reader_slot_t;

typedef struct nmo_texture_raw_slot {
    int32_t bits_per_pixel;
    int32_t width;
    int32_t height;
    uint32_t alpha_mask;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t compression;
    uint32_t blue_size;
    uint8_t *blue_data;
    uint32_t green_size;
    uint8_t *green_data;
    uint32_t red_size;
    uint8_t *red_data;
    uint32_t alpha_size;
    uint8_t *alpha_data;
} nmo_texture_raw_slot_t;

typedef struct nmo_texture_bitmap2_slot {
    int32_t header_size;
    uint32_t buffer_size;
    uint8_t *buffer;
} nmo_texture_bitmap2_slot_t;

/* ============================================================================
 * Bitmap/Sprite/Text Helpers
 * ============================================================================ */

typedef struct nmo_bitmapdata {
    uint32_t width;
    uint32_t height;
    uint8_t *pixel_data;
    size_t pixel_data_size;

    uint8_t *palette_data;
    size_t palette_size;
    uint8_t *system_copy_data;
    size_t system_copy_size;
    uint8_t *video_backup_data;
    size_t video_backup_size;
    uint8_t *pixels_data;
    size_t pixels_size;
    uint8_t *raw_chunk_data;
    size_t raw_chunk_size;
} nmo_bitmapdata_t;

typedef struct nmo_font_info {
    const char *font_name;
    int32_t size;
    int32_t weight;
    int32_t italic;
    int32_t underline;
} nmo_font_info_t;

/* ============================================================================
 * Lighting & Materials
 * ============================================================================ */

typedef struct nmo_light_data {
    VXLIGHT_TYPE type;
    nmo_color_t diffuse;
    nmo_color_t specular;
    nmo_color_t ambient;
    float position[3];
    float direction[3];
    float range;
    float falloff;
    float attenuation0;
    float attenuation1;
    float attenuation2;
    float inner_spot_cone;
    float outer_spot_cone;
} nmo_light_data_t;

typedef struct nmo_material_colors {
    nmo_color_t ambient;
    nmo_color_t diffuse;
    nmo_color_t specular;
    nmo_color_t emissive;
} nmo_material_colors_t;

/* ============================================================================
 * Scene/Place Helpers
 * ============================================================================ */

typedef struct nmo_scene_object_desc {
    nmo_ref_t ref;
    nmo_chunk_t *initial_value;
    nmo_chunk_t *reserved;
    uint32_t flags;
} nmo_scene_object_desc_t;

typedef struct nmo_place_portal_entry {
    nmo_ref_t place;
    nmo_ref_t portal;
} nmo_place_portal_entry_t;

/* ============================================================================
 * DataArray Helpers
 * ============================================================================ */

typedef struct nmo_dataarray_column_format {
    const char *name;
    CK_ARRAYTYPE type;
    nmo_guid_t parameter_type_guid;
} nmo_dataarray_column_format_t;

typedef struct nmo_dataarray_parameter {
    nmo_ref_t ref;
    nmo_chunk_t *chunk;
} nmo_dataarray_parameter_t;

typedef union nmo_dataarray_cell {
    int32_t int_value;
    float float_value;
    const char *string_value;
    nmo_ref_t object_ref;
    nmo_dataarray_parameter_t parameter;
} nmo_dataarray_cell_t;

typedef struct nmo_dataarray_row {
    uint32_t column_count;
    nmo_dataarray_cell_t *cells;
} nmo_dataarray_row_t;

/* ============================================================================
 * Curve/Character/Animation Helpers
 * ============================================================================ */

typedef struct nmo_curve_point_subchunk {
    nmo_ref_t ref;
    nmo_chunk_t *chunk;
} nmo_curve_point_subchunk_t;

typedef struct nmo_ik_joint {
    uint32_t flags;
    nmo_vector_t min;
    nmo_vector_t max;
    nmo_vector_t damping;
} nmo_ik_joint_t;

typedef struct nmo_character_subpart {
    nmo_object_id_t object_id;
    nmo_chunk_t *chunk;
} nmo_character_subpart_t;

typedef struct nmo_attribute_category {
    const char *name;
    uint32_t flags;
    bool present;
} nmo_attribute_category_t;

typedef struct nmo_attribute_descriptor {
    const char *name;
    nmo_guid_t parameter_type_guid;
    int32_t category_index;
    int32_t compatible_class_id;
    uint32_t flags;
    bool present;
} nmo_attribute_descriptor_t;

typedef struct nmo_keyedanimation_subanim {
    nmo_ref_t ref;
    nmo_chunk_t *chunk;
} nmo_keyedanimation_subanim_t;

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_STRUCT_DEFS_H */
