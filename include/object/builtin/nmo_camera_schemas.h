/**
 * @file nmo_camera_schemas.h
 * @brief CKCamera schema definitions header
 */

#ifndef NMO_CKCAMERA_SCHEMAS_H
#define NMO_CKCAMERA_SCHEMAS_H

#include "object/builtin/nmo_3dentity_schemas.h"
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
 * @brief CKCamera state structure
 * 
 * Represents the deserialized state of a CKCamera object.
 * This is a PARTIAL schema - some fields are preserved as raw data.
 */
typedef struct nmo_camera_state {
    nmo_3dentity_state_t entity;  ///< Parent CK3dEntity state
    
    // Camera projection parameters
    uint32_t projection_type;  ///< CK_PERSPECTIVEPROJECTION or CK_ORTHOGRAPHICPROJECTION
    float fov;                 ///< Field of view angle (radians)
    float orthographic_zoom;   ///< Orthographic zoom factor
    int32_t width;             ///< Viewport width
    int32_t height;            ///< Viewport height
    float near_plane;          ///< Near clipping plane distance
    float far_plane;           ///< Far clipping plane distance

    /* Chunk presence tracking */
    uint8_t has_cameraonly_chunk;
    uint8_t has_fov_chunk;
    uint8_t has_proj_chunk;
    uint8_t has_ortho_chunk;
    uint8_t has_aspect_chunk;
    uint8_t has_planes_chunk;
} nmo_camera_state_t;

NMO_API nmo_status_t nmo_camera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_camera_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_camera_vtable, nmo_register_camera_type)

NMO_API nmo_status_t nmo_camera_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_camera_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKCAMERA_SCHEMAS_H */
