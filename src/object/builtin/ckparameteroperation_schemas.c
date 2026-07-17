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
    NMO_FIELD_NAMED("owner", offsetof(nmo_parameteroperation_state_t, owner),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD_NAMED("in1", offsetof(nmo_parameteroperation_state_t, in1.ref),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD_NAMED("in2", offsetof(nmo_parameteroperation_state_t, in2.ref),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD_NAMED("out", offsetof(nmo_parameteroperation_state_t, out.ref),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD(nmo_parameteroperation_state_t, has_owner, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameteroperation_state_t, has_in1, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameteroperation_state_t, has_in2, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameteroperation_state_t, has_out, CKPGUID_UINT8),
    NMO_FIELD_NAMED("in1_chunk", offsetof(nmo_parameteroperation_state_t, in1.chunk),
                    sizeof(nmo_chunk_t *), CKPGUID_STATECHUNK,
                    NMO_FIELD_OPTIONAL, NMO_SEMANTIC_NONE),
    NMO_FIELD_NAMED("in2_chunk", offsetof(nmo_parameteroperation_state_t, in2.chunk),
                    sizeof(nmo_chunk_t *), CKPGUID_STATECHUNK,
                    NMO_FIELD_OPTIONAL, NMO_SEMANTIC_NONE),
    NMO_FIELD_NAMED("out_chunk", offsetof(nmo_parameteroperation_state_t, out.chunk),
                    sizeof(nmo_chunk_t *), CKPGUID_STATECHUNK,
                    NMO_FIELD_OPTIONAL, NMO_SEMANTIC_NONE)
};

static int nmo_parameteroperation_is_parameter_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

static void nmo_parameteroperation_check_parameter_ref(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED || repository == NULL) {
        return;
    }
    const nmo_object_t *target =
        nmo_object_repository_find_by_id(repository, ref->id);
    if (target != NULL && !nmo_parameteroperation_is_parameter_class(
            nmo_object_get_class_id(target))) {
        ref->state = NMO_REF_CLASS_MISMATCH;
    }
}

static nmo_status_t nmo_parameteroperation_deserialize_internal(
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

    nmo_parameteroperation_state_t decoded = *out_state;
    memset(&decoded.operation_guid, 0, sizeof(decoded.operation_guid));
    decoded.owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    decoded.in1.ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    decoded.in1.chunk = NULL;
    decoded.in2.ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    decoded.in2.chunk = NULL;
    decoded.out.ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    decoded.out.chunk = NULL;
    decoded.has_owner = 0;
    decoded.has_in1 = 0;
    decoded.has_in2 = 0;
    decoded.has_out = 0;

    const int is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;

    if (is_file) {
        nmo_status_t result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_OPERATIONNEWDATA);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &decoded.operation_guid));

            size_t count = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_start(
                chunk, &count));
            if (nmo_chunk_get_data_version(chunk) < 5) {
                if (count == 0) return NMO_ERR_INVALID_FORMAT;
                nmo_ref_t dummy = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &dummy));
                count -= 1;
            }
            if (count > 3) return NMO_ERR_INVALID_FORMAT;
            if (count >= 1) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.in1.ref));
                decoded.has_in1 = 1;
            }
            if (count >= 2) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.in2.ref));
                decoded.has_in2 = 1;
            }
            if (count >= 3) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.out.ref));
                decoded.has_out = 1;
            }
            goto commit;
        }
        if (result != NMO_ERR_NOT_FOUND) return result;

        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOP);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &decoded.operation_guid));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONDEFAULTDATA);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.owner));
            decoded.has_owner = 1;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOUTPUT);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.out.ref));
            decoded.has_out = 1;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONINPUTS);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.in1.ref));
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.in2.ref));
            decoded.has_in1 = 1;
            decoded.has_in2 = 1;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        goto commit;
    }

    nmo_status_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOP);
    if (result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &decoded.operation_guid));
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONDEFAULTDATA);
    if (result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.owner));
        decoded.has_owner = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOUTPUT);
    if (result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.out.ref));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_sub_chunk(chunk, &decoded.out.chunk));
        decoded.has_out = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONINPUTS);
    if (result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.in1.ref));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_sub_chunk(chunk, &decoded.in1.chunk));
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.in2.ref));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_sub_chunk(chunk, &decoded.in2.chunk));
        decoded.has_in1 = 1;
        decoded.has_in2 = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

commit:
    {
        const nmo_object_repository_t *repository =
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        nmo_ref_check_class(&decoded.owner, repository, types, NMO_CID_BEHAVIOR);
        nmo_parameteroperation_check_parameter_ref(&decoded.in1.ref, repository);
        nmo_parameteroperation_check_parameter_ref(&decoded.in2.ref, repository);
        nmo_parameteroperation_check_parameter_ref(&decoded.out.ref, repository);
    }
    *out_state = decoded;
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameteroperation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_parameteroperation_state_t *out_state =
        (nmo_parameteroperation_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_parameteroperation_state_t decoded = *out_state;
    nmo_status_t result = nmo_parameteroperation_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) return result;
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_parameteroperation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_parameteroperation_state_t *s = src;
    nmo_parameteroperation_state_t *d = dst;
    nmo_chunk_t *in1_chunk = NULL;
    nmo_chunk_t *in2_chunk = NULL;
    nmo_chunk_t *out_chunk = NULL;
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &in1_chunk, s->in1.chunk));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &in2_chunk, s->in2.chunk));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &out_chunk, s->out.chunk));
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    d->in1.chunk = in1_chunk;
    d->in2.chunk = in2_chunk;
    d->out.chunk = out_chunk;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parameteroperation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
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
    nmo_parameteroperation_state_t *state =
        (nmo_parameteroperation_state_t *)instance;
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->in1.ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->in2.ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->out.ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
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

static nmo_status_t nmo_parameteroperation_serialize_internal(
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

        size_t ref_count = in_state->has_out ? 3u :
            (in_state->has_in2 ? 2u : (in_state->has_in1 ? 1u : 0u));
        const int has_legacy_dummy = nmo_chunk_get_data_version(out_chunk) < 5;
        result = nmo_chunk_write_object_sequence_start(
            out_chunk, ref_count + (has_legacy_dummy ? 1u : 0u));
        if (result != NMO_OK) return result;

        const nmo_ref_t none = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        if (has_legacy_dummy) {
            NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(out_chunk, &none));
        }
        if (ref_count >= 1u) {
            NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(
                out_chunk, in_state->has_in1 ? &in_state->in1.ref : &none));
        }
        if (ref_count >= 2u) {
            NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(
                out_chunk, in_state->has_in2 ? &in_state->in2.ref : &none));
        }
        if (ref_count >= 3u) {
            NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(
                out_chunk, in_state->has_out ? &in_state->out.ref : &none));
        }

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
        const nmo_ref_t none = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_write(out_chunk, in_state->has_owner ? &in_state->owner : &none);
        if (result != NMO_OK) return result;
    }

    if ((save_flags & CK_STATESAVE_OPERATIONINPUTS) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONINPUTS);
        if (result != NMO_OK) return result;
        const nmo_ref_t none = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_write(out_chunk, in_state->has_in1 ? &in_state->in1.ref : &none);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->has_in1 ? in_state->in1.chunk : NULL);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, in_state->has_in2 ? &in_state->in2.ref : &none);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->has_in2 ? in_state->in2.chunk : NULL);
        if (result != NMO_OK) return result;
    }

    if ((save_flags & CK_STATESAVE_OPERATIONOUTPUT) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OPERATIONOUTPUT);
        if (result != NMO_OK) return result;
        const nmo_ref_t none = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_write(out_chunk, in_state->has_out ? &in_state->out.ref : &none);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_sub_chunk(out_chunk, in_state->has_out ? in_state->out.chunk : NULL);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameteroperation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_parameteroperation_validate(
        instance, type, context));
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_parameteroperation_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
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






