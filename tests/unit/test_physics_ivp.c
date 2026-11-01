#include "engine_fixture.h"

#include "engine/nmo_physics_ivp.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_ref.h"

#define GUID_PHYSICALIZE NMO_GUID(0x7522370e, 0x37ec15ec)
#define GUID_IMPULSE NMO_GUID(0x0c7e39bb, 0x16db20d5)
#define GUID_BUOYANCY NMO_GUID(0x2c015f2b, 0x5b147512)
#define GUID_RESET NMO_GUID(0x5714345e, 0x792262ae)
#define GUID_COLL_DETECTION NMO_GUID(0x7435488d, 0x201d1188)
#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)

/* A 3D object at (x, y, z) whose mesh is a box of half sizes `hx, hy, hz` */
static nmo_object_id_t box(engine_fixture_t *f, const char *name, float x, float y, float z,
                           float hx, float hy, float hz)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DOBJECT, name);
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, id);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (entity == NULL || state == NULL) {
        return 0;
    }
    memset(entity->world_matrix, 0, sizeof(entity->world_matrix));
    entity->world_matrix[0] = entity->world_matrix[5] = entity->world_matrix[10] = 1.0f;
    entity->world_matrix[15] = 1.0f;
    entity->world_matrix[12] = x;
    entity->world_matrix[13] = y;
    entity->world_matrix[14] = z;
    entity->current_mesh = nmo_ref_from_id(mesh);
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(nmo_session_get_arena(f->session),
                                                      8u * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    if (state->vertices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, 8u * sizeof(nmo_vertex_t));
    for (int i = 0; i < 8; i++) {
        state->vertices[i].position = (nmo_vector_t){(i & 1) ? hx : -hx, (i & 2) ? hy : -hy,
                                                     (i & 4) ? hz : -hz};
    }
    state->vertex_count = 8;
    return id;
}

/* Physicalize `target` with its own mesh as its convex shape, kept as `surface` */
static nmo_object_id_t physicalize(engine_fixture_t *f, nmo_object_id_t target, bool fixed,
                                   const char *surface)
{
    nmo_object_id_t block = fixture_block(f, GUID_PHYSICALIZE, "Physicalize");
    if (block == 0 || fixture_target(f, block) == 0) {
        return 0;
    }
    fixture_input_int(f, fixture_target(f, block), (int32_t)target);
    fixture_link(f, f->script_in, BLOCK_IN(f, block, 0), 0);
    fixture_input_int(f, BLOCK_PIN(f, block, 0), fixed ? 1 : 0);
    fixture_input_value(f, BLOCK_PIN(f, block, 10), surface, strlen(surface) + 1u);
    const nmo_3dentity_state_t *entity = (const nmo_3dentity_state_t *)fixture_state(f, target);
    fixture_input_int(f, BLOCK_PIN(f, block, 11), (int32_t)nmo_ref_runtime_id(&entity->current_mesh));
    return block;
}

static float position(engine_fixture_t *f, nmo_object_id_t id, int axis)
{
    float m[16];
    return nmo_engine_entity_world_matrix(f->engine, id, m) ? m[12 + axis] : -1000.0f;
}

TEST(physics_ivp, a_crate_falls_onto_the_floor_and_is_pushed)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t floor = box(&f, "Floor", 0.0f, 0.0f, 0.0f, 10.0f, 0.5f, 10.0f);
    nmo_object_id_t crate = box(&f, "Crate", 0.0f, 5.0f, 0.0f, 0.5f, 0.5f, 0.5f);
    ASSERT_TRUE(floor != 0 && crate != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    ASSERT_TRUE(physicalize(&f, floor, true, "Floor") != 0);
    ASSERT_TRUE(physicalize(&f, crate, false, "Crate") != 0);
    /* A push along X once it has landed (frame 300) */
    nmo_object_id_t impulse = fixture_block(&f, GUID_IMPULSE, "Push");
    ASSERT_TRUE(impulse != 0 && fixture_target(&f, impulse) != 0);
    fixture_input_int(&f, fixture_target(&f, impulse), (int32_t)crate);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, impulse, 0), 300);
    const float direction[3] = {1.0f, 0.0f, 0.0f}, strength = 2.0f;
    fixture_input_int(&f, BLOCK_PIN(&f, impulse, 1), (int32_t)crate); /* at its own center */
    fixture_input_value(&f, BLOCK_PIN(&f, impulse, 2), direction, sizeof(direction));
    fixture_input_value(&f, BLOCK_PIN(&f, impulse, 4), &strength, sizeof(strength));
    fixture_commit(&f, NULL);
    nmo_physics_ivp_t *physics = NULL;
    ASSERT_EQ(NMO_OK, nmo_physics_ivp_attach(f.engine, &physics));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 300; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f / 60.0f));
    }
    /* On the floor's top (0.5), its half size above it (and IVP's collision gap), near where
     * it fell: the impact turns it a little before it settles, level */
    float m[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, crate, m));
    ASSERT_FLOAT_EQ(1.0f, m[13], 0.05f);
    ASSERT_FLOAT_EQ(0.0f, m[12], 0.3f);
    ASSERT_FLOAT_EQ(1.0f, m[0], 0.001f);
    ASSERT_FLOAT_EQ(0.0f, position(&f, floor, 1), 0.0001f); /* fixed */
    float landed = m[12];
    for (int frame = 0; frame < 60; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f / 60.0f));
    }
    ASSERT_TRUE(position(&f, crate, 0) > landed + 0.2f); /* 2 along X, against the friction */
    ASSERT_FLOAT_EQ(1.0f, position(&f, crate, 1), 0.05f);
    nmo_physics_ivp_detach(physics);
    fixture_end(&f);
}

TEST(physics_ivp, a_light_crate_floats_in_a_buoyancy_pool)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    /* The pool's water below y = 0; the surface's points ordered so its normal points down */
    nmo_object_id_t pool = box(&f, "Pool", 0.0f, -5.0f, 0.0f, 10.0f, 5.0f, 10.0f);
    nmo_object_id_t crate = box(&f, "Crate", 0.0f, 3.0f, 0.0f, 0.5f, 0.5f, 0.5f);
    nmo_object_id_t p0 = box(&f, "P0", 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
    nmo_object_id_t p1 = box(&f, "P1", 0.0f, 0.0f, 1.0f, 0.1f, 0.1f, 0.1f);
    nmo_object_id_t p2 = box(&f, "P2", 1.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
    ASSERT_TRUE(pool != 0 && crate != 0 && p0 != 0 && p1 != 0 && p2 != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    ASSERT_TRUE(physicalize(&f, pool, true, "Pool") != 0);
    nmo_object_id_t crate_block = physicalize(&f, crate, false, "Crate");
    ASSERT_TRUE(crate_block != 0);
    const float mass = 100.0f; /* a tenth of the water it can push away */
    fixture_input_value(&f, BLOCK_PIN(&f, crate_block, 3), &mass, sizeof(mass));
    nmo_object_id_t buoyancy = fixture_block(&f, GUID_BUOYANCY, "Buoyancy");
    ASSERT_TRUE(buoyancy != 0 && fixture_target(&f, buoyancy) != 0);
    fixture_input_int(&f, fixture_target(&f, buoyancy), (int32_t)pool);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, buoyancy, 0), 0);
    const nmo_object_id_t points[5] = {p0, p1, p2, p0, p2};
    for (size_t i = 0; i < 5; i++) {
        fixture_input_int(&f, BLOCK_PIN(&f, buoyancy, i), (int32_t)points[i]);
    }
    const float still = 0.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, buoyancy, 5), &still, sizeof(still));
    /* Frame 600: the crate leaves the physics while in the water; frame 610: Physics Reset with
       the pool's buoyancy in place */
    nmo_object_id_t unphysicalize = fixture_block(&f, GUID_PHYSICALIZE, "Unphysicalize");
    nmo_object_id_t reset = fixture_block(&f, GUID_RESET, "Reset");
    ASSERT_TRUE(unphysicalize != 0 && fixture_target(&f, unphysicalize) != 0 && reset != 0);
    fixture_input_int(&f, fixture_target(&f, unphysicalize), (int32_t)crate);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, unphysicalize, 1), 600);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, reset, 0), 610);
    fixture_commit(&f, NULL);
    nmo_physics_ivp_t *physics = NULL;
    ASSERT_EQ(NMO_OK, nmo_physics_ivp_attach(f.engine, &physics));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    float lowest = 3.0f;
    for (int frame = 0; frame < 600; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f / 60.0f));
        float y = position(&f, crate, 1);
        lowest = y < lowest ? y : lowest;
    }
    /* It went into the water (the pool is a phantom), then came up: about a tenth under */
    ASSERT_TRUE(lowest < 0.4f);
    ASSERT_FLOAT_EQ(0.4f, position(&f, crate, 1), 0.15f);
    ASSERT_FLOAT_EQ(-5.0f, position(&f, pool, 1), 0.0001f);
    for (int frame = 0; frame < 20; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f / 60.0f));
    }
    ASSERT_FALSE(nmo_engine_physics_object(f.engine, crate, NULL));
    ASSERT_EQ(0, nmo_engine_physics_object_count(f.engine));
    nmo_physics_ivp_detach(physics);
    fixture_end(&f);
}

/* A landing is heard after the impact, with its speed (event_post_collision): the hit sounds'
   PhysicsCollDetection blocks need it over their Min Speed */
TEST(physics_ivp, a_landing_is_heard_with_its_speed)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t floor = box(&f, "Floor", 0.0f, 0.0f, 0.0f, 10.0f, 0.5f, 10.0f);
    nmo_object_id_t crate = box(&f, "Crate", 0.0f, 5.0f, 0.0f, 0.5f, 0.5f, 0.5f);
    ASSERT_TRUE(floor != 0 && crate != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    ASSERT_TRUE(physicalize(&f, floor, true, "Floor") != 0);
    ASSERT_TRUE(physicalize(&f, crate, false, "Crate") != 0);
    nmo_object_id_t detect = fixture_block(&f, GUID_COLL_DETECTION, "Detect");
    nmo_object_id_t hit = fixture_block(&f, GUID_NOP, "Hit");
    ASSERT_TRUE(detect != 0 && hit != 0 && fixture_target(&f, detect) != 0);
    fixture_input_int(&f, fixture_target(&f, detect), (int32_t)crate);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, detect, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, detect, 0), BLOCK_IN(&f, hit, 0), 0);
    const float min_speed = 1.0f, max_speed = 20.0f, sleep = 0.1f;
    fixture_input_value(&f, BLOCK_PIN(&f, detect, 0), &min_speed, sizeof(min_speed));
    fixture_input_value(&f, BLOCK_PIN(&f, detect, 1), &max_speed, sizeof(max_speed));
    fixture_input_value(&f, BLOCK_PIN(&f, detect, 2), &sleep, sizeof(sleep));
    fixture_commit(&f, NULL);
    nmo_physics_ivp_t *physics = NULL;
    ASSERT_EQ(NMO_OK, nmo_physics_ivp_attach(f.engine, &physics));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 120 && runs(&f, hit) == 0; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1000.0f / 60.0f));
    }
    /* The first impact: falling about 4 under gravity 9.81, about 8.5 m/s, 0.43 of the range */
    ASSERT_EQ(1u, runs(&f, hit));
    uint32_t other = 0;
    float speed = 0.0f;
    read_value(&f, BLOCK_POUT(&f, detect, 0), &other, sizeof(other));
    read_value(&f, BLOCK_POUT(&f, detect, 1), &speed, sizeof(speed));
    ASSERT_EQ(floor, other);
    ASSERT_TRUE(speed > 0.3f && speed < 0.5f);
    nmo_physics_ivp_detach(physics);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(physics_ivp, a_crate_falls_onto_the_floor_and_is_pushed);
REGISTER_TEST(physics_ivp, a_light_crate_floats_in_a_buoyancy_pool);
REGISTER_TEST(physics_ivp, a_landing_is_heard_with_its_speed);
TEST_MAIN_END()
