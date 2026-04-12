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

#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    behavior,
    nmo_behavior_state_t,
    do { \
        state->compatible_class_id = NMO_CID_BEOBJECT; \
        nmo_status_t result = nmo_array_init(&state->sub_behaviors, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        result = nmo_array_init(&state->sub_behavior_chunks, sizeof(nmo_chunk_t *), 0, NULL); \
        if (result != NMO_OK) return result; \
        nmo_object_array_set_chunk_lifecycle(&state->sub_behavior_chunks); \
        result = nmo_array_init(&state->sub_behavior_links, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        result = nmo_array_init(&state->operations, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        result = nmo_array_init(&state->in_parameters, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        result = nmo_array_init(&state->out_parameters, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        result = nmo_array_init(&state->local_parameters, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        result = nmo_array_init(&state->local_parameter_chunks, sizeof(nmo_chunk_t *), 0, NULL); \
        if (result != NMO_OK) return result; \
        nmo_object_array_set_chunk_lifecycle(&state->local_parameter_chunks); \
        result = nmo_array_init(&state->inputs, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        result = nmo_array_init(&state->outputs, sizeof(nmo_object_id_t), 0, NULL); \
        if (result != NMO_OK) return result; \
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
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_behavior_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_behavior_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_behavior_state_t, flags, NMO_GUID_ENUM_CK_BEHAVIOR_FLAGS),
    NMO_FIELD(nmo_behavior_state_t, priority, CKPGUID_INT),
    NMO_FIELD(nmo_behavior_state_t, compatible_class_id, CKPGUID_INT),
    NMO_FIELD_REF(nmo_behavior_state_t, owner_id),
    NMO_FIELD(nmo_behavior_state_t, behavior_type, NMO_GUID_ENUM_CK_BEHAVIOR_TYPE),
    NMO_FIELD(nmo_behavior_state_t, save_flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_behavior_state_t, has_save_flags, CKPGUID_BOOL),
    NMO_FIELD(nmo_behavior_state_t, use_legacy_identifiers, CKPGUID_BOOL),
    NMO_FIELD(nmo_behavior_state_t, block_guid, CKPGUID_GUID),
    NMO_FIELD(nmo_behavior_state_t, block_version, CKPGUID_UINT32),
    NMO_FIELD_REF(nmo_behavior_state_t, target_parameter_id),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, sub_behaviors),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, sub_behavior_chunks, CKPGUID_STATECHUNK),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, sub_behavior_links),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, operations),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, in_parameters),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, out_parameters),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, local_parameters),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, local_parameter_chunks, CKPGUID_STATECHUNK),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, inputs),
    NMO_FIELD_REF_ARRAY(nmo_behavior_state_t, outputs),
    NMO_FIELD(nmo_behavior_state_t, single_activity_flags, NMO_GUID_ENUM_CK_SCENEOBJECTACTIVITY_FLAGS),
    NMO_FIELD(nmo_behavior_state_t, has_single_activity, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_behavior_state_t, interface_chunk, CKPGUID_STATECHUNK),
    NMO_FIELD(nmo_behavior_state_t, has_interface, CKPGUID_BOOL)
};

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Read object ID array using XObjectPointerArray format
 */
static nmo_status_t read_object_sequence(nmo_chunk_t *chunk, nmo_array_t *out_ids) {
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;

    if (count == 0) {
        nmo_array_clear(out_ids);
        NMO_RETURN_OK();
    }

    const uint32_t MAX_ARRAY_SIZE = 100000;
    if (count > MAX_ARRAY_SIZE) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Array count exceeds maximum");
    }

    nmo_array_clear(out_ids);
    result = nmo_array_reserve(out_ids, count);
    if (result != NMO_OK) return result;

    nmo_object_id_t *ids = NULL;
    result = nmo_array_extend(out_ids, count, (void **)&ids);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_chunk_read_object_sequence_item(chunk, &ids[i]);
        if (result != NMO_OK) {
            out_ids->count = i;
            break;
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Write object ID array using XObjectPointerArray format
 */
static nmo_status_t write_object_sequence(nmo_chunk_t *chunk, const nmo_array_t *ids) {
    nmo_status_t result = nmo_chunk_write_object_sequence_start(chunk, (uint32_t)ids->count);
    if (result != NMO_OK) return result;

    const nmo_object_id_t *values = NMO_ARRAY_DATA(nmo_object_id_t, ids);
    for (uint32_t i = 0; i < ids->count; i++) {
        result = nmo_chunk_write_object_sequence_item(chunk, values[i]);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t read_object_subchunk_list(
    nmo_chunk_t *chunk,
    nmo_array_t *out_ids,
    nmo_array_t *out_chunks)
{
    int32_t count = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count);
    if (result != NMO_OK) return result;

    if (count <= 0) {
        nmo_array_clear(out_ids);
        nmo_array_clear(out_chunks);
        NMO_RETURN_OK();
    }

    const uint32_t MAX_ARRAY_SIZE = 100000;
    if ((uint32_t)count > MAX_ARRAY_SIZE) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Array count exceeds maximum");
    }

    nmo_array_clear(out_ids);
    nmo_array_clear(out_chunks);
    result = nmo_array_reserve(out_ids, count);
    if (result != NMO_OK) return result;
    result = nmo_array_reserve(out_chunks, count);
    if (result != NMO_OK) return result;

    nmo_object_id_t *ids = NULL;
    nmo_chunk_t **chunks = NULL;
    result = nmo_array_extend(out_ids, count, (void **)&ids);
    if (result != NMO_OK) return result;
    result = nmo_array_extend(out_chunks, count, (void **)&chunks);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_chunk_read_object_id(chunk, &ids[i]);
        if (result != NMO_OK) {
            out_ids->count = i;
            out_chunks->count = i;
            break;
        }
        (void)nmo_chunk_read_sub_chunk(chunk, &chunks[i]);
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
nmo_status_t nmo_behavior_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_behavior_state_t *out_state = (nmo_behavior_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_behavior_deserialize");
    }

    /* Deserialize base CKObject state (merged into this chunk by AddChunkAndDelete) */
    {
        nmo_status_t result = nmo_chunk_start_read(chunk);
        if (result != NMO_OK) return result;

        result = nmo_object_deserialize(&out_state->base.base, chunk, NULL, context);
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
            (void)read_object_subchunk_list(chunk,
                                            &out_state->sub_behaviors,
                                            &out_state->sub_behavior_chunks);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS) == NMO_OK) {
            (void)read_object_subchunk_list(chunk,
                                            &out_state->local_parameters,
                                            &out_state->local_parameter_chunks);
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
                result = read_object_sequence(chunk, &out_state->sub_behaviors);
                if (result != NMO_OK) return result;
            }

            if (graph_save_flags & CK_STATESAVE_BEHAVIORSUBLINKS) {
                result = read_object_sequence(chunk, &out_state->sub_behavior_links);
                if (result != NMO_OK) return result;
            }

            if (graph_save_flags & CK_STATESAVE_BEHAVIOROPERATIONS) {
                result = read_object_sequence(chunk, &out_state->operations);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORINPARAMS) {
                result = read_object_sequence(chunk, &out_state->in_parameters);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIOROUTPARAMS) {
                result = read_object_sequence(chunk, &out_state->out_parameters);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORLOCALPARAMS) {
                result = read_object_sequence(chunk, &out_state->local_parameters);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIORINPUTS) {
                result = read_object_sequence(chunk, &out_state->inputs);
                if (result != NMO_OK) return result;
            }

            if (save_flags & CK_STATESAVE_BEHAVIOROUTPUTS) {
                result = read_object_sequence(chunk, &out_state->outputs);
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
        if (sub_result == NMO_OK && interface_chunk) {
            /* Clear file_context so post-load parsing reads raw file IDs.
             * The is_building_block callback uses find_by_file_id, which
             * expects original CK_IDs rather than remapped runtime IDs. */
            nmo_chunk_set_file_context(interface_chunk, NULL);
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
            (void)read_object_sequence(chunk, &out_state->sub_behaviors);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORSUBLINKS) == NMO_OK) {
            (void)read_object_sequence(chunk, &out_state->sub_behavior_links);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROPERATIONS) == NMO_OK) {
            (void)read_object_sequence(chunk, &out_state->operations);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORINPARAMS) == NMO_OK) {
            (void)read_object_sequence(chunk, &out_state->in_parameters);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS) == NMO_OK) {
            (void)read_object_sequence(chunk, &out_state->local_parameters);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROUTPARAMS) == NMO_OK) {
            (void)read_object_sequence(chunk, &out_state->out_parameters);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORINPUTS) == NMO_OK) {
            (void)read_object_sequence(chunk, &out_state->inputs);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIOROUTPUTS) == NMO_OK) {
            (void)read_object_sequence(chunk, &out_state->outputs);
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
nmo_status_t nmo_behavior_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_behavior_state_t *in_state = (const nmo_behavior_state_t *)instance;
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_behavior_serialize");
    }

    const bool is_file = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    const bool write_file_format = is_file;

    /* Start write mode for behavior chunk */
    nmo_status_t result = nmo_chunk_start_write(out_chunk);
    if (result != NMO_OK) return result;

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_object_serialize(&in_state->base.base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (!write_file_format) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);

        if ((save_flags & CK_STATESAVE_BEHAVIORSUBBEHAV) != 0 &&
            in_state->sub_behaviors.count > 0 && in_state->sub_behaviors.data) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORSUBBEHAV);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->sub_behaviors.count);
            if (result != NMO_OK) return result;
            const nmo_object_id_t *sub_behaviors = NMO_ARRAY_DATA(nmo_object_id_t,
                                                                  &in_state->sub_behaviors);
            const nmo_chunk_t *const *sub_chunks = NMO_ARRAY_DATA(
                const nmo_chunk_t *, &in_state->sub_behavior_chunks);
            for (uint32_t i = 0; i < in_state->sub_behaviors.count; ++i) {
                result = nmo_chunk_write_object_id(out_chunk, sub_behaviors[i]);
                if (result != NMO_OK) return result;
                nmo_chunk_t *sub = NULL;
                if (sub_chunks && i < in_state->sub_behavior_chunks.count) {
                    sub = (nmo_chunk_t *)sub_chunks[i];
                }
                result = nmo_chunk_write_sub_chunk(out_chunk, sub);
                if (result != NMO_OK) return result;
            }
        }

        if ((in_state->flags & CKBEHAVIOR_BUILDINGBLOCK) ||
            (save_flags & CK_STATESAVE_BEHAVIORLOCALPARAMS) == 0) {
            NMO_RETURN_OK();
        }

        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS);
        if (result != NMO_OK) return result;

        if (in_state->local_parameters.count > 0) {
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->local_parameters.count);
            if (result != NMO_OK) return result;

            const nmo_object_id_t *local_parameters = NMO_ARRAY_DATA(
                nmo_object_id_t, &in_state->local_parameters);
            const nmo_chunk_t *const *local_chunks = NMO_ARRAY_DATA(
                const nmo_chunk_t *, &in_state->local_parameter_chunks);
            for (uint32_t i = 0; i < in_state->local_parameters.count; ++i) {
                result = nmo_chunk_write_object_id(out_chunk, local_parameters[i]);
                if (result != NMO_OK) return result;
                nmo_chunk_t *sub = NULL;
                if (local_chunks && i < in_state->local_parameter_chunks.count) {
                    sub = (nmo_chunk_t *)local_chunks[i];
                }
                result = nmo_chunk_write_sub_chunk(out_chunk, sub);
                if (result != NMO_OK) return result;
            }
        } else {
            result = nmo_chunk_write_dword(out_chunk, 0);
            if (result != NMO_OK) return result;
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
        if (in_state->sub_behaviors.count > 0) save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV;
        else save_flags &= ~CK_STATESAVE_BEHAVIORSUBBEHAV;
        if (in_state->sub_behavior_links.count > 0) save_flags |= CK_STATESAVE_BEHAVIORSUBLINKS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORSUBLINKS;
        if (in_state->operations.count > 0) save_flags |= CK_STATESAVE_BEHAVIOROPERATIONS;
        else save_flags &= ~CK_STATESAVE_BEHAVIOROPERATIONS;
        if (in_state->in_parameters.count > 0) save_flags |= CK_STATESAVE_BEHAVIORINPARAMS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORINPARAMS;
        if (in_state->out_parameters.count > 0) save_flags |= CK_STATESAVE_BEHAVIOROUTPARAMS;
        else save_flags &= ~CK_STATESAVE_BEHAVIOROUTPARAMS;
        if (in_state->local_parameters.count > 0) save_flags |= CK_STATESAVE_BEHAVIORLOCALPARAMS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORLOCALPARAMS;
        if (in_state->inputs.count > 0) save_flags |= CK_STATESAVE_BEHAVIORINPUTS;
        else save_flags &= ~CK_STATESAVE_BEHAVIORINPUTS;
        if (in_state->outputs.count > 0) save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;
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
        result = write_object_sequence(out_chunk, &in_state->sub_behaviors);
        if (result != NMO_OK) return result;
    }

    if (graph_save_flags & CK_STATESAVE_BEHAVIORSUBLINKS) {
        result = write_object_sequence(out_chunk, &in_state->sub_behavior_links);
        if (result != NMO_OK) return result;
    }

    if (graph_save_flags & CK_STATESAVE_BEHAVIOROPERATIONS) {
        result = write_object_sequence(out_chunk, &in_state->operations);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORINPARAMS) {
        result = write_object_sequence(out_chunk, &in_state->in_parameters);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIOROUTPARAMS) {
        result = write_object_sequence(out_chunk, &in_state->out_parameters);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORLOCALPARAMS) {
        result = write_object_sequence(out_chunk, &in_state->local_parameters);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIORINPUTS) {
        result = write_object_sequence(out_chunk, &in_state->inputs);
        if (result != NMO_OK) return result;
    }

    if (save_flags & CK_STATESAVE_BEHAVIOROUTPUTS) {
        result = write_object_sequence(out_chunk, &in_state->outputs);
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

static nmo_status_t nmo_behavior_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_behavior_state_t *s = src;
    nmo_behavior_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->sub_behaviors, &d->sub_behaviors,
                                        &s->sub_behaviors.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->sub_behavior_chunks,
                                                     &s->sub_behavior_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->sub_behavior_links, &d->sub_behavior_links,
                                        &s->sub_behavior_links.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->operations, &d->operations,
                                        &s->operations.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->in_parameters, &d->in_parameters,
                                        &s->in_parameters.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->out_parameters, &d->out_parameters,
                                        &s->out_parameters.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->local_parameters, &d->local_parameters,
                                        &s->local_parameters.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->local_parameter_chunks,
                                                     &s->local_parameter_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->inputs, &d->inputs, &s->inputs.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->outputs, &d->outputs, &s->outputs.allocator));
    return nmo_object_copy_chunk(arena, &d->interface_chunk, s->interface_chunk);
}

static nmo_status_t nmo_behavior_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_behavior_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->sub_behaviors.data, s->sub_behaviors.count, "sub_behaviors");
    NMO_VALIDATE_COUNT(s->sub_behavior_chunks.data, s->sub_behavior_chunks.count,
                       "sub_behavior_chunks");
    NMO_VALIDATE_COUNT(s->sub_behavior_links.data, s->sub_behavior_links.count,
                       "sub_behavior_links");
    NMO_VALIDATE_COUNT(s->operations.data, s->operations.count, "operations");
    NMO_VALIDATE_COUNT(s->in_parameters.data, s->in_parameters.count, "in_parameters");
    NMO_VALIDATE_COUNT(s->out_parameters.data, s->out_parameters.count, "out_parameters");
    NMO_VALIDATE_COUNT(s->local_parameters.data, s->local_parameters.count, "local_parameters");
    NMO_VALIDATE_COUNT(s->local_parameter_chunks.data, s->local_parameter_chunks.count,
                       "local_parameter_chunks");
    NMO_VALIDATE_COUNT(s->inputs.data, s->inputs.count, "inputs");
    NMO_VALIDATE_COUNT(s->outputs.data, s->outputs.count, "outputs");
    NMO_RETURN_OK();
}

nmo_status_t nmo_behavior_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behavior_remap_dependencies");
    }

    nmo_behavior_state_t *state = (nmo_behavior_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

    if (state->sub_behaviors.count > 0 && state->sub_behaviors.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior sub_behaviors missing");
    }
    if (state->sub_behavior_chunks.count > 0 && state->sub_behavior_chunks.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior sub_behavior_chunks missing");
    }
    if (state->sub_behavior_links.count > 0 && state->sub_behavior_links.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior sub_behavior_links missing");
    }
    if (state->operations.count > 0 && state->operations.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior operations missing");
    }
    if (state->in_parameters.count > 0 && state->in_parameters.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior in_parameters missing");
    }
    if (state->out_parameters.count > 0 && state->out_parameters.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior out_parameters missing");
    }
    if (state->local_parameters.count > 0 && state->local_parameters.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior local_parameters missing");
    }
    if (state->local_parameter_chunks.count > 0 && state->local_parameter_chunks.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior local_parameter_chunks missing");
    }
    if (state->inputs.count > 0 && state->inputs.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior inputs missing");
    }
    if (state->outputs.count > 0 && state->outputs.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior outputs missing");
    }

    if (repo) {
        if (state->owner_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->owner_id) == NULL) {
            state->owner_id = 0;
        }
        if (state->target_parameter_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->target_parameter_id) == NULL) {
            state->target_parameter_id = 0;
        }
    }

    if ((state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0) {
        state->sub_behaviors.count = 0;
        state->sub_behavior_chunks.count = 0;
        state->sub_behavior_links.count = 0;
        state->operations.count = 0;
    }

    if (state->sub_behaviors.count > 0) {
        nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, &state->sub_behaviors);
        uint32_t kept = 0;
        for (uint32_t i = 0; i < state->sub_behaviors.count; ++i) {
            nmo_object_id_t id = ids[i];
            if (id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, id) == NULL) {
                continue;
            }
            bool seen = false;
            for (uint32_t j = 0; j < kept; ++j) {
                if (ids[j] == id) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            ids[kept++] = id;
        }
        state->sub_behaviors.count = kept;
        if (state->sub_behavior_chunks.count > kept) {
            state->sub_behavior_chunks.count = kept;
        }
    } else {
        state->sub_behavior_chunks.count = 0;
    }

    if (state->sub_behavior_links.count > 0) {
        nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, &state->sub_behavior_links);
        uint32_t kept = 0;
        for (uint32_t i = 0; i < state->sub_behavior_links.count; ++i) {
            nmo_object_id_t id = ids[i];
            if (id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, id) == NULL) {
                continue;
            }
            bool seen = false;
            for (uint32_t j = 0; j < kept; ++j) {
                if (ids[j] == id) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            ids[kept++] = id;
        }
        state->sub_behavior_links.count = kept;
    }

    if (state->operations.count > 0) {
        nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, &state->operations);
        uint32_t kept = 0;
        for (uint32_t i = 0; i < state->operations.count; ++i) {
            nmo_object_id_t id = ids[i];
            if (id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, id) == NULL) {
                continue;
            }
            bool seen = false;
            for (uint32_t j = 0; j < kept; ++j) {
                if (ids[j] == id) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            ids[kept++] = id;
        }
        state->operations.count = kept;
    }

    nmo_array_t *arrays[] = {
        &state->in_parameters,
        &state->out_parameters,
        &state->local_parameters,
        &state->inputs,
        &state->outputs
    };
    for (size_t ai = 0; ai < sizeof(arrays) / sizeof(arrays[0]); ++ai) {
        nmo_array_t *arr = arrays[ai];
        if (arr->count == 0 || !arr->data) {
            continue;
        }
        nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, arr);
        uint32_t kept = 0;
        for (uint32_t i = 0; i < arr->count; ++i) {
            nmo_object_id_t id = ids[i];
            if (id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, id) == NULL) {
                continue;
            }
            bool seen = false;
            for (uint32_t j = 0; j < kept; ++j) {
                if (ids[j] == id) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            ids[kept++] = id;
        }
        arr->count = kept;
    }

    if (state->local_parameters.count > 0 &&
        state->local_parameter_chunks.count > state->local_parameters.count) {
        state->local_parameter_chunks.count = state->local_parameters.count;
    }

    return nmo_behavior_validate(state, NULL, NULL);
}

nmo_status_t nmo_behavior_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_behavior_validate(instance, type, context);
}

static nmo_status_t nmo_behavior_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behavior_pre_delete");
    }

    nmo_behavior_state_t *state = (nmo_behavior_state_t *)instance;
    state->owner_id = 0;
    state->target_parameter_id = 0;
    state->sub_behaviors.count = 0;
    state->sub_behavior_chunks.count = 0;
    state->sub_behavior_links.count = 0;
    state->operations.count = 0;
    state->in_parameters.count = 0;
    state->out_parameters.count = 0;
    state->local_parameters.count = 0;
    state->local_parameter_chunks.count = 0;
    state->inputs.count = 0;
    state->outputs.count = 0;
    NMO_RETURN_OK();
}

static void nmo_behavior_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Post-load interface chunk parsing
 * ============================================================================ */

static bool is_building_block_cb(nmo_object_id_t id, void *user_data) {
    if (id == 0) return false;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)user_data;
    nmo_object_t *obj = nmo_object_repository_find_by_file_id(repo, id);
    if (!obj) return false;
    if (obj->class_id != NMO_CID_BEHAVIOR) return false;
    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!state) return false;
    return (state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
}

nmo_status_t nmo_behavior_parse_all_interfaces(
    nmo_object_repository_t *repo,
    nmo_logger_t *logger)
{
    if (!repo) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &count);
    if (!all || count == 0) {
        return NMO_OK;
    }

    nmo_interface_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.is_building_block = is_building_block_cb;
    ctx.user_data = repo;
    /* Layout (inline vs sectioned) is auto-detected by the parser. */

    nmo_status_t first_error = NMO_OK;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = all[i];
        if (!obj || obj->class_id != NMO_CID_BEHAVIOR) continue;

        nmo_behavior_state_t *state =
            (nmo_behavior_state_t *)nmo_object_get_state(obj);
        if (!state || !state->interface_chunk) continue;
        if ((state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0) {
            continue;
        }

        /* Skip chunks with no data -- nothing to parse */
        if (state->interface_chunk->data.count == 0) continue;

        nmo_arena_t *arena = nmo_object_get_storage_arena(obj);
        if (!arena) {
            if (first_error == NMO_OK) {
                first_error = NMO_ERR_INVALID_ARGUMENT;
            }
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Behavior interface parse skipped: object id=%u file_id=%u name='%s' has no storage arena",
                        obj->id, obj->file_id, obj->name ? obj->name : "");
            }
            continue;
        }

        nmo_interface_data_t *idata = (nmo_interface_data_t *)nmo_arena_alloc(
            arena, sizeof(nmo_interface_data_t), alignof(nmo_interface_data_t));
        if (!idata) {
            if (first_error == NMO_OK) {
                first_error = NMO_ERR_NOMEM;
            }
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Behavior interface parse skipped: object id=%u file_id=%u name='%s' allocation failed",
                        obj->id, obj->file_id, obj->name ? obj->name : "");
            }
            continue;
        }

        nmo_status_t st = nmo_interface_chunk_parse(
            state->interface_chunk, arena, &ctx, idata);

        if (st == NMO_OK) {
            state->interface_data = idata;
            state->interface_chunk = NULL;  /* raw blob no longer needed */
        } else {
            state->interface_data = NULL;
            if (first_error == NMO_OK) {
                first_error = st;
            }
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Behavior interface parse failed: object id=%u file_id=%u name='%s' status=%d: %s",
                        obj->id, obj->file_id, obj->name ? obj->name : "", st,
                        nmo_last_error_message());
            }
        }
    }

    return first_error;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(behavior, nmo_behavior_state_t)

nmo_type_vtable_t nmo_behavior_vtable = {
    .prepare_dependencies = nmo_behavior_prepare_dependencies,
    .remap_dependencies = nmo_behavior_remap_dependencies,
    .pre_delete = nmo_behavior_pre_delete,
    .post_delete = nmo_behavior_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_behavior_create,
        nmo_behavior_destroy,
        nmo_behavior_serialize,
        nmo_behavior_deserialize,
        nmo_behavior_copy,
        nmo_behavior_validate,
        nmo_behavior_equals,
        nmo_behavior_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_behavior_type,
    CKPGUID_BEHAVIOR,
    "CKBehavior",
    NMO_CID_BEHAVIOR,
    CKPGUID_SCENEOBJECT,
    nmo_behavior_state_t,
    &nmo_behavior_vtable,
    nmo_behavior_fields)





