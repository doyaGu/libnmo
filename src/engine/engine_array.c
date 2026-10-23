/**
 * @file engine_array.c
 * @brief Runtime copies of the document's data arrays, and its groups.
 *
 * Follows CKDataArray: an INT, FLOAT or OBJECT element holds 32 bits, a
 * STRING element its own string, and a PARAMETER element a parameter of the
 * column's type, here its value bytes.
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

size_t eng_type_size(const nmo_engine_t *engine, nmo_guid_t type)
{
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(engine->types, type);
    return desc != NULL ? desc->size : 0u;
}

static char *eng_strdup(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length + 1u);
    }
    return copy;
}

static void eng_cell_clear(eng_cell_t *cell)
{
    free(cell->text);
    free(cell->data);
    memset(cell, 0, sizeof(*cell));
}

/* An empty cell of `column`: zero, no string, or a zeroed value of the parameter type. */
static bool eng_cell_init(const nmo_engine_t *engine, const eng_column_t *column, eng_cell_t *cell)
{
    memset(cell, 0, sizeof(*cell));
    if (column->type != CKARRAYTYPE_PARAMETER) {
        return true;
    }
    size_t size = eng_type_size(engine, column->param_type);
    if (size > 0) {
        cell->data = (uint8_t *)calloc(1, size);
        if (cell->data == NULL) {
            return false;
        }
        cell->size = (uint32_t)size;
    }
    return true;
}

static bool eng_array_reserve(eng_array_t *array, size_t rows)
{
    if (rows <= array->row_capacity || array->column_count == 0) {
        return true;
    }
    size_t capacity = array->row_capacity ? array->row_capacity * 2u : 8u;
    if (capacity < rows) {
        capacity = rows;
    }
    eng_cell_t *cells =
        (eng_cell_t *)realloc(array->cells, capacity * array->column_count * sizeof(eng_cell_t));
    if (cells == NULL) {
        return false;
    }
    array->cells = cells;
    array->row_capacity = capacity;
    return true;
}

static inline eng_cell_t *eng_array_cell(const eng_array_t *array, size_t row, size_t column)
{
    return &array->cells[row * array->column_count + column];
}

/* A cell's value from the file. */
static bool eng_cell_load(nmo_engine_t *engine, nmo_object_repository_t *repo,
                          const eng_column_t *column, const nmo_dataarray_cell_t *saved,
                          eng_cell_t *cell)
{
    if (!eng_cell_init(engine, column, cell)) {
        return false;
    }
    switch (column->type) {
    case CKARRAYTYPE_INT:
        cell->value = (uint32_t)saved->int_value;
        return true;
    case CKARRAYTYPE_FLOAT:
        memcpy(&cell->value, &saved->float_value, sizeof(cell->value));
        return true;
    case CKARRAYTYPE_OBJECT:
        cell->value = (uint32_t)nmo_ref_runtime_id(&saved->object_ref);
        return true;
    case CKARRAYTYPE_STRING:
        cell->text = eng_strdup(saved->string_value);
        return saved->string_value == NULL || cell->text != NULL;
    case CKARRAYTYPE_PARAMETER: {
        nmo_object_id_t id = nmo_ref_runtime_id(&saved->parameter.ref);
        nmo_object_t *object = id != 0 ? nmo_object_repository_find_by_id(repo, id) : NULL;
        nmo_class_id_t class_id = object != NULL ? nmo_object_get_class_id(object) : 0;
        if (class_id != NMO_CID_PARAMETEROUT && class_id != NMO_CID_PARAMETER &&
            class_id != NMO_CID_PARAMETERLOCAL) {
            return true; /* no parameter object (a sub-chunk): the zeroed value */
        }
        uint8_t *data = NULL;
        uint32_t size = 0;
        if (!eng_parameter_state_bytes((const nmo_parameter_state_t *)nmo_object_get_state(object),
                                       cell->size, &data, &size)) {
            return false;
        }
        free(cell->data);
        cell->data = data;
        cell->size = size;
        return true;
    }
    default:
        return true;
    }
}

static nmo_status_t eng_load_array(nmo_engine_t *engine, nmo_object_repository_t *repo,
                                   nmo_object_id_t id, const nmo_dataarray_state_t *state)
{
    eng_array_t *array = NULL;
    if (!ENG_VEC_PUSH(engine->arrays, array) ||
        !eng_map_put(&engine->array_map, id, (int32_t)(engine->arrays.count - 1u))) {
        return NMO_ERR_NOMEM;
    }
    array->id = id;
    array->key_column = state->key_column;
    array->column_count = state->column_count;
    if (array->column_count == 0) {
        return NMO_OK;
    }
    array->columns = (eng_column_t *)calloc(array->column_count, sizeof(eng_column_t));
    if (array->columns == NULL || !eng_array_reserve(array, state->row_count)) {
        return NMO_ERR_NOMEM;
    }
    for (uint32_t c = 0; c < array->column_count; c++) {
        array->columns[c].type = (int32_t)state->column_formats[c].type;
        array->columns[c].param_type = state->column_formats[c].parameter_type_guid;
    }
    for (uint32_t r = 0; r < state->row_count; r++) {
        const nmo_dataarray_row_t *row = &state->rows[r];
        for (uint32_t c = 0; c < array->column_count; c++) {
            eng_cell_t *cell = eng_array_cell(array, r, c);
            bool ok = c < row->column_count
                          ? eng_cell_load(engine, repo, &array->columns[c], &row->cells[c], cell)
                          : eng_cell_init(engine, &array->columns[c], cell);
            if (!ok) {
                array->row_count = r + 1u; /* the cells so far are freed with the array */
                return NMO_ERR_NOMEM;
            }
        }
        array->row_count = r + 1u;
    }
    return NMO_OK;
}

nmo_status_t eng_load_arrays(nmo_engine_t *engine)
{
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    size_t count = repo != NULL ? nmo_object_repository_get_count(repo) : 0u;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_DATAARRAY) {
            continue;
        }
        const nmo_dataarray_state_t *state =
            (const nmo_dataarray_state_t *)nmo_object_get_state(object);
        if (state == NULL) {
            continue;
        }
        nmo_status_t status = eng_load_array(engine, repo, nmo_object_get_id(object), state);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}

void eng_free_arrays(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->arrays.count; i++) {
        eng_array_t *array = &engine->arrays.data[i];
        for (size_t c = 0; c < array->row_count * array->column_count; c++) {
            eng_cell_clear(&array->cells[c]);
        }
        free(array->cells);
        free(array->columns);
    }
    free(engine->arrays.data);
    free(engine->array_map.slots);
}

eng_array_t *eng_find_array(const nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = engine != NULL ? eng_map_get(&engine->array_map, id) : -1;
    return index >= 0 ? &engine->arrays.data[index] : NULL;
}

/* ============================================================================
 * Public access
 * ============================================================================ */

int32_t nmo_engine_array_row_count(const nmo_engine_t *engine, nmo_object_id_t array_id)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    return array != NULL ? (int32_t)array->row_count : -1;
}

int32_t nmo_engine_array_column_count(const nmo_engine_t *engine, nmo_object_id_t array_id)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    return array != NULL ? (int32_t)array->column_count : -1;
}

int32_t nmo_engine_array_key_column(const nmo_engine_t *engine, nmo_object_id_t array_id)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    return array != NULL ? array->key_column : -1;
}

int32_t nmo_engine_array_column_type(const nmo_engine_t *engine, nmo_object_id_t array_id,
                                     int32_t column)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL || column < 0 || (uint32_t)column >= array->column_count) {
        return 0;
    }
    return array->columns[column].type;
}

static eng_cell_t *eng_lookup_cell(const nmo_engine_t *engine, nmo_object_id_t array_id,
                                   int32_t row, int32_t column, const eng_array_t **out_array)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL || row < 0 || (size_t)row >= array->row_count || column < 0 ||
        (uint32_t)column >= array->column_count) {
        return NULL;
    }
    if (out_array != NULL) {
        *out_array = array;
    }
    return eng_array_cell(array, (size_t)row, (size_t)column);
}

nmo_status_t nmo_engine_array_get_cell(const nmo_engine_t *engine, nmo_object_id_t array_id,
                                       int32_t row, int32_t column, nmo_engine_cell_t *out_cell)
{
    if (out_cell == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(out_cell, 0, sizeof(*out_cell));
    const eng_array_t *array = NULL;
    const eng_cell_t *cell = eng_lookup_cell(engine, array_id, row, column, &array);
    if (cell == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    out_cell->type = array->columns[column].type;
    out_cell->value = cell->value;
    out_cell->text = cell->text;
    out_cell->data = cell->data;
    out_cell->size = cell->size;
    out_cell->parameter_type = array->columns[column].param_type;
    return NMO_OK;
}

nmo_status_t nmo_engine_array_set_cell(nmo_engine_t *engine, nmo_object_id_t array_id,
                                       int32_t row, int32_t column, const nmo_engine_cell_t *value)
{
    if (value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const eng_array_t *array = NULL;
    eng_cell_t *cell = eng_lookup_cell(engine, array_id, row, column, &array);
    if (cell == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    switch (array->columns[column].type) {
    case CKARRAYTYPE_STRING: {
        char *text = eng_strdup(value->text);
        if (value->text != NULL && text == NULL) {
            return NMO_ERR_NOMEM;
        }
        free(cell->text);
        cell->text = text;
        return NMO_OK;
    }
    case CKARRAYTYPE_PARAMETER: {
        if (value->data == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        uint8_t *data = value->size > 0 ? (uint8_t *)malloc(value->size) : NULL;
        if (value->size > 0 && data == NULL) {
            return NMO_ERR_NOMEM;
        }
        if (value->size > 0) {
            memcpy(data, value->data, value->size);
        }
        free(cell->data);
        cell->data = data;
        cell->size = (uint32_t)value->size;
        return NMO_OK;
    }
    default:
        cell->value = value->value;
        return NMO_OK;
    }
}

nmo_status_t nmo_engine_array_insert_row(nmo_engine_t *engine, nmo_object_id_t array_id,
                                         int32_t row, int32_t *out_row)
{
    eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (row < -1 || (row >= 0 && (size_t)row > array->row_count)) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }
    size_t at = row < 0 ? array->row_count : (size_t)row;
    if (!eng_array_reserve(array, array->row_count + 1u)) {
        return NMO_ERR_NOMEM;
    }
    if (array->column_count > 0) {
        memmove(eng_array_cell(array, at + 1u, 0), eng_array_cell(array, at, 0),
                (array->row_count - at) * array->column_count * sizeof(eng_cell_t));
        for (uint32_t c = 0; c < array->column_count; c++) {
            if (!eng_cell_init(engine, &array->columns[c], eng_array_cell(array, at, c))) {
                /* Close the gap again; the cells initialized so far are freed */
                for (uint32_t k = 0; k < c; k++) {
                    eng_cell_clear(eng_array_cell(array, at, k));
                }
                memmove(eng_array_cell(array, at, 0), eng_array_cell(array, at + 1u, 0),
                        (array->row_count - at) * array->column_count * sizeof(eng_cell_t));
                return NMO_ERR_NOMEM;
            }
        }
    }
    array->row_count++;
    if (out_row != NULL) {
        *out_row = (int32_t)at;
    }
    return NMO_OK;
}

/* OpCompare: `cmp` is the sign of a compared with b. */
static bool eng_op_holds(int32_t op, int cmp)
{
    switch (op) {
    case 1: return cmp == 0;  /* CKEQUAL */
    case 2: return cmp != 0;  /* CKNOTEQUAL */
    case 3: return cmp < 0;   /* CKLESSER */
    case 4: return cmp <= 0;  /* CKLESSEREQUAL */
    case 5: return cmp > 0;   /* CKGREATER */
    case 6: return cmp >= 0;  /* CKGREATEREQUAL */
    default: return false;
    }
}

/* The equal functions of CKDataArray's columns (ArrayIntEqual, ...) under `op`. */
static bool eng_cell_matches(int32_t type, const eng_cell_t *cell, int32_t op,
                             const nmo_engine_cell_t *key)
{
    switch (type) {
    case CKARRAYTYPE_FLOAT: {
        float a, b;
        memcpy(&a, &cell->value, sizeof(a));
        memcpy(&b, &key->value, sizeof(b));
        if (a != a || b != b) {
            return op == 2; /* NaN: only "not equal" holds, as with the operators */
        }
        return eng_op_holds(op, (a > b) - (a < b));
    }
    case CKARRAYTYPE_STRING: {
        int cmp = strcmp(cell->text != NULL ? cell->text : "", key->text != NULL ? key->text : "");
        return eng_op_holds(op, (cmp > 0) - (cmp < 0));
    }
    case CKARRAYTYPE_PARAMETER: {
        size_t size = cell->size < key->size ? cell->size : key->size;
        if (size == 0 || cell->data == NULL || key->data == NULL) {
            return false;
        }
        int cmp = memcmp(cell->data, key->data, size);
        return eng_op_holds(op, (cmp > 0) - (cmp < 0));
    }
    default: {
        int32_t a = (int32_t)cell->value, b = (int32_t)key->value;
        return eng_op_holds(op, (a > b) - (a < b));
    }
    }
}

int32_t nmo_engine_array_find_row_op(const nmo_engine_t *engine, nmo_object_id_t array_id,
                                     int32_t column, int32_t op, const nmo_engine_cell_t *key,
                                     int32_t start)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL || key == NULL || column < 0 || (uint32_t)column >= array->column_count ||
        start < 0) {
        return -1;
    }
    for (size_t r = (size_t)start; r < array->row_count; r++) {
        if (eng_cell_matches(array->columns[column].type, eng_array_cell(array, r, column), op,
                             key)) {
            return (int32_t)r;
        }
    }
    return -1;
}

/* The sort functions of CKDataArray's columns (ArrayIntComp, ...), ascending. */
static int eng_cell_compare(int32_t type, const eng_cell_t *a, const eng_cell_t *b)
{
    switch (type) {
    case CKARRAYTYPE_FLOAT: {
        float x, y;
        memcpy(&x, &a->value, sizeof(x));
        memcpy(&y, &b->value, sizeof(y));
        return (x > y) - (x < y);
    }
    case CKARRAYTYPE_STRING: {
        int cmp = strcmp(a->text != NULL ? a->text : "", b->text != NULL ? b->text : "");
        return (cmp > 0) - (cmp < 0);
    }
    case CKARRAYTYPE_PARAMETER: {
        size_t size = a->size < b->size ? a->size : b->size;
        if (a->data == NULL || b->data == NULL || size == 0) {
            return (a->data != NULL) - (b->data != NULL);
        }
        int cmp = memcmp(a->data, b->data, size);
        return (cmp > 0) - (cmp < 0);
    }
    default: {
        int32_t x = (int32_t)a->value, y = (int32_t)b->value;
        return (x > y) - (x < y);
    }
    }
}

int32_t nmo_engine_array_highest_row(const nmo_engine_t *engine, nmo_object_id_t array_id,
                                     int32_t column)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL || column < 0 || (uint32_t)column >= array->column_count ||
        array->row_count == 0) {
        return -1;
    }
    int32_t type = array->columns[column].type;
    size_t highest = 0;
    for (size_t r = 1; r < array->row_count; r++) {
        if (eng_cell_compare(type, eng_array_cell(array, r, column),
                             eng_array_cell(array, highest, column)) > 0) {
            highest = r;
        }
    }
    return (int32_t)highest;
}

int32_t nmo_engine_array_find_row(const nmo_engine_t *engine, nmo_object_id_t array_id,
                                  int32_t column, const nmo_engine_cell_t *key, int32_t start)
{
    return nmo_engine_array_find_row_op(engine, array_id, column, 1 /* CKEQUAL */, key, start);
}

nmo_status_t nmo_engine_array_remove_row(nmo_engine_t *engine, nmo_object_id_t array_id,
                                         int32_t row)
{
    eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (row < 0 || (size_t)row >= array->row_count) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }
    for (uint32_t c = 0; c < array->column_count; c++) {
        eng_cell_clear(eng_array_cell(array, (size_t)row, c));
    }
    if (array->column_count > 0) {
        memmove(eng_array_cell(array, (size_t)row, 0), eng_array_cell(array, (size_t)row + 1u, 0),
                (array->row_count - (size_t)row - 1u) * array->column_count * sizeof(eng_cell_t));
    }
    array->row_count--;
    return NMO_OK;
}

nmo_status_t nmo_engine_array_clear(nmo_engine_t *engine, nmo_object_id_t array_id)
{
    eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    for (size_t c = 0; c < array->row_count * array->column_count; c++) {
        eng_cell_clear(&array->cells[c]);
    }
    array->row_count = 0;
    return NMO_OK;
}

nmo_status_t nmo_engine_array_swap_rows(nmo_engine_t *engine, nmo_object_id_t array_id,
                                        int32_t a, int32_t b)
{
    eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (a < 0 || b < 0 || (size_t)a >= array->row_count || (size_t)b >= array->row_count) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }
    for (uint32_t c = 0; a != b && c < array->column_count; c++) {
        eng_cell_t cell = *eng_array_cell(array, (size_t)a, c);
        *eng_array_cell(array, (size_t)a, c) = *eng_array_cell(array, (size_t)b, c);
        *eng_array_cell(array, (size_t)b, c) = cell;
    }
    return NMO_OK;
}

uint32_t nmo_engine_array_sum(const nmo_engine_t *engine, nmo_object_id_t array_id,
                              int32_t column)
{
    const eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL || column < 0 || (uint32_t)column >= array->column_count) {
        return 0u;
    }
    int32_t type = array->columns[column].type;
    if (type == CKARRAYTYPE_INT) {
        uint32_t sum = 0u;
        for (size_t r = 0; r < array->row_count; r++) {
            sum += eng_array_cell(array, r, (size_t)column)->value;
        }
        return sum;
    }
    if (type != CKARRAYTYPE_FLOAT) {
        return 0u;
    }
    float sum = 0.0f;
    for (size_t r = 0; r < array->row_count; r++) {
        float value;
        memcpy(&value, &eng_array_cell(array, r, (size_t)column)->value, sizeof(value));
        sum += value;
    }
    uint32_t bits;
    memcpy(&bits, &sum, sizeof(bits));
    return bits;
}

/* The cells laid out again for `columns` columns; `map[c]` is the old column of new column c, or -1. */
static bool eng_array_relayout(nmo_engine_t *engine, eng_array_t *array, eng_column_t *columns,
                               uint32_t column_count, const int32_t *map)
{
    size_t rows = array->row_count > array->row_capacity ? array->row_count : array->row_capacity;
    eng_cell_t *cells = NULL;
    if (column_count > 0 && rows > 0) {
        cells = (eng_cell_t *)calloc(rows * column_count, sizeof(eng_cell_t));
        if (cells == NULL) {
            return false;
        }
    }
    for (size_t r = 0; r < array->row_count; r++) {
        for (uint32_t c = 0; c < column_count; c++) {
            eng_cell_t *cell = &cells[r * column_count + c];
            if (map[c] >= 0) {
                *cell = *eng_array_cell(array, r, (size_t)map[c]);
            } else if (!eng_cell_init(engine, &columns[c], cell)) {
                for (size_t k = 0; k < r * column_count + c; k++) {
                    if (map[k % column_count] < 0) {
                        eng_cell_clear(&cells[k]);
                    }
                }
                free(cells);
                return false;
            }
        }
    }
    free(array->cells);
    free(array->columns);
    array->cells = cells;
    array->columns = columns;
    array->column_count = column_count;
    array->row_capacity = cells != NULL ? rows : 0u;
    return true;
}

nmo_status_t nmo_engine_array_insert_column(nmo_engine_t *engine, nmo_object_id_t array_id,
                                            int32_t column, int32_t type, nmo_guid_t param_type)
{
    eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    /* CKDataArray::InsertColumn: -1 appends; past the last column, nothing */
    if (column >= (int32_t)array->column_count || column < -1) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }
    uint32_t count = array->column_count + 1u;
    uint32_t at = column < 0 ? array->column_count : (uint32_t)column;
    eng_column_t *columns = (eng_column_t *)calloc(count, sizeof(eng_column_t));
    int32_t *map = (int32_t *)malloc(count * sizeof(int32_t));
    if (columns == NULL || map == NULL) {
        free(columns);
        free(map);
        return NMO_ERR_NOMEM;
    }
    for (uint32_t c = 0; c < count; c++) {
        if (c == at) {
            columns[c].type = type;
            columns[c].param_type = type == CKARRAYTYPE_PARAMETER ? param_type : NMO_GUID(0u, 0u);
            map[c] = -1;
        } else {
            uint32_t old = c < at ? c : c - 1u;
            columns[c] = array->columns[old];
            map[c] = (int32_t)old;
        }
    }
    bool ok = eng_array_relayout(engine, array, columns, count, map);
    free(map);
    if (!ok) {
        free(columns);
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

nmo_status_t nmo_engine_array_remove_column(nmo_engine_t *engine, nmo_object_id_t array_id,
                                            int32_t column)
{
    eng_array_t *array = eng_find_array(engine, array_id);
    if (array == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (column < 0 || (uint32_t)column >= array->column_count) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }
    uint32_t count = array->column_count - 1u;
    eng_column_t *columns = count > 0 ? (eng_column_t *)calloc(count, sizeof(eng_column_t)) : NULL;
    int32_t *map = count > 0 ? (int32_t *)malloc(count * sizeof(int32_t)) : NULL;
    if (count > 0 && (columns == NULL || map == NULL)) {
        free(columns);
        free(map);
        return NMO_ERR_NOMEM;
    }
    for (uint32_t c = 0; c < count; c++) {
        uint32_t old = c < (uint32_t)column ? c : c + 1u;
        columns[c] = array->columns[old];
        map[c] = (int32_t)old;
    }
    for (size_t r = 0; r < array->row_count; r++) {
        eng_cell_clear(eng_array_cell(array, r, (size_t)column));
    }
    bool ok = eng_array_relayout(engine, array, columns, count, map);
    free(map);
    if (!ok) {
        free(columns);
        return NMO_ERR_NOMEM;
    }
    /* The key column follows its column; removing the key column leaves the index */
    if (array->key_column > column) {
        array->key_column--;
    }
    return NMO_OK;
}

/* ============================================================================
 * Groups
 * ============================================================================ */

static const nmo_group_state_t *eng_group_state(const nmo_engine_t *engine, nmo_object_id_t id)
{
    if (engine == NULL || id == 0) {
        return NULL;
    }
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    nmo_object_t *object = repo != NULL ? nmo_object_repository_find_by_id(repo, id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_GROUP) {
        return NULL;
    }
    return (const nmo_group_state_t *)nmo_object_get_state(object);
}

int32_t nmo_engine_group_count(const nmo_engine_t *engine, nmo_object_id_t group_id)
{
    const nmo_group_state_t *group = eng_group_state(engine, group_id);
    return group != NULL ? (int32_t)group->object_ids.count : -1;
}

nmo_object_id_t nmo_engine_group_object(const nmo_engine_t *engine, nmo_object_id_t group_id,
                                        int32_t index)
{
    const nmo_group_state_t *group = eng_group_state(engine, group_id);
    if (group == NULL || index < 0 || (size_t)index >= group->object_ids.count) {
        return 0;
    }
    return nmo_ref_runtime_id(&((const nmo_ref_t *)group->object_ids.data)[index]);
}
