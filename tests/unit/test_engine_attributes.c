#include "engine_fixture.h"

#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_edit.h"
#include "type/nmo_type_guids.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_HAS_ATTRIBUTE NMO_GUID(0x25b54079, 0x6ff90545)
#define GUID_SET_ATTRIBUTE NMO_GUID(0x373040f2, 0x05e01b34)
#define GUID_REMOVE_ATTRIBUTE NMO_GUID(0x6b6340c4, 0x61e94a41)
#define GUID_OBJECTS_WITH_ATTRIBUTE NMO_GUID(0x6bc1494c, 0x0c816ad3)

/* An attribute type of the file, made in the edit: a float for objects of `class_id` */
static uint32_t attribute_type(engine_fixture_t *f, const char *name, uint32_t class_id)
{
    nmo_manager_entry_create_options_t create = {
        .enabled = true,
        .attribute_type_guid = CKPGUID_FLOAT,
        .category = "Test",
        .has_compatible_class_id = true,
        .compatible_class_id = class_id,
        .has_flags = true,
        .flags = 0u,
    };
    uint32_t type = UINT32_MAX;
    nmo_status_t status = nmo_object_edit_ensure_attribute_manager_entry(
        nmo_script_edit_workspace_edit(f->tx), name, &create, &type);
    return status == NMO_OK ? type : UINT32_MAX;
}

/* Save attribute `type` on `object` with a float parameter holding `value` */
static bool save_attribute(engine_fixture_t *f, nmo_object_id_t object, uint32_t type, float value)
{
    nmo_object_id_t param = fixture_create(f, NMO_CID_PARAMETEROUT, "Attribute Value");
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, param);
    nmo_beobject_state_t *owner = (nmo_beobject_state_t *)fixture_state(f, object);
    if (state == NULL || owner == NULL ||
        nmo_array_alloc(&state->buffer_data, 1u, sizeof(value), NULL) != NMO_OK) {
        return false;
    }
    state->type_guid = CKPGUID_FLOAT;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    memcpy(state->buffer_data.data, &value, sizeof(value));
    return nmo_beobject_attribute_array_append(&owner->attributes, param, type, NULL) == NMO_OK;
}

static float attribute_float(engine_fixture_t *f, nmo_object_id_t object, uint32_t type)
{
    const void *data = NULL;
    size_t size = 0;
    float value = -1.0f;
    if (nmo_engine_object_attribute_value(f->engine, object, (int32_t)type, &data, &size, NULL) ==
            NMO_OK &&
        size == sizeof(value)) {
        memcpy(&value, data, sizeof(value));
    }
    return value;
}

/* An attribute block targeting the script's owner; `type` into its Attribute input */
static nmo_object_id_t attribute_block(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                       uint32_t type)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block == 0 || fixture_target(f, block) == 0) {
        return 0;
    }
    fixture_input_int(f, fixture_target(f, block), (int32_t)f->owner_id);
    fixture_input_int(f, BLOCK_PIN(f, block, 0), (int32_t)type);
    return block;
}

TEST(engine_attributes, blocks_test_set_and_remove_attributes)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    uint32_t health = attribute_type(&f, "Health", NMO_CID_BEOBJECT);
    ASSERT_TRUE(health != UINT32_MAX);
    ASSERT_TRUE(save_attribute(&f, f.owner_id, health, 2.5f));

    nmo_object_id_t has = attribute_block(&f, GUID_HAS_ATTRIBUTE, "Has", health);
    nmo_object_id_t set = attribute_block(&f, GUID_SET_ATTRIBUTE, "Set", health);
    nmo_object_id_t remove = attribute_block(&f, GUID_REMOVE_ATTRIBUTE, "Remove", health);
    nmo_object_id_t has_not = attribute_block(&f, GUID_HAS_ATTRIBUTE, "Has Not", health);
    nmo_object_id_t gone = fixture_block(&f, GUID_NOP, "Gone");
    ASSERT_TRUE(has != 0 && set != 0 && remove != 0 && has_not != 0 && gone != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, has, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, has, 0), BLOCK_IN(&f, set, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, set, 0), BLOCK_IN(&f, remove, 0), 1);
    fixture_link(&f, BLOCK_OUT(&f, remove, 0), BLOCK_IN(&f, has_not, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, has_not, 1), BLOCK_IN(&f, gone, 0), 0);
    /* Has Attribute's value output takes the attribute's type, as its callback makes it */
    nmo_parameter_state_t *value_out =
        (nmo_parameter_state_t *)fixture_state(&f, BLOCK_POUT(&f, has, 0));
    ASSERT_NOT_NULL(value_out);
    value_out->type_guid = CKPGUID_FLOAT;
    nmo_object_id_t new_value = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, set, NMO_SCRIPT_EDIT_PARAM_IN,
                                                    CKPGUID_FLOAT, "Value", &new_value));
    const float seven = 7.5f;
    fixture_input_value(&f, new_value, &seven, sizeof(seven));
    fixture_commit(&f, NULL);

    ASSERT_TRUE(nmo_engine_object_has_attribute(f.engine, f.owner_id, (int32_t)health));
    ASSERT_FLOAT_EQ(2.5f, attribute_float(&f, f.owner_id, health), 0.0001f);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Has: True with the saved value; Set: the object has it, so only the value changes */
    float read = 0.0f;
    read_value(&f, BLOCK_POUT(&f, has, 0), &read, sizeof(read));
    ASSERT_FLOAT_EQ(2.5f, read, 0.0001f);
    ASSERT_FLOAT_EQ(7.5f, attribute_float(&f, f.owner_id, health), 0.0001f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FALSE(nmo_engine_object_has_attribute(f.engine, f.owner_id, (int32_t)health));
    ASSERT_EQ(1u, runs(&f, gone));
    fixture_end(&f);
}

TEST(engine_attributes, set_attribute_follows_the_attribute_type)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    uint32_t health = attribute_type(&f, "Health", NMO_CID_BEOBJECT);
    uint32_t sprite_only = attribute_type(&f, "Sprite Only", NMO_CID_2DENTITY);
    ASSERT_TRUE(health != UINT32_MAX && sprite_only != UINT32_MAX);
    fixture_commit(&f, NULL);

    ASSERT_FALSE(nmo_engine_object_has_attribute(f.engine, other, (int32_t)health));
    ASSERT_TRUE(nmo_engine_object_set_attribute(f.engine, other, (int32_t)health));
    ASSERT_FLOAT_EQ(0.0f, attribute_float(&f, other, health), 0.0001f);
    /* Once only, for a class the type allows, and a type the file has */
    ASSERT_FALSE(nmo_engine_object_set_attribute(f.engine, other, (int32_t)health));
    ASSERT_FALSE(nmo_engine_object_set_attribute(f.engine, other, (int32_t)sprite_only));
    ASSERT_FALSE(nmo_engine_object_set_attribute(f.engine, other, 99));
    ASSERT_FALSE(nmo_engine_object_set_attribute(f.engine, other, -1));
    ASSERT_TRUE(nmo_engine_object_remove_attribute(f.engine, other, (int32_t)health));
    ASSERT_FALSE(nmo_engine_object_remove_attribute(f.engine, other, (int32_t)health));
    fixture_end(&f);
}

TEST(engine_attributes, objects_with_attribute_iterator_walks_the_holders)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    fixture_create(&f, NMO_CID_3DENTITY, "Without");
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    uint32_t health = attribute_type(&f, "Health", NMO_CID_BEOBJECT);
    ASSERT_TRUE(health != UINT32_MAX);
    ASSERT_TRUE(save_attribute(&f, f.owner_id, health, 1.0f));
    ASSERT_TRUE(save_attribute(&f, other, health, 2.0f));
    nmo_object_id_t iterate = fixture_block(&f, GUID_OBJECTS_WITH_ATTRIBUTE, "Iterate");
    nmo_object_id_t each = fixture_block(&f, GUID_NOP, "Each");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(iterate != 0 && each != 0 && done != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, iterate, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, iterate, 1), BLOCK_IN(&f, each, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, each, 0), BLOCK_IN(&f, iterate, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, iterate, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, iterate, 0), (int32_t)health);
    /* The value output takes the attribute's type, as its callback makes it */
    nmo_parameter_state_t *value_out =
        (nmo_parameter_state_t *)fixture_state(&f, BLOCK_POUT(&f, iterate, 1));
    ASSERT_NOT_NULL(value_out);
    value_out->type_guid = CKPGUID_FLOAT;
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(2u, runs(&f, each));
    ASSERT_EQ(1u, runs(&f, done));
    /* The last holder and its value */
    uint32_t object = 0;
    float value = 0.0f;
    read_value(&f, BLOCK_POUT(&f, iterate, 0), &object, sizeof(object));
    read_value(&f, BLOCK_POUT(&f, iterate, 1), &value, sizeof(value));
    ASSERT_EQ(other, object);
    ASSERT_FLOAT_EQ(2.0f, value, 0.0001f);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_attributes, blocks_test_set_and_remove_attributes);
REGISTER_TEST(engine_attributes, set_attribute_follows_the_attribute_type);
REGISTER_TEST(engine_attributes, objects_with_attribute_iterator_walks_the_holders);
TEST_MAIN_END()
