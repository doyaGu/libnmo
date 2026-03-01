/**
 * @file nmo_sound_schemas.h
 * @brief CKSound/CKWaveSound/CKMidiSound schema definitions
 *
 * Implements serialization for sound-related objects based on CK2 sources:
 * - CKSound::Save/Load (CKSound.cpp)
 * - CKWaveSound::Save/Load (CKWaveSound.cpp)
 * - CKMidiSound::Save/Load (CKMidiSound.cpp)
 */

#ifndef NMO_CKSOUND_SCHEMAS_H
#define NMO_CKSOUND_SCHEMAS_H

#include "object/builtin/nmo_beobject_schemas.h"
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
 * @brief CKSound state
 */
typedef struct nmo_sound_state {
    nmo_beobject_state_t base;
    uint32_t save_options;
    char *file_name;
} nmo_sound_state_t;

/**
 * @brief CKWaveSound state
 */
typedef struct nmo_wavesound_state {
    nmo_sound_state_t base;

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
    nmo_vector_t position;
    nmo_vector_t direction;
} nmo_wavesound_state_t;

/**
 * @brief CKMidiSound state
 */
typedef struct nmo_midisound_state {
    nmo_sound_state_t base;
    uint8_t has_midi_file_name;
    char *midi_file_name;
} nmo_midisound_state_t;

/* Serialization entry points */
NMO_API nmo_status_t nmo_sound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sound_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_API nmo_status_t nmo_wavesound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_wavesound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_wavesound_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_API nmo_status_t nmo_midisound_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_midisound_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_midisound_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_DECLARE_OBJECT_SCHEMA(nmo_sound_vtable, nmo_register_sound_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_wavesound_vtable, nmo_register_wavesound_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_midisound_vtable, nmo_register_midisound_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSOUND_SCHEMAS_H */
