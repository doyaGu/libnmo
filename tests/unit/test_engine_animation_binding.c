#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_character.h"
#include "../../src/engine/engine_transform.h"
#include "object/builtin/nmo_character_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_ADD_ANIMATION NMO_GUID(0x6a794228,0x77197f5e)

static nmo_object_id_t bodypart(engine_fixture_t *f, nmo_object_id_t character, const char *name)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_BODYPART,name);
    nmo_bodypart_state_t *part = fixture_state(f,id);
    nmo_character_state_t *actor = fixture_state(f,character);
    if (part == NULL || actor == NULL) return 0;
    ((nmo_3dentity_state_t *)part)->parent = nmo_ref_from_id(character);
    part->has_character = 1; part->character = nmo_ref_from_id(character);
    if (actor->body_parts.element_size != sizeof(nmo_character_part_t)) {
        nmo_array_dispose(&actor->body_parts);
        if (nmo_array_init(&actor->body_parts,sizeof(nmo_character_part_t),0,NULL) != NMO_OK) return 0;
    }
    nmo_character_part_t entry = {.ref=nmo_ref_from_id(id)};
    return nmo_array_append(&actor->body_parts,&entry) == NMO_OK ? id : 0;
}

static nmo_object_id_t character_named(engine_fixture_t *f, const char *name, const char *body_name, nmo_object_id_t *body)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CHARACTER,name);
    *body = bodypart(f,id,body_name);
    nmo_character_state_t *state = fixture_state(f,id);
    if (state == NULL || *body == 0) return 0;
    state->root_body_part = nmo_ref_from_id(*body);
    return id;
}

static nmo_object_id_t character(engine_fixture_t *f, const char *name, nmo_object_id_t *body)
{
    return character_named(f,name,"Bone",body);
}

static nmo_object_id_t clip(engine_fixture_t *f, const char *name, nmo_object_id_t actor,
    nmo_object_id_t entity, nmo_object_id_t *channel)
{
    *channel = fixture_create(f,NMO_CID_OBJECTANIMATION,"Bone");
    nmo_objectanimation_state_t *part = fixture_state(f,*channel);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    float *keys = nmo_arena_alloc(arena,8*sizeof(float),_Alignof(float));
    nmo_objanim_controller_t *controller = nmo_arena_alloc(arena,sizeof(*controller),_Alignof(nmo_objanim_controller_t));
    nmo_ref_t *reference = nmo_arena_alloc(arena,sizeof(*reference),_Alignof(nmo_ref_t));
    if (part == NULL || keys == NULL || controller == NULL || reference == NULL) return 0;
    const float positions[] = {0,0,0,0,10,10,0,0}; memcpy(keys,positions,sizeof(positions));
    *controller = (nmo_objanim_controller_t){0x637c4301u,2,sizeof(positions),keys};
    part->format = CKOBJANIM_FORMAT_CONTROLLERS;
    part->has_length = 1; part->length = 10;
    part->entity = nmo_ref_from_id(entity); part->controllers = controller; part->controller_count = 1;
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,name);
    nmo_keyedanimation_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->base.has_data = state->base.has_length = state->base.has_character = state->base.has_root_entity = 1;
    state->base.character = nmo_ref_from_id(actor); state->base.root_entity = nmo_ref_from_id(entity);
    state->base.length = 10; state->base.frame_rate = 1000; state->base.flags = CKANIMATION_LINKTOFRAMERATE;
    *reference = nmo_ref_from_id(*channel); state->animation_ids = reference; state->animation_count = 1;
    return id;
}

static float x(nmo_engine_t *engine, nmo_object_id_t entity)
{
    float matrix[16];
    return nmo_engine_entity_world_matrix(engine,entity,matrix) ? matrix[12] : NAN;
}

TEST(engine_animation_binding, live_owner_and_target_changes_drive_playback_without_editing_source_refs)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character(&f,"Target",&new_body);
    nmo_object_id_t channel, animation = clip(&f,"Clip",old,old_body,&channel);
    ASSERT_TRUE(old != 0 && target != 0 && animation != 0);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_EQ(old,eng_animation_character(f.engine,animation)); ASSERT_EQ(old_body,eng_object_animation_entity(f.engine,channel));
    ASSERT_TRUE(eng_animation_set_character(f.engine,animation,target));
    ASSERT_TRUE(eng_object_animation_set_entity(f.engine,channel,new_body));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,animation,5));
    ASSERT_FLOAT_EQ(5,x(f.engine,new_body),1e-6f); ASSERT_FLOAT_EQ(0,x(f.engine,old_body),0);
    ASSERT_EQ(channel,eng_keyed_animation_part(f.engine,animation,new_body));
    ASSERT_EQ(0u,eng_keyed_animation_part(f.engine,animation,old_body));
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,target,animation,CK_TRANSITION_FROMNOW));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_set_next_animation(f.engine,old,animation,CK_TRANSITION_FROMNOW));
    ASSERT_EQ(old,nmo_ref_runtime_id(&((const nmo_keyedanimation_state_t *)fixture_state(&f,animation))->base.character));
    ASSERT_EQ(old_body,nmo_ref_runtime_id(&((const nmo_objectanimation_state_t *)fixture_state(&f,channel))->entity));
    ASSERT_FALSE(eng_animation_set_character(f.engine,animation,new_body));
    ASSERT_FALSE(eng_object_animation_set_entity(f.engine,channel,animation));
    fixture_end(&f);
}

TEST(engine_animation_binding, copied_runtime_bindings_remap_and_deleted_targets_stay_unbound)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character(&f,"Target",&new_body);
    nmo_object_id_t channel, animation = clip(&f,"Clip",old,old_body,&channel);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_TRUE(eng_animation_set_character(f.engine,animation,target));
    ASSERT_TRUE(eng_object_animation_set_entity(f.engine,channel,new_body));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,animation,5));
    nmo_object_id_t originals[] = {target,new_body,animation}, copies[COUNT(originals)] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,originals,COUNT(originals),false,copies));
    nmo_object_id_t copied_channel = eng_keyed_animation_part(f.engine,copies[2],copies[1]);
    ASSERT_TRUE(copied_channel != 0 && copied_channel != channel);
    ASSERT_EQ(copies[0],eng_animation_character(f.engine,copies[2]));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copies[2],6));
    ASSERT_FLOAT_EQ(6,x(f.engine,copies[1]),1e-6f); ASSERT_FLOAT_EQ(5,x(f.engine,new_body),1e-6f);
    nmo_engine_destroy_object(f.engine,new_body); nmo_engine_destroy_object(f.engine,target);
    ASSERT_EQ(0u,eng_object_animation_entity(f.engine,channel)); ASSERT_EQ(0u,eng_animation_character(f.engine,animation));
    ASSERT_EQ(copies[1],eng_object_animation_entity(f.engine,copied_channel));
    ASSERT_FALSE(eng_object_animation_set_entity(f.engine,channel,new_body));
    ASSERT_TRUE(eng_object_animation_set_entity(f.engine,channel,0)); ASSERT_TRUE(eng_animation_set_character(f.engine,animation,0));
    ASSERT_EQ(0u,eng_animation_character(f.engine,animation));
    fixture_end(&f);
}

TEST(engine_animation_binding, secondary_gates_follow_live_channel_targets)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character(&f,"Target",&new_body);
    nmo_object_id_t channel, animation = clip(&f,"Secondary",old,old_body,&channel);
    nmo_object_id_t other_channel, other = clip(&f,"Other",target,new_body,&other_channel);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_TRUE(eng_animation_set_character(f.engine,animation,target));
    ASSERT_TRUE(eng_object_animation_set_entity(f.engine,channel,new_body));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,target,animation,2,CKSECONDARYANIMATION_LASTFRAME,0));
    ASSERT_FLOAT_EQ(2,x(f.engine,new_body),1e-6f);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,other,7)); ASSERT_FLOAT_EQ(2,x(f.engine,new_body),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(f.engine,target,animation));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,other,7)); ASSERT_FLOAT_EQ(7,x(f.engine,new_body),1e-6f);
    ASSERT_FLOAT_EQ(0,x(f.engine,old_body),0);
    fixture_end(&f);
}

static nmo_object_id_t add_block(engine_fixture_t *f, nmo_object_id_t target, nmo_object_id_t animation)
{
    f->script_id = fixture_add_script(f,"Add Animation Script",&f->script_in);
    fixture_begin_edit(f);
    nmo_object_id_t block = fixture_block(f,GUID_ADD_ANIMATION,"Add Animation");
    if (block == 0) return 0;
    fixture_input_int(f,fixture_target(f,block),(int32_t)target);
    fixture_input_int(f,BLOCK_PIN(f,block,0),(int32_t)animation);
    fixture_link(f,f->script_in,BLOCK_IN(f,block,0),0);
    return block;
}

static void strict_commit(engine_fixture_t *f)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(f,&options);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f->engine,NMO_ENGINE_START_ALL));
}

TEST(engine_animation_binding, add_animation_transfers_membership_preserves_playback_and_recenters_repeated_adds)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character(&f,"Target",&new_body);
    nmo_object_id_t channel, animation = clip(&f,"Clip",old,old_body,&channel);
    nmo_character_state_t *source_actor = fixture_state(&f,old);
    source_actor->active_animation = nmo_ref_from_id(animation);
    nmo_ref_t aliases[] = {nmo_ref_from_id(animation),nmo_ref_from_id(animation)};
    nmo_array_dispose(&source_actor->animations);
    ASSERT_EQ(NMO_OK,nmo_array_alloc(&source_actor->animations,sizeof(nmo_ref_t),2,NULL));
    memcpy(source_actor->animations.data,aliases,sizeof(aliases));
    nmo_keyedanimation_state_t *saved = fixture_state(&f,animation);
    saved->base.has_current_step = 1; saved->base.current_step = .5f;
    nmo_object_id_t block = add_block(&f,target,animation);
    ASSERT_TRUE(block != 0); strict_commit(&f);
    ASSERT_EQ(2u,eng_character_animation_count(f.engine,old));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(1u,runs(&f,block)); ASSERT_EQ(target,eng_animation_character(f.engine,animation));
    ASSERT_EQ(0u,eng_character_animation_count(f.engine,old)); ASSERT_EQ(1u,eng_character_animation_count(f.engine,target));
    ASSERT_EQ(animation,eng_character_animation(f.engine,target,0));
    ASSERT_EQ(animation,eng_character_active_animation(f.engine,old)); ASSERT_EQ(0u,eng_character_active_animation(f.engine,target));
    ASSERT_EQ(new_body,eng_object_animation_entity(f.engine,channel));
    float frame = 0; ASSERT_TRUE(eng_animation_frame(f.engine,animation,&frame)); ASSERT_FLOAT_EQ(5,frame,0);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,animation,8)); ASSERT_FLOAT_EQ(8,x(f.engine,new_body),1e-6f);
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(1u,eng_character_animation_count(f.engine,target));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,animation,9)); ASSERT_FLOAT_EQ(1,x(f.engine,new_body),1e-6f);
    nmo_object_id_t originals[] = {target,new_body,animation}, copies[COUNT(originals)] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,originals,COUNT(originals),false,copies));
    ASSERT_EQ(1u,eng_character_animation_count(f.engine,copies[0]));
    ASSERT_EQ(copies[2],eng_character_animation(f.engine,copies[0],0));
    ASSERT_EQ(copies[0],eng_animation_character(f.engine,copies[2]));
    nmo_engine_destroy_object(f.engine,copies[2]);
    ASSERT_EQ(0u,eng_character_animation_count(f.engine,copies[0])); ASSERT_EQ(1u,eng_character_animation_count(f.engine,target));
    ASSERT_EQ(2u,source_actor->animations.count); ASSERT_EQ(old,nmo_ref_runtime_id(&saved->base.character));
    ASSERT_EQ(0u,((const nmo_character_state_t *)fixture_state(&f,target))->animations.count);
    fixture_end(&f);
}

TEST(engine_animation_binding, add_animation_keeps_exact_and_historical_suffix_matching_order)
{
    const char *names[] = {"Bone001","Bone","Bone12x","Bone1x3","Bone1234","Boneab3","bone001","Bone"};
    for (size_t i = 0; i < COUNT(names); i++) {
        engine_fixture_t f; fixture_begin_document(&f);
        nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character_named(&f,"Target",names[i],&new_body);
        nmo_object_id_t expected = i == 0 || i == 2 ? new_body : old_body;
        if (i == 0) expected = bodypart(&f,target,"Bone002");
        if (i == 1) { expected = bodypart(&f,target,"Bone"); ASSERT_TRUE(bodypart(&f,target,"Bone001") != 0); }
        if (i == 7) expected = bodypart(&f,target,"Bone001");
        nmo_object_id_t channel, animation = clip(&f,"Clip",old,i == 7 ? new_body : old_body,&channel);
        nmo_object_id_t block = add_block(&f,target,animation);
        ASSERT_TRUE(block != 0); strict_commit(&f);
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        ASSERT_EQ(expected,eng_object_animation_entity(f.engine,channel));
        ASSERT_EQ(target,eng_animation_character(f.engine,animation));
        fixture_end(&f);
    }
}

TEST(engine_animation_binding, duplicate_channel_slots_share_the_exact_match_marker)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character(&f,"Target",&new_body);
    ASSERT_TRUE(bodypart(&f,target,"Bone001") != 0);
    nmo_object_id_t channel, animation = clip(&f,"Clip",old,old_body,&channel);
    nmo_keyedanimation_state_t *state = fixture_state(&f,animation);
    state->animation_ids = nmo_arena_alloc(nmo_session_get_arena(f.session),2*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    ASSERT_NOT_NULL(state->animation_ids);
    state->animation_ids[0] = state->animation_ids[1] = nmo_ref_from_id(channel); state->animation_count = 2;
    nmo_object_id_t block = add_block(&f,target,animation); ASSERT_TRUE(block != 0); strict_commit(&f);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(new_body,eng_object_animation_entity(f.engine,channel));
    fixture_end(&f);
}

TEST(engine_animation_binding, plain_animations_transfer_and_missing_targets_or_animations_keep_control_flow)
{
    for (int mode = 0; mode < 3; mode++) {
        engine_fixture_t f; fixture_begin_document(&f);
        nmo_object_id_t body, target = character(&f,"Target",&body);
        nmo_object_id_t animation = fixture_create(&f,NMO_CID_ANIMATION,"Plain");
        nmo_object_id_t block = add_block(&f,mode == 1 ? 0 : target,mode == 2 ? body : animation);
        nmo_object_id_t after = fixture_block(&f,NMO_GUID(0x302561c4,0x0d282980),"After");
        fixture_link(&f,BLOCK_OUT(&f,block,0),BLOCK_IN(&f,after,0),0);
        strict_commit(&f);
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        ASSERT_EQ(1u,runs(&f,after)); ASSERT_EQ(mode == 0 ? target : 0,eng_animation_character(f.engine,animation));
        ASSERT_EQ(mode == 0 ? 1u : 0u,eng_character_animation_count(f.engine,target));
        fixture_end(&f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_animation_binding, live_owner_and_target_changes_drive_playback_without_editing_source_refs);
REGISTER_TEST(engine_animation_binding, copied_runtime_bindings_remap_and_deleted_targets_stay_unbound);
REGISTER_TEST(engine_animation_binding, secondary_gates_follow_live_channel_targets);
REGISTER_TEST(engine_animation_binding, add_animation_transfers_membership_preserves_playback_and_recenters_repeated_adds);
REGISTER_TEST(engine_animation_binding, add_animation_keeps_exact_and_historical_suffix_matching_order);
REGISTER_TEST(engine_animation_binding, duplicate_channel_slots_share_the_exact_match_marker);
REGISTER_TEST(engine_animation_binding, plain_animations_transfer_and_missing_targets_or_animations_keep_control_flow);
TEST_MAIN_END()
