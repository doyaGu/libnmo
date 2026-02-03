/**
 * @file cklayer_schemas.c
 * @brief CKLayer schema implementation
 */

#include "object/nmo_cklayer_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cklayer, nmo_cklayer_state_t)

nmo_status_t nmo_cklayer_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cklayer_state_t *out_state = (nmo_cklayer_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cklayer_deserialize");
    }

    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LAYERDATA) != NMO_OK) {
        NMO_RETURN_OK();
    }

    nmo_chunk_read_object_id(chunk, &out_state->grid_id);

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode) {
        int32_t format = 0;
        int32_t version = 0;
        if (nmo_chunk_read_int(chunk, &format) == NMO_OK) {
            out_state->format = format;
        }
        if (nmo_chunk_read_int(chunk, &version) == NMO_OK) {
            out_state->version = version;
            out_state->has_version = 1;
        }

        if (out_state->has_version && out_state->version >= 1) {
            uint32_t color = 0;
            if (nmo_chunk_read_dword(chunk, &color) == NMO_OK) {
                out_state->color_rgba = color;
                out_state->has_color = 1;
            }
            if (out_state->version >= 3) {
                if (nmo_chunk_read_guid(chunk, &out_state->param_guid) == NMO_OK) {
                    out_state->has_param_guid = 1;
                }
            }
            nmo_chunk_read_int(chunk, (int32_t *)&out_state->flags);
        }
    } else {
        int32_t type = 0;
        if (nmo_chunk_read_int(chunk, &type) == NMO_OK) {
            out_state->type = type;
            out_state->has_type = 1;
        }
        nmo_chunk_read_int(chunk, &out_state->format);
        nmo_chunk_read_int(chunk, (int32_t *)&out_state->flags);
    }

    if (out_state->format == 0) {
        void *raw = NULL;
        size_t raw_size = 0;
        if (nmo_chunk_read_buffer(chunk, &raw, &raw_size) == NMO_OK) {
            out_state->square_data = raw;
            out_state->square_data_size = raw_size;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_cklayer_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cklayer_state_t *in_state = (const nmo_cklayer_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cklayer_serialize");
    }

    nmo_status_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LAYERDATA);
    if (result != NMO_OK) return result;

    nmo_chunk_write_object_id(out_chunk, in_state->grid_id);

    const int file_mode = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode) {
        const int32_t version = in_state->has_version ? in_state->version : 3;
        nmo_chunk_write_int(out_chunk, in_state->format);
        nmo_chunk_write_int(out_chunk, version);
        nmo_chunk_write_dword(out_chunk, in_state->has_color ? in_state->color_rgba : 0);
        if (version >= 3) {
            nmo_chunk_write_guid(out_chunk,
                                 in_state->has_param_guid ? in_state->param_guid : (nmo_guid_t){0, 0});
        }
        nmo_chunk_write_int(out_chunk, (int32_t)in_state->flags);
    } else {
        nmo_chunk_write_int(out_chunk, in_state->has_type ? in_state->type : 0);
        nmo_chunk_write_int(out_chunk, in_state->format);
        nmo_chunk_write_int(out_chunk, (int32_t)in_state->flags);
    }

    if (in_state->format == 0 && in_state->square_data && in_state->square_data_size > 0) {
        return nmo_chunk_write_buffer(out_chunk, in_state->square_data, in_state->square_data_size);
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cklayer,
    nmo_cklayer_state_t,
    nmo_cklayer_serialize,
    nmo_cklayer_deserialize,
    NMO_GUID_CKLAYER,
    "CKLayer",
    NMO_CID_LAYER,
    NMO_GUID_CKOBJECT
)

