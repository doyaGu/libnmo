#include "engine_fixture.h"

#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <math.h>

#define GUID_CREATE_STRING NMO_GUID(0x4bcd2f9d, 0x382652e2)
#define GUID_GET_SUBSTRING NMO_GUID(0x10b16051, 0x26173b39)
#define GUID_SCAN_STRING NMO_GUID(0x4afa2f11, 0x034872e2)
#define GUID_REVERSE_STRING NMO_GUID(0x72b17c2e, 0x644e2563)
#define GUID_IDENTITY NMO_GUID(0x15151652, 0xaeefffd5)
#define OP_ADDITION NMO_GUID(0x33cc6b49, 0x3589282b)
#define OP_GET_LENGTH NMO_GUID(0x556a69af, 0x076e3f09)
#define OP_GET_OBJECT_BY_NAME NMO_GUID(0x599203f6, 0x23b06096)
#define OP_GET_TYPE NMO_GUID(0x35aa0ddb, 0x7a5e3335)
#define OP_GET_COUNT NMO_GUID(0x44df6f61, 0x5f8f55c8)

static void input_string(engine_fixture_t *f, nmo_object_id_t pin, const char *text)
{
    fixture_input_value(f, pin, text, strlen(text) + 1);
}

/* Add an input parameter of `type` holding `size` bytes to `block`. */
static nmo_object_id_t add_input(engine_fixture_t *f, nmo_object_id_t block, nmo_guid_t type,
                                 const void *value, size_t size)
{
    nmo_object_id_t pin = 0;
    if (nmo_script_edit_add_parameter(f->tx, block, NMO_SCRIPT_EDIT_PARAM_IN, type, "Value",
                                      &pin) != NMO_OK) {
        return 0;
    }
    fixture_input_value(f, pin, value, size);
    return pin;
}

static nmo_object_id_t add_param(engine_fixture_t *f, nmo_object_id_t block,
                                 nmo_script_edit_parameter_kind_t kind, nmo_guid_t type)
{
    nmo_object_id_t id = 0;
    return nmo_script_edit_add_parameter(f->tx, block, kind, type, "Value", &id) == NMO_OK ? id
                                                                                           : 0;
}

static void set_string(engine_fixture_t *f, nmo_object_id_t param, const char *text)
{
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f->engine, param, text, strlen(text) + 1));
}

/* The string parameter `param` holds, or "" */
static const char *string_value(engine_fixture_t *f, nmo_object_id_t param)
{
    const void *data = NULL;
    size_t size = 0;
    if (nmo_engine_param_value(f->engine, param, &data, &size, NULL) != NMO_OK || size == 0 ||
        memchr(data, '\0', size) == NULL) {
        return "";
    }
    return (const char *)data;
}

TEST(engine_strings, create_string_writes_values_as_ck2_does)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t create = fixture_block(&f, GUID_CREATE_STRING, "Create String");
    ASSERT_TRUE(create != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, create, 0), 0);
    input_string(&f, BLOCK_PIN(&f, create, 0), "|");
    const int32_t integer = 42, yes = 1;
    const float real = 1.5f, angle = 20.0f * 0.017453292f, time = 65500.0f, percent = 0.25f;
    const float vector[3] = {1.0f, 2.0f, 3.0f}, color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    ASSERT_TRUE(add_input(&f, create, CKPGUID_INT, &integer, sizeof(integer)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_FLOAT, &real, sizeof(real)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_INT, &integer, sizeof(integer)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_BOOL, &yes, sizeof(yes)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_ANGLE, &angle, sizeof(angle)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_VECTOR, vector, sizeof(vector)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_COLOR, color, sizeof(color)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_TIME, &time, sizeof(time)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_PERCENTAGE, &percent, sizeof(percent)) != 0);
    ASSERT_TRUE(add_input(&f, create, CKPGUID_STRING, "end", 4) != 0);
    /* Formats for the first three: "%03d", "%.2f", and "%s" that does not fit an Integer */
    nmo_object_id_t formats[3];
    for (int i = 0; i < 3; i++) {
        formats[i] = add_param(&f, create, NMO_SCRIPT_EDIT_PARAM_LOCAL, CKPGUID_STRING);
        ASSERT_TRUE(formats[i] != 0);
    }
    fixture_commit(&f, NULL);
    set_string(&f, formats[0], "%03d");
    set_string(&f, formats[1], "%.2f");
    set_string(&f, formats[2], "<%s>");

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_STR_EQ("042|1.50|42|TRUE|0:20|1,2,3|255,0,0,255|01m 05s 500ms|25%|end",
                  string_value(&f, BLOCK_POUT(&f, create, 0)));
    fixture_end(&f);
}

TEST(engine_strings, create_string_adds_lines_and_clears)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t create = fixture_block(&f, GUID_CREATE_STRING, "Create String");
    ASSERT_TRUE(create != 0);
    const int32_t value = 7;
    ASSERT_TRUE(add_input(&f, create, CKPGUID_INT, &value, sizeof(value)) != 0);
    ASSERT_TRUE(add_param(&f, create, NMO_SCRIPT_EDIT_PARAM_LOCAL, CKPGUID_STRING) != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, create, 1), 0); /* Add Line to nothing */
    fixture_link(&f, f.script_in, BLOCK_IN(&f, create, 1), 1);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, create, 2), 2);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, create, 0), 3);
    fixture_commit(&f, NULL);
    nmo_object_id_t text = BLOCK_POUT(&f, create, 0);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const char *expected[4] = {"7", "7\n7", "", "7"};
    for (int frame = 0; frame < 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        ASSERT_STR_EQ(expected[frame], string_value(&f, text));
    }
    fixture_end(&f);
}

TEST(engine_strings, substring_scan_and_reverse)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t tail = fixture_block(&f, GUID_GET_SUBSTRING, "Tail");
    nmo_object_id_t head = fixture_block(&f, GUID_GET_SUBSTRING, "Head");
    nmo_object_id_t scan = fixture_block(&f, GUID_SCAN_STRING, "Scan");
    nmo_object_id_t reverse = fixture_block(&f, GUID_REVERSE_STRING, "Reverse");
    ASSERT_TRUE(tail != 0 && head != 0 && scan != 0 && reverse != 0);
    nmo_object_id_t blocks[4] = {tail, head, scan, reverse};
    for (int i = 0; i < 4; i++) {
        fixture_link(&f, f.script_in, BLOCK_IN(&f, blocks[i], 0), 0);
    }
    input_string(&f, BLOCK_PIN(&f, tail, 0), "Hello World");
    fixture_input_int(&f, BLOCK_PIN(&f, tail, 1), 6);
    fixture_input_int(&f, BLOCK_PIN(&f, tail, 2), 0); /* to the end */
    input_string(&f, BLOCK_PIN(&f, head, 0), "Hello World");
    fixture_input_int(&f, BLOCK_PIN(&f, head, 1), -2); /* before the start: cut */
    fixture_input_int(&f, BLOCK_PIN(&f, head, 2), 4);
    input_string(&f, BLOCK_PIN(&f, scan, 0), "__Hi_3.5_1,0,1");
    input_string(&f, BLOCK_PIN(&f, scan, 1), "_");
    nmo_object_id_t word = add_param(&f, scan, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_STRING);
    nmo_object_id_t number = add_param(&f, scan, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_FLOAT);
    nmo_object_id_t vector = add_param(&f, scan, NMO_SCRIPT_EDIT_PARAM_OUT, CKPGUID_VECTOR);
    ASSERT_TRUE(word != 0 && number != 0 && vector != 0);
    nmo_object_id_t reversed = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                          f.tx, BLOCK_PIN(&f, reverse, 0), &reversed));
    input_string(&f, BLOCK_PIN(&f, reverse, 0), "abcd");
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_STR_EQ("World", string_value(&f, BLOCK_POUT(&f, tail, 0)));
    ASSERT_STR_EQ("He", string_value(&f, BLOCK_POUT(&f, head, 0)));
    int32_t found = 0;
    float scanned = 0.0f, scanned_vector[3] = {0.0f, 0.0f, 0.0f};
    read_value(&f, BLOCK_POUT(&f, scan, 0), &found, sizeof(found));
    read_value(&f, number, &scanned, sizeof(scanned));
    read_value(&f, vector, scanned_vector, sizeof(scanned_vector));
    ASSERT_EQ(3, found);
    ASSERT_STR_EQ("Hi", string_value(&f, word));
    ASSERT_FLOAT_EQ(3.5f, scanned, 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, scanned_vector[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, scanned_vector[1], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, scanned_vector[2], 0.0001f);
    /* Reverse String turns the string it reads around where it is */
    ASSERT_STR_EQ("dcba", string_value(&f, reversed));
    fixture_end(&f);
}

/* A local string parameter of the script holding `text` */
static nmo_object_id_t local_string(engine_fixture_t *f, const char *name, const char *text)
{
    nmo_object_id_t id = 0;
    if (nmo_script_edit_add_parameter(f->tx, f->script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                      CKPGUID_STRING, name, &id) != NMO_OK ||
        nmo_script_edit_set_parameter_bytes(f->tx, id, (const uint8_t *)text, strlen(text) + 1u) !=
            NMO_OK) {
        return 0;
    }
    return id;
}

/* An operation on `in1` and `in2`, its output of `type`; the output's ID */
static nmo_object_id_t operation(engine_fixture_t *f, nmo_guid_t guid, nmo_object_id_t in1,
                                 nmo_object_id_t in2, nmo_guid_t type)
{
    nmo_object_id_t op = 0;
    if (nmo_script_edit_add_operation(f->tx, f->script_id, guid, in1, in2, 0, &op) != NMO_OK) {
        return 0;
    }
    const nmo_parameteroperation_state_t *state =
        (const nmo_parameteroperation_state_t *)fixture_state(f, op);
    nmo_object_id_t out = state != NULL ? nmo_parameteroperation_out_id(state) : 0;
    nmo_parameter_state_t *out_state = (nmo_parameter_state_t *)fixture_state(f, out);
    nmo_parameterin_state_t *in1_state =
        (nmo_parameterin_state_t *)fixture_state(f, nmo_parameteroperation_in1_id(state));
    const nmo_parameter_state_t *source = (const nmo_parameter_state_t *)fixture_state(f, in1);
    if (out_state == NULL || in1_state == NULL || source == NULL) {
        return 0;
    }
    /* The operation takes its operands' types, as the operation that matches them does */
    in1_state->type_guid = source->type_guid;
    out_state->type_guid = type;
    return out;
}

/* An Identity reading `source` as `type` */
static nmo_object_id_t identity_of(engine_fixture_t *f, nmo_object_id_t source, nmo_guid_t type,
                                   nmo_object_id_t after)
{
    nmo_object_id_t identity = fixture_block(f, GUID_IDENTITY, "Identity");
    nmo_parameterin_state_t *pin = (nmo_parameterin_state_t *)fixture_state(f, BLOCK_PIN(f, identity, 0));
    nmo_parameter_state_t *pout = (nmo_parameter_state_t *)fixture_state(f, BLOCK_POUT(f, identity, 0));
    if (pin == NULL || pout == NULL) {
        return 0;
    }
    pin->type_guid = type;
    pout->type_guid = type;
    if (nmo_script_edit_connect_parameter(f->tx, source, BLOCK_PIN(f, identity, 0)) != NMO_OK) {
        return 0;
    }
    fixture_link(f, after, BLOCK_IN(f, identity, 0), 0);
    return identity;
}

TEST(engine_strings, string_operations)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t ab = local_string(&f, "A", "Ab");
    nmo_object_id_t cd = local_string(&f, "B", "cd");
    nmo_object_id_t owner_name = local_string(&f, "Name", "Owner");
    ASSERT_TRUE(ab != 0 && cd != 0 && owner_name != 0);
    nmo_object_id_t joined = operation(&f, OP_ADDITION, ab, cd, CKPGUID_STRING);
    ASSERT_TRUE(joined != 0);
    nmo_object_id_t length = operation(&f, OP_GET_LENGTH, joined, 0, CKPGUID_INT);
    nmo_object_id_t found = operation(&f, OP_GET_OBJECT_BY_NAME, owner_name, 0, CKPGUID_3DENTITY);
    ASSERT_TRUE(length != 0 && found != 0);
    nmo_object_id_t read_length = identity_of(&f, length, CKPGUID_INT, f.script_in);
    ASSERT_TRUE(read_length != 0);
    nmo_object_id_t read_found =
        identity_of(&f, found, CKPGUID_3DENTITY, BLOCK_OUT(&f, read_length, 0));
    ASSERT_TRUE(read_found != 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* "Ab" + "cd" is "Abcd": 4 characters */
    int32_t count = 0;
    read_value(&f, BLOCK_POUT(&f, read_length, 0), &count, sizeof(count));
    ASSERT_EQ(4, count);
    uint32_t object = 0;
    read_value(&f, BLOCK_POUT(&f, read_found, 0), &object, sizeof(object));
    ASSERT_EQ(f.owner_id, object);
    fixture_end(&f);
}

/* A local parameter of the script of object type `type`; its object is set in the engine */
static nmo_object_id_t local_object(engine_fixture_t *f, const char *name, nmo_guid_t type)
{
    nmo_object_id_t param = 0;
    return nmo_script_edit_add_parameter(f->tx, f->script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL, type,
                                         name, &param) == NMO_OK
               ? param
               : 0;
}

TEST(engine_strings, object_operations)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Group");
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_TRUE(other != 0 && state != NULL);
    nmo_ref_t *members = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&state->object_ids, 2u, (void **)&members));
    members[0] = nmo_ref_from_id(f.owner_id);
    members[1] = nmo_ref_from_id(other);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t object = local_object(&f, "Object", CKPGUID_3DENTITY);
    nmo_object_id_t in_group = local_object(&f, "Group", CKPGUID_GROUP);
    ASSERT_TRUE(object != 0 && in_group != 0);
    nmo_object_id_t type = operation(&f, OP_GET_TYPE, object, 0, CKPGUID_CLASSID);
    nmo_object_id_t count = operation(&f, OP_GET_COUNT, in_group, 0, CKPGUID_INT);
    ASSERT_TRUE(type != 0 && count != 0);
    nmo_object_id_t read_type = identity_of(&f, type, CKPGUID_CLASSID, f.script_in);
    ASSERT_TRUE(read_type != 0);
    nmo_object_id_t read_count = identity_of(&f, count, CKPGUID_INT, BLOCK_OUT(&f, read_type, 0));
    ASSERT_TRUE(read_count != 0);
    fixture_commit(&f, NULL);
    const uint32_t ids[2] = {(uint32_t)other, (uint32_t)group};
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, object, &ids[0], sizeof(ids[0])));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, in_group, &ids[1], sizeof(ids[1])));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    int32_t value = 0;
    read_value(&f, BLOCK_POUT(&f, read_type, 0), &value, sizeof(value));
    ASSERT_EQ(NMO_CID_3DENTITY, value);
    read_value(&f, BLOCK_POUT(&f, read_count, 0), &value, sizeof(value));
    ASSERT_EQ(2, value);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_strings, create_string_writes_values_as_ck2_does);
REGISTER_TEST(engine_strings, create_string_adds_lines_and_clears);
REGISTER_TEST(engine_strings, substring_scan_and_reverse);
REGISTER_TEST(engine_strings, string_operations);
REGISTER_TEST(engine_strings, object_operations);
TEST_MAIN_END()
