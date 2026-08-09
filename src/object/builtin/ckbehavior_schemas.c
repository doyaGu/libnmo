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
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_behavior_ref_dispose(void *element, void *user_data)
{
    (void)user_data;
    nmo_behavior_ref_t *value = (nmo_behavior_ref_t *)element;
    if (value != NULL && value->chunk != NULL) {
        nmo_chunk_destroy(value->chunk);
        value->chunk = NULL;
    }
}

static void nmo_behavior_ref_array_set_lifecycle(nmo_array_t *array)
{
    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.dispose = nmo_behavior_ref_dispose;
    nmo_array_set_lifecycle(array, &lifecycle);
}

nmo_status_t nmo_behavior_ref_array_append(
    nmo_array_t *array,
    nmo_object_id_t id,
    nmo_chunk_t *chunk)
{
    if (array == NULL || array->element_size != sizeof(nmo_behavior_ref_t)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_behavior_ref_t value = nmo_behavior_ref_from_id(id);
    value.chunk = chunk;
    return nmo_array_append(array, &value);
}

int nmo_behavior_ref_array_find(
    const nmo_array_t *array,
    nmo_object_id_t id,
    size_t *out_index)
{
    if (array == NULL || array->element_size != sizeof(nmo_behavior_ref_t) ||
        array->data == NULL) {
        return 0;
    }
    const nmo_behavior_ref_t *values = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, array);
    for (size_t i = 0; i < array->count; ++i) {
        if (nmo_behavior_ref_runtime_id(&values[i]) == id) {
            if (out_index != NULL) *out_index = i;
            return 1;
        }
    }
    return 0;
}

nmo_object_id_t nmo_behavior_ref_array_get_id(
    const nmo_array_t *array,
    size_t index)
{
    if (array == NULL || array->element_size != sizeof(nmo_behavior_ref_t) ||
        array->data == NULL || index >= array->count) {
        return NMO_OBJECT_ID_NONE;
    }
    const nmo_behavior_ref_t *values = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, array);
    return nmo_behavior_ref_runtime_id(&values[index]);
}

static void nmo_behavior_dispose_ref_arrays(nmo_behavior_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->sub_behaviors);
    nmo_array_dispose(&state->sub_behavior_links);
    nmo_array_dispose(&state->operations);
    nmo_array_dispose(&state->in_parameters);
    nmo_array_dispose(&state->out_parameters);
    nmo_array_dispose(&state->local_parameters);
    nmo_array_dispose(&state->inputs);
    nmo_array_dispose(&state->outputs);
}

static nmo_status_t nmo_behavior_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_behavior_state_t *state = (nmo_behavior_state_t *)instance;
    memset(state, 0, sizeof(*state));
    nmo_status_t result = nmo_sceneobject_vtable.create(
        &state->base, NULL, context);
    if (result != NMO_OK) return result;

    state->compatible_class_id = NMO_CID_BEOBJECT;
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->target_parameter = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_array_t *arrays[] = {
        &state->sub_behaviors, &state->sub_behavior_links,
        &state->operations, &state->in_parameters,
        &state->out_parameters, &state->local_parameters,
        &state->inputs, &state->outputs,
    };
    for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i) {
        result = nmo_array_init(
            arrays[i], sizeof(nmo_behavior_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_behavior_dispose_ref_arrays(state);
            nmo_sceneobject_vtable.destroy(&state->base, NULL, context);
            memset(state, 0, sizeof(*state));
            return result;
        }
        nmo_behavior_ref_array_set_lifecycle(arrays[i]);
    }
    return NMO_OK;
}

static void nmo_behavior_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return;
    nmo_behavior_state_t *state = (nmo_behavior_state_t *)instance;
    nmo_behavior_dispose_ref_arrays(state);
    nmo_sceneobject_vtable.destroy(&state->base, NULL, context);
    memset(state, 0, sizeof(*state));
}

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
    NMO_FIELD_REF_VALUE(nmo_behavior_state_t, owner),
    NMO_FIELD(nmo_behavior_state_t, behavior_type, NMO_GUID_ENUM_CK_BEHAVIOR_TYPE),
    NMO_FIELD(nmo_behavior_state_t, save_flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_behavior_state_t, has_save_flags, CKPGUID_BOOL),
    NMO_FIELD(nmo_behavior_state_t, use_legacy_identifiers, CKPGUID_BOOL),
    NMO_FIELD(nmo_behavior_state_t, block_guid, CKPGUID_GUID),
    NMO_FIELD(nmo_behavior_state_t, block_version, CKPGUID_UINT32),
    NMO_FIELD_REF_VALUE(nmo_behavior_state_t, target_parameter),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, sub_behaviors, CKPGUID_NONE),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, sub_behavior_links, CKPGUID_NONE),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, operations, CKPGUID_NONE),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, in_parameters, CKPGUID_NONE),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, out_parameters, CKPGUID_NONE),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, local_parameters, CKPGUID_NONE),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, inputs, CKPGUID_NONE),
    NMO_FIELD_ARRAY(nmo_behavior_state_t, outputs, CKPGUID_NONE),
    NMO_FIELD(nmo_behavior_state_t, single_activity_flags, NMO_GUID_ENUM_CK_SCENEOBJECTACTIVITY_FLAGS),
    NMO_FIELD(nmo_behavior_state_t, has_single_activity, CKPGUID_BOOL),
    NMO_FIELD_OPT(nmo_behavior_state_t, interface_chunk, CKPGUID_STATECHUNK),
    NMO_FIELD(nmo_behavior_state_t, has_interface, CKPGUID_BOOL),
    NMO_FIELD_PTR(nmo_behavior_state_t, interface_data, NMO_GUID_IFACE_DATA),
    NMO_FIELD(nmo_behavior_state_t, interface_ids_are_runtime, CKPGUID_BOOL)
};

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

static size_t nmo_behavior_identifier_remaining_dwords(
    const nmo_chunk_t *chunk);

/**
 * @brief Read object ID array using XObjectPointerArray format
 */
static nmo_status_t read_object_sequence(nmo_chunk_t *chunk, nmo_array_t *out_refs) {
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;

    if (count > nmo_behavior_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_array_t decoded;
    result = nmo_array_init(&decoded, sizeof(nmo_behavior_ref_t), count,
                            &out_refs->allocator);
    if (result != NMO_OK) return result;
    nmo_behavior_ref_array_set_lifecycle(&decoded);

    nmo_behavior_ref_t *refs = NULL;
    result = nmo_array_extend(&decoded, count, (void **)&refs);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded);
        return result;
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_ref_read(chunk, &refs[i].ref);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
    }

    NMO_RETURN_IF_ERROR(nmo_array_swap(out_refs, &decoded));
    nmo_array_dispose(&decoded);
    NMO_RETURN_OK();
}

/**
 * @brief Write object ID array using XObjectPointerArray format
 */
static nmo_status_t write_object_sequence(nmo_chunk_t *chunk, const nmo_array_t *refs) {
    if (!chunk || !refs || refs->count > INT32_MAX ||
        (refs->count > 0u &&
         (refs->data == NULL || refs->element_size != sizeof(nmo_behavior_ref_t)))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid behavior object sequence");
    }

    nmo_status_t result = nmo_chunk_write_object_sequence_start(
        chunk, (uint32_t)refs->count);
    if (result != NMO_OK) return result;

    const nmo_behavior_ref_t *values = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, refs);
    for (uint32_t i = 0; i < refs->count; i++) {
        result = nmo_ref_write_sequence_item(chunk, &values[i].ref);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static size_t nmo_behavior_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

static nmo_status_t read_object_subchunk_list(
    nmo_chunk_t *chunk,
    nmo_array_t *out_refs)
{
    int32_t count = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count);
    if (result != NMO_OK) return result;

    if (count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Object sub-chunk count cannot be negative");
    }

    if ((size_t)count * 2u >
        nmo_behavior_identifier_remaining_dwords(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Object sub-chunk count exceeds remaining DWORDs");
    }

    nmo_array_t decoded;
    result = nmo_array_init(&decoded, sizeof(nmo_behavior_ref_t), count,
                            &out_refs->allocator);
    if (result != NMO_OK) return result;
    nmo_behavior_ref_array_set_lifecycle(&decoded);

    nmo_behavior_ref_t *refs = NULL;
    result = nmo_array_extend(&decoded, count, (void **)&refs);
    if (result != NMO_OK) goto fail;

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        result = nmo_ref_read(chunk, &refs[i].ref);
        if (result != NMO_OK) goto fail;
        result = nmo_chunk_read_sub_chunk(chunk, &refs[i].chunk);
        if (result != NMO_OK) goto fail;
    }

    NMO_RETURN_IF_ERROR(nmo_array_swap(out_refs, &decoded));
    nmo_array_dispose(&decoded);
    NMO_RETURN_OK();

fail:
    nmo_array_dispose(&decoded);
    return result;
}

static void behavior_check_ref_array_class(
    nmo_array_t *array,
    nmo_class_id_t expected_class_id,
    void *context)
{
    if (array == NULL || array->data == NULL ||
        array->element_size != sizeof(nmo_behavior_ref_t)) {
        return;
    }
    const nmo_deserialize_context_t *deser =
        nmo_deserialize_context_get(context);
    if (deser == NULL) return;
    nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(nmo_behavior_ref_t, array);
    for (size_t i = 0; i < array->count; ++i) {
        nmo_ref_check_class(
            &refs[i].ref,
            (const nmo_object_repository_t *)deser->repository,
            deser->type_registry,
            expected_class_id);
    }
}

static void behavior_check_ref_classes(
    nmo_behavior_state_t *state,
    void *context)
{
    if (state != NULL) {
        const nmo_object_repository_t *repository =
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        nmo_ref_check_class(
            &state->owner,
            repository,
            types,
            NMO_CID_SCENEOBJECT);
        nmo_ref_check_class(
            &state->target_parameter,
            repository,
            types,
            NMO_CID_PARAMETERIN);
    }
    behavior_check_ref_array_class(
        &state->sub_behaviors, NMO_CID_BEHAVIOR, context);
    behavior_check_ref_array_class(
        &state->sub_behavior_links, NMO_CID_BEHAVIORLINK, context);
    behavior_check_ref_array_class(
        &state->operations, NMO_CID_PARAMETEROPERATION, context);
    behavior_check_ref_array_class(
        &state->in_parameters, NMO_CID_PARAMETERIN, context);
    behavior_check_ref_array_class(
        &state->out_parameters, NMO_CID_PARAMETEROUT, context);
    behavior_check_ref_array_class(
        &state->local_parameters, NMO_CID_PARAMETERLOCAL, context);
    behavior_check_ref_array_class(
        &state->inputs, NMO_CID_BEHAVIORIO, context);
    behavior_check_ref_array_class(
        &state->outputs, NMO_CID_BEHAVIORIO, context);
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
static nmo_status_t nmo_behavior_seek_optional(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    bool *out_found)
{
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, identifier);
    if (result == NMO_OK) {
        *out_found = true;
        return NMO_OK;
    }
    *out_found = false;
    return result == NMO_ERR_NOT_FOUND ? NMO_OK : result;
}

static nmo_status_t nmo_behavior_deserialize_internal(
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
    if (newdata_seek == NMO_ERR_NOT_FOUND) {
        newdata_id = CK_STATESAVE_BEHAVIORNEWDATA_LEGACY;
        newdata_seek = nmo_chunk_seek_identifier(chunk, newdata_id);
        if (newdata_seek == NMO_OK) {
            out_state->use_legacy_identifiers = true;
        }
    } else if (newdata_seek != NMO_OK) return newdata_seek;
    if (newdata_seek != NMO_OK && newdata_seek != NMO_ERR_NOT_FOUND) {
        return newdata_seek;
    }

    if (is_file && data_version >= 5 && newdata_seek != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "CKBehavior: missing NEWDATA identifier in file-mode chunk");
    }

    if (!is_file && newdata_seek != NMO_OK) {
        bool section_found = false;
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORSUBBEHAV, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_subchunk_list(
                chunk, &out_state->sub_behaviors));
        }

        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_subchunk_list(
                chunk, &out_state->local_parameters));
        }

        behavior_check_ref_classes(out_state, context);
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
                result = nmo_ref_read(chunk, &out_state->target_parameter);
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

            result = nmo_ref_read(chunk, &out_state->owner);
            if (result != NMO_OK) return result;

            if (out_state->flags & CKBEHAVIOR_BUILDINGBLOCK) {
                result = nmo_chunk_read_dword(chunk, &out_state->block_version);
                if (result != NMO_OK) return result;
            } else {
                uint32_t tmp = 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &tmp));
            }
        }
    } else {
        bool section_found = false;
        nmo_guid_t guid = {0};
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORPROTOGUID, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &guid));
            out_state->block_guid = guid;
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORFLAGS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, (int32_t *)&out_state->flags));
            if (out_state->flags & CKBEHAVIOR_USEFUNCTION) {
                out_state->flags |= CKBEHAVIOR_BUILDINGBLOCK;
                out_state->block_guid = guid;
            }
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORCOMPATIBLECID, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, (uint32_t *)&out_state->compatible_class_id));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORTYPE, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->behavior_type));
            if (out_state->behavior_type == 1) {
                out_state->flags |= CKBEHAVIOR_SCRIPT;
            }
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIOROWNER, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_state->owner));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORPRIORITY, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->priority));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORTARGET, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(
                chunk, &out_state->target_parameter));
        }
    }

    /* Optional: Interface chunk (for editing mode) */
    uint32_t interface_id = CK_STATESAVE_BEHAVIORINTERFACE;
    result = nmo_chunk_seek_identifier(chunk, interface_id);
    if (result == NMO_ERR_NOT_FOUND) {
        interface_id = CK_STATESAVE_BEHAVIORINTERFACE_LEGACY;
        result = nmo_chunk_seek_identifier(chunk, interface_id);
        if (result == NMO_OK) {
            out_state->use_legacy_identifiers = true;
        }
    } else if (result != NMO_OK) return result;
    if (result != NMO_OK && result != NMO_ERR_NOT_FOUND) return result;
    if (result == NMO_OK) {
        out_state->has_interface = true;
        nmo_chunk_t *interface_chunk = NULL;
        nmo_status_t sub_result = nmo_chunk_read_sub_chunk(chunk, &interface_chunk);
        if (sub_result == NMO_OK && interface_chunk) {
            /* Legacy file-authored interface chunks can set file_flag while
             * still storing raw CK_IDs and a separate object-ID table. */
            if (interface_chunk->ids.count > 0) {
                nmo_chunk_set_file_context(interface_chunk, NULL);
            }
            out_state->interface_chunk = interface_chunk;
        } else {
            out_state->interface_chunk = NULL;
            return sub_result;
        }
    }

    /* Optional: Single activity flags */
    uint32_t single_activity_id = out_state->use_legacy_identifiers
        ? CK_STATESAVE_BEHAVIORSINGLEACTIVITY_LEGACY
        : CK_STATESAVE_BEHAVIORSINGLEACTIVITY;
    result = nmo_chunk_seek_identifier(chunk, single_activity_id);
    if (result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->single_activity_flags));
        out_state->has_single_activity = true;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    if (nmo_chunk_get_data_version(chunk) < 5) {
        if (out_state->flags & CKBEHAVIOR_BUILDINGBLOCK) {
            NMO_RETURN_OK();
        }

        bool section_found = false;
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORSUBBEHAV, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->sub_behaviors));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORSUBLINKS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->sub_behavior_links));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIOROPERATIONS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->operations));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORINPARAMS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->in_parameters));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->local_parameters));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIOROUTPARAMS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->out_parameters));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIORINPUTS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->inputs));
        }
        NMO_RETURN_IF_ERROR(nmo_behavior_seek_optional(
            chunk, CK_STATESAVE_BEHAVIOROUTPUTS, &section_found));
        if (section_found) {
            NMO_RETURN_IF_ERROR(read_object_sequence(chunk, &out_state->outputs));
        }
    }

    behavior_check_ref_classes(out_state, context);
    NMO_RETURN_OK();
}

nmo_status_t nmo_behavior_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_state_t *out_state = (nmo_behavior_state_t *)instance;
    nmo_behavior_state_t decoded = {0};
    nmo_status_t result = nmo_behavior_create(&decoded, type, context);
    if (result != NMO_OK) {
        return result;
    }
    decoded.sub_behaviors.allocator = out_state->sub_behaviors.allocator;
    decoded.sub_behavior_links.allocator =
        out_state->sub_behavior_links.allocator;
    decoded.operations.allocator = out_state->operations.allocator;
    decoded.in_parameters.allocator = out_state->in_parameters.allocator;
    decoded.out_parameters.allocator = out_state->out_parameters.allocator;
    decoded.local_parameters.allocator =
        out_state->local_parameters.allocator;
    decoded.inputs.allocator = out_state->inputs.allocator;
    decoded.outputs.allocator = out_state->outputs.allocator;

    result = nmo_behavior_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_behavior_destroy(&decoded, type, context);
        return result;
    }

    nmo_behavior_destroy(out_state, type, context);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t build_interface_file_index_remap(
    nmo_arena_t *arena,
    nmo_object_repository_t *repo,
    const nmo_id_remap_t *runtime_to_file_index,
    nmo_id_remap_t **out_remap)
{
    if (!out_remap) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface remap output");
    }
    *out_remap = NULL;

    if (!arena || !repo || !runtime_to_file_index) {
        NMO_RETURN_OK();
    }

    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    if (!remap) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate interface ID remap");
    }

    size_t count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (!obj || obj->file_id == 0) {
            continue;
        }

        nmo_object_id_t file_index = 0;
        if (nmo_id_remap_lookup_id(runtime_to_file_index,
                                   obj->id,
                                   &file_index) == NMO_OK) {
            nmo_status_t st = nmo_id_remap_add(remap, obj->file_id, file_index);
            NMO_RETURN_IF_ERROR(st);
        }
    }

    *out_remap = remap;
    NMO_RETURN_OK();
}

/* =============================================================================
 * CKBehavior SERIALIZATION
 * ============================================================================= */

static nmo_status_t nmo_behavior_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

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
static nmo_status_t nmo_behavior_serialize_internal(
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
            const nmo_behavior_ref_t *sub_behaviors = NMO_ARRAY_DATA(
                nmo_behavior_ref_t, &in_state->sub_behaviors);
            for (uint32_t i = 0; i < in_state->sub_behaviors.count; ++i) {
                result = nmo_ref_write(out_chunk, &sub_behaviors[i].ref);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_sub_chunk(
                    out_chunk, sub_behaviors[i].chunk);
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

            const nmo_behavior_ref_t *local_parameters = NMO_ARRAY_DATA(
                nmo_behavior_ref_t, &in_state->local_parameters);
            for (uint32_t i = 0; i < in_state->local_parameters.count; ++i) {
                result = nmo_ref_write(out_chunk, &local_parameters[i].ref);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_sub_chunk(
                    out_chunk, local_parameters[i].chunk);
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

        if (in_state->interface_data) {
            nmo_chunk_t *interface_out = nmo_chunk_create(out_chunk->arena);
            if (!interface_out) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Cannot allocate InterfaceChunk output");
            }
            const nmo_serialize_context_t *ser_ctx =
                nmo_serialize_context_try(context);
            nmo_object_repository_t *repo = ser_ctx
                ? (nmo_object_repository_t *)ser_ctx->repository
                : NULL;
            /* Use scratch arena for interface temporaries when available */
            nmo_arena_t *temp_arena = (ser_ctx && ser_ctx->scratch)
                ? ser_ctx->scratch : out_chunk->arena;
            if (out_chunk->file_context != NULL &&
                out_chunk->file_context->runtime_to_file != NULL) {
                if (in_state->interface_ids_are_runtime) {
                    nmo_chunk_set_file_context(interface_out, out_chunk->file_context);
                } else if (repo != NULL) {
                    nmo_id_remap_t *interface_remap = NULL;
                    result = build_interface_file_index_remap(
                        temp_arena,
                        repo,
                        out_chunk->file_context->runtime_to_file,
                        &interface_remap);
                    if (result != NMO_OK) return result;

                    nmo_chunk_file_context_t *interface_file_ctx =
                        (nmo_chunk_file_context_t *)nmo_arena_alloc(
                            temp_arena,
                            sizeof(nmo_chunk_file_context_t),
                            alignof(nmo_chunk_file_context_t));
                    if (!interface_file_ctx) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                         "Cannot allocate InterfaceChunk file context");
                    }
                    interface_file_ctx->runtime_to_file = interface_remap;
                    interface_file_ctx->file_to_runtime = NULL;
                    interface_file_ctx->repository = repo;
                    nmo_chunk_set_file_context(interface_out, interface_file_ctx);
                } else {
                    NMO_RETURN_ERROR(
                        NMO_ERR_INVALID_ARGUMENT,
                        NMO_SEVERITY_ERROR,
                        "Cannot serialize raw InterfaceChunk IDs in file context without object repository");
                }
            }
            result = nmo_interface_chunk_write(interface_out,
                                               in_state->interface_data,
                                               NULL);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, interface_out);
            if (result != NMO_OK) return result;
        } else if (in_state->interface_chunk) {
            /* Building blocks skip interface parsing -- fall back to raw chunk */
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
        result = nmo_ref_write(out_chunk, &in_state->target_parameter);
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

nmo_status_t nmo_behavior_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_behavior_validate(instance, type, context));

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_behavior_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_interface_copy_array(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t elem_size,
    size_t count,
    const char *label)
{
    if (!dst || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface copy arguments");
    }
    *dst = NULL;
    if (count == 0) {
        NMO_RETURN_OK();
    }
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing %s data for interface copy", label);
    }
    if (elem_size != 0 && count > ((size_t)-1) / elem_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Interface copy size overflow for %s", label);
    }
    void *copy = nmo_arena_alloc(arena, elem_size * count, alignof(max_align_t));
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate %s for interface copy", label);
    }
    memcpy(copy, src, elem_size * count);
    *dst = copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_interface_copy_bytes(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t size,
    const char *label)
{
    return nmo_interface_copy_array(arena, dst, src, sizeof(uint8_t), size, label);
}

static nmo_status_t nmo_interface_copy_string(
    nmo_arena_t *arena,
    const char **dst,
    const char *src)
{
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface string copy output");
    }
    *dst = NULL;
    if (!src) {
        NMO_RETURN_OK();
    }
    void *copy = NULL;
    NMO_RETURN_IF_ERROR(nmo_interface_copy_bytes(
        arena, &copy, src, strlen(src) + 1u, "comment text"));
    *dst = (const char *)copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_interface_copy_graph_io(
    nmo_arena_t *arena,
    nmo_interface_graph_io_t **dst,
    const nmo_interface_graph_io_t *src)
{
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid graph IO copy output");
    }
    *dst = NULL;
    if (!src) {
        NMO_RETURN_OK();
    }

    nmo_interface_graph_io_t *copy =
        (nmo_interface_graph_io_t *)nmo_arena_alloc(
            arena, sizeof(*copy), alignof(nmo_interface_graph_io_t));
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate graph IO copy");
    }
    *copy = *src;
    copy->inward_inputs = NULL;
    copy->outward_inputs = NULL;
    copy->inward_outputs = NULL;
    copy->outward_outputs = NULL;
    copy->inward_input_tags = NULL;
    copy->outward_input_tags = NULL;
    copy->inward_output_tags = NULL;
    copy->outward_output_tags = NULL;

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_inputs, src->inward_inputs,
        sizeof(int32_t), src->inward_input_count, "graph inward inputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_inputs, src->outward_inputs,
        sizeof(int32_t), src->outward_input_count, "graph outward inputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_outputs, src->inward_outputs,
        sizeof(int32_t), src->inward_output_count, "graph inward outputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_outputs, src->outward_outputs,
        sizeof(int32_t), src->outward_output_count, "graph outward outputs"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_input_tags, src->inward_input_tags,
        sizeof(int32_t), src->inward_input_count, "graph inward input tags"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_input_tags, src->outward_input_tags,
        sizeof(int32_t), src->outward_input_count, "graph outward input tags"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->inward_output_tags, src->inward_output_tags,
        sizeof(int32_t), src->inward_output_count, "graph inward output tags"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->outward_output_tags, src->outward_output_tags,
        sizeof(int32_t), src->outward_output_count, "graph outward output tags"));

    *dst = copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_interface_copy_body(
    nmo_arena_t *arena,
    nmo_interface_body_t *dst,
    const nmo_interface_body_t *src)
{
    if (!dst || !src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface body copy arguments");
    }
    *dst = *src;
    dst->links = NULL;
    dst->operations = NULL;
    dst->comments = NULL;
    dst->params.locals = NULL;
    dst->params.shared = NULL;
    dst->graph_io = NULL;

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->links, src->links, sizeof(nmo_interface_link_t),
        src->link_count, "interface links"));
    for (size_t i = 0; i < dst->link_count; ++i) {
        if (src->links[i].point_count > SIZE_MAX / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Interface link point count overflow");
        }
        dst->links[i].points = NULL;
        NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
            arena, (void **)&dst->links[i].points, src->links[i].points,
            sizeof(float), src->links[i].point_count * 2u,
            "interface link points"));
    }

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->operations, src->operations,
        sizeof(nmo_interface_operation_t), src->operation_count,
        "interface operations"));

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->comments, src->comments,
        sizeof(nmo_interface_comment_t), src->comment_count,
        "interface comments"));
    for (size_t i = 0; i < dst->comment_count; ++i) {
        dst->comments[i].text = NULL;
        NMO_RETURN_IF_ERROR(nmo_interface_copy_string(
            arena, &dst->comments[i].text, src->comments[i].text));
    }

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->params.locals, src->params.locals,
        sizeof(nmo_interface_param_t), src->params.local_count,
        "interface local params"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&dst->params.shared, src->params.shared,
        sizeof(nmo_interface_param_t), src->params.shared_count,
        "interface shared params"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_graph_io(
        arena, &dst->graph_io, src->graph_io));

    NMO_RETURN_OK();
}

static nmo_status_t nmo_interface_copy_data(
    nmo_arena_t *arena,
    nmo_interface_data_t **dst,
    const nmo_interface_data_t *src)
{
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid interface data copy output");
    }
    *dst = NULL;
    if (!src) {
        NMO_RETURN_OK();
    }

    nmo_interface_data_t *copy =
        (nmo_interface_data_t *)nmo_arena_alloc(
            arena, sizeof(*copy), alignof(nmo_interface_data_t));
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Cannot allocate interface data copy");
    }
    *copy = *src;
    copy->script.snapshot_data = NULL;
    copy->script.body = (nmo_interface_body_t){0};
    copy->subs = NULL;
    copy->extra.entries = NULL;

    NMO_RETURN_IF_ERROR(nmo_interface_copy_bytes(
        arena, &copy->script.snapshot_data, src->script.snapshot_data,
        src->script.snapshot_size, "script snapshot"));
    NMO_RETURN_IF_ERROR(nmo_interface_copy_body(
        arena, &copy->script.body, &src->script.body));

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->subs, src->subs,
        sizeof(nmo_interface_behavior_t), src->sub_count,
        "interface sub behaviors"));
    for (size_t i = 0; i < copy->sub_count; ++i) {
        copy->subs[i].body = (nmo_interface_body_t){0};
        NMO_RETURN_IF_ERROR(nmo_interface_copy_body(
            arena, &copy->subs[i].body, &src->subs[i].body));
    }

    NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
        arena, (void **)&copy->extra.entries, src->extra.entries,
        sizeof(nmo_interface_extra_entry_t), src->extra.entry_count,
        "interface extra entries"));
    for (size_t i = 0; i < copy->extra.entry_count; ++i) {
        nmo_interface_extra_entry_t *dst_entry = &copy->extra.entries[i];
        const nmo_interface_extra_entry_t *src_entry = &src->extra.entries[i];
        dst_entry->sub_entries = NULL;
        NMO_RETURN_IF_ERROR(nmo_interface_copy_array(
            arena, (void **)&dst_entry->sub_entries, src_entry->sub_entries,
            sizeof(nmo_interface_extra_sub_t), src_entry->sub_count,
            "interface extra sub entries"));
        for (size_t j = 0; j < dst_entry->sub_count; ++j) {
            dst_entry->sub_entries[j].data = NULL;
            NMO_RETURN_IF_ERROR(nmo_interface_copy_bytes(
                arena, &dst_entry->sub_entries[j].data,
                src_entry->sub_entries[j].data,
                src_entry->sub_entries[j].data_size,
                "interface extra sub data"));
        }
    }

    *dst = copy;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_behavior_copy_ref_array(
    nmo_arena_t *arena,
    nmo_array_t *dst,
    const nmo_array_t *src)
{
    if (arena == NULL || dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (src->count > 0 &&
        (src->data == NULL ||
         src->element_size != sizeof(nmo_behavior_ref_t))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_array_dispose(dst);
    const nmo_allocator_t *allocator =
        src->allocator.alloc != NULL ? &src->allocator : NULL;
    nmo_status_t result = nmo_array_init(
        dst, sizeof(nmo_behavior_ref_t), src->count, allocator);
    if (result != NMO_OK) return result;
    nmo_behavior_ref_array_set_lifecycle(dst);

    nmo_behavior_ref_t *dst_refs = NULL;
    result = nmo_array_extend(dst, src->count, (void **)&dst_refs);
    if (result != NMO_OK) return result;
    const nmo_behavior_ref_t *src_refs = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, src);
    for (size_t i = 0; i < src->count; ++i) {
        dst_refs[i].ref = src_refs[i].ref;
        dst_refs[i].chunk = NULL;
        if (src_refs[i].chunk != NULL) {
            dst_refs[i].chunk = nmo_chunk_clone(src_refs[i].chunk, arena);
            if (dst_refs[i].chunk == NULL) return NMO_ERR_NOMEM;
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_behavior_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_behavior_state_t *s = src;
    nmo_behavior_state_t *d = dst;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_behavior_validate(s, type, NULL));

    nmo_behavior_state_t copied;
    nmo_status_t result = nmo_behavior_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;
    result = nmo_sceneobject_vtable.copy(
        &s->base, &copied.base, NULL, arena);
    if (result != NMO_OK) goto fail;

    copied.flags = s->flags;
    copied.priority = s->priority;
    copied.compatible_class_id = s->compatible_class_id;
    copied.owner = s->owner;
    copied.behavior_type = s->behavior_type;
    copied.save_flags = s->save_flags;
    copied.has_save_flags = s->has_save_flags;
    copied.use_legacy_identifiers = s->use_legacy_identifiers;
    copied.block_guid = s->block_guid;
    copied.block_version = s->block_version;
    copied.target_parameter = s->target_parameter;
    copied.single_activity_flags = s->single_activity_flags;
    copied.has_single_activity = s->has_single_activity;
    copied.has_interface = s->has_interface;
    copied.interface_ids_are_runtime = s->interface_ids_are_runtime;

    const nmo_array_t *src_arrays[] = {
        &s->sub_behaviors, &s->sub_behavior_links, &s->operations,
        &s->in_parameters, &s->out_parameters, &s->local_parameters,
        &s->inputs, &s->outputs
    };
    nmo_array_t *dst_arrays[] = {
        &copied.sub_behaviors, &copied.sub_behavior_links,
        &copied.operations, &copied.in_parameters,
        &copied.out_parameters, &copied.local_parameters,
        &copied.inputs, &copied.outputs,
    };
    for (size_t array_index = 0;
         array_index < sizeof(src_arrays) / sizeof(src_arrays[0]);
         ++array_index) {
        result = nmo_behavior_copy_ref_array(
            arena, dst_arrays[array_index], src_arrays[array_index]);
        if (result != NMO_OK) goto fail;
    }
    result = nmo_object_copy_chunk(
        arena, &copied.interface_chunk, s->interface_chunk);
    if (result != NMO_OK) goto fail;
    result = nmo_interface_copy_data(
        arena, &copied.interface_data, s->interface_data);
    if (result != NMO_OK) goto fail;

#define NMO_BEHAVIOR_DETACH_SHARED_ARRAY(field) \
    do { \
        if (d->field.data == s->field.data) { \
            memset(&d->field, 0, sizeof(d->field)); \
        } \
    } while (0)
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(sub_behaviors);
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(sub_behavior_links);
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(operations);
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(in_parameters);
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(out_parameters);
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(local_parameters);
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(inputs);
    NMO_BEHAVIOR_DETACH_SHARED_ARRAY(outputs);
#undef NMO_BEHAVIOR_DETACH_SHARED_ARRAY
    nmo_behavior_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_behavior_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_behavior_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_behavior_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_sceneobject_vtable.validate(
        &s->base, NULL, context));
    const nmo_array_t *arrays[] = {
        &s->sub_behaviors, &s->sub_behavior_links, &s->operations,
        &s->in_parameters, &s->out_parameters, &s->local_parameters,
        &s->inputs, &s->outputs,
    };
    const char *names[] = {
        "sub_behaviors", "sub_behavior_links", "operations",
        "in_parameters", "out_parameters", "local_parameters",
        "inputs", "outputs",
    };
    for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i) {
        const nmo_array_t *array = arrays[i];
        if (array->count > 0 && array->data == NULL) {
            NMO_RETURN_ERROR(
                NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                "Missing %s array for count %zu", names[i], array->count);
        }
        if (array->count > INT32_MAX ||
            ((array->element_size != 0 || array->count > 0) &&
             array->element_size != sizeof(nmo_behavior_ref_t))) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
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
    if (state->inputs.count > 0 && state->inputs.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior inputs missing");
    }
    if (state->outputs.count > 0 && state->outputs.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Behavior outputs missing");
    }

    /* Dependency validation must not normalize or compact serialized lanes.
     * Missing references and their parallel chunks are preserved verbatim;
     * explicit normalization is a separate caller-requested operation. */
    (void)repo;
    return nmo_behavior_validate(state, NULL, NULL);
}

nmo_status_t nmo_behavior_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_behavior_validate(instance, type, context);
}

static nmo_status_t normalize_behavior_array(
    nmo_array_t *refs_array,
    nmo_object_repository_t *repo,
    size_t *out_changes)
{
    if (!refs_array || !out_changes) return NMO_ERR_INVALID_ARGUMENT;
    if (refs_array->count > 0 && !refs_array->data) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    for (size_t i = 0; i < refs_array->count;) {
        nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(
            nmo_behavior_ref_t, refs_array);
        const nmo_object_id_t id = nmo_behavior_ref_runtime_id(&refs[i]);
        if (id != NMO_OBJECT_ID_NONE &&
            nmo_object_repository_find_by_id(repo, id) != NULL) {
            ++i;
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(refs_array, i, NULL));
        (*out_changes)++;
    }
    return NMO_OK;
}

nmo_status_t nmo_behavior_normalize_references(
    nmo_behavior_state_t *state,
    nmo_object_repository_t *repository,
    size_t *out_change_count)
{
    if (!state || !repository) return NMO_ERR_INVALID_ARGUMENT;
    size_t changed = 0;
    const nmo_object_id_t owner_id = nmo_behavior_owner_id(state);
    if (state->owner.state != NMO_REF_NONE &&
        (owner_id == NMO_OBJECT_ID_NONE ||
         !nmo_object_repository_find_by_id(repository, owner_id))) {
        state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        changed++;
    }
    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (state->target_parameter.state != NMO_REF_NONE &&
        (target_parameter_id == NMO_OBJECT_ID_NONE ||
         !nmo_object_repository_find_by_id(
             repository, target_parameter_id))) {
        state->target_parameter = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        changed++;
    }
    NMO_RETURN_IF_ERROR(normalize_behavior_array(
        &state->sub_behaviors, repository, &changed));
    NMO_RETURN_IF_ERROR(normalize_behavior_array(
        &state->local_parameters, repository, &changed));
    nmo_array_t *arrays[] = {
        &state->sub_behavior_links, &state->operations, &state->in_parameters,
        &state->out_parameters, &state->inputs, &state->outputs
    };
    for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i) {
        NMO_RETURN_IF_ERROR(normalize_behavior_array(
            arrays[i], repository, &changed));
    }
    if (out_change_count) *out_change_count = changed;
    return NMO_OK;
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
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->target_parameter = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_array_clear(&state->sub_behaviors);
    nmo_array_clear(&state->sub_behavior_links);
    nmo_array_clear(&state->operations);
    nmo_array_clear(&state->in_parameters);
    nmo_array_clear(&state->out_parameters);
    nmo_array_clear(&state->local_parameters);
    nmo_array_clear(&state->inputs);
    nmo_array_clear(&state->outputs);
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

static bool nmo_behavior_enumerate_array(
    const nmo_array_t *array,
    uint32_t kind,
    const char *field_name,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    if (array == NULL || array->count == 0) return true;
    if (array->data == NULL ||
        array->element_size != sizeof(nmo_behavior_ref_t)) return false;
    const nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, array);
    for (size_t i = 0; i < array->count; ++i) {
        const nmo_object_id_t id = nmo_behavior_ref_runtime_id(&refs[i]);
        if (id == NMO_OBJECT_ID_NONE) continue;
        if (!visitor(user_data, id, kind, field_name, (uint32_t)i)) {
            return false;
        }
    }
    return true;
}

static nmo_status_t nmo_behavior_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_behavior_state_t *state = instance;
    if (state == NULL || visitor == NULL) return NMO_OK;
    const nmo_object_id_t owner_id = nmo_behavior_owner_id(state);
    if (owner_id != NMO_OBJECT_ID_NONE &&
        !visitor(user_data, owner_id, NMO_REF_KIND_OWNER,
                 "owner_id", 0)) return NMO_OK;
    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != NMO_OBJECT_ID_NONE &&
        !visitor(user_data, target_parameter_id, NMO_REF_KIND_TARGET,
                 "target_parameter_id", 0)) return NMO_OK;
    if (!nmo_behavior_enumerate_array(
            &state->sub_behaviors, NMO_REF_KIND_SCRIPT, "sub_behaviors",
            visitor, user_data)) return NMO_OK;
    if (!nmo_behavior_enumerate_array(
            &state->sub_behavior_links, NMO_REF_KIND_BEHAVIOR_LINK,
            "sub_behavior_links", visitor, user_data)) return NMO_OK;
    if (!nmo_behavior_enumerate_array(
            &state->operations, NMO_REF_KIND_UNKNOWN, "operations",
            visitor, user_data)) return NMO_OK;
    if (!nmo_behavior_enumerate_array(
            &state->in_parameters, NMO_REF_KIND_PARAMETER, "in_parameters",
            visitor, user_data)) return NMO_OK;
    if (!nmo_behavior_enumerate_array(
            &state->out_parameters, NMO_REF_KIND_PARAMETER, "out_parameters",
            visitor, user_data)) return NMO_OK;
    if (!nmo_behavior_enumerate_array(
            &state->local_parameters, NMO_REF_KIND_PARAMETER,
            "local_parameters", visitor, user_data)) return NMO_OK;
    if (!nmo_behavior_enumerate_array(
            &state->inputs, NMO_REF_KIND_UNKNOWN, "inputs",
            visitor, user_data)) return NMO_OK;
    (void)nmo_behavior_enumerate_array(
        &state->outputs, NMO_REF_KIND_UNKNOWN, "outputs", visitor, user_data);
    return NMO_OK;
}

/* ============================================================================
 * Post-load interface chunk parsing
 * ============================================================================ */

typedef struct nmo_interface_lookup_ctx {
    nmo_object_repository_t *repo;
    bool prefer_runtime_ids;
} nmo_interface_lookup_ctx_t;

static bool is_building_block_cb(nmo_object_id_t id, void *user_data) {
    if (id == 0) return false;
    nmo_interface_lookup_ctx_t *lookup =
        (nmo_interface_lookup_ctx_t *)user_data;
    if (!lookup || !lookup->repo) return false;

    nmo_object_repository_t *repo = lookup->repo;
    nmo_object_t *obj = NULL;
    if (lookup->prefer_runtime_ids) {
        obj = nmo_object_repository_find_by_id(repo, id);
        if (!obj) {
            obj = nmo_object_repository_find_by_file_id(repo, id);
        }
    } else {
        obj = nmo_object_repository_find_by_file_id(repo, id);
        if (!obj) {
            obj = nmo_object_repository_find_by_id(repo, id);
        }
    }
    if (!obj) return false;
    if (obj->class_id != NMO_CID_BEHAVIOR) return false;
    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!state) return false;
    return (state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
}

static bool interface_script_id_matches_object(
    const nmo_interface_data_t *data,
    const nmo_object_t *obj,
    bool prefer_runtime_ids)
{
    if (!data || !obj || data->script.behavior_id == 0) {
        return false;
    }

    if (prefer_runtime_ids) {
        return data->script.behavior_id == obj->id;
    }

    if (obj->file_id != 0) {
        return data->script.behavior_id == obj->file_id;
    }

    return data->script.behavior_id == obj->id;
}

static void behavior_interface_stats_set_first_error(
    nmo_behavior_interface_parse_stats_t *stats,
    const nmo_object_t *obj,
    const nmo_chunk_t *chunk,
    nmo_status_t status)
{
    if (!stats || stats->first_error != NMO_OK) {
        return;
    }

    stats->first_error = status;
    stats->first_error_object_id = obj ? obj->id : 0;
    stats->first_error_file_id = obj ? obj->file_id : 0;
    stats->first_error_chunk_version = chunk ? chunk->chunk_version : 0;
    stats->first_error_data_version = chunk ? chunk->data_version : 0;
    stats->first_error_reader_offset = chunk ? nmo_chunk_get_position(chunk) : 0;
    stats->first_error_chunk_dwords = chunk ? chunk->data.count : 0;
}

nmo_status_t nmo_behavior_parse_all_interfaces_ex(
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    nmo_behavior_interface_parse_stats_t *out_stats)
{
    if (out_stats) {
        memset(out_stats, 0, sizeof(*out_stats));
    }

    if (!repo) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = nmo_object_repository_get_count(repo);
    if (count == 0) {
        return NMO_OK;
    }

    nmo_interface_lookup_ctx_t lookup_ctx;
    memset(&lookup_ctx, 0, sizeof(lookup_ctx));
    lookup_ctx.repo = repo;

    nmo_interface_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.is_building_block = is_building_block_cb;
    ctx.user_data = &lookup_ctx;
    /* Layout (inline vs sectioned) is auto-detected by the parser. */

    nmo_status_t first_error = NMO_OK;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (!obj || obj->class_id != NMO_CID_BEHAVIOR) continue;

        nmo_behavior_state_t *state =
            (nmo_behavior_state_t *)nmo_object_get_state(obj);
        if (!state || !state->interface_chunk) continue;
        if ((state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0) {
            continue;
        }

        /* Skip chunks with no data -- nothing to parse */
        if (state->interface_chunk->data.count == 0) continue;

        if (out_stats) {
            out_stats->attempted_count++;
        }

        nmo_arena_t *arena = nmo_object_get_storage_arena(obj);
        if (!arena) {
            if (out_stats) {
                out_stats->failed_count++;
                out_stats->skipped_no_arena_count++;
                behavior_interface_stats_set_first_error(
                    out_stats, obj, state->interface_chunk, NMO_ERR_INVALID_ARGUMENT);
            }
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
            if (out_stats) {
                out_stats->failed_count++;
                out_stats->allocation_failure_count++;
                behavior_interface_stats_set_first_error(
                    out_stats, obj, state->interface_chunk, NMO_ERR_NOMEM);
            }
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

        lookup_ctx.prefer_runtime_ids =
            (nmo_chunk_get_file_context(state->interface_chunk) != NULL);
        nmo_status_t st = nmo_interface_chunk_parse(
            state->interface_chunk, arena, &ctx, idata);
        if (st == NMO_OK &&
            !interface_script_id_matches_object(idata, obj, lookup_ctx.prefer_runtime_ids)) {
            NMO_SET_LAST_ERROR(NMO_ERR_INVALID_FORMAT,
                               NMO_SEVERITY_WARNING,
                               "InterfaceChunk script behavior ID does not match selected ID space");
            st = NMO_ERR_INVALID_FORMAT;
        }
        if (st != NMO_OK &&
            nmo_chunk_get_file_context(state->interface_chunk) != NULL) {
            nmo_chunk_set_file_context(state->interface_chunk, NULL);
            lookup_ctx.prefer_runtime_ids = false;
            st = nmo_interface_chunk_parse(
                state->interface_chunk, arena, &ctx, idata);
            if (st == NMO_OK &&
                !interface_script_id_matches_object(idata, obj, false)) {
                NMO_SET_LAST_ERROR(NMO_ERR_INVALID_FORMAT,
                                   NMO_SEVERITY_WARNING,
                                   "InterfaceChunk script behavior ID does not match raw ID space");
                st = NMO_ERR_INVALID_FORMAT;
            }
        }

        if (st == NMO_OK) {
            state->interface_data = idata;
            state->interface_ids_are_runtime = lookup_ctx.prefer_runtime_ids;
            if (out_stats) {
                out_stats->parsed_count++;
            }
            /* Keep the raw chunk for byte-level save round-trip. */
        } else {
            state->interface_data = NULL;
            state->interface_ids_are_runtime = false;
            if (out_stats) {
                out_stats->failed_count++;
                behavior_interface_stats_set_first_error(
                    out_stats, obj, state->interface_chunk, st);
            }
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

nmo_status_t nmo_behavior_parse_all_interfaces(
    nmo_object_repository_t *repo,
    nmo_logger_t *logger)
{
    return nmo_behavior_parse_all_interfaces_ex(repo, logger, NULL);
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static const nmo_object_serialize_pass_t nmo_behavior_compare_passes[] = {
    {
        .class_id = NMO_CID_BEHAVIOR,
        .data_version = 7,
        .chunk_options = NMO_CHUNK_OPTION_FILE,
    },
    {
        .class_id = NMO_CID_BEHAVIOR,
        .data_version = 7,
        .save_flags = CK_STATESAVE_BEHAVIORSUBBEHAV |
            CK_STATESAVE_BEHAVIORLOCALPARAMS,
        .use_context = 1,
    },
};

static bool nmo_behavior_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_behavior_serialize,
        nmo_behavior_compare_passes,
        sizeof(nmo_behavior_compare_passes) /
            sizeof(nmo_behavior_compare_passes[0]),
        4096);
}

static uint32_t nmo_behavior_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_behavior_serialize,
        nmo_behavior_compare_passes,
        sizeof(nmo_behavior_compare_passes) /
            sizeof(nmo_behavior_compare_passes[0]),
        4096);
}

nmo_type_vtable_t nmo_behavior_vtable = {
    .prepare_dependencies = nmo_behavior_prepare_dependencies,
    .remap_dependencies = nmo_behavior_remap_dependencies,
    .pre_delete = nmo_behavior_pre_delete,
    .post_delete = nmo_behavior_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_behavior_create,
        nmo_behavior_destroy,
        nmo_behavior_serialize,
        nmo_behavior_deserialize,
        nmo_behavior_copy,
        nmo_behavior_validate,
        nmo_behavior_equals,
        nmo_behavior_hash,
        nmo_behavior_enumerate_refs)
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





