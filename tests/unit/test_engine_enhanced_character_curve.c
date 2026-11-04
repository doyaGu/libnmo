#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_character.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_module.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_FOLLOW NMO_GUID(0x785e7760,0x46ea50b5)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct enhanced_fixture {
    engine_fixture_t f;
    nmo_object_id_t root, path, block, done, again;
    size_t messages;
} enhanced_fixture_t;

static nmo_object_id_t path(engine_fixture_t *f, const eng_vector_t *points, size_t count)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CURVE,"Path");
    nmo_curve_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->control_point_count = (uint32_t)count;
    state->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(f->session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    if (count != 0 && state->control_point_ids == NULL) return 0;
    for (size_t i = 0; i < count; i++) {
        nmo_object_id_t point = fixture_create(f,NMO_CID_CURVEPOINT,"Point");
        nmo_curvepoint_state_t *p = fixture_state(f,point);
        if (p == NULL) return 0;
        p->base.parent = nmo_ref_from_id(id); p->base.has_entityndata_chunk = 1;
        p->curve = nmo_ref_from_id(id); p->linear = 1;
        nmo_curvepoint_set_position(p,points[i].x,points[i].y,points[i].z);
        state->control_point_ids[i] = nmo_ref_from_id(point);
    }
    return id;
}

static nmo_object_id_t clip(enhanced_fixture_t *g, const char *name, float start, float end)
{
    engine_fixture_t *f = &g->f;
    nmo_object_id_t channel = fixture_create(f,NMO_CID_OBJECTANIMATION,name);
    nmo_objectanimation_state_t *part = fixture_state(f,channel);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    float *keys = nmo_arena_alloc(arena,8*sizeof(float),_Alignof(float));
    nmo_objanim_controller_t *controller = nmo_arena_alloc(arena,sizeof(*controller),_Alignof(nmo_objanim_controller_t));
    if (part == NULL || keys == NULL || controller == NULL) return 0;
    float values[] = {0,start,0,0,20,end,0,0}; memcpy(keys,values,sizeof(values));
    *controller = (nmo_objanim_controller_t){0x637c4301u,2,sizeof(values),keys};
    part->format = CKOBJANIM_FORMAT_CONTROLLERS; part->has_length = 1; part->length = 20;
    part->entity = nmo_ref_from_id(g->root); part->controller_count = 1; part->controllers = controller;
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,name);
    nmo_keyedanimation_state_t *animation = fixture_state(f,id);
    if (animation == NULL) return 0;
    animation->animation_ids = nmo_arena_alloc(arena,sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    if (animation->animation_ids == NULL) return 0;
    *animation->animation_ids = nmo_ref_from_id(channel); animation->animation_count = 1;
    animation->base.has_data = animation->base.has_length = animation->base.has_character = animation->base.has_root_entity = 1;
    animation->base.length = 20; animation->base.character = nmo_ref_from_id(f->owner_id); animation->base.root_entity = nmo_ref_from_id(g->root);
    animation->base.flags = CKANIMATION_LINKTOFRAMERATE; animation->base.frame_rate = 1000;
    return id;
}

static void begin(enhanced_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->f.owner_id = fixture_create(&g->f,NMO_CID_CHARACTER,"Actor"); g->root = fixture_create(&g->f,NMO_CID_BODYPART,"Root");
    nmo_character_state_t *actor = fixture_state(&g->f,g->f.owner_id); ASSERT_NOT_NULL(actor);
    nmo_bodypart_state_t *body = fixture_state(&g->f,g->root); ASSERT_NOT_NULL(body);
    actor->root_body_part = nmo_ref_from_id(g->root); actor->base.world_matrix[13] = 1;
    nmo_3dentity_state_t *root = (nmo_3dentity_state_t *)body;
    root->parent = nmo_ref_from_id(g->f.owner_id); root->has_entityndata_chunk = 1;
    root->world_matrix[12] = 2; root->world_matrix[13] = 3;
    body->has_character = 1; body->character = nmo_ref_from_id(g->f.owner_id);
    const eng_vector_t points[] = {{0,20,0},{20,20,0}};
    g->path = path(&g->f,points,COUNT(points)); ASSERT_TRUE(g->path != 0);
    g->f.script_id = fixture_add_script(&g->f,"Enhanced script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_FOLLOW,"Enhanced follow");
    g->done = fixture_block(&g->f,GUID_NOP,"Done"); g->again = fixture_block(&g->f,GUID_NOP,"Again");
    ASSERT_TRUE(g->block != 0 && g->done != 0 && g->again != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->f.owner_id);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->path);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,1),BLOCK_IN(&g->f,g->block,1),1);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->done,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,1),BLOCK_IN(&g->f,g->again,0),0);
}

static nmo_object_id_t local(enhanced_fixture_t *g, size_t index)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,index);
}

static void range(enhanced_fixture_t *g, float start, float end)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,1),&start,sizeof(start));
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,2),&end,sizeof(end));
}

static void log_messages(const nmo_engine_event_t *event, void *data)
{
    enhanced_fixture_t *g = data;
    if (event->kind == NMO_ENGINE_EVENT_MESSAGE_SENT) g->messages++;
}

static void commit(enhanced_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_FOLLOW));
    nmo_engine_set_trace(g->f.engine,log_messages,g); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static float value(enhanced_fixture_t *g, nmo_object_id_t parameter)
{
    float result; read_value(&g->f,parameter,&result,sizeof(result)); return result;
}

static void position(enhanced_fixture_t *g, float x, float y, float z)
{
    float m[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,g->f.owner_id,m));
    ASSERT_FLOAT_EQ(x,m[12],1e-5f); ASSERT_FLOAT_EQ(y,m[13],1e-5f); ASSERT_FLOAT_EQ(z,m[14],1e-5f);
}

static void move(enhanced_fixture_t *g, float x, float y, float z)
{
    const eng_vector_t p = {x,y,z}; eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,g->f.owner_id),&p,-1,false);
}

TEST(engine_enhanced_character_curve, initialization_preserves_output_and_uses_all_six_native_axes)
{
    const float matrices[][9] = {
        {1,0,0, 0,1,0, 0,0,1}, {-1,0,0, 0,1,0, 0,0,-1}, {0,0,1, 1,0,0, 0,1,0},
        {0,0,-1, 1,0,0, 0,-1,0}, {0,0,-1, 0,1,0, 1,0,0}, {0,0,1, 0,1,0, -1,0,0},
    };
    for (size_t i = 0; i < COUNT(matrices); i++) {
        enhanced_fixture_t g; begin(&g); range(&g,.25f,.75f);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),(int32_t)i+1);
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,local(&g,0),"99.5")); commit(&g);
        float previous = .125f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&previous,sizeof(previous)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,5,1,0);
        ASSERT_FLOAT_EQ(previous,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_FLOAT_EQ(99.5f,value(&g,local(&g,0)),0);
        ASSERT_FLOAT_EQ(5,value(&g,local(&g,2)),0); ASSERT_FLOAT_EQ(15,value(&g,local(&g,3)),0);
        int32_t ascending = 0; read_value(&g.f,local(&g,4),&ascending,sizeof(ascending)); ASSERT_EQ(1,ascending);
        eng_vector_t old; read_value(&g.f,local(&g,1),&old,sizeof(old)); ASSERT_FLOAT_EQ(20,old.y,0);
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
        for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++)
            ASSERT_FLOAT_EQ(matrices[i][3*row+column],matrix[4*row+column],1e-5f);
        ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(0u,g.messages);
        ASSERT_EQ(1u,runs(&g.f,g.again)); ASSERT_EQ(0u,runs(&g.f,g.done)); fixture_end(&g.f);
    }
}

TEST(engine_enhanced_character_curve, forward_end_is_inclusive_and_uses_cached_bounds)
{
    enhanced_fixture_t g; begin(&g); range(&g,.25f,.75f); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    nmo_object_id_t end = nmo_parameterin_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,2)));
    float changed = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,end,&changed,sizeof(changed)));
    move(&g,8,30,0); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,8,30,0);
    ASSERT_FLOAT_EQ(.4f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id));
    move(&g,15,40,0); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,15,40,0);
    ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_EQ(1u,runs(&g.f,g.done));
    ASSERT_FLOAT_EQ(15,value(&g,local(&g,2)),0); ASSERT_EQ(0u,g.messages);
    eng_vector_t old; read_value(&g.f,local(&g,1),&old,sizeof(old)); ASSERT_FLOAT_EQ(8,old.x,0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(3u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_enhanced_character_curve, reverse_end_is_strict_and_does_not_reverse_the_tangent)
{
    enhanced_fixture_t g; begin(&g); range(&g,.75f,.25f); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    int32_t ascending = 1; read_value(&g.f,local(&g,4),&ascending,sizeof(ascending)); ASSERT_EQ(0,ascending);
    move(&g,5,44,0); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(0u,runs(&g.f,g.done)); ASSERT_FLOAT_EQ(.25f,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); ASSERT_FLOAT_EQ(1,matrix[0],1e-5f);
    move(&g,4,77,0); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,4,77,0);
    ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_FLOAT_EQ(0,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
    ASSERT_FLOAT_EQ(4,value(&g,local(&g,2)),0); ASSERT_EQ(0u,g.messages); fixture_end(&g.f);
}

TEST(engine_enhanced_character_curve, animation_is_interruptible_and_loop_only_controls_stopping)
{
    for (int loop = 0; loop < 2; loop++) {
        enhanced_fixture_t g; begin(&g); range(&g,.25f,.75f);
        nmo_object_id_t animation = clip(&g,"Walk",0,20); ASSERT_TRUE(animation != 0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),(int32_t)animation);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,5),loop); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(animation,eng_character_next_animation(g.f.engine,g.f.owner_id));
        ASSERT_TRUE(eng_animation_playback_state(g.f.engine,animation)->flags & CKANIMATION_CANBEBREAK);
        ASSERT_EQ(0u,eng_animation_state(g.f.engine,animation)->flags & CKANIMATION_CANBEBREAK);
        ASSERT_FLOAT_EQ(1000,eng_animation_playback_state(g.f.engine,animation)->frame_rate,0);
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,animation));
        move(&g,18,1,0); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(loop ? animation : 0,eng_character_active_animation(g.f.engine,g.f.owner_id));
        ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); ASSERT_EQ(2u,runs(&g.f,g.block)); ASSERT_EQ(0u,g.messages);
        fixture_end(&g.f);
    }
}

TEST(engine_enhanced_character_curve, animation_requests_use_the_five_frame_best_warp)
{
    enhanced_fixture_t g; begin(&g); range(&g,.25f,.75f);
    nmo_object_id_t old = clip(&g,"Old",0,20), walk = clip(&g,"Walk",100,120);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),(int32_t)walk); commit(&g);
    ASSERT_TRUE(eng_animation_set_playback(g.f.engine,old,CKANIMATION_CANBEBREAK,CKANIMATION_CANBEBREAK,1000));
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,old));
    ASSERT_TRUE(eng_animation_set_frame(g.f.engine,old,5)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    nmo_object_id_t warper = eng_character_warper(g.f.engine,g.f.owner_id), from = 0, to = 0;
    ASSERT_EQ(warper,eng_character_active_animation(g.f.engine,g.f.owner_id));
    uint32_t mode = 0; ASSERT_TRUE(eng_character_warper_parameters(g.f.engine,g.f.owner_id,&mode,&from,NULL,&to,NULL));
    ASSERT_EQ(old,from); ASSERT_EQ(walk,to); ASSERT_EQ(CK_TRANSITION_WARPBEST|CK_TRANSITION_LOOPIFEQUAL|CK_TRANSITION_USEVELOCITY,mode);
    ASSERT_FLOAT_EQ(5,eng_animation_length(eng_animation_state(g.f.engine,warper)),0); ASSERT_EQ(0u,g.messages);
    fixture_end(&g.f);
}

TEST(engine_enhanced_character_curve, default_equal_and_zero_length_ranges_finish_on_loop_in)
{
    for (int kind = 0; kind < 3; kind++) {
        enhanced_fixture_t g; begin(&g);
        if (kind == 1) range(&g,.5f,.5f);
        if (kind == 2) {
            const nmo_curve_state_t *curve = fixture_state(&g.f,g.path);
            nmo_object_id_t last = nmo_ref_runtime_id(&curve->control_point_ids[1]);
            nmo_curvepoint_state_t *point = fixture_state(&g.f,last); nmo_curvepoint_set_position(point,0,20,0);
        }
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(0u,runs(&g.f,g.done)); ASSERT_FLOAT_EQ(kind == 0 ? 20 : kind == 1 ? 10 : 0,value(&g,local(&g,3)),0);
        if (kind == 0) move(&g,20,40,0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.done));
        ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_EQ(0u,g.messages); fixture_end(&g.f);
    }
}

TEST(engine_enhanced_character_curve, absent_owners_and_curves_report_errors_before_consuming_inputs)
{
    for (int kind = 0; kind < 3; kind++) {
        enhanced_fixture_t g; begin(&g);
        if (kind == 0) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        if (kind == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        commit(&g);
        if (kind == 2) { ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); nmo_engine_destroy_object(g.f.engine,g.path); }
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(kind == 0 ? NMO_ENGINE_BR_OWNERERROR : NMO_ENGINE_BR_PARAMETERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,kind == 2 ? 1 : 0)));
        ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(0u,g.messages); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(enhanced_fixture_t *g, size_t index, const char *text)
{
    nmo_object_id_t source = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,index),&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,source,text) != NMO_OK) return 0;
    return source;
}

static nmo_object_id_t saved_object(enhanced_fixture_t *g, nmo_object_id_t pin, nmo_object_id_t id)
{
    char text[32]; snprintf(text,sizeof(text),"%u",id);
    nmo_object_id_t source = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,source,text) != NMO_OK) return 0;
    return source;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t runtime_parameter(nmo_engine_t *engine, nmo_object_id_t block, bool is_local, size_t index)
{
    const nmo_behavior_state_t *state = nmo_object_get_state(eng_find_object(engine,block));
    nmo_object_id_t id = nmo_behavior_ref_array_get_id(is_local ? &state->local_parameters : &state->out_parameters,index);
    return eng_module_id(eng_module_of(engine,block,NULL),id);
}

TEST(engine_enhanced_character_curve, loaded_and_copied_followers_bind_their_own_paths_and_animations)
{
    enhanced_fixture_t source; begin(&source);
    nmo_object_id_t animation = clip(&source,"Walk",0,0); ASSERT_TRUE(animation != 0);
    nmo_object_id_t actor_parameter = saved_object(&source,fixture_target(&source.f,source.block),source.f.owner_id);
    nmo_object_id_t path_parameter = saved_object(&source,BLOCK_PIN(&source.f,source.block,0),source.path);
    nmo_object_id_t animation_parameter = saved_object(&source,BLOCK_PIN(&source.f,source.block,4),animation);
    ASSERT_TRUE(actor_parameter != 0 && path_parameter != 0 && animation_parameter != 0);
    ASSERT_TRUE(saved_input(&source,1,"25%") != 0 && saved_input(&source,2,"75%") != 0);
    fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id);
    owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id);
    script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *file_path = NMO_TEST_SCRATCH_FILE("engine_enhanced_character_curve.cmo");
    ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,file_path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        enhanced_fixture_t host = {0}; fixture_begin_document(&host.f); fixture_begin_edit(&host.f);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&host.f,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.f.engine,resolve_file,(void *)file_path);
        int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.f.engine,"enhanced.cmo",true,&file));
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t actor = nmo_engine_object_named(host.f.engine,"Actor",NMO_CID_CHARACTER);
        nmo_object_id_t root = nmo_engine_object_named(host.f.engine,"Root",NMO_CID_BODYPART);
        nmo_object_id_t curve = nmo_engine_object_named(host.f.engine,"Path",NMO_CID_CURVE);
        nmo_object_id_t walk = nmo_engine_object_named(host.f.engine,"Walk",NMO_CID_KEYEDANIMATION);
        nmo_object_id_t block = nmo_engine_object_named(host.f.engine,"Enhanced follow",NMO_CID_BEHAVIOR);
        nmo_object_id_t loaded_script = nmo_engine_object_named(host.f.engine,"Enhanced script",NMO_CID_BEHAVIOR);
        ASSERT_TRUE(actor != 0 && actor != source.f.owner_id && walk != 0 && walk != animation && block != 0);
        host.f.owner_id = actor; host.block = block; nmo_engine_set_trace(host.f.engine,log_messages,&host);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); position(&host,5,1,0);
        ASSERT_EQ(walk,eng_character_next_animation(host.f.engine,actor)); ASSERT_FALSE(eng_character_automatic(host.f.engine,actor));
        ASSERT_EQ(0u,eng_animation_state(host.f.engine,walk)->flags & CKANIMATION_CANBEBREAK);
        ASSERT_TRUE(eng_animation_playback_state(host.f.engine,walk)->flags & CKANIMATION_CANBEBREAK);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,loaded_script,false,false)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
        const eng_module_t *module = eng_module_of(host.f.engine,block,NULL);
        nmo_object_id_t ids[] = {actor,root,curve,walk,loaded_script,block,
            eng_module_id(module,actor_parameter),eng_module_id(module,path_parameter),eng_module_id(module,animation_parameter)}, copies[COUNT(ids)] = {0};
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.f.engine,ids,COUNT(ids),false,copies));
        ASSERT_EQ(copies[1],eng_character_root(host.f.engine,copies[0])); ASSERT_EQ(copies[0],eng_animation_character(host.f.engine,copies[3]));
        const eng_vector_t shifted = {100,0,0}; eng_entity_set_position(host.f.engine,eng_entity_index(host.f.engine,copies[2]),&shifted,-1,false);
        host.f.owner_id = copies[0]; host.block = copies[5];
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[4],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
        position(&host,105,1,0); ASSERT_EQ(copies[3],eng_character_next_animation(host.f.engine,copies[0]));
        move(&host,108,30,0); nmo_engine_destroy_object(host.f.engine,curve); nmo_engine_destroy_object(host.f.engine,walk);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); position(&host,108,30,0);
        ASSERT_EQ(copies[3],eng_character_next_animation(host.f.engine,copies[0]));
        /* Loop In enables automatic processing for the next manager frame. */
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
        ASSERT_EQ(copies[3],eng_character_active_animation(host.f.engine,copies[0]));
        float progress = -1; read_value(&host.f,runtime_parameter(host.f.engine,copies[5],false,0),&progress,sizeof(progress)); ASSERT_FLOAT_EQ(.4f,progress,1e-6f);
        read_value(&host.f,runtime_parameter(host.f.engine,copies[5],true,3),&progress,sizeof(progress)); ASSERT_FLOAT_EQ(15,progress,0);
        ASSERT_EQ(0u,host.messages); fixture_end(&host.f);
    }
    fixture_end(&source.f); remove(file_path);
}

TEST(engine_enhanced_character_curve, automatic_warped_playback_traverses_forward_and_reverse_ranges_without_a_controller)
{
    for (int reverse = 0; reverse < 2; reverse++) {
        enhanced_fixture_t g; begin(&g);
        if (reverse) { range(&g,.75f,.25f); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),2); }
        else {
            const eng_vector_t corner[] = {{0,20,0},{10,20,0},{10,20,10}};
            g.path = path(&g.f,corner,COUNT(corner)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)g.path);
        }
        nmo_object_id_t idle = clip(&g,"Idle",0,0), walk = clip(&g,"Walk",0,20);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),(int32_t)walk); commit(&g);
        ASSERT_TRUE(eng_animation_set_playback(g.f.engine,idle,CKANIMATION_CANBEBREAK,CKANIMATION_CANBEBREAK,1000));
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,idle));
        ASSERT_TRUE(eng_animation_set_frame(g.f.engine,idle,2)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(eng_character_warper(g.f.engine,g.f.owner_id),eng_character_active_animation(g.f.engine,g.f.owner_id));
        bool walked = false, turned = false;
        for (int step = 0; step < 64 && runs(&g.f,g.done) == 0; step++) {
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1));
            if (eng_character_active_animation(g.f.engine,g.f.owner_id) == walk) walked = true;
            float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
            if (reverse ? matrix[0] < -.99f : matrix[2] > .99f) turned = true;
            ASSERT_FLOAT_EQ(1,matrix[13],1e-4f);
        }
        ASSERT_TRUE(walked); ASSERT_TRUE(turned); ASSERT_EQ(1u,runs(&g.f,g.done));
        ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(0u,eng_character_active_animation(g.f.engine,g.f.owner_id));
        ASSERT_FLOAT_EQ(reverse ? 0 : 1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_EQ(0u,g.messages);
        uint64_t executions = runs(&g.f,g.block); ASSERT_TRUE(executions > 2);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1));
        ASSERT_EQ(executions,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.done));
        fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_enhanced_character_curve, initialization_preserves_output_and_uses_all_six_native_axes);
REGISTER_TEST(engine_enhanced_character_curve, forward_end_is_inclusive_and_uses_cached_bounds);
REGISTER_TEST(engine_enhanced_character_curve, reverse_end_is_strict_and_does_not_reverse_the_tangent);
REGISTER_TEST(engine_enhanced_character_curve, animation_is_interruptible_and_loop_only_controls_stopping);
REGISTER_TEST(engine_enhanced_character_curve, animation_requests_use_the_five_frame_best_warp);
REGISTER_TEST(engine_enhanced_character_curve, default_equal_and_zero_length_ranges_finish_on_loop_in);
REGISTER_TEST(engine_enhanced_character_curve, absent_owners_and_curves_report_errors_before_consuming_inputs);
REGISTER_TEST(engine_enhanced_character_curve, loaded_and_copied_followers_bind_their_own_paths_and_animations);
REGISTER_TEST(engine_enhanced_character_curve, automatic_warped_playback_traverses_forward_and_reverse_ranges_without_a_controller);
TEST_MAIN_END()
