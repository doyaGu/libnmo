/* Animation Synchronizer: character frame markers send messages to their owner. */
#include "virtools.h"
#include "core/nmo_utils.h"

#include <math.h>
#include <string.h>

static eng_param_t *sync_local(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine,node->first_local,node->local_count,pos);
}

/* Validate the whole buffer before rebuilding IDs or sending any messages.
 * Accept payload and total byte counts: the CREATE callback's empty buffer
 * is [4,0]. Sub-block sizes include their own size word. */
static bool sync_valid(const eng_param_t *data)
{
    if (data == NULL || data->size < 8) return false;
    uint32_t size = nmo_read_u32_le(data->data), count = nmo_read_u32_le(data->data+4);
    if (size < 4 || size > data->size) return false;
    size_t end = size <= data->size-4 ? (size_t)size+4 : size;
    if (end < 8 || count > (end-8)/12) return false;
    size_t cursor = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (end-cursor < 12) return false;
        uint32_t block_size = nmo_read_u32_le(data->data+cursor);
        if (block_size < 12 || block_size > end-cursor) return false;
        size_t block_end = cursor+block_size;
        uint32_t messages = nmo_read_u32_le(data->data+cursor+8);
        cursor += 12;
        if (messages > (block_end-cursor)/72) return false;
        for (uint32_t j = 0; j < messages; j++) {
            if (block_end-cursor < 72) return false;
            uint32_t keys = nmo_read_u32_le(data->data+cursor+68);
            cursor += 72;
            if (keys > block_end-cursor) return false;
            cursor += keys;
        }
        if (cursor != block_end) return false;
    }
    return true;
}

static bool sync_active(blk_t *b, nmo_object_id_t owner, nmo_object_id_t animation)
{
    if (animation == 0) return false;
    if (animation == eng_character_active_animation(b->engine,owner)) return true;
    size_t count = eng_character_secondary_count(b->engine,owner);
    for (size_t i = 0; i < count; i++) if (animation == eng_character_secondary_animation(b->engine,owner,i)) return true;
    return false;
}

static int sync_walk(blk_t *b, bool restore)
{
    eng_param_t *data = sync_local(b,1), *array = sync_local(b,0);
    if (!sync_valid(data)) return NMO_ENGINE_BR_GENERICERROR;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    size_t cursor = 8;
    uint32_t count = nmo_read_u32_le(data->data+4);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t block_size = nmo_read_u32_le(data->data+cursor);
        uint32_t index = nmo_read_u32_le(data->data+cursor+4);
        uint32_t messages = nmo_read_u32_le(data->data+cursor+8);
        nmo_object_id_t animation = 0;
        if (array != NULL && index < array->size/4) memcpy(&animation,array->data+4*(size_t)index,4);
        bool active = !restore && sync_active(b,owner,animation);
        float current = 0, previous = 0;
        if (active) {
            const nmo_animation_state_t *state = eng_animation_playback_state(b->engine,animation);
            if (state == NULL || !eng_animation_frame(b->engine,animation,&current)) active = false;
            else {
                if (current == eng_animation_length(state)) current += .01f;
                previous = current-((state->flags & CKANIMATION_LINKTOFRAMERATE)
                    ? nmo_engine_block_delta_time(b)*state->frame_rate/1000 : 1);
                if (!isfinite(current) || !isfinite(previous)) return NMO_ENGINE_BR_GENERICERROR;
            }
        }
        size_t message = cursor+12;
        for (uint32_t j = 0; j < messages; j++) {
            uint8_t *record = data->data+message;
            uint32_t keys = nmo_read_u32_le(record+68);
            if (restore) {
                char name[65];
                memcpy(name,record,64); name[64] = 0;
                int32_t type = name[0] != 0 ? eng_message_type_add(b->engine,name) : -1;
                nmo_write_u32_le(record+64,(uint32_t)type);
            } else if (active) {
                /* Bound the native integer scan without overflowing on wide
                 * cursors. Its inclusive frame test intentionally repeats keys
                 * at the previous endpoint and does not scan a loop's old tail. */
                double start = fmax(0,trunc((double)previous)-2);
                double end = fmin(keys,trunc((double)current)+2);
                int32_t type = (int32_t)nmo_read_u32_le(record+64);
                if (start < end && type >= 0)
                    for (size_t k = (size_t)start; k < (size_t)end; k++) {
                        float key = (float)k;
                        if (record[72+k] && key >= previous && key <= current)
                            nmo_engine_block_send_message(b,type,owner,owner);
                    }
            }
            message += 72+keys;
        }
        cursor += block_size;
    }
    return NMO_ENGINE_BR_OK;
}

static int sync_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) return NMO_ENGINE_BR_OK;
    eng_param_t *data = sync_local(b,1);
    if (data != NULL && data->size == 0) {
        const uint8_t empty[] = {4,0,0,0,0,0,0,0};
        if (nmo_engine_block_set_local_value(b,1,empty,sizeof(empty)) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    }
    return sync_walk(b,true);
}

static int sync_execute(blk_t *b)
{
    if (IN_ACTIVE(b,1)) { IN_OFF(b,1); return NMO_ENGINE_BR_OK; }
    IN_OFF(b,0);
    if (nmo_engine_block_owner(b) == 0) return NMO_ENGINE_BR_OK;
    int result = sync_walk(b,false);
    return result == NMO_ENGINE_BR_OK ? NMO_ENGINE_BR_ACTIVATENEXTFRAME : result;
}

nmo_status_t eng_register_animation_sync(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x2c1835eb,0x68083d54),
        "Animation Synchronizer",sync_execute,sync_callback,NULL};
    return nmo_engine_register_block(engine,&block);
}
