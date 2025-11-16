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

#include "schema/nmo_ckbeobject_schemas.h"
#include "schema/nmo_cksceneobject_schemas.h"
#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * CKBeObject IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From CKDefines.h */
#define CK_STATESAVE_SCRIPTS         0x00000003
#define CK_STATESAVE_DATAS           0x00000004
#define CK_STATESAVE_NEWATTRIBUTES   0x00000010

/* Attribute Manager GUID from CKAttributeManager.h */
#define ATTRIBUTE_MANAGER_GUID_D1    0x6BED328B
#define ATTRIBUTE_MANAGER_GUID_D2    0x141F5148
#define CK_STATESAVE_SINGLEACTIVITY  0x00000020

/* DATAS version flag */
#define CK_DATAS_VERSION_FLAG        0x10000000

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
static nmo_result_t nmo_ckbeobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbeobject_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbeobject_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckbeobject_state_t));
    
    /* Deserialize base CKSceneObject state first */
    nmo_cksceneobject_deserialize_fn parent_deserialize = nmo_get_cksceneobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }
    
    /* Default priority is 0 */
    out_state->priority = 0;

    /* Load scripts array - optional section */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCRIPTS);
    if (result.code == NMO_OK) {
        /* Read script count */
        uint32_t script_count;
        result = nmo_chunk_read_dword(chunk, &script_count);
        if (result.code != NMO_OK) {
            /* Log warning but continue - might be malformed data */
            (void)arena; /* Identifier found but data malformed - skip section */
            goto load_priority;
        }

        /* Allocate script array */
        if (script_count > 0) {
            /* Sanity check - prevent excessive allocations */
            const uint32_t MAX_SCRIPTS = 10000; /* Reasonable upper bound */
            if (script_count > MAX_SCRIPTS) {
                /* Likely corrupted data - skip section */
                goto load_priority;
            }
            
            out_state->script_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena,
                script_count * sizeof(nmo_object_id_t),
                _Alignof(nmo_object_id_t)
            );
            if (!out_state->script_ids) {
                /* Allocation failed - skip section */
                goto load_priority;
            }

            /* Read script IDs */
            for (uint32_t i = 0; i < script_count; i++) {
                result = nmo_chunk_read_object_id(chunk, &out_state->script_ids[i]);
                if (result.code != NMO_OK) {
                    /* Partial data read failure - skip remaining scripts */
                    out_state->script_count = i; /* Save what we got */
                    goto load_priority;
                }
            }
            out_state->script_count = script_count;
        }
    }
    /* If identifier not found, scripts section is optional - continue */

load_priority:
    /* Load priority data - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAS);
    if (result.code == NMO_OK) {
        uint32_t version_flag;
        result = nmo_chunk_read_dword(chunk, &version_flag);
        if (result.code != NMO_OK) {
            /* Identifier found but data malformed - skip section */
            goto load_attributes;
        }

        /* Check if modern format (version >= 5 with 0x10000000 flag) */
        if (version_flag & CK_DATAS_VERSION_FLAG) {
            int32_t priority;
            result = nmo_chunk_read_int(chunk, &priority);
            if (result.code == NMO_OK) {
                out_state->priority = priority;
            }
            /* If read failed, use default priority=0 */
        }
    }
    /* If identifier not found, priority section is optional - continue with default priority=0 */

load_attributes:
    /* Load attributes - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_NEWATTRIBUTES);
    if (result.code == NMO_OK) {
        /* Read attribute object sequence using proper sequence API
         * Reference: CKBeObject.cpp line 537: const int attrCount = chunk->StartReadSequence(); */
        size_t attr_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &attr_count);
        if (result.code != NMO_OK) {
            /* Identifier found but sequence start failed - skip section */
            goto deserialize_done;
        }

        if (attr_count > 0) {
            /* Allocate arrays for attributes */
            out_state->attribute_parameter_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena,
                attr_count * sizeof(nmo_object_id_t),
                _Alignof(nmo_object_id_t)
            );
            out_state->attribute_types = (uint32_t *)nmo_arena_alloc(
                arena,
                attr_count * sizeof(uint32_t),
                _Alignof(uint32_t)
            );
            
            if (!out_state->attribute_parameter_ids || !out_state->attribute_types) {
                /* Allocation failed - skip section */
                out_state->attribute_parameter_ids = NULL;
                out_state->attribute_types = NULL;
                goto deserialize_done;
            }

            /* Read attribute parameter object IDs 
             * Reference: CKBeObject.cpp lines 542-544 */
            for (size_t i = 0; i < attr_count; i++) {
                result = nmo_chunk_read_object_id(chunk, &out_state->attribute_parameter_ids[i]);
                if (result.code != NMO_OK) {
                    /* Partial data read failure - save what we got */
                    out_state->attribute_count = i;
                    goto load_manager_sequence;
                }
            }

            /* Skip sub-chunk sequence if present (only used in non-file mode)
             * Reference: CKBeObject.cpp lines 548-552
             * In non-file mode, there's a sub-chunk sequence here that we skip */
            
load_manager_sequence:
            /* C requires a statement after a label */
            ;
            /* Read manager sequence for attribute types
             * Reference: CKBeObject.cpp lines 555-577 */
            nmo_guid_t manager_guid;
            size_t seq_count = 0;
            result = nmo_chunk_start_manager_read_sequence(chunk, &manager_guid, &seq_count);
            if (result.code == NMO_OK && seq_count == attr_count) {
                /* Verify it's the attribute manager GUID 
                 * Reference: CKBeObject.cpp line 556 checks managerGuid == ATTRIBUTE_MANAGER_GUID */
                if (manager_guid.d1 == ATTRIBUTE_MANAGER_GUID_D1 && 
                    manager_guid.d2 == ATTRIBUTE_MANAGER_GUID_D2) {
                    /* Read attribute types from manager sequence 
                     * Note: Reference uses StartReadSequence() here which seems incorrect,
                     * it should just read DWORDs directly */
                    for (size_t i = 0; i < attr_count; i++) {
                        uint32_t attr_type;
                        result = nmo_chunk_read_dword(chunk, &attr_type);
                        if (result.code == NMO_OK) {
                            out_state->attribute_types[i] = attr_type;
                        } else {
                            /* Partial read - save what we got */
                            out_state->attribute_count = i;
                            goto deserialize_done;
                        }
                    }
                    out_state->attribute_count = attr_count;
                } else {
                    /* Wrong manager GUID - data might be corrupted */
                    goto deserialize_done;
                }
            } else if (result.code != NMO_OK || seq_count != attr_count) {
                /* Manager sequence not found or count mismatch - skip */
                goto deserialize_done;
            }
        }
    }
    /* If identifier not found, attributes section is optional - continue */

deserialize_done:
    /* Deserialization completed - all sections are optional */
    return nmo_result_ok();
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
static nmo_result_t nmo_ckbeobject_serialize(
    const nmo_ckbeobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbeobject_serialize"));
    }

    /* Write base class (CKSceneObject) data */
    nmo_cksceneobject_serialize_fn parent_serialize = nmo_get_cksceneobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) return result;
    }

    /* Write scripts if present */
    if (in_state->script_count > 0 && in_state->script_ids) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCRIPTS);
        if (result.code != NMO_OK) return result;

        /* Write script count */
        result = nmo_chunk_write_dword(out_chunk, in_state->script_count);
        if (result.code != NMO_OK) return result;

        /* Write script IDs */
        for (uint32_t i = 0; i < in_state->script_count; i++) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->script_ids[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    /* Write priority data if non-zero */
    if (in_state->priority != 0) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAS);
        if (result.code != NMO_OK) return result;

        /* Write version flag (modern format) */
        result = nmo_chunk_write_dword(out_chunk, CK_DATAS_VERSION_FLAG);
        if (result.code != NMO_OK) return result;

        /* Write priority value */
        result = nmo_chunk_write_int(out_chunk, in_state->priority);
        if (result.code != NMO_OK) return result;
    }

    /* Write attributes if present */
    if (in_state->attribute_count > 0 && in_state->attribute_parameter_ids && in_state->attribute_types) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_NEWATTRIBUTES);
        if (result.code != NMO_OK) return result;

        /* Start object ID sequence */
        result = nmo_chunk_write_dword(out_chunk, in_state->attribute_count);
        if (result.code != NMO_OK) return result;

        /* Write attribute parameter object IDs */
        for (uint32_t i = 0; i < in_state->attribute_count; i++) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->attribute_parameter_ids[i]);
            if (result.code != NMO_OK) return result;
        }

        /* Write manager sequence for attribute types */
        /* Note: We use a placeholder GUID - in real implementation this should be ATTRIBUTE_MANAGER_GUID */
        nmo_guid_t attr_mgr_guid = {0x6BED328B, 0x141F5148}; /* ATTRIBUTE_MANAGER_GUID */
        result = nmo_chunk_start_manager_sequence(out_chunk, attr_mgr_guid, in_state->attribute_count);
        if (result.code != NMO_OK) return result;

        /* Write attribute types */
        for (uint32_t i = 0; i < in_state->attribute_count; i++) {
            result = nmo_chunk_write_dword(out_chunk, in_state->attribute_types[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA VTABLE (for schema registry integration)
 * ============================================================================= */

/**
 * @brief Vtable read wrapper for CKBeObject
 * 
 * Adapts nmo_ckbeobject_deserialize to match nmo_schema_vtable_t signature.
 */
static nmo_result_t nmo_ckbeobject_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type; /* Type info not needed for CKBeObject */
    return nmo_ckbeobject_deserialize(chunk, arena, (nmo_ckbeobject_state_t *)out_ptr);
}

/**
 * @brief Vtable write wrapper for CKBeObject
 * 
 * Adapts nmo_ckbeobject_serialize to match nmo_schema_vtable_t signature.
 */
static nmo_result_t nmo_ckbeobject_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type; /* Type info not needed for CKBeObject */
    return nmo_ckbeobject_serialize((const nmo_ckbeobject_state_t *)in_ptr, chunk, arena);
}

/**
 * @brief Vtable for CKBeObject schema
 */
static const nmo_schema_vtable_t nmo_ckbeobject_vtable = {
    .read = nmo_ckbeobject_vtable_read,
    .write = nmo_ckbeobject_vtable_write,
    .validate = NULL  /* No custom validation */
};

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKBeObject schema types
 * 
 * Creates schema descriptors for CKBeObject state structures with vtable.
 * This enables schema registry-based deserialization in parser.c Phase 14.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckbeobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckbeobject_schemas"));
    }

    /* Get base types for fields */
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *int32_type = nmo_schema_registry_find_by_name(registry, "i32");
    const nmo_schema_type_t *object_id_type = nmo_schema_registry_find_by_name(registry, "ObjectID");
    
    if (!uint32_type || !int32_type || !object_id_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required base types not found in registry"));
    }

    /* Register CKBeObject state structure with vtable */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKBeObjectState",
                                                      sizeof(nmo_ckbeobject_state_t),
                                                      alignof(nmo_ckbeobject_state_t));
    
    /* Add fields (simplified - full implementation would include all fields) */
    nmo_builder_add_field_ex(&builder, "script_count", uint32_type,
                            offsetof(nmo_ckbeobject_state_t, script_count), 0);
    nmo_builder_add_field_ex(&builder, "priority", int32_type,
                            offsetof(nmo_ckbeobject_state_t, priority), 0);
    nmo_builder_add_field_ex(&builder, "attribute_count", uint32_type,
                            offsetof(nmo_ckbeobject_state_t, attribute_count), 0);
    
    /* Attach vtable for optimized read/write */
    nmo_builder_set_vtable(&builder, &nmo_ckbeobject_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Map class ID to schema */
    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKBeObjectState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_BEOBJECT, type);
        if (result.code != NMO_OK) {
            return result;
        }
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKBeObject
 * 
 * @return Deserialize function pointer
 */
nmo_ckbeobject_deserialize_fn nmo_get_ckbeobject_deserialize(void)
{
    return nmo_ckbeobject_deserialize;
}

/**
 * @brief Get the serialize function for CKBeObject
 * 
 * @return Serialize function pointer
 */
nmo_ckbeobject_serialize_fn nmo_get_ckbeobject_serialize(void)
{
    return nmo_ckbeobject_serialize;
}
