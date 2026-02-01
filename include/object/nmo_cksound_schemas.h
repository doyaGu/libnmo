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

NMO_API nmo_result_t nmo_register_cksound_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* Serialization entry points */
NMO_API nmo_result_t nmo_cksound_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksound_state_t *out_state);

NMO_API nmo_result_t nmo_cksound_serialize(
    const nmo_cksound_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckwavesound_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckwavesound_state_t *out_state);

NMO_API nmo_result_t nmo_ckwavesound_serialize(
    const nmo_ckwavesound_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckmidisound_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckmidisound_state_t *out_state);

NMO_API nmo_result_t nmo_ckmidisound_serialize(
    const nmo_ckmidisound_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSOUND_SCHEMAS_H */
