/**
 * @file ckmessagemanager_schemas.c
 * @brief CKMessageManager schema implementation
 *
 * Implements schema-driven deserialization for CKMessageManager (message type registry).
 * This is a manager class that handles message type registration and routing.
 * 
 * Based on official Virtools SDK (reference/src/CKMessageManager.cpp:178-250).
 */

#include "object/builtin/nmo_messagemanager_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_manager_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(messagemanager, nmo_messagemanager_state_t)

/* =============================================================================
 * IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKMessageManager.cpp */
#define CK_STATESAVE_MESSAGEMANAGER 0x53

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_messagemanager_fields[] = {
    NMO_FIELD(nmo_messagemanager_state_t, message_type_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_messagemanager_state_t, message_type_names, message_type_count, 1, CKPGUID_STRING)
};

/* =============================================================================
 * CKMessageManager DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKMessageManager state from chunk
 * 
 * Implements the symmetric read operation for CKMessageManager::LoadData.
 * Reads message type names from the chunk.
 * 
 * Reference: reference/src/CKMessageManager.cpp:218-247
 * 
 * @param chunk Chunk containing CKMessageManager data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static bool nmo_messagemanager_size_mul_overflows(
    size_t count,
    size_t element_size)
{
    return count != 0 && element_size > SIZE_MAX / count;
}

static nmo_status_t nmo_messagemanager_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_messagemanager_state_t *out_state = (nmo_messagemanager_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_messagemanager_deserialize");
    }

    /* Seek identifier */
    size_t section_dwords = 0;
    nmo_status_t result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_MESSAGEMANAGER, &section_dwords);
    if (result == NMO_ERR_NOT_FOUND) {
        /* No data to load - this is valid */
        NMO_RETURN_OK();
    }
    if (result != NMO_OK) return result;
    const size_t section_end =
        nmo_chunk_get_position(chunk) + section_dwords;

    /* Read message type count */
    int32_t type_count;
    result = nmo_chunk_read_int(chunk, &type_count);
    if (result != NMO_OK) return result;
    if (nmo_chunk_get_position(chunk) > section_end) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    if (type_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid message type count");
    }

    if (type_count == 0) {
        if (nmo_chunk_get_position(chunk) != section_end) {
            return NMO_ERR_INVALID_FORMAT;
        }
        NMO_RETURN_OK();
    }

    if ((size_t)type_count >
        section_end - nmo_chunk_get_position(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Message type count exceeds remaining DWORDs");
    }
    if (arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Message manager deserialization requires an arena");
    }
    if (nmo_messagemanager_size_mul_overflows(
            (size_t)type_count, sizeof(char *))) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Message type name allocation size overflows");
    }

    const char **names = (const char **)nmo_arena_alloc(
        arena, type_count * sizeof(char *), _Alignof(char *));
    if (!names) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate message type names");
    }

    /* Read each message type name */
    for (int32_t i = 0; i < type_count; i++) {
        char *name = NULL;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &name, NULL));
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (name == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Message type name is missing");
        }
        names[i] = name; /* Chunk manages the buffer */
    }

    if (nmo_chunk_get_position(chunk) != section_end) {
        return NMO_ERR_INVALID_FORMAT;
    }

    out_state->message_type_count = (uint32_t)type_count;
    out_state->message_type_names = names;

    NMO_RETURN_OK();
}

nmo_status_t nmo_messagemanager_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_messagemanager_state_t *out_state =
        (nmo_messagemanager_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_messagemanager_state_t decoded = {0};
    nmo_status_t result = nmo_messagemanager_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) return result;
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKMessageManager SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKMessageManager state to chunk
 * 
 * Implements the symmetric write operation for CKMessageManager::SaveData.
 * Writes message type names to the chunk.
 * 
 * Reference: reference/src/CKMessageManager.cpp:178-216
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_status_t nmo_messagemanager_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_messagemanager_state_t *in_state =
        (const nmo_messagemanager_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_messagemanager_serialize");
    }
    if (in_state->message_type_count > INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Message type count exceeds format limits");
    }

    if (in_state->message_type_count > 0 && in_state->message_type_names == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Message type names missing");
    }

    if (in_state->message_type_count == 0) {
        NMO_RETURN_OK();
    }

    nmo_status_t result;

    /* Write identifier */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESSAGEMANAGER);
    if (result != NMO_OK) return result;

    /* Write message type count */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->message_type_count);
    if (result != NMO_OK) return result;

    /* Write each message type name */
    for (uint32_t i = 0; i < in_state->message_type_count; i++) {
        const char *name = in_state->message_type_names[i];
        if (name == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Message type name is missing");
        }
        result = nmo_chunk_write_string(out_chunk, name);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_messagemanager_serialize(
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

    nmo_status_t result = nmo_messagemanager_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* =============================================================================
 * Vtable + registration
 * ============================================================================= */

static nmo_status_t nmo_messagemanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_messagemanager_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_messagemanager_validate(instance, type, context);
}

nmo_status_t nmo_messagemanager_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_messagemanager_remap_dependencies");
    }

    nmo_messagemanager_state_t *state = (nmo_messagemanager_state_t *)instance;

    return nmo_messagemanager_validate(state, NULL, NULL);
}

static nmo_status_t nmo_messagemanager_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_messagemanager_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_messagemanager_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_messagemanager_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    if (src == NULL || dst == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_messagemanager_state_t *source = src;
    NMO_RETURN_IF_ERROR(nmo_messagemanager_validate(
        source, type, NULL));

    const char **names = NULL;
    if (source->message_type_count > 0) {
        names = nmo_arena_alloc(
            arena,
            (size_t)source->message_type_count * sizeof(*names),
            _Alignof(char *));
        if (names == NULL) return NMO_ERR_NOMEM;

        for (uint32_t i = 0; i < source->message_type_count; ++i) {
            if (source->message_type_names[i] == NULL) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            names[i] = nmo_arena_strdup(
                arena, source->message_type_names[i]);
            if (names[i] == NULL) return NMO_ERR_NOMEM;
        }
    }

    nmo_messagemanager_state_t *target = dst;
    target->message_type_count = source->message_type_count;
    target->message_type_names = names;
    return NMO_OK;
}

static nmo_status_t nmo_messagemanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;

    const nmo_messagemanager_state_t *state = instance;
    if (state->message_type_count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (state->message_type_count > 0 &&
        state->message_type_names == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < state->message_type_count; ++i) {
        if (state->message_type_names[i] == NULL) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    return NMO_OK;
}

static bool nmo_messagemanager_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;

    const nmo_messagemanager_state_t *lhs = a;
    const nmo_messagemanager_state_t *rhs = b;
    if (lhs->message_type_count != rhs->message_type_count) return false;
    if (lhs->message_type_count > 0 &&
        (lhs->message_type_names == NULL || rhs->message_type_names == NULL)) {
        return false;
    }
    for (uint32_t i = 0; i < lhs->message_type_count; ++i) {
        const char *lhs_name = lhs->message_type_names[i];
        const char *rhs_name = rhs->message_type_names[i];
        if (lhs_name == rhs_name) continue;
        if (lhs_name == NULL || rhs_name == NULL ||
            strcmp(lhs_name, rhs_name) != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t nmo_messagemanager_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_messagemanager_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_messagemanager_state_t *state = instance;
    uint32_t hash = nmo_messagemanager_hash_bytes(
        2166136261u, &state->message_type_count,
        sizeof(state->message_type_count));
    if (state->message_type_names == NULL) return hash;

    for (uint32_t i = 0; i < state->message_type_count; ++i) {
        const char *name = state->message_type_names[i];
        const uint8_t present = name != NULL;
        hash = nmo_messagemanager_hash_bytes(
            hash, &present, sizeof(present));
        if (present) {
            hash = nmo_messagemanager_hash_bytes(
                hash, name, strlen(name));
        }
    }
    return hash;
}

nmo_type_vtable_t nmo_messagemanager_vtable = {
    .prepare_dependencies = nmo_messagemanager_prepare_dependencies,
    .remap_dependencies = nmo_messagemanager_remap_dependencies,
    .pre_delete = nmo_messagemanager_pre_delete,
    .post_delete = nmo_messagemanager_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_messagemanager_create,
        nmo_messagemanager_destroy,
        nmo_messagemanager_serialize,
        nmo_messagemanager_deserialize,
        nmo_messagemanager_copy,
        nmo_messagemanager_validate,
        nmo_messagemanager_equals,
        nmo_messagemanager_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_messagemanager_type,
    NMO_MANAGER_GUID_MESSAGE,
    "CKMessageManager",
    0,
    NMO_GUID_NULL,
    nmo_messagemanager_state_t,
    &nmo_messagemanager_vtable,
    nmo_messagemanager_fields)





