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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void nmo_dataarray_dispose_base_arrays(nmo_dataarray_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->base.scripts);
    nmo_array_dispose(&state->base.attributes);
    nmo_array_dispose(&state->base.legacy_attributes);
}

static void nmo_dataarray_dispose_copied_base_arrays(
    nmo_beobject_state_t *copied,
    const nmo_beobject_state_t *source)
{
#define NMO_DATAARRAY_DISPOSE_COPIED_ARRAY(field) \
    do { \
        if (copied->field.data == source->field.data) { \
            memset(&copied->field, 0, sizeof(copied->field)); \
        } else { \
            nmo_array_dispose(&copied->field); \
        } \
    } while (0)
    NMO_DATAARRAY_DISPOSE_COPIED_ARRAY(scripts);
    NMO_DATAARRAY_DISPOSE_COPIED_ARRAY(attributes);
    NMO_DATAARRAY_DISPOSE_COPIED_ARRAY(legacy_attributes);
#undef NMO_DATAARRAY_DISPOSE_COPIED_ARRAY
}

static nmo_status_t nmo_dataarray_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_dataarray_state_t *state = instance;
    memset(state, 0, sizeof(*state));
    state->key_column = -1;
    return nmo_beobject_vtable.create(&state->base, NULL, context);
}

static void nmo_dataarray_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) return;
    nmo_dataarray_state_t *state = instance;
    nmo_dataarray_dispose_base_arrays(state);
    memset(state, 0, sizeof(*state));
}

static size_t nmo_dataarray_identifier_remaining_dwords(nmo_chunk_t *chunk)
{
    if (chunk == NULL || chunk->parser_state == NULL) return 0;

    nmo_chunk_parser_state_t *state =
        (nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = 0;
    if (state->prev_identifier_pos + 1 < chunk->data.count) {
        next_pos = data[state->prev_identifier_pos + 1];
    }
    if (next_pos == 0 || next_pos > chunk->data.count) {
        next_pos = chunk->data.count;
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

static bool nmo_dataarray_size_mul_overflows(size_t count, size_t element_size)
{
    return count != 0 && element_size > SIZE_MAX / count;
}

static nmo_status_t nmo_dataarray_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

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
static nmo_status_t nmo_dataarray_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_dataarray_state_t *out_state = (nmo_dataarray_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || arena == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_dataarray_deserialize");
    }

    /* Deserialize base CKBeObject state first */
    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_dataarray_is_file_mode(chunk, context);

    nmo_last_error_clear();
    result = NMO_OK;

    /* Read column formats */
    size_t format_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_DATAARRAYFORMAT, &format_dwords);
    if (result == NMO_OK) {
        const size_t format_end =
            nmo_chunk_get_position(chunk) + format_dwords;
        int32_t column_count;
        result = nmo_chunk_read_int(chunk, &column_count);
        if (result != NMO_OK) return result;

        if (column_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid column count");
        }
        if (nmo_dataarray_size_mul_overflows(
                (size_t)column_count,
                sizeof(nmo_dataarray_column_format_t))) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Column format allocation size overflows");
        }
        if ((size_t)column_count >
            nmo_dataarray_identifier_remaining_dwords(chunk) / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Column count exceeds format section payload");
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
        const size_t position = nmo_chunk_get_position(chunk);
        if (position != format_end) {
            return position > format_end
                ? NMO_ERR_TRUNCATED_CHUNK
                : NMO_ERR_INVALID_FORMAT;
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Read data matrix */
    size_t data_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_DATAARRAYDATA, &data_dwords);
    if (result == NMO_OK) {
        const size_t data_end =
            nmo_chunk_get_position(chunk) + data_dwords;
        int32_t row_count;
        result = nmo_chunk_read_int(chunk, &row_count);
        if (result != NMO_OK) return result;

        if (row_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid row count");
        }
        if (nmo_dataarray_size_mul_overflows(
                (size_t)row_count, sizeof(nmo_dataarray_row_t))) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Row allocation size overflows");
        }
        if (out_state->column_count > 0 &&
            out_state->column_formats == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Data section has no column formats");
        }
        if (nmo_dataarray_size_mul_overflows(
                out_state->column_count, sizeof(nmo_dataarray_cell_t))) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Row cell allocation size overflows");
        }
        if (nmo_dataarray_size_mul_overflows(
                (size_t)row_count, out_state->column_count)) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Data array cell count overflows");
        }
        const size_t total_cells =
            (size_t)row_count * out_state->column_count;
        if (total_cells > nmo_dataarray_identifier_remaining_dwords(chunk)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Cell count exceeds data section payload");
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
                            nmo_ref_check_class(
                                &cell->object_ref,
                                (const nmo_object_repository_t *)
                                    nmo_deserialize_context_get_repository(context),
                                nmo_deserialize_context_get_type_registry(context),
                                NMO_CID_OBJECT);
                            break;

                        case CKARRAYTYPE_PARAMETER:
                            if (is_file) {
                                result = nmo_ref_read(chunk, &cell->parameter.ref);
                                if (result != NMO_OK) return result;
                                nmo_ref_check_class(
                                    &cell->parameter.ref,
                                    (const nmo_object_repository_t *)
                                        nmo_deserialize_context_get_repository(context),
                                    nmo_deserialize_context_get_type_registry(context),
                                    NMO_CID_PARAMETER);
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
        const size_t position = nmo_chunk_get_position(chunk);
        if (position != data_end) {
            return position > data_end
                ? NMO_ERR_TRUNCATED_CHUNK
                : NMO_ERR_INVALID_FORMAT;
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Read metadata members */
    size_t members_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_DATAARRAYMEMBERS, &members_dwords);
    if (result == NMO_OK) {
        const size_t members_end =
            nmo_chunk_get_position(chunk) + members_dwords;
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
        const size_t position = nmo_chunk_get_position(chunk);
        if (position != members_end) {
            return position > members_end
                ? NMO_ERR_TRUNCATED_CHUNK
                : NMO_ERR_INVALID_FORMAT;
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_dataarray_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_dataarray_state_t *out_state = (nmo_dataarray_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_dataarray_state_t decoded;
    nmo_status_t result = nmo_dataarray_create(&decoded, type, context);
    if (result != NMO_OK) return result;
    if (out_state->base.scripts.allocator.alloc != NULL) {
        decoded.base.scripts.allocator = out_state->base.scripts.allocator;
    }
    if (out_state->base.attributes.allocator.alloc != NULL) {
        decoded.base.attributes.allocator = out_state->base.attributes.allocator;
    }
    if (out_state->base.legacy_attributes.allocator.alloc != NULL) {
        decoded.base.legacy_attributes.allocator =
            out_state->base.legacy_attributes.allocator;
    }

    result = nmo_dataarray_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_dataarray_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_dataarray_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
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
static nmo_status_t nmo_dataarray_serialize_internal(
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
    NMO_RETURN_IF_ERROR(nmo_dataarray_validate(in_state, type, context));

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

    if (is_file || nmo_chunk_get_data_version(out_chunk) >= 5u) {
        result = nmo_chunk_write_int(out_chunk, in_state->key_column);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_dataarray_serialize(
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

    nmo_status_t result = nmo_dataarray_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_dataarray_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    if (src == NULL || dst == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (src == dst) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_dataarray_validate(src, type, NULL));

    const nmo_dataarray_state_t *source = src;
    nmo_dataarray_state_t copied;
    nmo_status_t result = nmo_dataarray_create(&copied, type, NULL);
    if (result != NMO_OK) return result;

    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_beobject_state_t),
    };
    result = nmo_beobject_vtable.copy(
        &source->base, &copied.base, &base_type, arena);
    if (result != NMO_OK) goto fail;

    copied.column_count = source->column_count;
    copied.row_count = source->row_count;
    copied.order = source->order;
    copied.column_index = source->column_index;
    copied.key_column = source->key_column;

    result = nmo_object_copy_array(
        arena, (void **)&copied.column_formats, source->column_formats,
        sizeof(*copied.column_formats), copied.column_count);
    if (result != NMO_OK) goto fail;
    for (uint32_t i = 0; i < copied.column_count; ++i) {
        copied.column_formats[i].name = NULL;
        result = nmo_object_copy_string(
            arena, (char **)&copied.column_formats[i].name,
            source->column_formats[i].name);
        if (result != NMO_OK) goto fail;
    }

    result = nmo_object_copy_array(
        arena, (void **)&copied.rows, source->rows,
        sizeof(*copied.rows), copied.row_count);
    if (result != NMO_OK) goto fail;
    for (uint32_t row_index = 0; row_index < copied.row_count; ++row_index) {
        const nmo_dataarray_row_t *source_row = &source->rows[row_index];
        nmo_dataarray_row_t *copied_row = &copied.rows[row_index];
        copied_row->cells = NULL;
        result = nmo_object_copy_array(
            arena, (void **)&copied_row->cells, source_row->cells,
            sizeof(*copied_row->cells), source_row->column_count);
        if (result != NMO_OK) goto fail;

        for (uint32_t column_index = 0;
             column_index < source_row->column_count;
             ++column_index) {
            const nmo_dataarray_cell_t *source_cell =
                &source_row->cells[column_index];
            nmo_dataarray_cell_t *copied_cell =
                &copied_row->cells[column_index];
            const nmo_arraytype_t cell_type =
                source->column_formats[column_index].type;
            if (cell_type == CKARRAYTYPE_STRING) {
                copied_cell->string_value = NULL;
                result = nmo_object_copy_string(
                    arena, (char **)&copied_cell->string_value,
                    source_cell->string_value);
                if (result != NMO_OK) goto fail;
            } else if (cell_type == CKARRAYTYPE_PARAMETER) {
                copied_cell->parameter.chunk = NULL;
                result = nmo_object_copy_chunk(
                    arena, &copied_cell->parameter.chunk,
                    source_cell->parameter.chunk);
                if (result != NMO_OK) goto fail;
            }
        }
    }

    nmo_dataarray_state_t *target = dst;
    nmo_dataarray_dispose_base_arrays(target);
    *target = copied;
    return NMO_OK;

fail:
    nmo_dataarray_dispose_copied_base_arrays(
        &copied.base, &source->base);
    return result;
}

static nmo_status_t nmo_dataarray_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_dataarray_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.validate(
        &s->base, NULL, context));
    if (s->column_count > (uint32_t)INT32_MAX ||
        s->row_count > (uint32_t)INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "DataArray dimensions exceed format limits");
    }
    NMO_VALIDATE_COUNT(s->column_formats, s->column_count, "column_formats");
    NMO_VALIDATE_COUNT(s->rows, s->row_count, "rows");
    if (nmo_dataarray_size_mul_overflows(
            s->column_count, sizeof(*s->column_formats)) ||
        nmo_dataarray_size_mul_overflows(
            s->row_count, sizeof(*s->rows)) ||
        nmo_dataarray_size_mul_overflows(
            s->column_count, sizeof(nmo_dataarray_cell_t)) ||
        nmo_dataarray_size_mul_overflows(
            s->row_count, s->column_count)) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "DataArray allocation size overflows");
    }
    for (uint32_t column_index = 0;
         column_index < s->column_count;
         ++column_index) {
        switch (s->column_formats[column_index].type) {
        case CKARRAYTYPE_INT:
        case CKARRAYTYPE_FLOAT:
        case CKARRAYTYPE_STRING:
        case CKARRAYTYPE_OBJECT:
        case CKARRAYTYPE_PARAMETER:
            break;
        default:
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                             NMO_SEVERITY_ERROR,
                             "DataArray column type is invalid");
        }
    }
    for (uint32_t row_index = 0; row_index < s->row_count; ++row_index) {
        const nmo_dataarray_row_t *row = &s->rows[row_index];
        if (row->column_count != s->column_count) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "DataArray row column count mismatch");
        }
        NMO_VALIDATE_COUNT(row->cells, row->column_count, "row.cells");
    }
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
    if (state == NULL || visitor == NULL) {
        NMO_RETURN_OK();
    }
    NMO_RETURN_IF_ERROR(nmo_dataarray_validate(state, NULL, NULL));

    for (uint32_t row = 0; row < state->row_count; row++) {
        const nmo_dataarray_row_t *row_data = &state->rows[row];
        for (uint32_t col = 0; col < state->column_count; col++) {
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

static bool nmo_dataarray_string_equals(const char *lhs, const char *rhs)
{
    if (lhs == rhs) return true;
    return lhs != NULL && rhs != NULL && strcmp(lhs, rhs) == 0;
}

static bool nmo_dataarray_ref_equals(const nmo_ref_t *lhs, const nmo_ref_t *rhs)
{
    return lhs->raw_id == rhs->raw_id &&
        lhs->id == rhs->id &&
        lhs->state == rhs->state;
}

static bool nmo_dataarray_chunk_equals(
    const nmo_chunk_t *lhs,
    const nmo_chunk_t *rhs)
{
    if (lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL) return false;
    size_t lhs_size = 0;
    size_t rhs_size = 0;
    const void *lhs_data = nmo_chunk_get_data(lhs, &lhs_size);
    const void *rhs_data = nmo_chunk_get_data(rhs, &rhs_size);
    return lhs_size == rhs_size &&
        (lhs_size == 0 ||
         (lhs_data != NULL && rhs_data != NULL &&
          memcmp(lhs_data, rhs_data, lhs_size) == 0));
}

static bool nmo_dataarray_cell_equals(
    const nmo_dataarray_cell_t *lhs,
    const nmo_dataarray_cell_t *rhs,
    nmo_arraytype_t type)
{
    switch (type) {
    case CKARRAYTYPE_INT:
        return lhs->int_value == rhs->int_value;
    case CKARRAYTYPE_FLOAT:
        return memcmp(
            &lhs->float_value, &rhs->float_value,
            sizeof(lhs->float_value)) == 0;
    case CKARRAYTYPE_STRING:
        return nmo_dataarray_string_equals(
            lhs->string_value, rhs->string_value);
    case CKARRAYTYPE_OBJECT:
        return nmo_dataarray_ref_equals(
            &lhs->object_ref, &rhs->object_ref);
    case CKARRAYTYPE_PARAMETER:
        return nmo_dataarray_ref_equals(
                &lhs->parameter.ref, &rhs->parameter.ref) &&
            nmo_dataarray_chunk_equals(
                lhs->parameter.chunk, rhs->parameter.chunk);
    default:
        return false;
    }
}

static bool nmo_dataarray_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_dataarray_state_t *lhs = a;
    const nmo_dataarray_state_t *rhs = b;
    if (!nmo_beobject_vtable.equals(&lhs->base, &rhs->base) ||
        lhs->column_count != rhs->column_count ||
        lhs->row_count != rhs->row_count ||
        lhs->order != rhs->order ||
        lhs->column_index != rhs->column_index ||
        lhs->key_column != rhs->key_column ||
        (lhs->column_count > 0 &&
         (lhs->column_formats == NULL || rhs->column_formats == NULL)) ||
        (lhs->row_count > 0 &&
         (lhs->rows == NULL || rhs->rows == NULL))) {
        return false;
    }

    for (uint32_t column_index = 0;
         column_index < lhs->column_count;
         ++column_index) {
        const nmo_dataarray_column_format_t *lhs_format =
            &lhs->column_formats[column_index];
        const nmo_dataarray_column_format_t *rhs_format =
            &rhs->column_formats[column_index];
        if (!nmo_dataarray_string_equals(lhs_format->name, rhs_format->name) ||
            lhs_format->type != rhs_format->type ||
            !nmo_guid_equals(lhs_format->parameter_type_guid,
                             rhs_format->parameter_type_guid)) {
            return false;
        }
    }

    for (uint32_t row_index = 0; row_index < lhs->row_count; ++row_index) {
        const nmo_dataarray_row_t *lhs_row = &lhs->rows[row_index];
        const nmo_dataarray_row_t *rhs_row = &rhs->rows[row_index];
        if (lhs_row->column_count != rhs_row->column_count ||
            lhs_row->column_count != lhs->column_count ||
            (lhs_row->column_count > 0 &&
             (lhs_row->cells == NULL || rhs_row->cells == NULL))) {
            return false;
        }
        for (uint32_t column_index = 0;
             column_index < lhs_row->column_count;
             ++column_index) {
            if (!nmo_dataarray_cell_equals(
                    &lhs_row->cells[column_index],
                    &rhs_row->cells[column_index],
                    lhs->column_formats[column_index].type)) {
                return false;
            }
        }
    }
    return true;
}

static uint32_t nmo_dataarray_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_dataarray_hash_string(
    uint32_t hash,
    const char *string)
{
    const uint8_t present = string != NULL;
    hash = nmo_dataarray_hash_bytes(hash, &present, sizeof(present));
    return present
        ? nmo_dataarray_hash_bytes(hash, string, strlen(string) + 1u)
        : hash;
}

static uint32_t nmo_dataarray_hash_ref(
    uint32_t hash,
    const nmo_ref_t *ref)
{
    hash = nmo_dataarray_hash_bytes(hash, &ref->raw_id, sizeof(ref->raw_id));
    hash = nmo_dataarray_hash_bytes(hash, &ref->id, sizeof(ref->id));
    return nmo_dataarray_hash_bytes(hash, &ref->state, sizeof(ref->state));
}

static uint32_t nmo_dataarray_hash_chunk(
    uint32_t hash,
    const nmo_chunk_t *chunk)
{
    const uint8_t present = chunk != NULL;
    hash = nmo_dataarray_hash_bytes(hash, &present, sizeof(present));
    if (chunk == NULL) return hash;
    size_t size = 0;
    const void *data = nmo_chunk_get_data(chunk, &size);
    hash = nmo_dataarray_hash_bytes(hash, &size, sizeof(size));
    return data != NULL && size > 0
        ? nmo_dataarray_hash_bytes(hash, data, size)
        : hash;
}

static uint32_t nmo_dataarray_hash_cell(
    uint32_t hash,
    const nmo_dataarray_cell_t *cell,
    nmo_arraytype_t type)
{
    switch (type) {
    case CKARRAYTYPE_INT:
        return nmo_dataarray_hash_bytes(
            hash, &cell->int_value, sizeof(cell->int_value));
    case CKARRAYTYPE_FLOAT:
        return nmo_dataarray_hash_bytes(
            hash, &cell->float_value, sizeof(cell->float_value));
    case CKARRAYTYPE_STRING:
        return nmo_dataarray_hash_string(hash, cell->string_value);
    case CKARRAYTYPE_OBJECT:
        return nmo_dataarray_hash_ref(hash, &cell->object_ref);
    case CKARRAYTYPE_PARAMETER:
        hash = nmo_dataarray_hash_ref(hash, &cell->parameter.ref);
        return nmo_dataarray_hash_chunk(hash, cell->parameter.chunk);
    default:
        return hash;
    }
}

static uint32_t nmo_dataarray_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_dataarray_state_t *state = instance;
    uint32_t hash = nmo_beobject_vtable.hash(&state->base);
#define NMO_DATAARRAY_HASH_FIELD(field) \
    hash = nmo_dataarray_hash_bytes(hash, &(field), sizeof(field))
    NMO_DATAARRAY_HASH_FIELD(state->column_count);
    if (state->column_count > 0 && state->column_formats == NULL) return hash;
    for (uint32_t column_index = 0;
         column_index < state->column_count;
         ++column_index) {
        const nmo_dataarray_column_format_t *format =
            &state->column_formats[column_index];
        hash = nmo_dataarray_hash_string(hash, format->name);
        NMO_DATAARRAY_HASH_FIELD(format->type);
        NMO_DATAARRAY_HASH_FIELD(format->parameter_type_guid.d1);
        NMO_DATAARRAY_HASH_FIELD(format->parameter_type_guid.d2);
    }
    NMO_DATAARRAY_HASH_FIELD(state->row_count);
    if (state->row_count > 0 && state->rows == NULL) return hash;
    for (uint32_t row_index = 0; row_index < state->row_count; ++row_index) {
        const nmo_dataarray_row_t *row = &state->rows[row_index];
        NMO_DATAARRAY_HASH_FIELD(row->column_count);
        if (row->column_count > 0 && row->cells == NULL) return hash;
        const uint32_t cell_count = row->column_count < state->column_count
            ? row->column_count
            : state->column_count;
        for (uint32_t column_index = 0;
             column_index < cell_count;
             ++column_index) {
            hash = nmo_dataarray_hash_cell(
                hash, &row->cells[column_index],
                state->column_formats[column_index].type);
        }
    }
    NMO_DATAARRAY_HASH_FIELD(state->order);
    NMO_DATAARRAY_HASH_FIELD(state->column_index);
    NMO_DATAARRAY_HASH_FIELD(state->key_column);
#undef NMO_DATAARRAY_HASH_FIELD
    return hash;
}

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

