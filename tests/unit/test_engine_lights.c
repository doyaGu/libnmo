/**
 * @file test_engine_lights.c
 * @brief Lights.dll blocks run in graphs: what they leave on the target
 *        light the engine keeps.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "object/builtin/nmo_parameter_schemas.h"
#include "type/nmo_param_guids.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_SET_LIGHT_ATTENUATION NMO_GUID(0xd63d004d, 0xd54d8100)
#define GUID_SET_LINEAR_ATTENUATION NMO_GUID(0x45895551, 0x59486677)
#define GUID_SET_QUADRATIC_ATTENUATION NMO_GUID(0x0a0e0f18, 0x7a01ef09)
#define GUID_SET_LIGHT_RANGE NMO_GUID(0x22252552, 0x00200020)
#define GUID_SET_LIGHT_TYPE NMO_GUID(0x32165a2f, 0x70000000)
#define GUID_SET_SPECULAR_FLAG NMO_GUID(0x1ae11f1a, 0x1a1e1f11)
#define GUID_SPOT_LIGHT_PROPERTIES NMO_GUID(0x01ed010b, 0x01fd010b)
#define GUID_SET_LIGHT_TARGET NMO_GUID(0x02580258, 0x02580258)
#define GUID_LIGHT_COLOR_PROGRESSION NMO_GUID(0x20171e01, 0x234a1b62)

#define STEP_MS 10.0f

static void input_float(engine_fixture_t *f, nmo_object_id_t pin, float value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

/* A block acting on `object`, run when the script starts */
static nmo_object_id_t block_on(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                nmo_object_id_t object)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    nmo_object_id_t target = id != 0 ? fixture_target(f, id) : 0;
    if (target == 0) {
        return 0;
    }
    fixture_input_int(f, target, (int32_t)object);
    fixture_link(f, f->script_in, BLOCK_IN(f, id, 0), 0);
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

static uint32_t entity_flags(engine_fixture_t *f, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(f->engine, id);
    return index >= 0 ? f->engine->entities.data[index].entity_flags : 0;
}

/*
 * Save the straight curve y = x into the 2D curve parameter `param`: two
 * linear points in the chunk CK2dCurve::Dump writes, under identifier 0xE.
 */
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
        nmo_chunk_write_dword(curve, 0x02u | 0x10000000u); /* linear */
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

TEST(engine_lights, setters_change_the_light_the_engine_keeps)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t light = fixture_create(&f, NMO_CID_TARGETLIGHT, "Spot");
    ASSERT_TRUE(light != 0);

    nmo_object_id_t attenuation = block_on(&f, GUID_SET_LIGHT_ATTENUATION, "Set Light Attenuation", light);
    nmo_object_id_t range = block_on(&f, GUID_SET_LIGHT_RANGE, "Set Light Range", light);
    nmo_object_id_t type = block_on(&f, GUID_SET_LIGHT_TYPE, "Set Light Type", light);
    nmo_object_id_t specular = block_on(&f, GUID_SET_SPECULAR_FLAG, "Set Specular Flag", light);
    nmo_object_id_t spot = block_on(&f, GUID_SPOT_LIGHT_PROPERTIES, "Spot Light Properties", light);
    ASSERT_TRUE(attenuation != 0 && range != 0 && type != 0 && specular != 0 && spot != 0);
    input_float(&f, BLOCK_PIN(&f, attenuation, 0), 2.0f);
    input_float(&f, BLOCK_PIN(&f, attenuation, 1), 0.5f);
    input_float(&f, BLOCK_PIN(&f, attenuation, 2), 0.25f);
    input_float(&f, BLOCK_PIN(&f, range, 0), 30.0f);
    fixture_input_int(&f, BLOCK_PIN(&f, type, 0), VX_LIGHTSPOT);
    fixture_input_int(&f, BLOCK_PIN(&f, specular, 0), 1);
    input_float(&f, BLOCK_PIN(&f, spot, 0), 0.5f);
    input_float(&f, BLOCK_PIN(&f, spot, 1), 2.0f);
    input_float(&f, BLOCK_PIN(&f, spot, 2), 0.3f);

    /* A frame later the obsolete single attenuation setters overwrite two */
    nmo_object_id_t linear = fixture_block(&f, GUID_SET_LINEAR_ATTENUATION, "Set Linear Attenuation");
    nmo_object_id_t quadratic =
        fixture_block(&f, GUID_SET_QUADRATIC_ATTENUATION, "Set Quadratic Attenuation");
    ASSERT_TRUE(linear != 0 && quadratic != 0);
    fixture_input_int(&f, fixture_target(&f, linear), (int32_t)light);
    fixture_input_int(&f, fixture_target(&f, quadratic), (int32_t)light);
    fixture_link(&f, BLOCK_OUT(&f, attenuation, 0), BLOCK_IN(&f, linear, 0), 1);
    fixture_link(&f, BLOCK_OUT(&f, attenuation, 0), BLOCK_IN(&f, quadratic, 0), 1);
    input_float(&f, BLOCK_PIN(&f, linear, 0), 0.125f);
    input_float(&f, BLOCK_PIN(&f, quadratic, 0), 0.0625f);
    start(&f);

    step(&f, 1);
    const eng_light_t *l = eng_light(f.engine, light);
    ASSERT_NOT_NULL(l);
    ASSERT_FLOAT_EQ(2.0f, l->attenuation[0], 0.0f);
    ASSERT_FLOAT_EQ(0.5f, l->attenuation[1], 0.0f);
    ASSERT_FLOAT_EQ(0.25f, l->attenuation[2], 0.0f);
    ASSERT_FLOAT_EQ(30.0f, l->range, 0.0f);
    ASSERT_EQ(VX_LIGHTSPOT, l->type);
    ASSERT_TRUE(l->specular);
    ASSERT_FLOAT_EQ(0.5f, l->fall_off, 0.0f);
    ASSERT_FLOAT_EQ(2.0f, l->fall_off_shape, 0.0f);
    ASSERT_FLOAT_EQ(0.3f, l->hot_spot, 0.0f);

    step(&f, 2);
    l = eng_light(f.engine, light);
    ASSERT_FLOAT_EQ(2.0f, l->attenuation[0], 0.0f);
    ASSERT_FLOAT_EQ(0.125f, l->attenuation[1], 0.0f);
    ASSERT_FLOAT_EQ(0.0625f, l->attenuation[2], 0.0f);
    fixture_end(&f);
}

TEST(engine_lights, set_light_target_takes_plain_3d_entities_only)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t light = fixture_create(&f, NMO_CID_TARGETLIGHT, "Spot");
    nmo_object_id_t aim = fixture_create(&f, NMO_CID_3DENTITY, "Aim");
    nmo_object_id_t object = fixture_create(&f, NMO_CID_3DOBJECT, "Object");
    ASSERT_TRUE(light != 0 && aim != 0 && object != 0);

    nmo_object_id_t first = block_on(&f, GUID_SET_LIGHT_TARGET, "Set Light Target", light);
    nmo_object_id_t second = fixture_block(&f, GUID_SET_LIGHT_TARGET, "Set Light Target");
    ASSERT_TRUE(first != 0 && second != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, first, 0), (int32_t)aim);
    fixture_input_int(&f, fixture_target(&f, second), (int32_t)light);
    fixture_input_int(&f, BLOCK_PIN(&f, second, 0), (int32_t)object);
    fixture_link(&f, BLOCK_OUT(&f, first, 0), BLOCK_IN(&f, second, 0), 1);
    start(&f);

    step(&f, 1);
    ASSERT_EQ(aim, eng_light(f.engine, light)->target);
    ASSERT_TRUE((entity_flags(&f, aim) & CK_3DENTITY_TARGETLIGHT) != 0);
    ASSERT_TRUE((entity_flags(&f, aim) & CK_3DENTITY_FRAME) == 0);

    /* A 3D object derives from 3D Entity but is not one: no target */
    step(&f, 2);
    ASSERT_EQ(0u, eng_light(f.engine, light)->target);
    ASSERT_TRUE((entity_flags(&f, aim) & CK_3DENTITY_TARGETLIGHT) == 0);
    ASSERT_TRUE((entity_flags(&f, aim) & CK_3DENTITY_FRAME) != 0);
    ASSERT_TRUE((entity_flags(&f, object) & CK_3DENTITY_TARGETLIGHT) == 0);
    fixture_end(&f);
}

TEST(engine_lights, light_color_progression_stops_short_of_the_end_color)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t light = fixture_create(&f, NMO_CID_TARGETLIGHT, "Spot");
    ASSERT_TRUE(light != 0);
    nmo_object_id_t flash = block_on(&f, GUID_LIGHT_COLOR_PROGRESSION, "Light Color Progression", light);
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    nmo_object_id_t curve = 0;
    ASSERT_TRUE(flash != 0 && done != 0);
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, BLOCK_PIN(&f, flash, 1), &curve));
    ASSERT_TRUE(save_straight_curve(&f, curve));
    fixture_link(&f, BLOCK_OUT(&f, flash, 1), BLOCK_IN(&f, flash, 1), 1);
    fixture_link(&f, BLOCK_OUT(&f, flash, 0), BLOCK_IN(&f, done, 0), 0);
    input_float(&f, BLOCK_PIN(&f, flash, 0), 4.0f * STEP_MS);
    const float from[4] = {0.0f, 0.0f, 0.0f, 0.5f};
    const float to[4] = {1.0f, 0.5f, 0.25f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, flash, 2), from, sizeof(from));
    fixture_input_value(&f, BLOCK_PIN(&f, flash, 3), to, sizeof(to));
    start(&f);

    /* The curve is read before the time advances: 0, 1/4, 2/4, then 3/4 and out */
    step(&f, 2);
    const eng_light_t *l = eng_light(f.engine, light);
    ASSERT_FLOAT_EQ(0.25f, l->color[0], 1e-5f);
    ASSERT_EQ(0u, runs(&f, done));
    step(&f, 6);
    l = eng_light(f.engine, light);
    ASSERT_EQ(1u, runs(&f, done));
    ASSERT_FLOAT_EQ(0.75f, l->color[0], 1e-5f);
    ASSERT_FLOAT_EQ(0.375f, l->color[1], 1e-5f);
    ASSERT_FLOAT_EQ(0.1875f, l->color[2], 1e-5f);
    ASSERT_FLOAT_EQ(0.5f, l->color[3], 0.0f); /* the start alpha */
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_lights, setters_change_the_light_the_engine_keeps);
    REGISTER_TEST(engine_lights, set_light_target_takes_plain_3d_entities_only);
    REGISTER_TEST(engine_lights, light_color_progression_stops_short_of_the_end_color);
TEST_MAIN_END()
