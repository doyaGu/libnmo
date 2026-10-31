/**
 * @file engine_array.h
 * @brief Data arrays (CKDataArray) and groups (CKGroup) as the engine changes
 *        them at run time.
 */

#ifndef NMO_ENGINE_ARRAY_H
#define NMO_ENGINE_ARRAY_H

#include "engine_base.h"

typedef struct eng_range eng_range_t;

/* A CKDataArray cell: INT, FLOAT and OBJECT hold 32 bits, STRING text, PARAMETER bytes. */
typedef struct eng_cell {
    uint32_t value;
    char *text;     /* STRING; NULL for no string */
    uint8_t *data;  /* PARAMETER */
    uint32_t size;
} eng_cell_t;

typedef struct eng_column {
    int32_t type;           /* CK_ARRAYTYPE */
    nmo_guid_t param_type;  /* PARAMETER columns */
    char *name;             /* NULL for none */
} eng_column_t;

/* A runtime copy of a CKDataArray; cells row by row. */
typedef struct eng_array {
    nmo_object_id_t id;
    uint32_t column_count;
    eng_column_t *columns;
    eng_cell_t *cells;
    size_t row_count;
    size_t row_capacity;
    int32_t key_column;     /* -1 when none */
    bool sorted;            /* m_Order: sorted ascending on sort_column, and not changed since */
    int32_t sort_column;    /* m_ColumnIndex */
} eng_array_t;

/* A CKGroup: its objects now, by engine ID. */
typedef struct eng_group {
    nmo_object_id_t id;
    nmo_object_id_t *objects;
    size_t count;
    size_t capacity;
} eng_group_t;

nmo_status_t eng_load_arrays(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_arrays(nmo_engine_t *engine);
eng_array_t *eng_find_array(const nmo_engine_t *engine, nmo_object_id_t id);
nmo_status_t eng_add_array(nmo_engine_t *engine, nmo_object_id_t id);
bool eng_array_clone(eng_array_t *dest, const eng_array_t *source);
void eng_array_release(eng_array_t *array);
nmo_status_t eng_load_groups(nmo_engine_t *engine, const eng_range_t *range);
nmo_status_t eng_add_group(nmo_engine_t *engine, nmo_object_id_t id);
void eng_group_join(nmo_engine_t *engine, nmo_object_id_t original, nmo_object_id_t copy);
bool eng_group_add(nmo_engine_t *engine, nmo_object_id_t group_id, nmo_object_id_t object_id);
/* Deleting an object: groups drop it and object cells that held it hold none */
void eng_forget_object(nmo_engine_t *engine, nmo_object_id_t id);
size_t eng_type_size(const nmo_engine_t *engine, nmo_guid_t type);

#endif /* NMO_ENGINE_ARRAY_H */
