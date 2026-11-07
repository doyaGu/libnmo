/**
 * @file test_engine_narratives.c
 * @brief Narratives.dll's global state, initial condition, object state,
 *        rename and script blocks run in graphs.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"

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
#define GUID_GET_CURRENT_SCENE NMO_GUID(0x00dc125f, 0x592b00a8)
#define GUID_REMOVE_OBJECT_FROM_SCENE NMO_GUID(0x683925f3, 0x6fdd3f16)
#define GUID_LAUNCH_SCENE NMO_GUID(0x188d6d43, 0x169613dd)

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

/* The owner's state at x, saved as a chunk (its initial value) */
static nmo_chunk_t *entity_ic(engine_fixture_t *f, nmo_object_id_t id, float x)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_3dentity_state_t saved = *(nmo_3dentity_state_t *)fixture_state(f, id);
    saved.world_matrix[12] = x;
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        return NULL;
    }
    chunk->class_id = NMO_CID_3DENTITY;
    nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena, f->repo, CK_STATESAVE_ALL);
    if (nmo_chunk_start_write(chunk) != NMO_OK || nmo_3dentity_vtable.serialize(&saved, chunk, NULL, &context) != NMO_OK) {
        return NULL;
    }
    nmo_chunk_close(chunk);
    return chunk;
}

/* A level whose scene holds `ids`, starting active and reset, with their initial values */
static void install_level_scene(engine_fixture_t *f, const nmo_object_id_t *ids, nmo_chunk_t *const *ics, size_t count)
{
    nmo_object_id_t level_id = fixture_create(f, NMO_CID_LEVEL, "Level");
    nmo_level_state_t *level = (nmo_level_state_t *)fixture_state(f, level_id);
    ASSERT_NOT_NULL(level);
    nmo_scene_state_t scene;
    memset(&scene, 0, sizeof(scene));
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&scene, NULL, NULL));
    scene.level = nmo_ref_from_id(level_id);
    nmo_scene_object_desc_t *descs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&scene.object_descs, count, (void **)&descs));
    for (size_t i = 0; i < count; i++) {
        descs[i].ref = nmo_ref_from_id(ids[i]);
        descs[i].flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        descs[i].initial_value = ics[i];
    }
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_SCENE;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_serialize_context_t context = nmo_serialize_context_create(arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(&scene, chunk, NULL, &context));
    nmo_chunk_close(chunk);
    level->level_scene_chunk = chunk;
    nmo_scene_vtable.destroy(&scene, NULL, NULL);
}

TEST(engine_narratives, launch_scene_resets_the_level_scene_and_objects_leave_it)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    place_x(&f, f.owner_id, 3.0f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    ASSERT_TRUE(other != 0);
    place_x(&f, other, 0.0f);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    nmo_object_id_t ids[] = {f.owner_id, f.script_id, other};
    nmo_chunk_t *ics[] = {entity_ic(&f, f.owner_id, 3.0f), NULL, NULL};
    ASSERT_NOT_NULL(ics[0]);
    install_level_scene(&f, ids, ics, 3);
    fixture_begin_edit(&f);
    nmo_object_id_t current = after(&f, GUID_GET_CURRENT_SCENE, "Get Current Scene", 0, 0);
    nmo_object_id_t remove = after(&f, GUID_REMOVE_OBJECT_FROM_SCENE, "Remove Object From Scene", current, 0);
    nmo_object_id_t launch = after(&f, GUID_LAUNCH_SCENE, "Launch Scene", remove, 0);
    ASSERT_TRUE(current != 0 && remove != 0 && launch != 0);
    target_of(&f, remove, other);
    fixture_input_int(&f, BLOCK_PIN(&f, launch, 2), 1); /* Reset */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_SCENE_DEFAULT));
    ASSERT_TRUE(eng_in_scene(f.engine, other));

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_object_id_t scene = 1;
    read_value(&f, BLOCK_POUT(&f, current, 0), &scene, sizeof(scene));
    ASSERT_EQ(0u, scene); /* the level scene */
    ASSERT_TRUE(!eng_in_scene(f.engine, other));
    move_x(&f, f.owner_id, 100.0f);
    /* The launch happens as the next frame starts: the owner back at its initial value */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_FLOAT_EQ(3.0f, x_of(&f, f.owner_id), 0.0f);
    ASSERT_TRUE(runs(&f, current) >= 2u); /* the script ran again after the reset */
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_narratives, global_states_enter_leave_and_test);
    REGISTER_TEST(engine_narratives, set_ic_save_state_and_read_state_bring_the_object_back);
    REGISTER_TEST(engine_narratives, object_rename_and_attach_script);
    REGISTER_TEST(engine_narratives, launch_scene_resets_the_level_scene_and_objects_leave_it);
TEST_MAIN_END()
