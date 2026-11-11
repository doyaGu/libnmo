#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/bb/terratools/terratools.h"

#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "type/nmo_param_guids.h"

#include <math.h>
#include <stdio.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_WAVE_PLAYER NMO_GUID(0x5bde0e45, 0x2e2107d5)
#define GUID_POINT_PARTICLES NMO_GUID(0x506b40f7, 0x30852e46)
#define GUID_WAVE_PARTICLES NMO_GUID(0x02da5a18, 0x52227285)
#define GUID_MOVIE_LOAD NMO_GUID(0x798e26a9, 0x34e4169e)
#define GUID_MOVIE_PLAYER NMO_GUID(0x778d16d4, 0x1dd60060)
#define GUID_PLAY_ANIMATION NMO_GUID(0x64221225, 0x769a143f)
#define GUID_PLAY_GLOBAL_ANIMATION NMO_GUID(0x1c9236e1, 0x42f40996)
#define GUID_SOUND_LOAD NMO_GUID(0x02bc537c, 0x147c22e0)
#define GUID_VOLUME_CONTROL NMO_GUID(0x33613f21, 0x776b185b)
#define GUID_PLAY_SOUND_INSTANCE NMO_GUID(0x00283a35, 0x38ef6b48)
#define GUID_TIME_SETTINGS NMO_GUID(0x060a0f8c, 0x09203b5f)
#define GUID_TT_SET_MIP_MAPPING NMO_GUID(0x38c34dab, 0x5dd03bdf)
#define GUID_POSITION_SOUND NMO_GUID(0x4ba42a5b, 0x3ca030dd)
#define GUID_SET_SOUND_TYPE NMO_GUID(0x7734470d, 0x23b221f8)
#define GUID_SET_SOUND_RANGE NMO_GUID(0x27f50a7c, 0x0ee91d0e)
#define GUID_SET_SOUND_CONE_VALUES NMO_GUID(0xabf60a8c, 0x0ff91d0d)
#define GUID_SET_LISTENER NMO_GUID(0x28cd6341, 0x0cad5793)
#define GUID_SOUND_MANAGER_SETUP NMO_GUID(0x04c05c6a, 0x6454792b)
#define GUID_FADE_OUT NMO_GUID(0x1b82338f, 0x5a656975)

static void input_float(engine_fixture_t *f, nmo_object_id_t pin, float value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

/* A CKWaveSound of `length` ms. */
static nmo_object_id_t create_sound(engine_fixture_t *f, int32_t length)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_WAVESOUND, "Sound");
    nmo_wavesound_state_t *state = (nmo_wavesound_state_t *)fixture_state(f, id);
    if (state == NULL) {
        return 0;
    }
    state->has_duration = 1;
    state->duration = length;
    return id;
}

TEST(engine_media, wave_player_ends_after_the_sound_length)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sound = create_sound(&f, 100);
    ASSERT_TRUE(sound != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t player = fixture_block(&f, GUID_WAVE_PLAYER, "Wave Player");
    nmo_object_id_t ended = fixture_block(&f, GUID_NOP, "Ended");
    ASSERT_TRUE(player != 0 && fixture_target(&f, player) != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, player, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, player, 1), BLOCK_IN(&f, ended, 0), 0);
    fixture_input_int(&f, fixture_target(&f, player), (int32_t)sound);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f)); /* frame 0: Play */
    ASSERT_TRUE(nmo_engine_sound_is_playing(f.engine, sound));
    /* The sound plays 100 ms: from frame 1 to frame 4, at 25 ms a frame */
    for (int frame = 1; frame < 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
        ASSERT_EQ(0u, runs(&f, ended));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_FALSE(nmo_engine_sound_is_playing(f.engine, sound));
    ASSERT_EQ(1u, runs(&f, ended));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, player));
    fixture_end(&f);
}

TEST(engine_media, particle_system_exits_off_once_its_particles_died)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t ps = fixture_block(&f, GUID_POINT_PARTICLES, "Particles");
    nmo_object_id_t stop = fixture_block(&f, GUID_DELAYER, "Stop Later");
    nmo_object_id_t exit_on = fixture_block(&f, GUID_NOP, "Exit On");
    nmo_object_id_t exit_off = fixture_block(&f, GUID_NOP, "Exit Off");
    ASSERT_TRUE(ps != 0 && stop != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, ps, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, stop, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, stop, 0), BLOCK_IN(&f, ps, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, ps, 0), BLOCK_IN(&f, exit_on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, ps, 1), BLOCK_IN(&f, exit_off, 0), 0);
    input_float(&f, BLOCK_PIN(&f, ps, 8), 100.0f); /* Lifespan */
    input_float(&f, BLOCK_PIN(&f, ps, 9), 0.0f);   /* Lifespan variation */
    input_float(&f, BLOCK_PIN(&f, stop, 0), 50.0f);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int32_t emitter = eng_entity_index(f.engine, f.owner_id);
    eng_vector_t low, high;
    ASSERT_FALSE(eng_entity_local_box(f.engine, emitter, &low, &high)); /* no mesh, no box */
    uint32_t frames = 0;
    while (runs(&f, exit_off) == 0 && frames < 40) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
        frames++;
    }
    /* each update with particles sets the emitter's box (SetBoundingBox) */
    ASSERT_TRUE(eng_entity_local_box(f.engine, emitter, &low, &high));
    ASSERT_EQ(1u, runs(&f, exit_on));
    ASSERT_EQ(1u, runs(&f, exit_off));
    /* The first frame emits (Emission Delay 200 ms, so only then); those particles live
       100 ms, four updates of 25 ms, and leave in the fifth, after Off at 50 ms */
    ASSERT_EQ(5u, frames);
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, ps));
    fixture_end(&f);
}

TEST(engine_media, wave_particles_stay_where_they_were_emitted)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t ps = fixture_block(&f, GUID_WAVE_PARTICLES, "Wave");
    ASSERT_TRUE(ps != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, ps, 0), 0);
    input_float(&f, BLOCK_PIN(&f, ps, 4), 0.01f);    /* Speed: a point emitter's would fly off */
    input_float(&f, BLOCK_PIN(&f, ps, 8), 1000.0f);  /* Lifespan */
    input_float(&f, BLOCK_PIN(&f, ps, 9), 0.0f);
    input_float(&f, BLOCK_PIN(&f, ps, 0), 0.0f);      /* Emission Delay: emit at every update */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int32_t owner = eng_entity_index(f.engine, f.owner_id);
    ASSERT_TRUE(owner >= 0);
    const float origin[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, origin, false));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    const float moved[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1};
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, moved, false));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    eng_show(f.engine, f.owner_id, 1);

    /* WaveEmitter: no direction, UpdateParticles2: no move. Each particle's quad is centred where
       the emitter was when it came out, two updates later: x 0 or 5 */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int quads = 0, at_origin = 0;
    for (int32_t d = 0; d < frame.draw_count; d++) {
        const nmo_engine_draw_t *draw = &frame.draws[d];
        if (draw->kind != NMO_ENGINE_DRAW_TRIANGLES || draw->object != f.owner_id) {
            continue;
        }
        for (int32_t q = 0; q + 6 <= draw->vertex_count; q += 6) {
            float low[3] = {1e9f, 1e9f, 1e9f}, high[3] = {-1e9f, -1e9f, -1e9f};
            for (int32_t k = 0; k < 6; k++) {
                const nmo_engine_vertex_t *v = &frame.vertices[draw->first_vertex + q + k];
                const float p[3] = {v->x, v->y, v->z};
                for (int c = 0; c < 3; c++) {
                    low[c] = p[c] < low[c] ? p[c] : low[c];
                    high[c] = p[c] > high[c] ? p[c] : high[c];
                }
            }
            const float x = (low[0] + high[0]) * 0.5f;
            ASSERT_TRUE(fabsf(x) < 1e-4f || fabsf(x - 5.0f) < 1e-4f);
            ASSERT_FLOAT_EQ(0.0f, (low[1] + high[1]) * 0.5f, 1e-4f);
            ASSERT_FLOAT_EQ(0.0f, (low[2] + high[2]) * 0.5f, 1e-4f);
            at_origin += fabsf(x) < 1e-4f;
            quads++;
        }
    }
    ASSERT_TRUE(quads > 0);
    ASSERT_TRUE(at_origin > 0);
    ASSERT_TRUE(at_origin < quads);
    fixture_end(&f);
}

/* The block's local parameter at `pos` */
static nmo_object_id_t local_of(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

/* A parameter of `type` holding `size` bytes, made as `class_id` */
static nmo_object_id_t value_parameter(engine_fixture_t *f, nmo_class_id_t class_id, nmo_guid_t type,
                                       const void *bytes, uint32_t size)
{
    nmo_object_id_t parameter = fixture_create(f, class_id, "Value");
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, parameter);
    if (state == NULL || nmo_array_alloc(&state->buffer_data, 1, size, NULL) != NMO_OK) {
        return 0;
    }
    state->has_state = true;
    state->mode = CKPARAM_MODE_BUFFER;
    state->type_guid = type;
    memcpy(state->buffer_data.data, bytes, size);
    return parameter;
}

/* The attribute `name` of parameter type `type` on `object`, its value `bytes`. After
   fixture_begin_edit(). */
static void value_attribute(engine_fixture_t *f, nmo_object_id_t object, const char *name, nmo_guid_t type,
                            const void *bytes, uint32_t size)
{
    nmo_manager_entry_create_options_t options = {.enabled = true, .attribute_type_guid = type,
        .category = "Particle System Interactors", .has_compatible_class_id = true,
        .compatible_class_id = NMO_CID_3DENTITY, .has_flags = true, .flags = 0};
    uint32_t attribute = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx), name,
                                                                     &options, &attribute));
    nmo_object_id_t parameter = value_parameter(f, NMO_CID_PARAMETEROUT, type, bytes, size);
    ASSERT_TRUE(parameter != 0);
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)fixture_state(f, object))->attributes,
                                                          parameter, attribute, NULL));
}

/* A 3D entity at the origin */
static nmo_object_id_t plain_entity(engine_fixture_t *f, const char *name)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DENTITY, name);
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    if (state != NULL) {
        state->has_entityndata_chunk = 1;
        memset(state->world_matrix, 0, sizeof(state->world_matrix));
        state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = state->world_matrix[15] = 1.0f;
    }
    return id;
}

/* A point particle system on a new emitter at the origin, emitting still particles of weight and
   surface 1 at every update and managing `interactors`. After fixture_begin_edit(). */
static nmo_object_id_t still_particles(engine_fixture_t *f, nmo_object_id_t emitter, int32_t interactors)
{
    (void)emitter; /* the script's owner, set before the script */
    nmo_object_id_t ps = fixture_block(f, GUID_POINT_PARTICLES, "Particles");
    if (ps == 0) {
        return 0;
    }
    fixture_link(f, f->script_in, BLOCK_IN(f, ps, 0), 0);
    input_float(f, BLOCK_PIN(f, ps, 0), 0.0f);    /* Emission Delay */
    input_float(f, BLOCK_PIN(f, ps, 4), 0.0f);    /* Speed */
    input_float(f, BLOCK_PIN(f, ps, 8), 1000.0f); /* Lifespan */
    input_float(f, BLOCK_PIN(f, ps, 9), 0.0f);
    fixture_input_int(f, BLOCK_PIN(f, ps, 11), 1); /* Emission */
    input_float(f, BLOCK_PIN(f, ps, 13), 1.0f);   /* Initial Size */
    input_float(f, BLOCK_PIN(f, ps, 19), 1.0f);   /* Weight */
    input_float(f, BLOCK_PIN(f, ps, 21), 1.0f);   /* Surface */
    const float blue[4] = {0, 0, 1, 1};
    fixture_input_value(f, BLOCK_PIN(f, ps, 23), blue, sizeof(blue)); /* Initial Color */
    const int32_t settings[2][2] = {{9, 0}, {12, interactors}}; /* no Variances; Manage Interactors */
    for (int i = 0; i < 2; i++) {
        engine_fixture_value_t *setting = &f->values[f->value_count++];
        setting->param = local_of(f, ps, (size_t)settings[i][0]);
        memcpy(setting->bytes, &settings[i][1], sizeof(int32_t));
        setting->size = sizeof(int32_t);
    }
    return ps;
}

TEST(engine_media, gravity_and_global_wind_push_the_particles)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t emitter = plain_entity(&f, "Emitter");
    nmo_object_id_t field = plain_entity(&f, "Field");
    ASSERT_TRUE(emitter != 0 && field != 0);
    f.owner_id = emitter; /* the particles' target */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    const float gravity = -0.001f, wind = 0.002f;
    value_attribute(&f, field, "Particle Gravity", CKPGUID_FLOAT, &gravity, sizeof(gravity));
    value_attribute(&f, field, "Particle Global Wind : Force", CKPGUID_FLOAT, &wind, sizeof(wind));
    ASSERT_TRUE(still_particles(&f, emitter, 0x003) != 0); /* Gravity, Global Wind */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));

    /* emitted still, then pushed before it moves: down by gravity * weight * 25 ms, along the
       field's Z by wind * surface * 25 ms, and moved by that for 25 ms */
    const eng_tt_t *tt = eng_tt(f.engine);
    ASSERT_EQ(1u, (uint32_t)tt->emitters.count);
    ASSERT_TRUE(tt->emitters.data[0].particles.count >= 1);
    const eng_particle_t *p = &tt->emitters.data[0].particles.data[0];
    ASSERT_FLOAT_EQ(0.0f, p->dir[0], 1e-7f);
    ASSERT_FLOAT_EQ(-0.025f, p->dir[1], 1e-6f);
    ASSERT_FLOAT_EQ(0.05f, p->dir[2], 1e-6f);
    ASSERT_FLOAT_EQ(-0.625f, p->pos[1], 1e-5f);
    ASSERT_FLOAT_EQ(1.25f, p->pos[2], 1e-5f);
    fixture_end(&f);
}

TEST(engine_media, a_mutation_box_brings_size_and_color_to_its_own)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t emitter = plain_entity(&f, "Emitter");
    nmo_object_id_t box = plain_entity(&f, "Box");
    ASSERT_TRUE(emitter != 0 && box != 0);
    f.owner_id = emitter; /* the particles' target */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* Size 2 at 0.01 a ms, red at 0.02 a ms (the structure's fields) */
    const float size = 2.0f, size_speed = 0.01f, red[4] = {1, 0, 0, 1}, color_speed = 0.02f;
    const nmo_object_id_t fields[4] = {
        value_parameter(&f, NMO_CID_PARAMETERLOCAL, CKPGUID_FLOAT, &size, sizeof(size)),
        value_parameter(&f, NMO_CID_PARAMETERLOCAL, CKPGUID_FLOAT, &size_speed, sizeof(size_speed)),
        value_parameter(&f, NMO_CID_PARAMETERLOCAL, CKPGUID_COLOR, red, sizeof(red)),
        value_parameter(&f, NMO_CID_PARAMETERLOCAL, CKPGUID_FLOAT, &color_speed, sizeof(color_speed))};
    const nmo_object_id_t structure[5] = {fields[0], fields[1], fields[2], fields[3], 0};
    value_attribute(&f, box, "Particle Mutation Box", CKPGUID_FLOOR, structure, sizeof(structure));
    ASSERT_TRUE(still_particles(&f, emitter, 0x080) != 0); /* Mutation Box */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const eng_vector_t low = {-10, -10, -10}, high = {10, 10, 10};
    eng_entity_set_user_box(f.engine, eng_entity_index(f.engine, box), &low, &high, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));

    /* inside the box: size 1 a quarter of the way to 2, blue half way to red, their own
       evolutions stopped */
    const eng_tt_t *tt = eng_tt(f.engine);
    ASSERT_EQ(1u, (uint32_t)tt->emitters.count);
    ASSERT_TRUE(tt->emitters.data[0].particles.count >= 1);
    const eng_particle_t *p = &tt->emitters.data[0].particles.data[0];
    ASSERT_FLOAT_EQ(1.25f, p->size, 1e-5f);
    ASSERT_FLOAT_EQ(0.0f, p->dsize, 0.0f);
    ASSERT_FLOAT_EQ(0.5f, p->color[0], 1e-5f);
    ASSERT_FLOAT_EQ(0.5f, p->color[2], 1e-5f);
    ASSERT_FLOAT_EQ(1.0f, p->color[3], 1e-5f);
    fixture_end(&f);
}

/* A point particle system on `emitter` shooting one particle a frame along the emitter's Z at
   0.05 a ms, bounce 1, managing `deflectors` and no interactor. After fixture_begin_edit(). */
static nmo_object_id_t shot_particles(engine_fixture_t *f, int32_t deflectors)
{
    nmo_object_id_t ps = fixture_block(f, GUID_POINT_PARTICLES, "Particles");
    if (ps == 0) {
        return 0;
    }
    fixture_link(f, f->script_in, BLOCK_IN(f, ps, 0), 0);
    input_float(f, BLOCK_PIN(f, ps, 0), 0.0f);    /* Emission Delay */
    input_float(f, BLOCK_PIN(f, ps, 2), 0.0f);    /* Yaw Variance */
    input_float(f, BLOCK_PIN(f, ps, 3), 0.0f);    /* Pitch Variance */
    input_float(f, BLOCK_PIN(f, ps, 4), 0.05f);   /* Speed */
    input_float(f, BLOCK_PIN(f, ps, 8), 1000.0f); /* Lifespan */
    fixture_input_int(f, BLOCK_PIN(f, ps, 11), 1); /* Emission */
    input_float(f, BLOCK_PIN(f, ps, 17), 1.0f);   /* Bounce */
    const int32_t settings[3][2] = {{9, 0}, {10, deflectors}, {12, 0}};
    for (int i = 0; i < 3; i++) {
        engine_fixture_value_t *setting = &f->values[f->value_count++];
        setting->param = local_of(f, ps, (size_t)settings[i][0]);
        memcpy(setting->bytes, &settings[i][1], sizeof(int32_t));
        setting->size = sizeof(int32_t);
    }
    return ps;
}

/* A deflector attribute `name` on `object`: response, friction and density */
static void deflector_attribute(engine_fixture_t *f, nmo_object_id_t object, const char *name, float response,
                                float friction, float density)
{
    const float values[3] = {response, friction, density};
    nmo_object_id_t structure[3];
    for (int i = 0; i < 3; i++) {
        structure[i] = value_parameter(f, NMO_CID_PARAMETERLOCAL, CKPGUID_FLOAT, &values[i], sizeof(float));
    }
    value_attribute(f, object, name, CKPGUID_FLOOR, structure, sizeof(structure));
}

TEST(engine_media, an_infinite_plane_deflector_sends_the_particles_back)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t emitter = plain_entity(&f, "Emitter");
    nmo_object_id_t plane = plain_entity(&f, "Floor");
    ASSERT_TRUE(emitter != 0 && plane != 0);
    /* the emitter half a unit up, its Z pointing down */
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(&f, emitter);
    const float down[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0.5f, 0, 1};
    memcpy(state->world_matrix, down, sizeof(down));
    f.owner_id = emitter;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    deflector_attribute(&f, plane, "Particle Infinite Plane Deflector", 1.0f, 1.0f, 1.0f);
    ASSERT_TRUE(shot_particles(&f, 0x02 | 0x40) != 0); /* Infinite Plane, impacts */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));

    /* 1.25 down from 0.5 crosses the plane at the origin: sent back up as far, as fast */
    const eng_emitter_t *e = &eng_tt(f.engine)->emitters.data[0];
    ASSERT_EQ(1u, (uint32_t)e->particles.count);
    const eng_particle_t *p = &e->particles.data[0];
    ASSERT_FLOAT_EQ(1.25f, p->pos[1], 1e-5f);
    ASSERT_FLOAT_EQ(0.05f, p->dir[1], 1e-6f);
    ASSERT_EQ(1u, (uint32_t)e->impacts.count);
    ASSERT_FLOAT_EQ(0.0f, e->impacts.data[0].position[1], 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, e->impacts.data[0].direction[1], 1e-6f);
    ASSERT_EQ(plane, e->impacts.data[0].object);
    fixture_end(&f);
}

TEST(engine_media, a_sphere_deflector_kills_the_particles_that_hit_it)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t emitter = plain_entity(&f, "Emitter");
    nmo_object_id_t sphere = plain_entity(&f, "Ball");
    ASSERT_TRUE(emitter != 0 && sphere != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, emitter))->world_matrix[14] = -2.0f; /* 2 back, facing it */
    f.owner_id = emitter;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    deflector_attribute(&f, sphere, "Particle Sphere Deflector", 1.0f, 1.0f, 1.0f);
    ASSERT_TRUE(shot_particles(&f, 0x08 | 0x80) != 0); /* Sphere, die on impact */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const eng_vector_t low = {-1, -1, -1}, high = {1, 1, 1}; /* the ball's box, to meet the emitter's */
    eng_entity_set_user_box(f.engine, eng_entity_index(f.engine, sphere), &low, &high, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));

    /* from z -2 to -0.75: through the unit sphere's surface at -1, so it dies */
    ASSERT_EQ(0u, (uint32_t)eng_tt(f.engine)->emitters.data[0].particles.count);
    fixture_end(&f);
}

/* The index of the frame's particle draw of `object`; -1 for none */
static int particle_draw(const nmo_engine_frame_t *frame, nmo_object_id_t object)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_TRIANGLES && frame->draws[i].object == object) {
            return i;
        }
    }
    return -1;
}

TEST(engine_media, particles_draw_where_their_render_last_emitter_sorts)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t nearer = plain_entity(&f, "Near");
    nmo_object_id_t farther = plain_entity(&f, "Far");
    ASSERT_TRUE(nearer != 0 && farther != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, nearer))->world_matrix[14] = 10.0f;
    ((nmo_3dentity_state_t *)fixture_state(&f, farther))->world_matrix[14] = 30.0f;
    /* a script with a particle system on each, the near one's first */
    f.owner_id = nearer;
    nmo_object_id_t near_in = 0, far_in = 0;
    nmo_object_id_t near_script = fixture_add_script(&f, "Near Script", &near_in);
    f.owner_id = farther;
    nmo_object_id_t far_script = fixture_add_script(&f, "Far Script", &far_in);
    fixture_begin_edit(&f);
    f.script_id = near_script, f.script_in = near_in;
    ASSERT_TRUE(still_particles(&f, nearer, 0) != 0);
    f.script_id = far_script, f.script_in = far_in;
    ASSERT_TRUE(still_particles(&f, farther, 0) != 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* each owner renders last from the start's new scene (CKM_BEHAVIORNEWSCENE) */
    const nmo_object_id_t owners[2] = {nearer, farther};
    for (int i = 0; i < 2; i++) {
        int32_t at = eng_entity_index(f.engine, owners[i]);
        ASSERT_TRUE((f.engine->entities.data[at].moveable_flags & VX_MOVEABLE_RENDERLAST) != 0);
        eng_show(f.engine, owners[i], 1);
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_EQ(2u, (uint32_t)eng_tt(f.engine)->emitters.count);
    ASSERT_EQ(nearer, eng_tt(f.engine)->emitters.data[0].entity);

    /* each emitter's particles draw as its entity renders: among the transparent objects back to
       front, the far one's first (one pass over the emitters would draw the near one's first) */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int near_draw = particle_draw(&frame, nearer), far_draw = particle_draw(&frame, farther);
    ASSERT_TRUE(near_draw >= 0 && far_draw >= 0);
    ASSERT_TRUE(far_draw < near_draw);

    /* a launch of the level scene warns the blocks again */
    int32_t near_at = eng_entity_index(f.engine, nearer);
    f.engine->entities.data[near_at].moveable_flags &= ~(uint32_t)VX_MOVEABLE_RENDERLAST;
    eng_launch_level_scene(f.engine, 3, 2); /* do nothing, no reset */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_TRUE((f.engine->entities.data[near_at].moveable_flags & VX_MOVEABLE_RENDERLAST) != 0);
    fixture_end(&f);
}

/* A particle system of `guid` on the script's owner at the origin, emitting 20 particles at
   0.001 a ms, no variances, no interactors or deflectors. After fixture_begin_edit(). */
static nmo_object_id_t shaped_particles(engine_fixture_t *f, nmo_guid_t guid)
{
    nmo_object_id_t ps = fixture_block(f, guid, "Particles");
    if (ps == 0) {
        return 0;
    }
    fixture_link(f, f->script_in, BLOCK_IN(f, ps, 0), 0);
    input_float(f, BLOCK_PIN(f, ps, 0), 0.0f);    /* Emission Delay */
    input_float(f, BLOCK_PIN(f, ps, 4), 0.001f);  /* Speed */
    input_float(f, BLOCK_PIN(f, ps, 8), 1000.0f); /* Lifespan */
    fixture_input_int(f, BLOCK_PIN(f, ps, 11), 20); /* Emission */
    const int32_t settings[3][2] = {{9, 0}, {10, 0}, {12, 0}};
    for (int i = 0; i < 3; i++) {
        engine_fixture_value_t *setting = &f->values[f->value_count++];
        setting->param = local_of(f, ps, (size_t)settings[i][0]);
        memcpy(setting->bytes, &settings[i][1], sizeof(int32_t));
        setting->size = sizeof(int32_t);
    }
    return ps;
}

TEST(engine_media, line_cubic_disc_and_cylinder_emitters_place_their_particles)
{
    const nmo_guid_t guids[4] = {NMO_GUID(0x1f1e7188, 0x0ec26d1f), NMO_GUID(0x3d7453f9, 0x1dba691f),
                                 NMO_GUID(0x3fb65480, 0x6ca450e2), NMO_GUID(0x67c90f47, 0x1480721e)};
    for (int shape = 0; shape < 4; shape++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t emitter = plain_entity(&f, "Emitter");
        ASSERT_TRUE(emitter != 0);
        f.owner_id = emitter;
        f.script_id = fixture_add_script(&f, "Script", &f.script_in);
        fixture_begin_edit(&f);
        ASSERT_TRUE(shaped_particles(&f, guids[shape]) != 0);
        fixture_commit(&f, NULL);
        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
        const eng_emitter_t *e = &eng_tt(f.engine)->emitters.data[0];
        ASSERT_EQ(20u, (uint32_t)e->particles.count);
        for (size_t i = 0; i < e->particles.count; i++) {
            const eng_particle_t *p = &e->particles.data[i];
            /* where it came out: back by its 25 ms move */
            const float at[3] = {p->pos[0] - p->dir[0] * 25.0f, p->pos[1] - p->dir[1] * 25.0f,
                                 p->pos[2] - p->dir[2] * 25.0f};
            switch (shape) {
            case 0: /* the local X from -1 to 1 */
                ASSERT_TRUE(fabsf(at[0]) <= 1.0f);
                ASSERT_FLOAT_EQ(0.0f, at[1], 1e-6f);
                ASSERT_FLOAT_EQ(0.0f, at[2], 1e-6f);
                break;
            case 1: /* the -1..1 cube */
                ASSERT_TRUE(fabsf(at[0]) <= 1.0f && fabsf(at[1]) <= 1.0f && fabsf(at[2]) <= 1.0f);
                break;
            case 2: /* within the unit disc of XY */
                ASSERT_TRUE(at[0] * at[0] + at[1] * at[1] <= 1.0f + 1e-5f);
                ASSERT_FLOAT_EQ(0.0f, at[2], 1e-6f);
                break;
            default: /* on the unit tube about Z, going straight out */
                ASSERT_FLOAT_EQ(1.0f, at[0] * at[0] + at[1] * at[1], 1e-4f);
                ASSERT_TRUE(fabsf(at[2]) <= 1.0f);
                ASSERT_FLOAT_EQ(0.0f, p->dir[2], 1e-7f);
                ASSERT_FLOAT_EQ(0.0f, p->dir[0] * at[1] - p->dir[1] * at[0], 1e-6f);
                ASSERT_TRUE(p->dir[0] * at[0] + p->dir[1] * at[1] > 0.0f);
                break;
            }
        }
        fixture_end(&f);
    }
}

TEST(engine_media, voices_follow_the_sound_settings_and_instances)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sound = create_sound(&f, 1000);
    ASSERT_TRUE(sound != 0);
    nmo_wavesound_state_t *state = (nmo_wavesound_state_t *)fixture_state(&f, sound);
    ASSERT_NOT_NULL(state);
    state->base.file_name = (char *)"hit.wav";
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t volume = fixture_block(&f, GUID_VOLUME_CONTROL, "Volume");
    nmo_object_id_t first = fixture_block(&f, GUID_PLAY_SOUND_INSTANCE, "First");
    nmo_object_id_t second = fixture_block(&f, GUID_PLAY_SOUND_INSTANCE, "Second");
    nmo_object_id_t player = fixture_block(&f, GUID_WAVE_PLAYER, "Player");
    nmo_object_id_t blocks[4] = {volume, first, second, player};
    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(blocks[i] != 0 && fixture_target(&f, blocks[i]) != 0);
        fixture_input_int(&f, fixture_target(&f, blocks[i]), (int32_t)sound);
    }
    fixture_link(&f, f.script_in, BLOCK_IN(&f, volume, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, volume, 0), BLOCK_IN(&f, first, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, first, 0), BLOCK_IN(&f, second, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, second, 0), BLOCK_IN(&f, player, 0), 0);
    input_float(&f, BLOCK_PIN(&f, volume, 0), 0.5f);
    for (int i = 1; i <= 2; i++) {
        input_float(&f, BLOCK_PIN(&f, blocks[i], 4), 50.0f); /* Min Delay */
        input_float(&f, BLOCK_PIN(&f, blocks[i], 5), 0.8f);  /* Volume */
    }
    input_float(&f, BLOCK_PIN(&f, player, 0), 100.0f); /* Fade In */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    size_t count = 0;
    const nmo_engine_voice_t *voices = nmo_engine_voices(f.engine, &count);
    /* The sound, and one instance: the second came within the first's Min Delay */
    ASSERT_EQ(2u, count);
    ASSERT_EQ(sound, voices[0].sound);
    ASSERT_STR_EQ("hit.wav", voices[0].file);
    ASSERT_FLOAT_EQ(25.0f, voices[0].position, 0.001f);
    ASSERT_FLOAT_EQ(0.5f * 0.25f, voices[0].gain, 0.0001f); /* a quarter into the fade in */
    ASSERT_EQ(sound, voices[1].sound);
    ASSERT_TRUE(voices[1].key != voices[0].key);
    ASSERT_FLOAT_EQ(0.8f, voices[1].gain, 0.0001f);
    fixture_end(&f);
}

TEST(engine_media, playing_sounds_get_a_velocity_from_their_moves)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sound = create_sound(&f, 1000);
    ASSERT_TRUE(sound != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t place = fixture_block(&f, GUID_POSITION_SOUND, "Place");
    nmo_object_id_t player = fixture_block(&f, GUID_WAVE_PLAYER, "Player");
    nmo_object_id_t blocks[2] = {place, player};
    for (int i = 0; i < 2; i++) {
        ASSERT_TRUE(blocks[i] != 0 && fixture_target(&f, blocks[i]) != 0);
        fixture_input_int(&f, fixture_target(&f, blocks[i]), (int32_t)sound);
    }
    fixture_link(&f, f.script_in, BLOCK_IN(&f, place, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, place, 0), BLOCK_IN(&f, player, 0), 0);
    const float position[3] = {5.0f, 0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, place, 1), position, sizeof(position));
    fixture_commit(&f, NULL);
    /* A point sound: the sound manager leaves background sounds where they are */
    eng_sound_t *made = eng_sound(f.engine, sound);
    ASSERT_NOT_NULL(made);
    made->type = CK_WAVESOUND_POINT;

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* From the origin to (5, 0, 0) in a frame: the move times the frame's 20 ms times 0.001 */
    const eng_sound_t *state = eng_sound(f.engine, sound);
    ASSERT_NOT_NULL(state);
    ASSERT_FLOAT_EQ(0.1f, state->velocity[0], 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, state->velocity[1], 0.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FLOAT_EQ(0.0f, eng_sound(f.engine, sound)->velocity[0], 0.0f);
    fixture_end(&f);
}

/* The files the engine loads, from the scratch directory */
static const char *resolve_scratch(void *user_data, const char *name)
{
    static char path[512];
    (void)user_data;
    int length = snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    return length > 0 && (size_t)length < sizeof(path) ? path : NULL;
}

/* The start of an AVI file: its RIFF header and main header, with `frames` frames */
static void write_movie(const char *name, uint32_t frames)
{
    uint8_t avi[32 + 56] = {0};
    memcpy(avi, "RIFF", 4);
    memcpy(avi + 8, "AVI LIST", 8);
    memcpy(avi + 20, "hdrlavih", 8);
    const uint32_t header_size = 56u, usec_per_frame = 40000u;
    memcpy(avi + 28, &header_size, 4);
    memcpy(avi + 32, &usec_per_frame, 4);
    memcpy(avi + 32 + 16, &frames, 4);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    FILE *out = fopen(path, "wb");
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(sizeof(avi), fwrite(avi, 1, sizeof(avi), out));
    fclose(out);
}

TEST(engine_media, movie_player_shows_the_movie_frames_over_its_duration)
{
    write_movie("movie.avi", 10u);
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Screen");
    ASSERT_TRUE(texture != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t missing = fixture_block(&f, GUID_MOVIE_LOAD, "Missing");
    nmo_object_id_t load = fixture_block(&f, GUID_MOVIE_LOAD, "Load");
    nmo_object_id_t player = fixture_block(&f, GUID_MOVIE_PLAYER, "Player");
    nmo_object_id_t played = fixture_block(&f, GUID_NOP, "Played");
    ASSERT_TRUE(missing != 0 && load != 0 && player != 0 && played != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, load, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, load, 0), BLOCK_IN(&f, player, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, player, 1), BLOCK_IN(&f, played, 0), 0);
    nmo_object_id_t blocks[3] = {missing, load, player};
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(fixture_target(&f, blocks[i]) != 0);
        fixture_input_int(&f, fixture_target(&f, blocks[i]), (int32_t)texture);
    }
    fixture_input_value(&f, BLOCK_PIN(&f, missing, 0), "none.avi", sizeof("none.avi"));
    fixture_input_value(&f, BLOCK_PIN(&f, load, 0), "movie.avi", sizeof("movie.avi"));
    input_float(&f, BLOCK_PIN(&f, player, 0), 1000.0f); /* Duration */
    fixture_input_int(&f, BLOCK_PIN(&f, player, 1), 0);  /* no Loop */
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* 10 frames over 1000 ms: a frame each 100 ms, from the first */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 100.0f));
    ASSERT_EQ(1u, runs(&f, player));
    ASSERT_EQ(0, nmo_engine_texture_current_slot(f.engine, texture));
    for (int frame = 1; frame <= 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 100.0f));
    }
    ASSERT_EQ(4, nmo_engine_texture_current_slot(f.engine, texture));
    for (int frame = 5; frame <= 9; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 100.0f));
    }
    ASSERT_EQ(9, nmo_engine_texture_current_slot(f.engine, texture));
    ASSERT_EQ(0u, runs(&f, played));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 100.0f));
    ASSERT_EQ(1u, runs(&f, played));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, player));
    fixture_end(&f);
}

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, 4);
}

/* A chunk header (a list's when `list` is set) at `at`; its size is set later */
static uint8_t *put_chunk(uint8_t *at, const char *id, const char *list)
{
    memcpy(at, id, 4);
    if (list != NULL) {
        memcpy(at + 8, list, 4);
        return at + 12;
    }
    return at + 8;
}

/*
 * An AVI of two 8x4 MS Video 1 frames. The first: the left block solid
 * red, the right one green in its two bottom rows and blue above. The
 * second skips the left block and fills the right one white.
 */
static void write_cram_movie(const char *name)
{
    static const uint8_t frame0[] = {0x00, 0xFC,             /* solid 0xFC00: red */
                                     0xFF, 0x00,             /* flags 0x00FF: two colors */
                                     0xE0, 0x03, 0x1F, 0x00, /* green, blue */
                                     0x00, 0x00};
    static const uint8_t frame1[] = {0x01, 0x84,  /* skip one block */
                                     0xFF, 0xFF}; /* solid white */
    uint8_t avi[12 + 12 + 64 + 12 + 64 + 48 + 12 + 8 + sizeof(frame0) + 8 + sizeof(frame1)] = {0};
    uint8_t *p = put_chunk(avi, "RIFF", "AVI ");
    uint8_t *hdrl = p;
    p = put_chunk(p, "LIST", "hdrl");
    put_u32(p + 4, 56);
    put_u32(p + 8, 40000);         /* microseconds a frame */
    put_u32(p + 24, 2);            /* frames */
    p = put_chunk(p, "avih", NULL) + 56;
    uint8_t *strl = p;
    p = put_chunk(p, "LIST", "strl");
    put_u32(p + 4, 56);
    memcpy(p + 8, "vidsMSVC", 8);
    p = put_chunk(p, "strh", NULL) + 56;
    put_u32(p + 4, 40);
    put_u32(p + 8, 40);            /* BITMAPINFOHEADER: its size, */
    put_u32(p + 12, 8);            /* width, */
    put_u32(p + 16, 4);            /* height, */
    put_u32(p + 20, 1u | 16u << 16); /* planes and bits, */
    memcpy(p + 24, "CRAM", 4);     /* compression */
    p = put_chunk(p, "strf", NULL) + 40;
    put_u32(strl + 4, (uint32_t)(p - strl) - 8);
    put_u32(hdrl + 4, (uint32_t)(p - hdrl) - 8);
    put_u32(p + 4, (uint32_t)(avi + sizeof(avi) - p) - 8);
    p = put_chunk(p, "LIST", "movi");
    put_u32(p + 4, sizeof(frame0));
    memcpy(put_chunk(p, "00dc", NULL), frame0, sizeof(frame0));
    p += 8 + sizeof(frame0);
    put_u32(p + 4, sizeof(frame1));
    memcpy(put_chunk(p, "00dc", NULL), frame1, sizeof(frame1));
    put_u32(avi + 4, (uint32_t)sizeof(avi) - 8);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    FILE *out = fopen(path, "wb");
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(sizeof(avi), fwrite(avi, 1, sizeof(avi), out));
    fclose(out);
}

static uint32_t image_pixel(const nmo_engine_image_t *image, int x, int y)
{
    const uint8_t *p = &image->rgba[((size_t)y * (size_t)image->width + (size_t)x) * 4u];
    return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

TEST(engine_media, movie_texture_shows_the_decoded_frame_of_its_slot)
{
    write_cram_movie("cram.avi");
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Screen");
    ASSERT_TRUE(texture != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t load = fixture_block(&f, GUID_MOVIE_LOAD, "Load");
    nmo_object_id_t player = fixture_block(&f, GUID_MOVIE_PLAYER, "Player");
    ASSERT_TRUE(load != 0 && player != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, load, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, load, 0), BLOCK_IN(&f, player, 0), 0);
    fixture_input_int(&f, fixture_target(&f, load), (int32_t)texture);
    fixture_input_int(&f, fixture_target(&f, player), (int32_t)texture);
    fixture_input_value(&f, BLOCK_PIN(&f, load, 0), "cram.avi", sizeof("cram.avi"));
    input_float(&f, BLOCK_PIN(&f, player, 0), 200.0f); /* Duration */
    fixture_input_int(&f, BLOCK_PIN(&f, player, 1), 0);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 50.0f));
    ASSERT_EQ(0, nmo_engine_texture_current_slot(f.engine, texture));
    nmo_engine_image_t image;
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_EQ(8, image.width);
    ASSERT_EQ(4, image.height);
    ASSERT_EQ(0xFF0000u, image_pixel(&image, 0, 0));
    ASSERT_EQ(0xFF0000u, image_pixel(&image, 3, 3));
    ASSERT_EQ(0x0000FFu, image_pixel(&image, 4, 0)); /* the right block's top rows */
    ASSERT_EQ(0x00FF00u, image_pixel(&image, 7, 3)); /* and its bottom ones */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 100.0f));
    ASSERT_EQ(1, nmo_engine_texture_current_slot(f.engine, texture));
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_EQ(0xFF0000u, image_pixel(&image, 0, 0)); /* kept from the first frame */
    ASSERT_EQ(0xFFFFFFu, image_pixel(&image, 4, 0));
    fixture_end(&f);
}

/* "key.bmp" in the scratch folder: a 2 x 1 24-bit BMP, black then white (bottom-up BGR rows
 * padded to 4 bytes) */
static void write_key_bmp(void)
{
    uint8_t bmp[54 + 8];
    memset(bmp, 0, sizeof(bmp));
    bmp[0] = 'B';
    bmp[1] = 'M';
    put_u32(bmp + 2, sizeof(bmp));
    put_u32(bmp + 10, 54);
    put_u32(bmp + 14, 40);
    put_u32(bmp + 18, 2);
    put_u32(bmp + 22, 1);
    put_u32(bmp + 26, 1u | 24u << 16);
    memset(bmp + 57, 0xFF, 3);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, "key.bmp");
    FILE *out = fopen(path, "wb");
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(sizeof(bmp), fwrite(bmp, 1, sizeof(bmp), out));
    fclose(out);
}

/* A texture of key.bmp, transparent with the file's default color */
static nmo_object_id_t key_texture(engine_fixture_t *f)
{
    nmo_object_id_t texture = fixture_create(f, NMO_CID_TEXTURE, "Keyed");
    nmo_texture_state_t *state = (nmo_texture_state_t *)fixture_state(f, texture);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    char **names = (char **)nmo_arena_alloc(arena, sizeof(char *), _Alignof(char *));
    if (state == NULL || names == NULL) {
        return 0;
    }
    names[0] = (char *)nmo_arena_alloc(arena, sizeof("key.bmp"), 1);
    if (names[0] == NULL) {
        return 0;
    }
    memcpy(names[0], "key.bmp", sizeof("key.bmp"));
    state->slot_count = 1;
    state->slot_filenames = names;
    state->has_slot_filenames = 1;
    state->is_transparent = 1;
    state->has_transparent_color = 0;
    return texture;
}

/* CKBitmapData keeps black as the transparent color when the file saves none */
TEST(engine_media, transparent_texture_clears_black_without_a_saved_color)
{
    write_key_bmp();
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t texture = key_texture(&f);
    ASSERT_TRUE(texture != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_image_t image;
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_EQ(2, image.width);
    ASSERT_EQ(0x000000u, image_pixel(&image, 0, 0));
    ASSERT_EQ(0, image.rgba[3]);
    ASSERT_EQ(0xFFFFFFu, image_pixel(&image, 1, 0));
    ASSERT_EQ(255, image.rgba[7]);
    fixture_end(&f);
}

/* CKTexture::UseMipmap: a texture saved without mipmaps gets them from TT_SetMipMapping,
 * which also sets its material's filters */
TEST(engine_media, set_mip_mapping_gives_the_texture_its_mipmaps)
{
    write_key_bmp();
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t texture = key_texture(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Keyed Material");
    nmo_material_state_t *material_state = (nmo_material_state_t *)fixture_state(&f, material);
    ASSERT_TRUE(texture != 0 && material_state != NULL);
    material_state->textures[0] = nmo_ref_from_id(texture);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t mip = fixture_block(&f, GUID_TT_SET_MIP_MAPPING, "Mip");
    ASSERT_TRUE(mip != 0 && fixture_target(&f, mip) != 0);
    fixture_input_int(&f, fixture_target(&f, mip), (int32_t)material);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, mip, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, mip, 0), 6); /* LINEARMIPLINEAR */
    fixture_input_int(&f, BLOCK_PIN(&f, mip, 1), 2); /* LINEAR */
    fixture_input_int(&f, BLOCK_PIN(&f, mip, 2), 1);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_image_t image;
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_FALSE(image.mipmapped);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, mip));
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_TRUE(image.mipmapped);
    nmo_engine_material_t desc;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    ASSERT_EQ(6, desc.min_filter);
    ASSERT_EQ(2, desc.mag_filter);
    fixture_end(&f);
}

/* A texture saving its own mipmap levels uses them (CKTexture::Load: SetUserMipMapMode), keyed
 * as its image is */
TEST(engine_media, user_mipmaps_are_decoded_and_keyed)
{
    write_key_bmp();
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t texture = key_texture(&f);
    nmo_texture_state_t *state = (nmo_texture_state_t *)fixture_state(&f, texture);
    ASSERT_TRUE(texture != 0 && state != NULL);
    /* Level 1 of the 2 x 1 image: one black texel, the transparent color */
    nmo_arena_t *arena = nmo_session_get_arena(f.session);
    nmo_texture_raw_slot_t *level = (nmo_texture_raw_slot_t *)nmo_arena_alloc(
        arena, sizeof(*level), _Alignof(nmo_texture_raw_slot_t));
    uint8_t *planes = (uint8_t *)nmo_arena_alloc(arena, 4u, 1);
    ASSERT_TRUE(level != NULL && planes != NULL);
    memset(level, 0, sizeof(*level));
    planes[0] = planes[1] = planes[2] = 0;
    planes[3] = 255;
    level->bits_per_pixel = 32;
    level->width = level->height = 1;
    level->blue_size = level->green_size = level->red_size = level->alpha_size = 1;
    level->blue_data = &planes[0];
    level->green_data = &planes[1];
    level->red_data = &planes[2];
    level->alpha_data = &planes[3];
    state->has_user_mipmaps = 1;
    state->user_mipmap_count = 1;
    state->user_mipmaps = level;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_image_t image;
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_TRUE(image.mipmapped);
    ASSERT_EQ(1, nmo_engine_texture_user_mipmap_count(f.engine, texture));
    ASSERT_TRUE(nmo_engine_texture_user_mipmap(f.engine, texture, 1, &image));
    ASSERT_EQ(1, image.width);
    ASSERT_EQ(1, image.height);
    ASSERT_EQ(0x000000u, image_pixel(&image, 0, 0));
    ASSERT_EQ(0, image.rgba[3]);
    ASSERT_FALSE(nmo_engine_texture_user_mipmap(f.engine, texture, 2, &image));
    fixture_end(&f);
}

/* A WAV file of `data_size` bytes at `bytes_per_second`, an odd-sized chunk before its data */
static void write_wave(const char *name, uint32_t bytes_per_second, uint32_t data_size)
{
    uint8_t wav[12 + 24 + 12 + 8] = {0};
    memcpy(wav, "RIFF", 4);
    memcpy(wav + 8, "WAVEfmt ", 8);
    const uint32_t format_size = 16u, list_size = 3u;
    memcpy(wav + 16, &format_size, 4);
    memcpy(wav + 28, &bytes_per_second, 4);
    memcpy(wav + 36, "LIST", 4);
    memcpy(wav + 40, &list_size, 4); /* 3 bytes, then a pad byte */
    memcpy(wav + 48, "data", 4);
    memcpy(wav + 52, &data_size, 4);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    FILE *out = fopen(path, "wb");
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(sizeof(wav), fwrite(wav, 1, sizeof(wav), out));
    fclose(out);
}

TEST(engine_media, sound_load_gives_the_sound_its_file_length)
{
    write_wave("tick.wav", 1000u, 100u); /* 100 ms */
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sound = create_sound(&f, 1000);
    nmo_object_id_t other = create_sound(&f, 1000); /* a failed load stops its sound too */
    ASSERT_TRUE(sound != 0 && other != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t load = fixture_block(&f, GUID_SOUND_LOAD, "Load");
    nmo_object_id_t missing = fixture_block(&f, GUID_SOUND_LOAD, "Missing");
    nmo_object_id_t player = fixture_block(&f, GUID_WAVE_PLAYER, "Wave Player");
    nmo_object_id_t ended = fixture_block(&f, GUID_NOP, "Ended");
    nmo_object_id_t failed = fixture_block(&f, GUID_NOP, "Failed");
    ASSERT_TRUE(load != 0 && missing != 0 && player != 0 && ended != 0 && failed != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, load, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, load, 0), BLOCK_IN(&f, player, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, player, 1), BLOCK_IN(&f, ended, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, failed, 0), 0);
    nmo_object_id_t targets[3] = {load, missing, player};
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(fixture_target(&f, targets[i]) != 0);
        fixture_input_int(&f, fixture_target(&f, targets[i]), (int32_t)(i == 1 ? other : sound));
    }
    fixture_input_value(&f, BLOCK_PIN(&f, load, 0), "tick.wav", sizeof("tick.wav"));
    fixture_input_value(&f, BLOCK_PIN(&f, missing, 0), "none.wav", sizeof("none.wav"));
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f)); /* frame 0: loaded, then played */
    ASSERT_EQ(1u, runs(&f, failed));
    ASSERT_TRUE(nmo_engine_sound_is_playing(f.engine, sound));
    /* 100 ms now, not the 1000 the document records */
    for (int frame = 1; frame < 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
        ASSERT_EQ(0u, runs(&f, ended));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_EQ(1u, runs(&f, ended));
    fixture_end(&f);
}

TEST(engine_media, play_animation_progresses_over_its_duration)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t entity = fixture_create(&f, NMO_CID_3DENTITY, "Entity");
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
    ASSERT_TRUE(entity != 0 && animation != 0);
    /* Over 10 frames: from the origin to x 10, turning a quarter about Y */
    static float positions[] = {0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f};
    static float rotations[] = {0.0f,  0.0f, 0.0f,        0.0f, 1.0f,
                                10.0f, 0.0f, 0.70710678f, 0.0f, 0.70710678f};
    static nmo_objanim_controller_t controllers[] = {
        {0x637c4301u, 2, sizeof(positions), positions}, /* linear position */
        {0x49ed4002u, 2, sizeof(rotations), rotations}, /* linear rotation */
    };
    nmo_objectanimation_state_t *state = (nmo_objectanimation_state_t *)fixture_state(&f, animation);
    ASSERT_NOT_NULL(state);
    state->entity = nmo_ref_from_id(entity);
    state->has_length = 1;
    state->length = 10.0f;
    state->controller_count = 2;
    state->controllers = controllers;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t play = fixture_block(&f, GUID_PLAY_ANIMATION, "Play");
    nmo_object_id_t played = fixture_block(&f, GUID_NOP, "Played");
    ASSERT_TRUE(play != 0 && played != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, play, 0), BLOCK_IN(&f, played, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, play, 0), (int32_t)animation);
    input_float(&f, BLOCK_PIN(&f, play, 1), 100.0f); /* Duration */
    fixture_input_int(&f, BLOCK_PIN(&f, play, 3), 0);  /* no Loop */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* From 0 in the first frame, a quarter each 25 ms */
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    }
    float progression = 0.0f;
    read_value(&f, BLOCK_POUT(&f, play, 0), &progression, sizeof(progression));
    ASSERT_FLOAT_EQ(0.5f, progression, 0.0001f);
    /* Halfway: at x 5, turned an eighth (the rotation's first row is (cos 45, 0, sin 45)) */
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, entity, world));
    ASSERT_FLOAT_EQ(5.0f, world[12], 0.0001f);
    ASSERT_FLOAT_EQ(0.70710678f, world[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.70710678f, world[2], 0.0001f);
    for (int frame = 3; frame < 5; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    }
    ASSERT_EQ(0u, runs(&f, played));
    /* Past the end: One Loop Played, then it stays at the end and stops */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_EQ(1u, runs(&f, played));
    read_value(&f, BLOCK_POUT(&f, play, 0), &progression, sizeof(progression));
    ASSERT_FLOAT_EQ(1.0f, progression, 0.0001f);
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, play));
    fixture_end(&f);
}

TEST(engine_media, play_global_animation_loops_over_its_duration)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t entity = fixture_create(&f, NMO_CID_3DENTITY, "Entity");
    nmo_object_id_t part = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Part");
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_KEYEDANIMATION, "Animation");
    ASSERT_TRUE(entity != 0 && part != 0 && animation != 0);
    /* Its one object animation turns about Y by TCB keys at frames 0, 4 and 10: 0, 90 and 180 degrees */
    static float keys[] = {
        0.0f,  0.0f, 0.0f,        0.0f, 1.0f,        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        4.0f,  0.0f, 0.70710678f, 0.0f, 0.70710678f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        10.0f, 0.0f, 1.0f,        0.0f, 0.0f,        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    };
    static nmo_objanim_controller_t controllers[] = {{0x45b52a02u, 3, sizeof(keys), keys}};
    nmo_objectanimation_state_t *part_state = (nmo_objectanimation_state_t *)fixture_state(&f, part);
    nmo_keyedanimation_state_t *state = (nmo_keyedanimation_state_t *)fixture_state(&f, animation);
    ASSERT_TRUE(part_state != NULL && state != NULL);
    part_state->entity = nmo_ref_from_id(entity);
    part_state->has_length = 1;
    part_state->length = 10.0f;
    part_state->controller_count = 1;
    part_state->controllers = controllers;
    static nmo_ref_t parts[1];
    parts[0] = nmo_ref_from_id(part);
    state->animation_count = 1;
    state->animation_ids = parts;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t play = fixture_block(&f, GUID_PLAY_GLOBAL_ANIMATION, "Play");
    nmo_object_id_t played = fixture_block(&f, GUID_NOP, "Played");
    ASSERT_TRUE(play != 0 && played != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, play, 0), BLOCK_IN(&f, played, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, play, 0), (int32_t)animation);
    input_float(&f, BLOCK_PIN(&f, play, 1), 100.0f); /* Duration; Loop by default */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* From 0 in the first frame, 0.4 each 40 ms: past 1 at the fourth frame, then on from 0.2 */
    for (int frame = 0; frame < 2; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 40.0f));
    }
    /* At 0.4, frame 4: the middle key's quarter turn, its first row (0, 0, 1) */
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, entity, world));
    ASSERT_FLOAT_EQ(0.0f, world[0], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, world[2], 0.0001f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 40.0f));
    /* At 0.8, frame 8: on the way to the half turn */
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, entity, world));
    ASSERT_TRUE(world[0] < -0.5f && world[0] > -1.0f);
    float progression = 0.0f;
    read_value(&f, BLOCK_POUT(&f, play, 0), &progression, sizeof(progression));
    ASSERT_FLOAT_EQ(0.8f, progression, 0.0001f);
    ASSERT_EQ(0u, runs(&f, played));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 40.0f));
    ASSERT_EQ(1u, runs(&f, played));
    read_value(&f, BLOCK_POUT(&f, play, 0), &progression, sizeof(progression));
    ASSERT_FLOAT_EQ(0.2f, progression, 0.0001f);
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, play));
    fixture_end(&f);
}

TEST(engine_media, time_settings_bound_and_scale_the_frame_time)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t settings = fixture_block(&f, GUID_TIME_SETTINGS, "Time Settings");
    ASSERT_TRUE(settings != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, settings, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, settings, 0), 3);  /* Frame Rate: limit */
    fixture_input_int(&f, BLOCK_PIN(&f, settings, 1), 30); /* Frame Limit Value */
    input_float(&f, BLOCK_PIN(&f, settings, 2), 2.0f);     /* Scale Time Value */
    input_float(&f, BLOCK_PIN(&f, settings, 5), 5.0f);     /* Min Delta Time */
    input_float(&f, BLOCK_PIN(&f, settings, 6), 100.0f);   /* Max Delta Time */
    fixture_commit(&f, NULL);

    /* CKTimeManager's defaults until it runs: synchronized to the screen, 1 to 200 ms */
    nmo_engine_time_settings_t t;
    nmo_engine_time_settings(f.engine, &t);
    ASSERT_EQ(0x11u, t.limit_options);
    ASSERT_FLOAT_EQ(150.0f, nmo_engine_frame_delta(f.engine, 150.0f), 0.0001f);
    ASSERT_FLOAT_EQ(200.0f, nmo_engine_frame_delta(f.engine, 250.0f), 0.0001f);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    nmo_engine_time_settings(f.engine, &t);
    ASSERT_EQ(0x40u, t.limit_options & 0xF0u);
    ASSERT_FLOAT_EQ(30.0f, t.frame_rate_limit, 0.0001f);
    /* Within 5 to 100 ms, then twice as long */
    ASSERT_FLOAT_EQ(10.0f, nmo_engine_frame_delta(f.engine, 1.0f), 0.0001f);
    ASSERT_FLOAT_EQ(40.0f, nmo_engine_frame_delta(f.engine, 20.0f), 0.0001f);
    ASSERT_FLOAT_EQ(200.0f, nmo_engine_frame_delta(f.engine, 500.0f), 0.0001f);
    fixture_end(&f);
}

/* A CKParameter chunk of CKStructSaver's: a buffer member (state 1) or an object (state 2) */
static nmo_chunk_t *struct_member(engine_fixture_t *f, uint32_t value, nmo_object_id_t object)
{
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(f->session));
    if (chunk == NULL || nmo_chunk_start_write(chunk) != NMO_OK ||
        nmo_chunk_write_identifier(chunk, 0x40u) != NMO_OK ||
        nmo_chunk_write_guid(chunk, NMO_GUID(0u, 0u)) != NMO_OK ||
        nmo_chunk_write_dword(chunk, object != 0 ? 2u : 1u) != NMO_OK) {
        return NULL;
    }
    nmo_status_t status = object != 0 ? nmo_chunk_write_object_id(chunk, object)
                                      : nmo_chunk_write_buffer(chunk, &value, sizeof(value));
    if (status != NMO_OK) {
        return NULL;
    }
    nmo_chunk_close(chunk);
    return chunk;
}

/* VXEFFECT_TEXGENREF: its "Tex coords gen+Referential" struct gives the generation and the entity */
TEST(engine_media, texgen_ref_material_reads_its_struct)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Chrome");
    nmo_object_id_t param = fixture_create(&f, NMO_CID_PARAMETER, "TexGen Params");
    nmo_object_id_t referential = fixture_create(&f, NMO_CID_3DENTITY, "Referential");
    nmo_material_state_t *material_state = (nmo_material_state_t *)fixture_state(&f, material);
    nmo_parameter_state_t *param_state = (nmo_parameter_state_t *)fixture_state(&f, param);
    ASSERT_TRUE(material_state != NULL && param_state != NULL && referential != 0);
    material_state->effect = 2u;
    material_state->has_effect = 1;
    material_state->has_effect_param = 1;
    material_state->effect_parameter = nmo_ref_from_id(param);
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(f.session));
    nmo_chunk_t *texgen = struct_member(&f, 3u, 0); /* chrome */
    nmo_chunk_t *entity = struct_member(&f, 0u, referential);
    ASSERT_TRUE(chunk != NULL && texgen != NULL && entity != NULL);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x12348765u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(chunk, texgen));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(chunk, entity));
    nmo_chunk_close(chunk);
    param_state->mode = CKPARAM_MODE_SUBCHUNK;
    param_state->subchunk = chunk;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);

    nmo_engine_material_t desc;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    ASSERT_EQ(3, desc.texgen);
    ASSERT_EQ(nmo_engine_object_named(f.engine, "Referential", 0), desc.texgen_referential);
    fixture_end(&f);
}

/* Blocks run one after the other from the script's start, each on `sound` when targetable */
static nmo_object_id_t chain_block(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                   nmo_object_id_t sound, nmo_object_id_t *previous)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    if (id == 0) {
        return 0;
    }
    nmo_object_id_t target = fixture_target(f, id);
    if (target != 0) {
        fixture_input_int(f, target, (int32_t)sound);
    }
    fixture_link(f, *previous != 0 ? BLOCK_OUT(f, *previous, 0) : f->script_in, BLOCK_IN(f, id, 0), 0);
    *previous = id;
    return id;
}

TEST(engine_media, sound_3d_settings_and_the_manager_reach_the_voices)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sound = create_sound(&f, 1000);
    nmo_object_id_t holder = fixture_create(&f, NMO_CID_3DENTITY, "Holder");
    nmo_object_id_t ear = fixture_create(&f, NMO_CID_3DENTITY, "Ear");
    nmo_wavesound_state_t *state = (nmo_wavesound_state_t *)fixture_state(&f, sound);
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(&f, holder);
    ASSERT_TRUE(state != NULL && entity != NULL && ear != 0);
    state->base.file_name = (char *)"hit.wav";
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1};
    memcpy(entity->world_matrix, world, sizeof(world));
    entity->has_entityndata_chunk = 1;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t previous = 0;
    nmo_object_id_t position = chain_block(&f, GUID_POSITION_SOUND, "Position Sound", sound, &previous);
    nmo_object_id_t type = chain_block(&f, GUID_SET_SOUND_TYPE, "Set Sound Type", sound, &previous);
    nmo_object_id_t range = chain_block(&f, GUID_SET_SOUND_RANGE, "Set Sound Range", sound, &previous);
    nmo_object_id_t cone = chain_block(&f, GUID_SET_SOUND_CONE_VALUES, "Set Sound Cone Values", sound, &previous);
    nmo_object_id_t listener = chain_block(&f, GUID_SET_LISTENER, "Set Listener", sound, &previous);
    nmo_object_id_t setup = chain_block(&f, GUID_SOUND_MANAGER_SETUP, "Sound Manager Setup", sound, &previous);
    nmo_object_id_t player = chain_block(&f, GUID_WAVE_PLAYER, "Player", sound, &previous);
    ASSERT_TRUE(position != 0 && type != 0 && range != 0 && cone != 0 && listener != 0 && setup != 0 &&
                player != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, position, 0), (int32_t)holder);
    const float at[3] = {1.0f, 0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, position, 1), at, sizeof(at));
    fixture_input_int(&f, BLOCK_PIN(&f, type, 0), 2); /* point */
    input_float(&f, BLOCK_PIN(&f, range, 0), 2.0f);
    input_float(&f, BLOCK_PIN(&f, range, 1), 50.0f);
    input_float(&f, BLOCK_PIN(&f, cone, 0), 1.0f);
    input_float(&f, BLOCK_PIN(&f, cone, 1), 2.0f);
    input_float(&f, BLOCK_PIN(&f, cone, 2), 0.25f);
    fixture_input_int(&f, BLOCK_PIN(&f, listener, 0), (int32_t)ear);
    input_float(&f, BLOCK_PIN(&f, setup, 3), 0.5f); /* Global Gain */
    input_float(&f, BLOCK_PIN(&f, player, 0), 0.0f); /* no fade in */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    size_t count = 0;
    const nmo_engine_voice_t *voices = nmo_engine_voices(f.engine, &count);
    ASSERT_EQ(1u, count);
    ASSERT_EQ(2u, voices[0].type);
    ASSERT_FLOAT_EQ(6.0f, voices[0].world_position[0], 1e-5f); /* Position in the holder's space */
    ASSERT_FLOAT_EQ(1.0f, voices[0].world_direction[2], 1e-5f);
    ASSERT_FLOAT_EQ(2.0f, voices[0].min_distance, 0.0f);
    ASSERT_FLOAT_EQ(50.0f, voices[0].max_distance, 0.0f);
    ASSERT_TRUE(voices[0].mute_after_max);
    ASSERT_FLOAT_EQ(57.295776f, voices[0].cone[0], 1e-4f); /* radians made degrees */
    ASSERT_FLOAT_EQ(0.25f, voices[0].cone[2], 0.0f);
    ASSERT_FLOAT_EQ(0.5f, voices[0].gain, 1e-6f); /* the global gain */
    nmo_object_id_t heard_by = 0;
    float distance_factor = 0.0f;
    ASSERT_TRUE(nmo_engine_sound_listener(f.engine, &heard_by, &distance_factor, NULL, NULL));
    ASSERT_EQ(ear, heard_by);
    ASSERT_FLOAT_EQ(1.0f, distance_factor, 0.0f); /* no Distance Factor given */
    fixture_end(&f);
}

TEST(engine_media, fade_out_lowers_the_global_gain_over_its_delay)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t fade = fixture_block(&f, GUID_FADE_OUT, "Fade Out");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    ASSERT_TRUE(fade != 0 && done != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, fade, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, fade, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, fade, 0), 20);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));

    float gain = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_sound_listener(f.engine, NULL, NULL, NULL, &gain));
    ASSERT_FLOAT_EQ(1.0f, gain, 0.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_sound_listener(f.engine, NULL, NULL, NULL, &gain));
    ASSERT_FLOAT_EQ(0.5f, gain, 1e-6f);
    ASSERT_EQ(0u, runs(&f, done));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_sound_listener(f.engine, NULL, NULL, NULL, &gain));
    ASSERT_FLOAT_EQ(0.0f, gain, 0.0f);
    ASSERT_EQ(1u, runs(&f, done));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_media, wave_player_ends_after_the_sound_length);
REGISTER_TEST(engine_media, voices_follow_the_sound_settings_and_instances);
    REGISTER_TEST(engine_media, playing_sounds_get_a_velocity_from_their_moves);
REGISTER_TEST(engine_media, particle_system_exits_off_once_its_particles_died);
REGISTER_TEST(engine_media, wave_particles_stay_where_they_were_emitted);
REGISTER_TEST(engine_media, gravity_and_global_wind_push_the_particles);
REGISTER_TEST(engine_media, a_mutation_box_brings_size_and_color_to_its_own);
REGISTER_TEST(engine_media, an_infinite_plane_deflector_sends_the_particles_back);
REGISTER_TEST(engine_media, a_sphere_deflector_kills_the_particles_that_hit_it);
REGISTER_TEST(engine_media, particles_draw_where_their_render_last_emitter_sorts);
REGISTER_TEST(engine_media, line_cubic_disc_and_cylinder_emitters_place_their_particles);
REGISTER_TEST(engine_media, movie_player_shows_the_movie_frames_over_its_duration);
REGISTER_TEST(engine_media, movie_texture_shows_the_decoded_frame_of_its_slot);
REGISTER_TEST(engine_media, transparent_texture_clears_black_without_a_saved_color);
REGISTER_TEST(engine_media, set_mip_mapping_gives_the_texture_its_mipmaps);
REGISTER_TEST(engine_media, user_mipmaps_are_decoded_and_keyed);
REGISTER_TEST(engine_media, play_animation_progresses_over_its_duration);
REGISTER_TEST(engine_media, play_global_animation_loops_over_its_duration);
REGISTER_TEST(engine_media, sound_load_gives_the_sound_its_file_length);
REGISTER_TEST(engine_media, time_settings_bound_and_scale_the_frame_time);
REGISTER_TEST(engine_media, texgen_ref_material_reads_its_struct);
    REGISTER_TEST(engine_media, sound_3d_settings_and_the_manager_reach_the_voices);
    REGISTER_TEST(engine_media, fade_out_lowers_the_global_gain_over_its_delay);
TEST_MAIN_END()
