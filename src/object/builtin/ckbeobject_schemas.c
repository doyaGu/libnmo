/**
 * @file ckbeobject_schemas.c
 * @brief CKBeObject schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKBeObject (behavioral objects).
 * CKBeObject extends CKSceneObject and adds scripts, priority, and attributes.
 * 
 * Based on official Virtools SDK (reference/src/CKBeObject.cpp:400-700):
 * - CKBeObject implements Load/Save with scripts, priority, and attribute data
 * - Many derived classes (CKRenderObject, CKMesh, CKTexture, etc.) do NOT override
 *   Load/Save and inherit this behavior directly
 * - CKRenderObject is an abstract base class with no serialization code
 * 
 * This is the serialization workhorse for the entire BeObject hierarchy.
 */

#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    beobject,
    nmo_beobject_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->script_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->attribute_parameter_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->attribute_types, sizeof(uint32_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->attribute_chunks, sizeof(nmo_chunk_t *), 0, NULL);
        if (result != NMO_OK) return result;
        nmo_object_array_set_chunk_lifecycle(&state->attribute_chunks);
        result = nmo_array_init(&state->legacy_attributes_raw, sizeof(uint8_t), 0, NULL);
        if (result != NMO_OK) return result;
    } while (0),
    ((void)0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_beobject_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("base", offsetof(nmo_beobject_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_SCENEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* Scripts */
    NMO_FIELD_REF_ARRAY(nmo_beobject_state_t, script_ids),
    /* Priority */
    NMO_FIELD(nmo_beobject_state_t, priority, CKPGUID_INT),
    /* Attributes */
    NMO_FIELD_REF_ARRAY(nmo_beobject_state_t, attribute_parameter_ids),
    NMO_FIELD_ARRAY(nmo_beobject_state_t, attribute_types, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_beobject_state_t, attribute_chunks, CKPGUID_STATECHUNK),
    NMO_FIELD_ARRAY(nmo_beobject_state_t, legacy_attributes_raw, CKPGUID_UINT8),
    /* Single activity */
    NMO_FIELD(nmo_beobject_state_t, has_single_activity, CKPGUID_BOOL),
    NMO_FIELD(nmo_beobject_state_t, single_activity_flags, NMO_GUID_ENUM_CK_SCENEOBJECTACTIVITY_FLAGS)
};


/* DATAS version flag */
#define CK_DATAS_VERSION_FLAG        0x10000000

/* =============================================================================
 * IDENTIFIER HELPERS
 * ============================================================================= */

static size_t nmo_beobject_identifier_remaining_dwords(nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) {
        return 0;
    }

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    size_t next_pos = 0;
    if (state->prev_identifier_pos + 1 < chunk->data.count) {
        next_pos = data[state->prev_identifier_pos + 1];
    }
    if (next_pos == 0 || next_pos > chunk->data.count) {
        next_pos = chunk->data.count;
    }
    if (next_pos < state->current_pos) {
        return 0;
    }

    return next_pos - state->current_pos;
}

static nmo_status_t nmo_beobject_read_raw_bytes(nmo_chunk_t *chunk, void *buffer, size_t bytes)
{
    if (!chunk || !buffer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_beobject_read_raw_bytes");
    }

    size_t dwords = (bytes + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, dwords, "Insufficient data for raw buffer");

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(buffer, &data[state->current_pos], bytes);
    state->current_pos += dwords;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_beobject_read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_array_t *out_ids)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) {
        return result;
    }

    if (count == 0) {
        nmo_array_clear(out_ids);
        NMO_RETURN_OK();
    }

    if (count > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid object sequence count");
    }

    nmo_array_clear(out_ids);
    result = nmo_array_reserve(out_ids, count);
    if (result != NMO_OK) return result;

    nmo_object_id_t *ids = NULL;
    result = nmo_array_extend(out_ids, count, (void **)&ids);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < (uint32_t)count; ++i) {
        result = nmo_chunk_read_object_sequence_item(chunk, &ids[i]);
        if (result != NMO_OK) {
            return result;
        }
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKBeObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBeObject state from chunk
 * 
 * Implements the symmetric read operation for CKBeObject::Load.
 * Reads scripts, priority, and attributes using identifier-based approach.
 * 
 * Reference: reference/src/CKBeObject.cpp:550-700
 * 
 * @param chunk Chunk containing CKBeObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_beobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_beobject_state_t *out_state = (nmo_beobject_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    (void)arena;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_beobject_deserialize");
    }

    /* Deserialize base CKSceneObject state first */
    nmo_status_t result = nmo_sceneobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;
    
    const bool is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    const uint32_t data_version = nmo_chunk_get_data_version(chunk);

    /* Load scripts array - optional section (legacy + modern) */
    nmo_last_error_clear();
    result = NMO_OK;
    if (is_file && data_version < 5) {
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORS);
        if (result == NMO_OK) {
            result = nmo_beobject_read_object_sequence(chunk, &out_state->script_ids);
            if (result != NMO_OK) return result;
        }
    }

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCRIPTS);
    if (result == NMO_OK) {
        result = nmo_beobject_read_object_sequence(chunk, &out_state->script_ids);
        if (result != NMO_OK) return result;
    }

    /* Load priority data - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAS);
    if (result == NMO_OK) {
        if (!is_file) {
            int32_t ignored = 0;
            (void)nmo_chunk_read_int(chunk, &ignored);
            goto load_attributes;
        }

        uint32_t version_flag = 0;
        result = nmo_chunk_read_dword(chunk, &version_flag);
        if (result != NMO_OK) {
            goto load_attributes;
        }

        if (data_version < 5) {
            int32_t ignored = 0;
            (void)nmo_chunk_read_int(chunk, &ignored);
            (void)nmo_chunk_read_int(chunk, &ignored);
            (void)nmo_chunk_read_int(chunk, &ignored);
            (void)nmo_chunk_read_int(chunk, &out_state->priority);
        } else if (version_flag & CK_DATAS_VERSION_FLAG) {
            (void)nmo_chunk_read_int(chunk, &out_state->priority);
        } else {
            if (data_version >= 5 && nmo_beobject_identifier_remaining_dwords(chunk) > 0) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "CKBeObject: DATAS section missing version flag but contains data");
            }
            out_state->priority = 0;
        }
    }

load_attributes:
    /* Load attributes - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_NEWATTRIBUTES);
    if (result == NMO_OK) {
        /* Read attribute object sequence using proper sequence API
         * Reference: CKBeObject.cpp line 537: const int attrCount = chunk->StartReadSequence(); */
        size_t attr_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &attr_count);
        if (result != NMO_OK) {
            return result;
        }

        nmo_array_clear(&out_state->attribute_parameter_ids);
        nmo_array_clear(&out_state->attribute_types);
        nmo_array_clear(&out_state->attribute_chunks);

        if (attr_count > 0) {
            /* Allocate arrays for attributes */
            result = nmo_array_reserve(&out_state->attribute_parameter_ids, attr_count);
            if (result != NMO_OK) return result;
            result = nmo_array_reserve(&out_state->attribute_types, attr_count);
            if (result != NMO_OK) return result;

            nmo_object_id_t *attr_ids = NULL;
            uint32_t *attr_types = NULL;
            result = nmo_array_extend(&out_state->attribute_parameter_ids, attr_count,
                                      (void **)&attr_ids);
            if (result != NMO_OK) return result;
            result = nmo_array_extend(&out_state->attribute_types, attr_count,
                                      (void **)&attr_types);
            if (result != NMO_OK) return result;
            memset(attr_types, 0, attr_count * sizeof(uint32_t));

            /* Read attribute parameter object IDs
             * Reference: CKBeObject.cpp lines 542-544 */
            for (size_t i = 0; i < attr_count; i++) {
                result = nmo_chunk_read_object_sequence_item(chunk, &attr_ids[i]);
                if (result != NMO_OK) {
                    return result;
                }
            }

            if (!is_file) {
                size_t sub_count = 0;
                result = nmo_chunk_start_read_sub_chunk_sequence(chunk, &sub_count);
                if (result != NMO_OK) {
                    return result;
                }
                if (sub_count != attr_count) {
                    NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                     "CKBeObject: attribute sub-chunk sequence count mismatch");
                }

                nmo_array_clear(&out_state->attribute_chunks);
                result = nmo_array_reserve(&out_state->attribute_chunks, attr_count);
                if (result != NMO_OK) return result;

                nmo_chunk_t **chunks = NULL;
                result = nmo_array_extend(&out_state->attribute_chunks, attr_count, (void **)&chunks);
                if (result != NMO_OK) return result;

                for (size_t i = 0; i < attr_count; ++i) {
                    result = nmo_chunk_read_sub_chunk(chunk, &chunks[i]);
                    if (result != NMO_OK) {
                        return result;
                    }
                }
            }

            /* Read manager sequence for attribute types
             * Reference: CKBeObject.cpp lines 555-577 */
            nmo_guid_t manager_guid;
            size_t seq_count = 0;
            result = nmo_chunk_start_manager_read_sequence(chunk, &manager_guid, &seq_count);
            if (result != NMO_OK) {
                return result;
            }
            if (seq_count != attr_count) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                 "CKBeObject: attribute manager sequence count mismatch");
            }
            /* Verify it's the attribute manager GUID */
            if (!nmo_guid_equals(manager_guid, NMO_MANAGER_GUID_ATTRIBUTE)) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                 "CKBeObject: attribute manager GUID mismatch");
            }

            /* Read attribute types from manager sequence */
            for (size_t i = 0; i < attr_count; i++) {
                result = nmo_chunk_read_dword(chunk, &attr_types[i]);
                if (result != NMO_OK) {
                    return result;
                }
            }
            out_state->attribute_parameter_ids.count = attr_count;
            out_state->attribute_types.count = attr_count;
        }
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ATTRIBUTES) == NMO_OK) {
        /* Legacy attribute payload - preserve raw bytes for round-trip */
        size_t remaining_dwords = nmo_beobject_identifier_remaining_dwords(chunk);
        size_t remaining_bytes = remaining_dwords * 4;
        if (remaining_bytes > 0) {
            nmo_array_clear(&out_state->legacy_attributes_raw);
            result = nmo_array_alloc(&out_state->legacy_attributes_raw, sizeof(uint8_t), remaining_bytes, NULL);
            if (result == NMO_OK) {
                (void)nmo_beobject_read_raw_bytes(chunk,
                                                  out_state->legacy_attributes_raw.data,
                                                  remaining_bytes);
            }
        }
    }
    /* If identifier not found, attributes section is optional - continue */

    if (is_file && nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SINGLEACTIVITY) == NMO_OK) {
        out_state->has_single_activity = 1;
        (void)nmo_chunk_read_dword(chunk, &out_state->single_activity_flags);
    }

    /* Deserialization completed - all sections are optional */
    NMO_RETURN_OK();
}

/* =============================================================================
 * CKBeObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBeObject state to chunk
 * 
 * Implements the symmetric write operation for CKBeObject::Save.
 * Writes scripts, priority, and attributes using identifier-based approach.
 * 
 * Reference: reference/src/CKBeObject.cpp:400-550
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_beobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_beobject_state_t *in_state = (const nmo_beobject_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_beobject_serialize");
    }

    /* Write base class (CKSceneObject) data */
    nmo_status_t result = nmo_sceneobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    if (!is_file && (save_flags & CK_STATESAVE_BEOBJECTONLY) == 0) {
        NMO_RETURN_OK();
    }

    /* Write scripts if present (file mode only) */
    if (is_file && in_state->script_ids.count > 0 && in_state->script_ids.data) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCRIPTS);
        if (result != NMO_OK) return result;

        /* Write script object sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, (uint32_t)in_state->script_ids.count);
        if (result != NMO_OK) return result;
        const nmo_object_id_t *script_ids = NMO_ARRAY_DATA(nmo_object_id_t, &in_state->script_ids);
        for (uint32_t i = 0; i < in_state->script_ids.count; i++) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, script_ids[i]);
            if (result != NMO_OK) return result;
        }
    }

    /* Write priority data if non-zero (file mode only) */
    if (is_file && in_state->priority != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAS);
        if (result != NMO_OK) return result;

        /* Write version flag (modern format) */
        result = nmo_chunk_write_dword(out_chunk, CK_DATAS_VERSION_FLAG);
        if (result != NMO_OK) return result;

        /* Write priority value */
        result = nmo_chunk_write_int(out_chunk, in_state->priority);
        if (result != NMO_OK) return result;
    }

    /* Write legacy attributes if no modern attributes were decoded */
    if (in_state->legacy_attributes_raw.data && in_state->legacy_attributes_raw.count > 0 &&
        in_state->attribute_parameter_ids.count == 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ATTRIBUTES);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                in_state->legacy_attributes_raw.data,
                                                in_state->legacy_attributes_raw.count);
        if (result != NMO_OK) return result;
    }

    /* Write attributes if present */
    if (in_state->attribute_parameter_ids.count > 0) {
        if (!in_state->attribute_parameter_ids.data || !in_state->attribute_types.data) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "CKBeObject: attribute arrays missing");
        }
        if (in_state->attribute_types.count != in_state->attribute_parameter_ids.count) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "CKBeObject: attribute type count mismatch");
        }

        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_NEWATTRIBUTES);
        if (result != NMO_OK) return result;

        uint32_t attr_count = (uint32_t)in_state->attribute_parameter_ids.count;

        /* Start object ID sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, attr_count);
        if (result != NMO_OK) return result;

        /* Write attribute parameter object IDs */
        const nmo_object_id_t *attr_ids = NMO_ARRAY_DATA(nmo_object_id_t,
                                                         &in_state->attribute_parameter_ids);
        for (uint32_t i = 0; i < attr_count; i++) {
            result = nmo_chunk_write_object_sequence_item(out_chunk,
                                                          attr_ids[i]);
            if (result != NMO_OK) return result;
        }

        if (!is_file) {
            result = nmo_chunk_start_sub_chunk_sequence(out_chunk, attr_count);
            if (result != NMO_OK) return result;
            const nmo_chunk_t *const *attr_chunks = NMO_ARRAY_DATA(const nmo_chunk_t *,
                                                                    &in_state->attribute_chunks);
            for (uint32_t i = 0; i < attr_count; i++) {
                nmo_chunk_t *sub = NULL;
                if (attr_chunks && i < in_state->attribute_chunks.count) {
                    sub = (nmo_chunk_t *)attr_chunks[i];
                }
                result = nmo_chunk_write_sub_chunk_sequence(out_chunk, sub);
                if (result != NMO_OK) return result;
            }
        }

        /* Write manager sequence for attribute types */
        nmo_guid_t attr_mgr_guid = NMO_MANAGER_GUID_ATTRIBUTE;
        result = nmo_chunk_start_manager_sequence(out_chunk, attr_mgr_guid, attr_count);
        if (result != NMO_OK) return result;

        /* Write attribute types */
        const uint32_t *attr_types = NMO_ARRAY_DATA(uint32_t, &in_state->attribute_types);
        for (uint32_t i = 0; i < attr_count; i++) {
            result = nmo_chunk_write_dword(out_chunk, attr_types[i]);
            if (result != NMO_OK) return result;
        }
    }

    /* Write single activity flags if present (file mode only) */
    if (is_file && in_state->has_single_activity) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SINGLEACTIVITY);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->single_activity_flags);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_beobject_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_beobject_state_t *s = src;
    nmo_beobject_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->script_ids, &d->script_ids, &s->script_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->attribute_parameter_ids, &d->attribute_parameter_ids,
                                        &s->attribute_parameter_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->attribute_types, &d->attribute_types,
                                        &s->attribute_types.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->attribute_chunks,
                                                     &s->attribute_chunks));
    return nmo_array_clone(&s->legacy_attributes_raw, &d->legacy_attributes_raw,
                           &s->legacy_attributes_raw.allocator);
}

static nmo_status_t nmo_beobject_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_beobject_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->script_ids.data, s->script_ids.count, "script_ids");
    NMO_VALIDATE_COUNT(s->attribute_parameter_ids.data, s->attribute_parameter_ids.count,
                       "attribute_parameter_ids");
    NMO_VALIDATE_COUNT(s->attribute_types.data, s->attribute_types.count, "attribute_types");
    NMO_VALIDATE_COUNT(s->attribute_chunks.data, s->attribute_chunks.count, "attribute_chunks");
    NMO_VALIDATE_BYTES(s->legacy_attributes_raw.data, s->legacy_attributes_raw.count,
                       "legacy_attributes_raw");
    NMO_RETURN_OK();
}

nmo_status_t nmo_beobject_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_beobject_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_beobject_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_beobject_remap_dependencies");
    }

    nmo_beobject_state_t *state = (nmo_beobject_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

    if (state->script_ids.count > 0 && state->script_ids.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "BeObject script_ids missing");
    }

    if (state->script_ids.count > 0) {
        nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, &state->script_ids);
        uint32_t kept = 0;
        for (uint32_t i = 0; i < state->script_ids.count; ++i) {
            nmo_object_id_t id = ids[i];
            if (id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, id) == NULL) {
                continue;
            }
            bool seen = false;
            for (uint32_t j = 0; j < kept; ++j) {
                if (ids[j] == id) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            ids[kept++] = id;
        }
        state->script_ids.count = kept;
    }

    if (state->attribute_parameter_ids.count > 0 && state->attribute_parameter_ids.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "BeObject attribute_parameter_ids missing");
    }
    if (state->attribute_types.count > 0 && state->attribute_types.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "BeObject attribute_types missing");
    }
    if (state->attribute_chunks.count > 0 && state->attribute_chunks.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "BeObject attribute_chunks missing");
    }

    uint32_t attr_count = (uint32_t)state->attribute_parameter_ids.count;
    if (state->attribute_types.count < attr_count) {
        attr_count = (uint32_t)state->attribute_types.count;
    }

    if (attr_count == 0) {
        state->attribute_parameter_ids.count = 0;
        state->attribute_types.count = 0;
        state->attribute_chunks.count = 0;
    } else {
        if (state->attribute_types.count != attr_count) {
            state->attribute_types.count = attr_count;
        }
        if (state->attribute_parameter_ids.count != attr_count) {
            state->attribute_parameter_ids.count = attr_count;
        }
        if (state->attribute_chunks.count > attr_count) {
            state->attribute_chunks.count = attr_count;
        }

        nmo_object_id_t *attr_ids = NMO_ARRAY_DATA(nmo_object_id_t, &state->attribute_parameter_ids);
        uint32_t *attr_types = NMO_ARRAY_DATA(uint32_t, &state->attribute_types);
        nmo_chunk_t **attr_chunks = NULL;
        if (state->attribute_chunks.count > 0 && state->attribute_chunks.data) {
            attr_chunks = NMO_ARRAY_DATA(nmo_chunk_t *, &state->attribute_chunks);
        }

        uint32_t kept = 0;
        for (uint32_t i = 0; i < attr_count; ++i) {
            nmo_object_id_t id = attr_ids[i];
            if (id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, id) == NULL) {
                continue;
            }
            attr_ids[kept] = id;
            attr_types[kept] = attr_types[i];
            if (attr_chunks && i < state->attribute_chunks.count) {
                attr_chunks[kept] = attr_chunks[i];
            }
            kept++;
        }

        state->attribute_parameter_ids.count = kept;
        state->attribute_types.count = kept;
        if (state->attribute_chunks.count > kept) {
            state->attribute_chunks.count = kept;
        }
    }

    if (state->attribute_parameter_ids.count > 0) {
        nmo_array_clear(&state->legacy_attributes_raw);
    }

    if (!state->has_single_activity) {
        state->single_activity_flags = 0;
    } else {
        state->has_single_activity = 1;
    }

    return nmo_beobject_validate(state, NULL, NULL);
}

static nmo_status_t nmo_beobject_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_beobject_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_beobject_post_delete(
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

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(beobject, nmo_beobject_state_t)

nmo_type_vtable_t nmo_beobject_vtable = {
    .prepare_dependencies = nmo_beobject_prepare_dependencies,
    .remap_dependencies = nmo_beobject_remap_dependencies,
    .pre_delete = nmo_beobject_pre_delete,
    .post_delete = nmo_beobject_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_beobject_create,
        nmo_beobject_destroy,
        nmo_beobject_serialize,
        nmo_beobject_deserialize,
        nmo_beobject_copy,
        nmo_beobject_validate,
        nmo_beobject_equals,
        nmo_beobject_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_beobject_type,
    CKPGUID_BEOBJECT,
    "CKBeObject",
    NMO_CID_BEOBJECT,
    CKPGUID_SCENEOBJECT,
    nmo_beobject_state_t,
    &nmo_beobject_vtable,
    nmo_beobject_fields)






