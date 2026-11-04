#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_character.h"
#include "../../src/engine/engine_transform.h"
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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_blend, live_factors_propagate_to_channels_and_copies_without_editing_loaded_data);
REGISTER_TEST(engine_blend, factor_block_uses_native_fractional_default_and_allows_absent_animation);
TEST_MAIN_END()
