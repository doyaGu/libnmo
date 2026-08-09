/**
 * @file ckinterfaceobjectmanager_schemas.c
 * @brief CKInterfaceObjectManager schema implementation
 */

#include "object/builtin/nmo_interfaceobjectmanager_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    interfaceobjectmanager,
    nmo_interfaceobjectmanager_state_t,
    do {
        NMO_RETURN_IF_ERROR(nmo_object_vtable.create(
            &state->base, NULL, context));
        state->chunk_count = 0;
        state->chunks = NULL;
        state->has_chunks_chunk = 1;
        state->guid = NMO_GUID_NULL;
        state->has_guid_chunk = 1;
    } while (0),
    nmo_object_vtable.destroy(&state->base, NULL, context))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_interfaceobjectmanager_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_interfaceobjectmanager_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_interfaceobjectmanager_state_t, chunk_count, CKPGUID_INT),
    NMO_FIELD_ARRAY_COUNTED(nmo_interfaceobjectmanager_state_t, chunks, chunk_count, 1, CKPGUID_STATECHUNK),
    NMO_FIELD(nmo_interfaceobjectmanager_state_t, has_chunks_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_interfaceobjectmanager_state_t, guid, CKPGUID_GUID),
    NMO_FIELD(nmo_interfaceobjectmanager_state_t, has_guid_chunk, CKPGUID_UINT8)
};

/* Identifiers from CKInterfaceObjectManager.cpp */
#define CK_STATESAVE_IOM_CHUNKS 0x01234567u
#define CK_STATESAVE_IOM_GUID   0x87654321u

static size_t nmo_interfaceobjectmanager_identifier_remaining_dwords(
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

static nmo_status_t nmo_interfaceobjectmanager_deserialize_internal(
    nmo_interfaceobjectmanager_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_interfaceobjectmanager_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_object_deserialize(
        &out_state->base, chunk, NULL, context));

    out_state->chunk_count = 0;
    out_state->chunks = NULL;
    out_state->has_chunks_chunk = 0;
    out_state->guid = NMO_GUID_NULL;
    out_state->has_guid_chunk = 0;

    int32_t parsed_count = 0;
    nmo_chunk_t **parsed_chunks = NULL;
    nmo_guid_t parsed_guid = NMO_GUID_NULL;
    uint8_t parsed_has_chunks = 0;
    uint8_t parsed_has_guid = 0;

    size_t chunks_section_dwords = 0;
    nmo_status_t seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_IOM_CHUNKS, &chunks_section_dwords);
    if (seek_result == NMO_OK) {
        if (chunks_section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t chunks_section_end =
            nmo_chunk_get_position(chunk) + chunks_section_dwords;
        int32_t count = 0;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result != NMO_OK) {
            return result;
        }
        if (count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Invalid CKInterfaceObjectManager chunk count");
        }
        if ((size_t)count > SIZE_MAX / sizeof(nmo_chunk_t *)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "CKInterfaceObjectManager chunk allocation size overflow");
        }
        if ((size_t)count >
            nmo_interfaceobjectmanager_identifier_remaining_dwords(chunk)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "CKInterfaceObjectManager chunk count exceeds remaining DWORDs");
        }

        nmo_chunk_t **chunks = NULL;
        if (count > 0) {
            chunks = (nmo_chunk_t **)nmo_arena_alloc(
                arena, sizeof(nmo_chunk_t *) * (size_t)count, _Alignof(nmo_chunk_t *));
            if (!chunks) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate CKInterfaceObjectManager chunks");
            }
            memset(chunks, 0, sizeof(nmo_chunk_t *) * (size_t)count);
            for (int32_t i = 0; i < count; ++i) {
                result = nmo_chunk_read_sub_chunk(chunk, &chunks[i]);
                if (result != NMO_OK) {
                    return result;
                }
            }
        }
        if (nmo_chunk_get_position(chunk) > chunks_section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        parsed_count = count;
        parsed_chunks = chunks;
        parsed_has_chunks = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    size_t guid_section_dwords = 0;
    seek_result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_IOM_GUID, &guid_section_dwords);
    if (seek_result == NMO_OK) {
        if (guid_section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        nmo_status_t result = nmo_chunk_read_guid(chunk, &parsed_guid);
        if (result != NMO_OK) {
            return result;
        }
        parsed_has_guid = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    out_state->chunk_count = parsed_count;
    out_state->chunks = parsed_chunks;
    out_state->has_chunks_chunk = parsed_has_chunks;
    out_state->guid = parsed_guid;
    out_state->has_guid_chunk = parsed_has_guid;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_interfaceobjectmanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t nmo_interfaceobjectmanager_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_interfaceobjectmanager_state_t *s = src;
    nmo_interfaceobjectmanager_state_t *d = dst;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_interfaceobjectmanager_validate(
        s, type, NULL));

    nmo_interfaceobjectmanager_state_t copied;
    nmo_status_t result = nmo_interfaceobjectmanager_create(
        &copied, NULL, NULL);
    if (result != NMO_OK) return result;
    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_object_state_t),
    };
    result = nmo_object_vtable.copy(
        &s->base, &copied.base, &base_type, arena);
    if (result != NMO_OK) goto fail;

    copied.chunk_count = s->chunk_count;
    copied.has_chunks_chunk = s->has_chunks_chunk;
    copied.guid = s->guid;
    copied.has_guid_chunk = s->has_guid_chunk;
    result = nmo_object_copy_chunk_array(
        arena, &copied.chunks, s->chunks, (uint32_t)s->chunk_count);
    if (result != NMO_OK) goto fail;

    nmo_interfaceobjectmanager_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_interfaceobjectmanager_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_interfaceobjectmanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_interfaceobjectmanager_state_t *s = instance;
    if (!s || s->chunk_count < 0) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->chunks, (uint32_t)s->chunk_count, "chunks");
    NMO_RETURN_OK();
}

nmo_status_t nmo_interfaceobjectmanager_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_interfaceobjectmanager_validate(instance, type, context);
}

nmo_status_t nmo_interfaceobjectmanager_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_interfaceobjectmanager_remap_dependencies");
    }

    nmo_interfaceobjectmanager_state_t *state = (nmo_interfaceobjectmanager_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    if (state->chunk_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Invalid CKInterfaceObjectManager chunk count");
    }
    if (state->chunk_count > 0 && state->chunks == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "CKInterfaceObjectManager chunks missing");
    }

    return nmo_interfaceobjectmanager_validate(state, NULL, NULL);
}

static nmo_status_t nmo_interfaceobjectmanager_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_interfaceobjectmanager_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_interfaceobjectmanager_post_delete(
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

static const nmo_object_serialize_pass_t
    nmo_interfaceobjectmanager_compare_pass = {
    .class_id = NMO_CID_INTERFACEOBJECTMANAGER,
    .data_version = 7,
    .serialize_flags = NMO_SERIALIZE_FLAG_FILE_MODE,
    .save_flags = UINT32_MAX,
    .use_context = 1,
};

static bool nmo_interfaceobjectmanager_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_interfaceobjectmanager_serialize,
        &nmo_interfaceobjectmanager_compare_pass, 1, 4096);
}

static uint32_t nmo_interfaceobjectmanager_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_interfaceobjectmanager_serialize,
        &nmo_interfaceobjectmanager_compare_pass, 1, 4096);
}

nmo_type_vtable_t nmo_interfaceobjectmanager_vtable = {
    .prepare_dependencies = nmo_interfaceobjectmanager_prepare_dependencies,
    .remap_dependencies = nmo_interfaceobjectmanager_remap_dependencies,
    .pre_delete = nmo_interfaceobjectmanager_pre_delete,
    .post_delete = nmo_interfaceobjectmanager_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_interfaceobjectmanager_create,
        nmo_interfaceobjectmanager_destroy,
        nmo_interfaceobjectmanager_serialize,
        nmo_interfaceobjectmanager_deserialize,
        nmo_interfaceobjectmanager_copy,
        nmo_interfaceobjectmanager_validate,
        nmo_interfaceobjectmanager_equals,
        nmo_interfaceobjectmanager_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_interfaceobjectmanager_type,
    CKPGUID_INTERFACEOBJECTMANAGER,
    "CKInterfaceObjectManager",
    NMO_CID_INTERFACEOBJECTMANAGER,
    CKPGUID_OBJECT,
    nmo_interfaceobjectmanager_state_t,
    &nmo_interfaceobjectmanager_vtable,
    nmo_interfaceobjectmanager_fields)

static nmo_status_t nmo_interfaceobjectmanager_serialize_internal(
    const nmo_interfaceobjectmanager_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_interfaceobjectmanager_serialize");
    }

    nmo_status_t result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (in_state->chunk_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid CKInterfaceObjectManager chunk count");
    }

    if (in_state->chunk_count > 0 && in_state->chunks == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "CKInterfaceObjectManager chunks missing");
    }

    if (in_state->has_chunks_chunk) {
        result = nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_IOM_CHUNKS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->chunk_count);
        if (result != NMO_OK) return result;

        for (int32_t i = 0; i < in_state->chunk_count; ++i) {
            nmo_chunk_t *sub = in_state->chunks[i];
            result = nmo_chunk_write_sub_chunk(out_chunk, sub);
            if (result != NMO_OK) return result;
        }
    }

    if (in_state->has_guid_chunk) {
        result = nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_IOM_GUID);
        if (result != NMO_OK) return result;
        return nmo_chunk_write_guid(out_chunk, in_state->guid);
    }
    return NMO_OK;
}

nmo_status_t nmo_interfaceobjectmanager_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_interfaceobjectmanager_state_t *out_state = (nmo_interfaceobjectmanager_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_interfaceobjectmanager_state_t decoded;
    nmo_status_t result = nmo_interfaceobjectmanager_create(
        &decoded, NULL, context);
    if (result != NMO_OK) return result;
    result = nmo_interfaceobjectmanager_deserialize_internal(
        &decoded, chunk, context);
    if (result != NMO_OK) {
        nmo_interfaceobjectmanager_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_interfaceobjectmanager_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_interfaceobjectmanager_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_interfaceobjectmanager_state_t *in_state =
        (const nmo_interfaceobjectmanager_state_t *)instance;
    if (!in_state || !out_chunk || !out_chunk->arena) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t result = nmo_interfaceobjectmanager_validate(
        in_state, NULL, NULL);
    if (result != NMO_OK) return result;
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (!staged) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    result = nmo_interfaceobjectmanager_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}
