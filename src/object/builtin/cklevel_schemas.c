/**
 * @file cklevel_schemas.c
 * @brief CKLevel schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKLevel (level/world container).
 * CKLevel extends CKBeObject and manages scenes, global objects, and execution context.
 * 
 * Based on official Virtools SDK (reference/src/CKLevel.cpp:346-471):
 * - CKLevel::Save writes multiple identifier-based sections
 * - Scene list stored using XObjectPointerArray format
 * - Level scene embedded as sub-chunk
 * - Optional manager activation state
 */

#include "object/nmo_cklevel_schemas.h"
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
#include "core/nmo_guid.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * CKLevel IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKLevel.cpp */
#define CK_STATESAVE_LEVELINACTIVEMAN  0x00002000
#define CK_STATESAVE_LEVELDUPLICATEMAN 0x00004000
#define CK_STATESAVE_LEVELDEFAULTDATA  0x20000000
#define CK_STATESAVE_LEVELSCENE        0x80000000

/* =============================================================================
 * CKLevel DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKLevel state from chunk
 * 
 * Implements the symmetric read operation for CKLevel::Load.
 * Reads scene list, current scene, level scene chunk, and manager state.
 * 
 * Reference: reference/src/CKLevel.cpp:405-471
 * 
 * @param chunk Chunk containing CKLevel data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_result_t nmo_cklevel_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cklevel_state_t *out_state = (nmo_cklevel_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cklevel_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_cklevel_state_t));
    
    /* Deserialize base CKBeObject state first */
    nmo_result_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    /* Section 1: LEVELDEFAULTDATA - Legacy arrays + scene list */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELDEFAULTDATA);
    if (result.code == NMO_OK) {
        /* 1) Legacy CKObjectArray (unused) */
        size_t legacy_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &legacy_count);
        if (result.code != NMO_OK) return result;
        for (size_t i = 0; i < legacy_count; ++i) {
            nmo_object_id_t ignored_id = 0;
            (void)nmo_chunk_read_object_sequence_item(chunk, &ignored_id);
        }

        /* 2) Legacy XObjectPointerArray (empty in modern files) */
        result = nmo_chunk_read_object_sequence_start(chunk, &legacy_count);
        if (result.code != NMO_OK) return result;
        for (size_t i = 0; i < legacy_count; ++i) {
            nmo_object_id_t ignored_id = 0;
            (void)nmo_chunk_read_object_sequence_item(chunk, &ignored_id);
        }

        /* 3) Scene list (XObjectPointerArray::Save) */
        size_t scene_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &scene_count);
        if (result.code != NMO_OK) return result;

        if (scene_count > 0) {
            const uint32_t MAX_SCENES = 10000;
            if (scene_count > MAX_SCENES) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                    NMO_SEVERITY_ERROR, "Scene count exceeds maximum"));
            }

            out_state->scene_count = (uint32_t)scene_count;
            out_state->scene_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena,
                scene_count * sizeof(nmo_object_id_t),
                _Alignof(nmo_object_id_t)
            );

            if (!out_state->scene_ids) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate scene ID array"));
            }

            for (size_t i = 0; i < scene_count; i++) {
                result = nmo_chunk_read_object_sequence_item(chunk, &out_state->scene_ids[i]);
                if (result.code != NMO_OK) {
                    out_state->scene_count = (uint32_t)i;
                    break;
                }
            }
        }
    }

    /* Section 2: LEVELSCENE - Current scene + level scene */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELSCENE);
    if (result.code == NMO_OK) {
        /* Read current scene ID */
        result = nmo_chunk_read_object_id(chunk, &out_state->current_scene_id);
        if (result.code != NMO_OK) return result;

        /* Read level scene ID */
        result = nmo_chunk_read_object_id(chunk, &out_state->level_scene_id);
        if (result.code != NMO_OK) return result;

        /* Read level scene sub-chunk */
        result = nmo_chunk_read_sub_chunk(chunk, &out_state->level_scene_chunk);
        if (result.code != NMO_OK) {
            /* Sub-chunk missing is not fatal - level scene might be simple */
            out_state->level_scene_chunk = NULL;
        }
    }

    /* Section 3: LEVELINACTIVEMAN (optional) - Inactive manager GUIDs */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELINACTIVEMAN);
    if (result.code == NMO_OK) {
        /* Read the identifier size to calculate GUID count */
        /* Note: SeekIdentifierAndReturnSize is not available in chunk API,
         * so we read GUIDs until we hit the next identifier or end of chunk */
        size_t start_pos = nmo_chunk_get_position(chunk);
        uint32_t guid_count = 0;
        
        /* Count GUIDs by reading them until we fail */
        nmo_guid_t temp_guid;
        nmo_result_t guid_result;
        for (;;) {
            guid_result = nmo_chunk_read_guid(chunk, &temp_guid);
            if (guid_result.code != NMO_OK) break;
            guid_count++;
            /* Safety limit */
            if (guid_count > 1000) break;
        }

        if (guid_count > 0) {
            out_state->inactive_manager_count = guid_count;
            out_state->inactive_manager_guids = (nmo_guid_t *)nmo_arena_alloc(
                arena,
                guid_count * sizeof(nmo_guid_t),
                _Alignof(nmo_guid_t)
            );

            if (!out_state->inactive_manager_guids) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate inactive manager GUID array"));
            }

            /* Re-read GUIDs from start position */
            result = nmo_chunk_goto(chunk, start_pos);
            if (result.code != NMO_OK) return result;

            for (uint32_t i = 0; i < guid_count; i++) {
                result = nmo_chunk_read_guid(chunk, &out_state->inactive_manager_guids[i]);
                if (result.code != NMO_OK) {
                    out_state->inactive_manager_count = i;
                    break;
                }
            }
        }

        /* Section 4: LEVELDUPLICATEMAN (optional) - Duplicate manager names */
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELDUPLICATEMAN);
        if (result.code == NMO_OK) {
            /* Count strings first (NULL-terminated list) */
            size_t str_start_pos = nmo_chunk_get_position(chunk);
            uint32_t name_count = 0;
            
            char *temp_name;
            for (;;) {
                size_t len = nmo_chunk_read_string(chunk, &temp_name);
                if (len == 0 || !temp_name) break;
                name_count++;
                /* Safety limit */
                if (name_count > 1000) break;
            }

            if (name_count > 0) {
                /* Allocate array for pointers */
                out_state->duplicate_manager_count = name_count;
                out_state->duplicate_manager_names = (char **)nmo_arena_alloc(
                    arena,
                    name_count * sizeof(char *),
                    _Alignof(char *)
                );

                if (!out_state->duplicate_manager_names) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate manager name array"));
                }

                /* Re-read strings */
                result = nmo_chunk_goto(chunk, str_start_pos);
                if (result.code != NMO_OK) return result;

                for (uint32_t i = 0; i < name_count; i++) {
                    size_t len = nmo_chunk_read_string(chunk, &out_state->duplicate_manager_names[i]);
                    if (len == 0 || !out_state->duplicate_manager_names[i]) {
                        out_state->duplicate_manager_count = i;
                        break;
                    }
                }
            }
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKLevel SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKLevel state to chunk
 * 
 * Implements the symmetric write operation for CKLevel::Save.
 * Writes scene list, current scene, level scene chunk, and manager state.
 * 
 * Reference: reference/src/CKLevel.cpp:346-403
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_result_t nmo_cklevel_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cklevel_state_t *in_state = (const nmo_cklevel_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cklevel_serialize"));
    }

    /* Write base class (CKBeObject) data */
    nmo_result_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    /* Section 1: LEVELDEFAULTDATA */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELDEFAULTDATA);
    if (result.code != NMO_OK) return result;

    /* 1) Legacy CKObjectArray (unused) */
    result = nmo_chunk_write_object_sequence_start(out_chunk, 0);
    if (result.code != NMO_OK) return result;

    /* 2) Legacy XObjectPointerArray (empty in modern files) */
    result = nmo_chunk_write_object_sequence_start(out_chunk, 0);
    if (result.code != NMO_OK) return result;

    /* 3) Scene list */
    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->scene_count);
    if (result.code != NMO_OK) return result;

    for (uint32_t i = 0; i < in_state->scene_count; i++) {
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->scene_ids[i]);
        if (result.code != NMO_OK) return result;
    }

    /* Section 2: LEVELSCENE */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELSCENE);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->current_scene_id);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->level_scene_id);
    if (result.code != NMO_OK) return result;

    /* Write level scene sub-chunk */
    if (in_state->level_scene_chunk) {
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->level_scene_chunk);
        if (result.code != NMO_OK) return result;
    }

    /* Section 3: LEVELINACTIVEMAN (optional) */
    if (in_state->inactive_manager_count > 0 && in_state->inactive_manager_guids) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELINACTIVEMAN);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->inactive_manager_count; i++) {
            result = nmo_chunk_write_guid(out_chunk, in_state->inactive_manager_guids[i]);
            if (result.code != NMO_OK) return result;
        }

        /* Section 4: LEVELDUPLICATEMAN (optional) */
        if (in_state->duplicate_manager_count > 0 && in_state->duplicate_manager_names) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELDUPLICATEMAN);
            if (result.code != NMO_OK) return result;

            for (uint32_t i = 0; i < in_state->duplicate_manager_count; i++) {
                result = nmo_chunk_write_string(out_chunk, in_state->duplicate_manager_names[i]);
                if (result.code != NMO_OK) return result;
            }

            /* Write NULL terminator */
            result = nmo_chunk_write_string(out_chunk, NULL);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cklevel,
    nmo_cklevel_state_t,
    nmo_cklevel_serialize,
    nmo_cklevel_deserialize,
    NMO_GUID_CKLEVEL,
    "CKLevel",
    NMO_CID_LEVEL,
    NMO_GUID_CKBEOBJECT
)

