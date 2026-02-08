/**
 * @file ckmessagemanager_schemas.c
 * @brief CKMessageManager schema implementation
 *
 * Implements schema-driven deserialization for CKMessageManager (message type registry).
 * This is a manager class that handles message type registration and routing.
 * 
 * Based on official Virtools SDK (reference/src/CKMessageManager.cpp:178-250).
 */

#include "object/nmo_ckmessagemanager_schemas.h"
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
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckmessagemanager, nmo_ckmessagemanager_state_t)

/* =============================================================================
 * IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKMessageManager.cpp */
#define CK_STATESAVE_MESSAGEMANAGER 0x53

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckmessagemanager_fields[] = {
    NMO_FIELD(nmo_ckmessagemanager_state_t, message_type_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_ckmessagemanager_state_t, message_type_names, CKPGUID_STRING)
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
nmo_status_t nmo_ckmessagemanager_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckmessagemanager_state_t *out_state = (nmo_ckmessagemanager_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmessagemanager_deserialize");
    }

    /* Seek identifier */
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESSAGEMANAGER);
    if (result != NMO_OK) {
        /* No data to load - this is valid */
        NMO_RETURN_OK();
    }

    /* Read message type count */
    int32_t type_count;
    result = nmo_chunk_read_int(chunk, &type_count);
    if (result != NMO_OK) return result;

    if (type_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid message type count");
    }

    if (type_count == 0) {
        NMO_RETURN_OK();
    }

    if (type_count > 10000) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid message type count");
    }

    out_state->message_type_count = (uint32_t)type_count;
    out_state->message_type_names = (const char **)nmo_arena_alloc(
        arena, type_count * sizeof(char *), _Alignof(char *));
    if (!out_state->message_type_names) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate message type names");
    }

    /* Read each message type name */
    for (int32_t i = 0; i < type_count; i++) {
        char *name = NULL;
        nmo_chunk_read_string(chunk, &name);
        out_state->message_type_names[i] = name; /* Chunk manages the buffer */
    }

    NMO_RETURN_OK();
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
nmo_status_t nmo_ckmessagemanager_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckmessagemanager_state_t *in_state =
        (const nmo_ckmessagemanager_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmessagemanager_serialize");
    }

    if (in_state->message_type_count > 0 && in_state->message_type_names == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Message type names missing");
    }

    /* Don't write if no used message types (all names empty) */
    uint32_t used_count = 0;
    for (uint32_t i = 0; i < in_state->message_type_count; i++) {
        const char *name = in_state->message_type_names
            ? in_state->message_type_names[i]
            : NULL;
        if (name && name[0] != '\0') {
            used_count++;
            break;
        }
    }
    if (used_count == 0) {
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
        result = nmo_chunk_write_string(out_chunk, name ? name : "");
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * Vtable + registration
 * ============================================================================= */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    ckmessagemanager,
    nmo_ckmessagemanager_state_t,
    nmo_ckmessagemanager_serialize,
    nmo_ckmessagemanager_deserialize,
    nmo_ckmessagemanager_fields,
    NMO_MANAGER_GUID_MESSAGE,
    "CKMessageManager",
    0,
    NMO_GUID_NULL
)


