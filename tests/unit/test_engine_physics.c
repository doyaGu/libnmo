#include "engine_fixture.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_edit.h"
#include "type/nmo_type_guids.h"
#include "object/nmo_ref.h"

#include <math.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_PHYSICALIZE NMO_GUID(0x7522370e, 0x37ec15ec)
#define GUID_IMPULSE NMO_GUID(0x0c7e39bb, 0x16db20d5)
#define GUID_GLOBALS NMO_GUID(0x72af347c, 0x03da71e1)
#define GUID_HINGE NMO_GUID(0x41cd3653, 0x0de60c1d)
#define GUID_FORCE NMO_GUID(0x56e20c57, 0x0b926068)
#define GUID_COLL_DETECTION NMO_GUID(0x7435488d, 0x201d1188)
#define GUID_CONTINUOUS_CONTACT NMO_GUID(0x199e4cf1, 0x545a78fe)
#define GUID_DELETE_SURFACES NMO_GUID(0x53bf75aa, 0x770c7021)
#define GUID_PROFILER_VALUES NMO_GUID(0x1c8e61d1, 0x32723c6f)
#define GUID_BUOYANCY NMO_GUID(0x2c015f2b, 0x5b147512)
#define GUID_RESET NMO_GUID(0x5714345e, 0x792262ae)

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
    int objects, destroyed, constraints, constraints_destroyed, pushes, wakes, surfaces_deleted;
    nmo_engine_physics_constraint_t last_constraint;
    void *last_object1, *last_object2;
    float gravity[3], push_at[3], push[3];
    float steps[8];
    int step_count;
    nmo_object_id_t mover; /* moved 1 along X each step */
    int handles[16];
    int buoyancies, resets;
    void *buoyant;
    nmo_engine_physics_buoyancy_t last_buoyancy;
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

static void rec_delete_surfaces(void *user_data)
{
    ((recorder_t *)user_data)->surfaces_deleted++;
}

static nmo_status_t rec_add_buoyancy(void *user_data, void *object,
                                     const nmo_engine_physics_buoyancy_t *buoyancy)
{
    recorder_t *r = (recorder_t *)user_data;
    r->buoyancies++;
    r->buoyant = object;
    r->last_buoyancy = *buoyancy;
    return NMO_OK;
}

static void rec_reset(void *user_data)
{
    ((recorder_t *)user_data)->resets++;
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

/* A provider with a clock the test sets, recording what listens */
typedef struct listening {
    double now;
    int listens, unlistens;
} listening_t;

static void lis_listen(void *user_data, void *object, bool on)
{
    listening_t *l = (listening_t *)user_data;
    (void)object;
    if (on) {
        l->listens++;
    } else {
        l->unlistens++;
    }
}

static double lis_time(void *user_data)
{
    return ((listening_t *)user_data)->now;
}

static nmo_status_t lis_create(void *user_data, const nmo_engine_physics_object_t *object, void **out)
{
    (void)object;
    *out = user_data;
    return NMO_OK;
}

static void set_listening(engine_fixture_t *f, listening_t *l)
{
    nmo_engine_physics_provider_t provider = {
        .user_data = l, .create_object = lis_create, .listen = lis_listen, .time = lis_time};
    nmo_engine_set_physics_provider(f->engine, &provider);
}

static nmo_engine_physics_collision_t collision(nmo_object_id_t object, nmo_object_id_t other,
                                                float speed)
{
    nmo_engine_physics_collision_t c = {
        .object = object, .other = other, .object_first = false,
        .speed = {0.0f, speed, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .point = {1.0f, 2.0f, 3.0f}};
    return c;
}

TEST(engine_physics, coll_detection_reports_collisions)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t ball = place(&f, "Ball", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t floor = place(&f, "Floor", 0.0f, -1.0f, 0.0f);
    ASSERT_TRUE(ball != 0 && floor != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    ASSERT_TRUE(block_of(&f, GUID_PHYSICALIZE, "Physicalize", ball, 0, 0) != 0);
    nmo_object_id_t detect = block_of(&f, GUID_COLL_DETECTION, "Detect", ball, 0, 0);
    nmo_object_id_t hit = fixture_block(&f, GUID_NOP, "Hit");
    ASSERT_TRUE(detect != 0 && hit != 0);
    fixture_link(&f, BLOCK_OUT(&f, detect, 0), BLOCK_IN(&f, hit, 0), 0);
    input_float(&f, BLOCK_PIN(&f, detect, 0), 1.0f);  /* Min Speed */
    input_float(&f, BLOCK_PIN(&f, detect, 1), 10.0f); /* Max Speed */
    input_float(&f, BLOCK_PIN(&f, detect, 2), 0.5f);  /* Sleep afterwards */
    fixture_commit(&f, NULL);
    listening_t l = {0};
    set_listening(&f, &l);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, l.listens);
    /* Too slow, then 5 m/s: the floor, half the speed range, the normal from the ball, z 0 */
    l.now = 1.0;
    nmo_engine_physics_collision_t c = collision(ball, floor, 0.5f);
    nmo_engine_physics_collision(f.engine, &c);
    c = collision(ball, floor, 5.0f);
    nmo_engine_physics_collision(f.engine, &c);
    uint32_t other = 0;
    float speed = 0.0f, normal[3], position[3];
    read_value(&f, BLOCK_POUT(&f, detect, 0), &other, sizeof(other));
    read_value(&f, BLOCK_POUT(&f, detect, 1), &speed, sizeof(speed));
    read_value(&f, BLOCK_POUT(&f, detect, 2), normal, sizeof(normal));
    read_value(&f, BLOCK_POUT(&f, detect, 3), position, sizeof(position));
    ASSERT_EQ(floor, other);
    ASSERT_FLOAT_EQ(0.5f, speed, 0.0001f);
    ASSERT_FLOAT_EQ(-1.0f, normal[1], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, position[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, position[2], 0.0001f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, hit));
    /* Within Sleep afterwards: not heard; after it: heard again */
    l.now = 1.3;
    c = collision(ball, floor, 20.0f);
    nmo_engine_physics_collision(f.engine, &c);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, hit));
    l.now = 1.6;
    nmo_engine_physics_collision(f.engine, &c);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(2u, runs(&f, hit));
    read_value(&f, BLOCK_POUT(&f, detect, 1), &speed, sizeof(speed));
    ASSERT_FLOAT_EQ(1.0f, speed, 0.0001f); /* above Max Speed */
    fixture_end(&f);
}

/* An integer attribute type named `name` given to `object` with `value` */
static bool int_attribute(engine_fixture_t *f, const char *name, nmo_object_id_t object, int32_t value)
{
    nmo_manager_entry_create_options_t create = {
        .enabled = true, .attribute_type_guid = CKPGUID_INT, .category = "Test",
        .has_compatible_class_id = true, .compatible_class_id = NMO_CID_BEOBJECT,
        .has_flags = true, .flags = 0u};
    uint32_t type = UINT32_MAX;
    if (nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx), name,
                                                       &create, &type) != NMO_OK) {
        return false;
    }
    nmo_object_id_t param = fixture_create(f, NMO_CID_PARAMETEROUT, "Attribute Value");
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, param);
    nmo_beobject_state_t *owner = (nmo_beobject_state_t *)fixture_state(f, object);
    if (state == NULL || owner == NULL ||
        nmo_array_alloc(&state->buffer_data, 1u, sizeof(value), NULL) != NMO_OK) {
        return false;
    }
    state->type_guid = CKPGUID_INT;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    memcpy(state->buffer_data.data, &value, sizeof(value));
    return nmo_beobject_attribute_array_append(&owner->attributes, param, type, NULL) == NMO_OK;
}

TEST(engine_physics, continuous_contact_waits_its_delays)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t ball = place(&f, "Ball", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t rail = place(&f, "Rail", 0.0f, -1.0f, 0.0f);
    ASSERT_TRUE(ball != 0 && rail != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    ASSERT_TRUE(int_attribute(&f, "Continuous Contact ID", rail, 2)); /* group 2 */
    ASSERT_TRUE(block_of(&f, GUID_PHYSICALIZE, "Physicalize", ball, 0, 0) != 0);
    nmo_object_id_t contact = block_of(&f, GUID_CONTINUOUS_CONTACT, "Contact", ball, 0, 0);
    nmo_object_id_t on = fixture_block(&f, GUID_NOP, "On 2");
    nmo_object_id_t off = fixture_block(&f, GUID_NOP, "Off 2");
    ASSERT_TRUE(contact != 0 && on != 0 && off != 0);
    fixture_link(&f, BLOCK_OUT(&f, contact, 2), BLOCK_IN(&f, on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, contact, 3), BLOCK_IN(&f, off, 0), 0);
    fixture_commit(&f, NULL);
    /* Local 0 is "Number Group Output" in the DLL and the files (the prototype data lists the
     * setting after the handle) */
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, contact);
    ASSERT_TRUE(state != NULL && state->local_parameters.count > 0);
    const int32_t groups = 5;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&state->local_parameters, 0),
                          &groups, sizeof(groups)));
    listening_t l = {0};
    set_listening(&f, &l);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, l.listens);
    /* On once Time Delay Start (0.1 s) has passed since the contact started */
    l.now = 1.0;
    nmo_engine_physics_contact(f.engine, ball, rail, true);
    l.now = 1.05;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(0u, runs(&f, on));
    l.now = 1.2;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, on));
    /* Off once Time Delay End has passed since it ended; a contact without the ID counts not */
    nmo_engine_physics_contact(f.engine, ball, ball, true);
    l.now = 2.0;
    nmo_engine_physics_contact(f.engine, ball, rail, false);
    l.now = 2.05;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(0u, runs(&f, off));
    l.now = 2.2;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, off));
    ASSERT_EQ(1u, runs(&f, on));
    fixture_end(&f);
}

TEST(engine_physics, collision_surfaces_go_once_nothing_is_physical)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = place(&f, "A", 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(a != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* Frame 0: A physical, the surfaces asked to go, the count; frame 2: A not physical */
    nmo_object_id_t physicalize = block_of(&f, GUID_PHYSICALIZE, "Physicalize", a, 0, 0);
    nmo_object_id_t unphysicalize = block_of(&f, GUID_PHYSICALIZE, "Unphysicalize", a, 1, 2);
    nmo_object_id_t delete_surfaces = fixture_block(&f, GUID_DELETE_SURFACES, "Delete");
    nmo_object_id_t during = fixture_block(&f, GUID_PROFILER_VALUES, "During");
    nmo_object_id_t after = fixture_block(&f, GUID_PROFILER_VALUES, "After");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(physicalize != 0 && unphysicalize != 0 && delete_surfaces != 0 && during != 0 &&
                after != 0 && done != 0);
    fixture_link(&f, BLOCK_OUT(&f, physicalize, 0), BLOCK_IN(&f, delete_surfaces, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, delete_surfaces, 0), BLOCK_IN(&f, during, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, after, 0), 3);
    fixture_link(&f, BLOCK_OUT(&f, after, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_commit(&f, NULL);
    recorder_t r;
    memset(&r, 0, sizeof(r));
    nmo_engine_physics_provider_t provider = {
        .user_data = &r,
        .create_object = rec_create_object,
        .destroy_object = rec_destroy_object,
        .delete_surfaces = rec_delete_surfaces,
    };
    nmo_engine_set_physics_provider(f.engine, &provider);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Out at once; the call waits while A is physical */
    int32_t count = -1;
    read_value(&f, BLOCK_POUT(&f, during, 0), &count, sizeof(count));
    ASSERT_EQ(1, count);
    ASSERT_EQ(0, r.surfaces_deleted);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(0, r.surfaces_deleted);
    /* Unphysicalized in frame 2's behaviors: the call goes through before its step */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, r.destroyed);
    ASSERT_EQ(1, r.surfaces_deleted);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, r.surfaces_deleted);
    ASSERT_EQ(1u, runs(&f, done));
    read_value(&f, BLOCK_POUT(&f, after, 0), &count, sizeof(count));
    ASSERT_EQ(0, count);
    fixture_end(&f);
}

TEST(engine_physics, buoyancy_waits_for_its_object_and_reset_forgets_it)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = place(&f, "A", 0.0f, 5.0f, 0.0f);
    nmo_object_id_t water = place(&f, "Water", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t p1 = place(&f, "P1", 0.0f, 1.0f, 0.0f);
    nmo_object_id_t p2 = place(&f, "P2", 1.0f, 1.0f, 0.0f);
    nmo_object_id_t p3 = place(&f, "P3", 0.0f, 1.0f, 1.0f);
    nmo_object_id_t start = place(&f, "Start", 0.0f, 0.0f, 0.0f);
    nmo_object_id_t end = place(&f, "End", 2.0f, 0.0f, 0.0f);
    ASSERT_TRUE(a != 0 && water != 0 && p1 != 0 && p2 != 0 && p3 != 0 && start != 0 && end != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* Frame 0: globals, A physical with a force, the water's buoyancy (it is not physical yet)
       and one without a current end; frame 1: the water physical; frame 2: Physics Reset */
    nmo_object_id_t globals = fixture_block(&f, GUID_GLOBALS, "Globals");
    ASSERT_TRUE(globals != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, globals, 0), 0);
    input_vector(&f, BLOCK_PIN(&f, globals, 0), 0.0f, -20.0f, 0.0f);
    input_float(&f, BLOCK_PIN(&f, globals, 1), 2.0f);
    nmo_object_id_t physicalize_a = block_of(&f, GUID_PHYSICALIZE, "Physicalize A", a, 0, 0);
    nmo_object_id_t force = block_of(&f, GUID_FORCE, "Force", a, 0, 0);
    nmo_object_id_t buoyancy = block_of(&f, GUID_BUOYANCY, "Buoyancy", water, 0, 0);
    nmo_object_id_t partial = block_of(&f, GUID_BUOYANCY, "Partial", a, 0, 0);
    nmo_object_id_t physicalize_w = block_of(&f, GUID_PHYSICALIZE, "Physicalize Water", water, 0, 1);
    nmo_object_id_t reset = fixture_block(&f, GUID_RESET, "Reset");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    nmo_object_id_t floated = fixture_block(&f, GUID_NOP, "Floated");
    ASSERT_TRUE(physicalize_a != 0 && force != 0 && buoyancy != 0 && partial != 0 &&
                physicalize_w != 0 && reset != 0 && done != 0 && floated != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, reset, 0), 2);
    fixture_link(&f, BLOCK_OUT(&f, reset, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, buoyancy, 0), BLOCK_IN(&f, floated, 0), 0);
    input_vector(&f, BLOCK_PIN(&f, force, 2), 0.0f, 3.0f, 0.0f);
    const nmo_object_id_t points[5] = {p1, p2, p3, start, end};
    for (size_t i = 0; i < 5; i++) {
        fixture_input_int(&f, BLOCK_PIN(&f, buoyancy, i), (int32_t)points[i]);
        if (i < 4) {
            fixture_input_int(&f, BLOCK_PIN(&f, partial, i), (int32_t)points[i]);
        }
    }
    input_float(&f, BLOCK_PIN(&f, buoyancy, 5), 3.0f);  /* current strength */
    input_float(&f, BLOCK_PIN(&f, buoyancy, 6), 500.0f); /* density */
    input_float(&f, BLOCK_PIN(&f, buoyancy, 10), 0.5f); /* airplane like factor */
    fixture_commit(&f, NULL);
    recorder_t r;
    memset(&r, 0, sizeof(r));
    nmo_engine_physics_provider_t provider = {
        .user_data = &r,
        .create_object = rec_create_object,
        .destroy_object = rec_destroy_object,
        .create_constraint = rec_create_constraint,
        .destroy_constraint = rec_destroy_constraint,
        .set_gravity = rec_set_gravity,
        .reset = rec_reset,
        .simulate = rec_simulate,
        .add_buoyancy = rec_add_buoyancy,
    };
    nmo_engine_set_physics_provider(f.engine, &provider);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Out at once; the call waits for the water, the partial one is over */
    ASSERT_EQ(1u, runs(&f, floated));
    ASSERT_EQ(0, r.buoyancies);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1, r.buoyancies);
    ASSERT_EQ(2, r.objects);
    ASSERT_TRUE(r.buoyant == &r.handles[1]);
    ASSERT_EQ(water, r.last_buoyancy.object);
    ASSERT_FLOAT_EQ(1.0f, r.last_buoyancy.surface[1][0], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, r.last_buoyancy.surface[2][2], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, r.last_buoyancy.current[0], 0.0001f); /* toward End, at its strength */
    ASSERT_FLOAT_EQ(500.0f, r.last_buoyancy.density, 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, r.last_buoyancy.damp_factor, 0.0001f);
    ASSERT_FLOAT_EQ(0.05f, r.last_buoyancy.friction_factor, 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, r.last_buoyancy.torque_factor, 0.0001f);
    ASSERT_FLOAT_EQ(0.1f, r.last_buoyancy.suction_factor, 0.0001f);
    ASSERT_FLOAT_EQ(0.0175f, r.steps[1], 0.00001f); /* (20 + 3 * 5) / 4 * 0.002 */

    /* The reset: nothing physical, the force gone, gravity and time factor back */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, done));
    ASSERT_EQ(1, r.resets);
    ASSERT_EQ(0, nmo_engine_physics_object_count(f.engine));
    ASSERT_FALSE(nmo_engine_physics_object(f.engine, a, NULL));
    ASSERT_EQ(2, r.destroyed);
    ASSERT_EQ(1, r.constraints_destroyed);
    float gravity[3];
    nmo_engine_physics_gravity(f.engine, gravity);
    ASSERT_FLOAT_EQ(-9.81f, gravity[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.0115625f, r.steps[2], 0.00001f); /* (20 + 3 * 8.75) / 4 * 0.001 */
    ASSERT_EQ(1, r.buoyancies);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_physics, blocks_feed_the_provider);
REGISTER_TEST(engine_physics, polygon_shapes_need_a_mesh_or_their_name);
REGISTER_TEST(engine_physics, coll_detection_reports_collisions);
REGISTER_TEST(engine_physics, continuous_contact_waits_its_delays);
REGISTER_TEST(engine_physics, collision_surfaces_go_once_nothing_is_physical);
REGISTER_TEST(engine_physics, buoyancy_waits_for_its_object_and_reset_forgets_it);
TEST_MAIN_END()
