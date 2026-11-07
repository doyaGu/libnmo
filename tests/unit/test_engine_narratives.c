/**
 * @file test_engine_narratives.c
 * @brief Narratives.dll's global state, initial condition, object state,
 *        rename and script blocks run in graphs.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "object/builtin/nmo_3dentity_schemas.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_ACTIVATE_GLOBAL_STATE NMO_GUID(0x373a615b, 0x769a6d53)
#define GUID_DEACTIVATE_GLOBAL_STATE NMO_GUID(0x520d14a5, 0x797502ae)
#define GUID_IS_GLOBAL_STATE_ACTIVE NMO_GUID(0x04e65bdd, 0x473c3681)
#define GUID_SET_IC NMO_GUID(0x72671a24, 0x161a2347)
#define GUID_RESTORE_IC NMO_GUID(0x766e4e44, 0x4fac6d52)
#define GUID_SAVE_STATE NMO_GUID(0x200123d8, 0x65f802b6)
#define GUID_READ_STATE NMO_GUID(0x3ab6273f, 0x578f7118)
#define GUID_OBJECT_RENAME NMO_GUID(0x4cae18cc, 0x03520c5a)
#define GUID_ATTACH_SCRIPT NMO_GUID(0x1dba5f4a, 0x14481212)

static nmo_object_id_t after(engine_fixture_t *f, nmo_guid_t guid, const char *name, nmo_object_id_t previous,
                             uint32_t delay)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    if (id != 0) {
        fixture_link(f, previous != 0 ? BLOCK_OUT(f, previous, 0) : f->script_in, BLOCK_IN(f, id, 0), delay);
    }
    return id;
}

static void target_of(engine_fixture_t *f, nmo_object_id_t block, nmo_object_id_t object)
{
    nmo_object_id_t target = fixture_target(f, block);
    ASSERT_TRUE(target != 0);
    fixture_input_int(f, target, (int32_t)object);
}

static void place_x(engine_fixture_t *f, nmo_object_id_t id, float x)
{
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    ASSERT_NOT_NULL(state);
    memset(state->world_matrix, 0, sizeof(state->world_matrix));
    state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = state->world_matrix[15] = 1;
    state->world_matrix[12] = x;
    state->has_entityndata_chunk = 1;
}

static float x_of(engine_fixture_t *f, nmo_object_id_t id)
{
    float world[16];
    return nmo_engine_entity_world_matrix(f->engine, id, world) ? world[12] : -1000.0f;
}

static void move_x(engine_fixture_t *f, nmo_object_id_t id, float x)
{
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f->engine, id, world));
    world[12] = x;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f->engine, id, world, true));
}

TEST(engine_narratives, global_states_enter_leave_and_test)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t state = fixture_create(&f, NMO_CID_STATE, "Night");
    ASSERT_TRUE(state != 0);
    nmo_object_id_t enter = after(&f, GUID_ACTIVATE_GLOBAL_STATE, "Activate", 0, 0);
    nmo_object_id_t test1 = after(&f, GUID_IS_GLOBAL_STATE_ACTIVE, "Is Active", enter, 0);
    nmo_object_id_t leave = after(&f, GUID_DEACTIVATE_GLOBAL_STATE, "Deactivate", test1, 0);
    nmo_object_id_t test2 = after(&f, GUID_IS_GLOBAL_STATE_ACTIVE, "Is Active Again", leave, 0);
    nmo_object_id_t yes = fixture_block(&f, GUID_NOP, "Yes");
    nmo_object_id_t no = fixture_block(&f, GUID_NOP, "No");
    ASSERT_TRUE(enter != 0 && test1 != 0 && leave != 0 && test2 != 0 && yes != 0 && no != 0);
    fixture_link(&f, BLOCK_OUT(&f, test2, 0), BLOCK_IN(&f, yes, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, test2, 1), BLOCK_IN(&f, no, 0), 0);
    nmo_object_id_t blocks[] = {enter, test1, leave, test2};
    for (int i = 0; i < 4; i++) {
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 0), (int32_t)state);
    }
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, leave)); /* only the first test's True leads there */
    ASSERT_EQ(0u, runs(&f, yes));
    ASSERT_EQ(1u, runs(&f, no)); /* left by then */
    ASSERT_TRUE(!eng_synchro(f.engine, state)->state_active);
    fixture_end(&f);
}

TEST(engine_narratives, set_ic_save_state_and_read_state_bring_the_object_back)
{
    engine_fixture_t f;
    fixture_begin(&f);
    place_x(&f, f.owner_id, 5.0f);
    nmo_object_id_t set_ic = after(&f, GUID_SET_IC, "Set IC", 0, 0);
    nmo_object_id_t restore = after(&f, GUID_RESTORE_IC, "Restore IC", set_ic, 1);
    nmo_object_id_t save = after(&f, GUID_SAVE_STATE, "Save State", restore, 1);
    nmo_object_id_t read = after(&f, GUID_READ_STATE, "Read State", save, 1);
    ASSERT_TRUE(set_ic != 0 && restore != 0 && save != 0 && read != 0);
    nmo_object_id_t blocks[] = {set_ic, restore, save, read};
    for (int i = 0; i < 4; i++) {
        target_of(&f, blocks[i], f.owner_id);
    }
    ASSERT_EQ(NMO_OK, nmo_script_edit_connect_parameter(f.tx, BLOCK_POUT(&f, save, 0), BLOCK_PIN(&f, read, 0)));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f)); /* Set IC at 5 */
    move_x(&f, f.owner_id, 7.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f)); /* Restore IC */
    ASSERT_FLOAT_EQ(5.0f, x_of(&f, f.owner_id), 0.0f);
    move_x(&f, f.owner_id, 9.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f)); /* Save State at 9 */
    move_x(&f, f.owner_id, 11.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f)); /* Read State */
    ASSERT_FLOAT_EQ(9.0f, x_of(&f, f.owner_id), 0.0f);
    fixture_end(&f);
}

TEST(engine_narratives, object_rename_and_attach_script)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t main_owner = f.owner_id;
    nmo_object_id_t holder = fixture_create(&f, NMO_CID_3DENTITY, "Holder");
    nmo_object_id_t receiver = fixture_create(&f, NMO_CID_3DENTITY, "Receiver");
    ASSERT_TRUE(holder != 0 && receiver != 0);
    f.owner_id = holder;
    nmo_object_id_t template_in = 0;
    nmo_object_id_t template = fixture_add_script(&f, "Template", &template_in);
    f.owner_id = main_owner;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    ASSERT_TRUE(template != 0 && f.script_id != 0);
    fixture_begin_edit(&f);
    nmo_object_id_t inner = fixture_block_in(&f, template, GUID_NOP, "Inner");
    ASSERT_TRUE(inner != 0);
    fixture_link_in(&f, template, template_in, BLOCK_IN(&f, inner, 0), 0);
    nmo_object_id_t rename = after(&f, GUID_OBJECT_RENAME, "Object Rename", 0, 0);
    nmo_object_id_t attach = after(&f, GUID_ATTACH_SCRIPT, "Attach Script", rename, 0);
    ASSERT_TRUE(rename != 0 && attach != 0);
    target_of(&f, rename, receiver);
    target_of(&f, attach, receiver);
    const char name[] = "Renamed";
    fixture_input_value(&f, BLOCK_PIN(&f, rename, 0), name, sizeof(name));
    fixture_input_int(&f, BLOCK_PIN(&f, attach, 1), (int32_t)template);
    fixture_input_int(&f, BLOCK_PIN(&f, attach, 0), 0); /* not dynamic */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    ASSERT_STR_EQ("Renamed", nmo_object_get_name(eng_find_object(f.engine, receiver)));
    nmo_object_id_t copy = 0;
    read_value(&f, BLOCK_POUT(&f, attach, 0), &copy, sizeof(copy));
    ASSERT_TRUE(copy != 0 && copy != template);
    int32_t node = eng_map_get(&f.engine->node_map, copy);
    ASSERT_TRUE(node >= 0);
    ASSERT_EQ(receiver, f.engine->nodes.data[node].owner);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(runs(&f, copy) >= 1u); /* the copy runs on its new owner */
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_narratives, global_states_enter_leave_and_test);
    REGISTER_TEST(engine_narratives, set_ic_save_state_and_read_state_bring_the_object_back);
    REGISTER_TEST(engine_narratives, object_rename_and_attach_script);
TEST_MAIN_END()
