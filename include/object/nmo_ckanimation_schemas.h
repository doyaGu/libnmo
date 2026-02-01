/**
 * @file nmo_ckanimation_schemas.h
 * @brief CKAnimation, CKKeyedAnimation, CKObjectAnimation schema definitions
 */

#ifndef NMO_CKANIMATION_SCHEMAS_H
#define NMO_CKANIMATION_SCHEMAS_H

#include "object/nmo_cksceneobject_schemas.h"
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
 * @brief CKAnimation state
 */
typedef struct nmo_ckanimation_state {
    nmo_cksceneobject_state_t base;

    uint8_t has_data;
    uint32_t flags;
    float frame_rate;

    uint8_t has_length;
    float length;

    uint8_t has_root_entity;
    nmo_object_id_t root_entity_id;

    uint8_t has_character;
    nmo_object_id_t character_id;

    uint8_t has_current_step;
    float current_step;
} nmo_ckanimation_state_t;

/**
 * @brief CKKeyedAnimation sub-animation entry
 */
typedef struct nmo_ckkeyedanimation_subanim {
    nmo_object_id_t object_id;
    nmo_chunk_t *chunk;
} nmo_ckkeyedanimation_subanim_t;

/**
 * @brief CKKeyedAnimation state
 */
typedef struct nmo_ckkeyedanimation_state {
    nmo_ckanimation_state_t base;

    uint32_t animation_count;
    nmo_object_id_t *animation_ids;

    uint8_t has_merge;
    int32_t merged;
    float merge_factor;

    uint32_t subanim_count;
    nmo_ckkeyedanimation_subanim_t *subanims;
} nmo_ckkeyedanimation_state_t;

/**
 * @brief ObjectAnimation data format
 */
typedef enum nmo_ckobjectanimation_format {
    NMO_OBJANIM_FORMAT_NONE = 0,
    NMO_OBJANIM_FORMAT_SHARED = 1,
    NMO_OBJANIM_FORMAT_CONTROLLERS = 2,
    NMO_OBJANIM_FORMAT_NEWDATA = 3,
    NMO_OBJANIM_FORMAT_LEGACY = 4
} nmo_ckobjectanimation_format_t;

/**
 * @brief CKObjectAnimation state
 */
typedef struct nmo_ckobjectanimation_state {
    nmo_cksceneobject_state_t base;

    nmo_ckobjectanimation_format_t format;

    nmo_vector_t root_pos;
    uint8_t has_root_pos;

    uint32_t flags;
    nmo_object_id_t entity_id;

    uint8_t has_length;
    float length;

    uint8_t has_merge;
    float merge_factor;
    nmo_object_id_t anim1_id;
    nmo_object_id_t anim2_id;

    uint8_t has_shared_anim;
    nmo_object_id_t shared_anim_id;

    uint8_t has_morph_counts;
    int32_t morph_vertex_count;
    int32_t morph_key_count;

    void *raw_tail;
    size_t raw_tail_size;
} nmo_ckobjectanimation_state_t;

NMO_API nmo_result_t nmo_register_ckanimation_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckanimation_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckanimation_state_t *out_state);

NMO_API nmo_result_t nmo_ckanimation_serialize(
    const nmo_ckanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckkeyedanimation_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckkeyedanimation_state_t *out_state);

NMO_API nmo_result_t nmo_ckkeyedanimation_serialize(
    const nmo_ckkeyedanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckobjectanimation_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckobjectanimation_state_t *out_state);

NMO_API nmo_result_t nmo_ckobjectanimation_serialize(
    const nmo_ckobjectanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKANIMATION_SCHEMAS_H */
