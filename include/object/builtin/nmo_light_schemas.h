/**
 * @file nmo_light_schemas.h
 * @brief CKLight schema definitions header
 * 
 * Based on reverse engineering analysis from CK2_3D.dll:
 * - RCKLight::Load at 0x1001B50E (678 bytes)
 * - RCKLight::Save at 0x1001B389 (389 bytes)
 * - CKLightData structure (104 bytes)
 * 
 * See docs/CK2_3D_reverse_notes_extended.md for detailed analysis.
 */

#ifndef NMO_CKLIGHT_SCHEMAS_H
#define NMO_CKLIGHT_SCHEMAS_H

#include "core/nmo_color.h"
#include "object/nmo_object_enum_defs.h"
#include "object/builtin/nmo_3dentity_schemas.h"
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
 * @brief RCKLight state structure
 * 
 * Represents the deserialized state of an RCKLight object.
 * Size: RCK3dEntity (424B) + CKLightData (104B) + flags (4B) + power (4B) = 536 bytes
 */
typedef struct nmo_light_state {
    nmo_3dentity_state_t entity;  ///< Parent CK3dEntity state
    
    // Light data (104 bytes)
    nmo_light_data_t light_data;
    
    // Flags (4 bytes at 0x210)
    uint32_t flags;                  ///< Light flags (active, specular, etc.)
    
    // Power multiplier (4 bytes at 0x214)
    float light_power;               ///< Intensity multiplier (default 1.0)

    /* Chunk presence tracking */
    uint8_t has_light_power_chunk;
} nmo_light_state_t;

NMO_API nmo_status_t nmo_light_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_light_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_light_vtable, nmo_register_light_type)

NMO_API nmo_status_t nmo_light_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_light_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKLIGHT_SCHEMAS_H */
