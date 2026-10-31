#include "engine_fixture.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
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
#define OP_CONVERT NMO_GUID(0x428009b4, 0x1caa5c78)
#define OP_GET_POSITION NMO_GUID(0x4bc87aea, 0x6b5b643e)
#define OP_GET_DISTANCE NMO_GUID(0x265c7d00, 0x5c8f1702)
#define OP_GET_Y NMO_GUID(0x389f72bd, 0x7aef3482)
#define OP_IS_CHILD_OF NMO_GUID(0x08333942, 0x00973fd1)
#define OP_GET_PARENT NMO_GUID(0x5bf9718f, 0x030b698c)
#define OP_GET_COUNT NMO_GUID(0x44df6f61, 0x5f8f55c8)
#define OP_GET_MATERIAL_LIST NMO_GUID(0x765f7aa3, 0x33d679b2)
#define OP_GET_ELEMENT NMO_GUID(0x660f5c52, 0x0adf7be9)
#define OP_GET_DIFFUSE NMO_GUID(0x0f9e20cc, 0x6ea10a47)
#define OP_GET_EMISSIVE NMO_GUID(0x13684596, 0x0ab57f34)

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
    nmo_parameterin_state_t *in2_state =
        in2 != 0 ? (nmo_parameterin_state_t *)fixture_state(f, nmo_parameteroperation_in2_id(state))
                 : NULL;
    const nmo_parameter_state_t *source2 =
        in2 != 0 ? (const nmo_parameter_state_t *)fixture_state(f, in2) : NULL;
    if (in2_state != NULL && source2 != NULL) {
        in2_state->type_guid = source2->type_guid;
    }
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

TEST(engine_strings, create_string_runs_its_inputs_operations)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* "Level" then a number made text by a Convert operation, as Ballance builds file names */
    nmo_object_id_t number = 0;
    const int32_t seven = 7;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, f.script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_INT, "N", &number));
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f.tx, number, (const uint8_t *)&seven,
                                                         sizeof(seven)));
    nmo_object_id_t text = operation(&f, OP_CONVERT, number, 0, CKPGUID_STRING);
    ASSERT_TRUE(text != 0);
    nmo_object_id_t create = fixture_block(&f, GUID_CREATE_STRING, "Create String");
    ASSERT_TRUE(create != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, create, 0), 0);
    input_string(&f, BLOCK_PIN(&f, create, 0), "");
    ASSERT_TRUE(add_input(&f, create, CKPGUID_STRING, "Level", 6) != 0);
    nmo_object_id_t pin = add_param(&f, create, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_STRING);
    ASSERT_TRUE(pin != 0);
    ASSERT_EQ(NMO_OK, nmo_script_edit_connect_parameter(f.tx, text, pin));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_STR_EQ("Level7", string_value(&f, BLOCK_POUT(&f, create, 0)));
    fixture_end(&f);
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

/* Get Object By Name into a Script takes only a behavior of that name (TypeToClassID): Gameplay
   looks up Ball_LightningSphere so, next to a parameter of the same name. "Owner" names the
   fixture's 3D entity. */
TEST(engine_strings, object_by_name_takes_the_script_class)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t entity_name = local_string(&f, "A", "Owner");
    nmo_object_id_t script_name = local_string(&f, "B", "Script");
    ASSERT_TRUE(entity_name != 0);
    ASSERT_TRUE(script_name != 0);
    nmo_object_id_t entity = operation(&f, OP_GET_OBJECT_BY_NAME, entity_name, 0, CKPGUID_SCRIPT);
    nmo_object_id_t script = operation(&f, OP_GET_OBJECT_BY_NAME, script_name, 0, CKPGUID_SCRIPT);
    ASSERT_TRUE(entity != 0 && script != 0);
    nmo_object_id_t read_entity = identity_of(&f, entity, CKPGUID_SCRIPT, f.script_in);
    ASSERT_TRUE(read_entity != 0);
    nmo_object_id_t read_script =
        identity_of(&f, script, CKPGUID_SCRIPT, BLOCK_OUT(&f, read_entity, 0));
    ASSERT_TRUE(read_script != 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    uint32_t object = 1;
    read_value(&f, BLOCK_POUT(&f, read_entity, 0), &object, sizeof(object));
    ASSERT_EQ(0u, object);
    read_value(&f, BLOCK_POUT(&f, read_script, 0), &object, sizeof(object));
    ASSERT_EQ((uint32_t)f.script_id, object);
    fixture_end(&f);
}

/* A color operation on `source` (an Object), whose input is a Material as AnimTrafo's are */
static nmo_object_id_t material_operation(engine_fixture_t *f, nmo_guid_t guid,
                                          nmo_object_id_t source)
{
    nmo_object_id_t op = 0;
    if (nmo_script_edit_add_operation(f->tx, f->script_id, guid, source, 0, 0, &op) != NMO_OK) {
        return 0;
    }
    const nmo_parameteroperation_state_t *state =
        (const nmo_parameteroperation_state_t *)fixture_state(f, op);
    nmo_parameterin_state_t *in1 =
        (nmo_parameterin_state_t *)fixture_state(f, nmo_parameteroperation_in1_id(state));
    nmo_object_id_t out = nmo_parameteroperation_out_id(state);
    nmo_parameter_state_t *out_state = (nmo_parameter_state_t *)fixture_state(f, out);
    if (in1 == NULL || out_state == NULL) {
        return 0;
    }
    in1->type_guid = CKPGUID_MATERIAL;
    out_state->type_guid = CKPGUID_COLOR;
    return out;
}

/* AnimTrafo's colors: a mesh's material list, its element 1, and that material's colors */
TEST(engine_strings, material_operations)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = fixture_create(&f, NMO_CID_MESH, "Mesh");
    nmo_object_id_t first = fixture_create(&f, NMO_CID_MATERIAL, "First");
    nmo_object_id_t second = fixture_create(&f, NMO_CID_MATERIAL, "Second");
    nmo_mesh_state_t *mesh_state = (nmo_mesh_state_t *)fixture_state(&f, mesh);
    nmo_material_state_t *material = (nmo_material_state_t *)fixture_state(&f, second);
    ASSERT_TRUE(mesh_state != NULL && material != NULL);
    material->diffuse_color = 0xFF18EA94u;
    material->emissive_color = 0xFF003719u;
    /* group 0 has no material, as CKMesh's first group */
    mesh_state->material_groups = (nmo_material_group_t *)nmo_arena_alloc(
        nmo_session_get_arena(f.session), 3u * sizeof(nmo_material_group_t),
        _Alignof(nmo_material_group_t));
    ASSERT_NOT_NULL(mesh_state->material_groups);
    memset(mesh_state->material_groups, 0, 3u * sizeof(nmo_material_group_t));
    mesh_state->material_groups[0].material = nmo_ref_from_id(0);
    mesh_state->material_groups[1].material = nmo_ref_from_id(first);
    mesh_state->material_groups[2].material = nmo_ref_from_id(second);
    mesh_state->material_group_count = 3;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t in_mesh = local_object(&f, "Mesh", CKPGUID_MESH);
    nmo_object_id_t index = 0;
    const int32_t one = 1;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, f.script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_INT, "Index", &index));
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f.tx, index, (const uint8_t *)&one,
                                                          sizeof(one)));
    ASSERT_TRUE(in_mesh != 0);
    nmo_object_id_t list = operation(&f, OP_GET_MATERIAL_LIST, in_mesh, 0, CKPGUID_OBJECTARRAY);
    nmo_object_id_t element = operation(&f, OP_GET_ELEMENT, list, index, CKPGUID_OBJECT);
    ASSERT_TRUE(list != 0 && element != 0);
    nmo_object_id_t diffuse = material_operation(&f, OP_GET_DIFFUSE, element);
    nmo_object_id_t emissive = material_operation(&f, OP_GET_EMISSIVE, element);
    ASSERT_TRUE(diffuse != 0 && emissive != 0);
    nmo_object_id_t read_diffuse = identity_of(&f, diffuse, CKPGUID_COLOR, f.script_in);
    ASSERT_TRUE(read_diffuse != 0);
    nmo_object_id_t read_emissive =
        identity_of(&f, emissive, CKPGUID_COLOR, BLOCK_OUT(&f, read_diffuse, 0));
    ASSERT_TRUE(read_emissive != 0);
    fixture_commit(&f, NULL);
    const uint32_t mesh_id = (uint32_t)mesh;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, in_mesh, &mesh_id, sizeof(mesh_id)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    float color[4] = {0};
    read_value(&f, BLOCK_POUT(&f, read_diffuse, 0), color, sizeof(color));
    ASSERT_TRUE(fabsf(color[1] - 234.0f / 255.0f) < 1e-4f && fabsf(color[2] - 148.0f / 255.0f) < 1e-4f);
    read_value(&f, BLOCK_POUT(&f, read_emissive, 0), color, sizeof(color));
    ASSERT_TRUE(fabsf(color[1] - 55.0f / 255.0f) < 1e-4f && fabsf(color[0]) < 1e-4f);
    fixture_end(&f);
}

/* A local parameter of the script of `type` holding the float `value` */
static nmo_object_id_t local_float(engine_fixture_t *f, const char *name, nmo_guid_t type,
                                   float value)
{
    nmo_object_id_t param = 0;
    if (nmo_script_edit_add_parameter(f->tx, f->script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL, type, name,
                                      &param) != NMO_OK ||
        nmo_script_edit_set_parameter_bytes(f->tx, param, (const uint8_t *)&value,
                                            sizeof(value)) != NMO_OK) {
        return 0;
    }
    return param;
}

TEST(engine_strings, operations_follow_parent_types)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = local_float(&f, "A", CKPGUID_TIME, 250.0f);
    nmo_object_id_t b = local_float(&f, "B", CKPGUID_TIME, 500.0f);
    nmo_object_id_t half = local_float(&f, "Half", CKPGUID_FLOAT, 0.5f);
    nmo_object_id_t x = local_float(&f, "X", CKPGUID_FLOAT, 2.0f);
    nmo_object_id_t y = local_float(&f, "Y", CKPGUID_FLOAT, 3.0f);
    ASSERT_TRUE(a != 0 && b != 0 && half != 0 && x != 0 && y != 0);
    /* Time + Time: the Float addition (Time derives from Float) */
    nmo_object_id_t sum = operation(&f, OP_ADDITION, a, b, CKPGUID_TIME);
    /* Bool from Float, not the Int conversion Bool derives from */
    nmo_object_id_t truth = operation(&f, OP_CONVERT, half, 0, CKPGUID_BOOL);
    nmo_object_id_t xy = operation(&f, OP_CONVERT, x, y, CKPGUID_2DVECTOR);
    ASSERT_TRUE(sum != 0 && truth != 0 && xy != 0);
    nmo_object_id_t read_sum = identity_of(&f, sum, CKPGUID_TIME, f.script_in);
    ASSERT_TRUE(read_sum != 0);
    nmo_object_id_t read_truth = identity_of(&f, truth, CKPGUID_BOOL, BLOCK_OUT(&f, read_sum, 0));
    ASSERT_TRUE(read_truth != 0);
    nmo_object_id_t read_xy = identity_of(&f, xy, CKPGUID_2DVECTOR, BLOCK_OUT(&f, read_truth, 0));
    ASSERT_TRUE(read_xy != 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    float time = 0.0f, vector[2] = {0.0f, 0.0f};
    int32_t boolean = 0;
    read_value(&f, BLOCK_POUT(&f, read_sum, 0), &time, sizeof(time));
    read_value(&f, BLOCK_POUT(&f, read_truth, 0), &boolean, sizeof(boolean));
    read_value(&f, BLOCK_POUT(&f, read_xy, 0), vector, sizeof(vector));
    ASSERT_FLOAT_EQ(750.0f, time, 0.001f);
    ASSERT_EQ(1, boolean);
    ASSERT_FLOAT_EQ(2.0f, vector[0], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, vector[1], 0.0001f);
    fixture_end(&f);
}

/* An entity saved at `x, y, z`, under `parent` (0 for none) */
static void place_entity(engine_fixture_t *f, nmo_object_id_t id, float x, float y, float z,
                         nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    ASSERT_NOT_NULL(state);
    memset(state->world_matrix, 0, sizeof(state->world_matrix));
    state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = 1.0f;
    state->world_matrix[15] = 1.0f;
    state->world_matrix[12] = x;
    state->world_matrix[13] = y;
    state->world_matrix[14] = z;
    if (parent != 0) {
        state->parent = nmo_ref_from_id(parent);
    }
}

TEST(engine_strings, entity_operations)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = fixture_create(&f, NMO_CID_3DENTITY, "Parent");
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    place_entity(&f, parent, 10.0f, 0.0f, 0.0f, 0);
    place_entity(&f, child, 1.0f, 2.0f, 3.0f, parent);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t a = local_object(&f, "A", CKPGUID_3DENTITY);
    nmo_object_id_t b = local_object(&f, "B", CKPGUID_3DENTITY);
    ASSERT_TRUE(a != 0 && b != 0);
    nmo_object_id_t world = operation(&f, OP_GET_POSITION, a, 0, CKPGUID_VECTOR);
    nmo_object_id_t local = operation(&f, OP_GET_POSITION, a, b, CKPGUID_VECTOR);
    nmo_object_id_t distance = operation(&f, OP_GET_DISTANCE, a, b, CKPGUID_FLOAT);
    nmo_object_id_t is_child = operation(&f, OP_IS_CHILD_OF, a, b, CKPGUID_BOOL);
    nmo_object_id_t parent_of = operation(&f, OP_GET_PARENT, a, 0, CKPGUID_3DENTITY);
    ASSERT_TRUE(world != 0 && local != 0 && distance != 0 && is_child != 0 && parent_of != 0);
    nmo_object_id_t y = operation(&f, OP_GET_Y, world, 0, CKPGUID_FLOAT);
    ASSERT_TRUE(y != 0);
    nmo_object_id_t reads[5];
    const nmo_object_id_t sources[5] = {local, distance, is_child, parent_of, y};
    const nmo_guid_t types[5] = {CKPGUID_VECTOR, CKPGUID_FLOAT, CKPGUID_BOOL, CKPGUID_3DENTITY,
                                 CKPGUID_FLOAT};
    nmo_object_id_t after = f.script_in;
    for (int i = 0; i < 5; i++) {
        reads[i] = identity_of(&f, sources[i], types[i], after);
        ASSERT_TRUE(reads[i] != 0);
        after = BLOCK_OUT(&f, reads[i], 0);
    }
    fixture_commit(&f, NULL);
    const uint32_t ids[2] = {(uint32_t)child, (uint32_t)parent};
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, a, &ids[0], sizeof(ids[0])));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, b, &ids[1], sizeof(ids[1])));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* In the parent's space: the local position */
    float v[3] = {0.0f, 0.0f, 0.0f}, number = 0.0f;
    read_value(&f, BLOCK_POUT(&f, reads[0], 0), v, sizeof(v));
    ASSERT_FLOAT_EQ(-9.0f, v[0], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, v[1], 0.0001f);
    read_value(&f, BLOCK_POUT(&f, reads[1], 0), &number, sizeof(number));
    ASSERT_FLOAT_EQ(sqrtf(94.0f), number, 0.0001f);
    int32_t flag = 0;
    read_value(&f, BLOCK_POUT(&f, reads[2], 0), &flag, sizeof(flag));
    ASSERT_EQ(1, flag);
    uint32_t id = 0;
    read_value(&f, BLOCK_POUT(&f, reads[3], 0), &id, sizeof(id));
    ASSERT_EQ(parent, id);
    /* Get Y of the world position */
    read_value(&f, BLOCK_POUT(&f, reads[4], 0), &number, sizeof(number));
    ASSERT_FLOAT_EQ(2.0f, number, 0.0001f);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_strings, create_string_writes_values_as_ck2_does);
REGISTER_TEST(engine_strings, create_string_adds_lines_and_clears);
REGISTER_TEST(engine_strings, substring_scan_and_reverse);
REGISTER_TEST(engine_strings, string_operations);
REGISTER_TEST(engine_strings, create_string_runs_its_inputs_operations);
REGISTER_TEST(engine_strings, object_operations);
REGISTER_TEST(engine_strings, material_operations);
REGISTER_TEST(engine_strings, object_by_name_takes_the_script_class);
REGISTER_TEST(engine_strings, operations_follow_parent_types);
REGISTER_TEST(engine_strings, entity_operations);
TEST_MAIN_END()
