/**
 * @file nmo_dataarray_schemas.h
 * @brief CKDataArray schema definitions
 *
 * CKDataArray represents a 2D table/matrix with typed columns.
 * It's used for storing structured data in Virtools (similar to spreadsheet).
 * 
 * Supports 5 data types:
 * - INT: Integer values
 * - FLOAT: Floating point values
 * - STRING: String values
 * - OBJECT: Object references (CK_ID)
 * - PARAMETER: Parameter objects (CKParameterOut)
 *   - File mode: stored as object ID
 *   - Non-file: stored as sub-chunk
 * 
 * Based on official Virtools SDK (reference/src/CKDataArray.cpp:1735-1960).
 */

#ifndef NMO_CKDATAARRAY_SCHEMAS_H
#define NMO_CKDATAARRAY_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "nmo_beobject_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;

typedef struct nmo_guid nmo_guid_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * ARRAY TYPE ENUMERATION
 * ============================================================================= */

/**
 * @brief Data array column type
 * 
 * Defines the type of data stored in a column.
 * Each type has specific serialization rules.
 */
typedef CK_ARRAYTYPE nmo_arraytype_t;

/**
 * @brief CKDataArray state structure
 * 
 * Complete state for CKDataArray serialization.
 * 
 * Structure:
 * - Column formats define the table schema (types, names)
 * - Data rows contain the actual values
 * - Metadata controls sorting and indexing
 * 
 * Reference: reference/src/CKDataArray.cpp:1735-1960
 */
typedef struct nmo_dataarray_state {
    /* Base class state */
    nmo_beobject_state_t base;         /**< CKBeObject base state */
    
    /**
     * @brief Number of columns
     * 
     * Defines the width of the table.
     */
    uint32_t column_count;

    /**
     * @brief Column format definitions
     * 
     * Array of column_count formats, allocated from arena.
     * Defines the schema of the table.
     */
    nmo_dataarray_column_format_t *column_formats;

    /**
     * @brief Number of rows
     * 
     * Defines the height of the table.
     */
    uint32_t row_count;

    /**
     * @brief Data rows
     * 
     * Array of row_count rows, allocated from arena.
     * Contains the actual table data.
     */
    nmo_dataarray_row_t *rows;

    /**
     * @brief Sorting order
     * 
     * Controls how rows are sorted:
     * - 0: No sorting
     * - 1: Ascending
     * - 2: Descending
     */
    int32_t order;

    /**
     * @brief Column index for sorting
     * 
     * Index of the column used for sorting (0-based).
     * Only meaningful when order != 0.
     */
    uint32_t column_index;

    /**
     * @brief Key column index
     * 
     * Index of the column used as primary key (0-based).
     * -1 means no key column.
     * Added in file version 5.
     */
    int32_t key_column;
} nmo_dataarray_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_dataarray_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_dataarray_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_dataarray_vtable, nmo_register_dataarray_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKDATAARRAY_SCHEMAS_H */
