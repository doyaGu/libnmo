#include "engine_fixture.h"

#include "object/builtin/nmo_sound_schemas.h"

#include <stdio.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_WAVE_PLAYER NMO_GUID(0x5bde0e45, 0x2e2107d5)
#define GUID_POINT_PARTICLES NMO_GUID(0x506b40f7, 0x30852e46)
#define GUID_MOVIE_LOAD NMO_GUID(0x798e26a9, 0x34e4169e)
#define GUID_MOVIE_PLAYER NMO_GUID(0x778d16d4, 0x1dd60060)

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
    uint32_t frames = 0;
    while (runs(&f, exit_off) == 0 && frames < 40) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
        frames++;
    }
    ASSERT_EQ(1u, runs(&f, exit_on));
    ASSERT_EQ(1u, runs(&f, exit_off));
    /* Off in the frame ending at 50 ms; the particles of that frame live 100 ms more */
    ASSERT_EQ(6u, frames);
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, ps));
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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_media, wave_player_ends_after_the_sound_length);
REGISTER_TEST(engine_media, particle_system_exits_off_once_its_particles_died);
REGISTER_TEST(engine_media, movie_player_shows_the_movie_frames_over_its_duration);
TEST_MAIN_END()
