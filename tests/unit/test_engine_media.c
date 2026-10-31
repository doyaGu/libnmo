#include "engine_fixture.h"

#include "object/builtin/nmo_sound_schemas.h"

#include <stdio.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_WAVE_PLAYER NMO_GUID(0x5bde0e45, 0x2e2107d5)
#define GUID_POINT_PARTICLES NMO_GUID(0x506b40f7, 0x30852e46)
#define GUID_MOVIE_LOAD NMO_GUID(0x798e26a9, 0x34e4169e)
#define GUID_MOVIE_PLAYER NMO_GUID(0x778d16d4, 0x1dd60060)
#define GUID_PLAY_ANIMATION NMO_GUID(0x64221225, 0x769a143f)
#define GUID_PLAY_GLOBAL_ANIMATION NMO_GUID(0x1c9236e1, 0x42f40996)
#define GUID_SOUND_LOAD NMO_GUID(0x02bc537c, 0x147c22e0)

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
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
    ASSERT_TRUE(animation != 0);
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
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_KEYEDANIMATION, "Animation");
    ASSERT_TRUE(animation != 0);
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
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 40.0f));
    }
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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_media, wave_player_ends_after_the_sound_length);
REGISTER_TEST(engine_media, particle_system_exits_off_once_its_particles_died);
REGISTER_TEST(engine_media, movie_player_shows_the_movie_frames_over_its_duration);
REGISTER_TEST(engine_media, play_animation_progresses_over_its_duration);
REGISTER_TEST(engine_media, play_global_animation_loops_over_its_duration);
REGISTER_TEST(engine_media, sound_load_gives_the_sound_its_file_length);
TEST_MAIN_END()
