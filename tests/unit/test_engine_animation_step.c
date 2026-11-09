#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_operations.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_ENTITY_STEP NMO_GUID(0x14782b1d,0x52692e21)
#define GUID_GLOBAL_STEP NMO_GUID(0x7ba27c0a,0x7bd21db6)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct step_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, animation, block, out, animation_parameter, step_parameter;
} step_fixture_t;

static nmo_object_id_t channel(engine_fixture_t *f, const char *name, nmo_object_id_t entity)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_OBJECTANIMATION,name);
    nmo_objectanimation_state_t *state = fixture_state(f,id);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    const float values[] = {0,0,0,0, 8,8,16,24};
    float *keys = nmo_arena_alloc(arena,sizeof(values),_Alignof(float));
    if (state == NULL || keys == NULL) return 0;
    memcpy(keys,values,sizeof(values));
    state->format = CKOBJANIM_FORMAT_CONTROLLERS; state->has_length = 1; state->length = 8;
    state->entity = nmo_ref_from_id(entity);
    state->controllers = nmo_arena_alloc(arena,sizeof(*state->controllers),_Alignof(nmo_objanim_controller_t));
    if (state->controllers == NULL) return 0;
    state->controller_count = 1;
    *state->controllers = (nmo_objanim_controller_t){0x637c4301u,2,sizeof(values),keys};
    return id;
}

static void begin(step_fixture_t *g, nmo_guid_t guid)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent");
    g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Animated Entity");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    nmo_3dentity_state_t *owner = fixture_state(&g->f,g->f.owner_id), *parent = fixture_state(&g->f,g->parent);
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child);
    ASSERT_NOT_NULL(owner); ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child);
    owner->has_entityndata_chunk = parent->has_entityndata_chunk = target->has_entityndata_chunk = child->has_entityndata_chunk = 1;
    owner->world_matrix[12] = 100; parent->world_matrix[12] = 10;
    target->parent = nmo_ref_from_id(g->parent); target->world_matrix[12] = 12;
    child->parent = nmo_ref_from_id(g->target); child->world_matrix[12] = 15;
    g->animation = channel(&g->f,"Object Animation",g->target); ASSERT_TRUE(g->animation != 0);
    g->f.script_id = fixture_add_script(&g->f,"Manual Animation Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,guid,"Manual Step"); g->out = fixture_block(&g->f,GUID_NOP,"After Step");
    ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->animation);
    const float step = .5f; fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,1),&step,sizeof(step));
    g->animation_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,0)));
    g->step_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,1)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(step_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void position(nmo_engine_t *engine, nmo_object_id_t id, float x, float y, float z)
{
    float m[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,m));
    ASSERT_TRUE(isfinite(m[12]) && isfinite(m[13]) && isfinite(m[14]));
    ASSERT_FLOAT_EQ(x,m[12],1e-5f); ASSERT_FLOAT_EQ(y,m[13],1e-5f); ASSERT_FLOAT_EQ(z,m[14],1e-5f);
}

static void trigger(step_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

static nmo_object_id_t actor(engine_fixture_t *f, const char *name, nmo_object_id_t *body)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CHARACTER,name);
    *body = fixture_create(f,NMO_CID_BODYPART,"Body");
    nmo_character_state_t *state = fixture_state(f,id);
    nmo_bodypart_state_t *part = fixture_state(f,*body);
    if (state == NULL || part == NULL) return 0;
    state->base.has_entityndata_chunk = ((nmo_3dentity_state_t *)part)->has_entityndata_chunk = 1;
    state->root_body_part = nmo_ref_from_id(*body); state->base.world_matrix[12] = 40;
    ((nmo_3dentity_state_t *)part)->parent = nmo_ref_from_id(id);
    ((nmo_3dentity_state_t *)part)->world_matrix[12] = 42;
    part->has_character = 1; part->character = nmo_ref_from_id(id);
    return id;
}

static nmo_object_id_t group(engine_fixture_t *f, const nmo_object_id_t *channels, size_t count)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,"Keyed Animation");
    nmo_keyedanimation_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->animation_ids = nmo_arena_alloc(nmo_session_get_arena(f->session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    if (state->animation_ids == NULL) return 0;
    for (size_t i = 0; i < count; i++) state->animation_ids[i] = nmo_ref_from_id(channels[i]);
    state->animation_count = (uint32_t)count;
    state->base.has_length = state->base.has_current_step = 1; state->base.length = 8; state->base.current_step = .125f;
    return id;
}

static void move(nmo_engine_t *engine, nmo_object_id_t id, float x)
{
    eng_vector_t value = {x,0,0}; eng_entity_set_position(engine,eng_entity_index(engine,id),&value,-1,false);
}

static void count_operations(const nmo_engine_event_t *event, void *data)
{
    if (event->kind == NMO_ENGINE_EVENT_OPERATION && event->result == NMO_OK) (*(size_t *)data)++;
}

static void computed_step(step_fixture_t *g)
{
    nmo_object_id_t a = 0, b = 0, result = 0, operation = 0;
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g->f.tx,g->f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_FLOAT,"A",&a));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g->f.tx,g->f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_FLOAT,"B",&b));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,a,"0.2"));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,b,"0.3"));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g->f.tx,g->f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_PERCENTAGE,"Sum",&result));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g->f.tx,g->f.script_id,NMO_OP_GUID_ADD,a,b,result,&operation));
    const nmo_parameteroperation_state_t *state = fixture_state(&g->f,operation); ASSERT_NOT_NULL(state);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g->f.tx,nmo_parameteroperation_out_id(state),BLOCK_PIN(&g->f,g->block,1)));
    size_t kept = 0;
    for (size_t i = 0; i < g->f.value_count; i++) if (g->f.values[i].param != g->step_parameter) g->f.values[kept++] = g->f.values[i];
    g->f.value_count = kept;
}

TEST(engine_animation_step, entity_samples_the_bound_target_and_moves_children_without_scheduling)
{
    step_fixture_t g; begin(&g,GUID_ENTITY_STEP); commit(&g);
    ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_ENTITY_STEP)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    position(g.f.engine,g.target,14,8,12); position(g.f.engine,g.child,17,8,12); position(g.f.engine,g.f.owner_id,100,0,0);
    ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(1u,runs(&g.f,g.block));
    ASSERT_FLOAT_EQ(12,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0);
    fixture_end(&g.f);
}

TEST(engine_animation_step, entity_reads_live_steps_and_animation_bindings)
{
    step_fixture_t g; begin(&g,GUID_ENTITY_STEP); commit(&g);
    const float values[] = {0,.25f,1,-1,2};
    const float clamped[] = {0,.25f,1,0,1};
    for (size_t i = 0; i < COUNT(values); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.step_parameter,&values[i],sizeof(float))); trigger(&g,100);
        position(g.f.engine,g.target,10+8*clamped[i],16*clamped[i],24*clamped[i]);
    }
    ASSERT_TRUE(eng_object_animation_set_entity(g.f.engine,g.animation,g.f.owner_id));
    const float step = .25f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.step_parameter,&step,sizeof(step))); trigger(&g,0);
    position(g.f.engine,g.f.owner_id,2,4,6); position(g.f.engine,g.target,18,16,24);
    fixture_end(&g.f);
}

TEST(engine_animation_step, entity_missing_and_deleted_animations_skip_lazy_step_inputs_but_activate_out)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        step_fixture_t g; begin(&g,GUID_ENTITY_STEP); computed_step(&g);
        if (!deleted) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        commit(&g); if (deleted) nmo_engine_destroy_object(g.f.engine,g.animation);
        size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0u,operations); ASSERT_EQ(1u,runs(&g.f,g.out));
        position(g.f.engine,g.target,12,0,0);
        if (!deleted) {
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.animation_parameter,&g.animation,sizeof(g.animation))); trigger(&g,0);
            ASSERT_EQ(1u,operations); position(g.f.engine,g.target,14,8,12);
        }
        fixture_end(&g.f);
    }
}

TEST(engine_animation_step, entity_respects_ignore_animation_and_tolerates_a_missing_bound_target)
{
    for (int missing = 0; missing < 2; missing++) {
        step_fixture_t g; begin(&g,GUID_ENTITY_STEP);
        if (!missing) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->moveable_flags |= CK_3DENTITY_IGNOREANIMATION;
        commit(&g); if (missing) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.out));
        if (!missing) position(g.f.engine,g.target,12,0,0);
        fixture_end(&g.f);
    }
}

TEST(engine_animation_step, global_accepts_object_and_base_animations_without_resetting_ordinary_ancestors)
{
    for (int base = 0; base < 2; base++) {
        step_fixture_t g; begin(&g,GUID_GLOBAL_STEP);
        if (base) {
            nmo_object_id_t animation = fixture_create(&g.f,NMO_CID_ANIMATION,"Base Animation");
            nmo_animation_state_t *state = fixture_state(&g.f,animation); ASSERT_NOT_NULL(state);
            state->has_length = 1; state->length = 8;
            fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)animation); g.animation = animation;
        }
        commit(&g); ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_GLOBAL_STEP));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(g.f.engine,g.parent,10,0,0); position(g.f.engine,g.f.owner_id,100,0,0);
        if (base) {
            float frame = NAN; ASSERT_TRUE(eng_animation_frame(g.f.engine,g.animation,&frame)); ASSERT_FLOAT_EQ(4,frame,0);
            position(g.f.engine,g.target,12,0,0);
        } else { position(g.f.engine,g.target,14,8,12); position(g.f.engine,g.child,17,8,12); }
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
        fixture_end(&g.f);
    }
}

TEST(engine_animation_step, global_keyed_sampling_resets_each_character_with_children_and_keeps_automatic_processing)
{
    step_fixture_t g; begin(&g,GUID_GLOBAL_STEP); nmo_object_id_t bodies[2], actors[2];
    actors[0] = actor(&g.f,"First Character",&bodies[0]); actors[1] = actor(&g.f,"Second Character",&bodies[1]);
    ASSERT_TRUE(actors[0] != 0 && actors[1] != 0);
    nmo_3dentity_state_t *child = fixture_state(&g.f,g.child); child->parent = nmo_ref_from_id(bodies[0]); child->world_matrix[12] = 45;
    nmo_object_id_t a = channel(&g.f,"First Body Channel",bodies[0]), b = channel(&g.f,"Second Body Channel",bodies[1]);
    const nmo_object_id_t channels[] = {a,0,b,a,g.animation};
    nmo_object_id_t keyed = group(&g.f,channels,COUNT(channels)); ASSERT_TRUE(keyed != 0);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)keyed); commit(&g);
    for (size_t i = 0; i < 2; i++) {
        ASSERT_TRUE(eng_character_set_automatic(g.f.engine,actors[i],true));
        /* Observe the manual pose before a later automatic root-motion pass. */
        ASSERT_EQ(NMO_OK,nmo_engine_activate_object(g.f.engine,actors[i],false));
    }
    const float steps[] = {-.5f,.5f,2}, clamped[] = {0,.5f,1};
    for (size_t s = 0; s < COUNT(steps); s++) {
        const eng_matrix_t displaced = {.m={{0,2,0,0},{-3,0,0,0},{0,0,4,0},{40,10,-20,1}}};
        for (size_t i = 0; i < 2; i++) eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,actors[i]),&displaced,false);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.step_parameter,&steps[s],sizeof(float))); trigger(&g,0);
        for (size_t i = 0; i < 2; i++) {
            ASSERT_TRUE(eng_character_automatic(g.f.engine,actors[i])); position(g.f.engine,actors[i],0,0,0);
            float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,actors[i],matrix));
            for (size_t cell = 0; cell < COUNT(matrix); cell++) {
                ASSERT_TRUE(isfinite(matrix[cell])); ASSERT_FLOAT_EQ(cell%5 == 0 ? 1 : 0,matrix[cell],0);
            }
            position(g.f.engine,bodies[i],8*clamped[s],16*clamped[s],24*clamped[s]);
        }
        position(g.f.engine,g.child,8*clamped[s]+3,16*clamped[s],24*clamped[s]);
        position(g.f.engine,g.target,10+8*clamped[s],16*clamped[s],24*clamped[s]);
        float step = NAN; ASSERT_TRUE(eng_animation_current_step(g.f.engine,keyed,&step)); ASSERT_FLOAT_EQ(clamped[s],step,0);
    }
    ASSERT_FLOAT_EQ(.125f,((nmo_keyedanimation_state_t *)fixture_state(&g.f,keyed))->base.current_step,0);
    ASSERT_EQ(3u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); ASSERT_EQ(3u,runs(&g.f,g.block));
    for (size_t i = 0; i < 2; i++) ASSERT_EQ(NMO_OK,nmo_engine_activate_object(g.f.engine,actors[i],true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    for (size_t i = 0; i < 2; i++) {
        ASSERT_TRUE(eng_character_automatic(g.f.engine,actors[i])); position(g.f.engine,actors[i],8,16,24);
        position(g.f.engine,bodies[i],8,16,24);
    }
    ASSERT_EQ(3u,runs(&g.f,g.block));
    fixture_end(&g.f);
}

TEST(engine_animation_step, global_character_reset_follows_live_channel_bindings_and_skips_deleted_channels)
{
    step_fixture_t g; begin(&g,GUID_GLOBAL_STEP); nmo_object_id_t body[2], character[2];
    character[0] = actor(&g.f,"First Character",&body[0]); character[1] = actor(&g.f,"Second Character",&body[1]);
    ASSERT_TRUE(character[0] != 0 && character[1] != 0);
    ((nmo_objectanimation_state_t *)fixture_state(&g.f,g.animation))->entity = nmo_ref_from_id(body[0]);
    nmo_object_id_t keyed = group(&g.f,&g.animation,1); ASSERT_TRUE(keyed != 0);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)keyed); commit(&g);
    ASSERT_TRUE(eng_object_animation_set_entity(g.f.engine,g.animation,body[1]));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    position(g.f.engine,character[0],40,0,0); position(g.f.engine,character[1],0,0,0); position(g.f.engine,body[1],4,8,12);
    move(g.f.engine,character[1],30); nmo_engine_destroy_object(g.f.engine,g.animation); trigger(&g,0);
    position(g.f.engine,character[1],30,0,0); ASSERT_EQ(2u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_animation_step, global_missing_and_deleted_animations_still_evaluate_step_and_activate_out)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        step_fixture_t g; begin(&g,GUID_GLOBAL_STEP); computed_step(&g);
        nmo_object_id_t body = 0, character = actor(&g.f,"Character",&body); ASSERT_TRUE(character != 0);
        ((nmo_objectanimation_state_t *)fixture_state(&g.f,g.animation))->entity = nmo_ref_from_id(body);
        nmo_object_id_t keyed = group(&g.f,&g.animation,1); ASSERT_TRUE(keyed != 0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),deleted ? (int32_t)keyed : 0);
        commit(&g); if (deleted) nmo_engine_destroy_object(g.f.engine,keyed);
        size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,operations); ASSERT_EQ(1u,runs(&g.f,g.out));
        position(g.f.engine,character,40,0,0); position(g.f.engine,body,42,0,0); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(step_fixture_t *g, int pin, const char *value)
{
    nmo_object_id_t source = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,pin),&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,source,value) != NMO_OK) return 0;
    return source;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

TEST(engine_animation_step, loaded_and_copied_graphs_remap_animation_bindings_and_keep_steps_independent)
{
    for (int global = 0; global < 2; global++) {
        step_fixture_t g; begin(&g,global ? GUID_GLOBAL_STEP : GUID_ENTITY_STEP);
        nmo_object_id_t body = 0, character = actor(&g.f,"Saved Character",&body); ASSERT_TRUE(character != 0);
        nmo_object_id_t keyed = group(&g.f,&g.animation,1); ASSERT_TRUE(keyed != 0);
        if (global) {
            ((nmo_objectanimation_state_t *)fixture_state(&g.f,g.animation))->entity = nmo_ref_from_id(body);
            nmo_3dentity_state_t *child = fixture_state(&g.f,g.child); child->parent = nmo_ref_from_id(body); child->world_matrix[12] = 45;
        }
        char text[32]; snprintf(text,sizeof(text),"%u",global ? keyed : g.animation);
        nmo_object_id_t parameters[] = {saved_input(&g,0,text),saved_input(&g,1,"0.25")};
        ASSERT_TRUE(parameters[0] != 0 && parameters[1] != 0); fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_animation_step.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"manual-animation.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Manual Step",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.parent,g.target,g.child,character,body,g.animation,keyed,
                g.f.script_id,g.block,g.out,parameters[0],parameters[1]};
            nmo_object_id_t ids[COUNT(original)], copies[COUNT(original)];
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            ASSERT_TRUE(ids[6] != g.animation); ASSERT_EQ(global ? ids[5] : ids[2],eng_object_animation_entity(host.engine,ids[6]));
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            position(host.engine,global ? ids[5] : ids[2],global ? 2.0f : 12.0f,4,6);
            position(host.engine,ids[3],global ? 5.0f : 15.0f,4,6); position(host.engine,ids[4],global ? 0.0f : 40.0f,0,0);
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            ASSERT_EQ(global ? copies[5] : copies[2],eng_object_animation_entity(host.engine,copies[6]));
            ASSERT_EQ(copies[6],eng_keyed_animation_part(host.engine,copies[7],global ? copies[5] : copies[2]));
            nmo_object_id_t copied_reference = 0; read_value(&host,copies[11],&copied_reference,sizeof(copied_reference));
            ASSERT_EQ(global ? copies[7] : copies[6],copied_reference);
            const float different = .75f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[12],&different,sizeof(different)));
            move(host.engine,global ? copies[4] : copies[1],global ? 60.0f : 30.0f);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,ids[8],true,true));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[8],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            position(host.engine,global ? ids[5] : ids[2],global ? 2.0f : 12.0f,4,6);
            position(host.engine,global ? copies[5] : copies[2],global ? 6.0f : 36.0f,12,18);
            position(host.engine,copies[3],global ? 9.0f : 39.0f,12,18); position(host.engine,copies[4],global ? 0.0f : 40.0f,0,0);
            float original_step = NAN; read_value(&host,ids[12],&original_step,sizeof(original_step)); ASSERT_FLOAT_EQ(.25f,original_step,0);
            nmo_engine_destroy_object(host.engine,ids[6]); nmo_engine_destroy_object(host.engine,ids[7]);
            nmo_engine_destroy_object(host.engine,global ? ids[4] : ids[2]); nmo_engine_destroy_object(host.engine,ids[0]);
            const float half = .5f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[12],&half,sizeof(half)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[8],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            position(host.engine,global ? copies[5] : copies[2],global ? 4.0f : 34.0f,8,12); position(host.engine,copies[3],global ? 7.0f : 37.0f,8,12);
            ASSERT_EQ(2u,runs(&host,copies[10])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); ASSERT_EQ(2u,runs(&host,copies[9]));
            fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(12,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0);
        fixture_end(&g.f); remove(path);
    }
}

TEST(engine_animation_step, bezier_progression_drives_entity_and_global_sampling_through_completion)
{
    for (int global = 0; global < 2; global++) {
        step_fixture_t g; begin(&g,global ? GUID_GLOBAL_STEP : GUID_ENTITY_STEP);
        if (global) {
            nmo_object_id_t keyed = group(&g.f,&g.animation,1); ASSERT_TRUE(keyed != 0);
            fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)keyed);
        }
        const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
        ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
        nmo_object_id_t progress = fixture_block(&g.f,NMO_GUID(0x6bb8699a,0x29fb6a4b),"Progress"); ASSERT_TRUE(progress != 0);
        const float values[] = {40,0,1};
        for (size_t i = 0; i < COUNT(values); i++) fixture_input_value(&g.f,BLOCK_PIN(&g.f,progress,i),&values[i],sizeof(float));
        nmo_object_id_t curve = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,progress,3),&curve));
        size_t kept = 0;
        for (size_t i = 0; i < g.f.value_count; i++) if (g.f.values[i].param != g.step_parameter) g.f.values[kept++] = g.f.values[i];
        g.f.value_count = kept;
        ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,progress,3),BLOCK_PIN(&g.f,g.block,1)));
        fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,progress,0),0);
        fixture_link(&g.f,BLOCK_OUT(&g.f,progress,1),BLOCK_IN(&g.f,progress,1),1);
        for (int out = 0; out < 2; out++) fixture_link(&g.f,BLOCK_OUT(&g.f,progress,out),BLOCK_IN(&g.f,g.block,0),0);
        commit(&g);
        for (int frame = 1; frame <= 5; frame++) {
            float step = frame < 4 ? (float)frame*.25f : 1;
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10));
            position(g.f.engine,g.target,10+8*step,16*step,24*step); position(g.f.engine,g.child,13+8*step,16*step,24*step);
        }
        ASSERT_EQ(5u,runs(&g.f,g.block)); ASSERT_EQ(5u,runs(&g.f,g.out)); ASSERT_EQ(5u,runs(&g.f,progress));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(5u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_animation_step, entity_samples_the_bound_target_and_moves_children_without_scheduling);
REGISTER_TEST(engine_animation_step, entity_reads_live_steps_and_animation_bindings);
REGISTER_TEST(engine_animation_step, entity_missing_and_deleted_animations_skip_lazy_step_inputs_but_activate_out);
REGISTER_TEST(engine_animation_step, entity_respects_ignore_animation_and_tolerates_a_missing_bound_target);
REGISTER_TEST(engine_animation_step, global_accepts_object_and_base_animations_without_resetting_ordinary_ancestors);
REGISTER_TEST(engine_animation_step, global_keyed_sampling_resets_each_character_with_children_and_keeps_automatic_processing);
REGISTER_TEST(engine_animation_step, global_character_reset_follows_live_channel_bindings_and_skips_deleted_channels);
REGISTER_TEST(engine_animation_step, global_missing_and_deleted_animations_still_evaluate_step_and_activate_out);
REGISTER_TEST(engine_animation_step, loaded_and_copied_graphs_remap_animation_bindings_and_keep_steps_independent);
REGISTER_TEST(engine_animation_step, bezier_progression_drives_entity_and_global_sampling_through_completion);
TEST_MAIN_END()
