/**
 * @file ckparameteroperation_schemas.c
 * @brief CKParameterOperation schema implementation
 */

#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_object_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(parameteroperation, nmo_parameteroperation_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameteroperation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameteroperation_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_parameteroperation_state_t, operation_guid, CKPGUID_GUID),
    NMO_FIELD_REF(nmo_parameteroperation_state_t, owner_id),
    NMO_FIELD_REF(nmo_parameteroperation_state_t, in1_id),
    NMO_FIELD_REF(nmo_parameteroperation_state_t, in2_id),
    NMO_FIELD_REF(nmo_parameteroperation_state_t, out_id),
    NMO_FIELD(nmo_parameteroperation_state_t, has_owner, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameteroperation_state_t, has_in1, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameteroperation_state_t, has_in2, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameteroperation_state_t, has_out, CKPGUID_UINT8),
    NMO_FIELD_OPT(nmo_parameteroperation_state_t, in1_chunk, CKPGUID_STATECHUNK),
    NMO_FIELD_OPT(nmo_parameteroperation_state_t, in2_chunk, CKPGUID_STATECHUNK),
    NMO_FIELD_OPT(nmo_parameteroperation_state_t, out_chunk, CKPGUID_STATECHUNK)
};

nmo_status_t nmo_parameteroperation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_parameteroperation_state_t *out_state = (nmo_parameteroperation_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_parameteroperation_deserialize");
    }

    {
        nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) {
            return result;
        }
    }

    const int is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    if (is_file) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONNEWDATA) == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &out_state->operation_guid));

            size_t count = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_start(
                chunk, &count));
            if (nmo_chunk_get_data_version(chunk) < 5) {
                nmo_object_id_t dummy = 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_item(chunk, &dummy));
                if (count > 0) {
                    count -= 1;
                }
            }
            if (count >= 1) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_item(chunk, &out_state->in1_id));
                out_state->has_in1 = 1;
            }
            if (count >= 2) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_item(chunk, &out_state->in2_id));
                out_state->has_in2 = 1;
            }
            if (count >= 3) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_item(chunk, &out_state->out_id));
                out_state->has_out = 1;
            }
            NMO_RETURN_OK();
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOP) == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &out_state->operation_guid));
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONDEFAULTDATA) == NMO_OK) {
            out_state->has_owner = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->owner_id));
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOUTPUT) == NMO_OK) {
            out_state->has_out = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->out_id));
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONINPUTS) == NMO_OK) {
            out_state->has_in1 = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->in1_id));
            out_state->has_in2 = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->in2_id));
        }

        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOP) == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &out_state->operation_guid));
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONDEFAULTDATA) == NMO_OK) {
        out_state->has_owner = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->owner_id));
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOUTPUT) == NMO_OK) {
        out_state->has_out = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->out_id));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_sub_chunk(chunk, &out_state->out_chunk));
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONINPUTS) == NMO_OK) {
        out_state->has_in1 = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->in1_id));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_sub_chunk(chunk, &out_state->in1_chunk));

        out_state->has_in2 = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->in2_id));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_sub_chunk(chunk, &out_state->in2_chunk));
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_parameteroperation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_parameteroperation_state_t *s = src;
    nmo_parameteroperation_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->in1_chunk, s->in1_chunk));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->in2_chunk, s->in2_chunk));
    return nmo_object_copy_chunk(arena, &d->out_chunk, s->out_chunk);
}

static nmo_status_t nmo_parameteroperation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameteroperation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameteroperation_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameteroperation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameteroperation_remap_dependencies");
    }

    nmo_parameteroperation_state_t *state = (nmo_parameteroperation_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    /* Preserve all reference/chunk lanes and presence flags. */
    return nmo_parameteroperation_validate(state, NULL, NULL);
}

static nmo_status_t nmo_parameteroperation_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameteroperation_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_parameteroperation_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

nmo_status_t nmo_parameteroperation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_parameteroperation_state_t *in_state =
        (const nmo_parameteroperation_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_parameteroperation_serialize");
    }

    {
        nmo_status_t result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
        if (result != NMO_OK) {
            return result;
        }
    }

    const int is_file = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    if (is_file) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONNEWDATA);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_guid(out_chunk, in_state->operation_guid);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, 3);
        if (result != NMO_OK) return result;

        NMO_RETURN_IF_ERROR(nmo_chunk_write_object_sequence_item(out_chunk, in_state->in1_id));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_object_sequence_item(out_chunk, in_state->in2_id));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_object_sequence_item(out_chunk, in_state->out_id));

        NMO_RETURN_OK();
    }

    uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    if (save_flags == 0) {
        NMO_RETURN_OK();
    }

    if ((save_flags & CK_STATESAVE_OPERATIONOP) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONOP);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_guid(out_chunk, in_state->operation_guid);
        if (result != NMO_OK) return result;
    }

    if ((save_flags & CK_STATESAVE_OPERATIONDEFAULTDATA) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONDEFAULTDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->has_owner ? in_state->owner_id : 0);
        if (result != NMO_OK) return result;
    }

    if ((save_flags & CK_STATESAVE_OPERATIONINPUTS) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONINPUTS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->has_in1 ? in_state->in1_id : 0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->has_in1 ? in_state->in1_chunk : NULL);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->has_in2 ? in_state->in2_id : 0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->has_in2 ? in_state->in2_chunk : NULL);
        if (result != NMO_OK) return result;
    }

    if ((save_flags & CK_STATESAVE_OPERATIONOUTPUT) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONOUTPUT);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->has_out ? in_state->out_id : 0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->has_out ? in_state->out_chunk : NULL);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(parameteroperation, nmo_parameteroperation_state_t)

nmo_type_vtable_t nmo_parameteroperation_vtable = {
    .prepare_dependencies = nmo_parameteroperation_prepare_dependencies,
    .remap_dependencies = nmo_parameteroperation_remap_dependencies,
    .pre_delete = nmo_parameteroperation_pre_delete,
    .post_delete = nmo_parameteroperation_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_parameteroperation_create,
        nmo_parameteroperation_destroy,
        nmo_parameteroperation_serialize,
        nmo_parameteroperation_deserialize,
        nmo_parameteroperation_copy,
        nmo_parameteroperation_validate,
        nmo_parameteroperation_equals,
        nmo_parameteroperation_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_parameteroperation_type,
    CKPGUID_PARAMETEROPERATION,
    "CKParameterOperation",
    NMO_CID_PARAMETEROPERATION,
    CKPGUID_OBJECT,
    nmo_parameteroperation_state_t,
    &nmo_parameteroperation_vtable,
    nmo_parameteroperation_fields)






