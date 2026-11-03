/* Unlimited Controller's historical records are 152-byte little-endian buffers. */
#include "virtools.h"
#include "core/nmo_utils.h"
#include "object/nmo_class_ids.h"

#include <math.h>
#include <string.h>

enum {
    UC_RECORD = 152,
    UC_LOOP = 1, UC_STOPABLE = 2, UC_WARP = 8, UC_PLAY = 16,
    UC_TURN = 32, UC_ORIENT = 64, UC_TIME = 128, UC_LOOPN = 512,
    UC_STAY = 1024, UC_WARPSTART = 2048, UC_WARPSAME = 4096,
};

static eng_param_t *uc_local(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine,node->first_local,node->local_count,pos);
}

static float uc_float(const uint8_t *bytes)
{
    uint32_t word = nmo_read_u32_le(bytes);
    float result;
    memcpy(&result,&word,4);
    return result;
}

static bool uc_valid(blk_t *b, int data, int count)
{
    eng_param_t *buffer = uc_local(b,data);
    int32_t n = -1;
    return nmo_engine_block_get_local_value(b,count,&n,sizeof(n)) == NMO_OK &&
        n >= 0 && buffer != NULL && (uint32_t)n <= buffer->size/UC_RECORD;
}

static nmo_object_id_t uc_indexed(blk_t *b, int32_t index)
{
    eng_param_t *array = uc_local(b,0);
    if (array == NULL || index < 0 || (uint32_t)index >= array->size/4) return 0;
    nmo_object_id_t id;
    memcpy(&id,array->data+4*(uint32_t)index,4);
    return eng_animation_state(b->engine,id) != NULL ? id : 0;
}

static void uc_restore(blk_t *b, int data, int count)
{
    eng_param_t *buffer = uc_local(b,data);
    for (int32_t i = 0, n = get_local_int(b,count,0); i < n; i++) {
        uint8_t *record = buffer->data+(size_t)i*UC_RECORD;
        char name[65];
        memcpy(name,record+72,64); name[64] = 0;
        nmo_write_u32_le(record+136,(uint32_t)(name[0] != 0 ? eng_message_type_add(b->engine,name) : -1));
        if (nmo_engine_block_version(b) <= 0x10000) {
            nmo_write_u16_le(record+54,0);
            memset(record+56,0xff,16);
        }
        nmo_object_id_t animation = (nmo_read_u16_le(record+54) & 1) ? 0 : uc_indexed(b,(int32_t)nmo_read_u32_le(record));
        nmo_write_u32_le(record+48,animation);
    }
}

static int uc_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD && message != NMO_ENGINE_MSG_RESET) return NMO_ENGINE_BR_OK;
    if (!uc_valid(b,1,2) || !uc_valid(b,6,7)) return NMO_ENGINE_BR_GENERICERROR;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    if (eng_entity_index(b->engine,owner) < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER)
        return NMO_ENGINE_BR_OWNERERROR;
    int32_t rotation[] = {eng_message_type_add(b->engine,"Joy_Left"),eng_message_type_add(b->engine,"Joy_Right")};
    nmo_engine_block_set_local_value(b,5,rotation,sizeof(rotation));
    uc_restore(b,1,2); uc_restore(b,6,7);
    nmo_engine_block_set_waiting_for_messages(b,owner,true);
    return NMO_ENGINE_BR_OK;
}

/* Preserve cached identity when deletion compacts, or editing reorders, Anims. */
static void uc_reconstruct(blk_t *b, int data, int count)
{
    eng_param_t *array = uc_local(b,0), *buffer = uc_local(b,data);
    for (int32_t i = 0, n = get_local_int(b,count,0); i < n; i++) {
        uint8_t *r = buffer->data+(size_t)i*UC_RECORD;
        if ((nmo_read_u16_le(r+54) & 1) && (int8_t)r[56] >= 0) continue;
        nmo_object_id_t cached = nmo_read_u32_le(r+48);
        int32_t found = -1;
        for (uint32_t j = 0; array != NULL && j < array->size/4; j++) {
            if (cached != 0 && uc_indexed(b,(int32_t)j) == cached) { found = (int32_t)j; break; }
        }
        nmo_write_u32_le(r,(uint32_t)found);
        if (found < 0) nmo_write_u32_le(r+48,0);
    }
}

static void uc_check(blk_t *b, int data, int count, nmo_object_id_t owner)
{
    eng_param_t *buffer = uc_local(b,data);
    for (int32_t i = 0, n = get_local_int(b,count,0); i < n; i++) {
        uint8_t *r = buffer->data+(size_t)i*UC_RECORD;
        if ((nmo_read_u16_le(r+54) & 1) && (int8_t)r[56] >= 0) continue;
        nmo_object_id_t cached = nmo_read_u32_le(r+48), indexed = uc_indexed(b,(int32_t)nmo_read_u32_le(r));
        const nmo_animation_state_t *state = eng_animation_state(b->engine,cached);
        nmo_object_id_t live = state != NULL ? cached : 0;
        if (live != indexed || (cached != 0 && live == 0)) {
            if (cached == 0) nmo_write_u32_le(r+48,indexed);
            else uc_reconstruct(b,data,count);
        } else if (state != NULL && eng_object_ref(b->engine,cached,&state->character) != owner) {
            nmo_write_u32_le(r,UINT32_MAX); nmo_write_u32_le(r+48,0);
        }
    }
}

static bool uc_received(blk_t *b, nmo_object_id_t owner, int32_t message)
{
    if (message < 0) return false;
    int count = nmo_engine_block_last_frame_message_count(b,owner);
    for (int i = 0; i < count; i++) if (nmo_engine_block_last_frame_message_type(b,owner,i) == message) return true;
    return false;
}

static nmo_object_id_t uc_animation(blk_t *b, const uint8_t *r)
{
    if (nmo_read_u16_le(r+54) != 0 && (int8_t)r[56] >= 0) {
        nmo_object_id_t id = nmo_engine_block_get_input_object(b,(int8_t)r[56]);
        return eng_animation_state(b->engine,id) != NULL ? id : 0;
    }
    return uc_indexed(b,(int32_t)nmo_read_u32_le(r));
}

static int32_t uc_int(blk_t *b, const uint8_t *r, int setting, int32_t fallback)
{
    return (nmo_read_u16_le(r+54) & (1u<<setting)) ? get_in_int(b,(int8_t)r[56+setting],fallback) : fallback;
}

static float uc_input_float(blk_t *b, const uint8_t *r, int setting, float fallback)
{
    return (nmo_read_u16_le(r+54) & (1u<<setting)) ? get_in_float(b,(int8_t)r[56+setting],fallback) : fallback;
}

static bool uc_primary(blk_t *b, nmo_object_id_t owner, const uint8_t *r)
{
    uint16_t flags = nmo_read_u16_le(r+148);
    uint32_t mode = CK_TRANSITION_FROMNOW;
    switch (flags & (UC_WARP | UC_WARPSTART | UC_WARPSAME)) {
    case UC_WARP: mode = CK_TRANSITION_WARPBEST; break;
    case UC_WARP | UC_WARPSTART: mode = CK_TRANSITION_WARPSTART; break;
    case UC_WARP | UC_WARPSAME: mode = CK_TRANSITION_WARPSAMEPOS; break;
    }
    nmo_object_id_t animation = uc_animation(b,r);
    float length = uc_input_float(b,r,2,uc_float(r+144));
    if (animation != 0) {
        uint32_t mask = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK | CKANIMATION_ALLOWTURN | CKANIMATION_ALIGNORIENTATION;
        uint32_t settings = (uc_int(b,r,4,flags & UC_TIME) ? CKANIMATION_LINKTOFRAMERATE : 0) |
            (uc_int(b,r,3,flags & UC_STOPABLE) ? CKANIMATION_CANBEBREAK : 0) |
            (uc_int(b,r,6,flags & UC_TURN) ? CKANIMATION_ALLOWTURN : 0) |
            (uc_int(b,r,7,flags & UC_ORIENT) ? CKANIMATION_ALIGNORIENTATION : 0);
        if (!eng_animation_set_playback(b->engine,animation,mask,settings,uc_input_float(b,r,5,uc_float(r+140)))) return false;
        mode = (uint32_t)uc_int(b,r,1,(int32_t)mode) | CK_TRANSITION_USEVELOCITY | CK_TRANSITION_LOOPIFEQUAL;
    } else mode = 0;
    eng_character_set_next_animation_warp(b->engine,owner,animation,mode,length);
    set_out_int(b,1,(int32_t)nmo_read_u32_le(r+136));
    return true;
}

static int uc_execute(blk_t *b)
{
    if (!uc_valid(b,1,2) || !uc_valid(b,6,7)) return NMO_ENGINE_BR_GENERICERROR;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    if (eng_entity_index(b->engine,owner) < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER)
        return NMO_ENGINE_BR_OWNERERROR;
    if (IN_ACTIVE(b,1)) {
        IN_OFF(b,1); OUT_ON(b,1);
        eng_character_stop_automatic(b->engine,owner);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b,0)) { IN_OFF(b,0); OUT_ON(b,0); }
    uc_check(b,1,2,owner); uc_check(b,6,7,owner);
    if (!eng_character_set_automatic(b->engine,owner,true)) return NMO_ENGINE_BR_GENERICERROR;
    eng_param_t *primary = uc_local(b,1);
    const uint8_t *selected = NULL, *fallback = NULL;
    for (int32_t i = 0, n = get_local_int(b,2,0); i < n; i++) {
        const uint8_t *r = primary->data+(size_t)i*UC_RECORD;
        int32_t message = (int32_t)nmo_read_u32_le(r+136);
        if (message < 0 && fallback == NULL) fallback = r;
        if (uc_received(b,owner,message)) { selected = r; break; }
    }
    if (selected == NULL) selected = fallback;
    if (selected != NULL && !uc_primary(b,owner,selected)) return NMO_ENGINE_BR_GENERICERROR;
    nmo_object_id_t current = eng_character_active_animation(b->engine,owner);
    float step = 0, frame = 0;
    eng_animation_current_step(b->engine,current,&step); eng_animation_frame(b->engine,current,&frame);
    set_out_int(b,0,(int32_t)current); set_out_float(b,2,step); set_out_float(b,3,frame);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

nmo_status_t eng_register_unlimited_controller(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x59537e89,0x79d02f25),
        "Unlimited Controller",uc_execute,uc_callback,NULL};
    return nmo_engine_register_block(engine,&block);
}
