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
        /* The engine writes a message type that is not in use as a null name. */
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
        result = nmo_chunk_write_string(out_chunk, name);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_messagemanager)

/* =============================================================================
 * Vtable + registration
 * ============================================================================= */

static nmo_status_t nmo_messagemanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_messagemanager)

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

/* One name of the list, as a record of its own. */
typedef struct nmo_messagemanager_name {
    const char *text;
} nmo_messagemanager_name_t;

static const nmo_object_state_member_t nmo_messagemanager_name_members[] = {
    NMO_STATE_STRING(nmo_messagemanager_name_t, text)
};

static const nmo_object_state_layout_t nmo_messagemanager_name_layout = {
    .size = sizeof(nmo_messagemanager_name_t),
    .members = nmo_messagemanager_name_members,
    .member_count = sizeof(nmo_messagemanager_name_members) /
        sizeof(nmo_messagemanager_name_members[0]),
};

static const nmo_object_state_member_t nmo_messagemanager_members[] = {
    NMO_STATE_VALUE(nmo_messagemanager_state_t, message_type_count),
    NMO_STATE_COUNTED_RECORDS(nmo_messagemanager_state_t, message_type_names,
                              message_type_count, nmo_messagemanager_name_layout)
};

static const nmo_object_state_layout_t nmo_messagemanager_layout = {
    .size = sizeof(nmo_messagemanager_state_t),
    .members = nmo_messagemanager_members,
    .member_count = sizeof(nmo_messagemanager_members) /
        sizeof(nmo_messagemanager_members[0]),
    .validate = nmo_messagemanager_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(messagemanager, nmo_messagemanager_layout)

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
    return NMO_OK;
}

nmo_type_vtable_t nmo_messagemanager_vtable = {
    .prepare_dependencies = nmo_messagemanager_prepare_dependencies,
    .remap_dependencies = nmo_messagemanager_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
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





