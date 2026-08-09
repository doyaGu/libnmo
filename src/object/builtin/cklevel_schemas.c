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

#include "object/builtin/nmo_level_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_level_dispose_state_arrays(nmo_level_state_t *state);
static nmo_status_t nmo_level_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DEFINE_OBJECT_LIFECYCLE(
    level,
    nmo_level_state_t,
    do {
        state->has_inactive_manager_section = 0;
        nmo_status_t result = nmo_beobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        result = nmo_array_init(
            &state->legacy_object_ids, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_level_dispose_state_arrays(state);
            return result;
        }
        result = nmo_array_init(
            &state->legacy_pointer_ids, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_level_dispose_state_arrays(state);
            return result;
        }
        result = nmo_array_init(&state->scene_ids, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_level_dispose_state_arrays(state);
            return result;
        }
        result = nmo_array_init(&state->inactive_manager_guids, sizeof(nmo_guid_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_level_dispose_state_arrays(state);
            return result;
        }
        result = nmo_array_init(&state->duplicate_manager_names, sizeof(char *), 0, NULL);
        if (result != NMO_OK) {
            nmo_level_dispose_state_arrays(state);
            return result;
        }
        nmo_object_array_set_string_lifecycle(&state->duplicate_manager_names);
    } while (0),
    nmo_level_dispose_state_arrays(state))

static void nmo_level_dispose_state_arrays(nmo_level_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->legacy_object_ids);
    nmo_array_dispose(&state->legacy_pointer_ids);
    nmo_array_dispose(&state->scene_ids);
    nmo_array_dispose(&state->inactive_manager_guids);
    nmo_array_dispose(&state->duplicate_manager_names);
    if (state->level_scene_chunk != NULL) {
        nmo_chunk_destroy(state->level_scene_chunk);
        state->level_scene_chunk = NULL;
    }
    nmo_beobject_vtable.destroy(&state->base, NULL, NULL);
}

static size_t nmo_level_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

static const nmo_allocator_t *nmo_level_array_allocator(
    const nmo_array_t *array)
{
    return array->allocator.alloc != NULL ? &array->allocator : NULL;
}

static nmo_status_t nmo_level_init_staged_state(
    nmo_level_state_t *staged,
    const nmo_level_state_t *current)
{
    memset(staged, 0, sizeof(*staged));
    if (current->base.scripts.allocator.alloc != NULL) {
        staged->base.scripts.allocator = current->base.scripts.allocator;
    }
    if (current->base.attributes.allocator.alloc != NULL) {
        staged->base.attributes.allocator = current->base.attributes.allocator;
    }
    if (current->base.legacy_attributes.allocator.alloc != NULL) {
        staged->base.legacy_attributes.allocator =
            current->base.legacy_attributes.allocator;
    }
    nmo_status_t result = nmo_array_init(
        &staged->legacy_object_ids, sizeof(nmo_ref_t), 0,
        nmo_level_array_allocator(&current->legacy_object_ids));
    if (result != NMO_OK) return result;
    result = nmo_array_init(
        &staged->legacy_pointer_ids, sizeof(nmo_ref_t), 0,
        nmo_level_array_allocator(&current->legacy_pointer_ids));
    if (result != NMO_OK) {
        nmo_level_dispose_state_arrays(staged);
        return result;
    }
    result = nmo_array_init(
        &staged->scene_ids, sizeof(nmo_ref_t), 0,
        nmo_level_array_allocator(&current->scene_ids));
    if (result != NMO_OK) {
        nmo_level_dispose_state_arrays(staged);
        return result;
    }
    result = nmo_array_init(
        &staged->inactive_manager_guids, sizeof(nmo_guid_t), 0,
        nmo_level_array_allocator(&current->inactive_manager_guids));
    if (result != NMO_OK) {
        nmo_level_dispose_state_arrays(staged);
        return result;
    }
    result = nmo_array_init(
        &staged->duplicate_manager_names, sizeof(char *), 0,
        nmo_level_array_allocator(&current->duplicate_manager_names));
    if (result != NMO_OK) {
        nmo_level_dispose_state_arrays(staged);
        return result;
    }
    nmo_object_array_set_string_lifecycle(
        &staged->duplicate_manager_names);
    return NMO_OK;
}

static nmo_status_t nmo_chunk_identifier_payload_size_bytes(nmo_chunk_t *chunk, size_t *out_size) {
    if (chunk == NULL || out_size == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to identifier size helper");
    }

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    if (state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Chunk parser state not initialized");
    }

    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    const size_t data_count = chunk->data.count;
    const size_t start_pos = state->current_pos;
    size_t end_pos = data_count;

    if (state->prev_identifier_pos + 1 < data_count) {
        const uint32_t next_pos = data[state->prev_identifier_pos + 1];
        if (next_pos != 0) {
            if (next_pos > data_count || next_pos < start_pos) {
                return NMO_ERR_INVALID_FORMAT;
            }
            end_pos = next_pos;
        }
    }

    if (end_pos < start_pos) {
        end_pos = start_pos;
    }

    *out_size = (end_pos - start_pos) * sizeof(uint32_t);
    return NMO_OK;
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_level_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_level_state_t, base),
                       sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_level_state_t, legacy_object_ids),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_level_state_t, legacy_pointer_ids),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_level_state_t, scene_ids),
    NMO_FIELD_REF_VALUE(nmo_level_state_t, current_scene),
    NMO_FIELD_REF_VALUE(nmo_level_state_t, level_scene),
    NMO_FIELD_OPT(nmo_level_state_t, level_scene_chunk, CKPGUID_STATECHUNK),
    NMO_FIELD(nmo_level_state_t, has_inactive_manager_section, CKPGUID_UINT8),
    NMO_FIELD_ARRAY(nmo_level_state_t, inactive_manager_guids, CKPGUID_GUID),
    NMO_FIELD_ARRAY(nmo_level_state_t, duplicate_manager_names, CKPGUID_STRING)
};

static nmo_status_t nmo_level_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t nmo_level_read_ref_sequence(
    nmo_chunk_t *chunk,
    const nmo_allocator_t *allocator,
    nmo_array_t *out_refs)
{
    size_t count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_start(
        chunk, &count));
    if (count > SIZE_MAX / sizeof(nmo_ref_t)) {
        return NMO_ERR_NOMEM;
    }
    if (count > nmo_level_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_array_t refs;
    nmo_status_t result = nmo_array_init(
        &refs, sizeof(nmo_ref_t), count, allocator);
    if (result != NMO_OK) return result;
    nmo_ref_t *items = NULL;
    result = nmo_array_extend(&refs, count, (void **)&items);
    if (result != NMO_OK) {
        nmo_array_dispose(&refs);
        return result;
    }
    for (size_t i = 0; i < count; ++i) {
        result = nmo_ref_read(chunk, &items[i]);
        if (result != NMO_OK) {
            nmo_array_dispose(&refs);
            return result;
        }
    }
    *out_refs = refs;
    return NMO_OK;
}

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
static nmo_status_t nmo_level_deserialize_internal(
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

    nmo_deserialize_context_t *deser_ctx = nmo_deserialize_context_get(context);
    const bool is_file = ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (deser_ctx != NULL && (deser_ctx->flags & NMO_DESER_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        NMO_RETURN_OK();
    }

    out_state->has_inactive_manager_section = 0;

    /* Section 1: LEVELDEFAULTDATA - Legacy arrays + scene list */
    size_t default_section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_LEVELDEFAULTDATA, &default_section_dwords);
    if (result == NMO_OK) {
        if (default_section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t default_section_end =
            nmo_chunk_get_position(chunk) + default_section_dwords;
        nmo_array_t legacy_object_ids = {0};
        nmo_array_t legacy_pointer_ids = {0};
        nmo_array_t scene_ids = {0};
        result = nmo_level_read_ref_sequence(
            chunk,
            nmo_level_array_allocator(&out_state->legacy_object_ids),
            &legacy_object_ids);
        if (result != NMO_OK) goto default_data_fail;
        result = nmo_level_read_ref_sequence(
            chunk,
            nmo_level_array_allocator(&out_state->legacy_pointer_ids),
            &legacy_pointer_ids);
        if (result != NMO_OK) goto default_data_fail;
        result = nmo_level_read_ref_sequence(
            chunk, nmo_level_array_allocator(&out_state->scene_ids),
            &scene_ids);
        if (result != NMO_OK) goto default_data_fail;
        if (nmo_chunk_get_position(chunk) > default_section_end) {
            result = NMO_ERR_TRUNCATED_CHUNK;
            goto default_data_fail;
        }
        if (nmo_chunk_get_position(chunk) != default_section_end) {
            result = NMO_ERR_INVALID_FORMAT;
            goto default_data_fail;
        }

        nmo_ref_t *scenes = NMO_ARRAY_DATA(nmo_ref_t, &scene_ids);
        for (size_t i = 0; i < scene_ids.count; ++i) {
            nmo_ref_check_class(
                &scenes[i],
                (const nmo_object_repository_t *)
                    nmo_deserialize_context_get_repository(context),
                nmo_deserialize_context_get_type_registry(context),
                NMO_CID_SCENE);
        }

        nmo_array_t old_legacy_object_ids = out_state->legacy_object_ids;
        nmo_array_t old_legacy_pointer_ids = out_state->legacy_pointer_ids;
        nmo_array_t old_scene_ids = out_state->scene_ids;
        out_state->legacy_object_ids = legacy_object_ids;
        out_state->legacy_pointer_ids = legacy_pointer_ids;
        out_state->scene_ids = scene_ids;
        legacy_object_ids = old_legacy_object_ids;
        legacy_pointer_ids = old_legacy_pointer_ids;
        scene_ids = old_scene_ids;
        nmo_array_dispose(&legacy_object_ids);
        nmo_array_dispose(&legacy_pointer_ids);
        nmo_array_dispose(&scene_ids);
        goto default_data_done;

default_data_fail:
        nmo_array_dispose(&legacy_object_ids);
        nmo_array_dispose(&legacy_pointer_ids);
        nmo_array_dispose(&scene_ids);
        return result;
default_data_done:;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Section 2: LEVELSCENE - Current scene + level scene */
    size_t scene_section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_LEVELSCENE, &scene_section_dwords);
    if (result == NMO_OK) {
        if (scene_section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t scene_section_end =
            nmo_chunk_get_position(chunk) + scene_section_dwords;
        nmo_ref_t current_scene = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        nmo_ref_t level_scene = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        nmo_chunk_t *level_scene_chunk = NULL;

        /* Read current scene ID */
        result = nmo_ref_read(chunk, &current_scene);
        if (result != NMO_OK) return result;

        /* Read level scene ID */
        result = nmo_ref_read(chunk, &level_scene);
        if (result != NMO_OK) return result;

        /* Read level scene sub-chunk */
        result = nmo_chunk_read_sub_chunk(chunk, &level_scene_chunk);
        if (result != NMO_OK) return result;
        if (nmo_chunk_get_position(chunk) > scene_section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < scene_section_end) {
            return NMO_ERR_INVALID_FORMAT;
        }

        nmo_ref_check_class(
            &current_scene,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_SCENE);
        nmo_ref_check_class(
            &level_scene,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_SCENE);
        out_state->current_scene = current_scene;
        out_state->level_scene = level_scene;
        out_state->level_scene_chunk = level_scene_chunk;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Section 3: LEVELINACTIVEMAN (optional) - Inactive manager GUIDs */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELINACTIVEMAN);
    if (result == NMO_OK) {
        size_t section_size_bytes = 0;
        result = nmo_chunk_identifier_payload_size_bytes(chunk, &section_size_bytes);
        if (result != NMO_OK) return result;
        if (section_size_bytes % sizeof(nmo_guid_t) != 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Inactive manager section has a partial GUID");
        }

        const uint32_t guid_count =
            (uint32_t)(section_size_bytes / sizeof(nmo_guid_t));

        nmo_array_t inactive_guids;
        result = nmo_array_init(&inactive_guids, sizeof(nmo_guid_t), guid_count,
                                &out_state->inactive_manager_guids.allocator);
        if (result != NMO_OK) return result;
        if (guid_count > 0) {
            nmo_guid_t *guids = NULL;
            result = nmo_array_extend(&inactive_guids, guid_count, (void **)&guids);
            if (result != NMO_OK) {
                nmo_array_dispose(&inactive_guids);
                return result;
            }

            for (uint32_t i = 0; i < guid_count; i++) {
                result = nmo_chunk_read_guid(chunk, &guids[i]);
                if (result != NMO_OK) {
                    nmo_array_dispose(&inactive_guids);
                    return result;
                }
            }
        }

        /* Section 4: LEVELDUPLICATEMAN (optional) - Duplicate manager names */
        size_t duplicate_section_dwords = 0u;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_LEVELDUPLICATEMAN,
            &duplicate_section_dwords);
        if (result == NMO_OK) {
            const size_t duplicate_section_end =
                nmo_chunk_get_position(chunk) + duplicate_section_dwords;
            nmo_array_t duplicate_names;
            result = nmo_array_init(
                &duplicate_names, sizeof(char *), 0,
                &out_state->duplicate_manager_names.allocator);
            if (result != NMO_OK) {
                nmo_array_dispose(&inactive_guids);
                return result;
            }
            nmo_array_set_lifecycle(
                &duplicate_names, &out_state->duplicate_manager_names.lifecycle);
            for (;;) {
                if (nmo_chunk_get_position(chunk) >= duplicate_section_end) {
                    nmo_array_dispose(&duplicate_names);
                    nmo_array_dispose(&inactive_guids);
                    return NMO_ERR_TRUNCATED_CHUNK;
                }
                char *name = NULL;
                size_t len = 0;
                result = nmo_chunk_read_string_checked(chunk, &name, &len);
                if (result != NMO_OK) {
                    nmo_array_dispose(&duplicate_names);
                    nmo_array_dispose(&inactive_guids);
                    return result;
                }
                if (nmo_chunk_get_position(chunk) > duplicate_section_end) {
                    nmo_array_dispose(&duplicate_names);
                    nmo_array_dispose(&inactive_guids);
                    return NMO_ERR_TRUNCATED_CHUNK;
                }
                if (len == 0 || name == NULL) {
                    break;
                }

                char **slot = NULL;
                result = nmo_array_extend(&duplicate_names, 1, (void **)&slot);
                if (result != NMO_OK) {
                    nmo_array_dispose(&duplicate_names);
                    nmo_array_dispose(&inactive_guids);
                    return result;
                }
                *slot = name;
            }
            if (nmo_chunk_get_position(chunk) < duplicate_section_end) {
                nmo_array_dispose(&duplicate_names);
                nmo_array_dispose(&inactive_guids);
                return NMO_ERR_INVALID_FORMAT;
            }
            result = nmo_array_swap(
                &out_state->duplicate_manager_names, &duplicate_names);
            nmo_array_dispose(&duplicate_names);
            if (result != NMO_OK) {
                nmo_array_dispose(&inactive_guids);
                return result;
            }
        } else if (result != NMO_ERR_NOT_FOUND) {
            nmo_array_dispose(&inactive_guids);
            return result;
        }
        NMO_RETURN_IF_ERROR(nmo_array_swap(
            &out_state->inactive_manager_guids, &inactive_guids));
        nmo_array_dispose(&inactive_guids);
        out_state->has_inactive_manager_section = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_level_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_level_state_t *out_state = (nmo_level_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_level_state_t decoded;
    nmo_status_t result = nmo_level_init_staged_state(&decoded, out_state);
    if (result != NMO_OK) return result;
    result = nmo_level_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_level_dispose_state_arrays(&decoded);
        return result;
    }
    nmo_level_dispose_state_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
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
static nmo_status_t nmo_level_serialize_internal(
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
    NMO_RETURN_IF_ERROR(nmo_level_validate(in_state, type, context));

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        NMO_RETURN_OK();
    }

    /* Section 1: LEVELDEFAULTDATA */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELDEFAULTDATA);
    if (result != NMO_OK) return result;

    /* 1) Legacy CKObjectArray */
    result = nmo_chunk_write_object_sequence_start(
        out_chunk, in_state->legacy_object_ids.count);
    if (result != NMO_OK) return result;
    const nmo_ref_t *legacy_object_ids = NMO_ARRAY_DATA(
        nmo_ref_t, &in_state->legacy_object_ids);
    for (size_t i = 0; i < in_state->legacy_object_ids.count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(
            out_chunk, &legacy_object_ids[i]));
    }

    /* 2) Legacy XObjectPointerArray */
    result = nmo_chunk_write_object_sequence_start(
        out_chunk, in_state->legacy_pointer_ids.count);
    if (result != NMO_OK) return result;
    const nmo_ref_t *legacy_pointer_ids = NMO_ARRAY_DATA(
        nmo_ref_t, &in_state->legacy_pointer_ids);
    for (size_t i = 0; i < in_state->legacy_pointer_ids.count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(
            out_chunk, &legacy_pointer_ids[i]));
    }

    /* 3) Scene list */
    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->scene_ids.count);
    if (result != NMO_OK) return result;

    const nmo_ref_t *scene_ids = NMO_ARRAY_DATA(nmo_ref_t, &in_state->scene_ids);
    for (uint32_t i = 0; i < in_state->scene_ids.count; i++) {
        result = nmo_ref_write_sequence_item(out_chunk, &scene_ids[i]);
        if (result != NMO_OK) return result;
    }

    /* Section 2: LEVELSCENE */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELSCENE);
    if (result != NMO_OK) return result;

    result = nmo_ref_write(out_chunk, &in_state->current_scene);
    if (result != NMO_OK) return result;

    result = nmo_ref_write(out_chunk, &in_state->level_scene);
    if (result != NMO_OK) return result;

    /* Write level scene sub-chunk (may be NULL) */
    result = nmo_chunk_write_sub_chunk(out_chunk, in_state->level_scene_chunk);
    if (result != NMO_OK) return result;

    /* Section 3: LEVELINACTIVEMAN (optional) */
    const bool write_inactive_manager_section =
        (in_state->has_inactive_manager_section != 0) ||
        (in_state->inactive_manager_guids.count > 0 && in_state->inactive_manager_guids.data) ||
        (in_state->duplicate_manager_names.count > 0 && in_state->duplicate_manager_names.data);

    if (write_inactive_manager_section) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELINACTIVEMAN);
        if (result != NMO_OK) return result;

        const nmo_guid_t *inactive_guids = NMO_ARRAY_DATA(nmo_guid_t, &in_state->inactive_manager_guids);
        for (size_t i = 0; i < in_state->inactive_manager_guids.count; ++i) {
            result = nmo_chunk_write_guid(out_chunk, inactive_guids[i]);
            if (result != NMO_OK) return result;
        }

        /* Section 4: LEVELDUPLICATEMAN (optional) */
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELDUPLICATEMAN);
        if (result != NMO_OK) return result;

        if (in_state->duplicate_manager_names.count > 0 && in_state->duplicate_manager_names.data) {
            const char *const *dup_names = NMO_ARRAY_DATA(const char *, &in_state->duplicate_manager_names);
            for (size_t i = 0;
                 i < in_state->duplicate_manager_names.count;
                 ++i) {
                result = nmo_chunk_write_string(out_chunk, dup_names[i]);
                if (result != NMO_OK) return result;
            }
        }

        /* Write NULL terminator */
        result = nmo_chunk_write_string(out_chunk, NULL);
        if (result != NMO_OK) return result;
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
    (void)type;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_level_validate(s, NULL, NULL));

    nmo_level_state_t copied;
    nmo_status_t result = nmo_level_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;
    result = nmo_beobject_vtable.copy(
        &s->base, &copied.base, NULL, arena);
    if (result != NMO_OK) goto fail;
    copied.current_scene = s->current_scene;
    copied.level_scene = s->level_scene;
    copied.has_inactive_manager_section = s->has_inactive_manager_section;

    nmo_array_dispose(&copied.legacy_object_ids);
    result = nmo_array_clone(
        &s->legacy_object_ids, &copied.legacy_object_ids,
        &s->legacy_object_ids.allocator);
    if (result != NMO_OK) goto fail;
    nmo_array_dispose(&copied.legacy_pointer_ids);
    result = nmo_array_clone(
        &s->legacy_pointer_ids, &copied.legacy_pointer_ids,
        &s->legacy_pointer_ids.allocator);
    if (result != NMO_OK) goto fail;
    nmo_array_dispose(&copied.scene_ids);
    result = nmo_array_clone(
        &s->scene_ids, &copied.scene_ids, &s->scene_ids.allocator);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_chunk(
        arena, &copied.level_scene_chunk, s->level_scene_chunk);
    if (result != NMO_OK) goto fail;
    nmo_array_dispose(&copied.inactive_manager_guids);
    result = nmo_array_clone(
        &s->inactive_manager_guids, &copied.inactive_manager_guids,
        &s->inactive_manager_guids.allocator);
    if (result != NMO_OK) goto fail;
    nmo_array_dispose(&copied.duplicate_manager_names);
    result = nmo_object_clone_string_array(
        arena, &copied.duplicate_manager_names,
        &s->duplicate_manager_names);
    if (result != NMO_OK) goto fail;
    nmo_object_array_set_string_lifecycle(&copied.duplicate_manager_names);

#define NMO_LEVEL_DETACH_SHARED_ARRAY(field) \
    do { \
        if (d->field.data == s->field.data) { \
            memset(&d->field, 0, sizeof(d->field)); \
        } \
    } while (0)
    NMO_LEVEL_DETACH_SHARED_ARRAY(base.scripts);
    NMO_LEVEL_DETACH_SHARED_ARRAY(base.attributes);
    NMO_LEVEL_DETACH_SHARED_ARRAY(base.legacy_attributes);
    NMO_LEVEL_DETACH_SHARED_ARRAY(legacy_object_ids);
    NMO_LEVEL_DETACH_SHARED_ARRAY(legacy_pointer_ids);
    NMO_LEVEL_DETACH_SHARED_ARRAY(scene_ids);
    NMO_LEVEL_DETACH_SHARED_ARRAY(inactive_manager_guids);
    NMO_LEVEL_DETACH_SHARED_ARRAY(duplicate_manager_names);
#undef NMO_LEVEL_DETACH_SHARED_ARRAY
    if (d->level_scene_chunk == s->level_scene_chunk) {
        d->level_scene_chunk = NULL;
    }
    nmo_level_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_level_destroy(&copied, NULL, NULL);
    return result;
}

nmo_status_t nmo_level_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_level_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_level_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_level_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.validate(
        &s->base, NULL, context));
    NMO_VALIDATE_COUNT(
        s->legacy_object_ids.data, s->legacy_object_ids.count,
        "legacy_object_ids");
    if (s->legacy_object_ids.element_size != sizeof(nmo_ref_t) ||
        s->legacy_object_ids.count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(
        s->legacy_pointer_ids.data, s->legacy_pointer_ids.count,
        "legacy_pointer_ids");
    if (s->legacy_pointer_ids.element_size != sizeof(nmo_ref_t) ||
        s->legacy_pointer_ids.count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->scene_ids.data, s->scene_ids.count, "scene_ids");
    if (s->scene_ids.element_size != sizeof(nmo_ref_t) ||
        s->scene_ids.count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->inactive_manager_guids.data, s->inactive_manager_guids.count,
                       "inactive_manager_guids");
    if (s->inactive_manager_guids.element_size != sizeof(nmo_guid_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->duplicate_manager_names.data, s->duplicate_manager_names.count,
                       "duplicate_manager_names");
    if (s->duplicate_manager_names.element_size != sizeof(char *)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    const char *const *duplicate_names = NMO_ARRAY_DATA(
        const char *, &s->duplicate_manager_names);
    for (size_t i = 0; i < s->duplicate_manager_names.count; ++i) {
        if (duplicate_names[i] == NULL) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_level_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_level_remap_dependencies");
    }

    nmo_level_state_t *state = (nmo_level_state_t *)instance;
    (void)context;

    if (state->scene_ids.count > 0 && state->scene_ids.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Level scene_ids missing");
    }

    /* Keep unresolved scene references and original ordering intact. */
    return nmo_level_validate(state, NULL, NULL);
}

nmo_status_t nmo_level_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_level_validate(instance, type, context);
}

static nmo_status_t nmo_level_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_level_pre_delete");
    }
    nmo_level_state_t *state = (nmo_level_state_t *)instance;
    state->legacy_object_ids.count = 0;
    state->legacy_pointer_ids.count = 0;
    state->scene_ids.count = 0;
    state->current_scene = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->level_scene = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->level_scene_chunk = NULL;
    state->inactive_manager_guids.count = 0;
    state->duplicate_manager_names.count = 0;
    state->has_inactive_manager_section = 0;
    NMO_RETURN_OK();
}

static void nmo_level_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static const nmo_object_serialize_pass_t nmo_level_compare_pass = {
    .class_id = NMO_CID_LEVEL,
    .data_version = 7,
    .chunk_options = NMO_CHUNK_OPTION_FILE,
};

static bool nmo_level_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_level_serialize, &nmo_level_compare_pass, 1, 4096);
}

static uint32_t nmo_level_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_level_serialize, &nmo_level_compare_pass, 1, 4096);
}

nmo_type_vtable_t nmo_level_vtable = {
    .prepare_dependencies = nmo_level_prepare_dependencies,
    .remap_dependencies = nmo_level_remap_dependencies,
    .pre_delete = nmo_level_pre_delete,
    .post_delete = nmo_level_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_level_create,
        nmo_level_destroy,
        nmo_level_serialize,
        nmo_level_deserialize,
        nmo_level_copy,
        nmo_level_validate,
        nmo_level_equals,
        nmo_level_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_level_type,
    CKPGUID_LEVEL,
    "CKLevel",
    NMO_CID_LEVEL,
    CKPGUID_BEOBJECT,
    nmo_level_state_t,
    &nmo_level_vtable,
    nmo_level_fields)






