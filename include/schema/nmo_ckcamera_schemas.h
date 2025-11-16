/**
 * @file nmo_ckcamera_schemas.h
 * @brief CKCamera schema definitions header
 */

#ifndef NMO_CKCAMERA_SCHEMAS_H
#define NMO_CKCAMERA_SCHEMAS_H

#include "schema/nmo_ck3dentity_schemas.h"
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
 * @brief CKCamera state structure
 * 
 * Represents the deserialized state of a CKCamera object.
 * This is a PARTIAL schema - some fields are preserved as raw data.
 */
typedef struct nmo_ckcamera_state {
    nmo_ck3dentity_state_t entity;  ///< Parent CK3dEntity state
    
    // Camera projection parameters
    uint32_t projection_type;  ///< 0=perspective, 1=orthographic
    float fov;                 ///< Field of view angle (degrees)
    float aspect_ratio;        ///< Width/height ratio
    float near_plane;          ///< Near clipping plane distance
    float far_plane;           ///< Far clipping plane distance
    float ortho_width;         ///< Orthographic view width
    float ortho_height;        ///< Orthographic view height
    
    // Preserved unknown data for future schema refinement
    void *raw_tail;           ///< Remaining chunk data (target, roll, etc)
    size_t raw_tail_size;     ///< Size of preserved data
} nmo_ckcamera_state_t;

/* Function pointer types for vtable */
typedef nmo_result_t (*nmo_ckcamera_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckcamera_state_t *out_state);

typedef nmo_result_t (*nmo_ckcamera_serialize_fn)(
    const nmo_ckcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_ckcamera_finish_loading_fn)(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Register CKCamera state schema
 * 
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckcamera_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get CKCamera deserialize function
 * @return Function pointer for deserialization
 */
NMO_API nmo_ckcamera_deserialize_fn nmo_get_ckcamera_deserialize(void);

/**
 * @brief Get CKCamera serialize function
 * @return Function pointer for serialization
 */
NMO_API nmo_ckcamera_serialize_fn nmo_get_ckcamera_serialize(void);

/**
 * @brief Get CKCamera finish_loading function
 * @return Function pointer for finish loading
 */
NMO_API nmo_ckcamera_finish_loading_fn nmo_get_ckcamera_finish_loading(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKCAMERA_SCHEMAS_H */
