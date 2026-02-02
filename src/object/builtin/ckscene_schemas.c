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

#include "object/nmo_ckscene_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
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

/* Scene object flags (CKEnums.h) */
#define CK_SCENEOBJECT_START_ACTIVATE   0x0001
#define CK_SCENEOBJECT_ACTIVE           0x0008
#define CK_SCENEOBJECT_START_DEACTIVATE 0x0010
#define CK_SCENEOBJECT_START_RESET      0x0040

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
nmo_result_t nmo_ckscene_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckscene_state_t *out_state = (nmo_ckscene_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckscene_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckscene_state_t));
    
    /* Deserialize base CKBeObject state first */
    nmo_result_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    /* Section 1: SCENENEWDATA - Level + scene objects */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENENEWDATA);
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
                result = nmo_chunk_read_object_sequence_item(chunk,
                    &out_state->object_descs[i].object_id);
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
                if (nmo_chunk_get_data_version(chunk) >= 8) {
                    out_state->object_descs[i].flags = flags;
                } else {
                    uint32_t converted = flags & CK_SCENEOBJECT_ACTIVE;
                    if (flags & 2) {
                        converted |= CK_SCENEOBJECT_START_RESET;
                    }
                    if (flags & 1) {
                        converted |= CK_SCENEOBJECT_START_ACTIVATE;
                    } else {
                        converted |= CK_SCENEOBJECT_START_DEACTIVATE;
                    }
                    out_state->object_descs[i].flags = converted;
                }
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
nmo_result_t nmo_ckscene_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckscene_state_t *in_state = (const nmo_ckscene_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckscene_serialize"));
    }

    /* Write base class (CKBeObject) data */
    nmo_result_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    /* Section 1: SCENENEWDATA */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCENENEWDATA);
    if (result.code != NMO_OK) return result;

    /* Write level ID */
    result = nmo_chunk_write_object_id(out_chunk, in_state->level_id);
    if (result.code != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->object_count);
    if (result.code != NMO_OK) return result;

    if (in_state->object_count > 0) {
        /* Write object ID sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->object_count);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->object_count; i++) {
            result = nmo_chunk_write_object_sequence_item(out_chunk,
                in_state->object_descs[i].object_id);
            if (result.code != NMO_OK) return result;
        }

        /* Write sub-chunk sequence (initial values + reserved NULLs) */
        result = nmo_chunk_start_sub_chunk_sequence(out_chunk, in_state->object_count * 2);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->object_count; i++) {
            /* Write initial value chunk */
            if (in_state->object_descs[i].initial_value) {
                result = nmo_chunk_write_sub_chunk_sequence(
                    out_chunk,
                    in_state->object_descs[i].initial_value);
                if (result.code != NMO_OK) return result;
            } else {
                /* Write NULL chunk */
                result = nmo_chunk_write_sub_chunk_sequence(out_chunk, NULL);
                if (result.code != NMO_OK) return result;
            }

            /* Write reserved NULL chunk */
            result = nmo_chunk_write_sub_chunk_sequence(out_chunk, NULL);
            if (result.code != NMO_OK) return result;
        }

        /* Write object flags */
        for (uint32_t i = 0; i < in_state->object_count; i++) {
            result = nmo_chunk_write_dword(out_chunk, in_state->object_descs[i].flags);
            if (result.code != NMO_OK) return result;
        }
    }

    /* Section 2: SCENELAUNCHED */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCENELAUNCHED);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->environment_settings);
    if (result.code != NMO_OK) return result;

    /* Section 3: SCENERENDERSETTINGS */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCENERENDERSETTINGS);
    if (result.code != NMO_OK) return result;

    /* Background and ambient */
    result = nmo_chunk_write_dword(out_chunk, in_state->background_color);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->ambient_light_color);
    if (result.code != NMO_OK) return result;

    /* Fog settings */
    result = nmo_chunk_write_dword(out_chunk, in_state->fog_mode);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->fog_color);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->fog_start);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->fog_end);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->fog_density);
    if (result.code != NMO_OK) return result;

    /* Scene references */
    result = nmo_chunk_write_object_id(out_chunk, in_state->background_texture_id);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->starting_camera_id);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckscene,
    nmo_ckscene_state_t,
    nmo_ckscene_serialize,
    nmo_ckscene_deserialize,
    NMO_GUID_CKSCENE,
    "CKScene",
    NMO_CID_SCENE,
    NMO_GUID_CKBEOBJECT
)

