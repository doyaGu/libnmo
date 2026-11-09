#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_character.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_module.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_FACTOR NMO_GUID(0x7afc20a3,0x6d367a99)
#define GUID_CREATE NMO_GUID(0x56864d6d,0x159b5d1e)
#define GUID_LEGACY_CREATE NMO_GUID(0x248a56ed,0x2c232967)
#define GUID_FRAME NMO_GUID(0x0e579636,0xa3b6e7f0)

static nmo_object_id_t channel(engine_fixture_t *f, const char *name, nmo_object_id_t entity, float length, float end)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_OBJECTANIMATION,name);
    nmo_objectanimation_state_t *state = fixture_state(f,id);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    float *keys = nmo_arena_alloc(arena,8*sizeof(float),_Alignof(float));
    nmo_objanim_controller_t *controller = nmo_arena_alloc(arena,sizeof(*controller),_Alignof(nmo_objanim_controller_t));
    if (state == NULL || keys == NULL || controller == NULL) return 0;
    float values[] = {0,0,0,0,length,end,0,0}; memcpy(keys,values,sizeof(values));
    *controller = (nmo_objanim_controller_t){0x637c4301u,2,sizeof(values),keys};
    state->format = CKOBJANIM_FORMAT_CONTROLLERS; state->entity = nmo_ref_from_id(entity);
    state->has_length = 1; state->length = length; state->controllers = controller; state->controller_count = 1;
    return id;
}

static nmo_object_id_t clip(engine_fixture_t *f, const char *name, nmo_object_id_t character,
    float length, const nmo_object_id_t *channels, size_t count)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,name);
    nmo_keyedanimation_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->base.has_length = state->base.has_character = 1;
    state->base.length = length; state->base.character = nmo_ref_from_id(character);
    state->animation_count = (uint32_t)count;
    state->animation_ids = nmo_arena_alloc(nmo_session_get_arena(f->session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    if (count != 0 && state->animation_ids == NULL) return 0;
    for (size_t i = 0; i < count; i++) state->animation_ids[i] = nmo_ref_from_id(channels[i]);
    return id;
}

static nmo_object_id_t blend_channel(engine_fixture_t *f, nmo_object_id_t a, nmo_object_id_t b, float length)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_OBJECTANIMATION,"Blend");
    nmo_objectanimation_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->format = CKOBJANIM_FORMAT_CONTROLLERS; state->entity = nmo_ref_from_id(f->owner_id);
    state->flags = CK_OBJECTANIMATION_MERGED; state->has_length = state->has_merge = 1;
    state->length = length; state->merge_factor = .5f;
    state->anim1 = nmo_ref_from_id(a); state->anim2 = nmo_ref_from_id(b);
    return id;
}

static float position(nmo_engine_t *engine, nmo_object_id_t entity)
{
    float matrix[16];
    return nmo_engine_entity_world_matrix(engine,entity,matrix) ? matrix[12] : NAN;
}

static nmo_object_id_t actor(engine_fixture_t *f, nmo_object_id_t *body)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CHARACTER,"Character");
    *body = fixture_create(f,NMO_CID_BODYPART,"Body");
    nmo_character_state_t *state = fixture_state(f,id);
    nmo_bodypart_state_t *part = fixture_state(f,*body);
    if (state == NULL || part == NULL) return 0;
    state->base.moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED;
    state->root_body_part = nmo_ref_from_id(*body);
    ((nmo_3dentity_state_t *)part)->has_entityndata_chunk = 1;
    ((nmo_3dentity_state_t *)part)->parent = nmo_ref_from_id(id);
    part->has_character = 1; part->character = nmo_ref_from_id(id);
    nmo_array_dispose(&state->body_parts);
    if (nmo_array_alloc(&state->body_parts,sizeof(nmo_character_part_t),1,NULL) != NMO_OK) return 0;
    ((nmo_character_part_t *)state->body_parts.data)[0] = (nmo_character_part_t){.ref=nmo_ref_from_id(*body)};
    return id;
}

TEST(engine_blend, live_factors_propagate_to_channels_and_copies_without_editing_loaded_data)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t a = channel(&f,"First",f.owner_id,10,10), b = channel(&f,"Second",f.owner_id,20,30);
    nmo_object_id_t part = blend_channel(&f,a,b,20), channels[] = {part,a,part};
    nmo_object_id_t animation = clip(&f,"Blended",0,20,channels,COUNT(channels));
    nmo_keyedanimation_state_t *saved = fixture_state(&f,animation); saved->has_merge = 1; saved->merged = 1; saved->merge_factor = .5f;
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    const float factors[] = {0,.25f,.5f,1,1.5f,-.5f};
    for (size_t i = 0; i < COUNT(factors); i++) {
        ASSERT_TRUE(eng_animation_set_merge_factor(f.engine,animation,factors[i]));
        ASSERT_TRUE(eng_object_animation_set_frame(f.engine,part,10,true));
        ASSERT_FLOAT_EQ(5+10*factors[i],position(f.engine,f.owner_id),1e-6f);
        ASSERT_FLOAT_EQ(factors[i],eng_animation_merge_factor(f.engine,a),0);
        ASSERT_FLOAT_EQ(.5f,((const nmo_objectanimation_state_t *)fixture_state(&f,part))->merge_factor,0);
        ASSERT_FLOAT_EQ(.5f,saved->merge_factor,0);
    }
    ASSERT_TRUE(eng_animation_set_merge_factor(f.engine,animation,.25f));
    nmo_object_id_t copy = 0;
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,&animation,1,false,&copy));
    nmo_object_id_t copied_part = eng_keyed_animation_part(f.engine,copy,f.owner_id);
    ASSERT_TRUE(copied_part != 0 && copied_part != part);
    ASSERT_FLOAT_EQ(.25f,eng_animation_merge_factor(f.engine,copy),0);
    ASSERT_FLOAT_EQ(.25f,eng_animation_merge_factor(f.engine,copied_part),0);
    ASSERT_TRUE(eng_animation_set_merge_factor(f.engine,copy,.75f));
    ASSERT_TRUE(eng_object_animation_set_frame(f.engine,copied_part,10,true));
    ASSERT_FLOAT_EQ(12.5f,position(f.engine,f.owner_id),1e-6f);
    ASSERT_FLOAT_EQ(.25f,eng_animation_merge_factor(f.engine,part),0);
    nmo_engine_destroy_object(f.engine,copied_part);
    ASSERT_FALSE(eng_animation_set_merge_factor(f.engine,copied_part,1));
    fixture_end(&f);
}

TEST(engine_blend, factor_block_uses_native_fractional_default_and_allows_absent_animation)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t a = channel(&f,"A",f.owner_id,10,10), b = channel(&f,"B",f.owner_id,20,30);
    nmo_object_id_t part = blend_channel(&f,a,b,20);
    nmo_object_id_t animation = clip(&f,"Blended",0,20,&part,1);
    f.script_id = fixture_add_script(&f,"Blend control",&f.script_in); fixture_begin_edit(&f);
    nmo_object_id_t block = fixture_block(&f,GUID_FACTOR,"Set Blended Animation Factor"); ASSERT_TRUE(block != 0);
    fixture_input_int(&f,BLOCK_PIN(&f,block,0),(int32_t)animation);
    nmo_object_id_t factor_param = nmo_parameterin_source_id(fixture_state(&f,BLOCK_PIN(&f,block,1)));
    nmo_object_id_t animation_param = nmo_parameterin_source_id(fixture_state(&f,BLOCK_PIN(&f,block,0)));
    fixture_link(&f,f.script_in,BLOCK_IN(&f,block,0),0);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_EQ(1u,runs(&f,block));
    ASSERT_FLOAT_EQ(.5f,eng_animation_merge_factor(f.engine,part),0);
    float factor = .25f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,factor_param,&factor,sizeof(factor)));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_TRUE(eng_object_animation_set_frame(f.engine,part,10,true)); ASSERT_FLOAT_EQ(7.5f,position(f.engine,f.owner_id),1e-6f);
    nmo_engine_destroy_object(f.engine,animation);
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    int32_t absent = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,animation_param,&absent,sizeof(absent)));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(4u,runs(&f,block));
    fixture_end(&f);
}

TEST(engine_blend, factory_pairs_live_targets_and_unions_independent_stretched_channels)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t body, character = actor(&f,&body);
    nmo_object_id_t left_only = fixture_create(&f,NMO_CID_3DENTITY,"Left only"), right_only = fixture_create(&f,NMO_CID_3DENTITY,"Right only");
    nmo_object_id_t a = channel(&f,"A",body,10,10), b = channel(&f,"B",f.owner_id,20,30);
    nmo_object_id_t lone = channel(&f,"Lone",left_only,5,40), other = channel(&f,"Other",right_only,10,60);
    nmo_object_id_t unbound = channel(&f,"Unbound",0,10,999);
    nmo_object_id_t left[] = {lone,0,a,unbound}, right[] = {b,other};
    nmo_object_id_t first = clip(&f,"First",character,10,left,COUNT(left));
    nmo_object_id_t second = clip(&f,"Second",0,20,right,COUNT(right));
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_TRUE(eng_object_animation_set_entity(f.engine,b,body));
    nmo_object_id_t common = eng_keyed_animation_merge(f.engine,first,second,true);
    nmo_object_id_t combined = eng_keyed_animation_merge(f.engine,first,second,false);
    ASSERT_TRUE(common != 0 && combined != 0 && common != combined);
    ASSERT_STR_EQ("First+Second",nmo_engine_object_name(f.engine,common));
    ASSERT_EQ(character,eng_animation_character(f.engine,common)); ASSERT_EQ(body,eng_keyed_animation_root_entity(f.engine,common));
    const nmo_keyedanimation_state_t *intersection = nmo_object_get_state(eng_find_object(f.engine,common));
    const nmo_keyedanimation_state_t *full = nmo_object_get_state(eng_find_object(f.engine,combined));
    ASSERT_EQ(1u,intersection->animation_count); ASSERT_EQ(3u,full->animation_count);
    ASSERT_TRUE(intersection->merged); ASSERT_FLOAT_EQ(20,intersection->base.length,0);
    ASSERT_EQ(0u,eng_character_animation_count(f.engine,character)); /* The block's separate setting attaches it. */
    nmo_object_id_t common_part = eng_keyed_animation_part(f.engine,common,body);
    ASSERT_EQ(a,eng_animation_merge_source(f.engine,common_part,false)); ASSERT_EQ(b,eng_animation_merge_source(f.engine,common_part,true));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,common,10)); ASSERT_FLOAT_EQ(10,position(f.engine,body),1e-6f);
    ASSERT_TRUE(eng_animation_set_merge_factor(f.engine,common,.25f));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,common,10)); ASSERT_FLOAT_EQ(7.5f,position(f.engine,body),1e-6f);
    nmo_object_id_t copied_lone = eng_keyed_animation_part(f.engine,combined,left_only), copied_other = eng_keyed_animation_part(f.engine,combined,right_only);
    ASSERT_TRUE(copied_lone != 0 && copied_lone != lone && copied_other != 0 && copied_other != other);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,combined,10));
    ASSERT_FLOAT_EQ(20,position(f.engine,left_only),1e-6f); ASSERT_FLOAT_EQ(30,position(f.engine,right_only),1e-6f);
    const nmo_objectanimation_state_t *saved = fixture_state(&f,lone);
    const nmo_objectanimation_state_t *cloned = nmo_object_get_state(eng_find_object(f.engine,copied_lone));
    ASSERT_FLOAT_EQ(5,((const float *)saved->controllers[0].data)[4],0);
    ASSERT_FLOAT_EQ(20,((const float *)cloned->controllers[0].data)[4],0);
    ASSERT_EQ(10,((const nmo_objectanimation_state_t *)fixture_state(&f,a))->length);
    ASSERT_EQ(f.owner_id,nmo_ref_runtime_id(&((const nmo_objectanimation_state_t *)fixture_state(&f,b))->entity));
    ASSERT_EQ(2u,((const nmo_keyedanimation_state_t *)fixture_state(&f,second))->animation_count);
    fixture_end(&f);
}

TEST(engine_blend, factory_preserves_nested_sources_copies_and_dynamic_lifetimes)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t body, character = actor(&f,&body);
    nmo_object_id_t a = channel(&f,"A",body,10,10), b = channel(&f,"B",body,10,30);
    nmo_object_id_t first = clip(&f,"A",character,10,&a,1), second = clip(&f,"B",character,10,&b,1);
    nmo_object_id_t no_owner = clip(&f,"Unowned",0,10,&a,1);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_EQ(0u,eng_keyed_animation_merge(f.engine,no_owner,second,false));
    ASSERT_EQ(0u,eng_keyed_animation_merge(f.engine,first,0,false));
    eng_mark_dynamic(f.engine,first);
    nmo_object_id_t merged = eng_keyed_animation_merge(f.engine,first,second,true);
    ASSERT_TRUE(merged != 0); ASSERT_TRUE(eng_animation_set_merge_factor(f.engine,merged,.25f));
    nmo_object_id_t nested = eng_keyed_animation_merge(f.engine,merged,second,true);
    ASSERT_TRUE(nested != 0); ASSERT_TRUE(eng_animation_set_frame(f.engine,nested,5));
    ASSERT_FLOAT_EQ(11.25f,position(f.engine,body),1e-6f);
    nmo_object_id_t ids[] = {first,second,merged,nested}, copies[COUNT(ids)] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,ids,COUNT(ids),false,copies));
    ASSERT_TRUE(eng_animation_set_merge_factor(f.engine,merged,1));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copies[3],5)); ASSERT_FLOAT_EQ(11.25f,position(f.engine,body),1e-6f);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,nested,5)); ASSERT_FLOAT_EQ(15,position(f.engine,body),1e-6f);
    nmo_object_id_t part = eng_keyed_animation_part(f.engine,merged,body), nested_part = eng_keyed_animation_part(f.engine,nested,body);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    f.engine->dynamic.delete_all = true;
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(0u,eng_animation_character(f.engine,merged)); ASSERT_EQ(0u,eng_animation_character(f.engine,nested));
    eng_animation_source_t source; ASSERT_FALSE(eng_animation_source(f.engine,part,&source)); ASSERT_FALSE(eng_animation_source(f.engine,nested_part,&source));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copies[3],5)); ASSERT_FLOAT_EQ(11.25f,position(f.engine,body),1e-6f);
    fixture_end(&f);
}

TEST(engine_blend, union_stretches_variable_key_formats_and_detaches_shared_data)
{
    const CK_OBJECTANIMATION_FORMAT formats[] = {CKOBJANIM_FORMAT_CONTROLLERS,CKOBJANIM_FORMAT_NEWDATA,CKOBJANIM_FORMAT_LEGACY};
    for (size_t mode = 0; mode < COUNT(formats); mode++) {
        engine_fixture_t f; fixture_begin_document(&f);
        nmo_object_id_t body, character = actor(&f,&body);
        nmo_object_id_t keys = fixture_create(&f,NMO_CID_OBJECTANIMATION,"Keys");
        nmo_object_id_t alias = fixture_create(&f,NMO_CID_OBJECTANIMATION,"Alias");
        struct { float time,x,y,z; uint32_t flags; } bezier[] = {{0,0,0,0,0},{5,10,0,0,0}};
        float axis[12] = {0};
        uint32_t stride = nmo_objanim_controller_format_key_size(0x2f200b08u,formats[mode]);
        axis[stride/sizeof(float)] = 5;
        struct { uint32_t header[3]; struct { float time,position[3]; } keys[2]; } morph = {{2,1,0},{{0,{0,0,0}},{5,{10,0,0}}}};
        nmo_objanim_controller_t controllers[] = {{0x921ab801u,2,sizeof(bezier),bezier},
            {0x2f200b08u,2,stride*2,axis},{NMO_OBJANIM_CONTROLLER_MORPH,0,sizeof(morph),&morph}};
        float linear[] = {0,0,0,0,5,10,0,0};
        if (mode != 0) controllers[0] = (nmo_objanim_controller_t){0x637c4301u,2,sizeof(linear),linear};
        nmo_objectanimation_state_t *state = fixture_state(&f,keys), *shared = fixture_state(&f,alias);
        state->format = formats[mode]; state->has_length = 1; state->length = 5;
        state->controllers = controllers; state->controller_count = mode == 0 ? 3 : 2;
        nmo_objanim_morph_key_t separate[] = {{0,sizeof(morph.keys[0].position),morph.keys[0].position},
            {5,sizeof(morph.keys[1].position),morph.keys[1].position}};
        if (mode != 0) {
            state->has_morph_counts = 1; state->morph_vertex_count = 1; state->morph_key_count = 2;
            state->morph_key_parsed_count = 2; state->morph_keys = separate;
        }
        shared->format = CKOBJANIM_FORMAT_SHARED; shared->has_shared_anim = 1;
        shared->shared_anim = nmo_ref_from_id(keys); shared->entity = nmo_ref_from_id(body);
        shared->flags = CK_OBJECTANIMATION_IGNOREROT;
        nmo_object_id_t first = clip(&f,"Short",character,5,&alias,1), second = clip(&f,"Long",0,20,NULL,0);
        fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
        nmo_object_id_t result = eng_keyed_animation_merge(f.engine,first,second,false);
        ASSERT_TRUE(result != 0);
        nmo_object_id_t part = eng_keyed_animation_part(f.engine,result,body); ASSERT_TRUE(part != 0);
        const nmo_objectanimation_state_t *copy = nmo_object_get_state(eng_find_object(f.engine,part));
        ASSERT_EQ(formats[mode],copy->format); ASSERT_EQ(CK_OBJECTANIMATION_IGNOREROT,copy->flags);
        ASSERT_FALSE(copy->has_shared_anim); ASSERT_FLOAT_EQ(20,copy->length,0);
        float time; memcpy(&time,(uint8_t *)copy->controllers[0].data+(mode == 0 ? sizeof(bezier[0]) : 4*sizeof(float)),sizeof(time)); ASSERT_FLOAT_EQ(20,time,0);
        memcpy(&time,(uint8_t *)copy->controllers[1].data+stride,sizeof(time)); ASSERT_FLOAT_EQ(20,time,0);
        ASSERT_TRUE(copy->controllers[0].data != bezier); ASSERT_FLOAT_EQ(5,bezier[1].time,0);
        if (mode == 0) {
            memcpy(&time,(uint8_t *)copy->controllers[2].data+12+sizeof(morph.keys[0]),sizeof(time)); ASSERT_FLOAT_EQ(20,time,0);
            ASSERT_FLOAT_EQ(5,morph.keys[1].time,0);
        } else {
            ASSERT_FLOAT_EQ(20,copy->morph_keys[1].time_step,0); ASSERT_FLOAT_EQ(5,separate[1].time_step,0);
            ASSERT_TRUE(copy->morph_keys[1].data != separate[1].data);
        }
        fixture_end(&f);
    }
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t local(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = fixture_state(f,block);
    return state != NULL ? nmo_behavior_ref_array_get_id(&state->local_parameters,pos) : 0;
}

static nmo_object_id_t saved_input(engine_fixture_t *f, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t source = 0;
    if (nmo_script_edit_ensure_input_parameter_source(f->tx,pin,&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(f->tx,source,text) != NMO_OK) return 0;
    return source;
}

static nmo_object_id_t saved_object(engine_fixture_t *f, nmo_object_id_t pin, nmo_object_id_t object)
{
    char text[32]; snprintf(text,sizeof(text),"%u",object);
    return saved_input(f,pin,text);
}

typedef struct legacy_blend_fixture {
    engine_fixture_t f;
    nmo_object_id_t source, source_body, owner_body, extra, first, second, block, inputs[2], output;
} legacy_blend_fixture_t;

static void legacy_begin(legacy_blend_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f); nmo_object_id_t extra_entity = g->f.owner_id;
    g->source = actor(&g->f,&g->source_body); ASSERT_TRUE(g->source != 0);
    g->f.owner_id = actor(&g->f,&g->owner_body); ASSERT_TRUE(g->f.owner_id != 0);
    ASSERT_EQ(NMO_OK,nmo_object_repository_rename(g->f.repo,g->source,"Legacy Source Character"));
    ASSERT_EQ(NMO_OK,nmo_object_repository_rename(g->f.repo,g->source_body,"Legacy Source Body"));
    ASSERT_EQ(NMO_OK,nmo_object_repository_rename(g->f.repo,g->f.owner_id,"Legacy Owner Character"));
    ASSERT_EQ(NMO_OK,nmo_object_repository_rename(g->f.repo,g->owner_body,"Legacy Owner Body"));
    nmo_object_id_t a = channel(&g->f,"Legacy A",g->source_body,10,10), b = channel(&g->f,"Legacy B",g->source_body,20,30);
    g->extra = channel(&g->f,"Legacy Left Only",extra_entity,10,40);
    nmo_object_id_t c = channel(&g->f,"Legacy Right Only",g->owner_body,20,80), left[] = {a,g->extra}, right[] = {b,c};
    ASSERT_TRUE(a != 0 && b != 0 && g->extra != 0 && c != 0);
    g->first = clip(&g->f,"Legacy First",g->source,10,left,COUNT(left)); g->second = clip(&g->f,"Legacy Second",0,20,right,COUNT(right)); ASSERT_TRUE(g->first != 0 && g->second != 0);
    g->f.script_id = fixture_add_script(&g->f,"Legacy Blend Script",&g->f.script_in); ASSERT_TRUE(g->f.script_id != 0); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_LEGACY_CREATE,"Create Legacy Blend"); ASSERT_TRUE(g->block != 0);
    g->inputs[0] = saved_object(&g->f,BLOCK_PIN(&g->f,g->block,0),g->first); g->inputs[1] = saved_object(&g->f,BLOCK_PIN(&g->f,g->block,1),g->second); g->output = BLOCK_POUT(&g->f,g->block,0);
    ASSERT_TRUE(g->inputs[0] != 0 && g->inputs[1] != 0 && g->output != 0);
}

static void legacy_ready(legacy_blend_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_LEGACY_CREATE));
}

static void legacy_notify(legacy_blend_fixture_t *g)
{
    int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g->f.engine,g->block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result);
}

static nmo_object_id_t legacy_edit(legacy_blend_fixture_t *g)
{
    legacy_notify(g);
    nmo_object_id_t animation = 0; read_value(&g->f,g->output,&animation,sizeof(animation)); return animation;
}

TEST(engine_blend, obsolete_create_uses_only_edit_callbacks_and_keeps_the_default_union_on_the_behavior_owner)
{
    legacy_blend_fixture_t g; legacy_begin(&g); const nmo_behavior_state_t *prototype = fixture_state(&g.f,g.block); ASSERT_EQ(0u,prototype->inputs.count); ASSERT_EQ(0u,prototype->outputs.count); ASSERT_EQ(0u,prototype->local_parameters.count); ASSERT_EQ(0u,fixture_target(&g.f,g.block)); legacy_ready(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); nmo_object_id_t unset = 99; read_value(&g.f,g.output,&unset,sizeof(unset)); ASSERT_EQ(0u,unset); ASSERT_EQ(0u,eng_character_animation_count(g.f.engine,g.f.owner_id));
    nmo_object_id_t result = legacy_edit(&g); ASSERT_TRUE(result != 0); const nmo_keyedanimation_state_t *state = nmo_object_get_state(eng_find_object(g.f.engine,result)); ASSERT_EQ(3u,state->animation_count); ASSERT_EQ(g.f.owner_id,eng_animation_character(g.f.engine,result)); ASSERT_EQ(1u,eng_character_animation_count(g.f.engine,g.f.owner_id)); ASSERT_EQ(result,eng_character_animation(g.f.engine,g.f.owner_id,0)); ASSERT_EQ(0u,eng_character_animation_count(g.f.engine,g.source)); ASSERT_FLOAT_EQ(.5f,eng_animation_merge_factor(g.f.engine,result),0);
    g.f.engine->entities.data[eng_entity_index(g.f.engine,g.f.owner_id)].moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED; ASSERT_TRUE(eng_animation_set_frame(g.f.engine,result,10)); ASSERT_FLOAT_EQ(10,position(g.f.engine,g.source_body),1e-6f); ASSERT_FLOAT_EQ(40,position(g.f.engine,g.owner_body),1e-6f); nmo_object_id_t extra_entity = eng_object_animation_entity(g.f.engine,g.extra); ASSERT_FLOAT_EQ(20,position(g.f.engine,extra_entity),1e-6f); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); ASSERT_EQ(0u,runs(&g.f,g.block)); ASSERT_EQ(result,legacy_edit(&g)); fixture_end(&g.f);
}

TEST(engine_blend, obsolete_create_before_start_keeps_the_first_result_when_inputs_change_or_disappear)
{
    legacy_blend_fixture_t g; legacy_begin(&g); legacy_ready(&g); nmo_object_id_t result = legacy_edit(&g); ASSERT_TRUE(result != 0); ASSERT_TRUE(eng_animation_set_merge_factor(g.f.engine,result,.25f));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.inputs[0],&g.second,sizeof(g.second))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.inputs[1],&g.first,sizeof(g.first))); ASSERT_EQ(result,legacy_edit(&g)); ASSERT_FLOAT_EQ(.25f,eng_animation_merge_factor(g.f.engine,result),0); nmo_engine_destroy_object(g.f.engine,g.first); nmo_engine_destroy_object(g.f.engine,g.second); ASSERT_EQ(result,legacy_edit(&g));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); ASSERT_EQ(result,legacy_edit(&g)); ASSERT_EQ(1u,eng_character_animation_count(g.f.engine,g.f.owner_id)); ASSERT_EQ(0u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_blend, obsolete_create_preserves_live_outputs_without_attaching_them_to_the_owner)
{
    legacy_blend_fixture_t g; legacy_begin(&g); legacy_ready(&g);
    const nmo_object_id_t outputs[] = {g.second,g.source_body};
    for (size_t i = 0; i < COUNT(outputs); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,&outputs[i],sizeof(outputs[i]))); ASSERT_EQ(outputs[i],legacy_edit(&g)); ASSERT_EQ(0u,eng_character_animation_count(g.f.engine,g.f.owner_id));
    }
    ASSERT_EQ(0u,eng_animation_character(g.f.engine,g.second)); fixture_end(&g.f);
}

TEST(engine_blend, obsolete_create_preserves_stale_outputs_for_missing_inputs_and_clears_them_after_failed_factories)
{
    for (int kind = 0; kind < 7; kind++) {
        legacy_blend_fixture_t g; legacy_begin(&g); nmo_object_id_t base = fixture_create(&g.f,NMO_CID_ANIMATION,"Base Animation"); ASSERT_TRUE(base != 0); legacy_ready(&g); nmo_object_id_t sentinel = 0x7ffffffeu, missing = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,&sentinel,sizeof(sentinel)));
        if (kind < 2) ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.inputs[kind],&missing,sizeof(missing)));
        else if (kind < 4) nmo_engine_destroy_object(g.f.engine,kind == 2 ? g.first : g.second);
        else if (kind == 4) ASSERT_TRUE(eng_animation_set_character(g.f.engine,g.first,0));
        else ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.inputs[kind-5],&base,sizeof(base)));
        ASSERT_EQ(kind < 4 ? sentinel : 0u,legacy_edit(&g)); ASSERT_EQ(0u,eng_character_animation_count(g.f.engine,g.f.owner_id)); fixture_end(&g.f);
    }
}

TEST(engine_blend, obsolete_create_replaces_deleted_or_cleared_outputs_only_once)
{
    legacy_blend_fixture_t g; legacy_begin(&g); legacy_ready(&g); nmo_object_id_t first = legacy_edit(&g); ASSERT_TRUE(first != 0); nmo_engine_destroy_object(g.f.engine,first);
    nmo_object_id_t second = legacy_edit(&g); ASSERT_TRUE(second != 0 && second != first); ASSERT_EQ(1u,eng_character_animation_count(g.f.engine,g.f.owner_id)); ASSERT_EQ(second,legacy_edit(&g)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,&(nmo_object_id_t){0},sizeof(nmo_object_id_t)));
    nmo_object_id_t third = legacy_edit(&g); ASSERT_TRUE(third != 0 && third != second); ASSERT_EQ(2u,eng_character_animation_count(g.f.engine,g.f.owner_id)); ASSERT_EQ(third,legacy_edit(&g)); fixture_end(&g.f);
}

TEST(engine_blend, obsolete_create_inherits_source_dynamic_lifetime_and_leaves_document_membership_and_output_untouched)
{
    for (int dynamic = 0; dynamic < 2; dynamic++) {
        legacy_blend_fixture_t g; legacy_begin(&g); legacy_ready(&g); if (dynamic) eng_mark_dynamic(g.f.engine,g.first); nmo_object_id_t result = legacy_edit(&g); ASSERT_TRUE(result != 0); const nmo_keyedanimation_state_t *merged = nmo_object_get_state(eng_find_object(g.f.engine,result)); nmo_object_id_t parts[3]; ASSERT_EQ(COUNT(parts),merged->animation_count);
        for (size_t i = 0; i < COUNT(parts); i++) parts[i] = eng_object_ref(g.f.engine,result,&merged->animation_ids[i]);
        ASSERT_EQ(0u,nmo_parameter_object_id(fixture_state(&g.f,g.output))); ASSERT_EQ(0u,((nmo_character_state_t *)fixture_state(&g.f,g.f.owner_id))->animations.count);
        ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); g.f.engine->dynamic.delete_all = true; ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(dynamic ? 0u : 1u,eng_character_animation_count(g.f.engine,g.f.owner_id)); ASSERT_EQ(dynamic ? 0u : g.f.owner_id,eng_animation_character(g.f.engine,result));
        for (size_t i = 0; i < COUNT(parts); i++) { eng_animation_source_t source; ASSERT_EQ(!dynamic,eng_animation_source(g.f.engine,parts[i],&source)); }
        fixture_end(&g.f);
    }
}

TEST(engine_blend, obsolete_edit_output_drives_runtime_factor_and_frame_blocks_without_executing_the_editor_block)
{
    legacy_blend_fixture_t g; legacy_begin(&g); nmo_object_id_t factor = fixture_block(&g.f,GUID_FACTOR,"Legacy Set Factor"), frame = fixture_block(&g.f,GUID_FRAME,"Legacy Apply Frame"); ASSERT_TRUE(factor != 0 && frame != 0);
    ASSERT_TRUE(saved_input(&g.f,BLOCK_PIN(&g.f,factor,1),"25%") != 0); ASSERT_TRUE(saved_input(&g.f,BLOCK_PIN(&g.f,frame,0),"10") != 0); ASSERT_TRUE(saved_object(&g.f,fixture_target(&g.f,frame),g.f.owner_id) != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,g.output,BLOCK_PIN(&g.f,factor,0))); ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,g.output,BLOCK_PIN(&g.f,frame,1))); fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,factor,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,factor,0),BLOCK_IN(&g.f,frame,0),0); legacy_ready(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0u,eng_character_animation_count(g.f.engine,g.f.owner_id)); nmo_object_id_t result = legacy_edit(&g); ASSERT_TRUE(result != 0);
    g.f.engine->entities.data[eng_entity_index(g.f.engine,g.f.owner_id)].moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED; ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_FLOAT_EQ(.25f,eng_animation_merge_factor(g.f.engine,result),0); ASSERT_FLOAT_EQ(7.5f,position(g.f.engine,g.source_body),1e-6f); ASSERT_EQ(2u,runs(&g.f,factor)); ASSERT_EQ(2u,runs(&g.f,frame)); ASSERT_EQ(0u,runs(&g.f,g.block)); ASSERT_EQ(result,legacy_edit(&g)); ASSERT_FLOAT_EQ(.25f,eng_animation_merge_factor(g.f.engine,result),0); fixture_end(&g.f);
}

TEST(engine_blend, obsolete_loaded_and_copied_callbacks_keep_remapped_outputs_and_recreate_after_originals_are_deleted)
{
    legacy_blend_fixture_t source; legacy_begin(&source); fixture_commit_bare(&source.f,NULL); const char *path = NMO_TEST_SCRATCH_FILE("engine_legacy_blend.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        legacy_blend_fixture_t g = {0}; fixture_begin_document(&g.f); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"legacy-blend.cmo",true,&file));
        g.block = nmo_engine_object_named(g.f.engine,"Create Legacy Blend",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL);
        g.f.owner_id = eng_module_id(module,source.f.owner_id); g.source = eng_module_id(module,source.source); g.owner_body = eng_module_id(module,source.owner_body); g.source_body = eng_module_id(module,source.source_body); g.first = eng_module_id(module,source.first); g.second = eng_module_id(module,source.second); g.extra = eng_module_id(module,source.extra); g.f.script_id = eng_module_id(module,source.f.script_id); g.inputs[0] = eng_module_id(module,source.inputs[0]); g.inputs[1] = eng_module_id(module,source.inputs[1]); g.output = eng_module_id(module,source.output);
        nmo_object_id_t unset = 99; read_value(&g.f,g.output,&unset,sizeof(unset)); ASSERT_EQ(0u,unset); ASSERT_EQ(0u,eng_character_animation_count(g.f.engine,g.f.owner_id)); ASSERT_EQ(g.f.owner_id,nmo_engine_block_owner(&(nmo_engine_block_t){g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}));
        nmo_object_id_t result = legacy_edit(&g); ASSERT_TRUE(result != 0); ASSERT_EQ(g.f.owner_id,eng_animation_character(g.f.engine,result)); ASSERT_EQ(3u,((nmo_keyedanimation_state_t *)nmo_object_get_state(eng_find_object(g.f.engine,result)))->animation_count); ASSERT_TRUE(eng_animation_set_merge_factor(g.f.engine,result,.25f)); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL));
        nmo_object_id_t ids[] = {g.f.owner_id,g.source,g.owner_body,g.source_body,g.first,g.second,eng_object_animation_entity(g.f.engine,g.extra),g.block,g.f.script_id,g.inputs[0],g.inputs[1],g.output,result}, copies[COUNT(ids)] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,COUNT(ids),false,copies)); for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
        nmo_object_id_t copied_output = 0; read_value(&g.f,copies[11],&copied_output,sizeof(copied_output)); ASSERT_EQ(copies[12],copied_output); g.f.owner_id = copies[0]; g.block = copies[7]; g.f.script_id = copies[8]; g.output = copies[11]; ASSERT_EQ(copies[12],legacy_edit(&g)); ASSERT_EQ(1u,eng_character_animation_count(g.f.engine,copies[0]));
        for (size_t i = 0; i < COUNT(ids); i++) { nmo_engine_destroy_object(g.f.engine,ids[i]); } g.f.engine->entities.data[eng_entity_index(g.f.engine,copies[0])].moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED; ASSERT_TRUE(eng_animation_set_frame(g.f.engine,copies[12],10)); ASSERT_FLOAT_EQ(7.5f,position(g.f.engine,copies[3]),1e-6f); ASSERT_EQ(copies[12],legacy_edit(&g)); ASSERT_EQ(copies[0],eng_animation_character(g.f.engine,copies[12]));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,&(nmo_object_id_t){0},sizeof(nmo_object_id_t))); nmo_object_id_t replacement = legacy_edit(&g); ASSERT_TRUE(replacement != 0 && replacement != copies[12]); ASSERT_EQ(copies[0],eng_animation_character(g.f.engine,replacement)); ASSERT_EQ(2u,eng_character_animation_count(g.f.engine,copies[0])); ASSERT_TRUE(eng_animation_set_frame(g.f.engine,replacement,10)); ASSERT_FLOAT_EQ(10,position(g.f.engine,copies[3]),1e-6f); ASSERT_EQ(replacement,legacy_edit(&g)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
    ASSERT_EQ(0u,nmo_parameter_object_id(fixture_state(&source.f,source.output))); ASSERT_EQ(0u,((nmo_character_state_t *)fixture_state(&source.f,source.f.owner_id))->animations.count); fixture_end(&source.f); remove(path);
}

TEST(engine_blend, create_block_honors_settings_reuses_absent_inputs_and_creates_each_trigger)
{
    for (int defaults = 0; defaults < 2; defaults++) {
        engine_fixture_t f; fixture_begin_document(&f);
        nmo_object_id_t body, character = actor(&f,&body);
        nmo_object_id_t recipient_body, recipient = actor(&f,&recipient_body);
        nmo_object_id_t a = channel(&f,"A",body,10,10), b = channel(&f,"B",body,20,30);
        nmo_object_id_t extra = channel(&f,"Extra",f.owner_id,10,40), left[] = {a,extra};
        nmo_object_id_t first = clip(&f,"First",character,10,left,COUNT(left)), second = clip(&f,"Second",0,20,&b,1);
        f.script_id = fixture_add_script(&f,"Create script",&f.script_in); fixture_begin_edit(&f);
        nmo_object_id_t block = fixture_block(&f,GUID_CREATE,"Create Blended Animation 2"); ASSERT_TRUE(block != 0);
        nmo_object_id_t input = saved_object(&f,BLOCK_PIN(&f,block,0),first);
        ASSERT_TRUE(input != 0 && saved_object(&f,BLOCK_PIN(&f,block,1),second) != 0);
        ASSERT_TRUE(saved_object(&f,fixture_target(&f,block),recipient) != 0);
        if (!defaults) {
            ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,local(&f,block,0),"FALSE"));
            ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,local(&f,block,1),"FALSE"));
        }
        fixture_link(&f,f.script_in,BLOCK_IN(&f,block,0),0);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_TRUE(nmo_engine_has_block(f.engine,GUID_CREATE)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        nmo_object_id_t result = 0; read_value(&f,BLOCK_POUT(&f,block,0),&result,sizeof(result)); ASSERT_TRUE(result != 0);
        const nmo_keyedanimation_state_t *state = nmo_object_get_state(eng_find_object(f.engine,result));
        ASSERT_EQ(defaults ? 1u : 2u,state->animation_count);
        ASSERT_EQ((size_t)defaults,eng_character_animation_count(f.engine,recipient));
        ASSERT_EQ(defaults ? recipient : character,eng_animation_character(f.engine,result));
        int32_t absent = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,input,&absent,sizeof(absent)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        nmo_object_id_t retained = 0; read_value(&f,BLOCK_POUT(&f,block,0),&retained,sizeof(retained)); ASSERT_EQ(result,retained);
        ASSERT_EQ((size_t)defaults,eng_character_animation_count(f.engine,recipient));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,input,&first,sizeof(first)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        nmo_object_id_t repeated = 0; read_value(&f,BLOCK_POUT(&f,block,0),&repeated,sizeof(repeated));
        ASSERT_TRUE(repeated != 0 && repeated != result); ASSERT_EQ((size_t)(defaults*2),eng_character_animation_count(f.engine,recipient));
        ASSERT_TRUE(eng_animation_set_character(f.engine,first,0));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        read_value(&f,BLOCK_POUT(&f,block,0),&retained,sizeof(retained)); ASSERT_EQ(0u,retained); ASSERT_EQ(4u,runs(&f,block));
        ASSERT_TRUE(eng_animation_set_character(f.engine,first,character));
        nmo_engine_destroy_object(f.engine,recipient);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        read_value(&f,BLOCK_POUT(&f,block,0),&retained,sizeof(retained)); ASSERT_TRUE(retained != 0);
        ASSERT_EQ(character,eng_animation_character(f.engine,retained)); ASSERT_EQ(5u,runs(&f,block));
        fixture_end(&f);
    }
}

TEST(engine_blend, saved_graph_creates_controls_and_applies_a_blend_after_module_load)
{
    engine_fixture_t source; fixture_begin_document(&source);
    nmo_object_id_t body, character = actor(&source,&body);
    nmo_object_id_t a = channel(&source,"Saved A",body,10,10), b = channel(&source,"Saved B",body,20,30);
    nmo_object_id_t first = clip(&source,"Saved First",character,10,&a,1), second = clip(&source,"Saved Second",character,20,&b,1);
    source.script_id = fixture_add_script(&source,"Saved Blend Script",&source.script_in); fixture_begin_edit(&source);
    nmo_object_id_t create = fixture_block(&source,GUID_CREATE,"Create saved blend");
    nmo_object_id_t factor = fixture_block(&source,GUID_FACTOR,"Set saved factor");
    nmo_object_id_t frame = fixture_block(&source,GUID_FRAME,"Apply saved frame");
    ASSERT_TRUE(create != 0 && factor != 0 && frame != 0);
    ASSERT_TRUE(saved_object(&source,fixture_target(&source,create),character) != 0);
    ASSERT_TRUE(saved_object(&source,BLOCK_PIN(&source,create,0),first) != 0);
    ASSERT_TRUE(saved_object(&source,BLOCK_PIN(&source,create,1),second) != 0);
    ASSERT_TRUE(saved_input(&source,BLOCK_PIN(&source,factor,1),"25%") != 0);
    ASSERT_TRUE(saved_object(&source,fixture_target(&source,frame),character) != 0);
    ASSERT_TRUE(saved_input(&source,BLOCK_PIN(&source,frame,0),"10") != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(source.tx,BLOCK_POUT(&source,create,0),BLOCK_PIN(&source,factor,0)));
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(source.tx,BLOCK_POUT(&source,create,0),BLOCK_PIN(&source,frame,1)));
    fixture_link(&source,source.script_in,BLOCK_IN(&source,create,0),0);
    fixture_link(&source,BLOCK_OUT(&source,create,0),BLOCK_IN(&source,factor,0),0);
    fixture_link(&source,BLOCK_OUT(&source,factor,0),BLOCK_IN(&source,frame,0),0);
    fixture_commit_bare(&source,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source,source.owner_id);
    owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&source,source.script_id);
    script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *path = NMO_TEST_SCRATCH_FILE("engine_blend_graph.cmo");
    ASSERT_EQ(NMO_OK,nmo_session_save_file(source.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&host,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
        int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"blend.cmo",true,&file));
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t loaded_create = nmo_engine_object_named(host.engine,"Create saved blend",NMO_CID_BEHAVIOR);
        nmo_object_id_t loaded_factor = nmo_engine_object_named(host.engine,"Set saved factor",NMO_CID_BEHAVIOR);
        nmo_object_id_t loaded_frame = nmo_engine_object_named(host.engine,"Apply saved frame",NMO_CID_BEHAVIOR);
        nmo_object_id_t loaded_character = nmo_engine_object_named(host.engine,"Character",NMO_CID_CHARACTER);
        nmo_object_id_t loaded_body = nmo_engine_object_named(host.engine,"Body",NMO_CID_BODYPART);
        ASSERT_TRUE(loaded_create != 0 && loaded_character != 0 && loaded_character != character);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(1u,runs(&host,loaded_create)); ASSERT_EQ(1u,runs(&host,loaded_factor)); ASSERT_EQ(1u,runs(&host,loaded_frame));
        ASSERT_EQ(1u,eng_character_animation_count(host.engine,loaded_character));
        nmo_object_id_t animation = eng_character_animation(host.engine,loaded_character,0); ASSERT_TRUE(animation != 0);
        ASSERT_FLOAT_EQ(.25f,eng_animation_merge_factor(host.engine,animation),0);
        ASSERT_FLOAT_EQ(7.5f,position(host.engine,loaded_body),1e-6f);
        float current = 0; ASSERT_TRUE(eng_animation_frame(host.engine,animation,&current)); ASSERT_FLOAT_EQ(10,current,0);
        ASSERT_EQ(0u,eng_character_active_animation(host.engine,loaded_character));
        fixture_end(&host);
    }
    fixture_end(&source); remove(path);
}

TEST(engine_blend, factory_keeps_cross_module_sources_and_local_copies_distinct)
{
    engine_fixture_t source; fixture_begin_document(&source);
    nmo_object_id_t b = channel(&source,"External common",source.owner_id,20,30);
    nmo_object_id_t extra = channel(&source,"External extra",source.owner_id,10,40), channels[] = {b,extra};
    ASSERT_TRUE(clip(&source,"External",0,20,channels,COUNT(channels)) != 0);
    const char *path = NMO_TEST_SCRATCH_FILE("engine_blend_external.cmo");
    ASSERT_EQ(NMO_OK,nmo_session_save_file(source.session,path,NULL,NULL));
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t body, character = actor(&f,&body), a = channel(&f,"Local",body,10,10);
    nmo_object_id_t first = clip(&f,"Local",character,10,&a,1);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_engine_set_file_resolver(f.engine,resolve_file,(void *)path);
    int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(f.engine,"external.cmo",false,&file));
    nmo_object_id_t second = nmo_engine_object_named(f.engine,"External",NMO_CID_KEYEDANIMATION);
    nmo_object_id_t external = nmo_engine_object_named(f.engine,"External common",NMO_CID_OBJECTANIMATION);
    nmo_object_id_t extra_part = nmo_engine_object_named(f.engine,"External extra",NMO_CID_OBJECTANIMATION);
    nmo_object_id_t external_entity = eng_object_animation_entity(f.engine,extra_part);
    ASSERT_TRUE(second != 0 && external != b && external_entity != 0);
    ASSERT_TRUE(eng_object_animation_set_entity(f.engine,external,body));
    nmo_object_id_t merged = eng_keyed_animation_merge(f.engine,first,second,false); ASSERT_TRUE(merged != 0);
    nmo_object_id_t part = eng_keyed_animation_part(f.engine,merged,body);
    ASSERT_EQ(external,eng_animation_merge_source(f.engine,part,true));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,merged,10)); ASSERT_FLOAT_EQ(10,position(f.engine,body),1e-6f);
    ASSERT_FLOAT_EQ(20,position(f.engine,external_entity),1e-6f);
    nmo_object_id_t copy = 0; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,&merged,1,false,&copy));
    ASSERT_TRUE(eng_animation_set_merge_factor(f.engine,copy,.25f));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copy,10)); ASSERT_FLOAT_EQ(7.5f,position(f.engine,body),1e-6f);
    nmo_engine_destroy_object(f.engine,external);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copy,10)); ASSERT_FLOAT_EQ(5,position(f.engine,body),1e-6f);
    fixture_end(&f); fixture_end(&source); remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_blend, live_factors_propagate_to_channels_and_copies_without_editing_loaded_data);
REGISTER_TEST(engine_blend, factor_block_uses_native_fractional_default_and_allows_absent_animation);
REGISTER_TEST(engine_blend, factory_pairs_live_targets_and_unions_independent_stretched_channels);
REGISTER_TEST(engine_blend, factory_preserves_nested_sources_copies_and_dynamic_lifetimes);
REGISTER_TEST(engine_blend, union_stretches_variable_key_formats_and_detaches_shared_data);
REGISTER_TEST(engine_blend, factory_keeps_cross_module_sources_and_local_copies_distinct);
REGISTER_TEST(engine_blend, create_block_honors_settings_reuses_absent_inputs_and_creates_each_trigger);
REGISTER_TEST(engine_blend, saved_graph_creates_controls_and_applies_a_blend_after_module_load);
REGISTER_TEST(engine_blend, obsolete_create_uses_only_edit_callbacks_and_keeps_the_default_union_on_the_behavior_owner);
REGISTER_TEST(engine_blend, obsolete_create_before_start_keeps_the_first_result_when_inputs_change_or_disappear);
REGISTER_TEST(engine_blend, obsolete_create_preserves_live_outputs_without_attaching_them_to_the_owner);
REGISTER_TEST(engine_blend, obsolete_create_preserves_stale_outputs_for_missing_inputs_and_clears_them_after_failed_factories);
REGISTER_TEST(engine_blend, obsolete_create_replaces_deleted_or_cleared_outputs_only_once);
REGISTER_TEST(engine_blend, obsolete_create_inherits_source_dynamic_lifetime_and_leaves_document_membership_and_output_untouched);
REGISTER_TEST(engine_blend, obsolete_edit_output_drives_runtime_factor_and_frame_blocks_without_executing_the_editor_block);
REGISTER_TEST(engine_blend, obsolete_loaded_and_copied_callbacks_keep_remapped_outputs_and_recreate_after_originals_are_deleted);
TEST_MAIN_END()
