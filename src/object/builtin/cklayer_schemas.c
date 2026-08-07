/**
 * @file cklayer_schemas.c
 * @brief CKLayer schema implementation
 */

#include "object/builtin/nmo_layer_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_utils.h"
#include "object/nmo_object_repository.h"
#include <string.h>

static void nmo_layer_set_defaults(nmo_layer_state_t *state) {
    if (state == NULL) {
        return;
    }

    /* Mirrors RCKLayer ctor defaults (see CKRenderEngine/src/CKLayer.cpp). */
    state->grid = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->type = 1;
    state->format = 0;
    state->version = 0;
    state->color_rgba = 0;
    state->param_guid = (nmo_guid_t){0, 0};
    state->flags = 1;

    state->has_layer_data = 1;
    state->has_type = 1;
    state->has_version = 0;
    state->has_color = 0;
    state->has_param_guid = 0;
    state->has_flags = 1;
    state->has_square_data = 0;

    state->square_data = NULL;
    state->square_data_size = 0;
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    layer,
    nmo_layer_state_t,
    do { \
        nmo_layer_set_defaults(state); \
    } while (0),
    ((void)0))

static nmo_status_t nmo_layer_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_layer_state_t *out_state = (nmo_layer_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_layer_deserialize");
    }

    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    out_state->has_layer_data = 0;
    out_state->has_type = 0;
    out_state->has_version = 0;
    out_state->has_color = 0;
    out_state->has_param_guid = 0;
    out_state->has_flags = 0;
    out_state->has_square_data = 0;
    out_state->square_data = NULL;
    out_state->square_data_size = 0;
    out_state->grid = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LAYERDATA);
    if (result == NMO_ERR_NOT_FOUND) {
        NMO_RETURN_OK();
    }
    if (result != NMO_OK) return result;
    out_state->has_layer_data = 1;

    nmo_ref_t grid = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &grid));
    nmo_ref_check_class(
        &grid,
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context),
        nmo_deserialize_context_get_type_registry(context),
        NMO_CID_GRID);
    out_state->grid = grid;

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode) {
        int32_t format = 0;
        int32_t version = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &format));
        out_state->format = format;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &version));
        out_state->version = version;
        out_state->has_version = 1;

        if (out_state->has_version && out_state->version >= 1) {
            uint32_t color = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &color));
            out_state->color_rgba = color;
            out_state->has_color = 1;
            if (out_state->version >= 2) {
                if (out_state->version >= 3) {
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(
                        chunk, &out_state->param_guid));
                    out_state->has_param_guid = 1;
                }
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(
                    chunk, (int32_t *)&out_state->flags));
                out_state->has_flags = 1;
            } else {
                out_state->flags = 1;
            }
        }
    } else {
        int32_t type = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &type));
        out_state->type = type;
        out_state->has_type = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->format));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(
            chunk, (int32_t *)&out_state->flags));
        out_state->has_flags = 1;
    }

    if (out_state->format == 0) {
        void *raw = NULL;
        size_t raw_size = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_buffer(chunk, &raw, &raw_size));
        /* Convert LE DWORD array to host endianness (no-op on LE,
           matches reference CKConvertEndianArray32 call). */
        uint32_t *dwords = (uint32_t *)raw;
        size_t dword_count = raw_size / 4;
        for (size_t i = 0; i < dword_count; i++) {
            dwords[i] = nmo_le32toh(dwords[i]);
        }
        out_state->square_data = raw;
        out_state->square_data_size = raw_size;
        out_state->has_square_data = 1;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_layer_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_layer_state_t *out_state = (nmo_layer_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_layer_state_t decoded;
    nmo_status_t result = nmo_layer_create(&decoded, type, context);
    if (result != NMO_OK) return result;
    result = nmo_layer_deserialize_internal(&decoded, chunk, type, context);
    if (result != NMO_OK) return result;

    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_layer_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_layer_state_t *in_state = (const nmo_layer_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_layer_serialize");
    }

    nmo_status_t result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const int is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_LAYERDATA) == 0) {
            return NMO_OK;
        }
    }

    if (!in_state->has_layer_data) {
        return NMO_OK;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LAYERDATA);
    if (result != NMO_OK) return result;

    NMO_RETURN_IF_ERROR(nmo_ref_write(out_chunk, &in_state->grid));

    if (is_file) {
        const int32_t version = in_state->has_version ? in_state->version : 3;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->format));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, version));
        if (version >= 1) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, in_state->has_color ? in_state->color_rgba : 0));
        }
        if (version >= 3) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_guid(out_chunk,
                                 in_state->has_param_guid ? in_state->param_guid : (nmo_guid_t){0, 0}));
        }
        if (version >= 2) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, (int32_t)(in_state->has_flags ? in_state->flags : 1)));
        }
    } else {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->has_type ? in_state->type : 0));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->format));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, (int32_t)(in_state->has_flags ? in_state->flags : 1)));
    }

    if (in_state->format == 0) {
        /* The format-0 payload always contains a sized buffer, including
           the empty case. The reader cannot distinguish omission from a
           truncated buffer header. */
        return nmo_chunk_write_buffer(
            out_chunk,
            in_state->has_square_data ? in_state->square_data : NULL,
            in_state->has_square_data ? in_state->square_data_size : 0u);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_layer_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_layer_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static const nmo_type_field_t nmo_layer_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_layer_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF(nmo_layer_state_t, grid),
    NMO_FIELD(nmo_layer_state_t, type, CKPGUID_INT),
    NMO_FIELD(nmo_layer_state_t, format, CKPGUID_INT),
    NMO_FIELD(nmo_layer_state_t, version, CKPGUID_INT),
    NMO_FIELD_NAMED("color_rgba", offsetof(nmo_layer_state_t, color_rgba),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("param_guid", offsetof(nmo_layer_state_t, param_guid),
                    sizeof(nmo_guid_t), CKPGUID_GUID, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_layer_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_layer_state_t, has_type, CKPGUID_UINT8),
    NMO_FIELD(nmo_layer_state_t, has_version, CKPGUID_UINT8),
    NMO_FIELD(nmo_layer_state_t, has_color, CKPGUID_UINT8),
    NMO_FIELD(nmo_layer_state_t, has_param_guid, CKPGUID_UINT8),
    NMO_FIELD_OPT(nmo_layer_state_t, square_data, CKPGUID_POINTER),
    NMO_FIELD(nmo_layer_state_t, square_data_size, CKPGUID_UINT64)
};

static nmo_status_t nmo_layer_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_layer_state_t *s = src;
    nmo_layer_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    return nmo_object_copy_bytes(arena, (void **)&d->square_data,
                                 s->square_data, s->square_data_size);
}

static nmo_status_t nmo_layer_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_layer_state_t *s = instance;
    NMO_VALIDATE_BYTES(s->square_data, s->square_data_size, "square_data");
    NMO_RETURN_OK();
}

nmo_status_t nmo_layer_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_layer_validate(instance, type, context);
}

nmo_status_t nmo_layer_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_layer_remap_dependencies");
    }

    nmo_layer_state_t *state = (nmo_layer_state_t *)instance;
    (void)context;
    /* Preserve unresolved grid reference and raw optional sections. */
    return nmo_layer_validate(state, NULL, NULL);
}

static nmo_status_t nmo_layer_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_layer_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_layer_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static const nmo_object_serialize_pass_t nmo_layer_compare_passes[] = {
    {
        .class_id = NMO_CID_LAYER,
        .data_version = 7,
        .chunk_options = NMO_CHUNK_OPTION_FILE,
        .serialize_flags = NMO_SERIALIZE_FLAG_FILE_MODE,
        .use_context = 1,
    },
    {
        .class_id = NMO_CID_LAYER,
        .data_version = 7,
        .save_flags = CK_STATESAVE_LAYERDATA,
        .use_context = 1,
    },
};

static bool nmo_layer_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_layer_serialize,
        nmo_layer_compare_passes,
        sizeof(nmo_layer_compare_passes) /
            sizeof(nmo_layer_compare_passes[0]),
        4096);
}

static uint32_t nmo_layer_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_layer_serialize,
        nmo_layer_compare_passes,
        sizeof(nmo_layer_compare_passes) /
            sizeof(nmo_layer_compare_passes[0]),
        4096);
}

nmo_type_vtable_t nmo_layer_vtable = {
    .prepare_dependencies = nmo_layer_prepare_dependencies,
    .remap_dependencies = nmo_layer_remap_dependencies,
    .pre_delete = nmo_layer_pre_delete,
    .post_delete = nmo_layer_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_layer_create,
        nmo_layer_destroy,
        nmo_layer_serialize,
        nmo_layer_deserialize,
        nmo_layer_copy,
        nmo_layer_validate,
        nmo_layer_equals,
        nmo_layer_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_layer_type,
    CKPGUID_LAYER,
    "CKLayer",
    NMO_CID_LAYER,
    CKPGUID_OBJECT,
    nmo_layer_state_t,
    &nmo_layer_vtable,
    nmo_layer_fields)
