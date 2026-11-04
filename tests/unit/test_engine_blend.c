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
    f.script_id = fixture_add_script(&f,"Blend control",&f.script_in); fixture_begin_edit(&f);
    nmo_object_id_t block = fixture_block(&f,GUID_FACTOR,"Set Blended Animation Factor"); ASSERT_TRUE(block != 0);
    fixture_input_int(&f,BLOCK_PIN(&f,block,0),(int32_t)part);
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
    int32_t absent = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,animation_param,&absent,sizeof(absent)));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(3u,runs(&f,block));
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
TEST_MAIN_END()
