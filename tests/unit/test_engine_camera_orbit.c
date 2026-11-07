/**
 * @file test_engine_camera_orbit.c
 * @brief Cameras.dll's orbit, Vertigo and Camera Color Filter blocks run in
 *        graphs: where the camera ends up, its field of view, and the
 *        quad the filter draws.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "core/nmo_arena.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"

#include <math.h>

#define GUID_CAMERA_ORBIT NMO_GUID(0x777d999e, 0xdef777d8)
#define GUID_KEYBOARD_CAMERA_ORBIT NMO_GUID(0x07610f4d, 0x69747b9d)
#define GUID_VERTIGO NMO_GUID(0xb00d010a, 0xc00d010a)
#define GUID_CAMERA_COLOR_FILTER NMO_GUID(0x00cd010b, 0x00dd010b)
#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)

#define STEP_MS 10.0f
#define KEY_LEFT 0xCBu
#define KEY_RIGHT 0xCDu

static void input_float(engine_fixture_t *f, nmo_object_id_t pin, float value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

/* A camera at `z` on the Z axis looking along +Z, its field of view `fov` */
static nmo_object_id_t camera_at(engine_fixture_t *f, float z, float fov)
{
    nmo_object_id_t camera = fixture_create(f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *state = (nmo_camera_state_t *)fixture_state(f, camera);
    if (state == NULL) {
        return 0;
    }
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, z, 1};
    memcpy(state->entity.world_matrix, world, sizeof(world));
    state->entity.has_entityndata_chunk = state->has_cameraonly_chunk = 1;
    state->projection_type = CK_PERSPECTIVEPROJECTION;
    state->fov = fov;
    state->aspect_width = state->aspect_height = 1;
    state->near_plane = 1;
    state->far_plane = 100;
    return camera;
}

/* A 3D entity at `z` on the Z axis */
static nmo_object_id_t entity_at(engine_fixture_t *f, float z)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DENTITY, "Point");
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    if (state == NULL) {
        return 0;
    }
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, z, 1};
    memcpy(state->world_matrix, world, sizeof(world));
    state->has_entityndata_chunk = 1;
    return id;
}

static nmo_object_id_t block_on(engine_fixture_t *f, nmo_guid_t guid, const char *name, nmo_object_id_t object)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    nmo_object_id_t target = id != 0 ? fixture_target(f, id) : 0;
    if (target == 0) {
        return 0;
    }
    fixture_input_int(f, target, (int32_t)object);
    return id;
}

static void start(engine_fixture_t *f)
{
    fixture_commit(f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f->engine, NMO_ENGINE_START_ALL));
}

static void step(engine_fixture_t *f, int frames)
{
    for (int i = 0; i < frames; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f->engine, STEP_MS));
    }
}

static const eng_matrix_t *world_of(engine_fixture_t *f, nmo_object_id_t id)
{
    return &f->engine->entities.data[eng_entity_index(f->engine, id)].world;
}

TEST(engine_camera_orbit, camera_orbit_turns_about_the_target_keeping_its_distance)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t camera = camera_at(&f, -10.0f, 1.0f);
    ASSERT_TRUE(camera != 0);
    nmo_object_id_t orbit = block_on(&f, GUID_CAMERA_ORBIT, "Camera Orbit", camera);
    ASSERT_TRUE(orbit != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, orbit, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, orbit, 0), BLOCK_IN(&f, orbit, 0), 1);
    start(&f);
    nmo_engine_set_key(f.engine, KEY_LEFT, true);

    /* Each frame: a thousandth of the distance a millisecond to the left, then back to 10 */
    step(&f, 1);
    const eng_matrix_t *at = world_of(&f, camera);
    ASSERT_FLOAT_EQ(-0.1f, at->m[3][0], 1e-3f);
    float distance = 0.0f;
    read_value(&f, BLOCK_POUT(&f, orbit, 0), &distance, sizeof(distance));
    ASSERT_FLOAT_EQ(10.0f, distance, 1e-3f);
    float length = sqrtf(at->m[3][0] * at->m[3][0] + at->m[3][1] * at->m[3][1] + at->m[3][2] * at->m[3][2]);
    ASSERT_FLOAT_EQ(10.0f, length, 1e-3f);
    /* It looks at the origin: its Z axis points there */
    ASSERT_FLOAT_EQ(-at->m[3][0] / length, at->m[2][0], 1e-4f);
    fixture_end(&f);
}

TEST(engine_camera_orbit, keyboard_camera_orbit_turns_then_returns_after_off)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t camera = camera_at(&f, -10.0f, 1.0f);
    ASSERT_TRUE(camera != 0);
    nmo_object_id_t orbit = block_on(&f, GUID_KEYBOARD_CAMERA_ORBIT, "Keyboard Camera Orbit", camera);
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(orbit != 0 && done != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, orbit, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, orbit, 1), BLOCK_IN(&f, done, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, orbit, 1), 2); /* Off on the third frame */
    input_float(&f, BLOCK_PIN(&f, orbit, 2), 1.0f); /* Move Speed: a radian a second */
    input_float(&f, BLOCK_PIN(&f, orbit, 3), 1.0f); /* Return Speed */
    start(&f);

    step(&f, 1); /* On: the angles about the origin */
    nmo_engine_set_key(f.engine, KEY_RIGHT, true);
    step(&f, 1);
    const eng_matrix_t *at = world_of(&f, camera);
    float x = -1.5707964f + 0.01f;
    ASSERT_FLOAT_EQ(10.0f * cosf(x), at->m[3][0], 1e-4f);
    ASSERT_FLOAT_EQ(10.0f * sinf(x), at->m[3][2], 1e-4f);

    /* Off: back at the return speed (one frame's worth), then Exit Off */
    nmo_engine_set_key(f.engine, KEY_RIGHT, false);
    ASSERT_EQ(0u, runs(&f, done));
    step(&f, 1);
    ASSERT_EQ(1u, runs(&f, done));
    at = world_of(&f, camera);
    ASSERT_FLOAT_EQ(0.0f, at->m[3][0], 1e-4f);
    ASSERT_FLOAT_EQ(-10.0f, at->m[3][2], 1e-4f);
    fixture_end(&f);
}

/* The straight curve y = x saved in a 2D curve parameter (CK2dCurve::Dump under 0xE) */
static bool save_straight_curve(engine_fixture_t *f, nmo_object_id_t param)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, param);
    nmo_chunk_t *curve = nmo_chunk_create(arena);
    nmo_chunk_t *saved = nmo_chunk_create(arena);
    if (state == NULL || curve == NULL || saved == NULL) {
        return false;
    }
    nmo_chunk_start_write(curve);
    nmo_chunk_set_data_version(curve, 7);
    nmo_chunk_write_identifier(curve, 0x100u);
    nmo_chunk_write_int(curve, 2);
    nmo_chunk_write_int(curve, 0);
    nmo_chunk_write_int(curve, 0);
    nmo_chunk_write_float(curve, 0.0f);
    for (int i = 0; i < 2; i++) {
        nmo_chunk_write_dword(curve, 0x02u | 0x10000000u);
        nmo_chunk_write_float(curve, (float)i);
        nmo_chunk_write_float(curve, (float)i);
        for (int v = 0; v < 9; v++) {
            nmo_chunk_write_float(curve, 0.0f);
        }
    }
    nmo_chunk_close(curve);
    nmo_chunk_start_write(saved);
    nmo_chunk_write_identifier(saved, 0x0eu);
    nmo_chunk_write_sub_chunk(saved, curve);
    nmo_chunk_close(saved);
    nmo_array_dispose(&state->buffer_data);
    state->mode = CKPARAM_MODE_SUBCHUNK;
    state->subchunk = saved;
    state->has_state = true;
    return true;
}

TEST(engine_camera_orbit, vertigo_dollies_in_keeping_the_focal_size)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t camera = camera_at(&f, 0.0f, 1.0f);
    nmo_object_id_t focus = entity_at(&f, 10.0f);
    ASSERT_TRUE(camera != 0 && focus != 0);
    nmo_object_id_t vertigo = block_on(&f, GUID_VERTIGO, "Vertigo", camera);
    nmo_object_id_t curve = 0;
    ASSERT_TRUE(vertigo != 0);
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, BLOCK_PIN(&f, vertigo, 4), &curve));
    ASSERT_TRUE(save_straight_curve(&f, curve));
    fixture_link(&f, f.script_in, BLOCK_IN(&f, vertigo, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, vertigo, 1), BLOCK_IN(&f, vertigo, 1), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, vertigo, 0), (int32_t)focus);
    fixture_input_int(&f, BLOCK_PIN(&f, vertigo, 1), 2);
    input_float(&f, BLOCK_PIN(&f, vertigo, 3), 0.5f);
    start(&f);

    /* In, then two steps of half of half the depth: 2.5 then 5 nearer */
    step(&f, 2);
    float size = tanf(0.5f) * 10.0f;
    ASSERT_FLOAT_EQ(2.5f, world_of(&f, camera)->m[3][2], 1e-4f);
    float fov = 0.0f;
    ASSERT_TRUE(eng_camera_fov(f.engine, camera, &fov));
    ASSERT_FLOAT_EQ(2.0f * atanf(size / 7.5f), fov, 1e-5f);
    step(&f, 1);
    ASSERT_FLOAT_EQ(5.0f, world_of(&f, camera)->m[3][2], 1e-4f);
    ASSERT_TRUE(eng_camera_fov(f.engine, camera, &fov));
    ASSERT_FLOAT_EQ(2.0f * atanf(size / 5.0f), fov, 1e-5f);
    fixture_end(&f);
}

/* The frame's material-less quads */
static int filter_quads(engine_fixture_t *f, nmo_engine_draw_t *last)
{
    nmo_engine_frame_t frame;
    if (nmo_engine_render_frame(f->engine, &frame) != NMO_OK) {
        return -1;
    }
    int count = 0;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        if (frame.draws[i].kind == NMO_ENGINE_DRAW_QUAD && frame.draws[i].blend && frame.draws[i].object == 0) {
            *last = frame.draws[i];
            count++;
        }
    }
    return count;
}

TEST(engine_camera_orbit, camera_color_filter_colors_the_view_of_its_camera)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t camera = camera_at(&f, 0.0f, 1.0f);
    ASSERT_TRUE(camera != 0);
    nmo_object_id_t filter = block_on(&f, GUID_CAMERA_COLOR_FILTER, "Camera Color Filter", camera);
    ASSERT_TRUE(filter != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, filter, 0), 0);
    const float color[4] = {1.0f, 0.5f, 0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, filter, 0), color, sizeof(color));
    input_float(&f, BLOCK_PIN(&f, filter, 1), 0.5f);
    start(&f);
    f.engine->render.camera = camera;

    step(&f, 1);
    nmo_engine_draw_t quad;
    ASSERT_EQ(1, filter_quads(&f, &quad));
    ASSERT_EQ(VXBLEND_ONE, quad.source_blend);
    ASSERT_EQ(VXBLEND_ONE, quad.dest_blend);
    ASSERT_EQ(0xFF7F3F00u, quad.color); /* Density times the color, opaque */
    /* The callback is the frame's: rendering again without the block draws nothing */
    ASSERT_EQ(0, filter_quads(&f, &quad));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_camera_orbit, camera_orbit_turns_about_the_target_keeping_its_distance);
    REGISTER_TEST(engine_camera_orbit, keyboard_camera_orbit_turns_then_returns_after_off);
    REGISTER_TEST(engine_camera_orbit, vertigo_dollies_in_keeping_the_focal_size);
    REGISTER_TEST(engine_camera_orbit, camera_color_filter_colors_the_view_of_its_camera);
TEST_MAIN_END()
