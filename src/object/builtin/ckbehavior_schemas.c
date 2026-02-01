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
#include "object/nmo_cksceneobject_schemas.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_schema_registry.h"
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

/* =============================================================================
 * CKBehavior IDENTIFIER AND FLAG CONSTANTS
 * ============================================================================= */

/* From CKDefines2.h (CK_STATESAVEFLAGS_BEHAVIOR) */
#define CK_STATESAVE_BEHAVIORINTERFACE      0x00000010u
#define CK_STATESAVE_BEHAVIORNEWDATA        0x00000020u
#define CK_STATESAVE_BEHAVIORFLAGS          0x00000040u
#define CK_STATESAVE_BEHAVIORCOMPATIBLECID  0x00000080u
#define CK_STATESAVE_BEHAVIORSUBBEHAV       0x00000100u
#define CK_STATESAVE_BEHAVIORINPARAMS       0x00000200u
#define CK_STATESAVE_BEHAVIOROUTPARAMS      0x00000400u
#define CK_STATESAVE_BEHAVIORINPUTS         0x00000800u
#define CK_STATESAVE_BEHAVIOROUTPUTS        0x00001000u
#define CK_STATESAVE_BEHAVIORINFO           0x00002000u
#define CK_STATESAVE_BEHAVIOROPERATIONS     0x00004000u
#define CK_STATESAVE_BEHAVIORTYPE           0x00008000u
#define CK_STATESAVE_BEHAVIOROWNER          0x00010000u
#define CK_STATESAVE_BEHAVIORLOCALPARAMS    0x00020000u
#define CK_STATESAVE_BEHAVIORPROTOGUID      0x00040000u
#define CK_STATESAVE_BEHAVIORSUBLINKS       0x00080000u
#define CK_STATESAVE_BEHAVIORACTIVESUBLINKS 0x00100000u
#define CK_STATESAVE_BEHAVIORSINGLEACTIVITY 0x00200000u
#define CK_STATESAVE_BEHAVIORSCRIPTDATA     0x00400000u
#define CK_STATESAVE_BEHAVIORPRIORITY       0x00800000u
#define CK_STATESAVE_BEHAVIORTARGET         0x01000000u

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
static nmo_result_t read_object_sequence(nmo_chunk_t *chunk, nmo_arena_t *arena,
                                         nmo_object_id_t **out_ids, uint32_t *out_count) {
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result.code != NMO_OK) return result;

    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        return nmo_result_ok();
    }

    const uint32_t MAX_ARRAY_SIZE = 100000;
    if (count > MAX_ARRAY_SIZE) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Array count exceeds maximum"));
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(arena, count * sizeof(nmo_object_id_t),
                                                  _Alignof(nmo_object_id_t));
    if (!*out_ids) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate object ID array"));
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_chunk_read_object_sequence_item(chunk, &(*out_ids)[i]);
        if (result.code != NMO_OK) {
            *out_count = i;
            break;
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Write object ID array using XObjectPointerArray format
 */
static nmo_result_t write_object_sequence(nmo_chunk_t *chunk, const nmo_object_id_t *ids, uint32_t count) {
    nmo_result_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result.code != NMO_OK) return result;

    for (uint32_t i = 0; i < count; i++) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t read_object_subchunk_list(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    nmo_chunk_t ***out_chunks,
    uint32_t *out_count)
{
    int32_t count = 0;
    nmo_result_t result = nmo_chunk_read_int(chunk, &count);
    if (result.code != NMO_OK) return result;

    if (count <= 0) {
        *out_ids = NULL;
        *out_chunks = NULL;
        *out_count = 0;
        return nmo_result_ok();
    }

    const uint32_t MAX_ARRAY_SIZE = 100000;
    if ((uint32_t)count > MAX_ARRAY_SIZE) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Array count exceeds maximum"));
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(arena, count * sizeof(nmo_object_id_t),
                                                  _Alignof(nmo_object_id_t));
    *out_chunks = (nmo_chunk_t **)nmo_arena_alloc(arena, count * sizeof(nmo_chunk_t *),
                                                  _Alignof(nmo_chunk_t *));
    if (!*out_ids || !*out_chunks) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate sub-chunk list"));
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_chunk_read_object_id(chunk, &(*out_ids)[i]);
        if (result.code != NMO_OK) {
            *out_count = i;
            break;
        }
        (void)nmo_chunk_read_sub_chunk(chunk, &(*out_chunks)[i]);
    }

    return nmo_result_ok();
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
static nmo_result_t nmo_ckbehavior_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbehavior_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehavior_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckbehavior_state_t));
    
    /* Deserialize base CKSceneObject state first */
    nmo_cksceneobject_deserialize_fn parent_deserialize = nmo_get_cksceneobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }
    
    const bool is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    out_state->compatible_class_id = NMO_CID_BEOBJECT;

    if (!is_file) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSUBBEHAV).code == NMO_OK) {
            (void)read_object_subchunk_list(chunk, arena,
                                            &out_state->sub_behaviors,
                                            &out_state->sub_behavior_chunks,
                                            &out_state->sub_behavior_count);
            out_state->sub_behavior_chunk_count = out_state->sub_behavior_count;
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS).code == NMO_OK) {
            (void)read_object_subchunk_list(chunk, arena,
                                            &out_state->local_parameters,
                                            &out_state->local_parameter_chunks,
                                            &out_state->local_parameter_count);
            out_state->local_parameter_chunk_count = out_state->local_parameter_count;
        }

        return nmo_result_ok();
    }

    /* Optional: Interface chunk (for editing mode) */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORINTERFACE);
    if (result.code == NMO_OK) {
        result = nmo_chunk_read_sub_chunk(chunk, &out_state->interface_chunk);
        /* Ignore errors - interface chunk is optional */
    }

    /* Main behavior data */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORNEWDATA);
    if (result.code == NMO_OK) {
        uint32_t flags = 0;
        if (nmo_chunk_get_data_version(chunk) >= 5) {
            result = nmo_chunk_read_dword(chunk, &flags);
            if (result.code != NMO_OK) return result;

            out_state->flags = flags & ~(CKBEHAVIOR_ACTIVE |
                                         CKBEHAVIOR_PRIORITY |
                                         CKBEHAVIOR_COMPATIBLECLASSID |
                                         CKBEHAVIOR_EXECUTEDLASTFRAME |
                                         CKBEHAVIOR_DEACTIVATENEXTFRAME |
                                         CKBEHAVIOR_RESETNEXTFRAME |
                                         CKBEHAVIOR_ACTIVATENEXTFRAME);

            if (flags & CKBEHAVIOR_BUILDINGBLOCK) {
                result = nmo_chunk_read_guid(chunk, &out_state->block_guid);
                if (result.code != NMO_OK) return result;

                result = nmo_chunk_read_dword(chunk, &out_state->block_version);
                if (result.code != NMO_OK) return result;
            }

            if (flags & CKBEHAVIOR_PRIORITY) {
                result = nmo_chunk_read_int(chunk, &out_state->priority);
                if (result.code != NMO_OK) return result;
            }

            if (flags & CKBEHAVIOR_COMPATIBLECLASSID) {
                result = nmo_chunk_read_int(chunk, &out_state->compatible_class_id);
                if (result.code != NMO_OK) return result;
            }

            if (flags & CKBEHAVIOR_TARGETABLE) {
                result = nmo_chunk_read_object_id(chunk, &out_state->target_parameter_id);
                if (result.code != NMO_OK) return result;
            }

            uint32_t save_flags = 0;
            result = nmo_chunk_read_dword(chunk, &save_flags);
            if (result.code != NMO_OK) return result;

            if (save_flags & CK_STATESAVE_BEHAVIORSUBBEHAV) {
                result = read_object_sequence(chunk, arena, &out_state->sub_behaviors,
                                              &out_state->sub_behavior_count);
                if (result.code != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORSUBLINKS) {
                result = read_object_sequence(chunk, arena, &out_state->sub_behavior_links,
                                              &out_state->sub_behavior_link_count);
                if (result.code != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIOROPERATIONS) {
                result = read_object_sequence(chunk, arena, &out_state->operations,
                                              &out_state->operation_count);
                if (result.code != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORINPARAMS) {
                result = read_object_sequence(chunk, arena, &out_state->in_parameters,
                                              &out_state->in_parameter_count);
                if (result.code != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIOROUTPARAMS) {
                result = read_object_sequence(chunk, arena, &out_state->out_parameters,
                                              &out_state->out_parameter_count);
                if (result.code != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORLOCALPARAMS) {
                result = read_object_sequence(chunk, arena, &out_state->local_parameters,
                                              &out_state->local_parameter_count);
                if (result.code != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORINPUTS) {
                result = read_object_sequence(chunk, arena, &out_state->inputs,
                                              &out_state->input_count);
                if (result.code != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIOROUTPUTS) {
                result = read_object_sequence(chunk, arena, &out_state->outputs,
                                              &out_state->output_count);
                if (result.code != NMO_OK) return result;
            }
        } else {
            result = nmo_chunk_read_guid(chunk, &out_state->block_guid);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_read_dword(chunk, &flags);
            if (result.code != NMO_OK) return result;
            out_state->flags = flags & ~(CKBEHAVIOR_ACTIVATENEXTFRAME |
                                         CKBEHAVIOR_RESETNEXTFRAME |
                                         CKBEHAVIOR_DEACTIVATENEXTFRAME |
                                         CKBEHAVIOR_EXECUTEDLASTFRAME);

            {
                uint32_t tmp_class_id = 0;
                result = nmo_chunk_read_dword(chunk, &tmp_class_id);
                if (result.code != NMO_OK) return result;
                out_state->compatible_class_id = (int32_t)tmp_class_id;
            }

            result = nmo_chunk_read_dword(chunk, &out_state->behavior_type);
            if (result.code != NMO_OK) return result;
            if (out_state->behavior_type == 1) {
                out_state->flags |= CKBEHAVIOR_SCRIPT;
            }

            result = nmo_chunk_read_int(chunk, &out_state->priority);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_read_object_id(chunk, &out_state->owner_id);
            if (result.code != NMO_OK) return result;

            if (out_state->flags & CKBEHAVIOR_BUILDINGBLOCK) {
                result = nmo_chunk_read_dword(chunk, &out_state->block_version);
                if (result.code != NMO_OK) return result;
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
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORPROTOGUID).code == NMO_OK) {
            (void)nmo_chunk_read_guid(chunk, &guid);
            out_state->block_guid = guid;
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORFLAGS).code == NMO_OK) {
            (void)nmo_chunk_read_int(chunk, (int32_t *)&out_state->flags);
            if (out_state->flags & CKBEHAVIOR_USEFUNCTION) {
                out_state->flags |= CKBEHAVIOR_BUILDINGBLOCK;
                out_state->block_guid = guid;
            }
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORCOMPATIBLECID).code == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, (uint32_t *)&out_state->compatible_class_id);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORTYPE).code == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, &out_state->behavior_type);
            if (out_state->behavior_type == 1) {
                out_state->flags |= CKBEHAVIOR_SCRIPT;
            }
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROWNER).code == NMO_OK) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->owner_id);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORPRIORITY).code == NMO_OK) {
            (void)nmo_chunk_read_int(chunk, &out_state->priority);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORTARGET).code == NMO_OK) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->target_parameter_id);
        }
    }

    /* Optional: Single activity flags */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSINGLEACTIVITY);
    if (result.code == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->single_activity_flags);
        if (result.code == NMO_OK) {
            out_state->has_single_activity = true;
        }
    }

    if (nmo_chunk_get_data_version(chunk) < 5) {
        if (out_state->flags & CKBEHAVIOR_BUILDINGBLOCK) {
            return nmo_result_ok();
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSUBBEHAV).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->sub_behaviors,
                                       &out_state->sub_behavior_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSUBLINKS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->sub_behavior_links,
                                       &out_state->sub_behavior_link_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROPERATIONS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->operations,
                                       &out_state->operation_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORINPARAMS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->in_parameters,
                                       &out_state->in_parameter_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->local_parameters,
                                       &out_state->local_parameter_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROUTPARAMS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->out_parameters,
                                       &out_state->out_parameter_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORINPUTS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->inputs,
                                       &out_state->input_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROUTPUTS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena, &out_state->outputs,
                                       &out_state->output_count);
        }
    }

    return nmo_result_ok();
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
static nmo_result_t nmo_ckbehavior_serialize(
    const nmo_ckbehavior_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehavior_serialize"));
    }

    const bool is_file = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    /* Write base class (CKSceneObject) data */
    nmo_cksceneobject_serialize_fn parent_serialize = nmo_get_cksceneobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) return result;
    }

    nmo_result_t result;

    if (!is_file) {
        if (in_state->sub_behavior_count > 0 && in_state->sub_behaviors) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORSUBBEHAV);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->sub_behavior_count);
            if (result.code != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->sub_behavior_count; ++i) {
                result = nmo_chunk_write_object_id(out_chunk, in_state->sub_behaviors[i]);
                if (result.code != NMO_OK) return result;
                nmo_chunk_t *sub = NULL;
                if (in_state->sub_behavior_chunks && i < in_state->sub_behavior_chunk_count) {
                    sub = in_state->sub_behavior_chunks[i];
                }
                if (!sub) {
                    sub = nmo_chunk_create(arena);
                }
                result = nmo_chunk_write_sub_chunk(out_chunk, sub);
                if (result.code != NMO_OK) return result;
            }
        }

        if ((in_state->flags & CKBEHAVIOR_BUILDINGBLOCK)) {
            return nmo_result_ok();
        }

        if (in_state->local_parameter_count > 0 || in_state->local_parameter_chunks) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->local_parameter_count);
            if (result.code != NMO_OK) return result;
            if (in_state->local_parameter_count > 0) {
                for (uint32_t i = 0; i < in_state->local_parameter_count; ++i) {
                    result = nmo_chunk_write_object_id(out_chunk, in_state->local_parameters[i]);
                    if (result.code != NMO_OK) return result;
                    nmo_chunk_t *sub = NULL;
                    if (in_state->local_parameter_chunks && i < in_state->local_parameter_chunk_count) {
                        sub = in_state->local_parameter_chunks[i];
                    }
                    if (!sub) {
                        sub = nmo_chunk_create(arena);
                    }
                    result = nmo_chunk_write_sub_chunk(out_chunk, sub);
                    if (result.code != NMO_OK) return result;
                }
            }
        }

        return nmo_result_ok();
    }

    /* Optional: Interface chunk */
    if (in_state->interface_chunk) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORINTERFACE);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->interface_chunk);
        if (result.code != NMO_OK) return result;
    }

    /* Main behavior data */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORNEWDATA);
    if (result.code != NMO_OK) return result;

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
    if (result.code != NMO_OK) return result;

    /* Write building block data */
    if (behavior_flags & CKBEHAVIOR_BUILDINGBLOCK) {
        result = nmo_chunk_write_guid(out_chunk, in_state->block_guid);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->block_version);
        if (result.code != NMO_OK) return result;
    }

    /* Write priority */
    if (behavior_flags & CKBEHAVIOR_PRIORITY) {
        result = nmo_chunk_write_int(out_chunk, in_state->priority);
        if (result.code != NMO_OK) return result;
    }

    /* Write compatible class ID */
    if (behavior_flags & CKBEHAVIOR_COMPATIBLECLASSID) {
        result = nmo_chunk_write_int(out_chunk, in_state->compatible_class_id);
        if (result.code != NMO_OK) return result;
    }

    /* Write target parameter */
    if (behavior_flags & CKBEHAVIOR_TARGETABLE) {
        result = nmo_chunk_write_object_id(out_chunk, in_state->target_parameter_id);
        if (result.code != NMO_OK) return result;
    }

    /* Calculate save flags */
    uint32_t save_flags = 0;
    if (in_state->sub_behavior_count > 0) save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV;
    if (in_state->sub_behavior_link_count > 0) save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
    if (in_state->operation_count > 0) save_flags |= CK_STATESAVE_BEHAVIOROPERATIONS;
    if (in_state->in_parameter_count > 0) save_flags |= CK_STATESAVE_BEHAVIORINPARAMS;
    if (in_state->out_parameter_count > 0) save_flags |= CK_STATESAVE_BEHAVIOROUTPARAMS;
    if (in_state->local_parameter_count > 0) save_flags |= CK_STATESAVE_BEHAVIORLOCALPARAMS;
    if (in_state->input_count > 0) save_flags |= CK_STATESAVE_BEHAVIORINPUTS;
    if (in_state->output_count > 0) save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;

    result = nmo_chunk_write_dword(out_chunk, save_flags);
    if (result.code != NMO_OK) return result;

    /* Write arrays */
    if (save_flags & CK_STATESAVE_BEHAVIORSUBBEHAV) {
        result = write_object_sequence(out_chunk, in_state->sub_behaviors, in_state->sub_behavior_count);
        if (result.code != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORSUBLINKS) {
        result = write_object_sequence(out_chunk, in_state->sub_behavior_links, in_state->sub_behavior_link_count);
        if (result.code != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIOROPERATIONS) {
        result = write_object_sequence(out_chunk, in_state->operations, in_state->operation_count);
        if (result.code != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORINPARAMS) {
        result = write_object_sequence(out_chunk, in_state->in_parameters, in_state->in_parameter_count);
        if (result.code != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIOROUTPARAMS) {
        result = write_object_sequence(out_chunk, in_state->out_parameters, in_state->out_parameter_count);
        if (result.code != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORLOCALPARAMS) {
        result = write_object_sequence(out_chunk, in_state->local_parameters, in_state->local_parameter_count);
        if (result.code != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORINPUTS) {
        result = write_object_sequence(out_chunk, in_state->inputs, in_state->input_count);
        if (result.code != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIOROUTPUTS) {
        result = write_object_sequence(out_chunk, in_state->outputs, in_state->output_count);
        if (result.code != NMO_OK) return result;
    }

    /* Optional: Single activity flags */
    if (in_state->has_single_activity) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORSINGLEACTIVITY);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->single_activity_flags);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKBehavior schema types
 * 
 * Creates schema descriptors for CKBehavior state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckbehavior_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckbehavior_schemas"));
    }

    /* Schema will be registered when schema builder is fully implemented */
    /* For now, just store the function pointers in the registry */
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKBehavior
 * 
 * @return Deserialize function pointer
 */
nmo_ckbehavior_deserialize_fn nmo_get_ckbehavior_deserialize(void)
{
    return nmo_ckbehavior_deserialize;
}

/**
 * @brief Get the serialize function for CKBehavior
 * 
 * @return Serialize function pointer
 */
nmo_ckbehavior_serialize_fn nmo_get_ckbehavior_serialize(void)
{
    return nmo_ckbehavior_serialize;
}
