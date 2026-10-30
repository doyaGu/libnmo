#include "engine_fixture.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"

#include <math.h>

#define GUID_SET_POSITION NMO_GUID(0xe456e78a, 0x456789aa)
#define GUID_SET_PARENT NMO_GUID(0x9d9d9d98, 0x7e7a7f75)
#define GUID_ROTATE NMO_GUID(0xffffffee, 0xeeffffff)
#define GUID_SCALE NMO_GUID(0x41236987, 0xa54a87a6)
#define GUID_SET_EULER_ORIENTATION NMO_GUID(0x0c4966d8, 0x6c0c6d14)
#define GUID_LOOK_AT NMO_GUID(0x26843971, 0x86249317)
#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_NEAREST_IN_GROUP NMO_GUID(0x085207eb, 0x584950d8)
#define GUID_SCALEABLE_PROXIMITY NMO_GUID(0x2a2a63ca, 0x00826175)
#define GUID_SET_DYNAMIC_POSITION NMO_GUID(0x0fd4755f, 0x7de22dc8)

#define PI_F 3.14159265f

/* An entity saved at `x, y, z` with unit axes, under `parent` (0 for none) */
static nmo_object_id_t place_entity(engine_fixture_t *f, nmo_object_id_t id, float x, float y,
                                    float z, nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    if (state == NULL) {
        return 0;
    }
    memset(state->world_matrix, 0, sizeof(state->world_matrix));
    state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = 1.0f;
    state->world_matrix[15] = 1.0f;
    state->world_matrix[12] = x;
    state->world_matrix[13] = y;
    state->world_matrix[14] = z;
    if (parent != 0) {
        state->parent = nmo_ref_from_id(parent);
    }
    return id;
}

static void world_matrix(engine_fixture_t *f, nmo_object_id_t id, float m[16])
{
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f->engine, id, m));
}

#define ASSERT_POSITION(f, id, x, y, z)                                                            \
    do {                                                                                           \
        float m_[16];                                                                              \
        world_matrix((f), (id), m_);                                                               \
        ASSERT_FLOAT_EQ((x), m_[12], 0.0001f);                                                     \
        ASSERT_FLOAT_EQ((y), m_[13], 0.0001f);                                                     \
        ASSERT_FLOAT_EQ((z), m_[14], 0.0001f);                                                     \
    } while (0)

/* A block targeting `target`, linked after `after` */
static nmo_object_id_t target_block(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                    nmo_object_id_t target, nmo_object_id_t after)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block == 0 || fixture_target(f, block) == 0) {
        return 0;
    }
    fixture_input_int(f, fixture_target(f, block), (int32_t)target);
    fixture_link(f, after, BLOCK_IN(f, block, 0), 0);
    return block;
}

TEST(engine_transform, loaded_children_keep_their_place_under_the_parent)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = fixture_create(&f, NMO_CID_3DENTITY, "Parent");
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    ASSERT_TRUE(place_entity(&f, parent, 10.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, child, 12.0f, 1.0f, 0.0f, parent) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);

    ASSERT_EQ(parent, nmo_engine_entity_parent(f.engine, child));
    ASSERT_EQ(0u, nmo_engine_entity_parent(f.engine, parent));
    float local[16];
    ASSERT_TRUE(nmo_engine_entity_local_matrix(f.engine, child, local));
    ASSERT_FLOAT_EQ(2.0f, local[12], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, local[13], 0.0001f);
    /* The host moves the parent: the child moves along, unless kept */
    float m[16];
    world_matrix(&f, parent, m);
    m[12] = 20.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, parent, m, false));
    ASSERT_POSITION(&f, child, 22.0f, 1.0f, 0.0f);
    m[12] = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, parent, m, true));
    ASSERT_POSITION(&f, child, 22.0f, 1.0f, 0.0f);
    ASSERT_TRUE(nmo_engine_entity_local_matrix(f.engine, child, local));
    ASSERT_FLOAT_EQ(22.0f, local[12], 0.0001f);
    ASSERT_FALSE(nmo_engine_entity_world_matrix(f.engine, f.script_id, m));
    fixture_end(&f);
}

TEST(engine_transform, set_position_and_set_parent)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = fixture_create(&f, NMO_CID_3DENTITY, "Parent");
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    nmo_object_id_t loose = fixture_create(&f, NMO_CID_3DENTITY, "Loose");
    ASSERT_TRUE(place_entity(&f, parent, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, child, 1.0f, 0.0f, 0.0f, parent) != 0);
    ASSERT_TRUE(place_entity(&f, loose, 0.0f, 0.0f, 5.0f, 0) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* Moved with its child, then without, then the loose one into the parent's space */
    nmo_object_id_t along = target_block(&f, GUID_SET_POSITION, "Along", parent, f.script_in);
    ASSERT_TRUE(along != 0);
    nmo_object_id_t alone = target_block(&f, GUID_SET_POSITION, "Alone", parent, BLOCK_OUT(&f, along, 0));
    ASSERT_TRUE(alone != 0);
    nmo_object_id_t adopt = target_block(&f, GUID_SET_PARENT, "Adopt", loose, BLOCK_OUT(&f, alone, 0));
    ASSERT_TRUE(adopt != 0);
    nmo_object_id_t inside = target_block(&f, GUID_SET_POSITION, "Inside", loose, BLOCK_OUT(&f, adopt, 0));
    ASSERT_TRUE(inside != 0);
    const float ten[3] = {10.0f, 0.0f, 0.0f}, twenty[3] = {20.0f, 0.0f, 0.0f};
    const float one_up[3] = {0.0f, 1.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, along, 0), ten, sizeof(ten));
    fixture_input_value(&f, BLOCK_PIN(&f, alone, 0), twenty, sizeof(twenty));
    fixture_input_int(&f, BLOCK_PIN(&f, alone, 2), 0); /* Hierarchy: no */
    fixture_input_int(&f, BLOCK_PIN(&f, adopt, 0), (int32_t)parent);
    fixture_input_value(&f, BLOCK_PIN(&f, inside, 0), one_up, sizeof(one_up));
    fixture_input_int(&f, BLOCK_PIN(&f, inside, 1), (int32_t)parent); /* Referential */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_POSITION(&f, parent, 20.0f, 0.0f, 0.0f);
    ASSERT_POSITION(&f, child, 11.0f, 0.0f, 0.0f); /* followed to 10, then kept */
    /* Set Parent keeps the world position: the local one follows */
    ASSERT_EQ(parent, nmo_engine_entity_parent(f.engine, loose));
    ASSERT_POSITION(&f, loose, 20.0f, 1.0f, 0.0f);
    float local[16];
    ASSERT_TRUE(nmo_engine_entity_local_matrix(f.engine, loose, local));
    ASSERT_FLOAT_EQ(1.0f, local[13], 0.0001f);
    fixture_end(&f);
}

TEST(engine_transform, rotate_scale_and_orientation)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t turned = fixture_create(&f, NMO_CID_3DENTITY, "Turned");
    nmo_object_id_t scaled = fixture_create(&f, NMO_CID_3DENTITY, "Scaled");
    nmo_object_id_t oriented = fixture_create(&f, NMO_CID_3DENTITY, "Oriented");
    nmo_object_id_t looking = fixture_create(&f, NMO_CID_3DENTITY, "Looking");
    ASSERT_TRUE(place_entity(&f, turned, 1.0f, 2.0f, 3.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, scaled, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, oriented, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, looking, 0.0f, 0.0f, 0.0f, 0) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t rotate = target_block(&f, GUID_ROTATE, "Rotate", turned, f.script_in);
    ASSERT_TRUE(rotate != 0);
    nmo_object_id_t twice = target_block(&f, GUID_SCALE, "Twice", scaled, BLOCK_OUT(&f, rotate, 0));
    ASSERT_TRUE(twice != 0);
    nmo_object_id_t again = target_block(&f, GUID_SCALE, "Again", scaled, BLOCK_OUT(&f, twice, 0));
    ASSERT_TRUE(again != 0);
    nmo_object_id_t euler =
        target_block(&f, GUID_SET_EULER_ORIENTATION, "Euler", oriented, BLOCK_OUT(&f, again, 0));
    ASSERT_TRUE(euler != 0);
    nmo_object_id_t look = target_block(&f, GUID_LOOK_AT, "Look", looking, BLOCK_OUT(&f, euler, 0));
    ASSERT_TRUE(look != 0);
    const float axis[3] = {0.0f, 1.0f, 0.0f};
    const float quarter = PI_F / 2.0f;
    const float double_x[3] = {2.0f, 1.0f, 1.0f};
    const float yaw[3] = {0.0f, PI_F / 2.0f, 0.0f};
    const float target[3] = {-5.0f, 0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, rotate, 0), axis, sizeof(axis));
    fixture_input_value(&f, BLOCK_PIN(&f, rotate, 1), &quarter, sizeof(quarter));
    fixture_input_value(&f, BLOCK_PIN(&f, twice, 0), double_x, sizeof(double_x));
    fixture_input_value(&f, BLOCK_PIN(&f, again, 0), double_x, sizeof(double_x));
    fixture_input_value(&f, BLOCK_PIN(&f, euler, 0), yaw, sizeof(yaw));
    fixture_input_value(&f, BLOCK_PIN(&f, look, 0), target, sizeof(target));
    fixture_input_value(&f, BLOCK_PIN(&f, look, 2), &(float){1.0f}, sizeof(float)); /* at once */
    fixture_commit(&f, NULL);
    /* Settings: Merge Angles Params for the euler block, not Time Based for Look At */
    const nmo_behavior_state_t *euler_state = (const nmo_behavior_state_t *)fixture_state(&f, euler);
    const nmo_behavior_state_t *look_state = (const nmo_behavior_state_t *)fixture_state(&f, look);
    ASSERT_TRUE(euler_state != NULL && look_state != NULL);
    const int32_t yes = 1, no = 0, z_axis = 5;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&euler_state->local_parameters, 0),
                          &yes, sizeof(yes)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&look_state->local_parameters, 0),
                          &no, sizeof(no)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&look_state->local_parameters, 1),
                          &z_axis, sizeof(z_axis)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    float m[16];
    /* A quarter turn about Y (Vx3DMatrixFromRotation) keeps the position: X goes to Z, Z to -X */
    world_matrix(&f, turned, m);
    ASSERT_FLOAT_EQ(1.0f, m[2], 0.0001f);
    ASSERT_FLOAT_EQ(-1.0f, m[8], 0.0001f);
    ASSERT_POSITION(&f, turned, 1.0f, 2.0f, 3.0f);
    /* Twice times two along X */
    world_matrix(&f, scaled, m);
    ASSERT_FLOAT_EQ(4.0f, m[0], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, m[5], 0.0001f);
    /* The angles' axes: Z turned to X */
    world_matrix(&f, oriented, m);
    ASSERT_FLOAT_EQ(1.0f, m[8], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, m[5], 0.0001f);
    /* Looking toward -X: the Z axis points there */
    world_matrix(&f, looking, m);
    ASSERT_FLOAT_EQ(-1.0f, m[8], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, m[9], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, m[5], 0.0001f);
    fixture_end(&f);
}

/* The `pos`th local parameter (or setting) of a block, set in the engine to `value` */
static void set_local_int(engine_fixture_t *f, nmo_object_id_t block, size_t pos, int32_t value)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    ASSERT_TRUE(state != NULL && pos < state->local_parameters.count);
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f->engine, nmo_behavior_ref_array_get_id(&state->local_parameters, pos),
                          &value, sizeof(value)));
}

static void move_to(engine_fixture_t *f, nmo_object_id_t id, float x)
{
    float m[16];
    world_matrix(f, id, m);
    m[12] = x;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f->engine, id, m, false));
}

TEST(engine_transform, nearest_in_group_and_proximity)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t entities[3];
    const float xs[3] = {1.0f, 5.0f, 9.0f};
    for (int i = 0; i < 3; i++) {
        entities[i] = fixture_create(&f, NMO_CID_3DENTITY, "Member");
        ASSERT_TRUE(place_entity(&f, entities[i], xs[i], 0.0f, 0.0f, 0) != 0);
    }
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Group");
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_NOT_NULL(state);
    nmo_ref_t *members = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&state->object_ids, 3u, (void **)&members));
    for (int i = 0; i < 3; i++) {
        members[i] = nmo_ref_from_id(entities[i]);
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t nearest = fixture_block(&f, GUID_NEAREST_IN_GROUP, "Nearest");
    nmo_object_id_t near = fixture_block(&f, GUID_SCALEABLE_PROXIMITY, "Near");
    ASSERT_TRUE(nearest != 0 && near != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, nearest, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, near, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, nearest, 0), (int32_t)group);
    const float point[3] = {6.0f, 0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, nearest, 1), point, sizeof(point));
    /* Within 3 of each other, checked every frame */
    const float three = 3.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, near, 0), &three, sizeof(three));
    fixture_input_int(&f, BLOCK_PIN(&f, near, 1), (int32_t)entities[0]);
    fixture_input_int(&f, BLOCK_PIN(&f, near, 2), (int32_t)entities[1]);
    fixture_input_int(&f, BLOCK_PIN(&f, near, 6), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, near, 7), 1);
    /* In Range, Out Range, Enter Range, Exit Range */
    nmo_object_id_t outputs[4];
    for (int i = 0; i < 4; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        ASSERT_TRUE(outputs[i] != 0);
        fixture_link(&f, BLOCK_OUT(&f, near, (size_t)i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_commit(&f, NULL);
    set_local_int(&f, near, 2, 15); /* every output */
    set_local_int(&f, near, 3, 7);  /* X, Y and Z */
    set_local_int(&f, near, 4, 1);  /* squared */

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    uint32_t found = 0;
    float distance = 0.0f;
    read_value(&f, BLOCK_POUT(&f, nearest, 0), &found, sizeof(found));
    read_value(&f, BLOCK_POUT(&f, nearest, 1), &distance, sizeof(distance));
    ASSERT_EQ(entities[1], found);
    ASSERT_FLOAT_EQ(1.0f, distance, 0.0001f);
    /* 4 apart: the first check exits (In starts neither in nor out), then stays out */
    ASSERT_EQ(1u, runs(&f, outputs[3]));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    move_to(&f, entities[1], 2.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outputs[0]));
    float squared = 0.0f;
    read_value(&f, BLOCK_POUT(&f, near, 0), &squared, sizeof(squared));
    ASSERT_FLOAT_EQ(1.0f, squared, 0.0001f);
    fixture_end(&f);
}

TEST(engine_transform, set_dynamic_position_follows_the_object)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t follower = fixture_create(&f, NMO_CID_3DENTITY, "Follower");
    nmo_object_id_t leader = fixture_create(&f, NMO_CID_3DENTITY, "Leader");
    ASSERT_TRUE(place_entity(&f, follower, 0.0f, 0.0f, 0.0f, 0) != 0);
    ASSERT_TRUE(place_entity(&f, leader, 10.0f, 0.0f, 0.0f, 0) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t follow =
        target_block(&f, GUID_SET_DYNAMIC_POSITION, "Follow", follower, f.script_in);
    ASSERT_TRUE(follow != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, follow, 0), (int32_t)leader);
    const float half = 0.5f;
    for (int axis = 1; axis <= 3; axis++) {
        fixture_input_value(&f, BLOCK_PIN(&f, follow, (size_t)axis), &half, sizeof(half));
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f)); /* On: where it starts */
    ASSERT_POSITION(&f, follower, 0.0f, 0.0f, 0.0f);
    /* Then half the way each second (the force times the seconds) */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_POSITION(&f, follower, 5.0f, 0.0f, 0.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f));
    ASSERT_POSITION(&f, follower, 7.5f, 0.0f, 0.0f);
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, follow));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_transform, loaded_children_keep_their_place_under_the_parent);
REGISTER_TEST(engine_transform, set_position_and_set_parent);
REGISTER_TEST(engine_transform, rotate_scale_and_orientation);
REGISTER_TEST(engine_transform, nearest_in_group_and_proximity);
REGISTER_TEST(engine_transform, set_dynamic_position_follows_the_object);
TEST_MAIN_END()
