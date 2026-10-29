#include "engine_fixture.h"

#include "type/nmo_type_guids.h"

#include <stdio.h>
#include <stdlib.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_SET_DATABASE_PROPERTIES NMO_GUID(0x1436624f, 0x34e1290a)
#define GUID_LOAD_DATABASE NMO_GUID(0x05441494, 0x38ac7789)
#define GUID_READ_REGISTRY NMO_GUID(0x460044b5, 0x6e927b66)
#define GUID_LIST_DRIVER NMO_GUID(0x62d00456, 0x30eb4245)
#define GUID_LIST_SCREEN_MODES NMO_GUID(0x4e7a0194, 0x040328fd)
#define GUID_TT_TIMER NMO_GUID(0x6ac67901, 0x7d2a6059)
#define GUID_SPLIT_STRING NMO_GUID(0x5ae74e2f, 0x799c49b5)
#define GUID_LOAD_STRING NMO_GUID(0x391555d6, 0x42f2500e)

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

/* The database file TT Database Manager reads: one array, scrambled as TT scrambles it */
static void write_database(const char *name)
{
    tdb_t array = {0};
    tdb_int(&array, 3); /* columns */
    tdb_int(&array, 2); /* rows */
    tdb_int(&array, 1); /* key column */
    tdb_text(&array, "Name");
    tdb_int(&array, CKARRAYTYPE_STRING);
    tdb_text(&array, "Points");
    tdb_int(&array, CKARRAYTYPE_INT);
    tdb_text(&array, "Ratio");
    tdb_int(&array, CKARRAYTYPE_FLOAT);
    tdb_text(&array, "Ann"); /* column by column */
    tdb_text(&array, "Bob");
    tdb_int(&array, 4200);
    tdb_int(&array, 3100);
    const float ratios[2] = {0.5f, 0.25f};
    tdb_bytes(&array, ratios, sizeof(ratios));

    tdb_t file = {0};
    tdb_text(&file, "Other"); /* a chunk before it */
    tdb_int(&file, 4);
    tdb_int(&file, 0);
    tdb_text(&file, "Scores");
    tdb_int(&file, (int32_t)array.size);
    tdb_bytes(&file, array.data, array.size);
    for (size_t i = 0; i < file.size; i++) {
        /* The reader turns b into -(rotl(b, 3) ^ 0xAF) */
        unsigned char x = (unsigned char)((unsigned char)-file.data[i] ^ 0xAFu);
        file.data[i] = (unsigned char)((x >> 3) | (x << 5));
    }

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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_tt, load_database_fills_the_array_of_its_name);
REGISTER_TEST(engine_tt, read_registry_reads_what_the_host_set);
REGISTER_TEST(engine_tt, drivers_and_screen_modes_fill_their_arrays);
REGISTER_TEST(engine_tt, timer_and_strings);
TEST_MAIN_END()
