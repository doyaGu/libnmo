/**
 * @file nmo_ckcharacter_schemas.h
 * @brief CKCharacter and CKBodyPart schema definitions
 */

#ifndef NMO_CKCHARACTER_SCHEMAS_H
#define NMO_CKCHARACTER_SCHEMAS_H

#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_ck3dobject_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "core/nmo_math.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKCharacter state
 */
typedef struct nmo_ckcharacter_state {
    nmo_ck3dentity_state_t base;

    uint32_t body_part_count;
    nmo_object_id_t *body_part_ids;

    uint32_t animation_count;
    nmo_object_id_t *animation_ids;

    nmo_object_id_t active_animation_id;
    nmo_object_id_t anim_dest_id;
    nmo_object_id_t root_body_part_id;
    nmo_object_id_t floor_ref_id;

    uint32_t subpart_count;
    nmo_character_subpart_t *subparts;
} nmo_ckcharacter_state_t;

/**
 * @brief CKBodyPart state
 */
typedef struct nmo_ckbodypart_state {
    nmo_ck3dobject_state_t base;

    uint8_t has_character;
    nmo_object_id_t character_id;

    uint8_t has_rotation_joint;
    nmo_ik_joint_t rotation_joint;
} nmo_ckbodypart_state_t;

NMO_API nmo_status_t nmo_ckcharacter_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckcharacter_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckbodypart_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckbodypart_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckcharacter_vtable, nmo_register_ckcharacter_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_ckbodypart_vtable, nmo_register_ckbodypart_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKCHARACTER_SCHEMAS_H */
