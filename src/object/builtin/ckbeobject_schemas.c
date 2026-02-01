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

#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_cksceneobject_schemas.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_schema.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * CKBeObject IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From CKDefines2.h (CK_STATESAVEFLAGS_BEOBJECT) */
#define CK_STATESAVE_ATTRIBUTES      0x00000010u
#define CK_STATESAVE_NEWATTRIBUTES   0x00000011u
#define CK_STATESAVE_DATAS           0x00000040u
#define CK_STATESAVE_BEHAVIORS       0x00000100u
#define CK_STATESAVE_SINGLEACTIVITY  0x00000400u
#define CK_STATESAVE_SCRIPTS         0x00000800u

/* Attribute Manager GUID from CKEnums.h */
#define ATTRIBUTE_MANAGER_GUID_D1    0x3d242466u
#define ATTRIBUTE_MANAGER_GUID_D2    0x00000000u

/* DATAS version flag */
#define CK_DATAS_VERSION_FLAG        0x10000000

/* =============================================================================
 * IDENTIFIER HELPERS
 * ============================================================================= */

static size_t nmo_ckbeobject_identifier_remaining_dwords(nmo_chunk_t *chunk)
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

static nmo_result_t nmo_ckbeobject_read_raw_bytes(nmo_chunk_t *chunk, void *buffer, size_t bytes)
{
    if (!chunk || !buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbeobject_read_raw_bytes"));
    }

    size_t dwords = (bytes + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, dwords, "Insufficient data for raw buffer");

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(buffer, &data[state->current_pos], bytes);
    state->current_pos += dwords;

    return nmo_result_ok();
}

static nmo_result_t nmo_ckbeobject_read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result.code != NMO_OK) {
        return result;
    }

    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        return nmo_result_ok();
    }

    if (count > UINT32_MAX) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Invalid object sequence count"));
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, sizeof(nmo_object_id_t) * (*out_count), _Alignof(nmo_object_id_t));
    if (!*out_ids) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate object ID array"));
    }

    for (uint32_t i = 0; i < *out_count; ++i) {
        result = nmo_chunk_read_object_sequence_item(chunk, &(*out_ids)[i]);
        if (result.code != NMO_OK) {
            *out_count = i;
            return nmo_result_ok();
        }
    }

    return nmo_result_ok();
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
    
    const bool is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    /* Default priority is 0 */
    out_state->priority = 0;

    /* Load scripts array - optional section (legacy + modern) */
    nmo_result_t result = nmo_result_ok();
    if (is_file && nmo_chunk_get_data_version(chunk) < 5) {
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORS);
        if (result.code == NMO_OK) {
            (void)nmo_ckbeobject_read_object_sequence(chunk, arena,
                                                      &out_state->script_ids,
                                                      &out_state->script_count);
        }
    }

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCRIPTS);
    if (result.code == NMO_OK) {
        (void)nmo_ckbeobject_read_object_sequence(chunk, arena,
                                                  &out_state->script_ids,
                                                  &out_state->script_count);
    }

    /* Load priority data - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAS);
    if (result.code == NMO_OK) {
        if (!is_file) {
            int32_t ignored = 0;
            (void)nmo_chunk_read_int(chunk, &ignored);
            goto load_attributes;
        }

        uint32_t version_flag = 0;
        result = nmo_chunk_read_dword(chunk, &version_flag);
        if (result.code != NMO_OK) {
            goto load_attributes;
        }

        if (nmo_chunk_get_data_version(chunk) < 5) {
            int32_t ignored = 0;
            (void)nmo_chunk_read_int(chunk, &ignored);
            (void)nmo_chunk_read_int(chunk, &ignored);
            (void)nmo_chunk_read_int(chunk, &ignored);
            (void)nmo_chunk_read_int(chunk, &out_state->priority);
        } else if (version_flag & CK_DATAS_VERSION_FLAG) {
            (void)nmo_chunk_read_int(chunk, &out_state->priority);
        } else {
            out_state->priority = 0;
        }
    }

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
                result = nmo_chunk_read_object_sequence_item(chunk,
                                                            &out_state->attribute_parameter_ids[i]);
                if (result.code != NMO_OK) {
                    out_state->attribute_count = (uint32_t)i;
                    break;
                }
            }

            if (!is_file) {
                size_t sub_count = 0;
                result = nmo_chunk_start_read_sub_chunk_sequence(chunk, &sub_count);
                if (result.code == NMO_OK && sub_count > 0) {
                    out_state->attribute_chunks = (nmo_chunk_t **)nmo_arena_alloc(
                        arena, sizeof(nmo_chunk_t *) * sub_count, _Alignof(nmo_chunk_t *));
                    if (out_state->attribute_chunks) {
                        out_state->attribute_chunk_count = (uint32_t)sub_count;
                        for (size_t i = 0; i < sub_count; ++i) {
                            (void)nmo_chunk_read_sub_chunk(chunk, &out_state->attribute_chunks[i]);
                        }
                    }
                }
            }

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
                    /* Read attribute types from manager sequence */
                    for (size_t i = 0; i < attr_count; i++) {
                        uint32_t attr_type;
                        result = nmo_chunk_read_dword(chunk, &attr_type);
                        if (result.code == NMO_OK) {
                            out_state->attribute_types[i] = attr_type;
                        } else {
                            /* Partial read - save what we got */
                            out_state->attribute_count = (uint32_t)i;
                            goto deserialize_done;
                        }
                    }
                    out_state->attribute_count = (uint32_t)attr_count;
                } else {
                    /* Wrong manager GUID - data might be corrupted */
                    goto deserialize_done;
                }
            } else if (result.code != NMO_OK || seq_count != attr_count) {
                /* Manager sequence not found or count mismatch - skip */
                goto deserialize_done;
            }
        }
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ATTRIBUTES).code == NMO_OK) {
        /* Legacy attribute payload - preserve raw bytes for round-trip */
        size_t remaining_dwords = nmo_ckbeobject_identifier_remaining_dwords(chunk);
        size_t remaining_bytes = remaining_dwords * 4;
        if (remaining_bytes > 0) {
            out_state->legacy_attributes_raw = nmo_arena_alloc(arena, remaining_bytes, 1);
            if (out_state->legacy_attributes_raw) {
                (void)nmo_ckbeobject_read_raw_bytes(chunk,
                                                    out_state->legacy_attributes_raw,
                                                    remaining_bytes);
                out_state->legacy_attributes_size = remaining_bytes;
            }
        }
    }
    /* If identifier not found, attributes section is optional - continue */

    if (is_file && nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SINGLEACTIVITY).code == NMO_OK) {
        out_state->has_single_activity = 1;
        (void)nmo_chunk_read_dword(chunk, &out_state->single_activity_flags);
    }

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

    const bool is_file = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    /* Write scripts if present (file mode only) */
    if (is_file && in_state->script_count > 0 && in_state->script_ids) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCRIPTS);
        if (result.code != NMO_OK) return result;

        /* Write script object sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->script_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->script_count; i++) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->script_ids[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    /* Write priority data if non-zero (file mode only) */
    if (is_file && in_state->priority != 0) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAS);
        if (result.code != NMO_OK) return result;

        /* Write version flag (modern format) */
        result = nmo_chunk_write_dword(out_chunk, CK_DATAS_VERSION_FLAG);
        if (result.code != NMO_OK) return result;

        /* Write priority value */
        result = nmo_chunk_write_int(out_chunk, in_state->priority);
        if (result.code != NMO_OK) return result;
    }

    /* Write legacy attributes if no modern attributes were decoded */
    if (in_state->legacy_attributes_raw && in_state->legacy_attributes_size > 0 &&
        in_state->attribute_count == 0) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ATTRIBUTES);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                in_state->legacy_attributes_raw,
                                                in_state->legacy_attributes_size);
        if (result.code != NMO_OK) return result;
    }

    /* Write attributes if present */
    if (in_state->attribute_count > 0 && in_state->attribute_parameter_ids && in_state->attribute_types) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_NEWATTRIBUTES);
        if (result.code != NMO_OK) return result;

        /* Start object ID sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->attribute_count);
        if (result.code != NMO_OK) return result;

        /* Write attribute parameter object IDs */
        for (uint32_t i = 0; i < in_state->attribute_count; i++) {
            result = nmo_chunk_write_object_sequence_item(out_chunk,
                                                          in_state->attribute_parameter_ids[i]);
            if (result.code != NMO_OK) return result;
        }

        if (!is_file) {
            result = nmo_chunk_start_sub_chunk_sequence(out_chunk, in_state->attribute_count);
            if (result.code != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->attribute_count; i++) {
                nmo_chunk_t *sub = NULL;
                if (in_state->attribute_chunks && i < in_state->attribute_chunk_count) {
                    sub = in_state->attribute_chunks[i];
                }
                if (!sub) {
                    sub = nmo_chunk_create(arena);
                }
                result = nmo_chunk_write_sub_chunk(out_chunk, sub);
                if (result.code != NMO_OK) return result;
            }
        }

        /* Write manager sequence for attribute types */
        nmo_guid_t attr_mgr_guid = {ATTRIBUTE_MANAGER_GUID_D1, ATTRIBUTE_MANAGER_GUID_D2};
        result = nmo_chunk_start_manager_sequence(out_chunk, attr_mgr_guid, in_state->attribute_count);
        if (result.code != NMO_OK) return result;

        /* Write attribute types */
        for (uint32_t i = 0; i < in_state->attribute_count; i++) {
            result = nmo_chunk_write_dword(out_chunk, in_state->attribute_types[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    /* Write single activity flags if present (file mode only) */
    if (is_file && in_state->has_single_activity) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SINGLEACTIVITY);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->single_activity_flags);
        if (result.code != NMO_OK) return result;
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
    nmo_builder_add_field_ex(&builder, "single_activity_flags", uint32_type,
                            offsetof(nmo_ckbeobject_state_t, single_activity_flags), 0);
    
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
