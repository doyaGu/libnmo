#include "engine_fixture.h"

#include "core/nmo_array.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
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
#define GUID_CLEAR_ARRAY NMO_GUID(0x35c9352f, 0x7b1a193b)
#define GUID_REMOVE_KEY_ROW NMO_GUID(0x0f8334ea, 0x279a40cd)

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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_arrays, get_cell_reads_a_cell_or_reports_not_found);
REGISTER_TEST(engine_arrays, set_cell_and_add_row_change_the_engine_copy);
REGISTER_TEST(engine_arrays, iterator_and_get_row_walk_the_rows);
REGISTER_TEST(engine_arrays, find_row_compares_as_the_operator_says);
REGISTER_TEST(engine_arrays, iterator_if_walks_the_rows_that_pass);
REGISTER_TEST(engine_arrays, remove_key_row_and_clear_array_remove_rows);
REGISTER_TEST(engine_arrays, clear_array_block_empties_the_array);
REGISTER_TEST(engine_arrays, group_iterator_walks_the_group);
TEST_MAIN_END()
