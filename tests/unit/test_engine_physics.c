#include "engine_fixture.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_ref.h"

#include <math.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_PHYSICALIZE NMO_GUID(0x7522370e, 0x37ec15ec)
#define GUID_IMPULSE NMO_GUID(0x0c7e39bb, 0x16db20d5)
#define GUID_GLOBALS NMO_GUID(0x72af347c, 0x03da71e1)
#define GUID_HINGE NMO_GUID(0x41cd3653, 0x0de60c1d)
#define GUID_FORCE NMO_GUID(0x56e20c57, 0x0b926068)

/* An entity saved at `x, y, z` with unit axes */
static nmo_object_id_t place(engine_fixture_t *f, const char *name, float x, float y, float z)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DOBJECT, name);
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
    return id;
}

/* A block of `target`, run `delay` frames after the script starts */
static nmo_object_id_t block_of(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                nmo_object_id_t target, int input, uint32_t delay)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block == 0 || fixture_target(f, block) == 0) {
        return 0;
    }
    fixture_input_int(f, fixture_target(f, block), (int32_t)target);
    fixture_link(f, f->script_in, BLOCK_IN(f, block, (size_t)input), delay);
    return block;
}

static void input_vector(engine_fixture_t *f, nmo_object_id_t pin, float x, float y, float z)
{
    const float v[3] = {x, y, z};
    fixture_input_value(f, pin, v, sizeof(v));
}

static void input_float(engine_fixture_t *f, nmo_object_id_t pin, float value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

/* A provider recording what the engine gives it */
typedef struct recorder {
    nmo_engine_t *engine;
    int objects, destroyed, constraints, constraints_destroyed, pushes, wakes;
    nmo_engine_physics_constraint_t last_constraint;
    void *last_object1, *last_object2;
    float gravity[3], push_at[3], push[3];
    float steps[8];
    int step_count;
    nmo_object_id_t mover; /* moved 1 along X each step */
    int handles[16];
} recorder_t;

static nmo_status_t rec_create_object(void *user_data, const nmo_engine_physics_object_t *object,
                                      void **out_handle)
{
    recorder_t *r = (recorder_t *)user_data;
    (void)object;
    *out_handle = &r->handles[r->objects++ % 16];
    return NMO_OK;
}

static void rec_destroy_object(void *user_data, void *handle)
{
    (void)handle;
    ((recorder_t *)user_data)->destroyed++;
}

static nmo_status_t rec_create_constraint(void *user_data, const nmo_engine_physics_constraint_t *c,
                                          void *object1, void *object2, void **out_handle)
{
    recorder_t *r = (recorder_t *)user_data;
    r->constraints++;
    r->last_constraint = *c;
    r->last_object1 = object1;
    r->last_object2 = object2;
    *out_handle = r;
    return NMO_OK;
}

static void rec_destroy_constraint(void *user_data, void *handle)
{
    (void)handle;
    ((recorder_t *)user_data)->constraints_destroyed++;
}

static void rec_push(void *user_data, void *object, const float position[3], const float impulse[3])
{
    recorder_t *r = (recorder_t *)user_data;
    (void)object;
    r->pushes++;
    memcpy(r->push_at, position, sizeof(r->push_at));
    memcpy(r->push, impulse, sizeof(r->push));
}

static void rec_wake_up(void *user_data, void *object)
{
    (void)object;
    ((recorder_t *)user_data)->wakes++;
}

static void rec_set_gravity(void *user_data, const float gravity[3])
{
    memcpy(((recorder_t *)user_data)->gravity, gravity, 3u * sizeof(float));
}

static void rec_simulate(void *user_data, nmo_engine_t *engine, float seconds)
{
    recorder_t *r = (recorder_t *)user_data;
    if (r->step_count < 8) {
        r->steps[r->step_count] = seconds;
    }
    r->step_count++;
    float m[16];
    if (nmo_engine_physics_object(engine, r->mover, NULL) &&
        nmo_engine_entity_world_matrix(engine, r->mover, m)) {
        m[12] += 1.0f;
        nmo_engine_set_entity_world_matrix(engine, r->mover, m, false);
    }
}

TEST(engine_physics, blocks_feed_the_provider)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = place(&f, "A", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t b = place(&f, "B", 5.0f, 0.0f, 0.0f);
    nmo_object_id_t joint = place(&f, "Joint", 1.0f, 2.0f, 3.0f);
    ASSERT_TRUE(a != 0 && b != 0 && joint != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* Frame 0: gravity and time factor, A physical (a ball), a force on it, a hinge to B */
    nmo_object_id_t globals = fixture_block(&f, GUID_GLOBALS, "Globals");
    ASSERT_TRUE(globals != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, globals, 0), 0);
    input_vector(&f, BLOCK_PIN(&f, globals, 0), 0.0f, -20.0f, 0.0f);
    input_float(&f, BLOCK_PIN(&f, globals, 1), 2.0f);
    nmo_object_id_t physicalize_a = block_of(&f, GUID_PHYSICALIZE, "Physicalize A", a, 0, 0);
    nmo_object_id_t again = block_of(&f, GUID_PHYSICALIZE, "Again", a, 0, 0);
    nmo_object_id_t force = block_of(&f, GUID_FORCE, "Force", a, 0, 0);
    nmo_object_id_t hinge = block_of(&f, GUID_HINGE, "Hinge", a, 0, 0);
    /* Frame 1: B physical, then a push on A; frame 2: A not physical, the hinge shut down */
    nmo_object_id_t physicalize_b = block_of(&f, GUID_PHYSICALIZE, "Physicalize B", b, 0, 1);
    nmo_object_id_t impulse = block_of(&f, GUID_IMPULSE, "Impulse", a, 0, 1);
    nmo_object_id_t unphysicalize = block_of(&f, GUID_PHYSICALIZE, "Unphysicalize", a, 1, 2);
    ASSERT_TRUE(physicalize_a != 0 && again != 0 && force != 0 && hinge != 0 && physicalize_b != 0 &&
                impulse != 0 && unphysicalize != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, hinge, 1), 2); /* its Shutdown */
    nmo_object_id_t out1 = fixture_block(&f, GUID_NOP, "Out1");
    nmo_object_id_t out2 = fixture_block(&f, GUID_NOP, "Out2");
    ASSERT_TRUE(out1 != 0 && out2 != 0);
    fixture_link(&f, BLOCK_OUT(&f, again, 0), BLOCK_IN(&f, out1, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, unphysicalize, 1), BLOCK_IN(&f, out2, 0), 0);
    input_float(&f, BLOCK_PIN(&f, physicalize_a, 1), 0.6f); /* Friction */
    input_float(&f, BLOCK_PIN(&f, physicalize_a, 3), 2.0f); /* Mass */
    input_vector(&f, BLOCK_PIN(&f, force, 0), 1.0f, 0.0f, 0.0f);
    fixture_input_int(&f, BLOCK_PIN(&f, force, 1), (int32_t)a); /* A's own space */
    input_vector(&f, BLOCK_PIN(&f, force, 2), 0.0f, 3.0f, 0.0f);
    input_float(&f, BLOCK_PIN(&f, force, 4), 5.0f);
    fixture_input_int(&f, BLOCK_PIN(&f, hinge, 0), (int32_t)b);
    fixture_input_int(&f, BLOCK_PIN(&f, hinge, 1), (int32_t)joint);
    fixture_input_int(&f, BLOCK_PIN(&f, hinge, 2), 1);
    input_float(&f, BLOCK_PIN(&f, hinge, 3), -90.0f);
    input_float(&f, BLOCK_PIN(&f, hinge, 4), 90.0f);
    input_vector(&f, BLOCK_PIN(&f, impulse, 0), 0.0f, 1.0f, 0.0f);
    input_vector(&f, BLOCK_PIN(&f, impulse, 2), 0.0f, 0.0f, 2.0f);
    fixture_commit(&f, NULL);
    recorder_t r;
    memset(&r, 0, sizeof(r));
    r.mover = a;
    nmo_engine_physics_provider_t provider = {
        .user_data = &r,
        .create_object = rec_create_object,
        .destroy_object = rec_destroy_object,
        .create_constraint = rec_create_constraint,
        .destroy_constraint = rec_destroy_constraint,
        .push = rec_push,
        .wake_up = rec_wake_up,
        .set_gravity = rec_set_gravity,
        .simulate = rec_simulate,
    };
    nmo_engine_set_physics_provider(f.engine, &provider);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* A physical once (again: Out1 only); a ball, its surface name empty, so no collision */
    nmo_engine_physics_object_t object;
    ASSERT_TRUE(nmo_engine_physics_object(f.engine, a, &object));
    ASSERT_EQ(1, r.objects);
    ASSERT_EQ(1u, runs(&f, out1));
    ASSERT_EQ(NMO_ENGINE_PHYSICS_BALL, object.shape);
    ASSERT_FALSE(object.enable_collision);
    ASSERT_FLOAT_EQ(0.6f, object.friction, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, object.mass, 0.0001f);
    float gravity[3];
    nmo_engine_physics_gravity(f.engine, gravity);
    ASSERT_FLOAT_EQ(-20.0f, gravity[1], 0.0001f);
    ASSERT_FLOAT_EQ(-20.0f, r.gravity[1], 0.0001f);
    /* The force now (A is physical), the hinge waits for B */
    ASSERT_EQ(1, nmo_engine_physics_constraint_count(f.engine));
    nmo_engine_physics_constraint_t c;
    ASSERT_TRUE(nmo_engine_physics_constraint(f.engine, 0, &c));
    ASSERT_EQ(NMO_ENGINE_PHYSICS_FORCE, c.kind);
    ASSERT_FLOAT_EQ(1.0f, c.anchor[0], 0.0001f);
    ASSERT_FLOAT_EQ(5.0f, c.axis[1], 0.0001f);
    /* The step: the frame time smoothed (20 / 4) times the time factor (2 / 1000), then A moved */
    ASSERT_EQ(1, r.step_count);
    ASSERT_FLOAT_EQ(0.01f, r.steps[0], 0.00001f);
    float m[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, a, m));
    ASSERT_FLOAT_EQ(1.0f, m[12], 0.0001f);

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* B physical: the hinge at the joint's place, about its Z axis, limited in radians */
    ASSERT_EQ(2, nmo_engine_physics_constraint_count(f.engine));
    ASSERT_TRUE(nmo_engine_physics_constraint(f.engine, 1, &c));
    ASSERT_EQ(NMO_ENGINE_PHYSICS_HINGE, c.kind);
    ASSERT_EQ(a, c.object1);
    ASSERT_EQ(b, c.object2);
    ASSERT_FLOAT_EQ(2.0f, c.anchor[1], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, c.axis[2], 0.0001f);
    ASSERT_TRUE(c.limited);
    ASSERT_FLOAT_EQ(1.5707963f, c.upper, 0.0001f);
    ASSERT_TRUE(r.last_object1 != NULL && r.last_object2 != NULL && r.last_object1 != r.last_object2);
    /* The push: 10 along Z (Direction normalized), at the point, A woken first */
    ASSERT_EQ(1, r.pushes);
    ASSERT_EQ(1, r.wakes);
    ASSERT_FLOAT_EQ(1.0f, r.push_at[1], 0.0001f);
    ASSERT_FLOAT_EQ(10.0f, r.push[2], 0.0001f);
    ASSERT_FLOAT_EQ(0.0175f, r.steps[1], 0.00001f); /* (20 + 3 * 5) / 4 * 0.002 */

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FALSE(nmo_engine_physics_object(f.engine, a, NULL));
    ASSERT_EQ(1, r.destroyed);
    ASSERT_EQ(1u, runs(&f, out2));
    ASSERT_TRUE(nmo_engine_physics_object(f.engine, b, NULL));
    ASSERT_EQ(1, r.constraints_destroyed); /* the hinge shut down */
    fixture_end(&f);
}

/* A mesh with a vertex, for a polygon shape */
static nmo_object_id_t small_mesh(engine_fixture_t *f)
{
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, "Mesh");
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (state == NULL) {
        return 0;
    }
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(nmo_session_get_arena(f->session),
                                                      sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    if (state->vertices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, sizeof(nmo_vertex_t));
    state->vertex_count = 1;
    return mesh;
}

TEST(engine_physics, polygon_shapes_need_a_mesh_or_their_name)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t floor = place(&f, "Floor", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t bare = place(&f, "Bare", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t named = place(&f, "Named", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t mesh = small_mesh(&f);
    ASSERT_TRUE(floor != 0 && bare != 0 && named != 0 && mesh != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* With a mesh: a polygon shape kept by its name; without: none ("incorrect mesh"),
     * unless the name was made before */
    nmo_object_id_t with_mesh = block_of(&f, GUID_PHYSICALIZE, "With Mesh", floor, 0, 0);
    nmo_object_id_t without = block_of(&f, GUID_PHYSICALIZE, "Without", bare, 0, 0);
    nmo_object_id_t by_name = block_of(&f, GUID_PHYSICALIZE, "By Name", named, 0, 1);
    ASSERT_TRUE(with_mesh != 0 && without != 0 && by_name != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, with_mesh, 0), 1); /* Fixed */
    fixture_input_value(&f, BLOCK_PIN(&f, with_mesh, 10), "Floors", sizeof("Floors"));
    fixture_input_int(&f, BLOCK_PIN(&f, with_mesh, 11), (int32_t)mesh);
    fixture_input_value(&f, BLOCK_PIN(&f, without, 10), "Other", sizeof("Other"));
    fixture_input_value(&f, BLOCK_PIN(&f, by_name, 10), "Floors", sizeof("Floors"));
    nmo_object_id_t out = fixture_block(&f, GUID_NOP, "Out");
    ASSERT_TRUE(out != 0);
    fixture_link(&f, BLOCK_OUT(&f, without, 0), BLOCK_IN(&f, out, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_physics_object_t object;
    ASSERT_TRUE(nmo_engine_physics_object(f.engine, floor, &object));
    ASSERT_EQ(NMO_ENGINE_PHYSICS_POLYGON, object.shape);
    ASSERT_TRUE(object.fixed);
    ASSERT_TRUE(object.enable_collision);
    ASSERT_STR_EQ("Floors", object.collision_surface);
    ASSERT_EQ(1, object.convex_count);
    ASSERT_EQ(mesh, object.convex[0]);
    ASSERT_FALSE(nmo_engine_physics_object(f.engine, bare, NULL));
    ASSERT_EQ(1u, runs(&f, out)); /* Out1 all the same */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_TRUE(nmo_engine_physics_object(f.engine, named, &object));
    ASSERT_EQ(NMO_ENGINE_PHYSICS_POLYGON, object.shape);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_physics, blocks_feed_the_provider);
REGISTER_TEST(engine_physics, polygon_shapes_need_a_mesh_or_their_name);
TEST_MAIN_END()
