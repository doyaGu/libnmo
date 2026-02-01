/**
 * @file nmo_ckmesh_schemas.h
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

#include "object/nmo_ckbeobject_schemas.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/**
 * @brief Mesh flags (VXMESH_FLAGS from CKEnums.h)
 * 
 * Matches the Virtools SDK mesh flags used by RCKMesh.
 */
typedef enum nmo_ck_mesh_flags {
    NMO_MESH_BOUNDINGUPTODATE     = 0x00000001,
    NMO_MESH_VISIBLE              = 0x00000002,
    NMO_MESH_OPTIMIZED            = 0x00000004,
    NMO_MESH_RENDERCHANNELS       = 0x00000008,
    NMO_MESH_HASTRANSPARENCY      = 0x00000010,
    NMO_MESH_PRELITMODE           = 0x00000080,
    NMO_MESH_WRAPU                = 0x00000100,
    NMO_MESH_WRAPV                = 0x00000200,
    NMO_MESH_FORCETRANSPARENCY    = 0x00001000,
    NMO_MESH_TRANSPARENCYUPTODATE = 0x00002000,
    NMO_MESH_UV_CHANGED           = 0x00004000,
    NMO_MESH_NORMAL_CHANGED       = 0x00008000,
    NMO_MESH_COLOR_CHANGED        = 0x00010000,
    NMO_MESH_POS_CHANGED          = 0x00020000,
    NMO_MESH_HINTDYNAMIC          = 0x00040000,
    NMO_MESH_GENNORMALS           = 0x00080000,
    NMO_MESH_PROCEDURALUV         = 0x00100000,
    NMO_MESH_PROCEDURALPOS        = 0x00200000,
    NMO_MESH_STRIPIFY             = 0x00400000,
    NMO_MESH_MONOMATERIAL         = 0x00800000,
    NMO_MESH_PM_BUILDNORM         = 0x01000000,
    NMO_MESH_BWEIGHTS_CHANGED     = 0x02000000,

    NMO_MESH_ALLFLAGS             = 0x007FF39F,
    NMO_MESH_ALLOWED_FLAGS_MASK   = 0x03FFF39A
} nmo_ck_mesh_flags_t;

/**
 * @brief Vertex save flags (compression/optimization)
 * 
 * Flags returned by GetSaveFlags() to control vertex data serialization.
 * Optimize storage by detecting uniform values or external references.
 */
typedef enum nmo_vertex_save_flags {
    NMO_VERTEX_COLOR1_UNIFORM   = 0x01,  ///< All vertex colors1 identical
    NMO_VERTEX_SPECULAR_UNIFORM = 0x02,  ///< All specular colors identical
    NMO_VERTEX_NORMALS_MISSING  = 0x04,  ///< No normals (need rebuild)
    NMO_VERTEX_UV_UNIFORM       = 0x08,  ///< All UVs identical
    NMO_VERTEX_POS_EXTERNAL     = 0x10   ///< Positions in external storage
} nmo_vertex_save_flags_t;

/**
 * @brief VxVector structure (3D vector, 12 bytes)
 */
typedef struct nmo_vx_vector {
    float x;  ///< X component
    float y;  ///< Y component
    float z;  ///< Z component
} nmo_vx_vector_t;

/**
 * @brief Vx2DVector structure (2D UV coordinates, 8 bytes)
 */
typedef struct nmo_vx_2d_vector {
    float u;  ///< U texture coordinate
    float v;  ///< V texture coordinate
} nmo_vx_2d_vector_t;

/**
 * @brief VxVertex structure (32 bytes)
 * 
 * Complete vertex data: position (12B) + normal (12B) + UV (8B)
 */
typedef struct nmo_vx_vertex {
    nmo_vx_vector_t position;    ///< 3D position (x, y, z)
    nmo_vx_vector_t normal;      ///< Surface normal (nx, ny, nz)
    nmo_vx_2d_vector_t uv;       ///< Texture coordinates (u, v)
} nmo_vx_vertex_t;

/**
 * @brief CKFace structure (16 bytes)
 * 
 * Stores face normal, material group index, and channel mask.
 * Vertex indices stored separately in m_FaceVertexIndices array.
 */
typedef struct nmo_ck_face {
    nmo_vx_vector_t normal;        ///< Face normal (12 bytes)
    uint16_t material_group_idx;   ///< Material group index
    uint16_t channel_mask;         ///< Multi-material channel mask
} nmo_ck_face_t;

/**
 * @brief CKMaterialChannel structure (~32 bytes)
 * 
 * Defines a material layer with custom UVs and blending modes.
 */
typedef struct nmo_ck_material_channel {
    nmo_object_id_t material_id;      ///< Associated CKMaterial object ID
    uint32_t flags;                   ///< Channel flags
    uint32_t source_blend;            ///< Source blend mode (VXBLEND_MODE)
    uint32_t dest_blend;              ///< Destination blend mode
    uint32_t uv_count;                ///< Custom UV count (0 = use main UV)
    nmo_vx_2d_vector_t *uv_coords;    ///< Custom UV array (arena-allocated)
} nmo_ck_material_channel_t;

/**
 * @brief CKMaterialGroup structure
 * 
 * Groups faces sharing the same material for efficient rendering.
 */
typedef struct nmo_ck_material_group {
    nmo_object_id_t material_id;      ///< Material object ID
} nmo_ck_material_group_t;

/**
 * @brief RCKMesh state structure (260 bytes)
 * 
 * Complete mesh data: vertices, faces, materials, channels, weights, LOD.
 */
typedef struct nmo_ck_mesh_state {
    nmo_ckbeobject_state_t beobject;     ///< Parent CKBeObject state (80 bytes)
    
    // === Mesh flags (4 bytes at 0x50) ===
    uint32_t flags;                       ///< Mesh flags (mask 0x7FE39A)
    
    // === Geometry attributes (40 bytes at 0x54-0x7B) ===
    nmo_vx_vector_t bary_center;         ///< Geometric center
    float radius;                         ///< Bounding sphere radius
    nmo_vx_vector_t local_box_min;       ///< Local bounding box min
    nmo_vx_vector_t local_box_max;       ///< Local bounding box max
    
    // === Topology data (arrays) ===
    uint32_t face_count;                  ///< Number of faces
    nmo_ck_face_t *faces;                 ///< Face array (arena-allocated)
    uint16_t *face_vertex_indices;        ///< Vertex indices (3 per face)
    
    uint32_t line_count;                  ///< Number of line segments
    uint16_t *line_indices;               ///< Line indices (2 per line)
    
    // === Vertex data ===
    uint32_t vertex_count;                ///< Number of vertices
    nmo_vx_vertex_t *vertices;            ///< Vertex array (position+normal+UV)
    uint32_t *vertex_colors;              ///< Vertex colors (ARGB packed)
    uint32_t *vertex_specular;            ///< Specular colors (ARGB packed)
    float *vertex_weights;                ///< Bone weights (skinning)
    uint32_t vertex_weight_count;          ///< Vertex weight count
    
    // === Material system ===
    uint32_t material_group_count;        ///< Material group count
    nmo_ck_material_group_t *material_groups;  ///< Material groups
    
    uint32_t material_channel_count;      ///< Material channel count
    nmo_ck_material_channel_t *material_channels;  ///< Material channels
    
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
} nmo_ck_mesh_state_t;

/* Function pointer types for vtable */
typedef nmo_result_t (*nmo_ckmesh_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_mesh_state_t *out_state);

typedef nmo_result_t (*nmo_ckmesh_serialize_fn)(
    const nmo_ck_mesh_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_ckmesh_finish_loading_fn)(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Register CKMesh state schema
 * 
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckmesh_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get CKMesh deserialize function
 * @return Function pointer for deserialization
 */
NMO_API nmo_ckmesh_deserialize_fn nmo_get_ckmesh_deserialize(void);

/**
 * @brief Get CKMesh serialize function
 * @return Function pointer for serialization
 */
NMO_API nmo_ckmesh_serialize_fn nmo_get_ckmesh_serialize(void);

/**
 * @brief Get CKMesh finish_loading function
 * @return Function pointer for finish loading
 */
NMO_API nmo_ckmesh_finish_loading_fn nmo_get_ckmesh_finish_loading(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKMESH_SCHEMAS_H */
