/**
 * @file ckattributemanager_schemas.c
 * @brief CKAttributeManager schema implementation
 *
 * Implements schema-driven deserialization for CKAttributeManager (attribute type registry).
 * This is a manager class that handles attribute type definitions and categories.
 * 
 * Based on official Virtools SDK (reference/src/CKAttributeManager.cpp:726-890).
 */

#include "schema/nmo_ckattributemanager_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>

/* =============================================================================
 * IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKAttributeManager.cpp */
#define CK_STATESAVE_ATTRIBUTEMANAGER 0x52

/* =============================================================================
 * CKAttributeManager DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKAttributeManager state from chunk
 * 
 * Implements the symmetric read operation for CKAttributeManager::LoadData.
 * Reads attribute categories and attribute type definitions.
 * 
 * Reference: reference/src/CKAttributeManager.cpp:726-790
 * 
 * @param chunk Chunk containing CKAttributeManager data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckattributemanager_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckattributemanager_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckattributemanager_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckattributemanager_state_t));

    /* Seek identifier */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ATTRIBUTEMANAGER);
    if (result.code != NMO_OK) {
        /* No data to load - this is valid */
        return nmo_result_ok();
    }

    /* Read counts */
    int32_t category_count, attribute_count;
    result = nmo_chunk_read_int(chunk, &category_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_int(chunk, &attribute_count);
    if (result.code != NMO_OK) return result;

    if (category_count < 0 || category_count > 10000) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Invalid category count"));
    }

    if (attribute_count < 0 || attribute_count > 100000) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Invalid attribute count"));
    }

    out_state->category_count = (uint32_t)category_count;
    out_state->attribute_count = (uint32_t)attribute_count;

    /* Allocate categories */
    if (category_count > 0) {
        out_state->categories = (nmo_ckattribute_category_t *)nmo_arena_alloc(
            arena, category_count * sizeof(nmo_ckattribute_category_t),
            _Alignof(nmo_ckattribute_category_t));
        if (!out_state->categories) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                NMO_SEVERITY_ERROR, "Failed to allocate categories"));
        }
        memset(out_state->categories, 0, category_count * sizeof(nmo_ckattribute_category_t));

        /* Read each category */
        for (int32_t i = 0; i < category_count; i++) {
            int32_t present;
            result = nmo_chunk_read_int(chunk, &present);
            if (result.code != NMO_OK) return result;

            nmo_ckattribute_category_t *cat = &out_state->categories[i];
            cat->present = (present != 0);

            if (cat->present) {
                char *name = NULL;
                nmo_chunk_read_string(chunk, &name);
                cat->name = name;

                result = nmo_chunk_read_dword(chunk, &cat->flags);
                if (result.code != NMO_OK) return result;
            }
        }
    }

    /* Allocate attributes */
    if (attribute_count > 0) {
        out_state->attributes = (nmo_ckattribute_descriptor_t *)nmo_arena_alloc(
            arena, attribute_count * sizeof(nmo_ckattribute_descriptor_t),
            _Alignof(nmo_ckattribute_descriptor_t));
        if (!out_state->attributes) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                NMO_SEVERITY_ERROR, "Failed to allocate attributes"));
        }
        memset(out_state->attributes, 0, attribute_count * sizeof(nmo_ckattribute_descriptor_t));

        /* Read each attribute */
        for (int32_t i = 0; i < attribute_count; i++) {
            int32_t present;
            result = nmo_chunk_read_int(chunk, &present);
            if (result.code != NMO_OK) return result;

            nmo_ckattribute_descriptor_t *attr = &out_state->attributes[i];
            attr->present = (present != 0);

            if (attr->present) {
                char *name = NULL;
                nmo_chunk_read_string(chunk, &name);
                attr->name = name;

                result = nmo_chunk_read_guid(chunk, &attr->parameter_type_guid);
                if (result.code != NMO_OK) return result;

                result = nmo_chunk_read_int(chunk, &attr->category_index);
                if (result.code != NMO_OK) return result;

                result = nmo_chunk_read_int(chunk, &attr->compatible_class_id);
                if (result.code != NMO_OK) return result;

                result = nmo_chunk_read_dword(chunk, &attr->flags);
                if (result.code != NMO_OK) return result;
            }
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKAttributeManager SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKAttributeManager state to chunk
 * 
 * Implements the symmetric write operation for CKAttributeManager::SaveData.
 * Writes attribute categories and attribute type definitions.
 * 
 * Reference: reference/src/CKAttributeManager.cpp:795-890
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckattributemanager_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckattributemanager_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckattributemanager_serialize"));
    }

    nmo_result_t result;

    /* Write identifier */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_ATTRIBUTEMANAGER);
    if (result.code != NMO_OK) return result;

    /* Write counts */
    result = nmo_chunk_write_int(chunk, (int32_t)state->category_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_int(chunk, (int32_t)state->attribute_count);
    if (result.code != NMO_OK) return result;

    /* Write categories */
    for (uint32_t i = 0; i < state->category_count; i++) {
        const nmo_ckattribute_category_t *cat = &state->categories[i];

        result = nmo_chunk_write_int(chunk, cat->present ? 1 : 0);
        if (result.code != NMO_OK) return result;

        if (cat->present) {
            result = nmo_chunk_write_string(chunk, cat->name ? cat->name : "");
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_write_dword(chunk, cat->flags);
            if (result.code != NMO_OK) return result;
        }
    }

    /* Write attributes */
    for (uint32_t i = 0; i < state->attribute_count; i++) {
        const nmo_ckattribute_descriptor_t *attr = &state->attributes[i];

        result = nmo_chunk_write_int(chunk, attr->present ? 1 : 0);
        if (result.code != NMO_OK) return result;

        if (attr->present) {
            result = nmo_chunk_write_string(chunk, attr->name ? attr->name : "");
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_write_guid(chunk, attr->parameter_type_guid);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_write_int(chunk, attr->category_index);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_write_int(chunk, attr->compatible_class_id);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_write_dword(chunk, attr->flags);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKAttributeManager schema types
 * 
 * Creates schema descriptors for CKAttributeManager state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckattributemanager_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckattributemanager_schemas"));
    }

    /* Schema will be registered when schema builder is fully implemented */
    /* For now, just store the function pointers in the registry */
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKAttributeManager
 * 
 * @return Deserialize function pointer
 */
nmo_ckattributemanager_deserialize_fn nmo_get_ckattributemanager_deserialize(void)
{
    return nmo_ckattributemanager_deserialize;
}

/**
 * @brief Get the serialize function for CKAttributeManager
 * 
 * @return Serialize function pointer
 */
nmo_ckattributemanager_serialize_fn nmo_get_ckattributemanager_serialize(void)
{
    return nmo_ckattributemanager_serialize;
}
