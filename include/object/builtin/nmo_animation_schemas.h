/**
 * @file nmo_animation_schemas.h
 * @brief CKAnimation, CKKeyedAnimation, CKObjectAnimation schema definitions
 */

#ifndef NMO_CKANIMATION_SCHEMAS_H
#define NMO_CKANIMATION_SCHEMAS_H

#include "object/builtin/nmo_sceneobject_schemas.h"
#include "object/nmo_object_enum_defs.h"
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
 * @brief CKAnimation state
 */
typedef struct nmo_animation_state {
    nmo_sceneobject_state_t base;

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
} nmo_animation_state_t;

/**
 * @brief CKKeyedAnimation state
 */
typedef struct nmo_keyedanimation_state {
    nmo_animation_state_t base;

    uint32_t animation_count;
    nmo_object_id_t *animation_ids;

    uint8_t has_merge;
    int32_t merged;
    float merge_factor;

    uint32_t subanim_count;
    nmo_keyedanimation_subanim_t *subanims;
} nmo_keyedanimation_state_t;

/**
 * @brief ObjectAnimation data format
 */
typedef CK_OBJECTANIMATION_FORMAT nmo_objectanimation_format_t;

/**
 * @brief CKObjectAnimation state
 */
typedef struct nmo_objectanimation_state {
    nmo_sceneobject_state_t base;

    nmo_objectanimation_format_t format;

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
} nmo_objectanimation_state_t;

NMO_API nmo_status_t nmo_animation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_animation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_keyedanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_keyedanimation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_objectanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_objectanimation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_animation_vtable, nmo_register_animation_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_keyedanimation_vtable, nmo_register_keyedanimation_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_objectanimation_vtable, nmo_register_objectanimation_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKANIMATION_SCHEMAS_H */
