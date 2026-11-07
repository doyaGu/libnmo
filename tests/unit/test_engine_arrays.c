#include "engine_fixture.h"

#include "core/nmo_array.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_GET_CELL NMO_GUID(0x33b99f51, 0x07d95c45)
#define GUID_SET_CELL NMO_GUID(0x30ed1c6d, 0x4a3b7067)
#define GUID_GET_ROW NMO_GUID(0x33b77f41, 0x07b95c45)
#define GUID_ADD_ROW NMO_GUID(0x1c7e5dc6, 0x3f6423c2)
#define GUID_ITERATOR NMO_GUID(0x198f0af9, 0x0268249f)
#define GUID_ITERATOR_IF NMO_GUID(0x6bec4be6, 0x12d64c7c)
#define GUID_GROUP_ITERATOR NMO_GUID(0x6050252f, 0x3aa82d40)
#define GUID_OBJECT_DELETE NMO_GUID(0x74120ded, 0x76524673)
#define GUID_CLEAR_ARRAY NMO_GUID(0x35c9352f, 0x7b1a193b)
#define GUID_REMOVE_KEY_ROW NMO_GUID(0x0f8334ea, 0x279a40cd)
#define GUID_GET_HIGHEST NMO_GUID(0x46f71e13, 0x13d26f61)
#define GUID_IS_IN_GROUP NMO_GUID(0x58a1210b, 0x14e5715c)
#define GUID_GET_KEY_ROW NMO_GUID(0x49064205, 0x10e72f7a)
#define GUID_ROW_SEARCH NMO_GUID(0x78863443, 0x45af59b6)
#define GUID_SHUFFLE_ORDER NMO_GUID(0x270f177a, 0x4ca20023)
#define GUID_REMOVE_ROW_IF NMO_GUID(0x57865622, 0x662d2fee)
#define GUID_REMOVE_COLUMN NMO_GUID(0x3f377888, 0x128a7767)
#define GUID_INSERT_COLUMN NMO_GUID(0x05cb7802, 0x04a64a58)
#define GUID_COLUMN_SUM NMO_GUID(0x49124252, 0x1be909a4)
#define GUID_VALUE_COUNT NMO_GUID(0x534377de, 0x75fd478a)
#define GUID_TEST_CELL NMO_GUID(0x4e6c6da8, 0x636904fc)
#define GUID_CHANGE_VALUE_IF NMO_GUID(0x07253edb, 0x4d1237ed)
#define GUID_SORT_ROWS NMO_GUID(0x6f623e68, 0x62bb5a98)
#define GUID_REMOVE_ROW NMO_GUID(0x1fa57136, 0x14310857)
#define GUID_COLLECTION_ITERATOR NMO_GUID(0x419602ec, 0x7822779e)
#define GUID_GET_LOWEST NMO_GUID(0x71b25ed7, 0x3c365a30)
#define GUID_GET_NEAREST NMO_GUID(0x0fac60ad, 0x1bd31ac9)
#define GUID_COLUMN_PRODUCT NMO_GUID(0x39f64a9a, 0x682b721c)
#define GUID_COLUMN_OPERATION NMO_GUID(0x32351c65, 0x4f6916be)
#define GUID_COLUMN_TRANSFORM NMO_GUID(0x4f8f0dfe, 0x7c517321)
#define GUID_REVERSE_ORDER NMO_GUID(0x3b7d3db7, 0x519f015b)
#define GUID_MOVE_ROW NMO_GUID(0x101a62f4, 0x6e2d25f6)
#define GUID_SWAP_ROWS NMO_GUID(0x2ed72740, 0x406b5027)
#define GUID_MAKE_ROWS_UNIQUE NMO_GUID(0x2b175818, 0x2c270b20)
#define GUID_MOVE_COLUMN NMO_GUID(0x1ace1319, 0x66b112b1)
#define GUID_GET_COLUMN_NAME NMO_GUID(0x33b7ff41, 0x07b95c44)
#define GUID_SET_KEY_ROW NMO_GUID(0x2fff4c63, 0x4fcb1b94)
#define GUID_CREATE_GROUP_FROM_ARRAY NMO_GUID(0x22c71529, 0x6bb265d2)

/* Columns: Key (int), Weight (float), Name (string), Object (object). */
static nmo_object_id_t create_array(engine_fixture_t *f, int32_t key_column, uint32_t rows,
                                    const int32_t *keys, const float *weights,
                                    const char *const *names)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_DATAARRAY, "Table");
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)fixture_state(f, id);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    if (state == NULL) {
        return 0;
    }
    static const CK_ARRAYTYPE types[] = {CKARRAYTYPE_INT, CKARRAYTYPE_FLOAT, CKARRAYTYPE_STRING,
                                         CKARRAYTYPE_OBJECT};
    static const char *const column_names[] = {"Key", "Weight", "Name", "Object"};
    state->column_count = 4;
    state->column_formats = (nmo_dataarray_column_format_t *)nmo_arena_alloc(
        arena, 4 * sizeof(nmo_dataarray_column_format_t), _Alignof(nmo_dataarray_column_format_t));
    state->rows = (nmo_dataarray_row_t *)nmo_arena_alloc(
        arena, (rows > 0 ? rows : 1) * sizeof(nmo_dataarray_row_t), _Alignof(nmo_dataarray_row_t));
    if (state->column_formats == NULL || state->rows == NULL) {
        return 0;
    }
    memset(state->column_formats, 0, 4 * sizeof(nmo_dataarray_column_format_t));
    for (uint32_t c = 0; c < 4; c++) {
        state->column_formats[c].name = column_names[c];
        state->column_formats[c].type = types[c];
    }
    state->row_count = rows;
    for (uint32_t r = 0; r < rows; r++) {
        nmo_dataarray_cell_t *cells = (nmo_dataarray_cell_t *)nmo_arena_alloc(
            arena, 4 * sizeof(nmo_dataarray_cell_t), _Alignof(nmo_dataarray_cell_t));
        if (cells == NULL) {
            return 0;
        }
        memset(cells, 0, 4 * sizeof(nmo_dataarray_cell_t));
        cells[0].int_value = keys[r];
        cells[1].float_value = weights[r];
        cells[2].string_value = names[r];
        cells[3].object_ref = nmo_ref_from_id(f->owner_id);
        state->rows[r].column_count = 4;
        state->rows[r].cells = cells;
    }
    state->key_column = key_column;
    return id;
}

/* A building block on `array`: its target holds the array. */
static nmo_object_id_t array_block(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                   nmo_object_id_t array)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    nmo_object_id_t target = fixture_target(f, block);
    if (block != 0 && target != 0) {
        fixture_input_int(f, target, (int32_t)array);
    }
    return block;
}

static nmo_object_id_t add_param(engine_fixture_t *f, nmo_object_id_t block,
                                 nmo_script_edit_parameter_kind_t kind, nmo_guid_t type,
                                 const char *name)
{
    nmo_object_id_t id = 0;
    return nmo_script_edit_add_parameter(f->tx, block, kind, type, name, &id) == NMO_OK ? id : 0;
}

static const int32_t keys[] = {10, 20, 30};
static const float weights[] = {0.5f, 1.5f, 2.5f};
static const char *const names[] = {"ten", "twenty", NULL};

TEST(engine_arrays, get_cell_reads_a_cell_or_reports_not_found)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, -1, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t found = array_block(&f, GUID_GET_CELL, "Get", array);
    nmo_object_id_t missing = array_block(&f, GUID_GET_CELL, "Get Missing", array);
    nmo_object_id_t not_found = fixture_block(&f, GUID_NOP, "Not Found");
    ASSERT_TRUE(found != 0 && missing != 0);
    ASSERT_TRUE(fixture_target(&f, found) != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, found, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, found, 0), BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, not_found, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, found, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, found, 1), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, missing, 0), 3);
    fixture_input_int(&f, BLOCK_PIN(&f, missing, 1), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(3, nmo_engine_array_row_count(f.engine, array));
    ASSERT_EQ(4, nmo_engine_array_column_count(f.engine, array));
    ASSERT_EQ(-1, nmo_engine_array_row_count(f.engine, f.owner_id));
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 1, 2, &cell));
    ASSERT_STR_EQ("twenty", cell.text);
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 2, 3, &cell));
    ASSERT_EQ((uint32_t)f.owner_id, cell.value);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    int32_t value = 0;
    read_value(&f, BLOCK_POUT(&f, found, 0), &value, sizeof(value));
    ASSERT_EQ(20, value);
    ASSERT_EQ(1u, runs(&f, missing));
    ASSERT_EQ(1u, runs(&f, not_found));
    fixture_end(&f);
}

TEST(engine_arrays, set_cell_and_add_row_change_the_engine_copy)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, 0, 2, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t set = array_block(&f, GUID_SET_CELL, "Set", array);
    nmo_object_id_t add = array_block(&f, GUID_ADD_ROW, "Add", array);
    nmo_object_id_t again = array_block(&f, GUID_ADD_ROW, "Add Again", array);
    nmo_object_id_t present = fixture_block(&f, GUID_NOP, "Already Present");
    ASSERT_TRUE(set != 0 && add != 0 && again != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, set, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, set, 0), BLOCK_IN(&f, add, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, add, 0), BLOCK_IN(&f, again, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, again, 1), BLOCK_IN(&f, present, 0), 0);
    /* Set Cell (1, 1) to 9.0: Value is an int parameter, a float column takes its bits */
    fixture_input_int(&f, BLOCK_PIN(&f, set, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, set, 1), 1);
    float nine = 9.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, set, 2), &nine, sizeof(nine));
    /* Add Row: Key 40, Weight 4.5 (Name and Object left out); twice, the key is unique */
    for (int i = 0; i < 2; i++) {
        nmo_object_id_t block = i == 0 ? add : again;
        nmo_object_id_t key = add_param(&f, block, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Key");
        nmo_object_id_t weight =
            add_param(&f, block, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_FLOAT, "Weight");
        ASSERT_TRUE(key != 0 && weight != 0);
        fixture_input_int(&f, key, 40);
        float w = 4.5f;
        fixture_input_value(&f, weight, &w, sizeof(w));
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    nmo_engine_cell_t cell;
    float weight = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 1, 1, &cell));
    memcpy(&weight, &cell.value, sizeof(weight));
    ASSERT_TRUE(weight == 9.0f);
    ASSERT_EQ(3, nmo_engine_array_row_count(f.engine, array));
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 2, 0, &cell));
    ASSERT_EQ(40u, cell.value);
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 2, 1, &cell));
    memcpy(&weight, &cell.value, sizeof(weight));
    ASSERT_TRUE(weight == 4.5f);
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 2, 2, &cell));
    ASSERT_NULL(cell.text);
    ASSERT_EQ(1u, runs(&f, present));
    nmo_engine_cell_t key = {.type = CKARRAYTYPE_INT, .value = 40};
    ASSERT_EQ(2, nmo_engine_array_find_row(f.engine, array, 0, &key, 0));
    fixture_end(&f);
}

TEST(engine_arrays, iterator_and_get_row_walk_the_rows)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, -1, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t it = array_block(&f, GUID_ITERATOR, "Iterator", array);
    nmo_object_id_t body = fixture_block(&f, GUID_NOP, "Body");
    nmo_object_id_t get_row = array_block(&f, GUID_GET_ROW, "Get Row", array);
    ASSERT_TRUE(it != 0 && get_row != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, it, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, it, 1), BLOCK_IN(&f, body, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, body, 0), BLOCK_IN(&f, it, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, it, 0), BLOCK_IN(&f, get_row, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, it, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, it, 1), -1);
    fixture_input_int(&f, BLOCK_PIN(&f, it, 2), 0);
    nmo_object_id_t it_key = add_param(&f, it, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_INT, "Key");
    fixture_input_int(&f, BLOCK_PIN(&f, get_row, 0), 0);
    nmo_object_id_t row_key = add_param(&f, get_row, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_INT, "Key");
    nmo_object_id_t row_weight =
        add_param(&f, get_row, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_FLOAT, "Weight");
    nmo_object_id_t row_name =
        add_param(&f, get_row, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_STRING, "Name");
    ASSERT_TRUE(it_key != 0 && row_key != 0 && row_weight != 0 && row_name != 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* Three rows within one frame, then Out */
    ASSERT_EQ(3u, runs(&f, body));
    ASSERT_EQ(4u, runs(&f, it));
    int32_t value = 0;
    read_value(&f, it_key, &value, sizeof(value));
    ASSERT_EQ(30, value);
    ASSERT_EQ(1u, runs(&f, get_row));
    read_value(&f, row_key, &value, sizeof(value));
    ASSERT_EQ(10, value);
    float weight = 0.0f;
    read_value(&f, row_weight, &weight, sizeof(weight));
    ASSERT_TRUE(weight == 0.5f);
    const void *text = NULL;
    size_t size = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, row_name, &text, &size, NULL));
    ASSERT_STR_EQ("ten", (const char *)text);
    fixture_end(&f);
}

TEST(engine_arrays, find_row_compares_as_the_operator_says)
{
    /* Iterator If searches this way */
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, -1, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);

    nmo_engine_cell_t fifteen = {.type = CKARRAYTYPE_INT, .value = 15};
    ASSERT_EQ(1, nmo_engine_array_find_row_op(f.engine, array, 0, 5 /* CKGREATER */, &fifteen, 0));
    ASSERT_EQ(2, nmo_engine_array_find_row_op(f.engine, array, 0, 5, &fifteen, 2));
    ASSERT_EQ(-1, nmo_engine_array_find_row_op(f.engine, array, 0, 1 /* CKEQUAL */, &fifteen, 0));
    float one = 1.0f;
    nmo_engine_cell_t weight = {.type = CKARRAYTYPE_FLOAT};
    memcpy(&weight.value, &one, sizeof(one));
    ASSERT_EQ(0, nmo_engine_array_find_row_op(f.engine, array, 1, 3 /* CKLESSER */, &weight, 0));
    /* A missing string compares as "" */
    nmo_engine_cell_t empty = {.type = CKARRAYTYPE_STRING, .text = ""};
    ASSERT_EQ(2, nmo_engine_array_find_row(f.engine, array, 2, &empty, 0));
    nmo_engine_cell_t twenty = {.type = CKARRAYTYPE_STRING, .text = "twenty"};
    ASSERT_EQ(0, nmo_engine_array_find_row_op(f.engine, array, 2, 3 /* CKLESSER */, &twenty, 0));
    fixture_end(&f);
}

TEST(engine_arrays, iterator_if_walks_the_rows_that_pass)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, -1, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t it = array_block(&f, GUID_ITERATOR_IF, "Iterator If", array);
    nmo_object_id_t body = fixture_block(&f, GUID_NOP, "Body");
    ASSERT_TRUE(it != 0 && body != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, it, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, it, 1), BLOCK_IN(&f, body, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, body, 0), BLOCK_IN(&f, it, 1), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, it, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, it, 1), 5 /* CKGREATER */);
    nmo_object_id_t reference =
        add_param(&f, it, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Reference Value");
    ASSERT_TRUE(reference != 0);
    fixture_input_int(&f, reference, 15);
    nmo_object_id_t key = add_param(&f, it, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_INT, "Key");
    ASSERT_TRUE(key != 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* The keys above 15, 20 and 30, within one frame */
    ASSERT_EQ(2u, runs(&f, body));
    int32_t value = 0;
    read_value(&f, BLOCK_POUT(&f, it, 0), &value, sizeof(value));
    ASSERT_EQ(2, value);
    read_value(&f, key, &value, sizeof(value));
    ASSERT_EQ(30, value);
    fixture_end(&f);
}

TEST(engine_arrays, remove_key_row_and_clear_array_remove_rows)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, 0, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t remove = array_block(&f, GUID_REMOVE_KEY_ROW, "Remove", array);
    nmo_object_id_t missing = array_block(&f, GUID_REMOVE_KEY_ROW, "Remove Missing", array);
    nmo_object_id_t not_found = fixture_block(&f, GUID_NOP, "Not Found");
    ASSERT_TRUE(remove != 0 && missing != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, remove, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, remove, 0), BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, not_found, 0), 0);
    nmo_object_id_t key = add_param(&f, remove, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Key");
    nmo_object_id_t other = add_param(&f, missing, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Key");
    ASSERT_TRUE(key != 0 && other != 0);
    fixture_input_int(&f, key, 20);
    fixture_input_int(&f, other, 99);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(2, nmo_engine_array_row_count(f.engine, array));
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 1, 0, &cell));
    ASSERT_EQ(30u, cell.value);
    ASSERT_EQ(1u, runs(&f, not_found));
    ASSERT_EQ(NMO_OK, nmo_engine_array_clear(f.engine, array));
    ASSERT_EQ(0, nmo_engine_array_row_count(f.engine, array));
    fixture_end(&f);
}

TEST(engine_arrays, clear_array_block_empties_the_array)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, -1, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t clear = array_block(&f, GUID_CLEAR_ARRAY, "Clear", array);
    ASSERT_TRUE(clear != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, clear, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0, nmo_engine_array_row_count(f.engine, array));
    int32_t row = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_row(f.engine, array, -1, &row));
    ASSERT_EQ(0, row);
    fixture_end(&f);
}

TEST(engine_arrays, get_highest_finds_the_highest_row)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    static const int32_t unordered[] = {20, 30, 10};
    nmo_object_id_t array = create_array(&f, -1, 3, unordered, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t highest = array_block(&f, GUID_GET_HIGHEST, "Highest", array);
    ASSERT_TRUE(highest != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, highest, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, highest, 0), 0);
    nmo_object_id_t key = add_param(&f, highest, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_INT, "Key");
    ASSERT_TRUE(key != 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(1, nmo_engine_array_highest_row(f.engine, array, 0));
    ASSERT_EQ(2, nmo_engine_array_highest_row(f.engine, array, 1));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    int32_t value = 0;
    read_value(&f, BLOCK_POUT(&f, highest, 0), &value, sizeof(value));
    ASSERT_EQ(1, value);
    read_value(&f, key, &value, sizeof(value));
    ASSERT_EQ(30, value);
    fixture_end(&f);
}

TEST(engine_arrays, is_in_group_tests_membership)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = fixture_create(&f, NMO_CID_3DENTITY, "A");
    nmo_object_id_t b = fixture_create(&f, NMO_CID_3DENTITY, "B");
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Group");
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_NOT_NULL(state);
    nmo_ref_t ref = nmo_ref_from_id(a);
    ASSERT_EQ(NMO_OK, nmo_array_append(&state->object_ids, &ref));
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t test_a = fixture_block(&f, GUID_IS_IN_GROUP, "A In Group");
    nmo_object_id_t test_b = fixture_block(&f, GUID_IS_IN_GROUP, "B In Group");
    nmo_object_id_t a_in = fixture_block(&f, GUID_NOP, "A In");
    nmo_object_id_t b_out = fixture_block(&f, GUID_NOP, "B Out");
    ASSERT_TRUE(test_a != 0 && test_b != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, test_a, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, test_b, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, test_a, 0), BLOCK_IN(&f, a_in, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, test_b, 1), BLOCK_IN(&f, b_out, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, test_a, 0), (int32_t)a);
    fixture_input_int(&f, BLOCK_PIN(&f, test_a, 1), (int32_t)group);
    fixture_input_int(&f, BLOCK_PIN(&f, test_b, 0), (int32_t)b);
    fixture_input_int(&f, BLOCK_PIN(&f, test_b, 1), (int32_t)group);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, a_in));
    ASSERT_EQ(1u, runs(&f, b_out));
    fixture_end(&f);
}

TEST(engine_arrays, group_iterator_walks_the_group)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = fixture_create(&f, NMO_CID_3DENTITY, "A");
    nmo_object_id_t b = fixture_create(&f, NMO_CID_3DENTITY, "B");
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Group");
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_NOT_NULL(state);
    nmo_ref_t refs[] = {nmo_ref_from_id(a), nmo_ref_from_id(b)};
    ASSERT_EQ(NMO_OK, nmo_array_append(&state->object_ids, &refs[0]));
    ASSERT_EQ(NMO_OK, nmo_array_append(&state->object_ids, &refs[1]));
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t it = fixture_block(&f, GUID_GROUP_ITERATOR, "Group Iterator");
    nmo_object_id_t body = fixture_block(&f, GUID_NOP, "Body");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, it, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, it, 1), BLOCK_IN(&f, body, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, body, 0), BLOCK_IN(&f, it, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, it, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, it, 0), (int32_t)group);
    fixture_commit(&f, NULL);

    ASSERT_EQ(2, nmo_engine_group_count(f.engine, group));
    ASSERT_EQ(b, nmo_engine_group_object(f.engine, group, 1));
    ASSERT_EQ(-1, nmo_engine_group_count(f.engine, a));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(2u, runs(&f, body));
    ASSERT_EQ(1u, runs(&f, done));
    uint32_t element = 0;
    read_value(&f, BLOCK_POUT(&f, it, 0), &element, sizeof(element));
    ASSERT_EQ((uint32_t)b, element);
    int32_t index = 0;
    read_value(&f, BLOCK_POUT(&f, it, 1), &index, sizeof(index));
    ASSERT_EQ(1, index);
    fixture_end(&f);
}

/*
 * Ballance's "init Resetpoints": each member deleted as the iterator walks
 * the group. Objects destroyed while behaviors run go at the end of the
 * frame (m_DeferDestroyObjects), so the group keeps its members until then
 * and every one is reached.
 */
TEST(engine_arrays, group_iterator_deletes_every_member_in_a_frame)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t members[3] = {fixture_create(&f, NMO_CID_3DENTITY, "A"),
                                  fixture_create(&f, NMO_CID_3DENTITY, "B"),
                                  fixture_create(&f, NMO_CID_3DENTITY, "C")};
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Group");
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_NOT_NULL(state);
    for (int i = 0; i < 3; i++) {
        nmo_ref_t ref = nmo_ref_from_id(members[i]);
        ASSERT_EQ(NMO_OK, nmo_array_append(&state->object_ids, &ref));
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t it = fixture_block(&f, GUID_GROUP_ITERATOR, "Group Iterator");
    nmo_object_id_t deleter = fixture_block(&f, GUID_OBJECT_DELETE, "Delete");
    ASSERT_TRUE(it != 0 && deleter != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, it, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, it, 1), BLOCK_IN(&f, deleter, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, deleter, 0), BLOCK_IN(&f, it, 1), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, it, 0), (int32_t)group);
    /* The Element output (a CKBeObject) into the Object input, typed alike to link them */
    nmo_parameterin_state_t *object_in =
        (nmo_parameterin_state_t *)fixture_state(&f, BLOCK_PIN(&f, deleter, 0));
    ASSERT_NOT_NULL(object_in);
    object_in->type_guid = CKPGUID_BEOBJECT;
    ASSERT_EQ(NMO_OK, nmo_script_edit_connect_parameter(f.tx, BLOCK_POUT(&f, it, 0),
                                                        BLOCK_PIN(&f, deleter, 0)));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(3u, runs(&f, deleter));
    ASSERT_EQ(0, nmo_engine_group_count(f.engine, group));
    float m[16];
    for (int i = 0; i < 3; i++) {
        ASSERT_FALSE(nmo_engine_entity_world_matrix(f.engine, members[i], m));
    }
    fixture_end(&f);
}

TEST(engine_arrays, get_key_row_and_row_search_find_rows)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, 0, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t get = array_block(&f, GUID_GET_KEY_ROW, "Get Key Row", array);
    nmo_object_id_t search = array_block(&f, GUID_ROW_SEARCH, "Row Search", array);
    ASSERT_TRUE(get != 0 && search != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, get, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, search, 0), 0);
    nmo_object_id_t key = add_param(&f, get, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Key");
    nmo_object_id_t key_out = add_param(&f, get, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_INT, "Key");
    nmo_object_id_t weight = add_param(&f, get, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_FLOAT, "Weight");
    nmo_object_id_t reference =
        add_param(&f, search, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_FLOAT, "Reference");
    ASSERT_TRUE(key != 0 && key_out != 0 && weight != 0 && reference != 0);
    fixture_input_int(&f, key, 20);
    fixture_input_int(&f, BLOCK_PIN(&f, search, 0), 1); /* Weight */
    fixture_input_int(&f, BLOCK_PIN(&f, search, 2), 5); /* Greater */
    const float one = 1.0f;
    fixture_input_value(&f, reference, &one, sizeof(one));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    int32_t found_key = 0, found_row = -1;
    float found_weight = 0.0f;
    read_value(&f, key_out, &found_key, sizeof(found_key));
    read_value(&f, weight, &found_weight, sizeof(found_weight));
    read_value(&f, BLOCK_POUT(&f, search, 0), &found_row, sizeof(found_row));
    ASSERT_EQ(20, found_key);
    ASSERT_FLOAT_EQ(1.5f, found_weight, 0.0001f);
    ASSERT_EQ(1, found_row);
    fixture_end(&f);
}

TEST(engine_arrays, column_sum_and_remove_row_if)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, 0, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t sum_keys = array_block(&f, GUID_COLUMN_SUM, "Sum Keys", array);
    nmo_object_id_t sum_weights = array_block(&f, GUID_COLUMN_SUM, "Sum Weights", array);
    nmo_object_id_t remove = array_block(&f, GUID_REMOVE_ROW_IF, "Remove If", array);
    ASSERT_TRUE(sum_keys != 0 && sum_weights != 0 && remove != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sum_keys, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sum_keys, 0), BLOCK_IN(&f, sum_weights, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sum_weights, 0), BLOCK_IN(&f, remove, 0), 0);
    nmo_object_id_t key_total = add_param(&f, sum_keys, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_INT, "Sum");
    nmo_object_id_t weight_total =
        add_param(&f, sum_weights, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_FLOAT, "Sum");
    nmo_object_id_t limit = add_param(&f, remove, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Limit");
    ASSERT_TRUE(key_total != 0 && weight_total != 0 && limit != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, sum_weights, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 1), 3); /* Lesser */
    fixture_input_int(&f, limit, 25);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    int32_t keys_sum = 0, removed = 0;
    float weights_sum = 0.0f;
    read_value(&f, key_total, &keys_sum, sizeof(keys_sum));
    read_value(&f, weight_total, &weights_sum, sizeof(weights_sum));
    read_value(&f, BLOCK_POUT(&f, remove, 0), &removed, sizeof(removed));
    ASSERT_EQ(60, keys_sum);
    ASSERT_FLOAT_EQ(4.5f, weights_sum, 0.0001f);
    /* 10 and 20 are under 25 */
    ASSERT_EQ(2, removed);
    ASSERT_EQ(1, nmo_engine_array_row_count(f.engine, array));
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 0, 0, &cell));
    ASSERT_EQ(30u, cell.value);
    fixture_end(&f);
}

TEST(engine_arrays, shuffle_insert_and_remove_columns)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, 0, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t shuffle = array_block(&f, GUID_SHUFFLE_ORDER, "Shuffle", array);
    nmo_object_id_t insert = array_block(&f, GUID_INSERT_COLUMN, "Insert", array);
    nmo_object_id_t remove = array_block(&f, GUID_REMOVE_COLUMN, "Remove", array);
    ASSERT_TRUE(shuffle != 0 && insert != 0 && remove != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, shuffle, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, shuffle, 0), BLOCK_IN(&f, insert, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, insert, 0), BLOCK_IN(&f, remove, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, insert, 0), 1); /* an Integer column before Weight */
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 0), 0); /* then Key goes */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* New Column, Weight, Name, Object */
    ASSERT_EQ(3, nmo_engine_array_row_count(f.engine, array));
    ASSERT_EQ(4, nmo_engine_array_column_count(f.engine, array));
    ASSERT_EQ(CKARRAYTYPE_INT, nmo_engine_array_column_type(f.engine, array, 0));
    ASSERT_EQ(CKARRAYTYPE_FLOAT, nmo_engine_array_column_type(f.engine, array, 1));
    /* Removing the key column leaves the key index where it was */
    ASSERT_EQ(0, nmo_engine_array_key_column(f.engine, array));
    /* The rows were shuffled whole: each weight keeps its name, each new cell is 0 */
    float total = 0.0f;
    for (int32_t r = 0; r < 3; r++) {
        nmo_engine_cell_t extra, weight, name;
        ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, r, 0, &extra));
        ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, r, 1, &weight));
        ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, r, 2, &name));
        ASSERT_EQ(0u, extra.value);
        float w = 0.0f;
        memcpy(&w, &weight.value, sizeof(w));
        total += w;
        const char *expected = w == 0.5f ? "ten" : w == 1.5f ? "twenty" : NULL;
        if (expected == NULL) {
            ASSERT_NULL(name.text);
        } else {
            ASSERT_STR_EQ(expected, name.text);
        }
    }
    ASSERT_FLOAT_EQ(4.5f, total, 0.0001f);
    fixture_end(&f);
}

TEST(engine_arrays, count_test_change_sort_and_remove_rows)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, 0, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t count = array_block(&f, GUID_VALUE_COUNT, "Count", array);
    nmo_object_id_t test = array_block(&f, GUID_TEST_CELL, "Test", array);
    nmo_object_id_t change = array_block(&f, GUID_CHANGE_VALUE_IF, "Change", array);
    nmo_object_id_t sort = array_block(&f, GUID_SORT_ROWS, "Sort", array);
    nmo_object_id_t remove = array_block(&f, GUID_REMOVE_ROW, "Remove", array);
    nmo_object_id_t missing = array_block(&f, GUID_REMOVE_ROW, "Missing", array);
    nmo_object_id_t passed = fixture_block(&f, GUID_NOP, "Passed");
    nmo_object_id_t absent = fixture_block(&f, GUID_NOP, "Absent");
    ASSERT_TRUE(count != 0 && test != 0 && change != 0 && sort != 0 && remove != 0 &&
                missing != 0 && passed != 0 && absent != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, count, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, count, 0), BLOCK_IN(&f, test, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, test, 0), BLOCK_IN(&f, passed, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, passed, 0), BLOCK_IN(&f, change, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, change, 0), BLOCK_IN(&f, sort, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sort, 0), BLOCK_IN(&f, remove, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, remove, 0), BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, absent, 0), 0);
    /* The reference values the callbacks add, typed by the key column */
    nmo_object_id_t count_ref = add_param(&f, count, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Ref");
    nmo_object_id_t test_ref = add_param(&f, test, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Ref");
    nmo_object_id_t change_ref = add_param(&f, change, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Ref");
    nmo_object_id_t change_to = add_param(&f, change, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "New");
    ASSERT_TRUE(count_ref != 0 && test_ref != 0 && change_ref != 0 && change_to != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, count, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, count, 1), 5); /* Greater */
    fixture_input_int(&f, count_ref, 15);
    fixture_input_int(&f, BLOCK_PIN(&f, test, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, test, 1), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, test, 2), 1); /* Equal */
    fixture_input_int(&f, test_ref, 20);
    fixture_input_int(&f, BLOCK_PIN(&f, change, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, change, 1), 1);
    fixture_input_int(&f, change_ref, 20);
    fixture_input_int(&f, change_to, 25);
    fixture_input_int(&f, BLOCK_PIN(&f, sort, 0), 1); /* by Weight */
    fixture_input_int(&f, BLOCK_PIN(&f, sort, 1), 0); /* descending */
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, missing, 0), 5);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    int32_t greater = 0, changed = 0;
    read_value(&f, BLOCK_POUT(&f, count, 0), &greater, sizeof(greater));
    read_value(&f, BLOCK_POUT(&f, change, 0), &changed, sizeof(changed));
    ASSERT_EQ(2, greater);  /* 20 and 30 */
    ASSERT_EQ(1u, runs(&f, passed));
    ASSERT_EQ(1, changed);  /* 20 became 25 */
    ASSERT_EQ(1u, runs(&f, absent));
    /* Sorted by weight, heaviest first (30, 25, 10), then the first row removed */
    ASSERT_EQ(2, nmo_engine_array_row_count(f.engine, array));
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 0, 0, &cell));
    ASSERT_EQ(25u, cell.value);
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 1, 0, &cell));
    ASSERT_EQ(10u, cell.value);
    fixture_end(&f);
}

TEST(engine_arrays, sort_keeps_equal_rows_and_trusts_an_ascending_sort)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    static const int32_t same[] = {7, 3, 7};
    nmo_object_id_t array = create_array(&f, 0, 3, same, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);

    nmo_engine_array_sort(f.engine, array, 0, true);
    /* 3, then the two 7s in their order (weights 0.5 then 2.5) */
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 0, 0, &cell));
    ASSERT_EQ(3u, cell.value);
    float weight = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 1, 1, &cell));
    memcpy(&weight, &cell.value, sizeof(weight));
    ASSERT_FLOAT_EQ(0.5f, weight, 0.0001f);
    /* Swapping rows is no change CK tracks: GetHighest still trusts the sort */
    ASSERT_EQ(NMO_OK, nmo_engine_array_swap_rows(f.engine, array, 0, 2));
    ASSERT_EQ(2, nmo_engine_array_highest_row(f.engine, array, 0));
    /* Setting a cell of the sort column ends the sort: sorting again moves the 1 first */
    nmo_engine_cell_t one = {.type = CKARRAYTYPE_INT, .value = 1u};
    ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, array, 1, 0, &one));
    nmo_engine_array_sort(f.engine, array, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, array, 0, 0, &cell));
    ASSERT_EQ(1u, cell.value);
    fixture_end(&f);
}

TEST(engine_arrays, collection_iterator_walks_the_objects)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t iterate = fixture_block(&f, GUID_COLLECTION_ITERATOR, "Iterate");
    nmo_object_id_t each = fixture_block(&f, GUID_NOP, "Each");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(iterate != 0 && each != 0 && done != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, iterate, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, iterate, 1), BLOCK_IN(&f, each, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, each, 0), BLOCK_IN(&f, iterate, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, iterate, 0), BLOCK_IN(&f, done, 0), 0);
    /* A collection holds the objects' IDs */
    const uint32_t ids[2] = {(uint32_t)f.owner_id, (uint32_t)other};
    fixture_input_value(&f, BLOCK_PIN(&f, iterate, 0), ids, sizeof(ids));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(2u, runs(&f, each));
    ASSERT_EQ(1u, runs(&f, done));
    uint32_t element = 0;
    int32_t index = -1;
    read_value(&f, BLOCK_POUT(&f, iterate, 0), &element, sizeof(element));
    read_value(&f, BLOCK_POUT(&f, iterate, 1), &index, sizeof(index));
    ASSERT_EQ(other, element);
    ASSERT_EQ(1, index);
    fixture_end(&f);
}

static int32_t cell_int(engine_fixture_t *f, nmo_object_id_t array, int32_t row, int32_t column)
{
    nmo_engine_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    nmo_engine_array_get_cell(f->engine, array, row, column, &cell);
    return (int32_t)cell.value;
}

static float cell_float(engine_fixture_t *f, nmo_object_id_t array, int32_t row, int32_t column)
{
    nmo_engine_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    nmo_engine_array_get_cell(f->engine, array, row, column, &cell);
    float value;
    memcpy(&value, &cell.value, sizeof(value));
    return value;
}

TEST(engine_arrays, lowest_nearest_product_and_column_arithmetic)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, -1, 3, keys, weights, names);
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t lowest = array_block(&f, GUID_GET_LOWEST, "Lowest", array);
    nmo_object_id_t nearest = array_block(&f, GUID_GET_NEAREST, "Nearest", array);
    nmo_object_id_t product = array_block(&f, GUID_COLUMN_PRODUCT, "Product", array);
    nmo_object_id_t operation = array_block(&f, GUID_COLUMN_OPERATION, "Operation", array);
    nmo_object_id_t transform = array_block(&f, GUID_COLUMN_TRANSFORM, "Transform", array);
    ASSERT_TRUE(lowest != 0 && nearest != 0 && product != 0 && operation != 0 && transform != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, lowest, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, lowest, 0), BLOCK_IN(&f, nearest, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, nearest, 0), BLOCK_IN(&f, product, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, product, 0), BLOCK_IN(&f, operation, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, operation, 0), BLOCK_IN(&f, transform, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, lowest, 0), 1);       /* the lowest weight */
    fixture_input_int(&f, BLOCK_PIN(&f, nearest, 0), 0);      /* the key nearest 22 */
    nmo_object_id_t near_value = add_param(&f, nearest, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Value");
    fixture_input_int(&f, near_value, 22);
    fixture_input_int(&f, BLOCK_PIN(&f, product, 0), 0);      /* 10 * 20 * 30 */
    nmo_object_id_t product_out = add_param(&f, product, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_INT, "Product");
    ASSERT_TRUE(near_value != 0 && product_out != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, operation, 0), 0);    /* Key = Key + Key */
    fixture_input_int(&f, BLOCK_PIN(&f, operation, 1), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, operation, 2), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, operation, 3), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, transform, 0), 1);    /* Weight *= 2 */
    fixture_input_int(&f, BLOCK_PIN(&f, transform, 1), 3);
    nmo_object_id_t factor = add_param(&f, transform, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_FLOAT, "Value");
    ASSERT_TRUE(factor != 0);
    const float two = 2.0f;
    fixture_input_value(&f, factor, &two, sizeof(two));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));

    int32_t row = -1, value = 0;
    read_value(&f, BLOCK_POUT(&f, lowest, 0), &row, sizeof(row));
    ASSERT_EQ(0, row);
    read_value(&f, BLOCK_POUT(&f, nearest, 0), &row, sizeof(row));
    ASSERT_EQ(1, row);
    read_value(&f, product_out, &value, sizeof(value));
    ASSERT_EQ(6000, value);
    for (int32_t r = 0; r < 3; r++) {
        ASSERT_EQ(2 * keys[r], cell_int(&f, array, r, 0));
        ASSERT_FLOAT_EQ(2.0f * weights[r], cell_float(&f, array, r, 1), 0.0f);
    }
    fixture_end(&f);
}

TEST(engine_arrays, reverse_move_swap_unique_and_key_rows)
{
    static const int32_t twin_keys[] = {10, 30, 10};
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, 0, 3, keys, weights, names);
    nmo_object_id_t twins = create_array(&f, 0, 3, twin_keys, weights, names);
    ASSERT_TRUE(array != 0 && twins != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t reverse = array_block(&f, GUID_REVERSE_ORDER, "Reverse", array);
    nmo_object_id_t move = array_block(&f, GUID_MOVE_ROW, "Move", array);
    nmo_object_id_t swap = array_block(&f, GUID_SWAP_ROWS, "Swap", array);
    nmo_object_id_t set_key = array_block(&f, GUID_SET_KEY_ROW, "Set Key Row", array);
    nmo_object_id_t unique = array_block(&f, GUID_MAKE_ROWS_UNIQUE, "Unique", twins);
    ASSERT_TRUE(reverse != 0 && move != 0 && swap != 0 && set_key != 0 && unique != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, reverse, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, reverse, 0), BLOCK_IN(&f, move, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, move, 0), BLOCK_IN(&f, swap, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, swap, 0), BLOCK_IN(&f, set_key, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, unique, 0), 0);
    /* 30 20 10 reversed; row 0 last: 20 10 30; rows 1 and 2 swapped: 20 30 10 */
    fixture_input_int(&f, BLOCK_PIN(&f, move, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, move, 1), -1);
    fixture_input_int(&f, BLOCK_PIN(&f, swap, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, swap, 1), 2);
    /* Set Key Row: the row keyed 30 gets the weight 7 */
    nmo_object_id_t key_in = add_param(&f, set_key, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Key");
    nmo_object_id_t weight_in = add_param(&f, set_key, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_FLOAT, "Weight");
    ASSERT_TRUE(key_in != 0 && weight_in != 0);
    fixture_input_int(&f, key_in, 30);
    const float seven = 7.0f;
    fixture_input_value(&f, weight_in, &seven, sizeof(seven));
    fixture_input_int(&f, BLOCK_PIN(&f, unique, 0), 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));

    ASSERT_EQ(20, cell_int(&f, array, 0, 0));
    ASSERT_EQ(30, cell_int(&f, array, 1, 0));
    ASSERT_EQ(10, cell_int(&f, array, 2, 0));
    ASSERT_FLOAT_EQ(7.0f, cell_float(&f, array, 1, 1), 0.0f);
    ASSERT_FLOAT_EQ(1.5f, cell_float(&f, array, 0, 1), 0.0f);
    /* Unique sorts the keys and keeps one 10 */
    ASSERT_EQ(2, nmo_engine_array_row_count(f.engine, twins));
    ASSERT_EQ(10, cell_int(&f, twins, 0, 0));
    ASSERT_EQ(30, cell_int(&f, twins, 1, 0));
    fixture_end(&f);
}

TEST(engine_arrays, move_column_names_and_create_group)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = create_array(&f, -1, 3, keys, weights, names);
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Found");
    ASSERT_TRUE(array != 0 && group != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t create = array_block(&f, GUID_CREATE_GROUP_FROM_ARRAY, "Create Group", array);
    nmo_object_id_t empty = array_block(&f, GUID_CREATE_GROUP_FROM_ARRAY, "Create Nothing", array);
    nmo_object_id_t move = array_block(&f, GUID_MOVE_COLUMN, "Move Column", array);
    nmo_object_id_t name = array_block(&f, GUID_GET_COLUMN_NAME, "Column Name", array);
    nmo_object_id_t none_found = fixture_block(&f, GUID_NOP, "None Found");
    ASSERT_TRUE(create != 0 && empty != 0 && move != 0 && name != 0 && none_found != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, create, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, create, 0), BLOCK_IN(&f, empty, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, empty, 1), BLOCK_IN(&f, none_found, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, empty, 1), BLOCK_IN(&f, move, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, move, 0), BLOCK_IN(&f, name, 0), 0);
    /* The objects of rows keyed above 15: the owner, once; then above 99: none */
    nmo_object_id_t blocks[2] = {create, empty};
    const int32_t thresholds[2] = {15, 99};
    for (int i = 0; i < 2; i++) {
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 0), 3);
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 1), 0);
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 2), 5); /* CKGREATER */
        nmo_object_id_t threshold = add_param(&f, blocks[i], NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_INT, "Value");
        ASSERT_TRUE(threshold != 0);
        fixture_input_int(&f, threshold, thresholds[i]);
    }
    fixture_input_int(&f, BLOCK_PIN(&f, move, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, move, 1), -1);
    fixture_input_int(&f, BLOCK_PIN(&f, name, 0), 0);
    fixture_commit(&f, NULL);
    const uint32_t id = (uint32_t)group;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, BLOCK_POUT(&f, create, 0), &id, sizeof(id)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, BLOCK_POUT(&f, empty, 0), &id, sizeof(id)));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));

    ASSERT_EQ(1u, runs(&f, none_found));
    ASSERT_EQ(0, nmo_engine_group_count(f.engine, group)); /* the second emptied it */
    /* Key went last: Weight, Name, Object, Key */
    ASSERT_STR_EQ("Weight", nmo_engine_array_column_name(f.engine, array, 0));
    ASSERT_STR_EQ("Key", nmo_engine_array_column_name(f.engine, array, 3));
    ASSERT_EQ(20, cell_int(&f, array, 1, 3));
    char text[16] = {0};
    read_value(&f, BLOCK_POUT(&f, name, 0), text, 7);
    ASSERT_STR_EQ("Weight", text);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_arrays, get_cell_reads_a_cell_or_reports_not_found);
REGISTER_TEST(engine_arrays, set_cell_and_add_row_change_the_engine_copy);
REGISTER_TEST(engine_arrays, iterator_and_get_row_walk_the_rows);
REGISTER_TEST(engine_arrays, find_row_compares_as_the_operator_says);
REGISTER_TEST(engine_arrays, iterator_if_walks_the_rows_that_pass);
REGISTER_TEST(engine_arrays, remove_key_row_and_clear_array_remove_rows);
REGISTER_TEST(engine_arrays, clear_array_block_empties_the_array);
REGISTER_TEST(engine_arrays, get_highest_finds_the_highest_row);
REGISTER_TEST(engine_arrays, is_in_group_tests_membership);
REGISTER_TEST(engine_arrays, group_iterator_walks_the_group);
REGISTER_TEST(engine_arrays, group_iterator_deletes_every_member_in_a_frame);
REGISTER_TEST(engine_arrays, get_key_row_and_row_search_find_rows);
REGISTER_TEST(engine_arrays, column_sum_and_remove_row_if);
REGISTER_TEST(engine_arrays, shuffle_insert_and_remove_columns);
REGISTER_TEST(engine_arrays, count_test_change_sort_and_remove_rows);
REGISTER_TEST(engine_arrays, sort_keeps_equal_rows_and_trusts_an_ascending_sort);
REGISTER_TEST(engine_arrays, collection_iterator_walks_the_objects);
REGISTER_TEST(engine_arrays, lowest_nearest_product_and_column_arithmetic);
REGISTER_TEST(engine_arrays, reverse_move_swap_unique_and_key_rows);
REGISTER_TEST(engine_arrays, move_column_names_and_create_group);
TEST_MAIN_END()
