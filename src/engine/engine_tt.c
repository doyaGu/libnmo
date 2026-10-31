/**
 * @file engine_tt.c
 * @brief Ports of the Terratools (TT) building blocks Ballance uses, and of
 *        Time Settings.
 *
 * Each function does what the block's function in the TT_*_RT DLLs does (the
 * reference is the Ballanced source). The database manager reads arrays from
 * the database file (TT Database Manager), found through the host's file
 * resolver. What the blocks ask the machine for comes from a fixed machine:
 * Windows, one render driver with 4:3 screen modes in 16 and 32 bits, no
 * video memory, not in interface mode, in debug mode once TT_DebugON turned
 * it on. The registry, the environment, the current directory and the path
 * manager's paths hold what the host set (nmo_engine_set_registry_value() and
 * the like). Statistics reports the frame counts CK2 keeps; the engine
 * measures no time but the frame's.
 */

#include "engine_internal.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_type_guids.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef nmo_engine_block_t blk_t;

#define IN_ACTIVE(b, i) nmo_engine_block_is_input_active((b), (i))
#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

static int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static const char *input_string(blk_t *b, int pos)
{
    size_t size = 0;
    const char *text = (const char *)nmo_engine_block_input_data(b, pos, &size);
    return text != NULL && size > 0 && memchr(text, '\0', size) != NULL ? text : NULL;
}

static void set_out(blk_t *b, int pos, const void *value, size_t size)
{
    nmo_engine_block_set_output_value(b, pos, value, size);
}

static eng_param_t *block_output(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine, node->first_pout, node->pout_count, pos);
}

/* ============================================================================
 * TT Database Manager
 * ============================================================================ */

/* DatabaseManager::Clear: no array registered, no database file */
static void database_clear(nmo_engine_t *engine)
{
    free(engine->database_file);
    engine->database_file = NULL;
    for (size_t i = 0; i < engine->database_arrays.count; i++) {
        free(engine->database_arrays.data[i]);
    }
    free(engine->database_arrays.data);
    engine->database_arrays.data = NULL;
    engine->database_arrays.count = engine->database_arrays.capacity = 0;
}

void eng_free_tt(nmo_engine_t *engine)
{
    database_clear(engine);
    for (size_t i = 0; i < engine->registry.count; i++) {
        free(engine->registry.data[i].section);
        free(engine->registry.data[i].name);
        free(engine->registry.data[i].value);
    }
    free(engine->registry.data);
    engine->registry.data = NULL;
    engine->registry.count = engine->registry.capacity = 0;
    for (size_t i = 0; i < engine->environment.count; i++) {
        free(engine->environment.data[i].name);
        free(engine->environment.data[i].value);
    }
    free(engine->environment.data);
    engine->environment.data = NULL;
    engine->environment.count = engine->environment.capacity = 0;
    free(engine->current_directory);
    engine->current_directory = NULL;
    for (int c = 0; c < ENG_PATH_CATEGORIES; c++) {
        for (size_t i = 0; i < engine->paths[c].count; i++) {
            free(engine->paths[c].data[i]);
        }
        free(engine->paths[c].data);
        engine->paths[c].data = NULL;
        engine->paths[c].count = engine->paths[c].capacity = 0;
    }
}

static unsigned char rotl8(unsigned char value, int shift)
{
    return (unsigned char)((value << shift) | (value >> (8 - shift)));
}

static bool read_i32(const unsigned char *data, size_t size, size_t offset, int32_t *out)
{
    if (offset > size || size - offset < sizeof(int32_t)) {
        return false;
    }
    memcpy(out, data + offset, sizeof(*out));
    return true;
}

/* The size of the string at `offset`, terminator included; 0 when it does not end. */
static size_t string_size(const unsigned char *data, size_t size, size_t offset)
{
    const unsigned char *end = offset < size ? memchr(data + offset, '\0', size - offset) : NULL;
    return end != NULL ? (size_t)(end - (data + offset)) + 1u : 0u;
}

/* The whole file a name resolves to; NULL when it is not there. */
static unsigned char *read_file(nmo_engine_t *engine, const char *name, size_t *out_size)
{
    const char *path = engine->resolve_file != NULL
                           ? engine->resolve_file(engine->resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        return NULL;
    }
    unsigned char *data = NULL;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (size > 0 && size < INT_MAX && fseek(file, 0, SEEK_SET) == 0) {
        data = (unsigned char *)malloc((size_t)size);
        if (data != NULL && fread(data, 1, (size_t)size, file) != (size_t)size) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    *out_size = data != NULL ? (size_t)size : 0u;
    return data;
}

/* DatabaseManager::Register: an array to save, once; 21 when it is already there */
static int database_register(nmo_engine_t *engine, const char *array_name)
{
    for (size_t i = 0; i < engine->database_arrays.count; i++) {
        if (strcmp(engine->database_arrays.data[i], array_name) == 0) {
            return 21;
        }
    }
    char **slot = NULL;
    size_t size = strlen(array_name) + 1u;
    char *copy = (char *)malloc(size);
    if (copy != NULL) {
        memcpy(copy, array_name, size);
    }
    if (copy == NULL || !ENG_VEC_PUSH(engine->database_arrays, slot)) {
        free(copy);
        return 0;
    }
    *slot = copy;
    return 1;
}

/*
 * DatabaseManager::Load: the array of that name from the database file into
 * the data array of that name, its columns, key column and rows replaced.
 * 1 when loaded, 31 when the file cannot be read, 32 when its content is
 * wrong, 33 when it has no such array (or the engine has no array of that
 * name, which the original would make). With `auto_register` the array is
 * registered for saving once found, before it is read.
 */
static int database_load(nmo_engine_t *engine, const char *array_name, bool auto_register)
{
    if (array_name == NULL) {
        return 33;
    }
    if (engine->database_file == NULL || engine->database_file[0] == '\0') {
        return 31;
    }
    size_t size = 0;
    unsigned char *data = read_file(engine, engine->database_file, &size);
    if (data == NULL) {
        return 31;
    }
    if (engine->database_crypted) {
        for (size_t i = 0; i < size; i++) {
            data[i] = (unsigned char)-(rotl8(data[i], 3) ^ 0xAF);
        }
    }

    /* Chunks: a name, the array's size, the array */
    size_t pos = 0, name_size = 0;
    int32_t array_size = 0;
    bool found = false;
    while (pos < size) {
        name_size = string_size(data, size, pos);
        if (name_size == 0 || !read_i32(data, size, pos + name_size, &array_size) ||
            array_size < 0 || (size_t)array_size > size - pos - name_size - sizeof(int32_t)) {
            free(data);
            return 32;
        }
        if (strcmp((const char *)data + pos, array_name) == 0) {
            found = true;
            break;
        }
        pos += name_size + sizeof(int32_t) + (size_t)array_size;
    }
    nmo_object_id_t array = found ? eng_object_named(engine, array_name, NMO_CID_DATAARRAY) : 0;
    if (!found || eng_find_array(engine, array) == NULL) {
        free(data);
        return 33;
    }

    if (auto_register) {
        database_register(engine, array_name);
    }
    const unsigned char *table = data + pos + name_size + sizeof(int32_t);
    size_t table_size = (size_t)array_size;
    int32_t columns = 0, rows = 0, key = 0;
    if (!read_i32(table, table_size, 0, &columns) || !read_i32(table, table_size, 4, &rows) ||
        !read_i32(table, table_size, 8, &key) || columns < 0 || rows < 0 || key < -1 ||
        key >= columns) {
        free(data);
        return 32;
    }
    nmo_engine_array_clear(engine, array);
    while (nmo_engine_array_column_count(engine, array) > 0) {
        nmo_engine_array_remove_column(engine, array, nmo_engine_array_column_count(engine, array) - 1);
    }

    size_t offset = 12;
    for (int32_t c = 0; c < columns; c++) {
        size_t column_name = string_size(table, table_size, offset);
        int32_t type = 0;
        if (column_name == 0 || !read_i32(table, table_size, offset + column_name, &type) ||
            (type != CKARRAYTYPE_INT && type != CKARRAYTYPE_FLOAT &&
             type != CKARRAYTYPE_STRING)) {
            free(data);
            return 32;
        }
        nmo_engine_array_insert_column(engine, array, -1, type, NMO_GUID(0u, 0u));
        nmo_engine_array_set_column_name(engine, array, c, (const char *)table + offset);
        offset += column_name + sizeof(int32_t);
    }
    eng_find_array(engine, array)->key_column = key;
    for (int32_t r = 0; r < rows; r++) {
        nmo_engine_array_insert_row(engine, array, -1, NULL);
    }
    /* Column by column */
    for (int32_t c = 0; c < columns; c++) {
        int32_t type = nmo_engine_array_column_type(engine, array, c);
        for (int32_t r = 0; r < rows; r++) {
            nmo_engine_cell_t cell = {.type = type};
            if (type == CKARRAYTYPE_STRING) {
                size_t text_size = string_size(table, table_size, offset);
                if (text_size == 0) {
                    free(data);
                    return 32;
                }
                cell.text = (const char *)table + offset;
                offset += text_size;
            } else {
                int32_t value = 0;
                if (!read_i32(table, table_size, offset, &value)) {
                    free(data);
                    return 32;
                }
                cell.value = (uint32_t)value;
                offset += sizeof(int32_t);
            }
            nmo_engine_array_set_cell(engine, array, r, c, &cell);
        }
    }
    free(data);
    return 1;
}

/* TT Set Database Properties: the database file and whether it is scrambled. */
static int bb_set_database_properties(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    const char *file = input_string(b, 0);
    if (file == NULL || file[0] == '\0') {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    size_t length = strlen(file);
    char *copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    memcpy(copy, file, length + 1u);
    free(engine->database_file);
    engine->database_file = copy;
    engine->database_crypted = get_in_int(b, 1, 1) != 0;
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Load Database: OK when the array loaded, Error otherwise. */
static int bb_load_database(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    }
    int result = database_load(nmo_engine_block_engine(b), input_string(b, 1),
                               get_in_int(b, 0, 1) != 0);
    OUT_ON(b, result == 1 ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* Bytes appended to a growing buffer; false when out of memory */
typedef struct database_bytes {
    unsigned char *data;
    size_t size, capacity;
} database_bytes_t;

static bool database_put(database_bytes_t *out, const void *bytes, size_t size)
{
    if (out->size + size > out->capacity) {
        size_t capacity = out->capacity > 0 ? out->capacity : 256u;
        while (capacity < out->size + size) {
            capacity *= 2u;
        }
        unsigned char *grown = (unsigned char *)realloc(out->data, capacity);
        if (grown == NULL) {
            return false;
        }
        out->data = grown;
        out->capacity = capacity;
    }
    if (size > 0) {
        memcpy(out->data + out->size, bytes, size);
    }
    out->size += size;
    return true;
}

static bool database_put_text(database_bytes_t *out, const char *text)
{
    return database_put(out, text != NULL ? text : "", (text != NULL ? strlen(text) : 0u) + 1u);
}

static bool database_put_i32(database_bytes_t *out, int32_t value)
{
    return database_put(out, &value, sizeof(value));
}

/*
 * DatabaseManager::Save: each registered array as a chunk (its name, the
 * array's size, columns, rows, key column, each column's name and type,
 * then the cells column by column: ints and floats as 4 bytes, strings with
 * their terminator, other types nothing), scrambled unless not crypted, to
 * the database file through the host's writer. 1 when saved, 41 when there
 * is no file or it cannot be written, 42 when a registered array is gone.
 */
static int database_save(nmo_engine_t *engine)
{
    if (engine->database_file == NULL || engine->database_file[0] == '\0') {
        return 41;
    }
    database_bytes_t file = {NULL, 0u, 0u};
    for (size_t n = 0; n < engine->database_arrays.count; n++) {
        const char *array_name = engine->database_arrays.data[n];
        nmo_object_id_t array = eng_object_named(engine, array_name, NMO_CID_DATAARRAY);
        if (array == 0 || eng_find_array(engine, array) == NULL) {
            free(file.data);
            return 42;
        }
        int32_t columns = nmo_engine_array_column_count(engine, array);
        int32_t rows = nmo_engine_array_row_count(engine, array);
        database_bytes_t table = {NULL, 0u, 0u};
        bool ok = database_put_i32(&table, columns) && database_put_i32(&table, rows) &&
                  database_put_i32(&table, nmo_engine_array_key_column(engine, array));
        for (int32_t c = 0; ok && c < columns; c++) {
            ok = database_put_text(&table, nmo_engine_array_column_name(engine, array, c)) &&
                 database_put_i32(&table, nmo_engine_array_column_type(engine, array, c));
        }
        for (int32_t c = 0; ok && c < columns; c++) {
            int32_t type = nmo_engine_array_column_type(engine, array, c);
            for (int32_t r = 0; ok && r < rows; r++) {
                nmo_engine_cell_t cell;
                memset(&cell, 0, sizeof(cell));
                nmo_engine_array_get_cell(engine, array, r, c, &cell);
                if (type == CKARRAYTYPE_INT || type == CKARRAYTYPE_FLOAT) {
                    ok = database_put_i32(&table, (int32_t)cell.value);
                } else if (type == CKARRAYTYPE_STRING) {
                    ok = database_put_text(&table, cell.text);
                }
            }
        }
        ok = ok && database_put_text(&file, array_name) &&
             database_put_i32(&file, (int32_t)table.size) &&
             database_put(&file, table.data, table.size);
        free(table.data);
        if (!ok) {
            free(file.data);
            return 41;
        }
    }
    if (file.data == NULL) {
        return 41;
    }
    if (engine->database_crypted) {
        for (size_t i = 0; i < file.size; i++) {
            unsigned char x = (unsigned char)((unsigned char)-file.data[i] ^ 0xAFu);
            file.data[i] = (unsigned char)((x >> 3) | (x << 5));
        }
    }
    nmo_status_t status = engine->write_file != NULL
                              ? engine->write_file(engine->write_file_user_data,
                                                   engine->database_file, file.data, file.size)
                              : NMO_ERR_NOT_SUPPORTED;
    free(file.data);
    return status == NMO_OK ? 1 : 41;
}

/* TT Save Database: OK when the registered arrays were saved, Error otherwise. */
static int bb_save_database(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, database_save(nmo_engine_block_engine(b)) == 1 ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Register Array: Register adds the array to the ones TT Save Database
 * saves, by name (OK; Error without one, or when it is registered already);
 * Clear forgets them and the database file (OK).
 */
static int bb_register_array(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
        const char *name = array != 0 ? nmo_engine_object_name(engine, array) : NULL;
        OUT_ON(b, name != NULL && database_register(engine, name) == 1 ? 0 : 1);
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        database_clear(engine);
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Registry
 * ============================================================================ */

static char *text_copy(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

/* The length of a key without its trailing backslashes. */
static size_t section_length(const char *section)
{
    size_t length = strlen(section);
    while (length > 0 && section[length - 1] == '\\') {
        length--;
    }
    return length;
}

static bool same_text(const char *a, size_t a_length, const char *b, size_t b_length)
{
    if (a_length != b_length) {
        return false;
    }
    for (size_t i = 0; i < a_length; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

static eng_registry_value_t *registry_find(nmo_engine_t *engine, const char *section,
                                           const char *name)
{
    size_t length = section_length(section);
    for (size_t i = 0; i < engine->registry.count; i++) {
        eng_registry_value_t *entry = &engine->registry.data[i];
        if (same_text(entry->section, strlen(entry->section), section, length) &&
            same_text(entry->name, strlen(entry->name), name, strlen(name))) {
            return entry;
        }
    }
    return NULL;
}

nmo_status_t nmo_engine_set_registry_value(nmo_engine_t *engine, const char *section,
                                           const char *name, const char *value)
{
    if (engine == NULL || section == NULL || name == NULL || value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = text_copy(value, strlen(value));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    eng_registry_value_t *entry = registry_find(engine, section, name);
    if (entry != NULL) {
        free(entry->value);
        entry->value = copy;
        return NMO_OK;
    }
    char *section_copy = text_copy(section, section_length(section));
    char *name_copy = text_copy(name, strlen(name));
    if (section_copy == NULL || name_copy == NULL || !ENG_VEC_PUSH(engine->registry, entry)) {
        free(copy);
        free(section_copy);
        free(name_copy);
        return NMO_ERR_NOMEM;
    }
    entry->section = section_copy;
    entry->name = name_copy;
    entry->value = copy;
    return NMO_OK;
}

static const char *registry_value(nmo_engine_t *engine, const char *section, const char *name)
{
    const eng_registry_value_t *entry =
        section != NULL && name != NULL ? registry_find(engine, section, name) : NULL;
    return entry != NULL ? entry->value : NULL;
}

/* VxConfig reads a DWORD value; false when the value is not a number. */
static bool registry_int(nmo_engine_t *engine, const char *section, const char *name,
                         int32_t *out)
{
    const char *text = registry_value(engine, section, name);
    char *end = NULL;
    long value = text != NULL ? strtol(text, &end, 0) : 0;
    if (text == NULL || end == text || *end != '\0') {
        return false;
    }
    *out = (int32_t)value;
    return true;
}

static bool registry_float(nmo_engine_t *engine, const char *section, const char *name,
                           float *out)
{
    const char *text = registry_value(engine, section, name);
    char *end = NULL;
    float value = text != NULL ? strtof(text, &end) : 0.0f;
    if (text == NULL || end == text || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

/* ============================================================================
 * Environment, current directory and paths
 * ============================================================================ */

static eng_registry_value_t *environment_find(nmo_engine_t *engine, const char *name)
{
    for (size_t i = 0; i < engine->environment.count; i++) {
        eng_registry_value_t *entry = &engine->environment.data[i];
        if (same_text(entry->name, strlen(entry->name), name, strlen(name))) {
            return entry;
        }
    }
    return NULL;
}

nmo_status_t nmo_engine_set_environment_variable(nmo_engine_t *engine, const char *name,
                                                 const char *value)
{
    if (engine == NULL || name == NULL || value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = text_copy(value, strlen(value));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    eng_registry_value_t *entry = environment_find(engine, name);
    if (entry != NULL) {
        free(entry->value);
        entry->value = copy;
        return NMO_OK;
    }
    char *name_copy = text_copy(name, strlen(name));
    if (name_copy == NULL || !ENG_VEC_PUSH(engine->environment, entry)) {
        free(copy);
        free(name_copy);
        return NMO_ERR_NOMEM;
    }
    entry->name = name_copy;
    entry->value = copy;
    return NMO_OK;
}

nmo_status_t nmo_engine_set_current_directory(nmo_engine_t *engine, const char *path)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = path != NULL ? text_copy(path, strlen(path)) : NULL;
    if (path != NULL && copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    free(engine->current_directory);
    engine->current_directory = copy;
    return NMO_OK;
}

nmo_status_t nmo_engine_add_path(nmo_engine_t *engine, int32_t category, const char *path)
{
    if (engine == NULL || path == NULL || category < 0 || category >= ENG_PATH_CATEGORIES) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = text_copy(path, strlen(path));
    char **slot = NULL;
    if (copy == NULL || !ENG_VEC_PUSH(engine->paths[category], slot)) {
        free(copy);
        return NMO_ERR_NOMEM;
    }
    *slot = copy;
    return NMO_OK;
}

const char *nmo_engine_path(const nmo_engine_t *engine, int32_t category, int32_t index)
{
    if (engine == NULL || category < 0 || category >= ENG_PATH_CATEGORIES || index < 0 ||
        (size_t)index >= engine->paths[category].count) {
        return NULL;
    }
    return engine->paths[category].data[index];
}

/* The registry value name of a row: its index. */
static char *row_entry(int32_t row)
{
    int length = snprintf(NULL, 0, "%d", (int)row);
    char *entry = length > 0 ? (char *)malloc((size_t)length + 1u) : NULL;
    if (entry != NULL) {
        snprintf(entry, (size_t)length + 1u, "%d", (int)row);
    }
    return entry;
}

/*
 * SaveArray-Mode of TT_ReadRegistry: each cell from value <row> of key
 * <section><array name>\<column>\; false at the first it cannot read.
 */
static bool read_registry_array(blk_t *b, const char *section, nmo_object_id_t array,
                                const char *array_name)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    int32_t rows = nmo_engine_array_row_count(engine, array);
    bool ok = true;
    for (int32_t c = 0; c < columns && ok; c++) {
        int length = snprintf(NULL, 0, "%s\\%s\\%d\\", section, array_name, (int)c);
        char *column_section = length > 0 ? (char *)malloc((size_t)length + 1u) : NULL;
        if (column_section == NULL) {
            return false;
        }
        snprintf(column_section, (size_t)length + 1u, "%s\\%s\\%d\\", section, array_name, (int)c);
        int32_t type = nmo_engine_array_column_type(engine, array, c);
        for (int32_t r = 0; r < rows && ok; r++) {
            char *entry = row_entry(r);
            nmo_engine_cell_t cell = {.type = type};
            int32_t value = 0;
            float number = 0.0f;
            const char *text = NULL;
            switch (type) {
            case CKARRAYTYPE_INT:
            case CKARRAYTYPE_PARAMETER: /* a Boolean parameter: TT_ReadRegistry refuses others */
                ok = entry != NULL && registry_int(engine, column_section, entry, &value);
                cell.value = (uint32_t)value;
                break;
            case CKARRAYTYPE_FLOAT:
                ok = entry != NULL && registry_float(engine, column_section, entry, &number);
                memcpy(&cell.value, &number, sizeof(number));
                break;
            case CKARRAYTYPE_STRING:
                text = entry != NULL ? registry_value(engine, column_section, entry) : NULL;
                ok = text != NULL;
                cell.text = text;
                break;
            default:
                free(entry);
                continue;
            }
            if (ok) {
                nmo_engine_array_set_cell(engine, array, r, c, &cell);
            }
            free(entry);
        }
        free(column_section);
    }
    return ok;
}

/* ============================================================================
 * TT Toolbox and TT Interface Manager
 * ============================================================================ */

/*
 * TT_Debug: with Enable, in debug mode, writes its text to the console and
 * the log file (an effect) and takes its input; otherwise In stays active.
 * Exit either way.
 */
static int bb_tt_debug(blk_t *b)
{
    if (get_in_int(b, 3, 0) != 0 && b->engine->tt_debug_mode) {
        IN_OFF(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT_DebugON: turns the debug manager's debug mode on. */
static int bb_debug_on(blk_t *b)
{
    b->engine->tt_debug_mode = true;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Exit to System: asks the player to quit (nmo_engine_exit_requested());
 * its input stays active, as in the original.
 */
static int bb_exit_to_system(blk_t *b)
{
    b->engine->exit_requested = true;
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT GetEnvironmentVariable: the variable's value (Found), or Not Found and a parameter error. */
static int bb_get_environment_variable(blk_t *b)
{
    IN_OFF(b, 0);
    const char *name = input_string(b, 0);
    const eng_registry_value_t *entry = name != NULL ? environment_find(b->engine, name) : NULL;
    if (entry == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    set_out(b, 0, entry->value, strlen(entry->value) + 1u);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT_GetCurrentDirectory: the current directory (OK), or empty (Fail); its input stays active. */
static int bb_get_current_directory(blk_t *b)
{
    const char *directory = b->engine->current_directory;
    if (directory == NULL || directory[0] == '\0') {
        set_out(b, 0, "", 1u);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    set_out(b, 0, directory, strlen(directory) + 1u);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ReplacePath: the first path of category PathIDX (0 bitmap, 1 data,
 * 2 sound) that is not Current Directory + Old renamed Current Directory +
 * New (Ok). The original compares that way round: Failed only when every
 * path is the old one, there is none, or the category is another. Its
 * input stays active.
 */
static int bb_replace_path(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    const char *old_path = input_string(b, 0);
    const char *new_path = input_string(b, 1);
    const char *directory = input_string(b, 2);
    int32_t category = get_in_int(b, 3, 0);
    if (category < 0 || category >= ENG_PATH_CATEGORIES) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    directory = directory != NULL ? directory : "";
    old_path = old_path != NULL ? old_path : "";
    new_path = new_path != NULL ? new_path : "";
    size_t directory_length = strlen(directory);
    size_t old_length = strlen(old_path);
    for (size_t i = 0; i < engine->paths[category].count; i++) {
        const char *path = engine->paths[category].data[i];
        if (strlen(path) == directory_length + old_length &&
            memcmp(path, directory, directory_length) == 0 &&
            memcmp(path + directory_length, old_path, old_length) == 0) {
            continue;
        }
        size_t new_length = strlen(new_path);
        char *renamed = (char *)malloc(directory_length + new_length + 1u);
        if (renamed == NULL) {
            return NMO_ENGINE_BR_GENERICERROR;
        }
        memcpy(renamed, directory, directory_length);
        memcpy(renamed + directory_length, new_path, new_length + 1u);
        free(engine->paths[category].data[i]);
        engine->paths[category].data[i] = renamed;
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* TT_SetMipMapping: the target material's texture mipmapped and its filter modes set (an effect). */
static int bb_set_mip_mapping(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_TextureInfo: the driver's video memory (none), the textures, and those
 * in video memory (TT_PreloadTextures); the engine holds no surfaces, so
 * the memory they use reads 0 MB.
 */
static int bb_texture_info(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(b->engine, NMO_CID_TEXTURE, &ids);
    int32_t in_video = 0;
    for (size_t i = 0; i < count; i++) {
        if (eng_map_get(&b->engine->video_textures, ids[i]) >= 0) {
            in_video++;
        }
    }
    free(ids);
    const int32_t max_video_memory = 0, textures = (int32_t)count;
    const float no_memory = 0.0f;
    set_out(b, 0, &max_video_memory, sizeof(max_video_memory));
    set_out(b, 1, &textures, sizeof(textures));
    set_out(b, 2, &no_memory, sizeof(no_memory));
    set_out(b, 3, &in_video, sizeof(in_video));
    set_out(b, 4, &no_memory, sizeof(no_memory));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_LinearVolume: a volume in [0, 1] made linear, 0.02 * 50^x: 1 above 1,
 * 0 at 0.01 or below (and for a NaN, as the x87 compares it).
 */
static int bb_linear_volume(blk_t *b)
{
    IN_OFF(b, 0);
    float x = 0.0f;
    nmo_engine_block_get_input_value(b, 0, &x, sizeof(x));
    float linear = 0.0f;
    if (x > 1.0f) {
        linear = 1.0f;
    } else if ((double)x > 0.01) { /* against the double 0.01 */
        linear = (float)(pow(50.0, (double)x) * 0.02);
    }
    nmo_engine_block_set_output_value(b, 0, &linear, sizeof(linear));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ReflectionMapping: the target's mesh mapped as the camera sees it
 * reflected (its UVs, which the engine does not draw): Off ends it (Exit
 * Off), On reports it (Exit On), and it stays active while the target has
 * a mesh.
 */
static int bb_reflection_mapping(blk_t *b)
{
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    }
    int32_t entity = target != 0 ? eng_entity_index(b->engine, target) : -1;
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    return entity >= 0 && eng_entity_has_mesh(b->engine, entity) ? NMO_ENGINE_BR_ACTIVATENEXTFRAME
                                                                  : NMO_ENGINE_BR_OK;
}

/*
 * TT_PreloadTextures: each texture not in video memory put there; the
 * count of those moved. The engine's driver takes every texture, so a
 * texture counts the first time.
 */
static int bb_preload_textures(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(b->engine, NMO_CID_TEXTURE, &ids);
    int32_t moved = 0;
    for (size_t i = 0; i < count; i++) {
        if (eng_map_get(&b->engine->video_textures, ids[i]) < 0 &&
            eng_map_put(&b->engine->video_textures, ids[i], 1)) {
            moved++;
        }
    }
    free(ids);
    nmo_engine_block_set_output_value(b, 0, &moved, sizeof(moved));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Player Active: Active outside interface mode (In stays active, as in the original). */
static int bb_player_active(blk_t *b)
{
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Set_2DSprite: places its 2D frame (an effect). */
static int bb_set_2d_sprite(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT OperationSystem: a Windows of the VXOS_WIN31..VXOS_WINSEVEN range reads as 2, 0, 6. */
static int bb_operation_system(blk_t *b)
{
    const int32_t id = 2, minor = 0, major = 6;
    set_out(b, 0, &id, sizeof(id));
    set_out(b, 1, &minor, sizeof(minor));
    set_out(b, 2, &major, sizeof(major));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT GetMemoryStatus: the fixed machine's memory, in megabytes. */
static int bb_get_memory_status(blk_t *b)
{
    const int32_t load = 50;
    const float total = 4096.0f, available = 2048.0f, virtual_total = 2048.0f,
                virtual_available = 1536.0f;
    set_out(b, 0, &load, sizeof(load));
    set_out(b, 1, &total, sizeof(total));
    set_out(b, 2, &available, sizeof(available));
    set_out(b, 3, &virtual_total, sizeof(virtual_total));
    set_out(b, 4, &virtual_available, sizeof(virtual_available));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static const struct {
    int32_t width, height, bpp;
} screen_modes[] = {{640, 480, 16},  {640, 480, 32},  {800, 600, 16},
                    {800, 600, 32},  {1024, 768, 16}, {1024, 768, 32}};

/* The array an input names, emptied of rows and columns; 0 when it names none. */
static nmo_object_id_t emptied_array(blk_t *b, int pos)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, pos);
    if (array == 0 || eng_find_array(engine, array) == NULL) {
        return 0;
    }
    nmo_engine_array_clear(engine, array);
    while (nmo_engine_array_column_count(engine, array) > 0) {
        nmo_engine_array_remove_column(engine, array, 0);
    }
    return array;
}

static void set_int_cell(nmo_engine_t *engine, nmo_object_id_t array, int32_t row, int32_t column,
                         int32_t value)
{
    nmo_engine_cell_t cell = {.type = CKARRAYTYPE_INT, .value = (uint32_t)value};
    nmo_engine_array_set_cell(engine, array, row, column, &cell);
}

static void set_text_cell(nmo_engine_t *engine, nmo_object_id_t array, int32_t row,
                          int32_t column, const char *text)
{
    nmo_engine_cell_t cell = {.type = CKARRAYTYPE_STRING, .text = text};
    nmo_engine_array_set_cell(engine, array, row, column, &cell);
}

/* TT List Driver: DriverName, DriverID, DriverDesc of each render driver; the driver in use. */
static int bb_list_driver(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t drivers = emptied_array(b, 0);
    if (drivers == 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_insert_column(engine, drivers, -1, CKARRAYTYPE_STRING, NMO_GUID(0u, 0u));
    nmo_engine_array_insert_column(engine, drivers, 0, CKARRAYTYPE_STRING, NMO_GUID(0u, 0u));
    nmo_engine_array_insert_column(engine, drivers, 1, CKARRAYTYPE_INT, NMO_GUID(0u, 0u));
    nmo_engine_array_insert_row(engine, drivers, -1, NULL);
    set_text_cell(engine, drivers, 0, 0, "libnmo");
    set_int_cell(engine, drivers, 0, 1, 0);
    set_text_cell(engine, drivers, 0, 2, "No rendering");
    const int32_t driver = 0;
    set_out(b, 0, &driver, sizeof(driver));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT List ScreenModes: Mode, Width, Height, Bpp of the driver's modes; the mode in use. */
static int bb_list_screen_modes(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t driver = get_in_int(b, 0, 0);
    nmo_object_id_t modes = emptied_array(b, 1);
    if (modes == 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_insert_column(engine, modes, -1, CKARRAYTYPE_INT, NMO_GUID(0u, 0u));
    for (int32_t c = 0; c < 3; c++) {
        nmo_engine_array_insert_column(engine, modes, c, CKARRAYTYPE_INT, NMO_GUID(0u, 0u));
    }
    if (driver != 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    for (int32_t i = 0; i < (int32_t)(sizeof(screen_modes) / sizeof(screen_modes[0])); i++) {
        nmo_engine_array_insert_row(engine, modes, -1, NULL);
        set_int_cell(engine, modes, i, 0, i);
        set_int_cell(engine, modes, i, 1, screen_modes[i].width);
        set_int_cell(engine, modes, i, 2, screen_modes[i].height);
        set_int_cell(engine, modes, i, 3, screen_modes[i].bpp);
    }
    set_out(b, 0, &engine->screen_mode, sizeof(engine->screen_mode));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Change ScreenMode: asks the player for a mode of the driver's list (an
 * effect); Error for another driver or mode. In stays active, as in the original.
 */
static int bb_change_screen_mode(blk_t *b)
{
    int32_t driver = get_in_int(b, 0, 0);
    int32_t mode = get_in_int(b, 1, 0);
    if (driver != 0 || mode < 0 || mode >= (int32_t)(sizeof(screen_modes) / sizeof(screen_modes[0]))) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    b->engine->screen_mode = mode;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ReadRegistry: value Reg-Entry of key Reg-Section into Data, as Data's
 * type reads it; in SaveArray-Mode, the destination array's cells. OK, or
 * FAILED when a value is missing (In stays active either way).
 */
static int bb_read_registry(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    bool save_array = false;
    nmo_engine_block_get_local_value(b, 0, &save_array, sizeof(save_array));
    const char *section = input_string(b, 0);
    if (section == NULL) {
        section = "";
    }
    bool ok = false;
    if (save_array) {
        nmo_object_id_t array = nmo_engine_block_get_input_object(b, 2);
        const char *array_name = input_string(b, 3);
        if (array_name == NULL || array_name[0] == '\0') {
            array_name = array != 0 ? nmo_engine_object_name(engine, array) : NULL;
        }
        ok = array != 0 && eng_find_array(engine, array) != NULL && array_name != NULL &&
             array_name[0] != '\0' && read_registry_array(b, section, array, array_name);
    } else {
        const char *entry = input_string(b, 1);
        nmo_guid_t type = nmo_engine_block_output_parameter_type(b, 0);
        if (nmo_guid_equals(type, CKPGUID_INT) || nmo_guid_equals(type, CKPGUID_BOOL)) {
            int32_t value = 0;
            ok = registry_int(engine, section, entry, &value);
            if (ok) {
                set_out(b, 0, &value, sizeof(value));
            }
        } else if (nmo_guid_equals(type, CKPGUID_FLOAT)) {
            float value = 0.0f;
            ok = registry_float(engine, section, entry, &value);
            if (ok) {
                set_out(b, 0, &value, sizeof(value));
            }
        } else if (nmo_guid_equals(type, CKPGUID_STRING)) {
            const char *value = registry_value(engine, section, entry);
            ok = value != NULL;
            if (ok) {
                nmo_engine_block_set_output_text(b, 0, value);
            }
        } else {
            ok = true; /* nothing to read */
        }
    }
    OUT_ON(b, ok ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Sky: On makes the sky around the camera and keeps it there each frame
 * (an effect), Off hides it.
 */
static int bb_sky(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT PushButton2: the button state (local 0) and whether the left button was down (local 1) */
static int32_t get_local_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

static void set_local_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

/*
 * TT PushButton2: Roll Over when the mouse comes over the target 2D entity
 * with the cursor shown, Released when it leaves, Mouse Down when the left
 * button goes down over it.
 */
static int bb_push_button2(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool turned_on = false;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 0, 0);
        set_local_int(b, 1, 0);
        turned_on = true;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t old_state = get_local_int(b, 0, 0);
    bool old_down = get_local_int(b, 1, 0) != 0;
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t state = target == nmo_engine_entity_under_mouse(engine) &&
                            nmo_engine_cursor_is_visible(engine)
                        ? 1
                        : 0;
    bool down = nmo_engine_is_mouse_button_down(engine, 0);
    bool changed = false;
    if (turned_on && down) {
        old_down = true;
        set_local_int(b, 1, 1);
    }
    if (down != old_down) {
        set_local_int(b, 1, down ? 1 : 0);
        changed = true;
    }
    if (state != old_state) {
        set_local_int(b, 0, state);
        OUT_ON(b, state != 0 ? 1 : 0); /* Roll Over, Released */
    }
    if (changed && state == 1 && down) {
        OUT_ON(b, 2); /* Mouse Down */
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT_Timer: the TT Time Manager's time, which every running timer advances
 * by the frame time (On/Reset and Off restart it, Pause and Play stop and
 * resume it), in Elapsed Time.
 */
static int bb_tt_timer(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    if (IN_ACTIVE(b, 0)) {
        engine->tt_timing = true;
        engine->tt_time = 0.0f;
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    if (IN_ACTIVE(b, 1)) {
        engine->tt_timing = false;
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b, 2)) {
        engine->tt_timing = true;
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b, 3)) {
        engine->tt_timing = true;
        engine->tt_time = 0.0f;
        IN_OFF(b, 3);
        OUT_ON(b, 3);
        return NMO_ENGINE_BR_OK;
    }
    float delta = nmo_engine_block_delta_time(b);
    float elapsed = engine->tt_time + delta;
    if (engine->tt_timing) {
        engine->tt_time += delta;
    }
    set_out(b, 0, &elapsed, sizeof(elapsed));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT_SplitString: the parts of Text between Delimiters (" " when none) into
 * the outputs after Elements Found, each as its output's type reads text.
 */
static int bb_split_string(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *text = input_string(b, 0);
    if (text == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    const char *delimiter = input_string(b, 1);
    if (delimiter == NULL || delimiter[0] == '\0') {
        delimiter = " ";
    }
    size_t delimiter_length = strlen(delimiter);
    int outputs = nmo_engine_block_output_parameter_count(b);
    int32_t elements = 0;
    const char *rest = text;
    for (const char *p = strstr(rest, delimiter); p != NULL; p = strstr(p + delimiter_length, delimiter)) {
        if (p != rest) {
            if (++elements >= outputs) {
                break;
            }
            size_t length = (size_t)(p - rest);
            char *token = (char *)malloc(length + 1u);
            if (token == NULL) {
                break;
            }
            memcpy(token, rest, length);
            token[length] = '\0';
            eng_param_set_text(b->engine, block_output(b, elements), token);
            free(token);
        }
        rest = p + delimiter_length;
    }
    if (rest[0] != '\0' && ++elements < outputs) {
        eng_param_set_text(b->engine, block_output(b, elements), rest);
    }
    set_out(b, 0, &elements, sizeof(elements));
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_Counter: Initialize sets the count to Start (Min when Start is outside
 * [Min, Max]); Count Up and Count Down step it, wrapping around [Min, Max].
 * The count in the local and the output, each call.
 */
static int bb_tt_counter(blk_t *b)
{
    int32_t start = get_in_int(b, 0, 0), min = get_in_int(b, 1, 0), max = get_in_int(b, 2, 9);
    int32_t count = get_local_int(b, 0, 0);
    if (start < min || start > max) {
        start = min; /* "TT Counter: Start value out of range" */
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        count = start;
        OUT_ON(b, 0);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        if (++count > max) {
            count = min;
        }
        OUT_ON(b, 1);
    }
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        if (--count < min) {
            count = max;
        }
        OUT_ON(b, 2);
    }
    set_local_int(b, 0, count);
    set_out(b, 0, &count, sizeof(count));
    return NMO_ENGINE_BR_OK;
}

/*
 * TT ConvertPixel-Homogen: a 2D vector in pixels of the render context made
 * homogeneous (divided by its size), or, with local 0 false, a homogeneous
 * one made pixels (truncated); X, Y and the vector out.
 */
static int bb_convert_pixel_homogen(blk_t *b)
{
    IN_OFF(b, 0);
    float v[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, v, sizeof(v));
    float width = (float)screen_modes[b->engine->screen_mode].width;
    float height = (float)screen_modes[b->engine->screen_mode].height;
    if ((get_local_int(b, 0, 1) & 0xff) != 0) {
        v[0] /= width;
        v[1] /= height;
    } else {
        v[0] = (float)(int32_t)(width * v[0]);
        v[1] = (float)(int32_t)(height * v[1]);
    }
    set_out(b, 0, &v[0], sizeof(v[0]));
    set_out(b, 1, &v[1], sizeof(v[1]));
    set_out(b, 2, v, sizeof(v));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Planar Filter: On draws the filtering quad over the view after the
 * sprites each frame (an effect) until Off; Exit On or Exit Off.
 */
static int bb_planar_filter(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Statistics (Visuals): the last frame's statistics the settings ask for,
 * each frame while on; On and Off turn the profiling on and off. The frame
 * rate comes from the frame's length; the engine measures no other time
 * (the interface time is the whole frame, as CK2 estimates it) and draws
 * nothing.
 */
static int bb_statistics(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        engine->profiling = true;
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        engine->profiling = false;
        return NMO_ENGINE_BR_OK;
    }
    const eng_stats_t *stats = &engine->profile_stats;
    int32_t flags = 1;
    nmo_engine_block_get_local_value(b, 0, &flags, sizeof(flags));
    if (flags != 0) {
        float fps = 1000.0f / stats->frame_time;
        set_out(b, 0, &fps, sizeof(fps));
    }
    /* General Time: interface, then processing, rendering, parametric operations,
       animation, IK, total behavior and behavior code time */
    flags = 0xFF;
    nmo_engine_block_get_local_value(b, 1, &flags, sizeof(flags));
    for (int i = 0; i < 8; i++) {
        if ((flags & (1 << i)) != 0) {
            float time = i == 0 ? stats->frame_time : 0.0f;
            set_out(b, 1 + i, &time, sizeof(time));
        }
    }
    flags = 0x1F;
    nmo_engine_block_get_local_value(b, 2, &flags, sizeof(flags));
    const int32_t behavioral[5] = {stats->active_objects_executed, stats->behaviors_executed,
                                   stats->building_blocks_executed, stats->links_parsed,
                                   stats->links_delayed};
    for (int i = 0; i < 5; i++) {
        if ((flags & (1 << i)) != 0) {
            set_out(b, 9 + i, &behavioral[i], sizeof(behavioral[i]));
        }
    }
    flags = 0x1F;
    nmo_engine_block_get_local_value(b, 3, &flags, sizeof(flags));
    for (int i = 0; i < 5; i++) {
        if ((flags & (1 << i)) != 0) {
            const int32_t none = 0;
            set_out(b, 14 + i, &none, sizeof(none));
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Time Settings: the time manager's rates and limits (an effect on the scene). */
static int bb_time_settings(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_tt_blocks[] = {
    {NMO_GUID(0x09b335b3, 0x12d17cdc), "TT_LinearVolume", bb_linear_volume, NULL, NULL},
    {NMO_GUID(0x7e212b2f, 0x24db67c6), "TT_ReflectionMapping", bb_reflection_mapping, NULL, NULL},
    {NMO_GUID(0x416b4f0e, 0x192d339a), "TT_PreloadTextures", bb_preload_textures, NULL, NULL},
    {NMO_GUID(0x1436624f, 0x34e1290a), "TT Set Database Properties", bb_set_database_properties,
     NULL, NULL},
    {NMO_GUID(0x05441494, 0x38ac7789), "TT Load Database", bb_load_database, NULL, NULL},
    {NMO_GUID(0x5d303e9d, 0x552c0af2), "TT Save Database", bb_save_database, NULL, NULL},
    {NMO_GUID(0x348773dc, 0x19ae6322), "TT Register Array", bb_register_array, NULL, NULL},
    {NMO_GUID(0x4a446c43, 0x66fa2375), "TT_Debug", bb_tt_debug, NULL, NULL},
    {NMO_GUID(0x3d00718f, 0x2c8b33a2), "TT_DebugON", bb_debug_on, NULL, NULL},
    {NMO_GUID(0x5bc50af4, 0x41e92536), "TT Exit to System", bb_exit_to_system, NULL, NULL},
    {NMO_GUID(0x4c6513e9, 0x20e11177), "TT GetEnvironmentVariable", bb_get_environment_variable,
     NULL, NULL},
    {NMO_GUID(0x556e6df3, 0x24207d37), "TT_GetCurrentDirectory", bb_get_current_directory, NULL,
     NULL},
    {NMO_GUID(0x524a6bcb, 0x66f67774), "TT_ReplacePath", bb_replace_path, NULL, NULL},
    {NMO_GUID(0x38c34dab, 0x5dd03bdf), "TT_SetMipMapping", bb_set_mip_mapping, NULL, NULL},
    {NMO_GUID(0x7c663b40, 0x2f4b347e), "TT_TextureInfo", bb_texture_info, NULL, NULL},
    {NMO_GUID(0x0694658d, 0x7ffd236f), "TT Player Active", bb_player_active, NULL, NULL},
    {NMO_GUID(0x3c392558, 0x419d2680), "TT Set_2DSprite", bb_set_2d_sprite, NULL, NULL},
    {NMO_GUID(0x4c94621d, 0x24fe2cf3), "TT OperationSystem", bb_operation_system, NULL, NULL},
    {NMO_GUID(0x3b826e04, 0x6e764285), "TT GetMemoryStatus", bb_get_memory_status, NULL, NULL},
    {NMO_GUID(0x62d00456, 0x30eb4245), "TT List Driver", bb_list_driver, NULL, NULL},
    {NMO_GUID(0x4e7a0194, 0x040328fd), "TT List ScreenModes", bb_list_screen_modes, NULL, NULL},
    {NMO_GUID(0x460044b5, 0x6e927b66), "TT_ReadRegistry", bb_read_registry, NULL, NULL},
    {NMO_GUID(0x38b84d97, 0x13932f28), "TT Change ScreenMode", bb_change_screen_mode, NULL, NULL},
    {NMO_GUID(0x060a0f8c, 0x09203b5f), "Time Settings", bb_time_settings, NULL, NULL},
    {NMO_GUID(0x6ac67901, 0x7d2a6059), "TT_Timer", bb_tt_timer, NULL, NULL},
    {NMO_GUID(0x5ae74e2f, 0x799c49b5), "TT_SplitString", bb_split_string, NULL, NULL},
    {NMO_GUID(0x36691920, 0x3b261630), "TT Sky", bb_sky, NULL, NULL},
    {NMO_GUID(0x14d325d1, 0x6748654e), "TT PushButton2", bb_push_button2, NULL, NULL},
    {NMO_GUID(0x750f20fc, 0x3741186d), "TT_Counter", bb_tt_counter, NULL, NULL},
    {NMO_GUID(0x18f96977, 0x18e20f83), "TT ConvertPixel-Homogen", bb_convert_pixel_homogen, NULL,
     NULL},
    {NMO_GUID(0x00cd320b, 0x32ed010b), "Planar Filter", bb_planar_filter, NULL, NULL},
    {NMO_GUID(0x5fb70201, 0x65595af3), "Statistics", bb_statistics, NULL, NULL},
};

nmo_status_t eng_register_tt(nmo_engine_t *engine)
{
    engine->database_crypted = true;
    engine->tt_timing = true;
    for (size_t i = 0; i < sizeof(eng_tt_blocks) / sizeof(eng_tt_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_tt_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
