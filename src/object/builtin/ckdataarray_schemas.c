/**
 * @file ckdataarray_schemas.c
 * @brief CKDataArray schema implementation
 *
 * Implements schema-driven deserialization for CKDataArray (2D data tables).
 * CKDataArray extends CKBeObject and provides structured table storage.
 * 
 * Based on official Virtools SDK (reference/src/CKDataArray.cpp:1735-1960).
 */

#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    dataarray,
    nmo_dataarray_state_t,
    do { \
        state->key_column = -1; \
    } while (0),
    ((void)0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_dataarray_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_dataarray_state_t, base),
                         sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_dataarray_state_t, column_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_dataarray_state_t, column_formats, column_count, 1, NMO_GUID_STRUCT_CKDATAARRAYCOLUMNFORMAT),
    NMO_FIELD(nmo_dataarray_state_t, row_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_dataarray_state_t, rows, row_count, 1, NMO_GUID_STRUCT_CKDATAARRAYROW),
    NMO_FIELD(nmo_dataarray_state_t, order, CKPGUID_INT),
    NMO_FIELD(nmo_dataarray_state_t, column_index, CKPGUID_UINT32),
    NMO_FIELD(nmo_dataarray_state_t, key_column, CKPGUID_INT)
};

static int nmo_dataarray_is_file_mode(const nmo_chunk_t *chunk, void *context) {
    const nmo_deserialize_context_t *deser_ctx = nmo_deserialize_context_get(context);
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    return (chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE)) ||
        (deser_ctx != NULL && (deser_ctx->flags & NMO_DESER_FLAG_FILE_MODE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
}

static bool nmo_dataarray_is_parameter_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

static void nmo_dataarray_check_parameter_ref(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED || repository == NULL) {
        return;
    }
    const nmo_object_t *target =
        nmo_object_repository_find_by_id(repository, ref->id);
    if (target != NULL && !nmo_dataarray_is_parameter_class(
            nmo_object_get_class_id(target))) {
        ref->state = NMO_REF_CLASS_MISMATCH;
    }
}

/* =============================================================================
 * CKDataArray DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKDataArray state from chunk
 * 
 * Implements the symmetric read operation for CKDataArray::Load.
 * Reads column formats, data matrix, and metadata.
 * 
 * Reference: reference/src/CKDataArray.cpp:1823-1950
 * 
 * @param chunk Chunk containing CKDataArray data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_dataarray_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_dataarray_state_t *out_state = (nmo_dataarray_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_dataarray_deserialize");
    }

    /* Deserialize base CKBeObject state first */
    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_dataarray_is_file_mode(chunk, context);

    nmo_last_error_clear();
    result = NMO_OK;

    /* Read column formats */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAARRAYFORMAT);
    if (result == NMO_OK) {
        int32_t column_count;
        result = nmo_chunk_read_int(chunk, &column_count);
        if (result != NMO_OK) return result;

        if (column_count < 0 || column_count > 10000) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid column count");
        }

        out_state->column_count = (uint32_t)column_count;
        if (column_count > 0) {
            out_state->column_formats = (nmo_dataarray_column_format_t *)
                nmo_arena_alloc(arena, column_count * sizeof(nmo_dataarray_column_format_t),
                               _Alignof(nmo_dataarray_column_format_t));
            if (!out_state->column_formats) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate column formats");
            }
            memset(out_state->column_formats, 0,
                   (size_t)column_count * sizeof(nmo_dataarray_column_format_t));

            /* Read each column format */
            for (int32_t i = 0; i < column_count; i++) {
                nmo_dataarray_column_format_t *fmt = &out_state->column_formats[i];

                /* Read column name */
                char *temp_name = NULL;
                NMO_RETURN_IF_ERROR(
                    nmo_chunk_read_string_checked(chunk, &temp_name, NULL));
                fmt->name = temp_name; /* Note: This relies on chunk's internal buffer */

                /* Read column type */
                int32_t type;
                result = nmo_chunk_read_int(chunk, &type);
                if (result != NMO_OK) return result;
                fmt->type = (nmo_arraytype_t)((uint32_t)type);

                /* Read parameter type GUID for PARAMETER columns */
                if (fmt->type == CKARRAYTYPE_PARAMETER) {
                    result = nmo_chunk_read_guid(chunk, &fmt->parameter_type_guid);
                    if (result != NMO_OK) return result;

                    /* Handle legacy CKPGUID_OLDTIME (8-byte GUID) */
                    if (nmo_guid_equals(fmt->parameter_type_guid, CKPGUID_OLDTIME)) {
                        fmt->parameter_type_guid = CKPGUID_TIME;
                    }
                } else {
                    /* Initialize GUID to zero for non-PARAMETER types */
                    fmt->parameter_type_guid = NMO_GUID_NULL;
                }
            }
        }
    }

    /* Read data matrix */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAARRAYDATA);
    if (result == NMO_OK) {
        int32_t row_count;
        result = nmo_chunk_read_int(chunk, &row_count);
        if (result != NMO_OK) return result;

        if (row_count < 0 || row_count > 1000000) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid row count");
        }

        out_state->row_count = (uint32_t)row_count;
        if (row_count > 0) {
            out_state->rows = (nmo_dataarray_row_t *)
                nmo_arena_alloc(arena, row_count * sizeof(nmo_dataarray_row_t),
                               _Alignof(nmo_dataarray_row_t));
            if (!out_state->rows) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate rows");
            }
            memset(out_state->rows, 0,
                   (size_t)row_count * sizeof(nmo_dataarray_row_t));

            /* Read each row */
            for (uint32_t row_idx = 0; row_idx < out_state->row_count; row_idx++) {
                nmo_dataarray_row_t *row = &out_state->rows[row_idx];
                row->column_count = out_state->column_count;

                if (out_state->column_count > 0) {
                    row->cells = (nmo_dataarray_cell_t *)
                        nmo_arena_alloc(arena, out_state->column_count * sizeof(nmo_dataarray_cell_t),
                                       _Alignof(nmo_dataarray_cell_t));
                    if (!row->cells) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate row cells");
                    }
                    memset(row->cells, 0,
                           (size_t)out_state->column_count * sizeof(nmo_dataarray_cell_t));

                    /* Read each cell */
                    for (uint32_t col_idx = 0; col_idx < out_state->column_count; col_idx++) {
                        nmo_dataarray_column_format_t *fmt = &out_state->column_formats[col_idx];
                        nmo_dataarray_cell_t *cell = &row->cells[col_idx];

                        switch (fmt->type) {
                        case CKARRAYTYPE_INT:
                            result = nmo_chunk_read_int(chunk, &cell->int_value);
                            if (result != NMO_OK) return result;
                            break;

                        case CKARRAYTYPE_FLOAT:
                            result = nmo_chunk_read_float(chunk, &cell->float_value);
                            if (result != NMO_OK) return result;
                            break;

                        case CKARRAYTYPE_STRING: {
                            char *temp_str = NULL;
                            NMO_RETURN_IF_ERROR(
                                nmo_chunk_read_string_checked(chunk, &temp_str, NULL));
                            cell->string_value = temp_str; /* Note: Relies on chunk's buffer */
                            break;
                        }

                        case CKARRAYTYPE_OBJECT:
                            result = nmo_ref_read(chunk, &cell->object_ref);
                            if (result != NMO_OK) return result;
                            break;

                        case CKARRAYTYPE_PARAMETER:
                            if (is_file) {
                                result = nmo_ref_read(chunk, &cell->parameter.ref);
                                if (result != NMO_OK) return result;
                                nmo_dataarray_check_parameter_ref(
                                    &cell->parameter.ref,
                                    (const nmo_object_repository_t *)
                                        nmo_deserialize_context_get_repository(context));
                            } else {
                                cell->parameter.ref =
                                    nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                                result = nmo_chunk_read_sub_chunk(
                                    chunk, &cell->parameter.chunk);
                                if (result != NMO_OK) return result;
                            }
                            break;

                        default:
                            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Unknown array type");
                        }
                    }
                } else {
                    row->cells = NULL;
                }
            }
        }
    }

    /* Read metadata members */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAARRAYMEMBERS);
    if (result == NMO_OK) {
        result = nmo_chunk_read_int(chunk, &out_state->order);
        if (result != NMO_OK) return result;

        int32_t column_index;
        result = nmo_chunk_read_int(chunk, &column_index);
        if (result != NMO_OK) return result;
        out_state->column_index = (uint32_t)column_index;

        if (is_file || nmo_chunk_get_data_version(chunk) >= 5) {
            result = nmo_chunk_read_int(chunk, &out_state->key_column);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKDataArray SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKDataArray state to chunk
 * 
 * Implements the symmetric write operation for CKDataArray::Save.
 * Writes column formats, data matrix, and metadata.
 * 
 * Reference: reference/src/CKDataArray.cpp:1735-1822
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_dataarray_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_dataarray_state_t *in_state = (const nmo_dataarray_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_dataarray_serialize");
    }

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_dataarray_is_file_mode(out_chunk, context);

    nmo_last_error_clear();
    result = NMO_OK;

    /* Write column formats */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAARRAYFORMAT);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->column_count);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < in_state->column_count; i++) {
        const nmo_dataarray_column_format_t *fmt = &in_state->column_formats[i];

        result = nmo_chunk_write_string(out_chunk, fmt->name ? fmt->name : "");
        if (result != NMO_OK) return result;

        /* Reference writes WriteDword but reads ReadInt; use int for
           consistency since array type values are small enum constants. */
        result = nmo_chunk_write_int(out_chunk, (int32_t)fmt->type);
        if (result != NMO_OK) return result;

        if (fmt->type == CKARRAYTYPE_PARAMETER) {
            result = nmo_chunk_write_guid(out_chunk, fmt->parameter_type_guid);
            if (result != NMO_OK) return result;
        }
    }

    /* Write data matrix */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAARRAYDATA);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->row_count);
    if (result != NMO_OK) return result;

    for (uint32_t row_idx = 0; row_idx < in_state->row_count; row_idx++) {
        const nmo_dataarray_row_t *row = &in_state->rows[row_idx];

        for (uint32_t col_idx = 0; col_idx < in_state->column_count; col_idx++) {
            const nmo_dataarray_column_format_t *fmt = &in_state->column_formats[col_idx];
            const nmo_dataarray_cell_t *cell = &row->cells[col_idx];

            switch (fmt->type) {
            case CKARRAYTYPE_INT:
                result = nmo_chunk_write_int(out_chunk, cell->int_value);
                if (result != NMO_OK) return result;
                break;

            case CKARRAYTYPE_FLOAT:
                result = nmo_chunk_write_float(out_chunk, cell->float_value);
                if (result != NMO_OK) return result;
                break;

            case CKARRAYTYPE_STRING:
                result = nmo_chunk_write_string(out_chunk, cell->string_value ? cell->string_value : "");
                if (result != NMO_OK) return result;
                break;

            case CKARRAYTYPE_OBJECT:
                result = nmo_ref_write(out_chunk, &cell->object_ref);
                if (result != NMO_OK) return result;
                break;

            case CKARRAYTYPE_PARAMETER:
                if (is_file) {
                    result = nmo_ref_write(out_chunk, &cell->parameter.ref);
                    if (result != NMO_OK) return result;
                } else {
                    result = nmo_chunk_write_sub_chunk(
                        out_chunk, cell->parameter.chunk);
                    if (result != NMO_OK) return result;
                }
                break;

            default:
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Unknown array type");
            }
        }
    }

    /* Write metadata members */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAARRAYMEMBERS);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->order);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->column_index);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->key_column);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_dataarray_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_dataarray_state_t *s = src;
    nmo_dataarray_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));

    if (s->column_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->column_formats,
                                                  s->column_formats, sizeof(nmo_dataarray_column_format_t),
                                                  s->column_count));
        for (uint32_t i = 0; i < s->column_count; ++i) {
            if (s->column_formats[i].name) {
                d->column_formats[i].name = nmo_arena_strdup(arena, s->column_formats[i].name);
            }
        }
    }

    if (s->row_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->rows,
                                                  s->rows, sizeof(nmo_dataarray_row_t), s->row_count));
        for (uint32_t r = 0; r < s->row_count; ++r) {
            const nmo_dataarray_row_t *sr = &s->rows[r];
            nmo_dataarray_row_t *dr = &d->rows[r];
            if (sr->column_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dr->cells,
                                                          sr->cells, sizeof(nmo_dataarray_cell_t),
                                                          sr->column_count));
                for (uint32_t c = 0; c < sr->column_count; ++c) {
                    nmo_dataarray_cell_t *cell = &dr->cells[c];
                    if (d->column_formats && c < d->column_count) {
                        nmo_arraytype_t type_id = d->column_formats[c].type;
                        if (type_id == CKARRAYTYPE_STRING && sr->cells[c].string_value) {
                            cell->string_value = nmo_arena_strdup(arena, sr->cells[c].string_value);
                        } else if (type_id == CKARRAYTYPE_PARAMETER &&
                                   sr->cells[c].parameter.chunk) {
                            cell->parameter.chunk = nmo_chunk_clone(
                                sr->cells[c].parameter.chunk, arena);
                        }
                    }
                }
            }
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_dataarray_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_dataarray_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->column_formats, s->column_count, "column_formats");
    NMO_VALIDATE_COUNT(s->rows, s->row_count, "rows");
    NMO_RETURN_OK();
}

nmo_status_t nmo_dataarray_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_dataarray_remap_dependencies");
    }

    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)instance;
    (void)context;

    if (state->column_count > 0 && state->column_formats == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "DataArray column_formats missing");
    }
    if (state->row_count > 0 && state->rows == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "DataArray rows missing");
    }

    for (uint32_t r = 0; r < state->row_count; ++r) {
        nmo_dataarray_row_t *row = &state->rows[r];
        if (state->column_count > 0 && row->cells == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "DataArray row cells missing");
        }
    }
    /* Cell references and raw indexing metadata are not normalized here. */
    return nmo_dataarray_validate(state, NULL, NULL);
}

nmo_status_t nmo_dataarray_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_dataarray_validate(instance, type, context);
}

static nmo_status_t nmo_dataarray_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_dataarray_pre_delete");
    }
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)instance;
    state->column_count = 0;
    state->row_count = 0;
    state->column_formats = NULL;
    state->rows = NULL;
    state->order = 0;
    state->column_index = 0;
    state->key_column = -1;
    NMO_RETURN_OK();
}

static void nmo_dataarray_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Reference enumeration
 * ============================================================================ */

static nmo_status_t nmo_dataarray_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_dataarray_state_t *state = (const nmo_dataarray_state_t *)instance;
    if (state == NULL || visitor == NULL ||
        state->rows == NULL || state->column_formats == NULL) {
        NMO_RETURN_OK();
    }

    for (uint32_t row = 0; row < state->row_count; row++) {
        const nmo_dataarray_row_t *row_data = &state->rows[row];
        if (row_data->cells == NULL) {
            continue;
        }
        uint32_t col_count =
            row_data->column_count < state->column_count
                ? row_data->column_count
                : state->column_count;
        for (uint32_t col = 0; col < col_count; col++) {
            CK_ARRAYTYPE col_type = state->column_formats[col].type;
            nmo_object_id_t target_id = 0;
            nmo_ref_kind_t kind = NMO_REF_KIND_DATA_ARRAY;

            if (col_type == CKARRAYTYPE_OBJECT) {
                target_id = nmo_ref_runtime_id(
                    &row_data->cells[col].object_ref);
            } else if (col_type == CKARRAYTYPE_PARAMETER) {
                target_id = nmo_ref_runtime_id(
                    &row_data->cells[col].parameter.ref);
                kind = NMO_REF_KIND_PARAMETER;
            }

            if (target_id != 0) {
                uint32_t index = row * state->column_count + col;
                if (!visitor(user_data, target_id, kind, "rows", index)) {
                    NMO_RETURN_OK();
                }
            }
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(dataarray, nmo_dataarray_state_t)

nmo_type_vtable_t nmo_dataarray_vtable = {
    .prepare_dependencies = nmo_dataarray_prepare_dependencies,
    .remap_dependencies = nmo_dataarray_remap_dependencies,
    .pre_delete = nmo_dataarray_pre_delete,
    .post_delete = nmo_dataarray_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_dataarray_create,
        nmo_dataarray_destroy,
        nmo_dataarray_serialize,
        nmo_dataarray_deserialize,
        nmo_dataarray_copy,
        nmo_dataarray_validate,
        nmo_dataarray_equals,
        nmo_dataarray_hash,
        nmo_dataarray_enumerate_refs)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_dataarray_type,
    CKPGUID_DATAARRAY,
    "CKDataArray",
    NMO_CID_DATAARRAY,
    CKPGUID_BEOBJECT,
    nmo_dataarray_state_t,
    &nmo_dataarray_vtable,
    nmo_dataarray_fields)

