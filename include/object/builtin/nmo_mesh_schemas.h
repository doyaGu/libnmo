/**
 * @file nmo_mesh_schemas.h
 * @brief CKMesh schema definitions header
 * 
 * Based on reverse engineering analysis from CK2_3D.dll:
 * - RCKMesh::Load at 0x1002816A (2398 bytes)
 * - RCKMesh::Save at 0x10027385 (2661 bytes)
 * - RCKMesh structure (260 bytes)
 * 
 * See docs/CK2_3D_reverse_notes_extended.md lines 1829-2392 for detailed analysis.
 */

#ifndef NMO_CKMESH_SCHEMAS_H
#define NMO_CKMESH_SCHEMAS_H

#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief RCKMesh state structure (260 bytes)
 * 
 * Complete mesh data: vertices, faces, materials, channels, weights, LOD.
 */
typedef struct nmo_mesh_state {
    nmo_beobject_state_t beobject;     ///< Parent CKBeObject state (80 bytes)
    
    // === Mesh flags (4 bytes at 0x50) ===
    uint32_t flags;                       ///< Mesh flags (mask 0x7FE39A)
    
    // === Geometry attributes (40 bytes at 0x54-0x7B) ===
    nmo_vector_t bary_center;         ///< Geometric center
    float radius;                         ///< Bounding sphere radius
    nmo_vector_t local_box_min;       ///< Local bounding box min
    nmo_vector_t local_box_max;       ///< Local bounding box max
    
    // === Topology data (arrays) ===
    uint32_t face_count;                  ///< Number of faces
    nmo_face_t *faces;                 ///< Face array (arena-allocated)
    uint16_t *face_vertex_indices;        ///< Vertex indices (3 per face)
    
    uint32_t line_count;                  ///< Number of line segments
    uint16_t *line_indices;               ///< Line indices (2 per line)
    
    // === Vertex data ===
    uint32_t vertex_count;                ///< Number of vertices
    nmo_vertex_t *vertices;            ///< Vertex array (position+normal+UV)
    uint32_t *vertex_colors;              ///< Vertex colors (ARGB packed)
    uint32_t *vertex_specular;            ///< Specular colors (ARGB packed)
    float *vertex_weights;                ///< Bone weights (skinning)
    uint32_t vertex_weight_count;          ///< Vertex weight count
    
    // === Material system ===
    uint32_t material_group_count;        ///< Material group count
    nmo_material_group_t *material_groups;  ///< Material groups
    
    uint32_t material_channel_count;      ///< Material channel count
    nmo_material_channel_t *material_channels;  ///< Material channels
    
    // === Rendering optimization ===
    bool is_valid;                        ///< Mesh validity flag
    uint32_t vertex_buffer_handle;        ///< Hardware vertex buffer (D3D/OpenGL)
    uint32_t index_buffer_handle;         ///< Hardware index buffer
    
    // === Progressive mesh (LOD) ===
    bool has_progressive_mesh;            ///< Has LOD data
    int32_t pm_field_0;                   ///< Progressive mesh internal field
    int32_t pm_morph_enabled;             ///< Morph animation enabled
    int32_t pm_morph_step;                ///< Morph step size
    uint32_t pm_data_size;                ///< Progressive data buffer size
    void *pm_data;                        ///< Progressive mesh data (arena)
} nmo_mesh_state_t;

/* Function pointer types for vtable */
NMO_API nmo_status_t nmo_mesh_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_mesh_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_mesh_serialize_ex(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context,
    bool skip_geometry);

NMO_DECLARE_OBJECT_SCHEMA(nmo_mesh_vtable, nmo_register_mesh_type)

NMO_API nmo_status_t nmo_mesh_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKMESH_SCHEMAS_H */
