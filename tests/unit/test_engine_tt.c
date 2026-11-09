#include "engine_fixture.h"

#include "engine/nmo_engine_render.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_param_guids.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_edit.h"
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
#define GUID_SET_FSAA NMO_GUID(0x30bd6156, 0x14772834)
#define GUID_VIEW_FRUSTUM_CLIPPING NMO_GUID(0x412e4cd0, 0x6f860de3)
#define GUID_BITMAP_FONT NMO_GUID(0x173e60ff, 0x4db14bcc)
#define GUID_SKY_AROUND NMO_GUID(0x2c7d5826, 0x688778f4)
#define GUID_SHADOW_PLANE NMO_GUID(0x137c3d66, 0x3ff1752d)
#define GUID_SHADOW_MAPPING NMO_GUID(0x334c7dfe, 0x47c81a31)
#define GUID_REAL_SHADOW_MAPPING NMO_GUID(0x452816af, 0x681a3a81)
#define GUID_LENS_FLARE NMO_GUID(0x241b32a5, 0x71fe3357)
#define GUID_SCENE_STENCIL_SHADOW NMO_GUID(0x4dcf4ca0, 0x474e25ec)
#define GUID_SET_2D_SPRITE_OLD NMO_GUID(0x56680b62, 0x23083054)
#define GUID_SET_3D_SPRITE NMO_GUID(0x408e30b5, 0x22a07ada)
#define GUID_GET_UV_2D_SPRITE NMO_GUID(0x4f16689a, 0x472b1c63)
#define GUID_TT_PUSH_BUTTON NMO_GUID(0x4df00dac, 0x562c27da)
#define GUID_MOTOR_SOUND NMO_GUID(0x1a1a67aa, 0x664108ed)
#define GUID_LOAD_MOTOR_SETTINGS NMO_GUID(0x0f822838, 0x74116583)
#define GUID_FONT_COORDINATES NMO_GUID(0x73e256e5, 0x6552118e)
#define GUID_CREATE_FONT NMO_GUID(0x7d644088, 0x40dd0349)
#define GUID_COPY_FILE NMO_GUID(0x22e510f9, 0x070f528a)
#define GUID_CREATE_DIRECTORY NMO_GUID(0x03442842, 0x01df7a5f)
#define GUID_EXIST_FILE NMO_GUID(0x53ba46bc, 0x31782f60)
#define GUID_DELETE_FILE NMO_GUID(0x562f6aab, 0x07ea0f0b)
#define GUID_LIST_DIR NMO_GUID(0x496f429e, 0x7d602e9d)
#define GUID_FILE_NAME_FROM_PATH NMO_GUID(0x68e631f7, 0x13f914fe)
#define GUID_GET_GAME_SCORE NMO_GUID(0x34951557, 0x4d180895)
#define GUID_CREATE_GAME_INFO NMO_GUID(0x0a8b6c27, 0x402b43b4)
#define GUID_EXISTS_GAME_INFO NMO_GUID(0x56173aa5, 0x73ff1a7f)
#define GUID_DELETE_GAME_INFO NMO_GUID(0x29f17890, 0x2e0117a2)
#define GUID_SET_LEVEL_DATA NMO_GUID(0x02030ea1, 0x30182971)
#define GUID_SET_LEVEL_ID NMO_GUID(0x20b86a4c, 0x3c62472a)
#define GUID_GET_LEVEL_DATA NMO_GUID(0x6bca40f9, 0x42641a24)
#define GUID_GET_LEVEL_ID NMO_GUID(0x649011fe, 0x437d5918)
#define GUID_SET_GAME_NAME NMO_GUID(0x30641426, 0x12403bb9)
#define GUID_GET_GAME_NAME NMO_GUID(0x39b21b0c, 0x5db65810)
#define GUID_SET_INT_REGISTRY NMO_GUID(0x175c07d1, 0x1d235544)
#define GUID_SET_FLOAT_REGISTRY NMO_GUID(0x0060522f, 0x041d4c1f)
#define GUID_GET_INT_REGISTRY NMO_GUID(0x50c02b30, 0x2e5a2eda)
#define GUID_GET_FLOAT_REGISTRY NMO_GUID(0x00fe6fe4, 0x12a66150)
#define GUID_WRITE_REGISTRY NMO_GUID(0x7aa8084e, 0x1ee229b7)
#define GUID_INSTALL_ARRAY NMO_GUID(0x41f47ec4, 0x69470bbd)
#define GUID_UNINSTALL_ARRAY NMO_GUID(0x77dd56cf, 0x63dc30b4)
#define GUID_READ_ARRAY NMO_GUID(0x5c825017, 0x6d367973)
#define GUID_WRITE_ARRAY NMO_GUID(0x07414af4, 0x505e52ee)
#define GUID_LOAD_CMO NMO_GUID(0x63dd09a7, 0x567f7218)
#define GUID_RESTART_CMO NMO_GUID(0x0b585f6f, 0x46253c21)
#define GUID_LIMIT_FRAMERATE NMO_GUID(0x12a40e8f, 0x6f2d0613)
#define GUID_SET_ROOKIE NMO_GUID(0x77de64df, 0x694f141f)
#define GUID_IS_ROOKIE NMO_GUID(0x6dc06abb, 0x44451de5)
#define GUID_WINDOW_ACTIVATE NMO_GUID(0x70936783, 0x7fee4a3b)

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

/* A 3D object, a unit cube (half size 0.5), at the position */
static nmo_object_id_t unit_cube(engine_fixture_t *f, const char *name, float x, float y, float z)
{
    nmo_object_id_t object = placed(f, NMO_CID_3DOBJECT, name, x, y, z);
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
    nmo_object_id_t nearer = unit_cube(&f, "Near", 1.5f, 0.0f, 0.0f);
    nmo_object_id_t farther = unit_cube(&f, "Far", 3.0f, 0.0f, 0.0f);
    nmo_object_id_t missile = placed(&f, NMO_CID_3DENTITY, "Missile", 0.0f, 0.0f, 20.0f);
    nmo_object_id_t goal = unit_cube(&f, "Goal", 10.0f, 0.0f, 20.0f);
    ASSERT_TRUE(a && nearer && farther && missile && goal);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t spheres[2] = {started_block(&f, GUID_BOUNDING_SPHERE, "Near Test"),
                                  started_block(&f, GUID_BOUNDING_SPHERE, "Far Test")};
    nmo_object_id_t homing = started_block(&f, GUID_HOMING_MISSILE, "Homing");
    ASSERT_TRUE(spheres[0] && spheres[1] && homing);
    nmo_object_id_t outs[2][2], reach[2];
    const nmo_object_id_t others[2] = {nearer, farther};
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
    nmo_vector_t farther = mesh_vertex(&f, waved_mesh, 8);
    ASSERT_FLOAT_EQ(2.0f, farther.x, 0.0f); /* X and Z stay */
    ASSERT_FLOAT_EQ(2.0f, farther.z, 0.0f);

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
    fixture_input_int(&f, BLOCK_PIN(&f, cel, 2), 1); /* Outline */
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

    /* Outline: the toon drawn again after itself, in wireframe over everything */
    eng_show(f.engine, toon, 1);
    eng_show(f.engine, cel_mesh, 1);
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int toon_draws = 0;
    const nmo_engine_draw_t *outline = NULL;
    for (int32_t d = 0; d < frame.draw_count; d++) {
        if (frame.draws[d].kind == NMO_ENGINE_DRAW_MESH && frame.draws[d].object == toon) {
            if (toon_draws++ == 1) {
                outline = &frame.draws[d];
            }
        }
    }
    ASSERT_EQ(2, toon_draws);
    ASSERT_TRUE(outline != NULL && outline->wireframe && outline->no_z_test);

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

TEST(engine_tt, fsaa_bitmap_font_and_frustum_clipping)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t font = fixture_create(&f, NMO_CID_MATERIAL, "Font");
    nmo_object_id_t camera = placed(&f, NMO_CID_CAMERA, "Camera", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t front = unit_cube(&f, "Front", 0.0f, 0.0f, 10.0f);
    nmo_object_id_t behind = unit_cube(&f, "Behind", 0.0f, 0.0f, -10.0f);
    ASSERT_TRUE(font && camera && front && behind);
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    lens->fov = 1.0f;
    lens->near_plane = 1.0f;
    lens->far_plane = 1000.0f;
    lens->aspect_width = 4;
    lens->aspect_height = 3;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t fsaa = started_block(&f, GUID_SET_FSAA, "FSAA");
    nmo_object_id_t text = started_block(&f, GUID_BITMAP_FONT, "Text");
    nmo_object_id_t clip = started_block(&f, GUID_VIEW_FRUSTUM_CLIPPING, "Clip");
    ASSERT_TRUE(fsaa && text && clip);
    fixture_input_int(&f, BLOCK_PIN(&f, clip, 0), (int32_t)camera);
    fixture_input_int(&f, BLOCK_PIN(&f, fsaa, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, text, 0), (int32_t)font);
    const float position[2] = {3.0f, 4.0f}, scale[2] = {1.0f, 2.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, text, 1), position, sizeof(position));
    fixture_input_value(&f, BLOCK_PIN(&f, text, 2), scale, sizeof(scale));
    input_text(&f, BLOCK_PIN(&f, text, 3), "AB");
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(frame.antialias);
    ASSERT_TRUE((eng_object_visibility(f.engine, front) & CK_OBJECT_VISIBLE) != 0); /* in the camera's view */
    ASSERT_TRUE((eng_object_visibility(f.engine, behind) & CK_OBJECT_VISIBLE) == 0);

    /* A quad a letter at Z 10; 'A' is cell 33: column 1, row 2 of the 16 x 16 texture */
    nmo_object_id_t mesh = nmo_engine_object_named(f.engine, "Mesh", NMO_CID_MESH);
    ASSERT_TRUE(nmo_engine_object_named(f.engine, "Ent", NMO_CID_3DENTITY) != 0 && mesh != 0);
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &out));
    ASSERT_EQ(8, out.vertex_count);
    ASSERT_FLOAT_EQ(3.0f, out.vertices[1].position.x, 0.0f);
    ASSERT_FLOAT_EQ(6.0f, out.vertices[1].position.y, 0.0f);
    ASSERT_FLOAT_EQ(10.0f, out.vertices[1].position.z, 0.0f);
    ASSERT_FLOAT_EQ(0.0625f, out.vertices[1].uv.x, 0.0f);
    ASSERT_FLOAT_EQ(0.125f, out.vertices[1].uv.y, 0.0f);
    ASSERT_FLOAT_EQ(5.0f, out.vertices[6].position.x, 0.0f); /* 'B' ends 2 to the right */
    ASSERT_EQ(font, nmo_engine_mesh_material(f.engine, mesh, (int32_t)out.faces[3].material_group_idx));
    fixture_end(&f);
}

TEST(engine_tt, sky_around_follows_the_viewpoint)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t sky = started_block(&f, GUID_SKY_AROUND, "Sky");
    ASSERT_TRUE(sky != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sky, 1), 2); /* Off two frames on */
    nmo_object_id_t sides = local_of(&f, sky, 1), top = local_of(&f, sky, 2), bottom = local_of(&f, sky, 3);
    ASSERT_TRUE(sides && top && bottom);
    fixture_commit(&f, NULL);
    const int32_t four = 4, yes = 1;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, sides, &four, sizeof(four)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, top, &yes, sizeof(yes)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, bottom, &yes, sizeof(yes)));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Four square sides of radius 70 (chord 70 sqrt 2), each with a top and a bottom triangle */
    nmo_object_id_t ring = nmo_engine_object_named(f.engine, "TT_SkyAround_Entity", NMO_CID_3DENTITY);
    nmo_object_id_t mesh = nmo_engine_object_named(f.engine, "TT_SkyAround_Mesh", NMO_CID_MESH);
    ASSERT_TRUE(ring != 0 && mesh != 0);
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &out));
    ASSERT_EQ(40, out.vertex_count);
    ASSERT_EQ(16, out.face_count);
    ASSERT_TRUE(out.prelit);
    ASSERT_FLOAT_EQ(70.0f, out.vertices[0].position.x, 1e-4f);
    ASSERT_FLOAT_EQ(-35.0f * sqrtf(2.0f), out.vertices[0].position.y, 1e-3f);
    ASSERT_FLOAT_EQ(0.5f, out.vertices[6].uv.x, 0.0f); /* the top fan's middle */
    ASSERT_TRUE((eng_object_visibility(f.engine, ring) & CK_OBJECT_VISIBLE) != 0);
    eng_matrix_t eye;
    eng_render_viewpoint(f.engine, &eye);
    int32_t index = eng_entity_index(f.engine, ring);
    ASSERT_TRUE(index >= 0);
    ASSERT_FLOAT_EQ(eye.m[3][0], f.engine->entities.data[index].world.m[3][0], 1e-5f);
    ASSERT_FLOAT_EQ(eye.m[3][2], f.engine->entities.data[index].world.m[3][2], 1e-5f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_TRUE((eng_object_visibility(f.engine, ring) & CK_OBJECT_VISIBLE) == 0); /* Off */
    fixture_end(&f);
}

/* The Floor attribute on `object` (while editing) */
static void floor_attribute(engine_fixture_t *f, nmo_object_id_t object)
{
    nmo_manager_entry_create_options_t options = {.enabled = true, .attribute_type_guid = CKPGUID_FLOOR,
        .category = "Floor Manager", .has_compatible_class_id = true, .compatible_class_id = NMO_CID_3DENTITY,
        .has_flags = true, .flags = 0};
    uint32_t attribute = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx), "Floor",
                                                                     &options, &attribute));
    nmo_object_id_t parameter = fixture_create(f, NMO_CID_PARAMETEROUT, "Floor");
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, parameter);
    state->has_state = true;
    state->mode = CKPARAM_MODE_BUFFER;
    state->type_guid = CKPGUID_FLOOR;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, 1, 20, NULL));
    memset(state->buffer_data.data, 0, 20);
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)fixture_state(f, object))->attributes,
                                                          parameter, attribute, NULL));
}

/* The frame's TRIANGLES draw of `object`; NULL for none */
static const nmo_engine_draw_t *triangles_of(const nmo_engine_frame_t *frame, nmo_object_id_t object)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_TRIANGLES && frame->draws[i].object == object) {
            return &frame->draws[i];
        }
    }
    return NULL;
}

TEST(engine_tt, shadow_plane_and_mapping_project_the_box_from_the_light)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t ground_mesh = 0;
    nmo_object_id_t ground = grid_object(&f, "Ground", 11, &ground_mesh); /* 0 to 10 in X and Z */
    nmo_object_id_t box = unit_cube(&f, "Box", 5.0f, 2.0f, 5.0f);
    nmo_object_id_t lamp = placed(&f, NMO_CID_LIGHT, "Lamp", 5.0f, 10.0f, 5.0f);
    ASSERT_TRUE(ground && box && lamp);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    floor_attribute(&f, ground);
    nmo_object_id_t plane = started_block(&f, GUID_SHADOW_PLANE, "Plane");
    nmo_object_id_t mapping = started_block(&f, GUID_SHADOW_MAPPING, "Mapping");
    ASSERT_TRUE(plane && mapping);
    const float size[2] = {1.0f, 1.0f};
    nmo_object_id_t blocks[2] = {plane, mapping};
    for (int i = 0; i < 2; i++) {
        fixture_input_int(&f, fixture_target(&f, blocks[i]), (int32_t)box);
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 0), (int32_t)lamp);
        fixture_input_value(&f, BLOCK_PIN(&f, blocks[i], 2), size, sizeof(size));
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 3), VXBLEND_ZERO);
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 4), VXBLEND_SRCCOLOR);
    }
    fixture_link(&f, f.script_in, BLOCK_IN(&f, mapping, 1), 2); /* Off two frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Plane: the light 8 above, the floor 10 under it: the box's corners 1.25 times as far out */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *quad = triangles_of(&frame, box);
    ASSERT_TRUE(quad != NULL);
    ASSERT_EQ(6, quad->vertex_count);
    ASSERT_TRUE(quad->no_z_test);
    ASSERT_EQ(VXBLEND_SRCCOLOR, quad->dest_blend);
    const nmo_engine_vertex_t *corner = &frame.vertices[quad->first_vertex];
    ASSERT_FLOAT_EQ(5.625f, corner->x, 1e-4f);
    ASSERT_FLOAT_EQ(0.0f, corner->y, 1e-4f);
    ASSERT_FLOAT_EQ(5.625f, corner->z, 1e-4f);

    /* Mapping: a channel on the floor, its UVs across that rectangle (1.25 wide round the box) */
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, ground_mesh, &out));
    ASSERT_EQ(1, out.channel_count);
    ASSERT_EQ(nmo_engine_object_named(f.engine, "SimpleShadow Material", NMO_CID_MATERIAL), out.channels[0].material);
    ASSERT_EQ(VXBLEND_ZERO, out.channels[0].source_blend);
    nmo_vector2_t under = mesh_uv(&f, ground_mesh, 0, 60), farther = mesh_uv(&f, ground_mesh, 0, 0); /* (5,0,5), (0,0,0) */
    ASSERT_FLOAT_EQ(0.5f, under.x, 1e-5f);
    ASSERT_FLOAT_EQ(0.5f, under.y, 1e-5f);
    ASSERT_FLOAT_EQ(-3.5f, farther.x, 1e-4f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_TRUE(nmo_engine_mesh(f.engine, ground_mesh, &out));
    ASSERT_EQ(0, out.channel_count); /* Off */
    fixture_end(&f);
}

/* TT RealShadowMapping: the light 8 above the box (Light-Vector from it), the floor 2 under it: the
   corners (the box's radius out) 1.25 times as far out round (5, 0, 5). Off stops the block, but
   its callback places the shadow once more first. */
TEST(engine_tt, real_shadow_mapping_throws_the_square_from_the_light)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t ground_mesh = 0;
    nmo_object_id_t ground = grid_object(&f, "Ground", 11, &ground_mesh);
    nmo_object_id_t box = unit_cube(&f, "Box", 5.0f, 2.0f, 5.0f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Shadow");
    ASSERT_TRUE(ground && box && texture);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    floor_attribute(&f, ground);
    nmo_object_id_t block = started_block(&f, GUID_REAL_SHADOW_MAPPING, "Real");
    ASSERT_TRUE(block != 0);
    const float light[3] = {0.0f, 8.0f, 0.0f}, size[2] = {1.0f, 1.0f};
    fixture_input_int(&f, fixture_target(&f, block), (int32_t)box);
    fixture_input_value(&f, BLOCK_PIN(&f, block, 0), light, sizeof(light));
    fixture_input_int(&f, BLOCK_PIN(&f, block, 1), (int32_t)texture);
    fixture_input_value(&f, BLOCK_PIN(&f, block, 2), size, sizeof(size));
    fixture_input_int(&f, BLOCK_PIN(&f, block, 3), VXBLEND_ZERO);
    fixture_input_int(&f, BLOCK_PIN(&f, block, 4), VXBLEND_SRCCOLOR);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, block, 1), 2); /* Off two frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    float r = eng_entity_radius(f.engine, eng_entity_index(f.engine, box));
    ASSERT_TRUE(r > 0.0f);
    float center[3] = {0}, corner[3] = {0};
    read_value(&f, BLOCK_POUT(&f, block, 0), center, sizeof(center));
    read_value(&f, BLOCK_POUT(&f, block, 3), corner, sizeof(corner));
    ASSERT_FLOAT_EQ(5.0f, center[0], 1e-4f);
    ASSERT_FLOAT_EQ(0.0f, center[1], 1e-4f);
    ASSERT_FLOAT_EQ(5.0f, center[2], 1e-4f);
    ASSERT_FLOAT_EQ(5.0f - 1.25f * r, corner[0], 1e-4f); /* the third corner: (-r, -r) */
    ASSERT_FLOAT_EQ(5.0f - 1.25f * r, corner[2], 1e-4f);
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, ground_mesh, &out));
    ASSERT_EQ(1, out.channel_count);
    nmo_object_id_t material = nmo_engine_object_named(f.engine, "SimpleShadow Material", NMO_CID_MATERIAL);
    ASSERT_EQ(material, out.channels[0].material);
    ASSERT_EQ(VXBLEND_ZERO, out.channels[0].source_blend);
    ASSERT_EQ(VXBLEND_SRCCOLOR, out.channels[0].dest_blend);
    nmo_engine_material_t desc;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    ASSERT_EQ(texture, desc.texture);
    nmo_vector2_t under = mesh_uv(&f, ground_mesh, 0, 60), farther = mesh_uv(&f, ground_mesh, 0, 0); /* (5,0,5), (0,0,0) */
    ASSERT_FLOAT_EQ(0.5f, under.x, 1e-5f);
    ASSERT_FLOAT_EQ(0.5f, under.y, 1e-5f);
    ASSERT_FLOAT_EQ(-(5.0f - 1.25f * r) / (2.5f * r), farther.x, 1e-4f);
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    }
    ASSERT_TRUE(nmo_engine_mesh(f.engine, ground_mesh, &out));
    ASSERT_EQ(1, out.channel_count); /* left where the last callback put it */
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, block));
    fixture_end(&f);
}

TEST(engine_tt, lens_flare_draws_its_flares_in_front_of_the_eye)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t camera = placed(&f, NMO_CID_CAMERA, "Camera", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t sun = placed(&f, NMO_CID_LIGHT, "Sun", 0.0f, 0.0f, 100.0f);
    ASSERT_TRUE(camera && sun);
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    lens->fov = 1.0f;
    lens->near_plane = 1.0f;
    lens->far_plane = 1000.0f;
    lens->aspect_width = 4;
    lens->aspect_height = 3;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t flare = started_block(&f, GUID_LENS_FLARE, "Flare");
    ASSERT_TRUE(flare != 0);
    fixture_input_int(&f, fixture_target(&f, flare), (int32_t)sun);
    const float multiplier[2] = {1.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, flare, 2), multiplier, sizeof(multiplier));
    fixture_input_int(&f, BLOCK_PIN(&f, flare, 3), VXBLEND_ONE);
    fixture_input_int(&f, BLOCK_PIN(&f, flare, 4), VXBLEND_ONE);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, flare, 1), 2); /* Off two frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* The default flare (at the light, 1 by 1) straight ahead: a quad round the axis at twice the
       near plane (1.01) */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *quad = triangles_of(&frame, sun);
    ASSERT_TRUE(quad != NULL);
    ASSERT_EQ(6, quad->vertex_count);
    const nmo_engine_vertex_t *corner = &frame.vertices[quad->first_vertex];
    ASSERT_FLOAT_EQ(-1.0f, corner->x, 1e-5f);
    ASSERT_FLOAT_EQ(1.0f, corner->y, 1e-5f);
    ASSERT_FLOAT_EQ(2.02f, corner->z, 1e-5f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(triangles_of(&frame, sun) == NULL); /* Off */
    fixture_end(&f);
}

/* The StencilShadow attribute on `object` (while editing): mesh 0, fixed, length, redrawn */
static void stencil_shadow_attribute(engine_fixture_t *f, nmo_object_id_t object, bool fixed, float length, bool redraw)
{
    const nmo_guid_t type = NMO_GUID(469842135u, 437651951u);
    nmo_manager_entry_create_options_t options = {.enabled = true, .attribute_type_guid = type,
        .category = "Visuals FX", .has_compatible_class_id = true, .compatible_class_id = NMO_CID_3DENTITY,
        .has_flags = true, .flags = 0};
    uint32_t attribute = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx),
                                                                     "StencilShadow", &options, &attribute));
    nmo_object_id_t parameter = fixture_create(f, NMO_CID_PARAMETEROUT, "StencilShadow");
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, parameter);
    state->has_state = true;
    state->mode = CKPARAM_MODE_BUFFER;
    state->type_guid = type;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, 1, 16, NULL));
    memset(state->buffer_data.data, 0, 16);
    int32_t values[4] = {0, fixed ? 1 : 0, 0, redraw ? 1 : 0};
    memcpy(&values[2], &length, sizeof(length));
    const nmo_guid_t types[4] = {CKPGUID_INT, CKPGUID_BOOL, CKPGUID_FLOAT, CKPGUID_BOOL};
    for (int i = 0; i < 4; i++) {
        nmo_object_id_t field = fixture_create(f, NMO_CID_PARAMETERLOCAL, "Field");
        nmo_parameter_state_t *value = (nmo_parameter_state_t *)fixture_state(f, field);
        value->has_state = true;
        value->mode = CKPARAM_MODE_BUFFER;
        value->type_guid = types[i];
        ASSERT_EQ(NMO_OK, nmo_array_alloc(&value->buffer_data, 1, 4, NULL));
        memcpy(value->buffer_data.data, &values[i], 4);
        memcpy((uint8_t *)state->buffer_data.data + 4 * i, &field, 4);
    }
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)fixture_state(f, object))->attributes,
                                                          parameter, attribute, NULL));
}

TEST(engine_tt, scene_stencil_shadow_extrudes_the_silhouette_and_masks_the_sprite)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t square_mesh = 0;
    nmo_object_id_t square = grid_object(&f, "Square", 2, &square_mesh); /* two faces, 1 by 1 */
    nmo_object_id_t lamp = placed(&f, NMO_CID_LIGHT, "Lamp", 0.5f, 10.0f, 0.5f);
    ASSERT_TRUE(square && lamp);
    ((nmo_3dentity_state_t *)fixture_state(&f, square))->world_matrix[13] = 2.0f;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    stencil_shadow_attribute(&f, square, false, 100.0f, true);
    nmo_object_id_t shadow = started_block(&f, GUID_SCENE_STENCIL_SHADOW, "Shadows");
    ASSERT_TRUE(shadow != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, shadow, 0), (int32_t)lamp);
    const float color[4] = {0.0f, 0.0f, 0.0f, 0.5f};
    fixture_input_value(&f, BLOCK_PIN(&f, shadow, 1), color, sizeof(color));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Lit from above, the square's four outer edges are its silhouette (the diagonal between two
       lit faces is not): five edges, four quads pushed 100 away from the light */
    nmo_object_id_t volume = nmo_engine_object_named(f.engine, "ShadowMesh", NMO_CID_MESH);
    nmo_object_id_t holder = nmo_engine_object_named(f.engine, "ShadowEnt", NMO_CID_3DENTITY);
    nmo_object_id_t sprite = nmo_engine_object_named(f.engine, "ShadowSprite", NMO_CID_2DENTITY);
    ASSERT_TRUE(volume != 0 && holder != 0 && sprite != 0);
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, volume, &out));
    ASSERT_EQ(20, out.vertex_count);
    const double reach = sqrt(0.25 + 64.0 + 0.25);
    ASSERT_FLOAT_EQ(0.0f, out.vertices[0].position.x, 0.0f);
    ASSERT_FLOAT_EQ((float)(-0.5 / reach * 100.0), out.vertices[2].position.x, 1e-3f);
    ASSERT_FLOAT_EQ((float)(-8.0 / reach * 100.0), out.vertices[2].position.y, 1e-3f);
    ASSERT_FLOAT_EQ(0.0f, out.vertices[16].position.y, 0.0f); /* the fifth slot unused */

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int volumes = 0, mirrored = 0, masked = 0, redrawn = 0;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        const nmo_engine_draw_t *draw = &frame.draws[i];
        if (draw->object == holder && draw->stencil_only) {
            volumes++;
            mirrored += draw->mirrored ? 1 : 0;
        } else if (draw->object == sprite && draw->stencil.enabled && draw->stencil.func == VXCMP_NOTEQUAL &&
                   draw->stencil.ref == 0) {
            masked++;
        } else if (draw->object == square && draw->no_z_test && draw->stencil.enabled) {
            redrawn++;
        }
    }
    ASSERT_EQ(2, volumes); /* front, then turned over */
    ASSERT_EQ(1, mirrored);
    ASSERT_EQ(1, masked);
    ASSERT_EQ(1, redrawn);
    fixture_end(&f);
}

static float out_float(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    float value = -1.0f;
    read_value(f, BLOCK_POUT(f, block, pos), &value, sizeof(value));
    return value;
}

TEST(engine_tt, sprite_blocks_place_the_sprites_and_read_the_uvs)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t panel = fixture_create(&f, NMO_CID_2DENTITY, "Panel");
    nmo_object_id_t sprite = fixture_create(&f, NMO_CID_SPRITE3D, "Sprite");
    ASSERT_TRUE(panel != 0 && sprite != 0);
    nmo_sprite3d_state_t *state = (nmo_sprite3d_state_t *)fixture_state(&f, sprite);
    state->has_data = 1;
    state->half_width = 2.0f;
    state->half_height = 3.0f;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t old = started_block(&f, GUID_SET_2D_SPRITE_OLD, "Old");
    nmo_object_id_t uv = fixture_block(&f, GUID_GET_UV_2D_SPRITE, "UV");
    nmo_object_id_t set3d = started_block(&f, GUID_SET_3D_SPRITE, "3D");
    ASSERT_TRUE(old && uv && set3d);
    fixture_link(&f, BLOCK_OUT(&f, old, 0), BLOCK_IN(&f, uv, 0), 0);
    fixture_input_int(&f, fixture_target(&f, old), (int32_t)panel);
    fixture_input_int(&f, fixture_target(&f, uv), (int32_t)panel);
    fixture_input_int(&f, fixture_target(&f, set3d), (int32_t)sprite);
    const float position[2] = {0.1f, 0.2f}, size[2] = {0.5f, 0.25f};
    const float left_right[2] = {0.25f, 0.75f}, top_bottom[2] = {0.0f, 0.5f};
    fixture_input_value(&f, BLOCK_PIN(&f, old, 0), position, sizeof(position));
    fixture_input_value(&f, BLOCK_PIN(&f, old, 1), size, sizeof(size));
    fixture_input_value(&f, BLOCK_PIN(&f, old, 2), left_right, sizeof(left_right));
    fixture_input_value(&f, BLOCK_PIN(&f, old, 3), top_bottom, sizeof(top_bottom));
    const float place[3] = {1.0f, 2.0f, 3.0f}, sprite_size[2] = {4.0f, 2.0f}, offset[2] = {0.5f, 0.0f};
    const float left_top[2] = {0.1f, 0.2f}, right_bottom[2] = {0.9f, 0.8f};
    fixture_input_value(&f, BLOCK_PIN(&f, set3d, 0), place, sizeof(place));
    fixture_input_value(&f, BLOCK_PIN(&f, set3d, 1), sprite_size, sizeof(sprite_size));
    fixture_input_value(&f, BLOCK_PIN(&f, set3d, 2), offset, sizeof(offset));
    fixture_input_value(&f, BLOCK_PIN(&f, set3d, 3), left_top, sizeof(left_top));
    fixture_input_value(&f, BLOCK_PIN(&f, set3d, 4), right_bottom, sizeof(right_bottom));
    fixture_commit(&f, NULL);
    const int32_t all_2d = 7, all_3d = 15; /* every input of "Add Input" */
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, local_of(&f, old, 0), &all_2d, sizeof(all_2d)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, local_of(&f, set3d, 0), &all_3d, sizeof(all_3d)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* The old one: the rect from the two vectors, the position and size homogeneous */
    const eng_2d_t *entity = eng_2d_entity(f.engine, panel);
    ASSERT_TRUE(entity != NULL && (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) != 0);
    ASSERT_FLOAT_EQ(0.1f, entity->homogeneous.left, 1e-6f);
    ASSERT_FLOAT_EQ(0.6f, entity->homogeneous.right, 1e-6f);
    ASSERT_FLOAT_EQ(0.45f, entity->homogeneous.bottom, 1e-6f);
    nmo_rect_t read = {0};
    read_value(&f, BLOCK_POUT(&f, uv, 0), &read, sizeof(read));
    ASSERT_FLOAT_EQ(0.25f, read.left, 0.0f);
    ASSERT_FLOAT_EQ(0.0f, read.top, 0.0f);
    ASSERT_FLOAT_EQ(0.75f, read.right, 0.0f);
    ASSERT_FLOAT_EQ(0.5f, read.bottom, 0.0f);
    /* The 3D one: sized 4 x 2, then the box moved to the offset with its size kept */
    const eng_sprite3d_t *placed_sprite = eng_sprite3d(f.engine, sprite);
    ASSERT_TRUE(placed_sprite != NULL);
    ASSERT_FLOAT_EQ(-1.0f, placed_sprite->min.x, 1e-6f);
    ASSERT_FLOAT_EQ(3.0f, placed_sprite->max.x, 1e-6f);
    ASSERT_FLOAT_EQ(-1.0f, placed_sprite->min.y, 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, placed_sprite->max.y, 1e-6f);
    ASSERT_FLOAT_EQ(0.9f, placed_sprite->uv_rect.right, 0.0f);
    ASSERT_FLOAT_EQ(0.2f, placed_sprite->uv_rect.top, 0.0f);
    eng_vector_t at = eng_entity_position(f.engine, eng_entity_index(f.engine, sprite), -1);
    ASSERT_FLOAT_EQ(2.0f, at.y, 1e-6f);
    ASSERT_FLOAT_EQ(3.0f, at.z, 1e-6f);
    fixture_end(&f);
}

TEST(engine_tt, push_button_swaps_materials_and_starts_with_the_button_down)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t frame = fixture_create(&f, NMO_CID_2DENTITY, "Frame");
    nmo_object_id_t materials[3];
    const char *names[3] = {"Released", "Pressed", "Rollover"};
    for (int i = 0; i < 3; i++) {
        materials[i] = fixture_create(&f, NMO_CID_MATERIAL, names[i]);
        ASSERT_TRUE(materials[i] != 0);
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t button = started_block(&f, GUID_TT_PUSH_BUTTON, "Button");
    ASSERT_TRUE(frame != 0 && button != 0);
    fixture_input_int(&f, fixture_target(&f, button), (int32_t)frame);
    for (int i = 0; i < 3; i++) {
        fixture_input_int(&f, BLOCK_PIN(&f, button, (size_t)i), (int32_t)materials[i]);
    }
    /* Released, Roll Over, Mouse Down, Mouse Up */
    nmo_object_id_t outputs[4];
    for (size_t i = 0; i < 4; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        fixture_link(&f, BLOCK_OUT(&f, button, i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_set_entity_under_mouse(f.engine, frame);
    /* On over the button with the mouse up: Roll Over, and Mouse Up since On left it down */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    ASSERT_EQ(1u, runs(&f, outputs[3]));
    ASSERT_EQ(materials[2], eng_2d_entity(f.engine, frame)->material);
    nmo_engine_set_mouse_button(f.engine, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    ASSERT_EQ(materials[1], eng_2d_entity(f.engine, frame)->material);
    nmo_engine_set_mouse_button(f.engine, 0, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(2u, runs(&f, outputs[3]));
    ASSERT_EQ(materials[2], eng_2d_entity(f.engine, frame)->material);
    nmo_engine_set_entity_under_mouse(f.engine, 0);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[0]));
    ASSERT_EQ(materials[0], eng_2d_entity(f.engine, frame)->material);
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    fixture_end(&f);
}

static nmo_object_id_t motor_sound(engine_fixture_t *f, const char *name)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_WAVESOUND, name);
    nmo_wavesound_state_t *state = (nmo_wavesound_state_t *)fixture_state(f, id);
    if (state == NULL) {
        return 0;
    }
    state->has_duration = 1;
    state->duration = 1000;
    return id;
}

TEST(engine_tt, motor_sound_plays_the_bands_of_the_loaded_settings)
{
    /* Two bands in the file; the third keeps the DLL's {2, 0.5, 20, 10, 0, 0} */
    static const char settings[] =
        "Volume:0.5\n"
        "PitchMax:2 PitchMin:1 vMax:10 vMin:0 vFadeIn:2 vFadeOut:8\n"
        "PitchMax:3 PitchMin:1 vMax:20 vMin:10 vFadeIn:12 vFadeOut:18\n";
    size_t length = strlen(NMO_TEST_SCRATCH_DIR) + sizeof("/motor.txt");
    char *path = (char *)malloc(length);
    ASSERT_NOT_NULL(path);
    memcpy(path, NMO_TEST_SCRATCH_DIR, strlen(NMO_TEST_SCRATCH_DIR));
    memcpy(path + strlen(NMO_TEST_SCRATCH_DIR), "/motor.txt", sizeof("/motor.txt"));
    FILE *out = fopen(path, "wb");
    free(path);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(sizeof(settings) - 1u, fwrite(settings, 1, sizeof(settings) - 1u, out));
    fclose(out);

    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sounds[3] = {motor_sound(&f, "Low"), motor_sound(&f, "Middle"), motor_sound(&f, "High")};
    ASSERT_TRUE(sounds[0] && sounds[1] && sounds[2]);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t load = started_block(&f, GUID_LOAD_MOTOR_SETTINGS, "Load");
    nmo_object_id_t motor = fixture_block(&f, GUID_MOTOR_SOUND, "Motor");
    nmo_object_id_t missing = started_block(&f, GUID_LOAD_MOTOR_SETTINGS, "Missing");
    nmo_object_id_t error = fixture_block(&f, GUID_NOP, "Error");
    ASSERT_TRUE(load && motor && missing && error);
    input_text(&f, BLOCK_PIN(&f, load, 0), "motor.txt");
    input_text(&f, BLOCK_PIN(&f, missing, 0), "no motor.txt");
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, error, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, load, 0), BLOCK_IN(&f, motor, 0), 0);
    const float speed = 1.0f, global_volume = 0.8f;
    fixture_input_value(&f, BLOCK_PIN(&f, motor, 0), &speed, sizeof(speed));
    for (int i = 0; i < 3; i++) {
        fixture_input_int(&f, BLOCK_PIN(&f, motor, (size_t)i + 1u), (int32_t)sounds[i]);
    }
    fixture_input_value(&f, BLOCK_PIN(&f, motor, 4), &global_volume, sizeof(global_volume));
    nmo_object_id_t speed_source =
        nmo_parameterin_source_id((const nmo_parameterin_state_t *)fixture_state(&f, BLOCK_PIN(&f, motor, 0)));
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, error));
    /* Speed 1 is in the first band only: pitch 1 + 1/10, half way up its fade in */
    ASSERT_FLOAT_EQ(1.1f, out_float(&f, motor, 0), 1e-6f);
    ASSERT_FLOAT_EQ(0.5f, out_float(&f, motor, 1), 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, out_float(&f, motor, 3), 0.0f);
    const eng_sound_t *low = eng_sound(f.engine, sounds[0]);
    ASSERT_TRUE(low->playing && low->loop);
    ASSERT_FLOAT_EQ(0.2f, low->gain, 1e-6f); /* 0.5 x 0.8 x 0.5 */
    ASSERT_FALSE(eng_sound(f.engine, sounds[1])->playing); /* started silent, then stopped */
    ASSERT_TRUE(eng_sound(f.engine, sounds[1])->loop);

    const float faster = 15.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, speed_source, &faster, sizeof(faster)));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FALSE(eng_sound(f.engine, sounds[0])->playing);
    ASSERT_FLOAT_EQ(0.0f, out_float(&f, motor, 0), 0.0f);
    ASSERT_FLOAT_EQ(2.0f, out_float(&f, motor, 2), 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, out_float(&f, motor, 3), 0.0f);
    ASSERT_TRUE(eng_sound(f.engine, sounds[1])->playing);
    ASSERT_FLOAT_EQ(0.4f, eng_sound(f.engine, sounds[1])->gain, 1e-6f);
    /* The default third band fades out from 0: 1 - 15/20 */
    ASSERT_FLOAT_EQ(1.25f, out_float(&f, motor, 4), 1e-6f);
    ASSERT_FLOAT_EQ(0.25f, out_float(&f, motor, 5), 1e-6f);
    ASSERT_FLOAT_EQ(1.25f, eng_sound(f.engine, sounds[2])->pitch, 1e-6f);
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, motor));
    fixture_end(&f);
}

TEST(engine_tt, font_coordinates_fill_a_new_array)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Font Texture");
    ASSERT_TRUE(texture != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t font = started_block(&f, GUID_CREATE_FONT, "Font");
    nmo_object_id_t coordinates = fixture_block(&f, GUID_FONT_COORDINATES, "Coordinates");
    nmo_object_id_t none = started_block(&f, GUID_FONT_COORDINATES, "None");
    ASSERT_TRUE(font && coordinates && none);
    fixture_link(&f, BLOCK_OUT(&f, font, 0), BLOCK_IN(&f, coordinates, 0), 0);
    input_text(&f, BLOCK_PIN(&f, font, 0), "Grid");
    fixture_input_int(&f, BLOCK_PIN(&f, font, 1), (int32_t)texture);
    const float characters[2] = {16.0f, 8.0f};
    const nmo_rect_t zone = {0.0f, 0.0f, 16.0f, 8.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, font, 2), characters, sizeof(characters));
    fixture_input_int(&f, BLOCK_PIN(&f, font, 3), 0); /* fixed width */
    fixture_input_value(&f, BLOCK_PIN(&f, font, 4), &zone, sizeof(zone));
    input_text(&f, BLOCK_PIN(&f, coordinates, 0), "GridData");
    fixture_input_int(&f, BLOCK_PIN(&f, coordinates, 1), 1);
    input_text(&f, BLOCK_PIN(&f, none, 0), "NoData");
    fixture_input_int(&f, BLOCK_PIN(&f, none, 1), 7);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_texture_t *bitmap = eng_texture(f.engine, texture);
    ASSERT_TRUE(bitmap != NULL && eng_texture_create(f.engine, bitmap, 16, 8, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_object_id_t array = 0;
    read_value(&f, BLOCK_POUT(&f, coordinates, 0), &array, sizeof(array));
    ASSERT_TRUE(array != 0);
    ASSERT_EQ(array, eng_object_named(f.engine, "GridData", NMO_CID_DATAARRAY));
    ASSERT_EQ(6, nmo_engine_array_column_count(f.engine, array));
    ASSERT_STR_EQ("upostwidth", nmo_engine_array_column_name(f.engine, array, 4));
    ASSERT_EQ(255, nmo_engine_array_row_count(f.engine, array)); /* character 255 left out */
    /* Character 17 of a 16 x 8 grid: the second column of the second row */
    nmo_engine_cell_t cell;
    float value = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 17, 0, &cell));
    memcpy(&value, &cell.value, sizeof(value));
    ASSERT_FLOAT_EQ(1.0f / 16.0f, value, 1e-6f);
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 17, 5, &cell));
    memcpy(&value, &cell.value, sizeof(value));
    ASSERT_FLOAT_EQ(1.0f / 8.0f, value, 1e-6f);
    /* No font 7: the columns, no rows */
    read_value(&f, BLOCK_POUT(&f, none, 0), &array, sizeof(array));
    ASSERT_EQ(6, nmo_engine_array_column_count(f.engine, array));
    ASSERT_EQ(0, nmo_engine_array_row_count(f.engine, array));
    fixture_end(&f);
}

/* A host file system that records what it is asked */
typedef struct fake_files {
    int copies;
    bool last_overwrite;
    char last_copy[64];
    int removes;
} fake_files_t;

static bool fake_exists(void *user_data, const char *name)
{
    (void)user_data;
    return strcmp(name, "Data\\here.txt") == 0;
}

static bool fake_copy(void *user_data, const char *source, const char *destination, bool overwrite)
{
    fake_files_t *files = (fake_files_t *)user_data;
    files->copies++;
    files->last_overwrite = overwrite;
    size_t length = strlen(source);
    if (length < sizeof(files->last_copy)) {
        memcpy(files->last_copy, source, length + 1u);
    }
    return strcmp(destination, "Save\\copy.txt") == 0;
}

static bool fake_remove(void *user_data, const char *name)
{
    (void)name;
    ((fake_files_t *)user_data)->removes++;
    return false; /* in use */
}

static bool fake_list(void *user_data, const char *pattern, void (*found)(void *, const char *), void *context)
{
    (void)user_data;
    if (strcmp(pattern, "Lv\\*.nmo") != 0) {
        return false;
    }
    const char *names[4] = {".", "..", "Level_01.nmo", "Level_02.nmo"};
    for (int i = 0; i < 4; i++) {
        found(context, names[i]);
    }
    return true;
}

TEST(engine_tt, file_blocks_go_through_the_host_file_system)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t levels = fixture_create(&f, NMO_CID_DATAARRAY, "Levels");
    ASSERT_TRUE(levels != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t copy = started_block(&f, GUID_COPY_FILE, "Copy");
    nmo_object_id_t remove = started_block(&f, GUID_DELETE_FILE, "Delete");
    nmo_object_id_t here = started_block(&f, GUID_EXIST_FILE, "Here");
    nmo_object_id_t gone = started_block(&f, GUID_EXIST_FILE, "Gone");
    nmo_object_id_t name = started_block(&f, GUID_FILE_NAME_FROM_PATH, "Name");
    nmo_object_id_t slash = started_block(&f, GUID_FILE_NAME_FROM_PATH, "Slash");
    nmo_object_id_t list = started_block(&f, GUID_LIST_DIR, "List");
    nmo_object_id_t make = started_block(&f, GUID_CREATE_DIRECTORY, "Make");
    ASSERT_TRUE(copy && remove && here && gone && name && slash && list && make);
    input_text(&f, BLOCK_PIN(&f, copy, 0), "Data\\here.txt");
    input_text(&f, BLOCK_PIN(&f, copy, 1), "Save\\copy.txt");
    fixture_input_int(&f, BLOCK_PIN(&f, copy, 2), 0);
    input_text(&f, BLOCK_PIN(&f, remove, 0), "Data\\here.txt");
    input_text(&f, BLOCK_PIN(&f, here, 0), "Data\\here.txt");
    input_text(&f, BLOCK_PIN(&f, gone, 0), "Data\\gone.txt");
    input_text(&f, BLOCK_PIN(&f, name, 0), "C:\\B\\base.cmo");
    input_text(&f, BLOCK_PIN(&f, slash, 0), "3D/PH/Box.nmo");
    input_text(&f, BLOCK_PIN(&f, list, 0), "Lv\\*.nmo");
    fixture_input_int(&f, BLOCK_PIN(&f, list, 1), (int32_t)levels);
    input_text(&f, BLOCK_PIN(&f, make, 0), "Save");
    /* OK and Fail (Yes and No) of each */
    nmo_object_id_t outs[5][2];
    const nmo_object_id_t blocks[5] = {copy, remove, here, gone, make};
    for (int i = 0; i < 5; i++) {
        watch_outputs(&f, blocks[i], outs[i]);
    }
    fixture_commit(&f, NULL);
    const int32_t use_slash = 1;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, local_of(&f, slash, 0), &use_slash, sizeof(use_slash)));
    fake_files_t record;
    memset(&record, 0, sizeof(record));
    const nmo_engine_file_system_t files = {&record, fake_exists, fake_copy, fake_remove, NULL, NULL, NULL, fake_list};
    nmo_engine_set_file_system(f.engine, &files);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, levels, -1, CKARRAYTYPE_STRING, NMO_GUID(0u, 0u)));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, record.copies);
    ASSERT_FALSE(record.last_overwrite); /* fails on an existing destination */
    ASSERT_STR_EQ("Data\\here.txt", record.last_copy);
    ASSERT_EQ(1u, runs(&f, outs[0][0]));
    ASSERT_EQ(1u, runs(&f, outs[1][1])); /* the host refused */
    ASSERT_EQ(1u, runs(&f, outs[2][0]));
    ASSERT_EQ(1u, runs(&f, outs[3][1]));
    ASSERT_EQ(1u, runs(&f, outs[4][1])); /* no directory operation from the host */
    const void *text = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, name, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("base.cmo", (const char *)text);
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, slash, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("Box.nmo", (const char *)text);
    ASSERT_EQ(2, nmo_engine_array_row_count(f.engine, levels)); /* without "." and ".." */
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, levels, 1, 0, &cell));
    ASSERT_STR_EQ("Level_02.nmo", cell.text);
    /* Their inputs stay set, but nothing runs them again */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, record.copies);
    ASSERT_EQ(2, nmo_engine_array_row_count(f.engine, levels));
    fixture_end(&f);
}

/* A chain of blocks, each run from the one before's output `outs[i]` in the same frame */
static void chain(engine_fixture_t *f, const nmo_object_id_t *blocks, const int *outs, int count)
{
    fixture_link(f, f->script_in, BLOCK_IN(f, blocks[0], 0), 0);
    for (int i = 1; i < count; i++) {
        fixture_link(f, BLOCK_OUT(f, blocks[i - 1], outs[i - 1]), BLOCK_IN(f, blocks[i], 0), 0);
    }
}

TEST(engine_tt, game_info_blocks_keep_the_game_and_level_fields)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t early = fixture_block(&f, GUID_GET_GAME_SCORE, "Early");
    nmo_object_id_t create = fixture_block(&f, GUID_CREATE_GAME_INFO, "Create");
    nmo_object_id_t again = fixture_block(&f, GUID_CREATE_GAME_INFO, "Again");
    nmo_object_id_t exists = fixture_block(&f, GUID_EXISTS_GAME_INFO, "Exists");
    nmo_object_id_t set_level = fixture_block(&f, GUID_SET_LEVEL_DATA, "Set Level");
    nmo_object_id_t set_id = fixture_block(&f, GUID_SET_LEVEL_ID, "Set ID");
    nmo_object_id_t get_level = fixture_block(&f, GUID_GET_LEVEL_DATA, "Get Level");
    nmo_object_id_t get_id = fixture_block(&f, GUID_GET_LEVEL_ID, "Get ID");
    nmo_object_id_t set_name = fixture_block(&f, GUID_SET_GAME_NAME, "Set Name");
    nmo_object_id_t get_name = fixture_block(&f, GUID_GET_GAME_NAME, "Get Name");
    nmo_object_id_t wrong = fixture_block(&f, GUID_DELETE_GAME_INFO, "Wrong");
    nmo_object_id_t remove = fixture_block(&f, GUID_DELETE_GAME_INFO, "Delete");
    nmo_object_id_t gone = fixture_block(&f, GUID_EXISTS_GAME_INFO, "Gone");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    const nmo_object_id_t blocks[] = {early, create, again, exists, set_level, set_id, get_level,
                                      get_id, set_name, get_name, wrong, remove, gone, done};
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        ASSERT_TRUE(blocks[i] != 0);
    }
    /* Exists: True; Wrong (another address): Not Found; Gone: False */
    const int outs[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1};
    chain(&f, blocks, outs, 14);
    input_text(&f, BLOCK_PIN(&f, create, 0), "Software\\Game\\");
    input_text(&f, BLOCK_PIN(&f, again, 0), "Other\\");
    fixture_input_int(&f, BLOCK_PIN(&f, exists, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, set_level, 0), 5);
    fixture_input_int(&f, BLOCK_PIN(&f, set_level, 1), 6);
    fixture_input_int(&f, BLOCK_PIN(&f, set_level, 2), 7);
    fixture_input_int(&f, BLOCK_PIN(&f, set_id, 0), 9);
    input_text(&f, BLOCK_PIN(&f, set_name, 0), "Ballance");
    fixture_input_int(&f, BLOCK_PIN(&f, wrong, 0), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, gone, 0), 1);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, done));
    /* Without a game info the getter posts TT_MSG_NO_GAMEINFO with its number and goes on */
    ASSERT_EQ(1, nmo_engine_player_message_count(f.engine));
    nmo_engine_player_message_t message = {0};
    ASSERT_TRUE(nmo_engine_player_message(f.engine, 0, &message));
    ASSERT_EQ((int64_t)NMO_ENGINE_PLAYER_NO_GAMEINFO, (int64_t)message.message);
    ASSERT_EQ(4, message.param);
    /* The first address is 1; a second create leaves its output alone */
    int32_t address = -1;
    read_value(&f, BLOCK_POUT(&f, create, 0), &address, sizeof(address));
    ASSERT_EQ(1, address);
    address = -1;
    read_value(&f, BLOCK_POUT(&f, again, 0), &address, sizeof(address));
    ASSERT_EQ(0, address);
    /* Set Level ID writes the level bonus, as the DLL does; the level ID stays 0 */
    int32_t level[3] = {-1, -1, -1}, id = -1;
    for (int i = 0; i < 3; i++) {
        read_value(&f, BLOCK_POUT(&f, get_level, i), &level[i], sizeof(level[i]));
    }
    read_value(&f, BLOCK_POUT(&f, get_id, 0), &id, sizeof(id));
    ASSERT_EQ(5, level[0]);
    ASSERT_EQ(9, level[1]);
    ASSERT_EQ(7, level[2]);
    ASSERT_EQ(0, id);
    const void *name = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, get_name, 0), &name, NULL, NULL));
    ASSERT_STR_EQ("Ballance", (const char *)name);
    /* The next frame's messages replace these */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(0, nmo_engine_player_message_count(f.engine));
    fixture_end(&f);
}

TEST(engine_tt, registry_blocks_write_and_read_the_game_key)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t table = fixture_create(&f, NMO_CID_DATAARRAY, "Table");
    ASSERT_TRUE(table != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t no_info = fixture_block(&f, GUID_SET_INT_REGISTRY, "No Info");
    nmo_object_id_t create = fixture_block(&f, GUID_CREATE_GAME_INFO, "Create");
    nmo_object_id_t set_int = fixture_block(&f, GUID_SET_INT_REGISTRY, "Set Int");
    nmo_object_id_t set_float = fixture_block(&f, GUID_SET_FLOAT_REGISTRY, "Set Float");
    nmo_object_id_t get_int = fixture_block(&f, GUID_GET_INT_REGISTRY, "Get Int");
    nmo_object_id_t no_value = fixture_block(&f, GUID_GET_INT_REGISTRY, "No Value");
    nmo_object_id_t no_key = fixture_block(&f, GUID_GET_INT_REGISTRY, "No Key");
    nmo_object_id_t get_float = fixture_block(&f, GUID_GET_FLOAT_REGISTRY, "Get Float");
    nmo_object_id_t write = fixture_block(&f, GUID_WRITE_REGISTRY, "Write");
    nmo_object_id_t write_array = fixture_block(&f, GUID_WRITE_REGISTRY, "Write Array");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    const nmo_object_id_t blocks[] = {no_info, create, set_int, set_float, get_int, no_value,
                                      no_key, get_float, write, write_array, done};
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        ASSERT_TRUE(blocks[i] != 0);
    }
    /* No Info: FAILED; the missing value and key: FAILED */
    const int outs[] = {1, 0, 0, 0, 0, 1, 1, 0, 0, 0};
    chain(&f, blocks, outs, 11);
    input_text(&f, BLOCK_PIN(&f, create, 0), "Software\\Game\\");
    fixture_input_int(&f, BLOCK_PIN(&f, set_int, 0), 42);
    input_text(&f, BLOCK_PIN(&f, set_int, 1), "Keys");
    input_text(&f, BLOCK_PIN(&f, set_int, 2), "Up");
    const float quarter = 0.25f;
    fixture_input_value(&f, BLOCK_PIN(&f, set_float, 0), &quarter, sizeof(quarter));
    input_text(&f, BLOCK_PIN(&f, set_float, 1), "Keys");
    input_text(&f, BLOCK_PIN(&f, set_float, 2), "Speed");
    input_text(&f, BLOCK_PIN(&f, get_int, 0), "keys\\");
    input_text(&f, BLOCK_PIN(&f, get_int, 1), "UP");
    input_text(&f, BLOCK_PIN(&f, no_value, 0), "Keys");
    input_text(&f, BLOCK_PIN(&f, no_value, 1), "Down");
    input_text(&f, BLOCK_PIN(&f, no_key, 0), "Mouse");
    input_text(&f, BLOCK_PIN(&f, no_key, 1), "Up");
    input_text(&f, BLOCK_PIN(&f, get_float, 0), "Keys");
    input_text(&f, BLOCK_PIN(&f, get_float, 1), "Speed");
    input_text(&f, BLOCK_PIN(&f, write, 0), "Software\\W");
    input_text(&f, BLOCK_PIN(&f, write, 1), "Lives");
    fixture_input_int(&f, BLOCK_PIN(&f, write, 3), 3);
    input_text(&f, BLOCK_PIN(&f, write_array, 0), "Software\\W");
    fixture_input_int(&f, BLOCK_PIN(&f, write_array, 2), (int32_t)table);
    nmo_object_id_t save_array = local_of(&f, write_array, 0);
    ASSERT_TRUE(save_array != 0);
    fixture_commit(&f, NULL);
    const int32_t on = 1;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, save_array, &on, sizeof(on)));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, table, -1, CKARRAYTYPE_INT,
                                                     NMO_GUID(0u, 0u)));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, table, -1, CKARRAYTYPE_STRING,
                                                     NMO_GUID(0u, 0u)));
    for (int32_t r = 0; r < 2; r++) {
        ASSERT_EQ(NMO_OK, nmo_engine_array_insert_row(f.engine, table, -1, NULL));
        nmo_engine_cell_t number = {.type = CKARRAYTYPE_INT, .value = (uint32_t)(10 + r)};
        nmo_engine_cell_t text = {.type = CKARRAYTYPE_STRING, .text = r == 0 ? "a" : "b"};
        ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, table, r, 0, &number));
        ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, table, r, 1, &text));
    }

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, done));
    /* The game info's key and RegKey are joined as they are */
    ASSERT_STR_EQ("42", nmo_engine_registry_value(f.engine, "Software\\Game\\Keys", "Up"));
    ASSERT_STR_EQ("0.25", nmo_engine_registry_value(f.engine, "Software\\Game\\Keys", "Speed"));
    int32_t value = 0;
    read_value(&f, BLOCK_POUT(&f, get_int, 0), &value, sizeof(value));
    ASSERT_EQ(42, value);
    /* A missing value outputs 2, a missing key 1 */
    read_value(&f, BLOCK_POUT(&f, no_value, 0), &value, sizeof(value));
    ASSERT_EQ(2, value);
    read_value(&f, BLOCK_POUT(&f, no_key, 0), &value, sizeof(value));
    ASSERT_EQ(1, value);
    float speed = 0.0f;
    read_value(&f, BLOCK_POUT(&f, get_float, 0), &speed, sizeof(speed));
    ASSERT_FLOAT_EQ(0.25f, speed, 0.0f);
    /* TT_WriteRegistry writes under the section alone; the array as TT_ReadRegistry reads it */
    ASSERT_STR_EQ("3", nmo_engine_registry_value(f.engine, "Software\\W", "Lives"));
    ASSERT_STR_EQ("11", nmo_engine_registry_value(f.engine, "Software\\W\\Table\\0", "1"));
    ASSERT_STR_EQ("a", nmo_engine_registry_value(f.engine, "Software\\W\\Table\\1", "0"));
    fixture_end(&f);
}

static void set_int_cell_value(engine_fixture_t *f, nmo_object_id_t array, int32_t column,
                               int32_t value)
{
    nmo_engine_cell_t cell = {.type = CKARRAYTYPE_INT, .value = (uint32_t)value};
    ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f->engine, array, 0, column, &cell));
}

TEST(engine_tt, installed_arrays_keep_their_first_row_across_frames)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = fixture_create(&f, NMO_CID_DATAARRAY, "Kept");
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t install = fixture_block(&f, GUID_INSTALL_ARRAY, "Install");
    nmo_object_id_t read = fixture_block(&f, GUID_READ_ARRAY, "Read");
    nmo_object_id_t write = fixture_block(&f, GUID_WRITE_ARRAY, "Write");
    nmo_object_id_t read_again = fixture_block(&f, GUID_READ_ARRAY, "Read Again");
    nmo_object_id_t uninstall = fixture_block(&f, GUID_UNINSTALL_ARRAY, "Uninstall");
    nmo_object_id_t read_none = fixture_block(&f, GUID_READ_ARRAY, "Read None");
    const nmo_object_id_t blocks[] = {install, read, write, read_again, uninstall, read_none};
    /* one block a frame */
    fixture_link(&f, f.script_in, BLOCK_IN(&f, install, 0), 0);
    for (int i = 0; i < 6; i++) {
        ASSERT_TRUE(blocks[i] != 0);
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 0), (int32_t)array);
        input_text(&f, BLOCK_PIN(&f, blocks[i], 1), "level.cmo");
        if (i > 0) {
            fixture_link(&f, BLOCK_OUT(&f, blocks[i - 1], 0), BLOCK_IN(&f, blocks[i], 0), 1);
        }
    }
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, array, -1, CKARRAYTYPE_INT,
                                                     NMO_GUID(0u, 0u)));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, array, -1, CKARRAYTYPE_STRING,
                                                     NMO_GUID(0u, 0u)));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_row(f.engine, array, -1, NULL));
    set_int_cell_value(&f, array, 0, 5);
    nmo_engine_cell_t text = {.type = CKARRAYTYPE_STRING, .text = "a"};
    ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, array, 0, 1, &text));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* Install keeps 5, "a" */
    ASSERT_EQ(1u, runs(&f, install));
    set_int_cell_value(&f, array, 0, 9);
    text.text = "b";
    ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, array, 0, 1, &text));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* Read puts them back */
    ASSERT_EQ(1u, runs(&f, read));
    ASSERT_EQ(5, cell_int(&f, array, 0, 0));
    nmo_engine_cell_t cell = {0};
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 0, 1, &cell));
    ASSERT_STR_EQ("a", cell.text);
    set_int_cell_value(&f, array, 0, 7);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* Write keeps 7 */
    set_int_cell_value(&f, array, 0, 0);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* Read Again */
    ASSERT_EQ(1u, runs(&f, read_again));
    ASSERT_EQ(7, cell_int(&f, array, 0, 0));
    set_int_cell_value(&f, array, 0, 0);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* Uninstall forgets it */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* so Read None changes nothing */
    ASSERT_EQ(1u, runs(&f, read_none));
    ASSERT_EQ(0, cell_int(&f, array, 0, 0));
    fixture_end(&f);
}

TEST(engine_tt, player_blocks_post_messages_and_keep_flags)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t no_info = fixture_block(&f, GUID_LOAD_CMO, "No Info");
    nmo_object_id_t limit = fixture_block(&f, GUID_LIMIT_FRAMERATE, "Limit");
    nmo_object_id_t create = fixture_block(&f, GUID_CREATE_GAME_INFO, "Create");
    nmo_object_id_t load = fixture_block(&f, GUID_LOAD_CMO, "Load");
    nmo_object_id_t restart = fixture_block(&f, GUID_RESTART_CMO, "Restart");
    nmo_object_id_t rookie = fixture_block(&f, GUID_SET_ROOKIE, "Rookie");
    nmo_object_id_t is_rookie = fixture_block(&f, GUID_IS_ROOKIE, "Is Rookie");
    nmo_object_id_t window = fixture_block(&f, GUID_WINDOW_ACTIVATE, "Window");
    nmo_object_id_t window_again = fixture_block(&f, GUID_WINDOW_ACTIVATE, "Window Again");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(no_info != 0 && limit != 0 && create != 0 && load != 0 && restart != 0 &&
                rookie != 0 && is_rookie != 0 && window != 0 && window_again != 0 && done != 0);
    /* Load CMO without a game info stops the chain: Limit starts from the script */
    fixture_link(&f, f.script_in, BLOCK_IN(&f, no_info, 0), 0);
    const nmo_object_id_t blocks[] = {limit, create, load, restart, rookie, is_rookie, window,
                                      window_again, done};
    /* Window: Activate; Window Again: else */
    const int outs[] = {0, 0, 0, 0, 0, 0, 0, 1};
    chain(&f, blocks, outs, 9);
    fixture_input_int(&f, BLOCK_PIN(&f, limit, 0), 30);
    input_text(&f, BLOCK_PIN(&f, load, 0), "Level_02.nmo");
    fixture_input_int(&f, BLOCK_PIN(&f, rookie, 0), 1);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_set_window_activated(f.engine);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, done));
    ASSERT_EQ(1u, runs(&f, limit));
    ASSERT_FALSE(nmo_engine_io_is_active(f.engine, BLOCK_IN(&f, limit, 0)));
    const uint32_t expected[] = {NMO_ENGINE_PLAYER_LIMIT_FPS, NMO_ENGINE_PLAYER_CMO_LOAD,
                                 NMO_ENGINE_PLAYER_CMO_RESTART};
    ASSERT_EQ(3, nmo_engine_player_message_count(f.engine));
    for (int i = 0; i < 3; i++) {
        nmo_engine_player_message_t message = {0};
        ASSERT_TRUE(nmo_engine_player_message(f.engine, i, &message));
        ASSERT_EQ((int64_t)expected[i], (int64_t)message.message);
        if (i == 0) {
            ASSERT_EQ(30, message.param);
        } else if (i == 1) {
            ASSERT_STR_EQ("Level_02.nmo", message.text);
        }
    }
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
REGISTER_TEST(engine_tt, fsaa_bitmap_font_and_frustum_clipping);
REGISTER_TEST(engine_tt, sky_around_follows_the_viewpoint);
REGISTER_TEST(engine_tt, shadow_plane_and_mapping_project_the_box_from_the_light);
REGISTER_TEST(engine_tt, real_shadow_mapping_throws_the_square_from_the_light);
REGISTER_TEST(engine_tt, lens_flare_draws_its_flares_in_front_of_the_eye);
REGISTER_TEST(engine_tt, scene_stencil_shadow_extrudes_the_silhouette_and_masks_the_sprite);
REGISTER_TEST(engine_tt, sprite_blocks_place_the_sprites_and_read_the_uvs);
REGISTER_TEST(engine_tt, push_button_swaps_materials_and_starts_with_the_button_down);
REGISTER_TEST(engine_tt, motor_sound_plays_the_bands_of_the_loaded_settings);
REGISTER_TEST(engine_tt, font_coordinates_fill_a_new_array);
REGISTER_TEST(engine_tt, file_blocks_go_through_the_host_file_system);
REGISTER_TEST(engine_tt, game_info_blocks_keep_the_game_and_level_fields);
REGISTER_TEST(engine_tt, registry_blocks_write_and_read_the_game_key);
REGISTER_TEST(engine_tt, installed_arrays_keep_their_first_row_across_frames);
REGISTER_TEST(engine_tt, player_blocks_post_messages_and_keep_flags);
TEST_MAIN_END()
