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
 * @brief A single animation controller's key data.
 *
 * With key_count > 0, data holds exactly key_count keys and nothing else; the
 * key size depends on the controller type (see nmo_objanim_controller_key_size()
 * and nmo_objanim_controller_keys_size()). CONTROLLERS-format files store the
 * key count as a prefix of each controller blob; reading strips it and writing
 * adds it back. With key_count == 0, data is an opaque blob that is written
 * verbatim: unknown controller types, empty controllers, or blobs that do not
 * match the layout of their type.
 */
typedef struct nmo_objanim_controller {
    uint32_t type;       /**< CKANIMATION_CONTROLLER enum value */
    uint32_t key_count;  /**< Number of keys in data; 0 when data is an opaque blob */
    uint32_t data_size;  /**< Size of data in bytes */
    void    *data;       /**< Arena-allocated key bytes (or opaque blob) */
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
 *
 * This is the key size of the CONTROLLERS format and of the engine's memory.
 * Use nmo_objanim_controller_format_key_size() for other formats.
 *
 * @return Key size in bytes, or 0 if the type is unknown or its keys have a
 *         variable size (bezier controllers, see nmo_objanim_bezier_key_decode()).
 */
NMO_API uint32_t nmo_objanim_controller_key_size(uint32_t controller_type);

/**
 * @brief Get the size of a single key as stored in the given animation format.
 *
 * Equal to nmo_objanim_controller_key_size() except for the scale-axis
 * controller in NEWDATA and LEGACY files, whose 24-byte keys hold the time, an
 * unused float and the quaternion.
 *
 * @return Key size in bytes, or 0 if the type is unknown or has variable-size keys
 */
NMO_API uint32_t nmo_objanim_controller_format_key_size(
    uint32_t controller_type,
    nmo_objectanimation_format_t format);

/** @brief true for the bezier position and scale controller types. */
NMO_API bool nmo_objanim_controller_is_bezier(uint32_t controller_type);

/**
 * @brief One packed bezier key.
 *
 * On disk a key is 20 bytes (time, position, a dword holding two 16-bit flag
 * words) followed by a 12-byte tangent for each flag word that has bit 0x20
 * set: tangent[0] belongs to the low word, tangent[1] to the high word.
 */
typedef struct nmo_objanim_bezier_key {
    float time;
    float position[3];
    uint32_t flags;
    bool has_tangent[2];
    float tangent[2][3];
} nmo_objanim_bezier_key_t;

/**
 * @brief Decode the packed bezier key at key.
 *
 * @param key       Start of the key
 * @param available Bytes readable at key
 * @param out       Receives the decoded key; may be NULL
 * @return Size of the key in bytes, or 0 if it is truncated
 */
NMO_API size_t nmo_objanim_bezier_key_decode(const void *key,
                                             size_t available,
                                             nmo_objanim_bezier_key_t *out);

/**
 * @brief Get the size of key_count consecutive keys of a controller.
 *
 * @param controller_type CKANIMATION_CONTROLLER enum value
 * @param format          Animation format the keys are stored in
 * @param keys            Start of the keys (read for bezier controllers only)
 * @param available       Bytes readable at keys
 * @param key_count       Number of keys
 * @param out_size        Receives the total size of the keys in bytes
 * @return true if the type is known and the keys fit in available
 */
NMO_API bool nmo_objanim_controller_keys_size(uint32_t controller_type,
                                              nmo_objectanimation_format_t format,
                                              const void *keys,
                                              size_t available,
                                              uint32_t key_count,
                                              size_t *out_size);

/** @brief Controller type of the morph controller (CKANIMATION_MORPH_CONTROL). */
#define NMO_OBJANIM_CONTROLLER_MORPH 0x73847810u

/**
 * @brief Header of a morph controller blob in the CONTROLLERS format.
 *
 * The blob is three dwords, `key count`, `vertex count` and `has normals`,
 * followed by each key: a float time, 12 bytes per vertex of position and, if
 * has_normals is set, 4 bytes per vertex of compressed normal. The controller
 * keeps the blob whole in its data, with key_count 0.
 */
typedef struct nmo_objanim_morph_info {
    uint32_t key_count;
    uint32_t vertex_count;
    bool has_normals;
} nmo_objanim_morph_info_t;

/**
 * @brief Read the header of a morph controller and check that the blob has
 *        exactly the size its counts call for.
 *
 * @return true if the controller is a well-formed morph controller
 */
NMO_API bool nmo_objanim_morph_controller_info(
    const nmo_objanim_controller_t *controller,
    nmo_objanim_morph_info_t *out_info);

/**
 * @brief Get one key of a morph controller.
 *
 * @param controller  Controller accepted by nmo_objanim_morph_controller_info()
 * @param info        Its header
 * @param index       Key index, below info->key_count
 * @param out_time    Receives the key time
 * @param out_positions Receives a pointer to vertex_count vectors of three
 *                    floats inside the controller data; may be NULL
 * @param out_normals Receives a pointer to vertex_count compressed normals
 *                    (4 bytes each) or NULL without normals; may be NULL
 * @return true if index is in range
 */
NMO_API bool nmo_objanim_morph_controller_key(
    const nmo_objanim_controller_t *controller,
    const nmo_objanim_morph_info_t *info,
    uint32_t index,
    float *out_time,
    const float **out_positions,
    const uint8_t **out_normals);

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
