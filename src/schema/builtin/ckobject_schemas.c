/**
 * @file ckobject_schemas.c
 * @brief CKObject class hierarchy schema definitions with serialize/deserialize implementations
 *
 * Implements the schema-driven object deserialization system as required by TODO.md P0.1.
 * This replaces the old placeholder deserialization with proper schema-based approach.
 */

#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>
#include <stdalign.h>

/* =============================================================================
 * CKObject IDENTIFIER CONSTANTS
 * ============================================================================= */

/* Identifier constants from CKDefines.h */
#define CK_STATESAVE_OBJECTHIDDEN          0x00000001
#define CK_STATESAVE_OBJECTHIERAHIDDEN     0x00000002

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
nmo_result_t nmo_ckobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckobject_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckobject_deserialize"));
    }

    /* Initialize to default (visible) */
    out_state->visibility_flags = NMO_CKOBJECT_VISIBLE;

    /* Check for OBJECTHIDDEN identifier (highest priority) */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
    if (result.code == NMO_OK) {
        /* Object is completely hidden (no VISIBLE, no HIERARCHICAL) */
        out_state->visibility_flags = 0;
        return nmo_result_ok();
    }

    /* Check for OBJECTHIERAHIDDEN identifier */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
    if (result.code == NMO_OK) {
        /* Object is hierarchically hidden (no VISIBLE, but has HIERARCHICAL) */
        out_state->visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
        return nmo_result_ok();
    }

    /* No special identifiers found -> object is visible (default already set) */
    return nmo_result_ok();
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
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_result_t nmo_ckobject_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckobject_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckobject_serialize");
        return nmo_result_error(err);
    }

    /* Write appropriate identifier based on visibility state */
    if ((state->visibility_flags & NMO_CKOBJECT_VISIBLE) == 0) {
        if (state->visibility_flags & NMO_CKOBJECT_HIERARCHICAL) {
            /* Hierarchically hidden */
            nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
        } else {
            /* Completely hidden */
            nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
        }
    }
    /* If visible (default), no identifier is written */

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA VTABLE (for schema registry integration)
 * ============================================================================= */

/**
 * @brief Vtable read wrapper for CKObject
 * 
 * Adapts nmo_ckobject_deserialize to match nmo_schema_vtable_t signature.
 */
static nmo_result_t nmo_ckobject_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type; /* Type info not needed for CKObject */
    return nmo_ckobject_deserialize(chunk, arena, (nmo_ckobject_state_t *)out_ptr);
}

/**
 * @brief Vtable write wrapper for CKObject
 * 
 * Adapts nmo_ckobject_serialize to match nmo_schema_vtable_t signature.
 */
static nmo_result_t nmo_ckobject_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr)
{
    (void)type; /* Type info not needed for CKObject */
    return nmo_ckobject_serialize(chunk, (const nmo_ckobject_state_t *)in_ptr);
}

/**
 * @brief Vtable for CKObject schema
 */
static const nmo_schema_vtable_t nmo_ckobject_vtable = {
    .read = nmo_ckobject_vtable_read,
    .write = nmo_ckobject_vtable_write,
    .validate = NULL  /* No custom validation */
};

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKObject schema types
 * 
 * Creates schema descriptors for CKObject state structures with vtable.
 * This enables schema registry-based deserialization in parser.c Phase 14.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (registry == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckobject_schemas"));
    }

    /* Get base types */
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "uint32_t");
    if (uint32_type == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required type uint32_t not found in registry"));
    }

    /* Register CKObject state structure with vtable */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKObjectState", 
                                                      sizeof(nmo_ckobject_state_t),
                                                      alignof(nmo_ckobject_state_t));
    
    nmo_builder_add_field_ex(&builder, "visibility_flags", uint32_type,
                            offsetof(nmo_ckobject_state_t, visibility_flags),
                            0);  /* No special annotations */
    
    /* Attach vtable for optimized read/write */
    nmo_builder_set_vtable(&builder, &nmo_ckobject_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/**
 * @brief Get CKObject deserialize function pointer
 * 
 * Provides access to the deserialization function for use in parser.c Phase 14.
 * 
 * @return Function pointer to nmo_ckobject_deserialize
 */
nmo_ckobject_deserialize_fn nmo_get_ckobject_deserialize(void) {
    return nmo_ckobject_deserialize;
}

/**
 * @brief Get CKObject serialize function pointer
 * 
 * Provides access to the serialization function for use in save pipeline.
 * 
 * @return Function pointer to nmo_ckobject_serialize
 */
nmo_ckobject_serialize_fn nmo_get_ckobject_serialize(void) {
    return nmo_ckobject_serialize;
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
 * @param arena Arena for allocations (unused in base implementation)
 * @param repository Object repository (unused in base implementation)
 * @return Always NMO_OK
 */
nmo_result_t nmo_ckobject_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    /* Base implementation does nothing */
    (void)state;
    (void)arena;
    (void)repository;
    return nmo_result_ok();
}

/**
 * @brief Get the finish_loading function for CKObject
 * @return Finish loading function pointer
 */
nmo_ckobject_finish_loading_fn nmo_get_ckobject_finish_loading(void)
{
    return nmo_ckobject_finish_loading;
}
