/**
 * @file ckscene_schemas.c
 * @brief CKScene schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKScene (scene container).
 * CKScene extends CKBeObject and manages scene objects with initial states.
 * 
 * Based on official Virtools SDK (reference/src/CKScene.cpp:692-890):
 * - CKScene::Save writes object descriptors with initial value chunks
 * - Each object has activation/reset flags
 * - Rendering settings stored in separate identifier section
 */

#include "schema/nmo_ckscene_schemas.h"
#include "schema/nmo_ckbeobject_schemas.h"
#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * CKScene IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKScene.cpp */
#define CK_STATESAVE_SCENENEWDATA        0x00000001
#define CK_STATESAVE_SCENELAUNCHED       0x00000002
#define CK_STATESAVE_SCENERENDERSETTINGS 0x00000004

/* =============================================================================
 * CKScene DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKScene state from chunk
 * 
 * Implements the symmetric read operation for CKScene::Load.
 * Reads scene objects with initial states and rendering settings.
 * 
 * Reference: reference/src/CKScene.cpp:776-890
 * 
 * @param chunk Chunk containing CKScene data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckscene_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckscene_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckscene_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckscene_state_t));
    
    /* Deserialize base CKBeObject state first */
    nmo_ckbeobject_deserialize_fn parent_deserialize = nmo_get_ckbeobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }

    /* Section 1: SCENENEWDATA - Level + scene objects */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENENEWDATA);
    if (result.code == NMO_OK) {
        /* Read level ID */
        result = nmo_chunk_read_object_id(chunk, &out_state->level_id);
        if (result.code != NMO_OK) return result;

        /* Read object count */
        int32_t desc_count;
        result = nmo_chunk_read_int(chunk, &desc_count);
        if (result.code != NMO_OK) return result;

        if (desc_count > 0) {
            const uint32_t MAX_SCENE_OBJECTS = 100000;
            if ((uint32_t)desc_count > MAX_SCENE_OBJECTS) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                    NMO_SEVERITY_ERROR, "Scene object count exceeds maximum"));
            }

            out_state->object_count = (uint32_t)desc_count;
            out_state->object_descs = (nmo_scene_object_desc_t *)nmo_arena_alloc(
                arena,
                desc_count * sizeof(nmo_scene_object_desc_t),
                _Alignof(nmo_scene_object_desc_t)
            );

            if (!out_state->object_descs) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate scene object descriptor array"));
            }

            /* Initialize descriptors */
            for (int32_t i = 0; i < desc_count; i++) {
                out_state->object_descs[i].object_id = 0;
                out_state->object_descs[i].initial_value = NULL;
                out_state->object_descs[i].flags = 0;
            }

            /* Read object ID sequence */
            result = nmo_chunk_read_object_sequence_start(chunk, NULL);
            if (result.code != NMO_OK) return result;

            for (int32_t i = 0; i < desc_count; i++) {
                result = nmo_chunk_read_object_id(chunk, &out_state->object_descs[i].object_id);
                if (result.code != NMO_OK) {
                    out_state->object_count = i;
                    break;
                }
            }

            /* Read sub-chunk sequence (initial values + reserved) */
            size_t sub_chunk_count;
            result = nmo_chunk_start_read_sub_chunk_sequence(chunk, &sub_chunk_count);
            if (result.code != NMO_OK) {
                /* Sub-chunk sequence missing - objects have no initial state */
            } else {
                /* Read pairs of chunks: initial value + reserved (NULL) */
                for (int32_t i = 0; i < desc_count && (size_t)(i * 2) < sub_chunk_count; i++) {
                    /* Read initial value chunk */
                    result = nmo_chunk_read_sub_chunk(chunk, &out_state->object_descs[i].initial_value);
                    if (result.code != NMO_OK) {
                        out_state->object_descs[i].initial_value = NULL;
                        /* Continue reading - missing initial value is valid */
                    }

                    /* Read reserved chunk (always NULL, discard) */
                    nmo_chunk_t *reserved_chunk = NULL;
                    result = nmo_chunk_read_sub_chunk(chunk, &reserved_chunk);
                    /* Ignore result - reserved chunk is expected to be NULL */
                    (void)reserved_chunk;
                }
            }

            /* Read object flags */
            for (int32_t i = 0; i < desc_count; i++) {
                uint32_t flags;
                result = nmo_chunk_read_dword(chunk, &flags);
                if (result.code != NMO_OK) {
                    break;
                }
                out_state->object_descs[i].flags = flags;
            }
        }
    }

    /* Section 2: SCENELAUNCHED - Environment settings */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENELAUNCHED);
    if (result.code == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->environment_settings);
        if (result.code != NMO_OK) {
            out_state->environment_settings = 0;
        }
    }

    /* Section 3: SCENERENDERSETTINGS - Rendering configuration */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENERENDERSETTINGS);
    if (result.code == NMO_OK) {
        /* Background and ambient */
        result = nmo_chunk_read_dword(chunk, &out_state->background_color);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_dword(chunk, &out_state->ambient_light_color);
        if (result.code != NMO_OK) return result;

        /* Fog settings */
        result = nmo_chunk_read_dword(chunk, &out_state->fog_mode);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_dword(chunk, &out_state->fog_color);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_float(chunk, &out_state->fog_start);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_float(chunk, &out_state->fog_end);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_float(chunk, &out_state->fog_density);
        if (result.code != NMO_OK) return result;

        /* Scene references */
        result = nmo_chunk_read_object_id(chunk, &out_state->background_texture_id);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_object_id(chunk, &out_state->starting_camera_id);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKScene SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKScene state to chunk
 * 
 * Implements the symmetric write operation for CKScene::Save.
 * Writes scene objects with initial states and rendering settings.
 * 
 * Reference: reference/src/CKScene.cpp:692-775
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckscene_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckscene_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckscene_serialize"));
    }

    /* Write base class (CKBeObject) data */
    nmo_ckbeobject_serialize_fn parent_serialize = nmo_get_ckbeobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(chunk, &state->base);
        if (result.code != NMO_OK) return result;
    }

    /* Section 1: SCENENEWDATA */
    nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SCENENEWDATA);
    if (result.code != NMO_OK) return result;

    /* Write level ID */
    result = nmo_chunk_write_object_id(chunk, state->level_id);
    if (result.code != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(chunk, (int32_t)state->object_count);
    if (result.code != NMO_OK) return result;

    if (state->object_count > 0) {
        /* Write object ID sequence */
        result = nmo_chunk_write_object_sequence_start(chunk, state->object_count);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < state->object_count; i++) {
            result = nmo_chunk_write_object_id(chunk, state->object_descs[i].object_id);
            if (result.code != NMO_OK) return result;
        }

        /* Write sub-chunk sequence (initial values + reserved NULLs) */
        result = nmo_chunk_start_sub_chunk_sequence(chunk, state->object_count * 2);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < state->object_count; i++) {
            /* Write initial value chunk */
            if (state->object_descs[i].initial_value) {
                result = nmo_chunk_write_sub_chunk(chunk, state->object_descs[i].initial_value);
                if (result.code != NMO_OK) return result;
            } else {
                /* Write NULL chunk */
                result = nmo_chunk_write_sub_chunk(chunk, NULL);
                if (result.code != NMO_OK) return result;
            }

            /* Write reserved NULL chunk */
            result = nmo_chunk_write_sub_chunk(chunk, NULL);
            if (result.code != NMO_OK) return result;
        }

        /* Write object flags */
        for (uint32_t i = 0; i < state->object_count; i++) {
            result = nmo_chunk_write_dword(chunk, state->object_descs[i].flags);
            if (result.code != NMO_OK) return result;
        }
    }

    /* Section 2: SCENELAUNCHED */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SCENELAUNCHED);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, state->environment_settings);
    if (result.code != NMO_OK) return result;

    /* Section 3: SCENERENDERSETTINGS */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SCENERENDERSETTINGS);
    if (result.code != NMO_OK) return result;

    /* Background and ambient */
    result = nmo_chunk_write_dword(chunk, state->background_color);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, state->ambient_light_color);
    if (result.code != NMO_OK) return result;

    /* Fog settings */
    result = nmo_chunk_write_dword(chunk, state->fog_mode);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, state->fog_color);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, state->fog_start);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, state->fog_end);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(chunk, state->fog_density);
    if (result.code != NMO_OK) return result;

    /* Scene references */
    result = nmo_chunk_write_object_id(chunk, state->background_texture_id);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(chunk, state->starting_camera_id);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKScene schema types
 * 
 * Creates schema descriptors for CKScene state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckscene_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckscene_schemas"));
    }

    /* Schema will be registered when schema builder is fully implemented */
    /* For now, just store the function pointers in the registry */
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKScene
 * 
 * @return Deserialize function pointer
 */
nmo_ckscene_deserialize_fn nmo_get_ckscene_deserialize(void)
{
    return nmo_ckscene_deserialize;
}

/**
 * @brief Get the serialize function for CKScene
 * 
 * @return Serialize function pointer
 */
nmo_ckscene_serialize_fn nmo_get_ckscene_serialize(void)
{
    return nmo_ckscene_serialize;
}
