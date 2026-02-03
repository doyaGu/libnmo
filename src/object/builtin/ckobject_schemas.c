/**
 * @file ckobject_schemas.c
 * @brief CKObject class hierarchy schema definitions with serialize/deserialize implementations
 *
 * Implements the schema-driven object deserialization system as required by TODO.md P0.1.
 * This replaces the old placeholder deserialization with proper schema-based approach.
 */

#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>
#include <stdalign.h>

/* =============================================================================
 * CKObject LIFECYCLE
 * ============================================================================= */

static nmo_status_t nmo_ckobject_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_ckobject_create");
    }

    nmo_ckobject_state_t *state = (nmo_ckobject_state_t *)instance;
    memset(state, 0, sizeof(*state));
    state->visibility_flags = NMO_CKOBJECT_VISIBLE;
    NMO_RETURN_OK();
}

static void nmo_ckobject_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        return;
    }
    memset(instance, 0, sizeof(nmo_ckobject_state_t));
}

/* =============================================================================
 * CKObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKObject state from chunk
 * 
 * Implements the symmetric read operation for CKObject::Load.
 * Uses identifier-based reading as per Virtools convention.
 * 
 * Reference: reference/src/CKObject.cpp:87-103
 * 
 * @param chunk Chunk containing CKObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    nmo_ckobject_state_t *out_state = (nmo_ckobject_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckobject_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_ckobject_create(out_state, type, context));

    /* Check for OBJECTHIDDEN identifier (highest priority) */
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
    if (result == NMO_OK) {
        /* Object is completely hidden (no VISIBLE, no HIERARCHICAL) */
        out_state->visibility_flags = 0;
        NMO_RETURN_OK();
    }

    /* Check for OBJECTHIERAHIDDEN identifier */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
    if (result == NMO_OK) {
        /* Object is hierarchically hidden (no VISIBLE, but has HIERARCHICAL) */
        out_state->visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
        NMO_RETURN_OK();
    }

    /* No special identifiers found -> object is visible (default already set) */
    NMO_RETURN_OK();
}

/* =============================================================================
 * CKObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKObject state to chunk
 * 
 * Implements the symmetric write operation for CKObject::Save.
 * Uses identifier-based writing as per Virtools convention.
 * 
 * Reference: reference/src/CKObject.cpp:75-85
 * 
 * @param in_state  Input state structure to serialize (must not be NULL)
 * @param out_chunk Output chunk to write to (must not be NULL)
 * @param arena     Arena for temporary allocations (not needed for CKObject)
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckobject_state_t *in_state = (const nmo_ckobject_state_t *)instance;
    
    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments to nmo_ckobject_serialize");
    }

    /* Write appropriate identifier based on visibility state */
    if ((in_state->visibility_flags & NMO_CKOBJECT_VISIBLE) == 0) {
        if (in_state->visibility_flags & NMO_CKOBJECT_HIERARCHICAL) {
            /* Hierarchically hidden */
            nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
        } else {
            /* Completely hidden */
            nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJECTHIDDEN);
        }
    }
    /* If visible (default), no identifier is written */

    NMO_RETURN_OK();
}

/* =============================================================================
 * FINISH LOADING (Phase 15 - PostLoad equivalent)
 * ============================================================================= */

/**
 * @brief Finish loading CKObject (base implementation)
 * 
 * Base class implementation does nothing. Derived classes override to perform
 * reference resolution and runtime initialization.
 * 
 * @param state Object state (unused in base implementation)
 * @param context Serialization context (unused in base implementation)
 * @return Always NMO_OK
 */
nmo_status_t nmo_ckobject_finish_loading(
    void *state,
    void *context)
{
    /* Base implementation does nothing */
    (void)state;
    (void)context;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckobject,
    nmo_ckobject_state_t,
    nmo_ckobject_serialize,
    nmo_ckobject_deserialize,
    NMO_GUID_CKOBJECT,
    "CKObject",
    NMO_CID_OBJECT,
    (nmo_guid_t){0}
)

