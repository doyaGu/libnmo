/**
 * @file ckdataarray_schemas.c
 * @brief CKDataArray schema implementation
 *
 * Implements schema-driven deserialization for CKDataArray (2D data tables).
 * CKDataArray extends CKBeObject and provides structured table storage.
 * 
 * Based on official Virtools SDK (reference/src/CKDataArray.cpp:1735-1960).
 */

#include "object/nmo_ckdataarray_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_param_guids.h"
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
 * IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKDataArray.cpp */
#define CK_STATESAVE_DATAARRAYFORMAT  0x00000001
#define CK_STATESAVE_DATAARRAYDATA    0x00000002
#define CK_STATESAVE_DATAARRAYMEMBERS 0x00000004

static int nmo_chunk_is_file_mode(const nmo_chunk_t *chunk) {
    return chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);
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
nmo_result_t nmo_ckdataarray_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckdataarray_state_t *out_state = (nmo_ckdataarray_state_t *)instance;
        nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckdataarray_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckdataarray_state_t));
    
    /* Deserialize base CKBeObject state first */
    nmo_result_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) return result;
    out_state->key_column = -1; /* Default: no key column */

    result = nmo_result_ok();

    /* Read column formats */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAARRAYFORMAT);
    if (result.code == NMO_OK) {
        int32_t column_count;
        result = nmo_chunk_read_int(chunk, &column_count);
        if (result.code != NMO_OK) return result;

        if (column_count < 0 || column_count > 10000) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Invalid column count"));
        }

        out_state->column_count = (uint32_t)column_count;
        if (column_count > 0) {
            out_state->column_formats = (nmo_ckdataarray_column_format_t *)
                nmo_arena_alloc(arena, column_count * sizeof(nmo_ckdataarray_column_format_t),
                               _Alignof(nmo_ckdataarray_column_format_t));
            if (!out_state->column_formats) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate column formats"));
            }

            /* Read each column format */
            for (int32_t i = 0; i < column_count; i++) {
                nmo_ckdataarray_column_format_t *fmt = &out_state->column_formats[i];

                /* Read column name */
                char *temp_name = NULL;
                nmo_chunk_read_string(chunk, &temp_name);
                fmt->name = temp_name; /* Note: This relies on chunk's internal buffer */

                /* Read column type */
                int32_t type;
                result = nmo_chunk_read_int(chunk, &type);
                if (result.code != NMO_OK) return result;
                fmt->type = (nmo_ck_arraytype_t)((uint32_t)type);

                /* Read parameter type GUID for PARAMETER columns */
                if (fmt->type == NMO_ARRAYTYPE_PARAMETER) {
                    result = nmo_chunk_read_guid(chunk, &fmt->parameter_type_guid);
                    if (result.code != NMO_OK) return result;

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
    if (result.code == NMO_OK) {
        int32_t row_count;
        result = nmo_chunk_read_int(chunk, &row_count);
        if (result.code != NMO_OK) return result;

        if (row_count < 0 || row_count > 1000000) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Invalid row count"));
        }

        out_state->row_count = (uint32_t)row_count;
        if (row_count > 0) {
            out_state->rows = (nmo_ckdataarray_row_t *)
                nmo_arena_alloc(arena, row_count * sizeof(nmo_ckdataarray_row_t),
                               _Alignof(nmo_ckdataarray_row_t));
            if (!out_state->rows) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate rows"));
            }

            /* Read each row */
            for (uint32_t row_idx = 0; row_idx < out_state->row_count; row_idx++) {
                nmo_ckdataarray_row_t *row = &out_state->rows[row_idx];
                row->column_count = out_state->column_count;

                if (out_state->column_count > 0) {
                    row->cells = (nmo_ckdataarray_cell_t *)
                        nmo_arena_alloc(arena, out_state->column_count * sizeof(nmo_ckdataarray_cell_t),
                                       _Alignof(nmo_ckdataarray_cell_t));
                    if (!row->cells) {
                        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                            NMO_SEVERITY_ERROR, "Failed to allocate row cells"));
                    }

                    /* Read each cell */
                    for (uint32_t col_idx = 0; col_idx < out_state->column_count; col_idx++) {
                        nmo_ckdataarray_column_format_t *fmt = &out_state->column_formats[col_idx];
                        nmo_ckdataarray_cell_t *cell = &row->cells[col_idx];

                        switch (fmt->type) {
                        case NMO_ARRAYTYPE_INT:
                            result = nmo_chunk_read_int(chunk, &cell->int_value);
                            if (result.code != NMO_OK) return result;
                            break;

                        case NMO_ARRAYTYPE_FLOAT:
                            result = nmo_chunk_read_float(chunk, &cell->float_value);
                            if (result.code != NMO_OK) return result;
                            break;

                        case NMO_ARRAYTYPE_STRING: {
                            char *temp_str = NULL;
                            nmo_chunk_read_string(chunk, &temp_str);
                            cell->string_value = temp_str; /* Note: Relies on chunk's buffer */
                            break;
                        }

                        case NMO_ARRAYTYPE_OBJECT:
                            result = nmo_chunk_read_object_id(chunk, &cell->object_id);
                            if (result.code != NMO_OK) return result;
                            break;

                        case NMO_ARRAYTYPE_PARAMETER:
                            if (nmo_chunk_is_file_mode(chunk)) {
                                result = nmo_chunk_read_object_id(chunk, &cell->parameter_id);
                                if (result.code != NMO_OK) return result;
                            } else {
                                result = nmo_chunk_read_sub_chunk(chunk, &cell->parameter_chunk);
                                if (result.code != NMO_OK) return result;
                            }
                            break;

                        default:
                            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                NMO_SEVERITY_ERROR, "Unknown array type"));
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
    if (result.code == NMO_OK) {
        result = nmo_chunk_read_int(chunk, &out_state->order);
        if (result.code != NMO_OK) return result;

        int32_t column_index;
        result = nmo_chunk_read_int(chunk, &column_index);
        if (result.code != NMO_OK) return result;
        out_state->column_index = (uint32_t)column_index;

        if (nmo_chunk_is_file_mode(chunk) || nmo_chunk_get_data_version(chunk) >= 5) {
            result = nmo_chunk_read_int(chunk, &out_state->key_column);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
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
nmo_result_t nmo_ckdataarray_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckdataarray_state_t *in_state = (const nmo_ckdataarray_state_t *)instance;
        nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckdataarray_serialize"));
    }

    /* Write base class (CKBeObject) data */
    nmo_result_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    result = nmo_result_ok();

    /* Write column formats */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAARRAYFORMAT);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->column_count);
    if (result.code != NMO_OK) return result;

    for (uint32_t i = 0; i < in_state->column_count; i++) {
        const nmo_ckdataarray_column_format_t *fmt = &in_state->column_formats[i];

        result = nmo_chunk_write_string(out_chunk, fmt->name ? fmt->name : "");
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)fmt->type);
        if (result.code != NMO_OK) return result;

        if (fmt->type == NMO_ARRAYTYPE_PARAMETER) {
            result = nmo_chunk_write_guid(out_chunk, fmt->parameter_type_guid);
            if (result.code != NMO_OK) return result;
        }
    }

    /* Write data matrix */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAARRAYDATA);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->row_count);
    if (result.code != NMO_OK) return result;

    for (uint32_t row_idx = 0; row_idx < in_state->row_count; row_idx++) {
        const nmo_ckdataarray_row_t *row = &in_state->rows[row_idx];

        for (uint32_t col_idx = 0; col_idx < in_state->column_count; col_idx++) {
            const nmo_ckdataarray_column_format_t *fmt = &in_state->column_formats[col_idx];
            const nmo_ckdataarray_cell_t *cell = &row->cells[col_idx];

            switch (fmt->type) {
            case NMO_ARRAYTYPE_INT:
                result = nmo_chunk_write_int(out_chunk, cell->int_value);
                if (result.code != NMO_OK) return result;
                break;

            case NMO_ARRAYTYPE_FLOAT:
                result = nmo_chunk_write_float(out_chunk, cell->float_value);
                if (result.code != NMO_OK) return result;
                break;

            case NMO_ARRAYTYPE_STRING:
                result = nmo_chunk_write_string(out_chunk, cell->string_value ? cell->string_value : "");
                if (result.code != NMO_OK) return result;
                break;

            case NMO_ARRAYTYPE_OBJECT:
                result = nmo_chunk_write_object_id(out_chunk, cell->object_id);
                if (result.code != NMO_OK) return result;
                break;

            case NMO_ARRAYTYPE_PARAMETER:
                if (nmo_chunk_is_file_mode(out_chunk)) {
                    result = nmo_chunk_write_object_id(out_chunk, cell->parameter_id);
                    if (result.code != NMO_OK) return result;
                } else if (cell->parameter_chunk) {
                    result = nmo_chunk_write_sub_chunk(out_chunk, cell->parameter_chunk);
                    if (result.code != NMO_OK) return result;
                }
                break;

            default:
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                    NMO_SEVERITY_ERROR, "Unknown array type"));
            }
        }
    }

    /* Write metadata members */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAARRAYMEMBERS);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->order);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->column_index);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->key_column);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckdataarray,
    nmo_ckdataarray_state_t,
    nmo_ckdataarray_serialize,
    nmo_ckdataarray_deserialize,
    NMO_GUID_CKDATAARRAY,
    "CKDataArray",
    NMO_CID_DATAARRAY,
    NMO_GUID_CKBEOBJECT
)

