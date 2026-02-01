/**
 * @file nmo_ckcharacter_schemas.h
 * @brief CKCharacter and CKBodyPart schema definitions
 */

#ifndef NMO_CKCHARACTER_SCHEMAS_H
#define NMO_CKCHARACTER_SCHEMAS_H

#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_ck3dobject_schemas.h"
#include "core/nmo_math.h"
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
 * @brief IK joint data (matches CKIkJoint layout)
 */
typedef struct nmo_ckik_joint {
    uint32_t flags;
    nmo_vector_t min;
    nmo_vector_t max;
    nmo_vector_t damping;
} nmo_ckik_joint_t;

/**
 * @brief Character subpart entry (optional subchunks)
 */
typedef struct nmo_ckcharacter_subpart {
    nmo_object_id_t object_id;
    nmo_chunk_t *chunk;
} nmo_ckcharacter_subpart_t;

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
    nmo_ckcharacter_subpart_t *subparts;
} nmo_ckcharacter_state_t;

/**
 * @brief CKBodyPart state
 */
typedef struct nmo_ckbodypart_state {
    nmo_ck3dobject_state_t base;

    uint8_t has_character;
    nmo_object_id_t character_id;

    uint8_t has_rotation_joint;
    nmo_ckik_joint_t rotation_joint;
} nmo_ckbodypart_state_t;

NMO_API nmo_result_t nmo_register_ckcharacter_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckcharacter_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcharacter_state_t *out_state);

NMO_API nmo_result_t nmo_ckcharacter_serialize(
    const nmo_ckcharacter_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckbodypart_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbodypart_state_t *out_state);

NMO_API nmo_result_t nmo_ckbodypart_serialize(
    const nmo_ckbodypart_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKCHARACTER_SCHEMAS_H */
