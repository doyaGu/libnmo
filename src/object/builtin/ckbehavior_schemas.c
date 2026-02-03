/**
 * @file ckbehavior_schemas.c
 * @brief CKBehavior schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKBehavior (behavior graphs and building blocks).
 * CKBehavior extends CKObject and is the core of Virtools' behavior system.
 * 
 * Based on official Virtools SDK (reference/src/CKBehavior.cpp:1472-1900):
 * - CKBehavior can be a building block (GUID-based function) or graph (sub-behaviors)
 * - Contains complex graph structure with I/O, parameters, operations, and links
 * - Supports multiple data versions and file/non-file contexts
 * 
 * COMPLETE IMPLEMENTATION including:
 * - Legacy format support (DataVersion < 5)
 * - Non-file context recursive loading
 * - UseFunction/UseGraph mode handling
 * - Runtime flag filtering
 */

#include "object/nmo_ckbehavior_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_cksceneobject_schemas.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    ckbehavior,
    nmo_ckbehavior_state_t,
    do { \
        state->compatible_class_id = NMO_CID_BEOBJECT; \
    } while (0),
    ((void)0))

/* Legacy identifier values (older CK2 builds) */
#define CK_STATESAVE_BEHAVIORINTERFACE_LEGACY      0x00000001u
#define CK_STATESAVE_BEHAVIORNEWDATA_LEGACY        0x00000002u
#define CK_STATESAVE_BEHAVIORSINGLEACTIVITY_LEGACY 0x00000004u

/* Behavior flags (subset) from CKEnums.h */
#define CKBEHAVIOR_ACTIVE                    0x00000001u
#define CKBEHAVIOR_SCRIPT                    0x00000002u
#define CKBEHAVIOR_PRIORITY                  0x00000004u
#define CKBEHAVIOR_USEFUNCTION               0x00000008u
#define CKBEHAVIOR_COMPATIBLECLASSID         0x00000010u
#define CKBEHAVIOR_BUILDINGBLOCK             0x00008000u
#define CKBEHAVIOR_TARGETABLE                0x00040000u
#define CKBEHAVIOR_EXECUTEDLASTFRAME         0x00200000u
#define CKBEHAVIOR_DEACTIVATENEXTFRAME       0x00400000u
#define CKBEHAVIOR_RESETNEXTFRAME            0x00800000u
#define CKBEHAVIOR_ACTIVATENEXTFRAME         0x10000000u
#define CKBEHAVIOR_LOCKED                    0x20000000u
#define CKBEHAVIOR_LAUNCHEDONCE              0x80000000u

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Read object ID array using XObjectPointerArray format
 */
static nmo_status_t read_object_sequence(nmo_chunk_t *chunk, nmo_arena_t *arena,
                                         nmo_object_id_t **out_ids, uint32_t *out_count) {
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;

    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        NMO_RETURN_OK();
    }

    const uint32_t MAX_ARRAY_SIZE = 100000;
    if (count > MAX_ARRAY_SIZE) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Array count exceeds maximum");
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(arena, count * sizeof(nmo_object_id_t),
                                                  _Alignof(nmo_object_id_t));
    if (!*out_ids) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object ID array");
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_chunk_read_object_sequence_item(chunk, &(*out_ids)[i]);
        if (result != NMO_OK) {
            *out_count = i;
            break;
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Write object ID array using XObjectPointerArray format
 */
static nmo_status_t write_object_sequence(nmo_chunk_t *chunk, const nmo_object_id_t *ids, uint32_t count) {
    nmo_status_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < count; i++) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t read_object_subchunk_list(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    nmo_chunk_t ***out_chunks,
    uint32_t *out_count)
{
    int32_t count = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count);
    if (result != NMO_OK) return result;

    if (count <= 0) {
        *out_ids = NULL;
        *out_chunks = NULL;
        *out_count = 0;
        NMO_RETURN_OK();
    }

    const uint32_t MAX_ARRAY_SIZE = 100000;
    if ((uint32_t)count > MAX_ARRAY_SIZE) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Array count exceeds maximum");
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(arena, count * sizeof(nmo_object_id_t),
                                                  _Alignof(nmo_object_id_t));
    *out_chunks = (nmo_chunk_t **)nmo_arena_alloc(arena, count * sizeof(nmo_chunk_t *),
                                                  _Alignof(nmo_chunk_t *));
    if (!*out_ids || !*out_chunks) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate sub-chunk list");
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_chunk_read_object_id(chunk, &(*out_ids)[i]);
        if (result != NMO_OK) {
            *out_count = i;
            break;
        }
        (void)nmo_chunk_read_sub_chunk(chunk, &(*out_chunks)[i]);
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKBehavior DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBehavior state from chunk
 * 
 * Implements the symmetric read operation for CKBehavior::Load.
 * Reads behavior flags, graph data, parameters, and I/O arrays.
 * 
 * Reference: reference/src/CKBehavior.cpp:1648-1900
 * 
 * @param chunk Chunk containing CKBehavior data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckbehavior_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckbehavior_state_t *out_state = (nmo_ckbehavior_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehavior_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_ckbehavior_create(out_state, type, context));
    
    /* Deserialize base CKObject state (merged into this chunk by AddChunkAndDelete) */
    {
        nmo_status_t result = nmo_chunk_start_read(chunk);
        if (result != NMO_OK) return result;

        result = nmo_ckobject_deserialize(&out_state->base.base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    {
        nmo_status_t result = nmo_chunk_start_read(chunk);
        if (result != NMO_OK) return result;
    }
    
    const bool is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    const uint32_t data_version = nmo_chunk_get_data_version(chunk);

    uint32_t newdata_id = CK_STATESAVE_BEHAVIORNEWDATA;
    nmo_status_t newdata_seek = nmo_chunk_seek_identifier(chunk, newdata_id);
    if (newdata_seek != NMO_OK) {
        newdata_id = CK_STATESAVE_BEHAVIORNEWDATA_LEGACY;
        newdata_seek = nmo_chunk_seek_identifier(chunk, newdata_id);
        if (newdata_seek == NMO_OK) {
            out_state->use_legacy_identifiers = true;
        }
    }

    if (is_file && data_version >= 5 && newdata_seek != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "CKBehavior: missing NEWDATA identifier in file-mode chunk");
    }

    if (!is_file && newdata_seek != NMO_OK) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSUBBEHAV) == NMO_OK) {
            (void)read_object_subchunk_list(chunk, arena,
                                            &out_state->sub_behaviors,
                                            &out_state->sub_behavior_chunks,
                                            &out_state->sub_behavior_count);
            out_state->sub_behavior_chunk_count = out_state->sub_behavior_count;
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS) == NMO_OK) {
            (void)read_object_subchunk_list(chunk, arena,
                                            &out_state->local_parameters,
                                            &out_state->local_parameter_chunks,
                                            &out_state->local_parameter_count);
            out_state->local_parameter_chunk_count = out_state->local_parameter_count;
        }

        NMO_RETURN_OK();
    }

    /* Main behavior data */
    nmo_status_t result = newdata_seek;
    if (result == NMO_OK) {
        uint32_t flags = 0;
        if (data_version >= 5) {
            result = nmo_chunk_read_dword(chunk, &flags);
            if (result != NMO_OK) return result;

            out_state->flags = flags & ~(CKBEHAVIOR_ACTIVE |
                                         CKBEHAVIOR_PRIORITY |
                                         CKBEHAVIOR_COMPATIBLECLASSID |
                                         CKBEHAVIOR_EXECUTEDLASTFRAME |
                                         CKBEHAVIOR_DEACTIVATENEXTFRAME |
                                         CKBEHAVIOR_RESETNEXTFRAME |
                                         CKBEHAVIOR_ACTIVATENEXTFRAME);

            if (flags & CKBEHAVIOR_BUILDINGBLOCK) {
                result = nmo_chunk_read_guid(chunk, &out_state->block_guid);
                if (result != NMO_OK) return result;

                result = nmo_chunk_read_dword(chunk, &out_state->block_version);
                if (result != NMO_OK) return result;
            }

            if (flags & CKBEHAVIOR_PRIORITY) {
                result = nmo_chunk_read_int(chunk, &out_state->priority);
                if (result != NMO_OK) return result;
            }

            if (flags & CKBEHAVIOR_COMPATIBLECLASSID) {
                result = nmo_chunk_read_int(chunk, &out_state->compatible_class_id);
                if (result != NMO_OK) return result;
            }

            if (flags & CKBEHAVIOR_TARGETABLE) {
                result = nmo_chunk_read_object_id(chunk, &out_state->target_parameter_id);
                if (result != NMO_OK) return result;
            }

            uint32_t save_flags = 0;
            result = nmo_chunk_read_dword(chunk, &save_flags);
            if (result != NMO_OK) return result;

            out_state->save_flags = save_flags;
            out_state->has_save_flags = true;

            uint32_t graph_save_flags = save_flags;
            if (flags & CKBEHAVIOR_BUILDINGBLOCK) {
                graph_save_flags &= ~(CK_STATESAVE_BEHAVIORSUBBEHAV |
                                      CK_STATESAVE_BEHAVIORSUBLINKS |
                                      CK_STATESAVE_BEHAVIOROPERATIONS);
            }

            if (graph_save_flags & CK_STATESAVE_BEHAVIORSUBBEHAV) {
                result = read_object_sequence(chunk, arena, &out_state->sub_behaviors,
                                              &out_state->sub_behavior_count);
                if (result != NMO_OK) return result;
            }

            if (graph_save_flags & CK_STATESAVE_BEHAVIORSUBLINKS) {
                result = read_object_sequence(chunk, arena, &out_state->sub_behavior_links,
                                              &out_state->sub_behavior_link_count);
                if (result != NMO_OK) return result;
            }

            if (graph_save_flags & CK_STATESAVE_BEHAVIOROPERATIONS) {
                result = read_object_sequence(chunk, arena, &out_state->operations,
                                              &out_state->operation_count);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORINPARAMS) {
                result = read_object_sequence(chunk, arena, &out_state->in_parameters,
                                              &out_state->in_parameter_count);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIOROUTPARAMS) {
                result = read_object_sequence(chunk, arena, &out_state->out_parameters,
                                              &out_state->out_parameter_count);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORLOCALPARAMS) {
                result = read_object_sequence(chunk, arena, &out_state->local_parameters,
                                              &out_state->local_parameter_count);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORINPUTS) {
                result = read_object_sequence(chunk, arena, &out_state->inputs,
                                              &out_state->input_count);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIOROUTPUTS) {
                result = read_object_sequence(chunk, arena, &out_state->outputs,
                                              &out_state->output_count);
                if (result != NMO_OK) return result;
            }
        } else {
            result = nmo_chunk_read_guid(chunk, &out_state->block_guid);
            if (result != NMO_OK) return result;

            result = nmo_chunk_read_dword(chunk, &flags);
            if (result != NMO_OK) return result;
            out_state->flags = flags & ~(CKBEHAVIOR_ACTIVATENEXTFRAME |
                                         CKBEHAVIOR_RESETNEXTFRAME |
                                         CKBEHAVIOR_DEACTIVATENEXTFRAME |
                                         CKBEHAVIOR_EXECUTEDLASTFRAME);

            {
                uint32_t tmp_class_id = 0;
                result = nmo_chunk_read_dword(chunk, &tmp_class_id);
                if (result != NMO_OK) return result;
                out_state->compatible_class_id = (int32_t)tmp_class_id;
            }

            result = nmo_chunk_read_dword(chunk, &out_state->behavior_type);
            if (result != NMO_OK) return result;
            if (out_state->behavior_type == 1) {
                out_state->flags |= CKBEHAVIOR_SCRIPT;
            }

            result = nmo_chunk_read_int(chunk, &out_state->priority);
            if (result != NMO_OK) return result;

            result = nmo_chunk_read_object_id(chunk, &out_state->owner_id);
            if (result != NMO_OK) return result;

            if (out_state->flags & CKBEHAVIOR_BUILDINGBLOCK) {
                result = nmo_chunk_read_dword(chunk, &out_state->block_version);
                if (result != NMO_OK) return result;
                if (out_state->block_version == 0) {
                    out_state->block_version = 0x10000u;
                }
            } else {
                uint32_t tmp = 0;
                (void)nmo_chunk_read_dword(chunk, &tmp);
            }
        }
    } else {
        nmo_guid_t guid = {0};
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORPROTOGUID) == NMO_OK) {
            (void)nmo_chunk_read_guid(chunk, &guid);
            out_state->block_guid = guid;
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORFLAGS) == NMO_OK) {
            (void)nmo_chunk_read_int(chunk, (int32_t *)&out_state->flags);
            if (out_state->flags & CKBEHAVIOR_USEFUNCTION) {
                out_state->flags |= CKBEHAVIOR_BUILDINGBLOCK;
                out_state->block_guid = guid;
            }
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORCOMPATIBLECID) == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, (uint32_t *)&out_state->compatible_class_id);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORTYPE) == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, &out_state->behavior_type);
            if (out_state->behavior_type == 1) {
                out_state->flags |= CKBEHAVIOR_SCRIPT;
            }
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROWNER) == NMO_OK) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->owner_id);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORPRIORITY) == NMO_OK) {
            (void)nmo_chunk_read_int(chunk, &out_state->priority);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORTARGET) == NMO_OK) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->target_parameter_id);
        }
    }

    /* Optional: Interface chunk (for editing mode) */
    uint32_t interface_id = CK_STATESAVE_BEHAVIORINTERFACE;
    result = nmo_chunk_seek_identifier(chunk, interface_id);
    if (result != NMO_OK) {
        interface_id = CK_STATESAVE_BEHAVIORINTERFACE_LEGACY;
        result = nmo_chunk_seek_identifier(chunk, interface_id);
        if (result == NMO_OK) {
            out_state->use_legacy_identifiers = true;
        }
    }
    if (result == NMO_OK) {
        out_state->has_interface = true;
        nmo_chunk_t *interface_chunk = NULL;
        nmo_status_t sub_result = nmo_chunk_read_sub_chunk(chunk, &interface_chunk);
        if (sub_result == NMO_OK) {
            out_state->interface_chunk = interface_chunk;
        } else {
            out_state->interface_chunk = NULL;
        }
    }

    /* Optional: Single activity flags */
    uint32_t single_activity_id = out_state->use_legacy_identifiers
        ? CK_STATESAVE_BEHAVIORSINGLEACTIVITY_LEGACY
        : CK_STATESAVE_BEHAVIORSINGLEACTIVITY;
    result = nmo_chunk_seek_identifier(chunk, single_activity_id);
    if (result == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->single_activity_flags);
        if (result == NMO_OK) {
            out_state->has_single_activity = true;
        }
    }

    if (nmo_chunk_get_data_version(chunk) < 5) {
        if (out_state->flags & CKBEHAVIOR_BUILDINGBLOCK) {
            NMO_RETURN_OK();
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSUBBEHAV) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->sub_behaviors,
                                       &out_state->sub_behavior_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSUBLINKS) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->sub_behavior_links,
                                       &out_state->sub_behavior_link_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROPERATIONS) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->operations,
                                       &out_state->operation_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORINPARAMS) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->in_parameters,
                                       &out_state->in_parameter_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->local_parameters,
                                       &out_state->local_parameter_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROUTPARAMS) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->out_parameters,
                                       &out_state->out_parameter_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORINPUTS) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->inputs,
                                       &out_state->input_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROUTPUTS) == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->outputs,
                                       &out_state->output_count);
        }
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKBehavior SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBehavior state to chunk
 * 
 * Implements the symmetric write operation for CKBehavior::Save.
 * Writes behavior flags, graph data, parameters, and I/O arrays.
 * 
 * Reference: reference/src/CKBehavior.cpp:1472-1647
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckbehavior_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckbehavior_state_t *in_state = (const nmo_ckbehavior_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehavior_serialize");
    }

    const bool is_file = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    const bool write_file_format = is_file;

    if (write_file_format && !in_state->has_save_flags &&
        (in_state->flags & CKBEHAVIOR_BUILDINGBLOCK) == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Missing CKBehavior save flags");
    }

    /* Start write mode for behavior chunk */
    nmo_status_t result = nmo_chunk_start_write(out_chunk);
    if (result != NMO_OK) return result;

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_ckobject_serialize(&in_state->base.base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (!write_file_format) {
        if (in_state->sub_behavior_count > 0 && in_state->sub_behaviors) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORSUBBEHAV);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->sub_behavior_count);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->sub_behavior_count; ++i) {
                result = nmo_chunk_write_object_id(out_chunk, in_state->sub_behaviors[i]);
                if (result != NMO_OK) return result;
                nmo_chunk_t *sub = NULL;
                if (in_state->sub_behavior_chunks && i < in_state->sub_behavior_chunk_count) {
                    sub = in_state->sub_behavior_chunks[i];
                }
                if (!sub) {
                    sub = nmo_chunk_create(arena);
                }
                result = nmo_chunk_write_sub_chunk(out_chunk, sub);
                if (result != NMO_OK) return result;
            }
        }

        if ((in_state->flags & CKBEHAVIOR_BUILDINGBLOCK)) {
            NMO_RETURN_OK();
        }

        if (in_state->local_parameter_count > 0 || in_state->local_parameter_chunks) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->local_parameter_count);
            if (result != NMO_OK) return result;
            if (in_state->local_parameter_count > 0) {
                for (uint32_t i = 0; i < in_state->local_parameter_count; ++i) {
                    result = nmo_chunk_write_object_id(out_chunk, in_state->local_parameters[i]);
                    if (result != NMO_OK) return result;
                    nmo_chunk_t *sub = NULL;
                    if (in_state->local_parameter_chunks && i < in_state->local_parameter_chunk_count) {
                        sub = in_state->local_parameter_chunks[i];
                    }
                    if (!sub) {
                        sub = nmo_chunk_create(arena);
                    }
                    result = nmo_chunk_write_sub_chunk(out_chunk, sub);
                    if (result != NMO_OK) return result;
                }
            }
        }

        NMO_RETURN_OK();
    }

    /* Optional: Interface chunk */
    if (in_state->has_interface || in_state->interface_chunk) {
        const uint32_t interface_id = CK_STATESAVE_BEHAVIORINTERFACE;
        result = nmo_chunk_write_identifier(out_chunk, interface_id);
        if (result != NMO_OK) return result;

        if (in_state->interface_chunk) {
            result = nmo_chunk_write_sub_chunk(out_chunk, in_state->interface_chunk);
            if (result != NMO_OK) return result;
        } else {
            result = nmo_chunk_write_dword(out_chunk, 0);
            if (result != NMO_OK) return result;
        }
    }

    /* Main behavior data */
    const uint32_t newdata_id = CK_STATESAVE_BEHAVIORNEWDATA;
    result = nmo_chunk_write_identifier(out_chunk, newdata_id);
    if (result != NMO_OK) return result;

    /* Write behavior flags */
    uint32_t behavior_flags = in_state->flags;
    if (!(behavior_flags & CKBEHAVIOR_BUILDINGBLOCK)) {
        behavior_flags &= ~CKBEHAVIOR_LOCKED;
    }
    if (in_state->priority != 0) {
        behavior_flags |= CKBEHAVIOR_PRIORITY;
    } else {
        behavior_flags &= ~CKBEHAVIOR_PRIORITY;
    }
    if (in_state->compatible_class_id != NMO_CID_BEOBJECT) {
        behavior_flags |= CKBEHAVIOR_COMPATIBLECLASSID;
    } else {
        behavior_flags &= ~CKBEHAVIOR_COMPATIBLECLASSID;
    }
    behavior_flags &= ~CKBEHAVIOR_LAUNCHEDONCE;

    result = nmo_chunk_write_dword(out_chunk, behavior_flags);
    if (result != NMO_OK) return result;

    /* Write building block data */
    if (behavior_flags & CKBEHAVIOR_BUILDINGBLOCK) {
        result = nmo_chunk_write_guid(out_chunk, in_state->block_guid);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->block_version);
        if (result != NMO_OK) return result;
    }

    /* Write priority */
    if (behavior_flags & CKBEHAVIOR_PRIORITY) {
        result = nmo_chunk_write_int(out_chunk, in_state->priority);
        if (result != NMO_OK) return result;
    }

    /* Write compatible class ID */
    if (behavior_flags & CKBEHAVIOR_COMPATIBLECLASSID) {
        result = nmo_chunk_write_int(out_chunk, in_state->compatible_class_id);
        if (result != NMO_OK) return result;
    }

    /* Write target parameter */
    if (behavior_flags & CKBEHAVIOR_TARGETABLE) {
        result = nmo_chunk_write_object_id(out_chunk, in_state->target_parameter_id);
        if (result != NMO_OK) return result;
    }

    /* Calculate or preserve save flags */
    uint32_t save_flags = 0;
    if (in_state->has_save_flags) {
        save_flags = in_state->save_flags;
    } else {
        if (in_state->sub_behavior_count > 0) save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV;
        else save_flags &= ~CK_STATESAVE_BEHAVIORSUBBEHAV;
        if (in_state->sub_behavior_link_count > 0) save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORSUBLINKS;
        if (in_state->operation_count > 0) save_flags |= CK_STATESAVE_BEHAVIOROPERATIONS;
        else save_flags &= ~CK_STATESAVE_BEHAVIOROPERATIONS;
        if (in_state->in_parameter_count > 0) save_flags |= CK_STATESAVE_BEHAVIORINPARAMS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORINPARAMS;
        if (in_state->out_parameter_count > 0) save_flags |= CK_STATESAVE_BEHAVIOROUTPARAMS;
        else save_flags &= ~CK_STATESAVE_BEHAVIOROUTPARAMS;
        if (in_state->local_parameter_count > 0) save_flags |= CK_STATESAVE_BEHAVIORLOCALPARAMS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORLOCALPARAMS;
        if (in_state->input_count > 0) save_flags |= CK_STATESAVE_BEHAVIORINPUTS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORINPUTS;
        if (in_state->output_count > 0) save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;
        else save_flags &= ~CK_STATESAVE_BEHAVIOROUTPUTS;
    }

    result = nmo_chunk_write_dword(out_chunk, save_flags);
    if (result != NMO_OK) return result;

    /* Write arrays */
    uint32_t graph_save_flags = save_flags;
    if (behavior_flags & CKBEHAVIOR_BUILDINGBLOCK) {
        graph_save_flags &= ~(CK_STATESAVE_BEHAVIORSUBBEHAV |
                              CK_STATESAVE_BEHAVIORSUBLINKS |
                              CK_STATESAVE_BEHAVIOROPERATIONS);
    }

    if (graph_save_flags & CK_STATESAVE_BEHAVIORSUBBEHAV) {
        result = write_object_sequence(out_chunk, in_state->sub_behaviors, in_state->sub_behavior_count);
        if (result != NMO_OK) return result;
    }

    if (graph_save_flags & CK_STATESAVE_BEHAVIORSUBLINKS) {
        result = write_object_sequence(out_chunk, in_state->sub_behavior_links, in_state->sub_behavior_link_count);
        if (result != NMO_OK) return result;
    }

    if (graph_save_flags & CK_STATESAVE_BEHAVIOROPERATIONS) {
        result = write_object_sequence(out_chunk, in_state->operations, in_state->operation_count);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORINPARAMS) {
        result = write_object_sequence(out_chunk, in_state->in_parameters, in_state->in_parameter_count);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIOROUTPARAMS) {
        result = write_object_sequence(out_chunk, in_state->out_parameters, in_state->out_parameter_count);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORLOCALPARAMS) {
        result = write_object_sequence(out_chunk, in_state->local_parameters, in_state->local_parameter_count);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORINPUTS) {
        result = write_object_sequence(out_chunk, in_state->inputs, in_state->input_count);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIOROUTPUTS) {
        result = write_object_sequence(out_chunk, in_state->outputs, in_state->output_count);
        if (result != NMO_OK) return result;
    }

    /* Optional: Single activity flags */
    if (in_state->has_single_activity) {
        const uint32_t single_activity_id = in_state->use_legacy_identifiers
            ? CK_STATESAVE_BEHAVIORSINGLEACTIVITY_LEGACY
            : CK_STATESAVE_BEHAVIORSINGLEACTIVITY;
        result = nmo_chunk_write_identifier(out_chunk, single_activity_id);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->single_activity_flags);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckbehavior,
    nmo_ckbehavior_state_t,
    nmo_ckbehavior_serialize,
    nmo_ckbehavior_deserialize,
    NMO_GUID_CKBEHAVIOR,
    "CKBehavior",
    NMO_CID_BEHAVIOR,
    NMO_GUID_CKSCENEOBJECT
)

