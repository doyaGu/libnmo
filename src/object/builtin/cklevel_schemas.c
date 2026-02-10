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

#include "object/nmo_level_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_beobject_schemas.h"
#include "object/nmo_object_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    level,
    nmo_level_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->scene_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->inactive_manager_guids, sizeof(nmo_guid_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->duplicate_manager_names, sizeof(char *), 0, NULL);
        if (result != NMO_OK) return result;
        nmo_object_array_set_string_lifecycle(&state->duplicate_manager_names);
    } while (0),
    ((void)0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_level_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_level_state_t, base),
                       sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_ARRAY(nmo_level_state_t, scene_ids),
    NMO_FIELD_REF(nmo_level_state_t, current_scene_id),
    NMO_FIELD_REF(nmo_level_state_t, level_scene_id),
    NMO_FIELD_OPT(nmo_level_state_t, level_scene_chunk, CKPGUID_STATECHUNK),
    NMO_FIELD_ARRAY(nmo_level_state_t, inactive_manager_guids, CKPGUID_GUID),
    NMO_FIELD_ARRAY(nmo_level_state_t, duplicate_manager_names, CKPGUID_STRING)
};

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
nmo_status_t nmo_level_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_level_state_t *out_state = (nmo_level_state_t *)instance;
    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_level_deserialize");
    }

    /* Deserialize base CKBeObject state first */
    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Section 1: LEVELDEFAULTDATA - Legacy arrays + scene list */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELDEFAULTDATA);
    if (result == NMO_OK) {
        /* 1) Legacy CKObjectArray (unused) */
        size_t legacy_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &legacy_count);
        if (result != NMO_OK) return result;
        for (size_t i = 0; i < legacy_count; ++i) {
            nmo_object_id_t ignored_id = 0;
            (void)nmo_chunk_read_object_sequence_item(chunk, &ignored_id);
        }

        /* 2) Legacy XObjectPointerArray (empty in modern files) */
        result = nmo_chunk_read_object_sequence_start(chunk, &legacy_count);
        if (result != NMO_OK) return result;
        for (size_t i = 0; i < legacy_count; ++i) {
            nmo_object_id_t ignored_id = 0;
            (void)nmo_chunk_read_object_sequence_item(chunk, &ignored_id);
        }

        /* 3) Scene list (XObjectPointerArray::Save) */
        size_t scene_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &scene_count);
        if (result != NMO_OK) return result;

        nmo_array_clear(&out_state->scene_ids);
        if (scene_count > 0) {
            const uint32_t MAX_SCENES = 10000;
            if (scene_count > MAX_SCENES) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Scene count exceeds maximum");
            }

            result = nmo_array_reserve(&out_state->scene_ids, scene_count);
            if (result != NMO_OK) return result;

            nmo_object_id_t *scene_ids = NULL;
            result = nmo_array_extend(&out_state->scene_ids, scene_count, (void **)&scene_ids);
            if (result != NMO_OK) return result;

            for (size_t i = 0; i < scene_count; i++) {
                result = nmo_chunk_read_object_sequence_item(chunk, &scene_ids[i]);
                if (result != NMO_OK) {
                    out_state->scene_ids.count = i;
                    break;
                }
            }
        }
    }

    /* Section 2: LEVELSCENE - Current scene + level scene */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELSCENE);
    if (result == NMO_OK) {
        /* Read current scene ID */
        result = nmo_chunk_read_object_id(chunk, &out_state->current_scene_id);
        if (result != NMO_OK) return result;

        /* Read level scene ID */
        result = nmo_chunk_read_object_id(chunk, &out_state->level_scene_id);
        if (result != NMO_OK) return result;

        /* Read level scene sub-chunk */
        result = nmo_chunk_read_sub_chunk(chunk, &out_state->level_scene_chunk);
        if (result != NMO_OK) {
            /* Sub-chunk missing is not fatal - level scene might be simple */
            out_state->level_scene_chunk = NULL;
        }
    }

    /* Section 3: LEVELINACTIVEMAN (optional) - Inactive manager GUIDs */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELINACTIVEMAN);
    if (result == NMO_OK) {
        /* Read the identifier size to calculate GUID count */
        /* Note: SeekIdentifierAndReturnSize is not available in chunk API,
         * so we read GUIDs until we hit the next identifier or end of chunk */
        size_t start_pos = nmo_chunk_get_position(chunk);
        uint32_t guid_count = 0;
        
        /* Count GUIDs by reading them until we fail */
        nmo_guid_t temp_guid;
        nmo_status_t guid_result;
        for (;;) {
            guid_result = nmo_chunk_read_guid(chunk, &temp_guid);
            if (guid_result != NMO_OK) break;
            guid_count++;
            /* Safety limit */
            if (guid_count > 1000) break;
        }

        nmo_array_clear(&out_state->inactive_manager_guids);
        if (guid_count > 0) {
            result = nmo_array_reserve(&out_state->inactive_manager_guids, guid_count);
            if (result != NMO_OK) return result;

            nmo_guid_t *guids = NULL;
            result = nmo_array_extend(&out_state->inactive_manager_guids, guid_count, (void **)&guids);
            if (result != NMO_OK) return result;

            /* Re-read GUIDs from start position */
            result = nmo_chunk_goto(chunk, start_pos);
            if (result != NMO_OK) return result;

            for (uint32_t i = 0; i < guid_count; i++) {
                result = nmo_chunk_read_guid(chunk, &guids[i]);
                if (result != NMO_OK) {
                    out_state->inactive_manager_guids.count = i;
                    break;
                }
            }
        }

        /* Section 4: LEVELDUPLICATEMAN (optional) - Duplicate manager names */
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELDUPLICATEMAN);
        if (result == NMO_OK) {
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

            nmo_array_clear(&out_state->duplicate_manager_names);
            if (name_count > 0) {
                result = nmo_array_reserve(&out_state->duplicate_manager_names, name_count);
                if (result != NMO_OK) return result;

                char **names = NULL;
                result = nmo_array_extend(&out_state->duplicate_manager_names, name_count, (void **)&names);
                if (result != NMO_OK) return result;

                /* Re-read strings */
                result = nmo_chunk_goto(chunk, str_start_pos);
                if (result != NMO_OK) return result;

                for (uint32_t i = 0; i < name_count; i++) {
                    size_t len = nmo_chunk_read_string(chunk, &names[i]);
                    if (len == 0 || !names[i]) {
                        out_state->duplicate_manager_names.count = i;
                        break;
                    }
                }
            }
        }
    }

    NMO_RETURN_OK();
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
nmo_status_t nmo_level_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_level_state_t *in_state = (const nmo_level_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_level_serialize");
    }

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Section 1: LEVELDEFAULTDATA */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELDEFAULTDATA);
    if (result != NMO_OK) return result;

    /* 1) Legacy CKObjectArray (unused) */
    result = nmo_chunk_write_object_sequence_start(out_chunk, 0);
    if (result != NMO_OK) return result;

    /* 2) Legacy XObjectPointerArray (empty in modern files) */
    result = nmo_chunk_write_object_sequence_start(out_chunk, 0);
    if (result != NMO_OK) return result;

    /* 3) Scene list */
    result = nmo_chunk_write_object_sequence_start(out_chunk, (uint32_t)in_state->scene_ids.count);
    if (result != NMO_OK) return result;

    const nmo_object_id_t *scene_ids = NMO_ARRAY_DATA(nmo_object_id_t, &in_state->scene_ids);
    for (uint32_t i = 0; i < in_state->scene_ids.count; i++) {
        result = nmo_chunk_write_object_sequence_item(out_chunk, scene_ids[i]);
        if (result != NMO_OK) return result;
    }

    /* Section 2: LEVELSCENE */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELSCENE);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->current_scene_id);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->level_scene_id);
    if (result != NMO_OK) return result;

    /* Write level scene sub-chunk */
    if (in_state->level_scene_chunk) {
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->level_scene_chunk);
        if (result != NMO_OK) return result;
    }

    /* Section 3: LEVELINACTIVEMAN (optional) */
    if (in_state->inactive_manager_guids.count > 0 && in_state->inactive_manager_guids.data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELINACTIVEMAN);
        if (result != NMO_OK) return result;

        const nmo_guid_t *inactive_guids = NMO_ARRAY_DATA(nmo_guid_t, &in_state->inactive_manager_guids);
        for (uint32_t i = 0; i < in_state->inactive_manager_guids.count; i++) {
            result = nmo_chunk_write_guid(out_chunk, inactive_guids[i]);
            if (result != NMO_OK) return result;
        }

        /* Section 4: LEVELDUPLICATEMAN (optional) */
        if (in_state->duplicate_manager_names.count > 0 && in_state->duplicate_manager_names.data) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELDUPLICATEMAN);
            if (result != NMO_OK) return result;

            const char *const *dup_names = NMO_ARRAY_DATA(const char *, &in_state->duplicate_manager_names);
            for (uint32_t i = 0; i < in_state->duplicate_manager_names.count; i++) {
                result = nmo_chunk_write_string(out_chunk, dup_names[i]);
                if (result != NMO_OK) return result;
            }

            /* Write NULL terminator */
            result = nmo_chunk_write_string(out_chunk, NULL);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_level_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_level_state_t *s = src;
    nmo_level_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.base.raw_tail,
                                              s->base.base.raw_tail, s->base.base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.script_ids,
                                              s->base.script_ids, sizeof(nmo_object_id_t), s->base.script_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.attribute_parameter_ids,
                                              s->base.attribute_parameter_ids, sizeof(nmo_object_id_t), s->base.attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.attribute_types,
                                              s->base.attribute_types, sizeof(uint32_t), s->base.attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk_array(arena, &d->base.attribute_chunks,
                                                    s->base.attribute_chunks, s->base.attribute_chunk_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.legacy_attributes_raw,
                                              s->base.legacy_attributes_raw, s->base.legacy_attributes_size));

    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->scene_ids, &d->scene_ids, &s->scene_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->level_scene_chunk, s->level_scene_chunk));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->inactive_manager_guids, &d->inactive_manager_guids,
                                        &s->inactive_manager_guids.allocator));
    return nmo_object_clone_string_array(arena, &d->duplicate_manager_names,
                                         &s->duplicate_manager_names);
}

static nmo_status_t nmo_level_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_level_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->scene_ids.data, s->scene_ids.count, "scene_ids");
    NMO_VALIDATE_COUNT(s->inactive_manager_guids.data, s->inactive_manager_guids.count,
                       "inactive_manager_guids");
    NMO_VALIDATE_COUNT(s->duplicate_manager_names.data, s->duplicate_manager_names.count,
                       "duplicate_manager_names");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    level,
    nmo_level_state_t,
    nmo_level_serialize,
    nmo_level_deserialize,
    nmo_level_fields,
    CKPGUID_LEVEL,
    "CKLevel",
    NMO_CID_LEVEL,
    CKPGUID_BEOBJECT
)


