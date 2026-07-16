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

NMO_DEFINE_OBJECT_LIFECYCLE(
    level,
    nmo_level_state_t,
    do {
        state->has_inactive_manager_section = 0;
        nmo_status_t result = nmo_array_init(&state->scene_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->inactive_manager_guids, sizeof(nmo_guid_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->duplicate_manager_names, sizeof(char *), 0, NULL);
        if (result != NMO_OK) return result;
        nmo_object_array_set_string_lifecycle(&state->duplicate_manager_names);
    } while (0),
    ((void)0))

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
        if (next_pos != 0 && next_pos < data_count) {
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
    NMO_FIELD_REF_ARRAY(nmo_level_state_t, scene_ids),
    NMO_FIELD_REF(nmo_level_state_t, current_scene_id),
    NMO_FIELD_REF(nmo_level_state_t, level_scene_id),
    NMO_FIELD_OPT(nmo_level_state_t, level_scene_chunk, CKPGUID_STATECHUNK),
    NMO_FIELD(nmo_level_state_t, has_inactive_manager_section, CKPGUID_UINT8),
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

    nmo_deserialize_context_t *deser_ctx = nmo_deserialize_context_get(context);
    const bool is_file = ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (deser_ctx != NULL && (deser_ctx->flags & NMO_DESER_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        NMO_RETURN_OK();
    }

    out_state->has_inactive_manager_section = 0;

    /* Section 1: LEVELDEFAULTDATA - Legacy arrays + scene list */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELDEFAULTDATA);
    if (result == NMO_OK) {
        /* 1) Legacy CKObjectArray (unused) */
        size_t legacy_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &legacy_count);
        if (result != NMO_OK) return result;
        for (size_t i = 0; i < legacy_count; ++i) {
            nmo_object_id_t ignored_id = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_item(chunk, &ignored_id));
        }

        /* 2) Legacy XObjectPointerArray (empty in modern files) */
        result = nmo_chunk_read_object_sequence_start(chunk, &legacy_count);
        if (result != NMO_OK) return result;
        for (size_t i = 0; i < legacy_count; ++i) {
            nmo_object_id_t ignored_id = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_item(chunk, &ignored_id));
        }

        /* 3) Scene list (XObjectPointerArray::Save) */
        size_t scene_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &scene_count);
        if (result != NMO_OK) return result;

        nmo_array_t scene_ids;
        result = nmo_array_init(&scene_ids, sizeof(nmo_object_id_t),
                                scene_count, &out_state->scene_ids.allocator);
        if (result != NMO_OK) return result;
        if (scene_count > 0) {
            const uint32_t MAX_SCENES = 10000;
            if (scene_count > MAX_SCENES) {
                nmo_array_dispose(&scene_ids);
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                 "Scene count exceeds maximum");
            }

            nmo_object_id_t *ids = NULL;
            result = nmo_array_extend(&scene_ids, scene_count, (void **)&ids);
            if (result != NMO_OK) {
                nmo_array_dispose(&scene_ids);
                return result;
            }

            for (size_t i = 0; i < scene_count; i++) {
                result = nmo_chunk_read_object_sequence_item(chunk, &ids[i]);
                if (result != NMO_OK) {
                    nmo_array_dispose(&scene_ids);
                    return result;
                }
            }
        }
        NMO_RETURN_IF_ERROR(nmo_array_swap(&out_state->scene_ids, &scene_ids));
        nmo_array_dispose(&scene_ids);
    }

    /* Section 2: LEVELSCENE - Current scene + level scene */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELSCENE);
    if (result == NMO_OK) {
        nmo_object_id_t current_scene_id = NMO_OBJECT_ID_NONE;
        nmo_object_id_t level_scene_id = NMO_OBJECT_ID_NONE;
        nmo_chunk_t *level_scene_chunk = NULL;

        /* Read current scene ID */
        result = nmo_chunk_read_object_id(chunk, &current_scene_id);
        if (result != NMO_OK) return result;

        /* Read level scene ID */
        result = nmo_chunk_read_object_id(chunk, &level_scene_id);
        if (result != NMO_OK) return result;

        /* Read level scene sub-chunk */
        result = nmo_chunk_read_sub_chunk(chunk, &level_scene_chunk);
        if (result != NMO_OK) return result;

        out_state->current_scene_id = current_scene_id;
        out_state->level_scene_id = level_scene_id;
        out_state->level_scene_chunk = level_scene_chunk;
    }

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
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LEVELDUPLICATEMAN);
        if (result == NMO_OK) {
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
                char *name = NULL;
                size_t len = 0;
                result = nmo_chunk_read_string_checked(chunk, &name, &len);
                if (result != NMO_OK) {
                    nmo_array_dispose(&duplicate_names);
                    nmo_array_dispose(&inactive_guids);
                    return result;
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
            result = nmo_array_swap(
                &out_state->duplicate_manager_names, &duplicate_names);
            nmo_array_dispose(&duplicate_names);
            if (result != NMO_OK) {
                nmo_array_dispose(&inactive_guids);
                return result;
            }
        }
        NMO_RETURN_IF_ERROR(nmo_array_swap(
            &out_state->inactive_manager_guids, &inactive_guids));
        nmo_array_dispose(&inactive_guids);
        out_state->has_inactive_manager_section = 1;
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

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        NMO_RETURN_OK();
    }

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
        for (uint32_t i = 0; i < in_state->inactive_manager_guids.count; i++) {
            result = nmo_chunk_write_guid(out_chunk, inactive_guids[i]);
            if (result != NMO_OK) return result;
        }

        /* Section 4: LEVELDUPLICATEMAN (optional) */
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LEVELDUPLICATEMAN);
        if (result != NMO_OK) return result;

        if (in_state->duplicate_manager_names.count > 0 && in_state->duplicate_manager_names.data) {
            const char *const *dup_names = NMO_ARRAY_DATA(const char *, &in_state->duplicate_manager_names);
            for (uint32_t i = 0; i < in_state->duplicate_manager_names.count; i++) {
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
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.scripts, &d->base.scripts,
                                        &s->base.scripts.allocator));
    NMO_RETURN_IF_ERROR(nmo_beobject_clone_attributes(
        arena, &d->base.attributes, &s->base.attributes));

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
    state->scene_ids.count = 0;
    state->current_scene_id = 0;
    state->level_scene_id = 0;
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

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(level, nmo_level_state_t)

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






