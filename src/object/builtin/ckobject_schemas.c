/**
 * @file ckobject_schemas.c
 * @brief CKObject class hierarchy schema definitions with serialize/deserialize implementations
 *
 * Implements the schema-driven object deserialization system as required by TODO.md P0.1.
 * This replaces the old placeholder deserialization with proper schema-based approach.
 */

#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>
#include <stdalign.h>

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_object_fields[] = {
    NMO_FIELD(nmo_object_state_t, visibility_flags, NMO_GUID_ENUM_CK_OBJECT_FLAGS)
};

/* =============================================================================
 * CKObject LIFECYCLE
 * ============================================================================= */

static nmo_status_t nmo_object_schema_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_object_schema_create");
    }

    nmo_object_state_t *state = (nmo_object_state_t *)instance;
    memset(state, 0, sizeof(*state));
    state->visibility_flags = NMO_CKOBJECT_VISIBLE;
    NMO_RETURN_OK();
}

static void nmo_object_schema_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        return;
    }
    memset(instance, 0, sizeof(nmo_object_state_t));
}

static bool nmo_object_state_equals(const void *a, const void *b)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return memcmp(a, b, sizeof(nmo_object_state_t)) == 0;
}

static uint32_t nmo_object_state_hash(const void *instance)
{
    if (!instance) {
        return 0;
    }
    return (uint32_t)nmo_hash_fnv1a(instance, sizeof(nmo_object_state_t));
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
nmo_status_t nmo_object_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    nmo_object_state_t *out_state = (nmo_object_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_object_deserialize");
    }

    /* Check for OBJECTHIDDEN identifier (highest priority) */
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
    if (result == NMO_OK) {
        /* Object is completely hidden (no VISIBLE, no HIERARCHICAL) */
        out_state->visibility_flags = 0;
        NMO_RETURN_OK();
    }
    if (result != NMO_ERR_NOT_FOUND) return result;

    /* Check for OBJECTHIERAHIDDEN identifier */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
    if (result == NMO_OK) {
        /* Object is hierarchically hidden (no VISIBLE, but has HIERARCHICAL) */
        out_state->visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
        NMO_RETURN_OK();
    }
    if (result != NMO_ERR_NOT_FOUND) return result;

    /* No special identifiers found -> object is visible (default already set) */
    out_state->visibility_flags = NMO_CKOBJECT_VISIBLE;
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
nmo_status_t nmo_object_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_object_state_t *in_state = (const nmo_object_state_t *)instance;
    
    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "Invalid arguments to nmo_object_serialize");
    }

    /* Write appropriate identifier based on visibility state */
    if (in_state->visibility_flags & NMO_CKOBJECT_HIERARCHICAL) {
        /* Hierarchically hidden */
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJECTHIERAHIDDEN));
    } else if ((in_state->visibility_flags & NMO_CKOBJECT_VISIBLE) == 0) {
        /* Completely hidden */
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJECTHIDDEN));
    }
    /* If visible (default), no identifier is written */

    NMO_RETURN_OK();
}

nmo_status_t nmo_object_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_object_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_object_remap_dependencies");
    }
    nmo_object_state_t *state = (nmo_object_state_t *)instance;
    state->visibility_flags &= (NMO_CKOBJECT_VISIBLE | NMO_CKOBJECT_HIERARCHICAL);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_object_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_object_pre_delete");
    }
    nmo_object_state_t *state = (nmo_object_state_t *)instance;
    state->visibility_flags &= (NMO_CKOBJECT_VISIBLE | NMO_CKOBJECT_HIERARCHICAL);
    NMO_RETURN_OK();
}

static void nmo_object_post_delete(
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

/* NOTE: This schema cannot use NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(object, ...)
 * because format/nmo_object.h declares nmo_object_create/nmo_object_destroy.
 */
nmo_type_vtable_t nmo_object_vtable = {
    .prepare_dependencies = nmo_object_prepare_dependencies,
    .remap_dependencies = nmo_object_remap_dependencies,
    .pre_delete = nmo_object_pre_delete,
    .post_delete = nmo_object_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_object_schema_create,
        nmo_object_schema_destroy,
        nmo_object_serialize,
        nmo_object_deserialize,
        nmo_object_default_copy,
        nmo_object_default_validate,
        nmo_object_state_equals,
        nmo_object_state_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_object_type,
    CKPGUID_OBJECT,
    "CKObject",
    NMO_CID_OBJECT,
    (nmo_guid_t){0},
    nmo_object_state_t,
    &nmo_object_vtable,
    nmo_object_fields)






