/**
 * @file addons.c
 * @brief Ports of the BuildingBlocksAddons1 building blocks.
 *
 * Each function does what the block's function in BuildingBlocksAddons1.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ).
 */

#include "virtools.h"

#include <math.h>
#include <string.h>

/* Movie Load (MovieLoader): the target texture plays the movie file. */
static int bb_movie_load(blk_t *b)
{
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    IN_OFF(b, 0);
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
    int32_t frames = name != NULL && size > 0 && memchr(name, '\0', size) != NULL
                         ? eng_movie_frame_count(b->engine, name)
                         : -1;
    if (frames < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    texture->movie = true;
    texture->slot_count = frames;
    texture->current_slot = 0;
    eng_movie_close(texture->movie_reader);
    texture->movie_reader = eng_movie_open(b->engine, name);
    eng_texture_changed(b->engine, texture->id);
    if (nmo_engine_block_output_parameter_count(b) > 0) {
        nmo_object_id_t id = texture->id;
        nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* An input of Time Settings that has a source: its value; else none (the setting stays) */
static bool time_input(blk_t *b, int pos, void *out, size_t size)
{
    return input_source(b, pos, false) != NULL &&
           nmo_engine_block_get_input_value(b, pos, out, size) == NMO_OK;
}

/*
 * Time Settings (TimeSettings): the time manager's frame and behavior rates,
 * limits, time scale and frame time bounds; an input with no source keeps
 * its setting (the absolute value of each number).
 */
static int bb_time_settings(blk_t *b)
{
    nmo_engine_time_settings_t *t = &b->engine->time_settings;
    int32_t frame = 0, behavior = 0, frame_limit = (int32_t)t->frame_rate_limit;
    int32_t behavior_limit = (int32_t)t->behavior_rate_limit;
    float scale = t->time_scale, min_delta = t->min_delta, max_delta = t->max_delta;
    uint32_t frame_option = t->limit_options & 0xF0u;
    bool frame_given = time_input(b, 0, &frame, sizeof(frame));
    time_input(b, 1, &frame_limit, sizeof(frame_limit));
    time_input(b, 2, &scale, sizeof(scale));
    time_input(b, 4, &behavior_limit, sizeof(behavior_limit));
    time_input(b, 5, &min_delta, sizeof(min_delta));
    time_input(b, 6, &max_delta, sizeof(max_delta));
    if (frame_given) {
        frame_option = frame == 1 ? 0x20u : frame == 2 ? 0x10u : 0x40u; /* free, sync, limit */
        if (frame != 1 && frame != 2 && frame_limit != 0) {
            t->frame_rate_limit = fabsf((float)frame_limit);
        }
    }
    t->time_scale = fabsf(scale);
    uint32_t behavior_option = t->limit_options & 0x0Fu;
    if (time_input(b, 3, &behavior, sizeof(behavior))) {
        behavior_option = behavior == 1 ? 0x1u : 0x4u;
        if (behavior != 1 && behavior_limit != 0) {
            t->behavior_rate_limit = fabsf((float)behavior_limit);
        }
    }
    t->limit_options = frame_option | behavior_option;
    if (fabsf(min_delta) > 0.0f) {
        t->min_delta = fabsf(min_delta);
    }
    if (fabsf(max_delta) > 0.0f) {
        t->max_delta = fabsf(max_delta);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_addons_blocks[] = {
    {NMO_GUID_INIT(0x798e26a9, 0x34e4169e), "Movie Load", bb_movie_load, NULL, NULL},
    {NMO_GUID_INIT(0x060a0f8c, 0x09203b5f), "Time Settings", bb_time_settings, NULL, NULL},
};

nmo_status_t eng_register_addons(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_addons_blocks);
}
