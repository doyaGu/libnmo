#include "engine_fixture.h"

#include "engine/nmo_engine_render.h"
#include "type/nmo_type_guids.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "../../src/engine/engine_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_SET_DATABASE_PROPERTIES NMO_GUID(0x1436624f, 0x34e1290a)
#define GUID_LOAD_DATABASE NMO_GUID(0x05441494, 0x38ac7789)
#define GUID_SAVE_DATABASE NMO_GUID(0x5d303e9d, 0x552c0af2)
#define GUID_READ_REGISTRY NMO_GUID(0x460044b5, 0x6e927b66)
#define GUID_LIST_DRIVER NMO_GUID(0x62d00456, 0x30eb4245)
#define GUID_LIST_SCREEN_MODES NMO_GUID(0x4e7a0194, 0x040328fd)
#define GUID_TT_TIMER NMO_GUID(0x6ac67901, 0x7d2a6059)
#define GUID_SPLIT_STRING NMO_GUID(0x5ae74e2f, 0x799c49b5)
#define GUID_LOAD_STRING NMO_GUID(0x391555d6, 0x42f2500e)
#define GUID_LINEAR_VOLUME NMO_GUID(0x09b335b3, 0x12d17cdc)
#define GUID_PRELOAD_TEXTURES NMO_GUID(0x416b4f0e, 0x192d339a)
#define GUID_CHANGE_SCREEN_MODE NMO_GUID(0x38b84d97, 0x13932f28)
#define GUID_CONVERT_PIXEL NMO_GUID(0x18f96977, 0x18e20f83)
#define GUID_PLANAR_FILTER NMO_GUID(0x00cd320b, 0x32ed010b)
#define GUID_TT_COUNTER NMO_GUID(0x750f20fc, 0x3741186d)
#define GUID_REGISTER_ARRAY NMO_GUID(0x348773dc, 0x19ae6322)
#define GUID_TT_DEBUG NMO_GUID(0x4a446c43, 0x66fa2375)
#define GUID_DEBUG_ON NMO_GUID(0x3d00718f, 0x2c8b33a2)
#define GUID_EXIT_TO_SYSTEM NMO_GUID(0x5bc50af4, 0x41e92536)
#define GUID_GET_ENVIRONMENT_VARIABLE NMO_GUID(0x4c6513e9, 0x20e11177)
#define GUID_GET_CURRENT_DIRECTORY NMO_GUID(0x556e6df3, 0x24207d37)
#define GUID_REPLACE_PATH NMO_GUID(0x524a6bcb, 0x66f67774)
#define GUID_STATISTICS NMO_GUID(0x5fb70201, 0x65595af3)
#define GUID_CONVERT_BOX NMO_GUID(0x694867ff, 0x7db16d1f)
#define GUID_FIND_SUBSTRING NMO_GUID(0x12f31e27, 0x4cea0bf5)
#define GUID_GET_FULL_ANGLE NMO_GUID(0x7a18014d, 0x009d2e73)
#define GUID_CONVERT_TIME_TO_STRING NMO_GUID(0x69e34443, 0x459f23db)
#define GUID_PARAMETER_MAPPER NMO_GUID(0x033e30a1, 0x42f452fc)
#define GUID_CHRONO NMO_GUID(0x325d722d, 0x3d9e16e9)
#define GUID_CHILDREN_ITERATOR NMO_GUID(0x78041c90, 0x16de3cde)
#define GUID_WRITE_BACK NMO_GUID(0x78b31127, 0x0a875030)
#define GUID_GET_CURVE_POINT NMO_GUID(0x50eb3a17, 0x015c5bf9)
#define GUID_GET_NEAREST_CURVE_POSITION NMO_GUID(0x04db6e14, 0x2c151f73)
#define GUID_BOUNDING_SPHERE NMO_GUID(0x32a6308d, 0x1d3d0931)
#define GUID_CHECK_CURVE_COLLISION NMO_GUID(0x7e9a4415, 0x199e4eb3)
#define GUID_HOMING_MISSILE NMO_GUID(0x44d43591, 0x28f91084)
#define GUID_FOLLOW_CAMERA NMO_GUID(0x595b4723, 0x576c6749)
#define GUID_GENERATE_PLANE NMO_GUID(0x5ae72f89, 0x57eb5413)
#define GUID_HIGH_MAP_MESHFORM NMO_GUID(0x2e3c2d7e, 0x7be7234e)
#define GUID_MAGNET_MESH_FORM NMO_GUID(0x08e53929, 0x4c9c5205)
#define GUID_SINUS_MESHDEFORM NMO_GUID(0x4f907f12, 0x16af15d6)
#define GUID_PMS_MESHDEFORM NMO_GUID(0x38b87d43, 0x149b755b)
#define GUID_RIPPLE_WAVE NMO_GUID(0x2f6a47b3, 0x35f4755c)
#define GUID_SET_MAPPING NMO_GUID(0x2e36319f, 0x6ec6031f)
#define GUID_REFRACTION_MAPPING NMO_GUID(0x53024810, 0x22ed280f)
#define GUID_BUMP_MAP NMO_GUID(0x630d05f7, 0x1dda541f)
#define GUID_CEL_SHADING NMO_GUID(0x2d2d2a64, 0x5a0501c7)
#define GUID_ANISO_MAPPING NMO_GUID(0x73b30b5e, 0x4fed4c65)
#define GUID_SAVE_ALPHA NMO_GUID(0x5ca62d31, 0x5d2031b8)
#define GUID_SET_MATERIAL_VALUES NMO_GUID(0x62963fa9, 0x7258745c)

/* The files the engine loads, from the scratch directory */
static const char *resolve_scratch(void *user_data, const char *name)
{
    static char path[512];
    (void)user_data;
    int length = snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    return length > 0 && (size_t)length < sizeof(path) ? path : NULL;
}

static void input_text(engine_fixture_t *f, nmo_object_id_t pin, const char *text)
{
    fixture_input_value(f, pin, text, strlen(text) + 1u);
}

/* Bytes of a database file */
typedef struct tdb {
    unsigned char data[256];
    size_t size;
} tdb_t;

static void tdb_bytes(tdb_t *t, const void *bytes, size_t size)
{
    ASSERT_TRUE(t->size + size <= sizeof(t->data));
    memcpy(t->data + t->size, bytes, size);
    t->size += size;
}

static void tdb_int(tdb_t *t, int32_t value)
{
    tdb_bytes(t, &value, sizeof(value));
}

static void tdb_text(tdb_t *t, const char *text)
{
    tdb_bytes(t, text, strlen(text) + 1u);
}

/* The "Scores" array as the database file holds it */
static void scores_table(tdb_t *array)
{
    tdb_int(array, 3); /* columns */
    tdb_int(array, 2); /* rows */
    tdb_int(array, 1); /* key column */
    tdb_text(array, "Name");
    tdb_int(array, CKARRAYTYPE_STRING);
    tdb_text(array, "Points");
    tdb_int(array, CKARRAYTYPE_INT);
    tdb_text(array, "Ratio");
    tdb_int(array, CKARRAYTYPE_FLOAT);
    tdb_text(array, "Ann"); /* column by column */
    tdb_text(array, "Bob");
    tdb_int(array, 4200);
    tdb_int(array, 3100);
    const float ratios[2] = {0.5f, 0.25f};
    tdb_bytes(array, ratios, sizeof(ratios));
}

/* The reader turns b into -(rotl(b, 3) ^ 0xAF); this is its inverse */
static void scramble(tdb_t *t)
{
    for (size_t i = 0; i < t->size; i++) {
        unsigned char x = (unsigned char)((unsigned char)-t->data[i] ^ 0xAFu);
        t->data[i] = (unsigned char)((x >> 3) | (x << 5));
    }
}

/* The database file TT Database Manager reads: another chunk, then "Scores", scrambled */
static void write_database(const char *name)
{
    tdb_t array = {0};
    scores_table(&array);

    tdb_t file = {0};
    tdb_text(&file, "Other"); /* a chunk before it */
    tdb_int(&file, 4);
    tdb_int(&file, 0);
    tdb_text(&file, "Scores");
    tdb_int(&file, (int32_t)array.size);
    tdb_bytes(&file, array.data, array.size);
    scramble(&file);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    FILE *out = fopen(path, "wb");
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(file.size, fwrite(file.data, 1, file.size, out));
    fclose(out);
}

static int32_t cell_int(engine_fixture_t *f, nmo_object_id_t array, int32_t row, int32_t column)
{
    nmo_engine_cell_t cell;
    return nmo_engine_array_get_cell(f->engine, array, row, column, &cell) == NMO_OK
               ? (int32_t)cell.value
               : -1;
}

TEST(engine_tt, load_database_fills_the_array_of_its_name)
{
    write_database("tt.tdb");
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t scores = fixture_create(&f, NMO_CID_DATAARRAY, "Scores");
    ASSERT_TRUE(scores != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t set = fixture_block(&f, GUID_SET_DATABASE_PROPERTIES, "Set");
    nmo_object_id_t load = fixture_block(&f, GUID_LOAD_DATABASE, "Load");
    nmo_object_id_t missing = fixture_block(&f, GUID_LOAD_DATABASE, "Missing");
    nmo_object_id_t loaded = fixture_block(&f, GUID_NOP, "Loaded");
    nmo_object_id_t failed = fixture_block(&f, GUID_NOP, "Failed");
    ASSERT_TRUE(set != 0 && load != 0 && missing != 0 && loaded != 0 && failed != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, set, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, set, 0), BLOCK_IN(&f, load, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, load, 0), BLOCK_IN(&f, loaded, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, loaded, 0), BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, failed, 0), 0);
    input_text(&f, BLOCK_PIN(&f, set, 0), "tt.tdb");
    input_text(&f, BLOCK_PIN(&f, load, 1), "Scores");
    input_text(&f, BLOCK_PIN(&f, missing, 1), "Nothing");
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, loaded));
    ASSERT_EQ(1u, runs(&f, failed));
    ASSERT_EQ(3, nmo_engine_array_column_count(f.engine, scores));
    ASSERT_EQ(2, nmo_engine_array_row_count(f.engine, scores));
    ASSERT_EQ(CKARRAYTYPE_STRING, nmo_engine_array_column_type(f.engine, scores, 0));
    ASSERT_EQ(CKARRAYTYPE_FLOAT, nmo_engine_array_column_type(f.engine, scores, 2));
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, scores, 1, 0, &cell));
    ASSERT_STR_EQ("Bob", cell.text);
    ASSERT_EQ(4200, cell_int(&f, scores, 0, 1));
    float ratio = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, scores, 1, 2, &cell));
    memcpy(&ratio, &cell.value, sizeof(ratio));
    ASSERT_FLOAT_EQ(0.25f, ratio, 0.0001f);
    fixture_end(&f);
}

/* A host writer keeping what was written */
typedef struct written {
    char name[64];
    unsigned char data[512];
    size_t size;
    int count;
} written_t;

static nmo_status_t keep_written(void *user_data, const char *name, const void *data, size_t size)
{
    written_t *w = (written_t *)user_data;
    if (size > sizeof(w->data) || strlen(name) >= sizeof(w->name)) {
        return NMO_ERR_BUFFER_OVERRUN;
    }
    memcpy(w->name, name, strlen(name) + 1u);
    memcpy(w->data, data, size);
    w->size = size;
    w->count++;
    return NMO_OK;
}

TEST(engine_tt, save_database_writes_the_registered_arrays)
{
    write_database("save.tdb");
    for (int with_writer = 0; with_writer < 2; with_writer++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        ASSERT_TRUE(fixture_create(&f, NMO_CID_DATAARRAY, "Scores") != 0);
        f.script_id = fixture_add_script(&f, "Script", &f.script_in);
        fixture_begin_edit(&f);
        /* Loaded with Auto Register (its default), then saved */
        nmo_object_id_t set = fixture_block(&f, GUID_SET_DATABASE_PROPERTIES, "Set");
        nmo_object_id_t load = fixture_block(&f, GUID_LOAD_DATABASE, "Load");
        nmo_object_id_t save = fixture_block(&f, GUID_SAVE_DATABASE, "Save");
        nmo_object_id_t saved = fixture_block(&f, GUID_NOP, "Saved");
        nmo_object_id_t failed = fixture_block(&f, GUID_NOP, "Failed");
        ASSERT_TRUE(set != 0 && load != 0 && save != 0 && saved != 0 && failed != 0);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, set, 0), 0);
        fixture_link(&f, BLOCK_OUT(&f, set, 0), BLOCK_IN(&f, load, 0), 0);
        fixture_link(&f, BLOCK_OUT(&f, load, 0), BLOCK_IN(&f, save, 0), 0);
        fixture_link(&f, BLOCK_OUT(&f, save, 0), BLOCK_IN(&f, saved, 0), 0);
        fixture_link(&f, BLOCK_OUT(&f, save, 1), BLOCK_IN(&f, failed, 0), 0);
        input_text(&f, BLOCK_PIN(&f, set, 0), "save.tdb");
        input_text(&f, BLOCK_PIN(&f, load, 1), "Scores");
        fixture_commit(&f, NULL);
        nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
        written_t w;
        memset(&w, 0, sizeof(w));
        if (with_writer) {
            nmo_engine_set_file_writer(f.engine, keep_written, &w);
        }

        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        if (!with_writer) {
            /* Nowhere to write: an unwritable file */
            ASSERT_EQ(1u, runs(&f, failed));
            ASSERT_EQ(0u, runs(&f, saved));
            fixture_end(&f);
            continue;
        }
        ASSERT_EQ(1u, runs(&f, saved));
        ASSERT_EQ(1, w.count);
        ASSERT_STR_EQ("save.tdb", w.name);
        /* Only the registered array, as it was read: names, types, cells column by column */
        tdb_t array = {0};
        scores_table(&array);
        tdb_t expected = {0};
        tdb_text(&expected, "Scores");
        tdb_int(&expected, (int32_t)array.size);
        tdb_bytes(&expected, array.data, array.size);
        scramble(&expected);
        ASSERT_EQ(expected.size, w.size);
        ASSERT_TRUE(memcmp(expected.data, w.data, w.size) == 0);
        fixture_end(&f);
    }
}

TEST(engine_tt, register_array_registers_once_and_clear_forgets)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = fixture_create(&f, NMO_CID_DATAARRAY, "A");
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t set = fixture_block(&f, GUID_SET_DATABASE_PROPERTIES, "Set");
    nmo_object_id_t once = fixture_block(&f, GUID_REGISTER_ARRAY, "Register");
    nmo_object_id_t twice = fixture_block(&f, GUID_REGISTER_ARRAY, "Register Again");
    nmo_object_id_t save = fixture_block(&f, GUID_SAVE_DATABASE, "Save");
    nmo_object_id_t clear = fixture_block(&f, GUID_REGISTER_ARRAY, "Clear");
    nmo_object_id_t save_cleared = fixture_block(&f, GUID_SAVE_DATABASE, "Save Cleared");
    nmo_object_id_t failed = fixture_block(&f, GUID_NOP, "Failed");
    ASSERT_TRUE(set != 0 && once != 0 && twice != 0 && save != 0 && clear != 0 &&
                save_cleared != 0 && failed != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, set, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, set, 0), BLOCK_IN(&f, once, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, once, 0), BLOCK_IN(&f, twice, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, twice, 1), BLOCK_IN(&f, save, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, save, 0), BLOCK_IN(&f, clear, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, clear, 0), BLOCK_IN(&f, save_cleared, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, save_cleared, 1), BLOCK_IN(&f, failed, 0), 0);
    input_text(&f, BLOCK_PIN(&f, set, 0), "registered.tdb");
    fixture_input_int(&f, BLOCK_PIN(&f, once, 0), (int32_t)array);
    fixture_input_int(&f, BLOCK_PIN(&f, twice, 0), (int32_t)array);
    fixture_commit(&f, NULL);
    written_t w;
    memset(&w, 0, sizeof(w));
    nmo_engine_set_file_writer(f.engine, keep_written, &w);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Registered once (the second is an Error): one chunk, the array "A" */
    ASSERT_EQ(1, w.count);
    ASSERT_STR_EQ("registered.tdb", w.name);
    ASSERT_TRUE(w.size > 6);
    unsigned char plain[512];
    for (size_t i = 0; i < w.size; i++) {
        unsigned char r = (unsigned char)((w.data[i] << 3) | (w.data[i] >> 5));
        plain[i] = (unsigned char)(0u - (r ^ 0xAFu));
    }
    ASSERT_TRUE(memcmp(plain, "A", 2) == 0);
    int32_t chunk_size = 0;
    memcpy(&chunk_size, plain + 2, sizeof(chunk_size));
    ASSERT_EQ(w.size, 6u + (size_t)chunk_size);
    /* Cleared: no array and no file to save to */
    ASSERT_EQ(1u, runs(&f, failed));
    ASSERT_EQ(1, w.count);
    fixture_end(&f);
}

TEST(engine_tt, system_values_come_from_the_host)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t variable = fixture_block(&f, GUID_GET_ENVIRONMENT_VARIABLE, "Variable");
    nmo_object_id_t unset = fixture_block(&f, GUID_GET_ENVIRONMENT_VARIABLE, "Unset");
    nmo_object_id_t directory = fixture_block(&f, GUID_GET_CURRENT_DIRECTORY, "Directory");
    nmo_object_id_t replace = fixture_block(&f, GUID_REPLACE_PATH, "Replace");
    nmo_object_id_t no_category = fixture_block(&f, GUID_REPLACE_PATH, "No Category");
    nmo_object_id_t not_found = fixture_block(&f, GUID_NOP, "Not Found");
    nmo_object_id_t replaced = fixture_block(&f, GUID_NOP, "Replaced");
    nmo_object_id_t failed = fixture_block(&f, GUID_NOP, "Failed");
    ASSERT_TRUE(variable != 0 && unset != 0 && directory != 0 && replace != 0 &&
                no_category != 0 && not_found != 0 && replaced != 0 && failed != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, variable, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, variable, 0), BLOCK_IN(&f, unset, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, unset, 1), BLOCK_IN(&f, not_found, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, not_found, 0), BLOCK_IN(&f, directory, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, directory, 0), BLOCK_IN(&f, replace, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, replace, 0), BLOCK_IN(&f, replaced, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, replaced, 0), BLOCK_IN(&f, no_category, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, no_category, 1), BLOCK_IN(&f, failed, 0), 0);
    input_text(&f, BLOCK_PIN(&f, variable, 0), "gravity");
    input_text(&f, BLOCK_PIN(&f, unset, 0), "Missing");
    input_text(&f, BLOCK_PIN(&f, replace, 0), "\\Sounds\\");
    input_text(&f, BLOCK_PIN(&f, replace, 1), "\\Sounds_low\\");
    input_text(&f, BLOCK_PIN(&f, replace, 2), "C:\\Game");
    fixture_input_int(&f, BLOCK_PIN(&f, replace, 3), NMO_ENGINE_PATH_SOUND);
    fixture_input_int(&f, BLOCK_PIN(&f, no_category, 3), 3);
    fixture_commit(&f, NULL);
    /* Names compare without case, as Windows compares them */
    ASSERT_EQ(NMO_OK, nmo_engine_set_environment_variable(f.engine, "Gravity", "C:\\Game\\"));
    ASSERT_EQ(NMO_OK, nmo_engine_set_current_directory(f.engine, "C:\\Game\\Bin"));
    ASSERT_EQ(NMO_OK, nmo_engine_add_path(f.engine, NMO_ENGINE_PATH_SOUND, "C:\\Game\\Sounds\\"));
    ASSERT_EQ(NMO_OK, nmo_engine_add_path(f.engine, NMO_ENGINE_PATH_SOUND, "C:\\Game\\Music\\"));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    const void *text = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, variable, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("C:\\Game\\", (const char *)text);
    ASSERT_EQ(1u, runs(&f, not_found));
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, directory, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("C:\\Game\\Bin", (const char *)text);
    /* The original renames the first path that is not the old one */
    ASSERT_EQ(1u, runs(&f, replaced));
    ASSERT_STR_EQ("C:\\Game\\Sounds\\", nmo_engine_path(f.engine, NMO_ENGINE_PATH_SOUND, 0));
    ASSERT_STR_EQ("C:\\Game\\Sounds_low\\", nmo_engine_path(f.engine, NMO_ENGINE_PATH_SOUND, 1));
    ASSERT_NULL(nmo_engine_path(f.engine, NMO_ENGINE_PATH_SOUND, 2));
    ASSERT_EQ(1u, runs(&f, failed));
    fixture_end(&f);
}

TEST(engine_tt, debug_mode_and_exit_to_system)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t before = fixture_block(&f, GUID_TT_DEBUG, "Before");
    nmo_object_id_t debug_on = fixture_block(&f, GUID_DEBUG_ON, "Debug On");
    nmo_object_id_t after = fixture_block(&f, GUID_TT_DEBUG, "After");
    nmo_object_id_t exit_block = fixture_block(&f, GUID_EXIT_TO_SYSTEM, "Exit");
    ASSERT_TRUE(before != 0 && debug_on != 0 && after != 0 && exit_block != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, before, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, before, 0), BLOCK_IN(&f, debug_on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, debug_on, 0), BLOCK_IN(&f, after, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, after, 0), BLOCK_IN(&f, exit_block, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, before, 3), 1); /* Enable */
    fixture_input_int(&f, BLOCK_PIN(&f, after, 3), 1);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_FALSE(nmo_engine_exit_requested(f.engine));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Not in debug mode the first keeps its input; the second writes and takes it */
    ASSERT_TRUE(nmo_engine_io_is_active(f.engine, BLOCK_IN(&f, before, 0)));
    ASSERT_FALSE(nmo_engine_io_is_active(f.engine, BLOCK_IN(&f, after, 0)));
    ASSERT_EQ(1u, runs(&f, exit_block));
    ASSERT_TRUE(nmo_engine_exit_requested(f.engine));
    fixture_end(&f);
}

TEST(engine_tt, statistics_reports_the_last_frame)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t stats = fixture_block(&f, GUID_STATISTICS, "Statistics");
    ASSERT_TRUE(stats != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, stats, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* Each frame runs the script and the block, and parses the script's link; from the
     * second frame on the profiling counts the object too */
    const int32_t objects[2] = {0, 1};
    for (int frame = 0; frame < 2; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        float fps = 0.0f;
        read_value(&f, BLOCK_POUT(&f, stats, 0), &fps, sizeof(fps));
        ASSERT_FLOAT_EQ(50.0f, fps, 0.0001f);
        float interface_time = 0.0f;
        read_value(&f, BLOCK_POUT(&f, stats, 1), &interface_time, sizeof(interface_time));
        ASSERT_FLOAT_EQ(20.0f, interface_time, 0.0001f);
        if (frame == 0) {
            continue; /* the counts of the frame before the first */
        }
        const int32_t expected[5] = {objects[frame - 1], 2, 1, 1, 0};
        for (int i = 0; i < 5; i++) {
            int32_t count = -1;
            read_value(&f, BLOCK_POUT(&f, stats, 9 + i), &count, sizeof(count));
            ASSERT_EQ(expected[i], count);
        }
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    int32_t active_objects = -1;
    read_value(&f, BLOCK_POUT(&f, stats, 9), &active_objects, sizeof(active_objects));
    ASSERT_EQ(objects[1], active_objects);
    fixture_end(&f);
}

TEST(engine_tt, read_registry_reads_what_the_host_set)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t found = fixture_block(&f, GUID_READ_REGISTRY, "Found");
    nmo_object_id_t missing = fixture_block(&f, GUID_READ_REGISTRY, "Missing");
    nmo_object_id_t ok = fixture_block(&f, GUID_NOP, "OK");
    nmo_object_id_t failed = fixture_block(&f, GUID_NOP, "Failed");
    ASSERT_TRUE(found != 0 && missing != 0 && ok != 0 && failed != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, found, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, found, 0), BLOCK_IN(&f, ok, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, ok, 0), BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, failed, 0), 0);
    input_text(&f, BLOCK_PIN(&f, found, 0), "Software\\Game\\");
    input_text(&f, BLOCK_PIN(&f, found, 1), "Language");
    input_text(&f, BLOCK_PIN(&f, missing, 0), "Software\\Game\\");
    input_text(&f, BLOCK_PIN(&f, missing, 1), "Volume");
    fixture_commit(&f, NULL);
    /* Keys and names compare without case, and without the key's trailing backslashes */
    ASSERT_EQ(NMO_OK, nmo_engine_set_registry_value(f.engine, "software\\game", "LANGUAGE", "1"));
    ASSERT_EQ(NMO_OK, nmo_engine_set_registry_value(f.engine, "Software\\Game", "Language", "3"));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    int32_t language = 0;
    read_value(&f, BLOCK_POUT(&f, found, 0), &language, sizeof(language));
    ASSERT_EQ(3, language);
    ASSERT_EQ(1u, runs(&f, ok));
    ASSERT_EQ(1u, runs(&f, failed));
    fixture_end(&f);
}

TEST(engine_tt, drivers_and_screen_modes_fill_their_arrays)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t drivers = fixture_create(&f, NMO_CID_DATAARRAY, "Drivers");
    nmo_object_id_t modes = fixture_create(&f, NMO_CID_DATAARRAY, "Modes");
    ASSERT_TRUE(drivers != 0 && modes != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t list_drivers = fixture_block(&f, GUID_LIST_DRIVER, "Drivers");
    nmo_object_id_t list_modes = fixture_block(&f, GUID_LIST_SCREEN_MODES, "Modes");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(list_drivers != 0 && list_modes != 0 && done != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, list_drivers, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, list_drivers, 0), BLOCK_IN(&f, list_modes, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, list_modes, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, list_drivers, 0), (int32_t)drivers);
    fixture_input_int(&f, BLOCK_PIN(&f, list_modes, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, list_modes, 1), (int32_t)modes);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, done));
    /* DriverName, DriverID, DriverDesc; Mode, Width, Height, Bpp */
    ASSERT_EQ(1, nmo_engine_array_row_count(f.engine, drivers));
    ASSERT_EQ(CKARRAYTYPE_STRING, nmo_engine_array_column_type(f.engine, drivers, 0));
    ASSERT_EQ(CKARRAYTYPE_INT, nmo_engine_array_column_type(f.engine, drivers, 1));
    ASSERT_EQ(CKARRAYTYPE_STRING, nmo_engine_array_column_type(f.engine, drivers, 2));
    ASSERT_EQ(4, nmo_engine_array_column_count(f.engine, modes));
    int32_t rows = nmo_engine_array_row_count(f.engine, modes);
    ASSERT_TRUE(rows > 1);
    bool has_16 = false;
    for (int32_t r = 0; r < rows; r++) {
        ASSERT_EQ(r, cell_int(&f, modes, r, 0));
        ASSERT_EQ(cell_int(&f, modes, r, 1) * 3, cell_int(&f, modes, r, 2) * 4);
        has_16 = has_16 || cell_int(&f, modes, r, 3) == 16;
    }
    ASSERT_TRUE(has_16);
    fixture_end(&f);
}

TEST(engine_tt, timer_and_strings)
{
    FILE *text = fopen(NMO_TEST_SCRATCH_FILE("text.txt"), "wb");
    ASSERT_NOT_NULL(text);
    fputs("one\r\ntwo", text);
    fclose(text);
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t timer = fixture_block(&f, GUID_TT_TIMER, "Timer");
    nmo_object_id_t split = fixture_block(&f, GUID_SPLIT_STRING, "Split");
    nmo_object_id_t load = fixture_block(&f, GUID_LOAD_STRING, "Load");
    ASSERT_TRUE(timer != 0 && split != 0 && load != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, timer, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, split, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, split, 0), BLOCK_IN(&f, load, 0), 0);
    input_text(&f, BLOCK_PIN(&f, split, 0), "a,b,,c");
    input_text(&f, BLOCK_PIN(&f, split, 1), ",");
    input_text(&f, BLOCK_PIN(&f, load, 0), "text.txt");
    nmo_object_id_t parts[3];
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, split, NMO_SCRIPT_EDIT_PARAM_OUT,
                                                        CKPGUID_STRING, "Part", &parts[i]));
    }
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    /* The time manager's time with this frame's, each frame */
    float elapsed = 0.0f;
    read_value(&f, BLOCK_POUT(&f, timer, 0), &elapsed, sizeof(elapsed));
    ASSERT_FLOAT_EQ(60.0f, elapsed, 0.001f);
    /* Empty parts are skipped */
    int32_t found = 0;
    read_value(&f, BLOCK_POUT(&f, split, 0), &found, sizeof(found));
    ASSERT_EQ(3, found);
    const char *expected[3] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        const void *data = NULL;
        ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, parts[i], &data, NULL, NULL));
        ASSERT_STR_EQ(expected[i], (const char *)data);
    }
    /* Read as Windows reads a text file */
    const void *loaded = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, load, 0), &loaded, NULL, NULL));
    ASSERT_STR_EQ("one\ntwo", (const char *)loaded);
    fixture_end(&f);
}

TEST(engine_tt, linear_volume_maps_the_volume)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* 1 above 1, 0 at 0.01 or below, 0.02 * 50^x between */
    const float volumes[4] = {2.0f, 1.0f, 0.5f, 0.01f};
    const float linear[4] = {1.0f, 1.0f, 0.141421f, 0.0f};
    nmo_object_id_t blocks[4];
    for (int i = 0; i < 4; i++) {
        blocks[i] = fixture_block(&f, GUID_LINEAR_VOLUME, "Volume");
        ASSERT_TRUE(blocks[i] != 0);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, blocks[i], 0), 0);
        fixture_input_value(&f, BLOCK_PIN(&f, blocks[i], 0), &volumes[i], sizeof(volumes[i]));
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    for (int i = 0; i < 4; i++) {
        float value = -1.0f;
        read_value(&f, BLOCK_POUT(&f, blocks[i], 0), &value, sizeof(value));
        ASSERT_FLOAT_EQ(linear[i], value, 0.0001f);
    }
    fixture_end(&f);
}

TEST(engine_tt, preload_textures_counts_each_texture_once)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    ASSERT_TRUE(fixture_create(&f, NMO_CID_TEXTURE, "One") != 0);
    ASSERT_TRUE(fixture_create(&f, NMO_CID_TEXTURE, "Two") != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t first = fixture_block(&f, GUID_PRELOAD_TEXTURES, "First");
    nmo_object_id_t again = fixture_block(&f, GUID_PRELOAD_TEXTURES, "Again");
    ASSERT_TRUE(first != 0 && again != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, first, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, first, 0), BLOCK_IN(&f, again, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    int32_t moved = -1;
    read_value(&f, BLOCK_POUT(&f, first, 0), &moved, sizeof(moved));
    ASSERT_EQ(2, moved);
    read_value(&f, BLOCK_POUT(&f, again, 0), &moved, sizeof(moved));
    ASSERT_EQ(0, moved); /* in video memory already */
    fixture_end(&f);
}

TEST(engine_tt, convert_pixel_uses_the_screen_mode)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t change = fixture_block(&f, GUID_CHANGE_SCREEN_MODE, "Mode");
    nmo_object_id_t to_homogen = fixture_block(&f, GUID_CONVERT_PIXEL, "To Homogen");
    nmo_object_id_t to_pixel = fixture_block(&f, GUID_CONVERT_PIXEL, "To Pixel");
    ASSERT_TRUE(change != 0 && to_homogen != 0 && to_pixel != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, change, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, change, 0), BLOCK_IN(&f, to_homogen, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, to_homogen, 0), BLOCK_IN(&f, to_pixel, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, change, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, change, 1), 2); /* 800 x 600 x 16 */
    const float pixels[2] = {400.0f, 150.0f}, homogen[2] = {0.5f, 0.3333f};
    fixture_input_value(&f, BLOCK_PIN(&f, to_homogen, 0), pixels, sizeof(pixels));
    fixture_input_value(&f, BLOCK_PIN(&f, to_pixel, 0), homogen, sizeof(homogen));
    fixture_commit(&f, NULL);
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, to_pixel);
    ASSERT_TRUE(state != NULL && state->local_parameters.count > 0);
    const int32_t pixel_to_homogen = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&state->local_parameters, 0),
                          &pixel_to_homogen, sizeof(pixel_to_homogen)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    float x = -1.0f, y = -1.0f, v[2] = {-1.0f, -1.0f};
    read_value(&f, BLOCK_POUT(&f, to_homogen, 0), &x, sizeof(x));
    read_value(&f, BLOCK_POUT(&f, to_homogen, 1), &y, sizeof(y));
    read_value(&f, BLOCK_POUT(&f, to_homogen, 2), v, sizeof(v));
    ASSERT_FLOAT_EQ(0.5f, x, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, y, 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, v[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, v[1], 0.0001f);
    /* Pixels truncated */
    read_value(&f, BLOCK_POUT(&f, to_pixel, 2), v, sizeof(v));
    ASSERT_FLOAT_EQ(400.0f, v[0], 0.0001f);
    ASSERT_FLOAT_EQ(199.0f, v[1], 0.0001f);
    fixture_end(&f);
}

TEST(engine_tt, planar_filter_stays_on_until_off)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t filter = fixture_block(&f, GUID_PLANAR_FILTER, "Filter");
    nmo_object_id_t on = fixture_block(&f, GUID_NOP, "On");
    nmo_object_id_t off = fixture_block(&f, GUID_NOP, "Off");
    ASSERT_TRUE(filter != 0 && on != 0 && off != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, filter, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, filter, 1), 3);
    fixture_link(&f, BLOCK_OUT(&f, filter, 0), BLOCK_IN(&f, on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, filter, 1), BLOCK_IN(&f, off, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 2; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    ASSERT_EQ(1u, runs(&f, on));
    ASSERT_EQ(0u, runs(&f, off));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, filter));
    for (int frame = 0; frame < 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    ASSERT_EQ(1u, runs(&f, on));
    ASSERT_EQ(1u, runs(&f, off));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, filter));
    fixture_end(&f);
}

/* The draws of the frame now; -1 when it cannot be made */
static int32_t frame_draws(engine_fixture_t *f)
{
    nmo_engine_frame_t frame;
    return nmo_engine_render_frame(f->engine, &frame) == NMO_OK ? frame.draw_count : -1;
}

/* CKScene::ActivateScript and ActivateBeObject call the script's blocks back at once */
TEST(engine_tt, planar_filter_leaves_with_its_script_and_comes_back)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t filter = fixture_block(&f, GUID_PLANAR_FILTER, "Filter");
    ASSERT_TRUE(filter != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, filter, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int32_t before = frame_draws(&f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    int32_t drawn = frame_draws(&f);
    ASSERT_EQ(before + 1, drawn);

    /* Not after the next frame: the deactivation takes it out now */
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, false, false));
    ASSERT_EQ(before, frame_draws(&f));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, filter));
    ASSERT_FALSE(nmo_engine_parent_script_is_active(f.engine, filter));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, false));
    ASSERT_EQ(drawn, frame_draws(&f));
    ASSERT_TRUE(nmo_engine_parent_script_is_active(f.engine, filter));

    /* The owner's deactivation tells its scripts too, its activation brings the filter back */
    ASSERT_EQ(NMO_OK, nmo_engine_activate_object(f.engine, f.owner_id, false));
    ASSERT_EQ(before, frame_draws(&f));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_object(f.engine, f.owner_id, true));
    ASSERT_EQ(drawn, frame_draws(&f));
    fixture_end(&f);
}

TEST(engine_tt, counter_wraps_around_its_range)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t counter = fixture_block(&f, GUID_TT_COUNTER, "Counter");
    ASSERT_TRUE(counter != 0);
    /* Frame 0: initialized and counted down at once; frames 1 and 2: counted up */
    fixture_link(&f, f.script_in, BLOCK_IN(&f, counter, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, counter, 2), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, counter, 1), 1);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, counter, 1), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, counter, 0), 5); /* outside [0, 3]: Min */
    fixture_input_int(&f, BLOCK_PIN(&f, counter, 1), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, counter, 2), 3);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const int32_t expected[3] = {3, 0, 1}; /* 0 - 1 wraps to Max, Max + 1 to Min */
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        int32_t count = -1;
        read_value(&f, BLOCK_POUT(&f, counter, 0), &count, sizeof(count));
        ASSERT_EQ(expected[frame], count);
    }
    fixture_end(&f);
}

/* A block run from the script's start */
static nmo_object_id_t started_block(engine_fixture_t *f, nmo_guid_t guid, const char *name)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block != 0) {
        fixture_link(f, f->script_in, BLOCK_IN(f, block, 0), 0);
    }
    return block;
}

TEST(engine_tt, logic_blocks_convert_map_and_find)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t named = fixture_create(&f, NMO_CID_3DENTITY, "Cube Floor");
    ASSERT_TRUE(named != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t box = started_block(&f, GUID_CONVERT_BOX, "Box");
    nmo_object_id_t angle = started_block(&f, GUID_GET_FULL_ANGLE, "Angle");
    nmo_object_id_t time = started_block(&f, GUID_CONVERT_TIME_TO_STRING, "Time");
    nmo_object_id_t find = started_block(&f, GUID_FIND_SUBSTRING, "Find");
    nmo_object_id_t mapper = started_block(&f, GUID_PARAMETER_MAPPER, "Mapper");
    nmo_object_id_t mapped = fixture_block(&f, GUID_NOP, "Out0"), found = fixture_block(&f, GUID_NOP, "Found");
    ASSERT_TRUE(box && angle && time && find && mapper && mapped && found);
    const float min[3] = {1, 2, 3}, max[3] = {4, 5, 6}, a[2] = {1, 0}, c[2] = {0, 1}, ms = 75430.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, box, 0), min, sizeof(min));
    fixture_input_value(&f, BLOCK_PIN(&f, box, 1), max, sizeof(max));
    fixture_input_value(&f, BLOCK_PIN(&f, angle, 0), a, sizeof(a));
    fixture_input_value(&f, BLOCK_PIN(&f, angle, 1), c, sizeof(c));
    fixture_input_value(&f, BLOCK_PIN(&f, time, 0), &ms, sizeof(ms));
    fixture_input_int(&f, BLOCK_PIN(&f, find, 0), (int32_t)named);
    input_text(&f, BLOCK_PIN(&f, find, 1), "Floor");
    fixture_link(&f, BLOCK_OUT(&f, find, 1), BLOCK_IN(&f, found, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, mapper, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, mapper, 1), BLOCK_IN(&f, mapped, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    float bbox[6];
    read_value(&f, BLOCK_POUT(&f, box, 0), bbox, sizeof(bbox));
    ASSERT_FLOAT_EQ(4.0f, bbox[0], 0.0f); /* Max first, as a VxBbox */
    ASSERT_FLOAT_EQ(1.0f, bbox[3], 0.0f);
    float degrees = 0.0f;
    read_value(&f, BLOCK_POUT(&f, angle, 1), &degrees, sizeof(degrees));
    ASSERT_FLOAT_EQ(270.0f, degrees, 0.001f); /* a quarter turn the other way round */
    const void *text = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, time, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("01:15:43", (const char *)text);
    int32_t rank = 0;
    read_value(&f, BLOCK_POUT(&f, find, 0), &rank, sizeof(rank));
    ASSERT_EQ(1, rank);
    ASSERT_EQ(1u, runs(&f, found));
    ASSERT_EQ(1u, runs(&f, mapped)); /* 0 is the first output after None */
    fixture_end(&f);
}

TEST(engine_tt, chrono_counts_and_children_iterate)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = fixture_create(&f, NMO_CID_3DENTITY, "Parent");
    nmo_object_id_t kids[2] = {fixture_create(&f, NMO_CID_3DENTITY, "Kid A"), fixture_create(&f, NMO_CID_3DENTITY, "Kid B")};
    ASSERT_TRUE(parent && kids[0] && kids[1]);
    for (int i = 0; i < 2; i++) {
        ((nmo_3dentity_state_t *)fixture_state(&f, kids[i]))->parent = nmo_ref_from_id(parent);
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t chrono = started_block(&f, GUID_CHRONO, "Chrono");
    nmo_object_id_t iterator = started_block(&f, GUID_CHILDREN_ITERATOR, "Children");
    nmo_object_id_t each = fixture_block(&f, GUID_NOP, "Each"), done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(chrono && iterator && each && done);
    fixture_input_int(&f, fixture_target(&f, iterator), (int32_t)parent);
    fixture_link(&f, BLOCK_OUT(&f, iterator, 1), BLOCK_IN(&f, each, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, each, 0), BLOCK_IN(&f, iterator, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, iterator, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    float elapsed = 0.0f;
    read_value(&f, BLOCK_POUT(&f, chrono, 0), &elapsed, sizeof(elapsed));
    ASSERT_FLOAT_EQ(60.0f, elapsed, 0.001f); /* counted from On's frame */
    ASSERT_EQ(2u, runs(&f, each));
    ASSERT_EQ(1u, runs(&f, done));
    int32_t count = 0;
    nmo_object_id_t last = 0;
    read_value(&f, BLOCK_POUT(&f, iterator, 0), &count, sizeof(count));
    read_value(&f, BLOCK_POUT(&f, iterator, 1), &last, sizeof(last));
    ASSERT_EQ(2, count);
    ASSERT_EQ(kids[1], last);
    fixture_end(&f);
}

TEST(engine_tt, write_back_writes_into_the_source_read)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t write = started_block(&f, GUID_WRITE_BACK, "Write");
    ASSERT_TRUE(write != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, write, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, write, 1), 7);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    int32_t value = 0;
    read_value(&f, BLOCK_PIN(&f, write, 0), &value, sizeof(value));
    ASSERT_EQ(7, value);
    fixture_end(&f);
}

/* An entity of `cid` at the position */
static nmo_object_id_t placed(engine_fixture_t *f, int32_t cid, const char *name, float x, float y, float z)
{
    nmo_object_id_t id = fixture_create(f, cid, name);
    nmo_3dentity_state_t *state = id != 0 ? (nmo_3dentity_state_t *)fixture_state(f, id) : NULL;
    if (state == NULL) {
        return 0;
    }
    state->world_matrix[12] = x;
    state->world_matrix[13] = y;
    state->world_matrix[14] = z;
    return id;
}

/* A unit cube (half size 0.5) at the position */
static nmo_object_id_t unit_cube(engine_fixture_t *f, const char *name, float x, float y, float z)
{
    nmo_object_id_t object = placed(f, NMO_CID_3DENTITY, name, x, y, z);
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_mesh_state_t *state = mesh != 0 ? (nmo_mesh_state_t *)fixture_state(f, mesh) : NULL;
    if (object == 0 || state == NULL) {
        return 0;
    }
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(nmo_session_get_arena(f->session), 8u * sizeof(nmo_vertex_t),
                                                      _Alignof(nmo_vertex_t));
    if (state->vertices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, 8u * sizeof(nmo_vertex_t));
    for (int i = 0; i < 8; i++) {
        state->vertices[i].position =
            (nmo_vector_t){(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f};
    }
    state->vertex_count = 8;
    nmo_mesh_update_bounding_volumes(state);
    ((nmo_3dentity_state_t *)fixture_state(f, object))->current_mesh = nmo_ref_from_id(mesh);
    return object;
}

/* A closed linear curve round the 10 by 10 square in XZ: A (0,0,0), B (10,0,0), C (10,0,10), D (0,0,10) */
static nmo_object_id_t square_curve(engine_fixture_t *f, nmo_object_id_t points[4])
{
    nmo_object_id_t curve = fixture_create(f, NMO_CID_CURVE, "Square");
    nmo_curve_state_t *state = curve != 0 ? (nmo_curve_state_t *)fixture_state(f, curve) : NULL;
    if (state == NULL) {
        return 0;
    }
    state->base.has_entityndata_chunk = 1;
    state->control_point_count = 4;
    state->opened = 0;
    state->control_point_ids = (nmo_ref_t *)nmo_arena_alloc(nmo_session_get_arena(f->session), 4 * sizeof(nmo_ref_t),
                                                            _Alignof(nmo_ref_t));
    if (state->control_point_ids == NULL) {
        return 0;
    }
    const float corners[4][2] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const char *names[4] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; i++) {
        points[i] = placed(f, NMO_CID_CURVEPOINT, names[i], corners[i][0], 0.0f, corners[i][1]);
        nmo_curvepoint_state_t *point = points[i] != 0 ? (nmo_curvepoint_state_t *)fixture_state(f, points[i]) : NULL;
        if (point == NULL) {
            return 0;
        }
        point->base.has_entityndata_chunk = 1;
        point->base.parent = nmo_ref_from_id(curve);
        point->curve = nmo_ref_from_id(curve);
        point->linear = 1;
        state->control_point_ids[i] = nmo_ref_from_id(points[i]);
    }
    return curve;
}

static nmo_object_id_t group_of(engine_fixture_t *f, const char *name, const nmo_object_id_t *ids, size_t count)
{
    nmo_object_id_t group = fixture_create(f, NMO_CID_GROUP, name);
    nmo_group_state_t *state = group != 0 ? (nmo_group_state_t *)fixture_state(f, group) : NULL;
    nmo_ref_t *members = NULL;
    if (state == NULL || nmo_array_extend(&state->object_ids, count, (void **)&members) != NMO_OK) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        members[i] = nmo_ref_from_id(ids[i]);
    }
    return group;
}

/* The block's local parameter `pos`, settings included in declaration order */
static nmo_object_id_t local_of(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

/* Two NOPs on a block's outputs 0 and 1 */
static void watch_outputs(engine_fixture_t *f, nmo_object_id_t block, nmo_object_id_t out[2])
{
    for (size_t k = 0; k < 2; k++) {
        out[k] = fixture_block(f, GUID_NOP, "Output");
        fixture_link(f, BLOCK_OUT(f, block, k), BLOCK_IN(f, out[k], 0), 0);
    }
}

TEST(engine_tt, curve_blocks_pick_points_project_and_collide)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t points[4];
    nmo_object_id_t curve = square_curve(&f, points);
    ASSERT_TRUE(curve != 0);
    nmo_object_id_t on_edge = unit_cube(&f, "On Edge", 5.0f, 0.0f, 0.0f);
    nmo_object_id_t inside = unit_cube(&f, "Inside", 5.0f, 0.0f, 5.0f);
    nmo_object_id_t curves = group_of(&f, "Curves", &curve, 1);
    ASSERT_TRUE(on_edge && inside && curves);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t pick = started_block(&f, GUID_GET_CURVE_POINT, "Pick");
    nmo_object_id_t nearest = started_block(&f, GUID_GET_NEAREST_CURVE_POSITION, "Nearest");
    nmo_object_id_t check[2] = {started_block(&f, GUID_CHECK_CURVE_COLLISION, "Check Edge"),
                                started_block(&f, GUID_CHECK_CURVE_COLLISION, "Check Inside")};
    ASSERT_TRUE(pick && nearest && check[0] && check[1]);
    fixture_input_int(&f, fixture_target(&f, pick), (int32_t)curve);
    fixture_input_int(&f, BLOCK_PIN(&f, pick, 0), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, nearest, 0), (int32_t)curve);
    const float query[3] = {8.0f, 1.0f, 6.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, nearest, 1), query, sizeof(query));
    nmo_object_id_t outs[2][2];
    const nmo_object_id_t targets[2] = {on_edge, inside};
    for (int i = 0; i < 2; i++) {
        fixture_input_int(&f, fixture_target(&f, check[i]), (int32_t)targets[i]);
        fixture_input_int(&f, BLOCK_PIN(&f, check[i], 0), (int32_t)curves);
        watch_outputs(&f, check[i], outs[i]);
    }
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    nmo_object_id_t picked = 0;
    read_value(&f, BLOCK_POUT(&f, pick, 0), &picked, sizeof(picked));
    ASSERT_EQ(points[2], picked);
    ASSERT_STR_EQ("Curvepoints", nmo_engine_object_name(f.engine, picked)); /* renamed, as the DLL does */
    float position[3] = {0, 0, 0}, step = 0.0f;
    read_value(&f, BLOCK_POUT(&f, nearest, 0), position, sizeof(position));
    read_value(&f, BLOCK_POUT(&f, nearest, 1), &step, sizeof(step));
    ASSERT_FLOAT_EQ(10.0f, position[0], 1e-4f); /* on B-C, the nearest edge in XZ */
    ASSERT_FLOAT_EQ(0.0f, position[1], 1e-4f);
    ASSERT_FLOAT_EQ(6.0f, position[2], 1e-4f);
    ASSERT_FLOAT_EQ(0.4f, step, 1e-5f); /* (10 to B + 6) of 40 */
    ASSERT_EQ(1u, runs(&f, outs[0][0])); /* A-B runs through the cube */
    ASSERT_EQ(1u, runs(&f, outs[1][1]));
    float distance[2] = {-1.0f, -1.0f};
    read_value(&f, BLOCK_POUT(&f, check[0], 0), &distance[0], sizeof(float));
    read_value(&f, BLOCK_POUT(&f, check[1], 0), &distance[1], sizeof(float));
    ASSERT_FLOAT_EQ(0.0f, distance[0], 1e-4f);
    ASSERT_FLOAT_EQ(2.0f * sqrtf(50.0f) - 10.0f, distance[1], 1e-4f);
    fixture_end(&f);
}

TEST(engine_tt, bounding_spheres_meet_and_the_missile_homes_in)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = unit_cube(&f, "A", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t near = unit_cube(&f, "Near", 1.5f, 0.0f, 0.0f);
    nmo_object_id_t far = unit_cube(&f, "Far", 3.0f, 0.0f, 0.0f);
    nmo_object_id_t missile = placed(&f, NMO_CID_3DENTITY, "Missile", 0.0f, 0.0f, 20.0f);
    nmo_object_id_t goal = unit_cube(&f, "Goal", 10.0f, 0.0f, 20.0f);
    ASSERT_TRUE(a && near && far && missile && goal);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t spheres[2] = {started_block(&f, GUID_BOUNDING_SPHERE, "Near Test"),
                                  started_block(&f, GUID_BOUNDING_SPHERE, "Far Test")};
    nmo_object_id_t homing = started_block(&f, GUID_HOMING_MISSILE, "Homing");
    ASSERT_TRUE(spheres[0] && spheres[1] && homing);
    nmo_object_id_t outs[2][2], reach[2];
    const nmo_object_id_t others[2] = {near, far};
    for (int i = 0; i < 2; i++) {
        fixture_input_int(&f, BLOCK_PIN(&f, spheres[i], 0), (int32_t)a);
        fixture_input_int(&f, BLOCK_PIN(&f, spheres[i], 1), (int32_t)others[i]);
        watch_outputs(&f, spheres[i], outs[i]);
    }
    fixture_input_int(&f, fixture_target(&f, homing), (int32_t)missile);
    fixture_input_int(&f, BLOCK_PIN(&f, homing, 0), (int32_t)goal);
    const float speed = 100.0f; /* 2 a 20 ms frame */
    fixture_input_value(&f, BLOCK_PIN(&f, homing, 1), &speed, sizeof(speed));
    watch_outputs(&f, homing, reach);
    fixture_link(&f, BLOCK_OUT(&f, homing, 1), BLOCK_IN(&f, homing, 1), 1);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outs[0][0])); /* 1.5 apart: 2.25 <= 1.5 + 1.5 */
    ASSERT_EQ(1u, runs(&f, outs[1][1])); /* 3 apart: 9 > 3 */
    for (int frame = 1; frame < 5; frame++) {
        ASSERT_EQ(0u, runs(&f, reach[0]));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    ASSERT_EQ(1u, runs(&f, reach[0])); /* at the goal on the fifth frame */
    ASSERT_EQ(4u, runs(&f, reach[1]));
    int32_t index = eng_entity_index(f.engine, missile);
    ASSERT_TRUE(index >= 0);
    const eng_matrix_t *world = &f.engine->entities.data[index].world;
    ASSERT_FLOAT_EQ(10.0f, world->m[3][0], 1e-4f);
    ASSERT_FLOAT_EQ(20.0f, world->m[3][2], 1e-4f);
    ASSERT_FLOAT_EQ(1.0f, world->m[2][0], 1e-5f);  /* facing +X */
    ASSERT_FLOAT_EQ(-1.0f, world->m[0][2], 1e-5f); /* right = up x dir */
    float velocity[3] = {0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, homing, 0), velocity, sizeof(velocity));
    ASSERT_FLOAT_EQ(2.0f, velocity[0], 1e-4f);
    fixture_end(&f);
}

TEST(engine_tt, follow_camera_eases_the_camera_and_its_target)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t camera = placed(&f, NMO_CID_TARGETCAMERA, "Camera", 0.0f, 5.0f, -20.0f);
    nmo_object_id_t aim = placed(&f, NMO_CID_3DENTITY, "Aim", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t ball = placed(&f, NMO_CID_3DENTITY, "Ball", 0.0f, 0.0f, 10.0f);
    ASSERT_TRUE(camera && aim && ball);
    nmo_targetcamera_state_t *state = (nmo_targetcamera_state_t *)fixture_state(&f, camera);
    state->has_target = 1;
    state->target = nmo_ref_from_id(aim);
    ((nmo_3dentity_state_t *)fixture_state(&f, aim))->entity_flags = CK_3DENTITY_TARGETCAMERA;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t follow = started_block(&f, GUID_FOLLOW_CAMERA, "Follow");
    ASSERT_TRUE(follow != 0);
    fixture_input_int(&f, fixture_target(&f, follow), (int32_t)camera);
    fixture_input_int(&f, BLOCK_PIN(&f, follow, 0), (int32_t)ball);
    const float half = 0.5f, fov = 1.0f, offset[3] = {0.0f, 5.0f, -10.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, follow, 2), &half, sizeof(half));
    fixture_input_value(&f, BLOCK_PIN(&f, follow, 3), offset, sizeof(offset));
    fixture_input_value(&f, BLOCK_PIN(&f, follow, 4), &half, sizeof(half));
    fixture_input_value(&f, BLOCK_PIN(&f, follow, 5), &fov, sizeof(fov));
    nmo_object_id_t time_based = local_of(&f, follow, 0);
    ASSERT_TRUE(time_based != 0);
    fixture_commit(&f, NULL);
    const int32_t off = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, time_based, &off, sizeof(off)));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* From where they stand (Init): halfway each frame to the ball and to 10 behind it */
    int32_t cam = eng_entity_index(f.engine, camera), target = eng_entity_index(f.engine, aim);
    ASSERT_TRUE(cam >= 0 && target >= 0);
    ASSERT_FLOAT_EQ(-5.0f, f.engine->entities.data[cam].world.m[3][2], 1e-4f);
    ASSERT_FLOAT_EQ(5.0f, f.engine->entities.data[cam].world.m[3][1], 1e-4f);
    ASSERT_FLOAT_EQ(7.5f, f.engine->entities.data[target].world.m[3][2], 1e-4f);
    float now = 0.0f;
    ASSERT_TRUE(eng_camera_fov(f.engine, camera, &now));
    ASSERT_FLOAT_EQ(1.0f, now, 0.0f);
    fixture_end(&f);
}

/* A 3D object at the origin with a mesh of `side` x `side` vertices one apart in XZ (rows along Z),
   UVs from 0 to 1, two faces a square; its mesh in `mesh_out` */
static nmo_object_id_t grid_object(engine_fixture_t *f, const char *name, int side, nmo_object_id_t *mesh_out)
{
    nmo_object_id_t object = placed(f, NMO_CID_3DOBJECT, name, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_mesh_state_t *state = mesh != 0 ? (nmo_mesh_state_t *)fixture_state(f, mesh) : NULL;
    if (object == 0 || state == NULL) {
        return 0;
    }
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    uint32_t count = (uint32_t)(side * side), faces = (uint32_t)(2 * (side - 1) * (side - 1));
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, count * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, faces * sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, faces * 3 * sizeof(uint16_t), _Alignof(uint16_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, count * sizeof(nmo_vertex_t));
    memset(state->faces, 0, faces * sizeof(nmo_face_t));
    for (int r = 0; r < side; r++) {
        for (int c = 0; c < side; c++) {
            nmo_vertex_t *v = &state->vertices[r * side + c];
            v->position = (nmo_vector_t){(float)c, 0.0f, (float)r};
            v->normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
            v->uv = (nmo_vector2_t){(float)c / (float)(side - 1), (float)r / (float)(side - 1)};
        }
    }
    uint32_t k = 0;
    for (int r = 0; r + 1 < side; r++) {
        for (int c = 0; c + 1 < side; c++) {
            const uint16_t a = (uint16_t)(r * side + c), b = (uint16_t)(a + 1), d = (uint16_t)(a + side);
            const uint16_t e = (uint16_t)(d + 1);
            const uint16_t quad[6] = {a, d, b, b, d, e};
            memcpy(&state->face_vertex_indices[k * 3], quad, sizeof(quad));
            state->faces[k].normal = state->faces[k + 1].normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
            k += 2;
        }
    }
    state->vertex_count = count;
    state->face_count = faces;
    nmo_mesh_update_bounding_volumes(state);
    ((nmo_3dentity_state_t *)fixture_state(f, object))->current_mesh = nmo_ref_from_id(mesh);
    *mesh_out = mesh;
    return object;
}

/* The mesh's vertex `v` as the engine has it now */
static nmo_vector_t mesh_vertex(engine_fixture_t *f, nmo_object_id_t mesh, int v)
{
    nmo_engine_mesh_t out;
    nmo_vector_t none = {NAN, NAN, NAN};
    return nmo_engine_mesh(f->engine, mesh, &out) && v < out.vertex_count ? out.vertices[v].position : none;
}

TEST(engine_tt, generate_plane_makes_its_entity_and_mesh)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t plane = started_block(&f, GUID_GENERATE_PLANE, "Plane");
    ASSERT_TRUE(plane != 0);
    const float position[3] = {1.0f, 2.0f, 3.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, plane, 0), position, sizeof(position));
    fixture_input_int(&f, BLOCK_PIN(&f, plane, 2), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, plane, 3), 1);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_object_id_t entity = nmo_engine_object_named(f.engine, "generated Entity", NMO_CID_3DENTITY);
    nmo_object_id_t mesh = nmo_engine_object_named(f.engine, "generated Mesh", NMO_CID_MESH);
    ASSERT_TRUE(entity != 0 && mesh != 0);
    int32_t index = eng_entity_index(f.engine, entity);
    ASSERT_TRUE(index >= 0);
    ASSERT_FLOAT_EQ(3.0f, f.engine->entities.data[index].world.m[3][2], 0.0f);
    ASSERT_TRUE(eng_in_scene(f.engine, entity));
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &out));
    ASSERT_EQ(8, out.vertex_count); /* four a square */
    ASSERT_EQ(4, out.face_count);
    ASSERT_FLOAT_EQ(2.0f, out.vertices[6].position.x, 0.0f); /* the second square's far right corner */
    ASSERT_FLOAT_EQ(-1.0f, out.vertices[6].position.z, 0.0f);
    fixture_end(&f);
}

TEST(engine_tt, sinus_and_magnet_deform_their_meshes)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t waved_mesh = 0, pulled_mesh = 0;
    nmo_object_id_t waved = grid_object(&f, "Waved", 3, &waved_mesh);
    nmo_object_id_t pulled = grid_object(&f, "Pulled", 3, &pulled_mesh);
    nmo_object_id_t magnet = placed(&f, NMO_CID_3DENTITY, "Magnet", 1.0f, 1.0f, 1.0f);
    ASSERT_TRUE(waved && pulled && magnet);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t sinus = started_block(&f, GUID_SINUS_MESHDEFORM, "Sinus");
    nmo_object_id_t pull = started_block(&f, GUID_MAGNET_MESH_FORM, "Magnet");
    ASSERT_TRUE(sinus && pull);
    fixture_input_int(&f, fixture_target(&f, sinus), (int32_t)waved);
    const float still = 0.0f, one = 1.0f, wavelength = 0.2f;
    fixture_input_value(&f, BLOCK_PIN(&f, sinus, 0), &still, sizeof(still)); /* no time: phase 0 */
    fixture_input_value(&f, BLOCK_PIN(&f, sinus, 1), &one, sizeof(one));
    fixture_input_value(&f, BLOCK_PIN(&f, sinus, 2), &wavelength, sizeof(wavelength));
    nmo_object_id_t sinus_count = local_of(&f, sinus, 0);
    fixture_input_int(&f, fixture_target(&f, pull), (int32_t)pulled);
    fixture_input_int(&f, BLOCK_PIN(&f, pull, 0), (int32_t)magnet);
    const float max = 5.0f, min = 0.5f;
    fixture_input_value(&f, BLOCK_PIN(&f, pull, 1), &max, sizeof(max));
    fixture_input_value(&f, BLOCK_PIN(&f, pull, 2), &min, sizeof(min));
    fixture_input_value(&f, BLOCK_PIN(&f, pull, 3), &one, sizeof(one));
    ASSERT_TRUE(sinus_count != 0);
    fixture_commit(&f, NULL);
    const int32_t waves = 1;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, sinus_count, &waves, sizeof(waves)));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Circles round SinPos (the origin): the sine table at |distance x k| degrees, k the box
       diagonal (2 x sqrt 2) times 6.28 over the wavelength */
    const float k = (float)(sqrt(8.0) * 6.2800002) / 0.2f;
    const int corners[3] = {1, 3, 8}; /* 1 away, 1 away, 2 sqrt 2 away */
    const double distances[3] = {1.0, 1.0, sqrt(8.0)};
    for (int i = 0; i < 3; i++) {
        int degree = (int)fabs(distances[i] * k) % 360;
        nmo_vector_t p = mesh_vertex(&f, waved_mesh, corners[i]);
        ASSERT_FLOAT_EQ((float)sin(degree * 3.14159265358979 / 180.0), p.y, 1e-4f);
    }
    nmo_vector_t far = mesh_vertex(&f, waved_mesh, 8);
    ASSERT_FLOAT_EQ(2.0f, far.x, 0.0f); /* X and Z stay */
    ASSERT_FLOAT_EQ(2.0f, far.z, 0.0f);

    /* The middle vertex, 1 under the magnet: (1 - 5) / (0.5 - 5) of the way up */
    nmo_vector_t middle = mesh_vertex(&f, pulled_mesh, 4);
    ASSERT_FLOAT_EQ(4.0f / 4.5f, middle.y, 1e-5f);
    ASSERT_FLOAT_EQ(1.0f, middle.x, 1e-6f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    middle = mesh_vertex(&f, pulled_mesh, 4);
    ASSERT_FLOAT_EQ(4.0f / 4.5f, middle.y, 1e-5f); /* from the rest shape again, not further */
    fixture_end(&f);
}

TEST(engine_tt, ripple_and_pms_waves_move_only_heights)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t pond_mesh = 0, sea_mesh = 0;
    nmo_object_id_t pond = grid_object(&f, "Pond", 5, &pond_mesh);
    nmo_object_id_t sea = grid_object(&f, "Sea", 4, &sea_mesh);
    ASSERT_TRUE(pond && sea);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t ripple = started_block(&f, GUID_RIPPLE_WAVE, "Ripple");
    nmo_object_id_t pms = started_block(&f, GUID_PMS_MESHDEFORM, "PMS");
    ASSERT_TRUE(ripple && pms);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, ripple, 1), 0); /* a drop as it starts */
    fixture_input_int(&f, fixture_target(&f, ripple), (int32_t)pond);
    const float strength = 1.0f, viscosity = 0.98f;
    fixture_input_value(&f, BLOCK_PIN(&f, ripple, 0), &strength, sizeof(strength));
    fixture_input_value(&f, BLOCK_PIN(&f, ripple, 1), &viscosity, sizeof(viscosity));
    fixture_input_int(&f, fixture_target(&f, pms), (int32_t)sea);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Only the inner cells (rows and columns 2 and 3) move: the two outer rings are held at 0;
       the drop's 3 x 3 dent, anywhere from (1,1) to (4,4), reaches one of them at least */
    bool dented = false;
    for (int v = 0; v < 25; v++) {
        nmo_vector_t p = mesh_vertex(&f, pond_mesh, v);
        int r = v / 5, c = v % 5;
        ASSERT_FLOAT_EQ((float)c, p.x, 0.0f);
        ASSERT_FLOAT_EQ((float)r, p.z, 0.0f);
        if (r < 2 || r > 3 || c < 2 || c > 3) {
            ASSERT_FLOAT_EQ(0.0f, p.y, 0.0f);
        } else {
            ASSERT_TRUE(p.y <= 0.0f);
            dented = dented || p.y < 0.0f;
        }
    }
    ASSERT_TRUE(dented);
    float highest = 0.0f;
    bool moved = false;
    for (int v = 0; v < 16; v++) {
        nmo_vector_t p = mesh_vertex(&f, sea_mesh, v);
        ASSERT_FLOAT_EQ((float)(v % 4), p.x, 0.0f);
        ASSERT_FLOAT_EQ((float)(v / 4), p.z, 0.0f);
        ASSERT_TRUE(isfinite(p.y));
        highest = fmaxf(highest, fabsf(p.y));
        moved = moved || p.y != 0.0f;
    }
    ASSERT_TRUE(moved);
    ASSERT_TRUE(highest < 2.0f * 0.6f * 1.8f); /* within two waves' bound */
    fixture_end(&f);
}

TEST(engine_tt, high_map_meshform_raises_vertices_by_texel_alpha)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = 0;
    nmo_object_id_t ground = grid_object(&f, "Ground", 2, &mesh);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Height Map");
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Ground Material");
    ASSERT_TRUE(ground && texture && material);
    ((nmo_material_state_t *)fixture_state(&f, material))->textures[0] = nmo_ref_from_id(texture);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(&f, mesh);
    state->material_groups = (nmo_material_group_t *)nmo_arena_alloc(nmo_session_get_arena(f.session),
                                                                     sizeof(nmo_material_group_t),
                                                                     _Alignof(nmo_material_group_t));
    ASSERT_TRUE(state->material_groups != NULL);
    memset(state->material_groups, 0, sizeof(nmo_material_group_t));
    state->material_groups[0].material = nmo_ref_from_id(material);
    state->material_group_count = 1;
    state->has_material_groups = 1;
    /* Texels (0,1), (1,1), (0,0), and a V of 0 that points under the image */
    const float uvs[4][2] = {{0.25f, 0.75f}, {0.75f, 0.75f}, {0.25f, 1.0f}, {0.75f, 0.0f}};
    for (int v = 0; v < 4; v++) {
        state->vertices[v].uv = (nmo_vector2_t){uvs[v][0], uvs[v][1]};
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t form = started_block(&f, GUID_HIGH_MAP_MESHFORM, "High Map");
    ASSERT_TRUE(form != 0);
    fixture_input_int(&f, fixture_target(&f, form), (int32_t)ground);
    fixture_input_int(&f, BLOCK_PIN(&f, form, 0), -1); /* the first material */
    const float height = 10.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, form, 1), &height, sizeof(height));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_texture_t *bitmap = eng_texture(f.engine, texture);
    ASSERT_TRUE(bitmap != NULL && eng_texture_create(f.engine, bitmap, 2, 2, 0));
    eng_surface_t *surface = eng_texture_surface(f.engine, bitmap, -1);
    ASSERT_TRUE(surface != NULL && surface->rgba != NULL);
    const uint8_t alphas[4] = {50, 0, 255, 100}; /* (0,0), (1,0), (0,1), (1,1) */
    for (int i = 0; i < 4; i++) {
        surface->rgba[4 * i + 3] = alphas[i];
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FLOAT_EQ(25.5f, mesh_vertex(&f, mesh, 0).y, 1e-4f); /* 255 x 10 / 100 */
    ASSERT_FLOAT_EQ(10.0f, mesh_vertex(&f, mesh, 1).y, 1e-4f);
    ASSERT_FLOAT_EQ(5.0f, mesh_vertex(&f, mesh, 2).y, 1e-4f);
    ASSERT_FLOAT_EQ(0.0f, mesh_vertex(&f, mesh, 3).y, 0.0f);
    fixture_end(&f);
}

/* The UV of vertex `v` in channel `channel` (-1 the mesh's own) as the engine has it now */
static nmo_vector2_t mesh_uv(engine_fixture_t *f, nmo_object_id_t mesh, int channel, int v)
{
    nmo_engine_mesh_t out;
    nmo_vector2_t none = {NAN, NAN};
    if (!nmo_engine_mesh(f->engine, mesh, &out) || v >= out.vertex_count) {
        return none;
    }
    if (channel < 0) {
        return out.vertices[v].uv;
    }
    if (channel >= out.channel_count || out.channels[channel].uvs == NULL) {
        return none;
    }
    return (nmo_vector2_t){out.channels[channel].uvs[2 * v], out.channels[channel].uvs[2 * v + 1]};
}

TEST(engine_tt, set_mapping_tiles_offsets_and_copies_materials)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = 0;
    nmo_object_id_t ground = grid_object(&f, "Ground", 3, &mesh);
    nmo_object_id_t from = fixture_create(&f, NMO_CID_MATERIAL, "From");
    nmo_object_id_t to = fixture_create(&f, NMO_CID_MATERIAL, "To");
    ASSERT_TRUE(ground && from && to);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t mapping = started_block(&f, GUID_SET_MAPPING, "Mapping");
    nmo_object_id_t values = started_block(&f, GUID_SET_MATERIAL_VALUES, "Values");
    ASSERT_TRUE(mapping && values);
    fixture_input_int(&f, fixture_target(&f, mapping), (int32_t)ground);
    fixture_input_int(&f, BLOCK_PIN(&f, mapping, 0), -1);
    const float offset[2] = {0.5f, 0.0f}, tile[2] = {2.0f, 2.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, mapping, 3), offset, sizeof(offset));
    fixture_input_value(&f, BLOCK_PIN(&f, mapping, 4), tile, sizeof(tile));
    fixture_input_int(&f, fixture_target(&f, values), (int32_t)to);
    fixture_input_int(&f, BLOCK_PIN(&f, values, 0), (int32_t)from);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_material_t *source = eng_material(f.engine, from);
    ASSERT_TRUE(source != NULL);
    source->desc.diffuse[1] = 0.25f;
    source->desc.power = 7.0f;
    source->desc.two_sided = true;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* U and V in [0, 1] round their middle: tiled by 2 (V the other way round) then offset */
    nmo_vector2_t middle = mesh_uv(&f, mesh, -1, 4), corner = mesh_uv(&f, mesh, -1, 8);
    ASSERT_FLOAT_EQ(1.0f, middle.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.5f, middle.y, 1e-6f);
    ASSERT_FLOAT_EQ(2.0f, corner.x, 1e-6f);
    ASSERT_FLOAT_EQ(1.5f, corner.y, 1e-6f);
    nmo_engine_material_t copied;
    ASSERT_TRUE(nmo_engine_material(f.engine, to, &copied));
    ASSERT_FLOAT_EQ(0.25f, copied.diffuse[1], 0.0f);
    ASSERT_FLOAT_EQ(7.0f, copied.power, 0.0f);
    ASSERT_TRUE(copied.two_sided);
    fixture_end(&f);
}

TEST(engine_tt, cel_shading_and_aniso_add_their_channels)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t cel_mesh = 0, aniso_mesh = 0;
    nmo_object_id_t toon = grid_object(&f, "Toon", 2, &cel_mesh);
    nmo_object_id_t shiny = grid_object(&f, "Shiny", 2, &aniso_mesh);
    nmo_object_id_t sun = placed(&f, NMO_CID_LIGHT, "Sun", 0.5f, 10.0f, 0.5f);
    nmo_object_id_t eye = placed(&f, NMO_CID_3DENTITY, "Eye", 1.0f, 10.0f, 0.0f);
    ASSERT_TRUE(toon && shiny && sun && eye);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t cel = started_block(&f, GUID_CEL_SHADING, "Cel");
    nmo_object_id_t aniso = started_block(&f, GUID_ANISO_MAPPING, "Aniso");
    ASSERT_TRUE(cel && aniso);
    fixture_input_int(&f, fixture_target(&f, cel), (int32_t)toon);
    fixture_input_int(&f, BLOCK_PIN(&f, cel, 0), (int32_t)sun);
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, cel, 1), red, sizeof(red));
    fixture_input_int(&f, fixture_target(&f, aniso), (int32_t)shiny);
    fixture_input_int(&f, BLOCK_PIN(&f, aniso, 0), (int32_t)eye); /* light and camera together */
    fixture_input_int(&f, BLOCK_PIN(&f, aniso, 1), (int32_t)eye);
    fixture_input_int(&f, BLOCK_PIN(&f, aniso, 2), 4);
    fixture_input_int(&f, BLOCK_PIN(&f, aniso, 5), 1);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Cel: a channel facing the light all over (U 1) on a tone texture of red */
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, cel_mesh, &out));
    ASSERT_EQ(1, out.channel_count);
    ASSERT_EQ(VXBLEND_ONE, out.channels[0].source_blend);
    ASSERT_EQ(VXBLEND_ZERO, out.channels[0].dest_blend);
    ASSERT_FLOAT_EQ(1.0f, mesh_uv(&f, cel_mesh, 0, 3).x, 0.0f);
    nmo_engine_material_t look;
    ASSERT_TRUE(nmo_engine_material(f.engine, out.channels[0].material, &look));
    nmo_engine_image_t image;
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, look.texture, &image));
    ASSERT_EQ(16, image.width);
    ASSERT_EQ(127, image.rgba[0]);      /* texel 0: half the colour */
    ASSERT_EQ(255, image.rgba[4 * 10]); /* texel 10: half of the colour plus white */
    ASSERT_EQ(127, image.rgba[4 * 10 + 1]);

    /* Aniso: added on (ONE, ONE); vertex (1,0,0): base (cos 1, sin 1) through a light and camera
       straight up */
    ASSERT_TRUE(nmo_engine_mesh(f.engine, aniso_mesh, &out));
    ASSERT_EQ(1, out.channel_count);
    ASSERT_EQ(VXBLEND_ONE, out.channels[0].dest_blend);
    nmo_vector2_t uv = mesh_uv(&f, aniso_mesh, 0, 1);
    ASSERT_FLOAT_EQ(0.0f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ((float)(cos(1.0) + sin(1.0)), uv.y, 1e-5f);
    ASSERT_TRUE(nmo_engine_object_named(f.engine, "Tex AnisoMap", NMO_CID_TEXTURE) != 0);
    fixture_end(&f);
}

TEST(engine_tt, bump_and_refraction_move_uvs_toward_light_and_eye)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t bump_mesh = 0, glass_mesh = 0;
    nmo_object_id_t bumped = grid_object(&f, "Bumped", 2, &bump_mesh);
    nmo_object_id_t glass = grid_object(&f, "Glass", 2, &glass_mesh);
    nmo_object_id_t lamp = placed(&f, NMO_CID_3DENTITY, "Lamp", 1.0f, 0.0f, -100.0f);
    nmo_object_id_t eye = placed(&f, NMO_CID_3DENTITY, "Eye", 0.0f, 10.0f, 0.0f);
    ASSERT_TRUE(bumped && glass && lamp && eye);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t bump = started_block(&f, GUID_BUMP_MAP, "Bump");
    nmo_object_id_t refraction = started_block(&f, GUID_REFRACTION_MAPPING, "Refraction");
    ASSERT_TRUE(bump && refraction);
    fixture_input_int(&f, fixture_target(&f, bump), (int32_t)bumped);
    fixture_input_int(&f, BLOCK_PIN(&f, bump, 0), -1);
    fixture_input_int(&f, BLOCK_PIN(&f, bump, 1), (int32_t)lamp);
    const float emboss = 0.5f, depth = 10.0f, factor = 0.95f;
    fixture_input_value(&f, BLOCK_PIN(&f, bump, 2), &emboss, sizeof(emboss));
    fixture_input_int(&f, fixture_target(&f, refraction), (int32_t)glass);
    fixture_input_int(&f, BLOCK_PIN(&f, refraction, 0), -1);
    fixture_input_int(&f, BLOCK_PIN(&f, refraction, 1), (int32_t)eye);
    fixture_input_value(&f, BLOCK_PIN(&f, refraction, 2), &depth, sizeof(depth));
    fixture_input_value(&f, BLOCK_PIN(&f, refraction, 3), &factor, sizeof(factor));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Bump: vertex 3 (1,0,1) is only the second face's third: its edge to vertex 1 points at the
       lamp (U + emboss), its edge to vertex 2 across it (V kept) */
    nmo_vector2_t uv = mesh_uv(&f, bump_mesh, -1, 3);
    ASSERT_FLOAT_EQ(1.5f, uv.x, 1e-4f);
    ASSERT_FLOAT_EQ(1.0f, uv.y, 1e-4f);

    /* Refraction: under the eye straight down to Depth; vertex (1,0,0) bent outward */
    uv = mesh_uv(&f, glass_mesh, -1, 0);
    ASSERT_FLOAT_EQ(0.0f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, uv.y, 1e-6f);
    double tx = -1.0 / sqrt(101.0), ty = 10.0 / sqrt(101.0);
    double rx = -tx, ry = -(ty + 0.95);
    double rl = sqrt(rx * rx + ry * ry);
    double t = -10.0 / (ry / rl);
    uv = mesh_uv(&f, glass_mesh, -1, 1);
    ASSERT_FLOAT_EQ((float)(rx / rl * t + 1.0), uv.x, 1e-4f);
    ASSERT_FLOAT_EQ(0.0f, uv.y, 1e-6f);
    fixture_end(&f);
}

TEST(engine_tt, save_alpha_moves_alpha_into_the_colour_and_saves)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Mask");
    ASSERT_TRUE(texture != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t save = started_block(&f, GUID_SAVE_ALPHA, "Save");
    ASSERT_TRUE(save != 0);
    fixture_input_int(&f, fixture_target(&f, save), (int32_t)texture);
    input_text(&f, BLOCK_PIN(&f, save, 0), "mask.tga");
    fixture_commit(&f, NULL);
    written_t written;
    memset(&written, 0, sizeof(written));
    nmo_engine_set_file_writer(f.engine, keep_written, &written);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_texture_t *bitmap = eng_texture(f.engine, texture);
    ASSERT_TRUE(bitmap != NULL && eng_texture_create(f.engine, bitmap, 2, 2, 0));
    eng_surface_t *surface = eng_texture_surface(f.engine, bitmap, -1);
    ASSERT_TRUE(surface != NULL);
    surface->rgba[3] = 200;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, written.count);
    ASSERT_STR_EQ("mask.tga", written.name);
    ASSERT_TRUE(written.size > 16);
    ASSERT_EQ(200, surface->rgba[0]);
    ASSERT_EQ(200, surface->rgba[2]);
    ASSERT_EQ(0, surface->rgba[3]);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_tt, load_database_fills_the_array_of_its_name);
REGISTER_TEST(engine_tt, save_database_writes_the_registered_arrays);
REGISTER_TEST(engine_tt, register_array_registers_once_and_clear_forgets);
REGISTER_TEST(engine_tt, system_values_come_from_the_host);
REGISTER_TEST(engine_tt, debug_mode_and_exit_to_system);
REGISTER_TEST(engine_tt, statistics_reports_the_last_frame);
REGISTER_TEST(engine_tt, read_registry_reads_what_the_host_set);
REGISTER_TEST(engine_tt, drivers_and_screen_modes_fill_their_arrays);
REGISTER_TEST(engine_tt, timer_and_strings);
REGISTER_TEST(engine_tt, linear_volume_maps_the_volume);
REGISTER_TEST(engine_tt, preload_textures_counts_each_texture_once);
REGISTER_TEST(engine_tt, convert_pixel_uses_the_screen_mode);
REGISTER_TEST(engine_tt, planar_filter_stays_on_until_off);
REGISTER_TEST(engine_tt, planar_filter_leaves_with_its_script_and_comes_back);
REGISTER_TEST(engine_tt, counter_wraps_around_its_range);
REGISTER_TEST(engine_tt, logic_blocks_convert_map_and_find);
REGISTER_TEST(engine_tt, chrono_counts_and_children_iterate);
REGISTER_TEST(engine_tt, write_back_writes_into_the_source_read);
REGISTER_TEST(engine_tt, curve_blocks_pick_points_project_and_collide);
REGISTER_TEST(engine_tt, bounding_spheres_meet_and_the_missile_homes_in);
REGISTER_TEST(engine_tt, follow_camera_eases_the_camera_and_its_target);
REGISTER_TEST(engine_tt, generate_plane_makes_its_entity_and_mesh);
REGISTER_TEST(engine_tt, sinus_and_magnet_deform_their_meshes);
REGISTER_TEST(engine_tt, ripple_and_pms_waves_move_only_heights);
REGISTER_TEST(engine_tt, high_map_meshform_raises_vertices_by_texel_alpha);
REGISTER_TEST(engine_tt, set_mapping_tiles_offsets_and_copies_materials);
REGISTER_TEST(engine_tt, cel_shading_and_aniso_add_their_channels);
REGISTER_TEST(engine_tt, bump_and_refraction_move_uvs_toward_light_and_eye);
REGISTER_TEST(engine_tt, save_alpha_moves_alpha_into_the_colour_and_saves);
TEST_MAIN_END()
