/**
 * @file nmo_ckdataarray_schemas.h
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
 * 
 * Based on official Virtools SDK (reference/src/CKDataArray.cpp:1735-1960).
 */

#ifndef NMO_CKDATAARRAY_SCHEMAS_H
#define NMO_CKDATAARRAY_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "nmo_ckbeobject_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_guid nmo_guid_t;

/* =============================================================================
 * ARRAY TYPE ENUMERATION
 * ============================================================================= */

/**
 * @brief Data array column type
 * 
 * Defines the type of data stored in a column.
 * Each type has specific serialization rules.
 */
typedef enum nmo_ck_arraytype {
    NMO_ARRAYTYPE_INT = 0,        /**< Integer (32-bit) */
    NMO_ARRAYTYPE_FLOAT = 1,      /**< Float (32-bit) */
    NMO_ARRAYTYPE_STRING = 2,     /**< String (null-terminated) */
    NMO_ARRAYTYPE_OBJECT = 3,     /**< Object reference (CK_ID) */
    NMO_ARRAYTYPE_PARAMETER = 4   /**< Parameter object (requires GUID) */
} nmo_ck_arraytype_t;

/* =============================================================================
 * CKDataArray STRUCTURES
 * ============================================================================= */

/**
 * @brief Column format descriptor
 * 
 * Describes a single column in the data array.
 * For PARAMETER type, parameter_type_guid is required.
 */
typedef struct nmo_ckdataarray_column_format {
    /**
     * @brief Column name
     * 
     * Allocated from arena, null-terminated string.
     */
    const char *name;

    /**
     * @brief Column data type
     * 
     * One of NMO_ARRAYTYPE_* values.
     */
    nmo_ck_arraytype_t type;

    /**
     * @brief Parameter type GUID (only for PARAMETER type)
     * 
     * Specifies the parameter type when type == NMO_ARRAYTYPE_PARAMETER.
     * Ignored for other types.
     */
    nmo_guid_t parameter_type_guid;
} nmo_ckdataarray_column_format_t;

/**
 * @brief Data array cell value (union for type safety)
 * 
 * Stores a single cell value in the data matrix.
 * The actual type is determined by the column format.
 */
typedef union nmo_ckdataarray_cell {
    int32_t int_value;            /**< INT type value */
    float float_value;            /**< FLOAT type value */
    const char *string_value;     /**< STRING type value (allocated from arena) */
    nmo_object_id_t object_id;    /**< OBJECT type value */
    nmo_chunk_t *parameter_chunk; /**< PARAMETER type value (sub-chunk) */
} nmo_ckdataarray_cell_t;

/**
 * @brief Data array row
 * 
 * Represents a single row in the data matrix.
 * Contains column_count cells.
 */
typedef struct nmo_ckdataarray_row {
    /**
     * @brief Number of cells in this row
     * 
     * Must match the number of columns in the array.
     */
    uint32_t column_count;

    /**
     * @brief Cell values
     * 
     * Array of column_count cells, allocated from arena.
     * The type of each cell is determined by the corresponding column format.
     */
    nmo_ckdataarray_cell_t *cells;
} nmo_ckdataarray_row_t;

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
typedef struct nmo_ckdataarray_state {
    /* Base class state */
    nmo_ckbeobject_state_t base;         /**< CKBeObject base state */
    
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
    nmo_ckdataarray_column_format_t *column_formats;

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
    nmo_ckdataarray_row_t *rows;

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
} nmo_ckdataarray_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief Function pointer for CKDataArray deserialization
 * 
 * @param chunk Chunk to read from
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckdataarray_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckdataarray_state_t *out_state);

/**
 * @brief Function pointer for CKDataArray serialization
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckdataarray_serialize_fn)(
    const nmo_ckdataarray_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

/**
 * @brief Register CKDataArray schema types
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckdataarray_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get the deserialize function for CKDataArray
 * 
 * @return Deserialize function pointer
 */
nmo_ckdataarray_deserialize_fn nmo_get_ckdataarray_deserialize(void);

/**
 * @brief Get the serialize function for CKDataArray
 * 
 * @return Serialize function pointer
 */
nmo_ckdataarray_serialize_fn nmo_get_ckdataarray_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKDATAARRAY_SCHEMAS_H */
