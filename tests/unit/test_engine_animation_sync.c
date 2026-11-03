#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_character.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "core/nmo_utils.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_SYNC NMO_GUID(0x2c1835eb,0x68083d54)
#define GUID_WAIT NMO_GUID(0x4587ffee,0x4587ffdd)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct sync_config {
    uint8_t bytes[2048];
    size_t size, block;
    uint32_t animations, messages;
} sync_config_t;

typedef struct message_log {
    nmo_object_id_t block, recipient;
    int32_t types[128];
    size_t count;
} message_log_t;

static void log_messages(const nmo_engine_event_t *event, void *user_data)
{
    message_log_t *log = user_data;
    if (event->kind != NMO_ENGINE_EVENT_MESSAGE_SENT) return;
    log->block = event->behavior_id; log->recipient = event->object_id;
    if (log->count < COUNT(log->types)) log->types[log->count] = event->result;
    log->count++;
}

static void config_begin(sync_config_t *config)
{
    memset(config,0,sizeof(*config)); config->size = 8;
    nmo_write_u32_le(config->bytes,4);
}

static void config_animation(sync_config_t *config, int32_t index)
{
    ASSERT_TRUE(config->size+12 <= sizeof(config->bytes));
    config->block = config->size; config->messages = 0; config->size += 12;
    nmo_write_u32_le(config->bytes,(uint32_t)config->size);
    nmo_write_u32_le(config->bytes+4,++config->animations);
    nmo_write_u32_le(config->bytes+config->block,12);
    nmo_write_u32_le(config->bytes+config->block+4,(uint32_t)index);
}

static void config_message(sync_config_t *config, const char *name, const uint8_t *keys, uint32_t count)
{
    ASSERT_TRUE(config->size+72+count <= sizeof(config->bytes));
    uint8_t *record = config->bytes+config->size;
    snprintf((char *)record,64,"%s",name);
    nmo_write_u32_le(record+64,0x12345678); /* Rebuild saved message IDs on LOAD. */
    nmo_write_u32_le(record+68,count);
    memcpy(record+72,keys,count);
    config->size += 72+count;
    nmo_write_u32_le(config->bytes,(uint32_t)config->size);
    nmo_write_u32_le(config->bytes+config->block,(uint32_t)(config->size-config->block));
    nmo_write_u32_le(config->bytes+config->block+8,++config->messages);
}

static nmo_object_id_t local(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = fixture_state(f,block);
    return state != NULL && pos < state->local_parameters.count ? nmo_behavior_ref_array_get_id(&state->local_parameters,pos) : 0;
}

static void parameter_bytes(engine_fixture_t *f, nmo_object_id_t id, const void *bytes, size_t size)
{
    nmo_parameter_state_t *state = fixture_state(f,id);
    ASSERT_NOT_NULL(state);
    nmo_array_dispose(&state->buffer_data);
    ASSERT_EQ(NMO_OK,nmo_array_alloc(&state->buffer_data,1,size,NULL));
    if (size != 0) memcpy(state->buffer_data.data,bytes,size);
    state->mode = CKPARAM_MODE_BUFFER; state->has_state = true;
}

static void begin(engine_fixture_t *f)
{
    fixture_begin_document(f);
    const char *const names[] = {"Existing A","Existing B"};
    fixture_install_messages(f,names,COUNT(names));
    f->owner_id = fixture_create(f,NMO_CID_CHARACTER,"Actor");
    ASSERT_TRUE(f->owner_id != 0);
}

static nmo_object_id_t animation(engine_fixture_t *f, const char *name, float length)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,name);
    nmo_keyedanimation_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->base.has_character = state->base.has_length = state->base.has_data = 1;
    state->base.character = nmo_ref_from_id(f->owner_id);
    state->base.length = length;
    state->base.flags = CKANIMATION_LINKTOFRAMERATE;
    state->base.frame_rate = 1000;
    return id;
}

static nmo_object_id_t synchronizer(engine_fixture_t *f, const nmo_object_id_t *animations,
    size_t count, const sync_config_t *config)
{
    f->script_id = fixture_add_script(f,"Synchronizer Script",&f->script_in);
    fixture_begin_edit(f);
    nmo_object_id_t block = fixture_block(f,GUID_SYNC,"Synchronizer");
    if (block == 0) return 0;
    parameter_bytes(f,local(f,block,0),animations,count*4);
    if (config != NULL) parameter_bytes(f,local(f,block,1),config->bytes,config->size);
    fixture_link(f,f->script_in,BLOCK_IN(f,block,0),0);
    return block;
}

static void commit(engine_fixture_t *f, message_log_t *log)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(f,&options);
    nmo_engine_set_trace(f->engine,log_messages,log);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f->engine,NMO_ENGINE_START_ALL));
}

TEST(engine_animation_sync, linked_primary_frames_send_each_marked_key_in_message_order)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t clips[] = {animation(&f,"Primary",10),animation(&f,"Inactive",10)};
    sync_config_t config; config_begin(&config); config_animation(&config,0);
    const uint8_t all[] = {1,1,1,1,1,1}, odd[] = {0,1,0,1,0,1};
    config_message(&config,"Tick",all,COUNT(all)); config_message(&config,"Odd",odd,COUNT(odd));
    config_animation(&config,1); config_message(&config,"Inactive",all,COUNT(all));
    nmo_object_id_t block = synchronizer(&f,clips,COUNT(clips),&config);
    ASSERT_TRUE(block != 0);
    message_log_t log = {0}; commit(&f,&log);
    ASSERT_TRUE(nmo_engine_has_block(f.engine,GUID_SYNC));
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,f.owner_id,clips[0]));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,clips[0],3));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
    ASSERT_EQ(5u,log.count); ASSERT_EQ(block,log.block); ASSERT_EQ(f.owner_id,log.recipient);
    int32_t tick = nmo_engine_message_type(f.engine,"Tick"), odd_type = nmo_engine_message_type(f.engine,"Odd");
    ASSERT_TRUE(tick >= 2 && odd_type >= 2);
    for (int i = 0; i < 5; i++) ASSERT_EQ(i < 3 ? tick : odd_type,log.types[i]);
    ASSERT_EQ(1u,runs(&f,block));
    ASSERT_EQ(0x12345678u,nmo_read_u32_le(config.bytes+84));
    const nmo_parameter_state_t *source = fixture_state(&f,local(&f,block,1));
    ASSERT_EQ(0,memcmp(config.bytes,source->buffer_data.data,config.size));
    fixture_end(&f);
}

TEST(engine_animation_sync, empty_unconfigured_blocks_keep_running_without_messages)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t block = synchronizer(&f,NULL,0,NULL);
    ASSERT_TRUE(block != 0);
    message_log_t log = {0}; commit(&f,&log);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(2u,runs(&f,block)); ASSERT_EQ(0u,log.count);
    uint8_t runtime[8]; read_value(&f,local(&f,block,1),runtime,sizeof(runtime));
    ASSERT_EQ(4u,nmo_read_u32_le(runtime)); ASSERT_EQ(0u,nmo_read_u32_le(runtime+4));
    ASSERT_EQ(0u,((const nmo_parameter_state_t *)fixture_state(&f,local(&f,block,1)))->buffer_data.count);
    fixture_end(&f);
}

TEST(engine_animation_sync, malformed_blocks_fail_before_rebinding_or_sending_messages)
{
    for (int invalid = 0; invalid < 7; invalid++) {
        engine_fixture_t f;
        begin(&f);
        nmo_object_id_t clip = animation(&f,"Primary",10);
        sync_config_t config; config_begin(&config); config_animation(&config,0);
        const uint8_t keys[] = {1,1,1}; config_message(&config,"First",keys,COUNT(keys));
        config_animation(&config,0); config_message(&config,"Invalid",keys,COUNT(keys));
        size_t bad = config.block;
        switch (invalid) {
        case 0: config.size = 7; break;
        case 1: nmo_write_u32_le(config.bytes,UINT32_MAX); break;
        case 2: nmo_write_u32_le(config.bytes+4,UINT32_MAX); break;
        case 3: nmo_write_u32_le(config.bytes+bad,0); break;
        case 4: nmo_write_u32_le(config.bytes+bad,UINT32_MAX); break;
        case 5: nmo_write_u32_le(config.bytes+bad+8,UINT32_MAX); break;
        case 6: nmo_write_u32_le(config.bytes+bad+12+68,UINT32_MAX); break;
        }
        nmo_object_id_t block = synchronizer(&f,&clip,1,&config);
        ASSERT_TRUE(block != 0);
        message_log_t log = {0}; commit(&f,&log);
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,f.owner_id,clip));
        ASSERT_TRUE(eng_animation_set_frame(f.engine,clip,2));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,1));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(f.engine,&failure));
        ASSERT_EQ(block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_GENERICERROR,failure.result);
        ASSERT_EQ(0u,log.count);
        if (invalid >= 2) {
            uint8_t runtime[2048]; read_value(&f,local(&f,block,1),runtime,config.size);
            ASSERT_EQ(0x12345678u,nmo_read_u32_le(runtime+84));
        }
        fixture_end(&f);
    }
}

TEST(engine_animation_sync, frame_windows_preserve_endpoint_pause_loop_reverse_and_unlinked_rules)
{
    const struct { float frame, delta, fps; bool linked; size_t messages; } cases[] = {
        {5.5f,2,1000,true,2}, {5,100,0,true,1}, {5,1000000,30,false,2},
        {5,0,30,false,2}, {10,5,1000,true,5}, {10,100,0,true,0},
        {0,2,1000,true,1}, {1,15,1000,true,2}, {5,2,-1000,true,0},
        {5,0,-1000,true,1}, {-2.5f,1,1000,true,0}, {-.5f,2,1000,true,0},
        {FLT_MAX,0,30,false,0},
    };
    for (size_t c = 0; c < COUNT(cases); c++) {
        engine_fixture_t f;
        begin(&f);
        nmo_object_id_t clip = animation(&f,"Primary",10);
        sync_config_t config; config_begin(&config); config_animation(&config,0);
        const uint8_t keys[] = {1,1,1,1,1,0xff,1,1,1,1,1};
        config_message(&config,"Tick",keys,COUNT(keys));
        nmo_object_id_t block = synchronizer(&f,&clip,1,&config);
        ASSERT_TRUE(block != 0);
        message_log_t log = {0}; commit(&f,&log);
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,f.owner_id,clip));
        ASSERT_TRUE(eng_animation_set_current_step(f.engine,clip,cases[c].frame/10));
        ASSERT_TRUE(eng_animation_set_playback(f.engine,clip,CKANIMATION_LINKTOFRAMERATE,
            cases[c].linked ? CKANIMATION_LINKTOFRAMERATE : 0,cases[c].fps));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,cases[c].delta));
        ASSERT_EQ(cases[c].messages,log.count);
        ASSERT_FLOAT_EQ(1000,eng_animation_state(f.engine,clip)->frame_rate,0);
        fixture_end(&f);
    }
}

TEST(engine_animation_sync, primary_and_secondary_membership_marks_each_configured_record_once)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t a = animation(&f,"Primary",10), b = animation(&f,"Secondary",10), inactive = animation(&f,"Inactive",10);
    nmo_object_id_t clips[] = {a,b,inactive,UINT32_MAX,a};
    sync_config_t config; config_begin(&config);
    const uint8_t keys[] = {1,1,1,1,1,1,1,1,1,1,1};
    for (int32_t i = 0; i < 5; i++) {
        config_animation(&config,i);
        config_message(&config,i == 1 ? "Secondary Tick" : "Primary Tick",keys,COUNT(keys));
    }
    nmo_object_id_t block = synchronizer(&f,clips,COUNT(clips),&config);
    ASSERT_TRUE(block != 0);
    message_log_t log = {0}; commit(&f,&log);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,f.owner_id,a));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,f.owner_id,a,2.5f,CKSECONDARYANIMATION_LASTFRAME,0));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,f.owner_id,b,4.5f,CKSECONDARYANIMATION_LASTFRAME,0));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(3u,log.count);
    ASSERT_EQ(nmo_engine_message_type(f.engine,"Secondary Tick"),log.types[1]);
    nmo_engine_destroy_object(f.engine,b);
    log.count = 0;
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(2u,log.count);
    eng_character_flush_secondary(f.engine,f.owner_id);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,f.owner_id,0));
    log.count = 0;
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(0u,log.count);
    fixture_end(&f);
}

TEST(engine_animation_sync, primary_warps_only_emit_for_the_current_warper)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t clips[] = {animation(&f,"Source",10),animation(&f,"Destination",10)};
    sync_config_t config; config_begin(&config);
    const uint8_t keys[] = {1,1,1,1};
    config_animation(&config,0); config_message(&config,"Source Tick",keys,COUNT(keys));
    config_animation(&config,1); config_message(&config,"Destination Tick",keys,COUNT(keys));
    nmo_object_id_t block = synchronizer(&f,clips,COUNT(clips),&config);
    ASSERT_TRUE(block != 0);
    message_log_t log = {0}; commit(&f,&log);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,f.owner_id,clips[0]));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,clips[0],2));
    ASSERT_TRUE(eng_animation_set_playback(f.engine,clips[0],CKANIMATION_CANBEBREAK,CKANIMATION_CANBEBREAK,1000));
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(f.engine,f.owner_id,clips[1],CK_TRANSITION_WARPSTART,2));
    nmo_object_id_t warper = eng_character_warper(f.engine,f.owner_id);
    ASSERT_TRUE(warper != 0); ASSERT_EQ(warper,eng_character_active_animation(f.engine,f.owner_id));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(0u,log.count);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,local(&f,block,0),&warper,4));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(1u,log.count);
    ASSERT_EQ(nmo_engine_message_type(f.engine,"Source Tick"),log.types[0]);
    nmo_engine_destroy_object(f.engine,warper);
    log.count = 0;
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(0u,log.count);
    fixture_end(&f);
}

TEST(engine_animation_sync, off_stops_synchronization_without_stopping_automatic_character_playback)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t clip = animation(&f,"Primary",10);
    sync_config_t config; config_begin(&config); config_animation(&config,0);
    const uint8_t keys[] = {1,1,1,1,1,1,1,1,1,1,1};
    config_message(&config,"Tick",keys,COUNT(keys));
    nmo_object_id_t block = synchronizer(&f,&clip,1,&config);
    ASSERT_TRUE(block != 0);
    fixture_link(&f,f.script_in,BLOCK_IN(&f,block,1),1);
    message_log_t log = {0}; commit(&f,&log);
    ASSERT_TRUE(eng_character_set_automatic(f.engine,f.owner_id,true));
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,f.owner_id,clip));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,clip,5));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(1u,log.count);
    /* Off takes priority even if the configuration is damaged meanwhile. */
    uint8_t invalid[7] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,local(&f,block,1),invalid,sizeof(invalid)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
    ASSERT_EQ(2u,runs(&f,block)); ASSERT_EQ(1u,log.count);
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine,block));
    ASSERT_TRUE(eng_character_automatic(f.engine,f.owner_id));
    float frame = 0; ASSERT_TRUE(eng_animation_frame(f.engine,clip,&frame)); ASSERT_FLOAT_EQ(8,frame,0);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_animation_sync, linked_primary_frames_send_each_marked_key_in_message_order);
REGISTER_TEST(engine_animation_sync, empty_unconfigured_blocks_keep_running_without_messages);
REGISTER_TEST(engine_animation_sync, malformed_blocks_fail_before_rebinding_or_sending_messages);
REGISTER_TEST(engine_animation_sync, frame_windows_preserve_endpoint_pause_loop_reverse_and_unlinked_rules);
REGISTER_TEST(engine_animation_sync, primary_and_secondary_membership_marks_each_configured_record_once);
REGISTER_TEST(engine_animation_sync, primary_warps_only_emit_for_the_current_warper);
REGISTER_TEST(engine_animation_sync, off_stops_synchronization_without_stopping_automatic_character_playback);
TEST_MAIN_END()
