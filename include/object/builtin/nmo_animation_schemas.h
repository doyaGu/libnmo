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
#include "object/nmo_ref.h"
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
    uint8_t data_is_legacy;
    uint32_t flags;
    float frame_rate;

    uint8_t has_length;
    float length;

    uint8_t has_root_entity;
    uint32_t legacy_body_part_count;
    nmo_ref_t *legacy_body_parts;
    nmo_ref_t root_entity;

    uint8_t has_character;
    nmo_ref_t character;

    uint8_t has_current_step;
    float current_step;
} nmo_animation_state_t;

/**
 * @brief CKKeyedAnimation state
 */
typedef struct nmo_keyedanimation_state {
    nmo_animation_state_t base;

    uint32_t animation_count;
    nmo_ref_t *animation_ids;

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
 * @brief A single animation controller's raw key data.
 *
 * The internal layout of keys depends on the controller type
 * (e.g., LINPOS = 16 bytes/key, LINROT = 20 bytes/key).
 * Use nmo_objanim_controller_key_size() to determine key size.
 */
typedef struct nmo_objanim_controller {
    uint32_t type;       /**< CKANIMATION_CONTROLLER enum value */
    uint32_t key_count;  /**< Explicit key count (NEWDATA/LEGACY); 0 for CONTROLLERS format */
    uint32_t data_size;  /**< Raw key data size in bytes */
    void    *data;       /**< Arena-allocated raw key bytes */
} nmo_objanim_controller_t;

/**
 * @brief Morph key with per-key variable-length vertex data (NEWDATA/LEGACY).
 */
typedef struct nmo_objanim_morph_key {
    float    time_step;  /**< Key time */
    uint32_t data_size;  /**< Vertex position data size in bytes */
    void    *data;       /**< Arena-allocated vertex positions */
} nmo_objanim_morph_key_t;

/**
 * @brief CKObjectAnimation state
 */
typedef struct nmo_objectanimation_state {
    nmo_sceneobject_state_t base;

    nmo_objectanimation_format_t format;

    nmo_vector_t root_pos;
    uint8_t has_root_pos;

    uint32_t flags;
    nmo_ref_t entity;

    uint8_t has_length;
    float length;

    uint8_t has_merge;
    float merge_factor;
    nmo_ref_t anim1;
    nmo_ref_t anim2;

    uint8_t has_shared_anim;
    nmo_ref_t shared_anim;

    uint8_t has_morph_counts;
    int32_t morph_vertex_count;
    int32_t morph_key_count;

    /* Parsed controller data */
    uint32_t controller_count;
    nmo_objanim_controller_t *controllers;
    uint8_t has_legacy_position_section;
    uint8_t has_legacy_rotation_section;
    uint8_t has_legacy_scale_section;
    uint8_t has_legacy_flags_section;
    uint8_t has_legacy_entity_section;

    /* Morph keys (NEWDATA/LEGACY formats) */
    uint32_t morph_key_parsed_count;
    nmo_objanim_morph_key_t *morph_keys;

    /* Morph normals (NEWDATA only, optional) */
    uint32_t morph_normals_id;      /**< 0, CK_STATESAVE_OBJANIMMORPHCOMP, or _MORPHNORMALS */
    uint32_t morph_normals_count;
    uint32_t *morph_normals_sizes;  /**< Per-key data sizes */
    void   **morph_normals_data;    /**< Per-key arena-allocated buffers */

    /* Uninterpreted CK_STATESAVE_OBJANIMMORPHKEYS payload (LEGACY only) */
    uint8_t has_legacy_morphkeys;
    uint8_t *legacy_morphkeys;
    size_t legacy_morphkeys_size;

    /* Fallback for unparseable remainder */
    uint8_t *raw_tail;
    size_t raw_tail_size;
} nmo_objectanimation_state_t;

/**
 * @brief Get the size of a single key for a given controller type.
 * @return Key size in bytes, or 0 if the type is unknown/variable.
 */
NMO_API uint32_t nmo_objanim_controller_key_size(uint32_t controller_type);

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

NMO_API nmo_status_t nmo_animation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_animation_remap_dependencies(
    void *instance,
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

NMO_API nmo_status_t nmo_keyedanimation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_keyedanimation_remap_dependencies(
    void *instance,
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

NMO_API nmo_status_t nmo_objectanimation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_objectanimation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_animation_vtable, nmo_register_animation_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_keyedanimation_vtable, nmo_register_keyedanimation_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_objectanimation_vtable, nmo_register_objectanimation_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKANIMATION_SCHEMAS_H */
