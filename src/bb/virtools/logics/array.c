/**
 * @file array.c
 * @brief Ports of the Logics/Array building blocks, on the engine's data
 *        arrays.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../virtools.h"

#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Logics/Array ---- */

static nmo_class_id_t type_class(blk_t *b, nmo_guid_t type)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(engine->types, type);
    return desc != NULL ? (nmo_class_id_t)desc->object_class_id : 0;
}

/* WriteOutputParameters: row `row` into the outputs from `first`, where types agree. */
static void array_write_outputs(blk_t *b, nmo_object_id_t array, int32_t row, int first)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    for (int32_t c = 0; c < columns; c++) {
        nmo_guid_t out = nmo_engine_block_output_parameter_type(b, first + c);
        nmo_engine_cell_t cell;
        if (nmo_guid_is_null(out) ||
            nmo_engine_array_get_cell(engine, array, row, c, &cell) != NMO_OK) {
            continue;
        }
        switch (cell.type) {
        case CKARRAYTYPE_INT:
            if (nmo_guid_equals(out, CKPGUID_INT)) {
                nmo_engine_block_set_output_value(b, first + c, &cell.value, sizeof(cell.value));
            }
            break;
        case CKARRAYTYPE_FLOAT:
            if (nmo_guid_equals(out, CKPGUID_FLOAT)) {
                nmo_engine_block_set_output_value(b, first + c, &cell.value, sizeof(cell.value));
            }
            break;
        case CKARRAYTYPE_STRING:
            if (nmo_guid_equals(out, CKPGUID_STRING)) {
                const char *text = cell.text != NULL ? cell.text : "";
                nmo_engine_block_set_output_value(b, first + c, text, strlen(text) + 1u);
            }
            break;
        case CKARRAYTYPE_OBJECT: {
            nmo_class_id_t cid = type_class(b, out);
            if (cid != 0) {
                uint32_t id = eng_is_child_class(engine, eng_object_class(engine, cell.value), cid)
                                  ? cell.value
                                  : 0u;
                nmo_engine_block_set_output_value(b, first + c, &id, sizeof(id));
            }
            break;
        }
        case CKARRAYTYPE_PARAMETER:
            /* CKParameter::CopyValue: when the types are compatible */
            if (cell.data != NULL && (type_derives(b, cell.parameter_type, out) ||
                                      type_derives(b, out, cell.parameter_type))) {
                nmo_engine_block_set_output_value(b, first + c, cell.data, cell.size);
            }
            break;
        default:
            break;
        }
    }
}

/* ReadInputParameters: the inputs from `first` into row `row`, where types agree. */
static void array_read_inputs(blk_t *b, nmo_object_id_t array, int32_t row, int first)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    for (int32_t c = 0; c < columns; c++) {
        nmo_guid_t in = nmo_engine_block_input_parameter_type(b, first + c);
        size_t size = 0;
        const void *data = nmo_engine_block_input_data(b, first + c, &size);
        if (nmo_guid_is_null(in) || data == NULL) {
            continue;
        }
        nmo_engine_cell_t cell = {.type = nmo_engine_array_column_type(engine, array, c)};
        switch (cell.type) {
        case CKARRAYTYPE_INT:
        case CKARRAYTYPE_FLOAT:
        case CKARRAYTYPE_OBJECT: {
            nmo_guid_t expected = cell.type == CKARRAYTYPE_INT     ? CKPGUID_INT
                                  : cell.type == CKARRAYTYPE_FLOAT ? CKPGUID_FLOAT
                                                                   : CKPGUID_BEOBJECT;
            if (!nmo_guid_equals(in, expected)) {
                continue;
            }
            memcpy(&cell.value, data, size < sizeof(cell.value) ? size : sizeof(cell.value));
            break;
        }
        case CKARRAYTYPE_STRING:
            if (!nmo_guid_equals(in, CKPGUID_STRING)) {
                continue;
            }
            cell.text = input_string(b, first + c);
            break;
        case CKARRAYTYPE_PARAMETER: {
            nmo_engine_cell_t current;
            if (nmo_engine_array_get_cell(engine, array, row, c, &current) != NMO_OK ||
                !nmo_guid_equals(in, current.parameter_type)) {
                continue;
            }
            cell.data = data;
            cell.size = size;
            break;
        }
        default:
            continue;
        }
        nmo_engine_array_set_cell(engine, array, row, c, &cell);
    }
}

/*
 * GetInputValue: input `pos` as a key for `column`; false when its type is not
 * the column's, and the key then is a zero (as the CK code leaves it).
 */
static void array_input_key(blk_t *b, nmo_object_id_t array, int32_t column, int pos,
                            nmo_engine_cell_t *key)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    memset(key, 0, sizeof(*key));
    key->type = nmo_engine_array_column_type(engine, array, column);
    nmo_guid_t in = nmo_engine_block_input_parameter_type(b, pos);
    switch (key->type) {
    case CKARRAYTYPE_INT:
        if (nmo_guid_equals(in, CKPGUID_INT)) {
            key->value = (uint32_t)get_in_int(b, pos, 0);
        }
        break;
    case CKARRAYTYPE_FLOAT:
        if (nmo_guid_equals(in, CKPGUID_FLOAT)) {
            nmo_engine_block_get_input_value(b, pos, &key->value, sizeof(key->value));
        }
        break;
    case CKARRAYTYPE_STRING:
        if (nmo_guid_equals(in, CKPGUID_STRING)) {
            key->text = input_string(b, pos);
        }
        break;
    case CKARRAYTYPE_OBJECT:
        if (nmo_guid_equals(in, CKPGUID_BEOBJECT)) {
            key->value = (uint32_t)get_in_int(b, pos, 0);
        }
        break;
    case CKARRAYTYPE_PARAMETER: {
        nmo_engine_cell_t first;
        if (nmo_engine_array_get_cell(engine, array, 0, column, &first) == NMO_OK &&
            nmo_guid_equals(in, first.parameter_type)) {
            key->data = nmo_engine_block_input_data(b, pos, &key->size);
        }
        break;
    }
    default:
        break;
    }
}

/* The data array a block works on (its target); 0 when the target is not one. */
static nmo_object_id_t array_target(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    return nmo_engine_array_row_count(nmo_engine_block_engine(b), target) >= 0 ? target : 0;
}

/* Get Cell (ArrayGetElement) */
static int bb_get_cell(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0), column = get_in_int(b, 1, 0);
    nmo_engine_cell_t cell;
    IN_OFF(b, 0);
    if (nmo_engine_array_get_cell(engine, array, row, column, &cell) != NMO_OK) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    /* The output written as a one-column row */
    nmo_guid_t out = nmo_engine_block_output_parameter_type(b, 0);
    switch (cell.type) {
    case CKARRAYTYPE_INT:
    case CKARRAYTYPE_FLOAT:
        if (nmo_guid_equals(out, cell.type == CKARRAYTYPE_INT ? CKPGUID_INT : CKPGUID_FLOAT)) {
            nmo_engine_block_set_output_value(b, 0, &cell.value, sizeof(cell.value));
        }
        break;
    case CKARRAYTYPE_STRING:
        if (nmo_guid_equals(out, CKPGUID_STRING)) {
            const char *text = cell.text != NULL ? cell.text : "";
            nmo_engine_block_set_output_value(b, 0, text, strlen(text) + 1u);
        }
        break;
    case CKARRAYTYPE_OBJECT: {
        nmo_class_id_t cid = type_class(b, out);
        if (cid != 0) {
            uint32_t id = eng_is_child_class(engine, eng_object_class(engine, cell.value), cid)
                              ? cell.value
                              : 0u;
            nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
        }
        break;
    }
    case CKARRAYTYPE_PARAMETER:
        if (cell.data != NULL && (type_derives(b, cell.parameter_type, out) ||
                                  type_derives(b, out, cell.parameter_type))) {
            nmo_engine_block_set_output_value(b, 0, cell.data, cell.size);
        }
        break;
    default:
        break;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* A parameter value as text (GetStringValue), for the types a string column takes. */
static char *value_text(blk_t *b, nmo_guid_t type, const void *data, size_t size)
{
    if (type_derives(b, type, CKPGUID_STRING)) {
        if (memchr(data, '\0', size) == NULL) {
            return NULL;
        }
        size_t length = strlen((const char *)data);
        char *copy = (char *)malloc(length + 1u);
        if (copy != NULL) {
            memcpy(copy, data, length + 1u);
        }
        return copy;
    }
    if (size < 4) {
        return NULL;
    }
    int32_t i = 0;
    float f = 0.0f;
    memcpy(&i, data, sizeof(i));
    memcpy(&f, data, sizeof(f));
    bool is_float = type_derives(b, type, CKPGUID_FLOAT);
    if (!is_float && !type_derives(b, type, CKPGUID_INT)) {
        return NULL;
    }
    int length = is_float ? snprintf(NULL, 0, "%g", f) : snprintf(NULL, 0, "%d", i);
    char *text = length >= 0 ? (char *)malloc((size_t)length + 1u) : NULL;
    if (text != NULL) {
        if (is_float) {
            snprintf(text, (size_t)length + 1u, "%g", f);
        } else {
            snprintf(text, (size_t)length + 1u, "%d", i);
        }
    }
    return text;
}

/* Set Cell (ArraySetElement, CKDataArray::SetElementValueFromParameter) */
static int bb_set_cell(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0), column = get_in_int(b, 1, 0);
    size_t size = 0;
    const void *data = nmo_engine_block_input_data(b, 2, &size);
    nmo_engine_cell_t current;
    IN_OFF(b, 0);
    if (data == NULL || nmo_engine_array_get_cell(engine, array, row, column, &current) != NMO_OK) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_cell_t cell = {.type = current.type};
    char *text = NULL;
    switch (current.type) {
    case CKARRAYTYPE_STRING:
        text = value_text(b, nmo_engine_block_input_parameter_type(b, 2), data, size);
        cell.text = text;
        nmo_engine_array_set_cell(engine, array, row, column, &cell);
        free(text);
        break;
    case CKARRAYTYPE_PARAMETER:
        if (type_derives(b, nmo_engine_block_input_parameter_type(b, 2), current.parameter_type) ||
            type_derives(b, current.parameter_type, nmo_engine_block_input_parameter_type(b, 2))) {
            cell.data = data;
            cell.size = size;
            nmo_engine_array_set_cell(engine, array, row, column, &cell);
        }
        break;
    default:
        memcpy(&cell.value, data, size < sizeof(cell.value) ? size : sizeof(cell.value));
        nmo_engine_array_set_cell(engine, array, row, column, &cell);
        break;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Get Row (ArrayGetLine) */
static int bb_get_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t row = get_in_int(b, 0, 0);
    IN_OFF(b, 0);
    if (row < 0 || row >= nmo_engine_array_row_count(nmo_engine_block_engine(b), array)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Row (ArraySetLine) */
static int bb_set_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0);
    int32_t unique = 1;
    nmo_engine_block_get_local_value(b, 0, &unique, sizeof(unique));
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (unique && key_column >= 0) {
        nmo_engine_cell_t key;
        array_input_key(b, array, key_column, key_column + 1, &key);
        int32_t found = nmo_engine_array_find_row(engine, array, key_column, &key, 0);
        if (found != -1 && found != row) {
            IN_OFF(b, 0);
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
    }
    if (row >= 0 && row < nmo_engine_array_row_count(engine, array)) {
        array_read_inputs(b, array, row, 1);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Add Row (AddArray) */
static int bb_add_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t unique = 1;
    nmo_engine_block_get_local_value(b, 0, &unique, sizeof(unique));
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (unique && key_column >= 0) {
        nmo_engine_cell_t key;
        array_input_key(b, array, key_column, key_column, &key);
        if (nmo_engine_array_find_row(engine, array, key_column, &key, 0) != -1) {
            IN_OFF(b, 0);
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
    }
    int32_t row = -1;
    if (nmo_engine_array_insert_row(engine, array, -1, &row) == NMO_OK) {
        array_read_inputs(b, array, row, 0);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Iterator (ArrayIterator) */
static int bb_array_iterator(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t count = nmo_engine_array_row_count(nmo_engine_block_engine(b), array);
    int32_t starting = get_in_int(b, 0, 0);
    if (starting >= count) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    int32_t ending = get_in_int(b, 1, -1);
    if (ending == -1) {
        ending = count - 1;
    }
    bool backward = get_in_int(b, 2, 0) != 0;
    int32_t index;
    bool first = false;
    if (IN_ACTIVE(b, 0)) {
        first = true;
        IN_OFF(b, 0);
        index = backward ? ending : starting;
        set_out_int(b, 0, index);
    } else {
        IN_OFF(b, 1);
        index = get_out_int(b, 0) + (backward ? -1 : 1);
        if (index <= -1) {
            index = count - 1;
        } else if (index >= count) {
            index = 0;
        }
        set_out_int(b, 0, index);
    }
    if (backward) {
        ending = starting - 1;
        if (ending == -1) {
            ending = count - 1;
        }
    } else if (++ending >= count) {
        ending = 0;
    }
    if ((!first || count == 0) && index == ending) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, index, 1);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* Clear Array (ArrayClear) */
static int bb_clear_array(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_array_clear(nmo_engine_block_engine(b), array);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Remove Key Row (RemoveArray): the row whose key equals the input. */
static int bb_remove_key_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (nmo_engine_block_input_parameter_count(b) < 1 || key_column < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_engine_cell_t key;
    array_input_key(b, array, key_column, 0, &key);
    int32_t row = nmo_engine_array_find_row(engine, array, key_column, &key, 0);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_remove_row(engine, array, row);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Iterator If (ArrayIteratorIf): the rows whose cell in a column passes a test. */
/* Get Key Row (GetArray): the row whose key column holds the key, into the outputs. */
static int bb_get_key_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (key_column < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "No column criterion defined" */
    }
    nmo_engine_cell_t key;
    array_input_key(b, array, key_column, 0, &key);
    int32_t row = nmo_engine_array_find_row(engine, array, key_column, &key, 0);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Row Search (ArraySearch): the first row from First Search Row whose cell compares true. */
static int bb_row_search(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t column = get_in_int(b, 0, 0);
    int32_t first = get_in_int(b, 1, 0);
    int32_t op = get_in_int(b, 2, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 3, &key);
    int32_t row = nmo_engine_array_find_row_op(engine, array, column, op, &key, first);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    set_out_int(b, 0, row);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Shuffle Order (CKDataArray::RandomShuffle): each row swapped with a random one. */
static int bb_shuffle_order(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t rows = nmo_engine_array_row_count(engine, array);
    for (int32_t i = 0; i < rows; i++) {
        int32_t r = (int32_t)(nmo_engine_block_random(b) * 32768.0); /* rand() */
        nmo_engine_array_swap_rows(engine, array, i, r % rows);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Remove Row If (ArrayRemoveIf): every row whose cell compares true, and how many. */
static int bb_remove_row_if(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t column = get_in_int(b, 0, 0);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 2, &key);
    int32_t count = 0;
    for (int32_t i = 0; i != -1;) {
        i = nmo_engine_array_find_row_op(engine, array, column, op, &key, i);
        if (i != -1) {
            nmo_engine_array_remove_row(engine, array, i);
            count++;
            if (i >= nmo_engine_array_row_count(engine, array)) {
                break;
            }
        }
    }
    set_out_int(b, 0, count);
    IN_OFF(b, 0);
    OUT_ON(b, count == 0 ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

/* Value Count (ArrayValueCount): how many cells of a column compare true. */
static int bb_value_count(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t column = get_in_int(b, 0, 1);
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 2, &key);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    set_out_int(b, 0, nmo_engine_array_count(nmo_engine_block_engine(b), array, column, op, &key));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Test Cell (ArrayTestRow): True when the cell compares true. In stays active, as in CK. */
static int bb_test_cell(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t row = get_in_int(b, 0, 0);
    int32_t column = get_in_int(b, 1, 0);
    int32_t op = get_in_int(b, 2, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 3, &key);
    bool holds = nmo_engine_array_test_row(nmo_engine_block_engine(b), array, row, column, op, &key);
    OUT_ON(b, holds ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* Sort Rows (ArraySort): the rows by a column. */
static int bb_sort_rows(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_array_sort(nmo_engine_block_engine(b), array, get_in_int(b, 0, 0),
                          get_in_int(b, 1, 1) != 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Remove Row (ArrayRemoveLine): Removed, or Not Present for a row it does not have. */
static int bb_remove_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0);
    IN_OFF(b, 0);
    if (row < 0 || row >= nmo_engine_array_row_count(engine, array)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_remove_row(engine, array, row);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Change Value If (ArrayChangeValueIf): New Value into every cell of a column that compares true. */
static int bb_change_value_if(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t column = get_in_int(b, 0, 0);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    nmo_engine_cell_t key, value;
    array_input_key(b, array, column, 2, &key);
    bool has_value = nmo_engine_block_input_parameter_count(b) > 3;
    if (has_value) {
        array_input_key(b, array, column, 3, &value);
    }
    int32_t count = 0;
    for (int32_t i = 0; i != -1;) {
        i = nmo_engine_array_find_row_op(engine, array, column, op, &key, i);
        if (i != -1) {
            if (has_value) {
                nmo_engine_array_set_cell(engine, array, i, column, &value);
            }
            i++;
            count++;
        }
    }
    set_out_int(b, 0, count);
    IN_OFF(b, 0);
    OUT_ON(b, count == 0 ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_remove_column(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_array_remove_column(nmo_engine_block_engine(b), array, get_in_int(b, 0, -1));
    return NMO_ENGINE_BR_OK;
}

/*
 * Insert Column: a column of Column Type before Index (-1: after the last).
 * A parameter column takes the type of the Parameter Type input; a Parameter
 * Type value is CK's index of a type, which the engine cannot map, so such a
 * column holds no values.
 */
static int bb_insert_column(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 1, &size);
    if (name == NULL || size == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t type = get_in_int(b, 2, CKARRAYTYPE_INT);
    nmo_guid_t param_type = NMO_GUID(0u, 0u);
    if (type == CKARRAYTYPE_PARAMETER) {
        param_type = nmo_engine_block_input_parameter_type(b, 3);
        if (nmo_guid_equals(param_type, CKPGUID_PARAMETERTYPE)) {
            param_type = NMO_GUID(0u, 0u);
        }
    }
    int32_t at = get_in_int(b, 0, -1);
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    if (nmo_engine_array_insert_column(engine, array, at, type, param_type) == NMO_OK) {
        int32_t column = at < 0 ? nmo_engine_array_column_count(engine, array) - 1 : at;
        nmo_engine_array_set_column_name(engine, array, column, name);
    }
    return NMO_ENGINE_BR_OK;
}

/* Column Sum (ArraySum): the sum of an Integer or Float column. */
static int bb_column_sum(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t sum = nmo_engine_array_sum(nmo_engine_block_engine(b), array, get_in_int(b, 0, 0));
    nmo_engine_block_set_output_value(b, 0, &sum, sizeof(sum));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_array_iterator_if(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t index;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        index = 0;
    } else {
        IN_OFF(b, 1);
        index = get_out_int(b, 0) + 1;
    }
    if (index >= nmo_engine_array_row_count(engine, array)) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    int32_t column = get_in_int(b, 0, 0);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 2, &key);
    int32_t found = nmo_engine_array_find_row_op(engine, array, column, op, &key, index);
    if (found < 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, found, 1);
    set_out_int(b, 0, found);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* Get Highest (ArrayGetHighest) */
static int bb_get_highest(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t row = nmo_engine_array_highest_row(nmo_engine_block_engine(b), array,
                                               get_in_int(b, 0, 0));
    set_out_int(b, 0, row);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 1);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Get Lowest (ArrayGetLowest): the row of the lowest cell of the column and its cells */
static int bb_get_lowest(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t row = eng_array_lowest_row(b->engine, array, get_in_int(b, 0, 0));
    set_out_int(b, 0, row);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 1);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Get Nearest (ArrayGetNearest): the row whose cell is nearest the key input */
static int bb_get_nearest(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t column = get_in_int(b, 0, 0);
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 1, &key);
    int32_t row = eng_array_nearest_row(b->engine, array, column, &key);
    set_out_int(b, 0, row);
    IN_OFF(b, 0);
    if (row < 0 || row >= nmo_engine_array_row_count(b->engine, array)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 1);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Column Operation (ArrayColumnsOperate): Result Column = Column 1 op Column 2 */
static int bb_column_operation(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_array_columns_operate(b->engine, array, get_in_int(b, 0, 0), get_in_int(b, 1, 1),
                              get_in_int(b, 2, 1), get_in_int(b, 3, 2));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Column Transform (ArrayColumnTransform): each cell op the value input */
static int bb_column_transform(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t column = get_in_int(b, 0, 0);
    nmo_engine_cell_t value;
    array_input_key(b, array, column, 2, &value);
    eng_array_column_transform(b->engine, array, column, get_in_int(b, 1, 1), value.value);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Column Product (ArrayProduct) */
static int bb_column_product(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t product = eng_array_product(b->engine, array, get_in_int(b, 0, 0));
    nmo_engine_block_set_output_value(b, 0, &product, sizeof(product));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Create Group From Array (CreateGroupFromArray): the group the output holds
 * gets the objects of Entities Column whose Search Column cell compares true.
 */
static int bb_create_group_from_array(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t entities = get_in_int(b, 0, 0), search = get_in_int(b, 1, 0);
    int32_t op = get_in_int(b, 2, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, search, 3, &key);
    nmo_object_id_t group = (nmo_object_id_t)get_out_int(b, 0);
    if (group == 0 || nmo_engine_group_count(b->engine, group) < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_array_create_group(b->engine, array, search, op, &key, group, entities);
    IN_OFF(b, 0);
    OUT_ON(b, nmo_engine_group_count(b->engine, group) == 0 ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

/* Get Column Name (ArrayGetColumnTitle); a parameter error past the columns */
static int bb_get_column_name(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t column = get_in_int(b, 0, 0);
    if (column < 0 || column >= nmo_engine_array_column_count(b->engine, array)) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    const char *name = nmo_engine_array_column_name(b->engine, array, column);
    name = name != NULL ? name : "";
    nmo_engine_block_set_output_value(b, 0, name, strlen(name) + 1u);
    return NMO_ENGINE_BR_OK;
}

/* Make Rows Unique (ArrayUnique) */
static int bb_make_rows_unique(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_array_unique(b->engine, array, get_in_int(b, 0, 0));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Move Column (ArrayMoveColumn) */
static int bb_move_column(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_array_move_column(b->engine, array, get_in_int(b, 0, -1), get_in_int(b, 1, -1));
    return NMO_ENGINE_BR_OK;
}

/* Move Row (ArrayMove): no array is no error here */
static int bb_move_row(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = array_target(b);
    if (array != 0) {
        eng_array_move_row(b->engine, array, get_in_int(b, 0, 0), get_in_int(b, 1, 1));
    }
    return NMO_ENGINE_BR_OK;
}

/* Reverse Order (ArrayReverse) */
static int bb_reverse_order(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_array_reverse(b->engine, array);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Swap Rows (ArraySwap): no array is no error here */
static int bb_swap_rows(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_swap_rows(b->engine, array, get_in_int(b, 0, 0), get_in_int(b, 1, 1));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Key Row (ArraySet): the inputs, one a column, into the first row whose
 * key column cell equals the key column's input.
 */
static int bb_set_key_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t column = nmo_engine_array_key_column(b->engine, array);
    if (column < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "No column criterion defined" */
    }
    nmo_engine_cell_t key;
    array_input_key(b, array, column, column, &key);
    int32_t row = nmo_engine_array_find_row(b->engine, array, column, &key, 0);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_read_inputs(b, array, row, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Array Load (ArrayLoad): the data file Filename names (the path manager's
 * data paths) into the array, as rows appended or over the rows from the
 * first, from column First Column on; File Error when it cannot be read.
 */
static int bb_array_load(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    const char *name = input_string(b, 0);
    bool append = get_in_int(b, 1, 1) != 0;
    int32_t column = get_in_int(b, 2, 0);
    size_t size = 0;
    unsigned char *text = name != NULL ? eng_read_path_file(b->engine, name, 1, &size) : NULL;
    IN_OFF(b, 0);
    if (text == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    eng_array_load_elements(b->engine, array, (const char *)text, size, append, column);
    free(text);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Array Save (ArrayWrite): Number of Column columns from First Column into
 * the file Filename names, as the original gives the name (it completes a
 * relative name, then writes the name as it was), through the host's writer.
 * The file is written as Windows' text mode writes it, a CR before each LF;
 * Write Error without a writer or when it fails.
 */
static int bb_array_save(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = b->engine;
    const char *name = input_string(b, 0);
    int32_t column = get_in_int(b, 1, 0), number = get_in_int(b, 2, 1);
    bool append = get_in_int(b, 3, 0) != 0;
    IN_OFF(b, 0);
    size_t size = 0;
    char *rows = eng_array_write_elements(engine, array, column, number, &size);
    size_t old_size = 0;
    unsigned char *old = append && name != NULL ? eng_read_file(engine, name, &old_size) : NULL;
    size_t lines = 0;
    for (size_t i = 0; rows != NULL && i < size; i++) {
        lines += rows[i] == '\n';
    }
    char *file = rows != NULL ? (char *)malloc(old_size + size + lines + 1u) : NULL;
    bool written = false;
    if (file != NULL && name != NULL && engine->host.write_file != NULL) {
        if (old_size > 0) {
            memcpy(file, old, old_size);
        }
        size_t at = old_size;
        for (size_t i = 0; i < size; i++) {
            if (rows[i] == '\n') {
                file[at++] = '\r';
            }
            file[at++] = rows[i];
        }
        written = engine->host.write_file(engine->host.write_file_user_data, name, file, at) == NMO_OK;
    }
    free(file);
    free(old);
    free(rows);
    OUT_ON(b, written ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_array_blocks[] = {
    {NMO_GUID_INIT(0x33b99f51, 0x07d95c45), "Get Cell", bb_get_cell, NULL, NULL},
    {NMO_GUID_INIT(0x30ed1c6d, 0x4a3b7067), "Set Cell", bb_set_cell, NULL, NULL},
    {NMO_GUID_INIT(0x33b77f41, 0x07b95c45), "Get Row", bb_get_row, NULL, NULL},
    {NMO_GUID_INIT(0x62e87901, 0x2df007dd), "Set Row", bb_set_row, NULL, NULL},
    {NMO_GUID_INIT(0x1c7e5dc6, 0x3f6423c2), "Add Row", bb_add_row, NULL, NULL},
    {NMO_GUID_INIT(0x198f0af9, 0x0268249f), "Iterator", bb_array_iterator, NULL, NULL},
    {NMO_GUID_INIT(0x6bec4be6, 0x12d64c7c), "Iterator If", bb_array_iterator_if, NULL, NULL},
    {NMO_GUID_INIT(0x35c9352f, 0x7b1a193b), "Clear Array", bb_clear_array, NULL, NULL},
    {NMO_GUID_INIT(0x0f8334ea, 0x279a40cd), "Remove Key Row", bb_remove_key_row, NULL, NULL},
    {NMO_GUID_INIT(0x49064205, 0x10e72f7a), "Get Key Row", bb_get_key_row, NULL, NULL},
    {NMO_GUID_INIT(0x78863443, 0x45af59b6), "Row Search", bb_row_search, NULL, NULL},
    {NMO_GUID_INIT(0x270f177a, 0x4ca20023), "Shuffle Order", bb_shuffle_order, NULL, NULL},
    {NMO_GUID_INIT(0x57865622, 0x662d2fee), "Remove Row If", bb_remove_row_if, NULL, NULL},
    {NMO_GUID_INIT(0x534377de, 0x75fd478a), "Value Count", bb_value_count, NULL, NULL},
    {NMO_GUID_INIT(0x4e6c6da8, 0x636904fc), "Test Cell", bb_test_cell, NULL, NULL},
    {NMO_GUID_INIT(0x6f623e68, 0x62bb5a98), "Sort Rows", bb_sort_rows, NULL, NULL},
    {NMO_GUID_INIT(0x1fa57136, 0x14310857), "Remove Row", bb_remove_row, NULL, NULL},
    {NMO_GUID_INIT(0x07253edb, 0x4d1237ed), "Change Value If", bb_change_value_if, NULL, NULL},
    {NMO_GUID_INIT(0x3f377888, 0x128a7767), "Remove Column", bb_remove_column, NULL, NULL},
    {NMO_GUID_INIT(0x05cb7802, 0x04a64a58), "Insert Column", bb_insert_column, NULL, NULL},
    {NMO_GUID_INIT(0x49124252, 0x1be909a4), "Column Sum", bb_column_sum, NULL, NULL},
    {NMO_GUID_INIT(0x46f71e13, 0x13d26f61), "Get Highest", bb_get_highest, NULL, NULL},
    {NMO_GUID_INIT(0x71b25ed7, 0x3c365a30), "Get Lowest", bb_get_lowest, NULL, NULL},
    {NMO_GUID_INIT(0x0fac60ad, 0x1bd31ac9), "Get Nearest", bb_get_nearest, NULL, NULL},
    {NMO_GUID_INIT(0x32351c65, 0x4f6916be), "Column Operation", bb_column_operation, NULL, NULL},
    {NMO_GUID_INIT(0x4f8f0dfe, 0x7c517321), "Column Transform", bb_column_transform, NULL, NULL},
    {NMO_GUID_INIT(0x39f64a9a, 0x682b721c), "Column Product", bb_column_product, NULL, NULL},
    {NMO_GUID_INIT(0x22c71529, 0x6bb265d2), "Create Group From Array", bb_create_group_from_array,
     NULL, NULL},
    {NMO_GUID_INIT(0x33b7ff41, 0x07b95c44), "Get Column Name", bb_get_column_name, NULL, NULL},
    {NMO_GUID_INIT(0x2b175818, 0x2c270b20), "Make Rows Unique", bb_make_rows_unique, NULL, NULL},
    {NMO_GUID_INIT(0x1ace1319, 0x66b112b1), "Move Column", bb_move_column, NULL, NULL},
    {NMO_GUID_INIT(0x101a62f4, 0x6e2d25f6), "Move Row", bb_move_row, NULL, NULL},
    {NMO_GUID_INIT(0x3b7d3db7, 0x519f015b), "Reverse Order", bb_reverse_order, NULL, NULL},
    {NMO_GUID_INIT(0x2ed72740, 0x406b5027), "Swap Rows", bb_swap_rows, NULL, NULL},
    {NMO_GUID_INIT(0x2fff4c63, 0x4fcb1b94), "Set Key Row", bb_set_key_row, NULL, NULL},
    {NMO_GUID_INIT(0x13bd2c64, 0x62db38e1), "Array Load", bb_array_load, NULL, NULL},
    {NMO_GUID_INIT(0x15185846, 0x41014a45), "Array Save", bb_array_save, NULL, NULL},
};

nmo_status_t eng_register_logics_array(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_array_blocks);
}
