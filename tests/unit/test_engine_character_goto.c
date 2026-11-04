#include "engine_fixture.h"
#include "../../src/engine/engine_character.h"
#include "../../src/engine/engine_transform.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_GOTO NMO_GUID(0x11223344,0xaabbccdd)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)
#define PI_F 3.14159265f

typedef struct message_log {
    int32_t types[16];
    nmo_object_id_t recipients[16], blocks[16];
    size_t count;
} message_log_t;

typedef struct goto_fixture {
    engine_fixture_t f;
    nmo_object_id_t body, target, block, branches[3];
    message_log_t log;
} goto_fixture_t;

static void log_messages(const nmo_engine_event_t *event, void *data)
{
    message_log_t *log = data;
    if (event->kind != NMO_ENGINE_EVENT_MESSAGE_SENT) return;
    if (log->count < COUNT(log->types)) {
        log->types[log->count] = event->result;
        log->recipients[log->count] = event->object_id;
        log->blocks[log->count] = event->behavior_id;
    }
    log->count++;
}

static nmo_object_id_t place(engine_fixture_t *f, nmo_object_id_t id, float x, float y, float z, nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->world_matrix[12] = x; state->world_matrix[13] = y; state->world_matrix[14] = z;
    state->parent = nmo_ref_from_id(parent); state->has_entityndata_chunk = 1;
    return id;
}

static nmo_object_id_t character(engine_fixture_t *f, const char *name, nmo_object_id_t *root)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CHARACTER,name);
    *root = fixture_create(f,NMO_CID_BODYPART,"Root");
    nmo_character_state_t *state = fixture_state(f,id);
    nmo_bodypart_state_t *body = fixture_state(f,*root);
    if (state == NULL || body == NULL || place(f,*root,0,7,0,id) == 0) return 0;
    state->root_body_part = nmo_ref_from_id(*root);
    body->has_character = 1; body->character = nmo_ref_from_id(id);
    nmo_array_dispose(&state->body_parts);
    if (nmo_array_alloc(&state->body_parts,sizeof(nmo_character_part_t),1,NULL) != NMO_OK) return 0;
    ((nmo_character_part_t *)state->body_parts.data)[0] = (nmo_character_part_t){.ref=nmo_ref_from_id(*root)};
    return id;
}

static void begin(goto_fixture_t *g, float x, float y, float z)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->f.owner_id = character(&g->f,"Actor",&g->body); ASSERT_TRUE(g->f.owner_id != 0);
    ASSERT_TRUE(place(&g->f,g->f.owner_id,100,0,100,0) != 0);
    g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Destination"); ASSERT_TRUE(place(&g->f,g->target,x,y,z,0) != 0);
    const char *names[] = {"Existing","Joy_Right","Other"}; fixture_install_messages(&g->f,names,COUNT(names));
    g->f.script_id = fixture_add_script(&g->f,"Go To Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_GOTO,"Go To"); ASSERT_TRUE(g->block != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->f.owner_id);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->target);
    for (size_t i = 0; i < COUNT(g->branches); i++) {
        g->branches[i] = fixture_block(&g->f,GUID_NOP,"Branch"); ASSERT_TRUE(g->branches[i] != 0);
        fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,i),BLOCK_IN(&g->f,g->branches[i],0),0);
    }
}

static void commit(goto_fixture_t *g, int input)
{
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,input),0);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options);
    ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_GOTO));
    nmo_engine_set_trace(g->f.engine,log_messages,&g->log);
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void expect(goto_fixture_t *g, int branch, const char *first, const char *second)
{
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
    ASSERT_EQ((size_t)(first != NULL)+(second != NULL),g->log.count);
    const char *names[] = {first,second};
    for (size_t i = 0; i < g->log.count; i++) {
        ASSERT_EQ(nmo_engine_message_type(g->f.engine,names[i]),g->log.types[i]);
        ASSERT_EQ(g->f.owner_id,g->log.recipients[i]); ASSERT_EQ(g->block,g->log.blocks[i]);
    }
    for (int i = 0; i < 3; i++) ASSERT_EQ(i == branch ? 1u : 0u,runs(&g->f,g->branches[i]));
    ASSERT_FALSE(nmo_engine_io_is_active(g->f.engine,BLOCK_IN(&g->f,g->block,0)));
    ASSERT_FALSE(nmo_engine_io_is_active(g->f.engine,BLOCK_IN(&g->f,g->block,1)));
    ASSERT_FALSE(nmo_engine_behavior_is_active(g->f.engine,g->block));
}

TEST(engine_character_goto, directions_reverse_and_message_order_match_native_steering)
{
    const struct { float x,z; int direction,reverse; const char *first,*second; } cases[] = {
        {30,0,1,0,"Joy_Up",NULL}, {0,30,1,0,"Joy_Left",NULL}, {0,-30,1,0,"Joy_Right",NULL},
        {30,20,1,0,"Joy_Up","Joy_Left"}, {-30,0,1,0,"Joy_Right",NULL},
        {-30,0,1,1,"Joy_Down",NULL}, {30,20,1,1,"Joy_Left",NULL},
        {-30,0,2,0,"Joy_Up",NULL}, {30,0,3,0,"Joy_Right",NULL}, {30,0,4,0,"Joy_Left",NULL},
        {0,30,5,0,"Joy_Up",NULL}, {30,0,5,0,"Joy_Right",NULL}, {0,-30,6,0,"Joy_Up",NULL},
    };
    for (size_t i = 0; i < COUNT(cases); i++) {
        goto_fixture_t g; begin(&g,cases[i].x,10000,cases[i].z);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),cases[i].direction);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),cases[i].reverse);
        commit(&g,(int)(i%2)); expect(&g,2,cases[i].first,cases[i].second);
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
        ASSERT_FLOAT_EQ(100,matrix[12],0); ASSERT_FLOAT_EQ(100,matrix[14],0);
        fixture_end(&g.f);
    }
}

TEST(engine_character_goto, arrival_boundaries_and_angle_clamp_preserve_turning)
{
    const struct { float x,z,distance,limit; int branch; const char *first,*second; } cases[] = {
        {12,0,12,1.22f,0,NULL,NULL}, {0,10,12,1.22f,0,"Joy_Left",NULL},
        {0,0,12,1.22f,0,NULL,NULL}, {0,0,-1,1.22f,2,NULL,NULL},
        {30,5,12,0,2,"Joy_Up",NULL}, {30,7,12,0,2,"Joy_Left",NULL},
        {-5,30,12,PI_F,2,"Joy_Up","Joy_Left"},
    };
    for (size_t i = 0; i < COUNT(cases); i++) {
        goto_fixture_t g; begin(&g,cases[i].x,-10000,cases[i].z);
        fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&cases[i].distance,sizeof(float));
        fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,3),&cases[i].limit,sizeof(float));
        commit(&g,0); expect(&g,cases[i].branch,cases[i].first,cases[i].second); fixture_end(&g.f);
    }
}

TEST(engine_character_goto, orientation_projects_normalized_world_axes_without_horizontal_renormalization)
{
    goto_fixture_t g; begin(&g,30,0,0);
    nmo_3dentity_state_t *actor = fixture_state(&g.f,g.f.owner_id);
    const float world[] = {1,1.73205081f,0,0, -2.59807621f,1.5f,0,0, 0,0,4,0, 100,0,100,1};
    memcpy(actor->world_matrix,world,sizeof(world));
    commit(&g,0); expect(&g,2,"Joy_Up","Joy_Right"); fixture_end(&g.f);
}

TEST(engine_character_goto, missing_or_deleted_targets_and_roots_take_no_object)
{
    for (int mode = 0; mode < 4; mode++) {
        goto_fixture_t g; begin(&g,30,0,0);
        if (mode == 0) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        if (mode == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)g.f.script_id);
        commit(&g,0);
        if (mode == 2) nmo_engine_destroy_object(g.f.engine,g.target);
        if (mode == 3) nmo_engine_destroy_object(g.f.engine,g.body);
        expect(&g,1,NULL,NULL); fixture_end(&g.f);
    }
}

TEST(engine_character_goto, missing_owners_report_owner_error_after_consuming_inputs)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        goto_fixture_t g; begin(&g,30,0,0);
        nmo_object_id_t root, target = deleted ? character(&g.f,"Target actor",&root) : 0;
        fixture_input_int(&g.f,fixture_target(&g.f,g.block),(int32_t)target); commit(&g,1);
        if (deleted) nmo_engine_destroy_object(g.f.engine,target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_EQ(0u,g.log.count);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1)));
        fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_character_goto, directions_reverse_and_message_order_match_native_steering);
REGISTER_TEST(engine_character_goto, arrival_boundaries_and_angle_clamp_preserve_turning);
REGISTER_TEST(engine_character_goto, orientation_projects_normalized_world_axes_without_horizontal_renormalization);
REGISTER_TEST(engine_character_goto, missing_or_deleted_targets_and_roots_take_no_object);
REGISTER_TEST(engine_character_goto, missing_owners_report_owner_error_after_consuming_inputs);
TEST_MAIN_END()
