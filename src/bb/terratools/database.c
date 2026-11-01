/**
 * @file database.c
 * @brief The TT Database Manager and its building blocks.
 *
 * Each function does what the block's function in TT_DatabaseManager_RT.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ). The manager reads arrays from the database file through the
 * host's file resolver and writes them through its file writer, scrambled as
 * the original does.
 */

#include "../bb_sdk.h"
#include "terratools.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * TT Database Manager
 * ============================================================================ */

/* DatabaseManager::Clear: no array registered, no database file */
static void database_clear(nmo_engine_t *engine)
{
    free(eng_tt(engine)->database_file);
    eng_tt(engine)->database_file = NULL;
    for (size_t i = 0; i < eng_tt(engine)->database_arrays.count; i++) {
        free(eng_tt(engine)->database_arrays.data[i]);
    }
    free(eng_tt(engine)->database_arrays.data);
    eng_tt(engine)->database_arrays.data = NULL;
    eng_tt(engine)->database_arrays.count = eng_tt(engine)->database_arrays.capacity = 0;
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

/* DatabaseManager::Register: an array to save, once; 21 when it is already there */
static int database_register(nmo_engine_t *engine, const char *array_name)
{
    for (size_t i = 0; i < eng_tt(engine)->database_arrays.count; i++) {
        if (strcmp(eng_tt(engine)->database_arrays.data[i], array_name) == 0) {
            return 21;
        }
    }
    char **slot = NULL;
    size_t size = strlen(array_name) + 1u;
    char *copy = (char *)malloc(size);
    if (copy != NULL) {
        memcpy(copy, array_name, size);
    }
    if (copy == NULL || !ENG_VEC_PUSH(eng_tt(engine)->database_arrays, slot)) {
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
    if (eng_tt(engine)->database_file == NULL || eng_tt(engine)->database_file[0] == '\0') {
        return 31;
    }
    size_t size = 0;
    unsigned char *data = eng_read_file(engine, eng_tt(engine)->database_file, &size);
    if (data == NULL) {
        return 31;
    }
    if (eng_tt(engine)->database_crypted) {
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
    free(eng_tt(engine)->database_file);
    eng_tt(engine)->database_file = copy;
    eng_tt(engine)->database_crypted = get_in_int(b, 1, 1) != 0;
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
    if (eng_tt(engine)->database_file == NULL || eng_tt(engine)->database_file[0] == '\0') {
        return 41;
    }
    database_bytes_t file = {NULL, 0u, 0u};
    for (size_t n = 0; n < eng_tt(engine)->database_arrays.count; n++) {
        const char *array_name = eng_tt(engine)->database_arrays.data[n];
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
    if (eng_tt(engine)->database_crypted) {
        for (size_t i = 0; i < file.size; i++) {
            unsigned char x = (unsigned char)((unsigned char)-file.data[i] ^ 0xAFu);
            file.data[i] = (unsigned char)((x >> 3) | (x << 5));
        }
    }
    nmo_status_t status = engine->host.write_file != NULL
                              ? engine->host.write_file(engine->host.write_file_user_data,
                                                   eng_tt(engine)->database_file, file.data, file.size)
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

static const nmo_engine_block_desc_t eng_tt_database_blocks[] = {
    {NMO_GUID_INIT(0x1436624f, 0x34e1290a), "TT Set Database Properties", bb_set_database_properties,
     NULL, NULL},
    {NMO_GUID_INIT(0x05441494, 0x38ac7789), "TT Load Database", bb_load_database, NULL, NULL},
    {NMO_GUID_INIT(0x5d303e9d, 0x552c0af2), "TT Save Database", bb_save_database, NULL, NULL},
    {NMO_GUID_INIT(0x348773dc, 0x19ae6322), "TT Register Array", bb_register_array, NULL, NULL},
};

static void tt_manager_destroy(nmo_engine_t *engine, void *state)
{
    eng_tt_t *tt = (eng_tt_t *)state;
    database_clear(engine);
    eng_free_emitters(engine);
    free(tt->skies.data);
    free(tt->shadows.data);
    free(tt->video_textures.slots);
}

/* The Terratools time, database and debug managers, and what TT Sky, TT Simple Shadow,
   TT_PreloadTextures and the particle systems keep */
static const eng_manager_desc_t tt_manager = {
    .name = "TT Managers",
    .state_size = sizeof(eng_tt_t),
    .destroy = tt_manager_destroy,
};

eng_tt_t *eng_tt(const nmo_engine_t *engine)
{
    return (eng_tt_t *)eng_manager_state(engine, &tt_manager);
}

nmo_status_t eng_tt_add(nmo_engine_t *engine)
{
    if (eng_tt(engine) != NULL) {
        return NMO_OK;
    }
    eng_tt_t *tt = NULL;
    nmo_status_t status = eng_add_manager(engine, &tt_manager, (void **)&tt);
    if (status == NMO_OK) {
        tt->database_crypted = true;
    }
    return status;
}

nmo_status_t eng_register_tt_database(nmo_engine_t *engine)
{
    nmo_status_t status = eng_tt_add(engine);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_tt_database_blocks) : status;
}
