/**
 * @file addons.c
 * @brief Ports of the BuildingBlocksAddons1 building blocks.
 *
 * Each function does what the block's function in BuildingBlocksAddons1.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ).
 */

#include "../engine_block_util.h"

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

/* Time Settings: the time manager's rates and limits (an effect on the scene). */
static int bb_time_settings(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_addons_blocks[] = {
    {NMO_GUID(0x798e26a9, 0x34e4169e), "Movie Load", bb_movie_load, NULL, NULL},
    {NMO_GUID(0x060a0f8c, 0x09203b5f), "Time Settings", bb_time_settings, NULL, NULL},
};

nmo_status_t eng_register_addons(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_addons_blocks);
}
