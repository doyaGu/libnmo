/**
 * @file nmo_cksound_schemas.h
 * @brief CKSound/CKWaveSound/CKMidiSound schema definitions
 *
 * Implements serialization for sound-related objects based on CK2 sources:
 * - CKSound::Save/Load (CKSound.cpp)
 * - CKWaveSound::Save/Load (CKWaveSound.cpp)
 * - CKMidiSound::Save/Load (CKMidiSound.cpp)
 */

#ifndef NMO_CKSOUND_SCHEMAS_H
#define NMO_CKSOUND_SCHEMAS_H

#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/**
 * @brief Simple 3D vector (x,y,z)
 */
typedef struct nmo_vx_vector3 {
    float x;
    float y;
    float z;
} nmo_vx_vector3_t;

/**
 * @brief CKSound state
 */
typedef struct nmo_cksound_state {
    nmo_ckbeobject_state_t base;
    uint32_t save_options;
    char *file_name;
} nmo_cksound_state_t;

/**
 * @brief CKWaveSound state
 */
typedef struct nmo_ckwavesound_state {
    nmo_cksound_state_t base;

    uint8_t has_wave_file_name;
    char *wave_file_name;

    uint8_t has_duration;
    int32_t duration;

    uint8_t has_data2;
    uint32_t state_flags;
    float priority;
    float gain;
    float pan;
    float pitch;

    float cone_in_angle;
    float cone_out_angle;
    float cone_out_gain;

    float min_distance;
    float max_distance;
    uint32_t distance_behavior;

    nmo_object_id_t attached_object_id;
    nmo_vx_vector3_t position;
    nmo_vx_vector3_t direction;
} nmo_ckwavesound_state_t;

/**
 * @brief CKMidiSound state
 */
typedef struct nmo_ckmidisound_state {
    nmo_cksound_state_t base;
    uint8_t has_midi_file_name;
    char *midi_file_name;
} nmo_ckmidisound_state_t;

/* Serialization entry points */
NMO_API nmo_result_t nmo_cksound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_cksound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckwavesound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckwavesound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckmidisound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckmidisound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cksound_vtable, nmo_register_cksound_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_ckwavesound_vtable, nmo_register_ckwavesound_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_ckmidisound_vtable, nmo_register_ckmidisound_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSOUND_SCHEMAS_H */
