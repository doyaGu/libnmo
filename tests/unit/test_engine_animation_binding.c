#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_character.h"
#include "../../src/engine/engine_transform.h"
#include "object/builtin/nmo_character_schemas.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "core/nmo_utils.h"

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
    ((nmo_3dentity_state_t *)part)->has_entityndata_chunk = 1;
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

static const char *resolve_file(void *data, const char *name)
{
    (void)name;
    return data;
}

static void channel_format(engine_fixture_t *f, nmo_object_id_t channel, nmo_objectanimation_format_t format)
{
    nmo_objectanimation_state_t *part = fixture_state(f,channel);
    if (format == CKOBJANIM_FORMAT_SHARED) {
        nmo_object_id_t shared = fixture_create(f,NMO_CID_OBJECTANIMATION,"Shared Keys");
        nmo_objectanimation_state_t *keys = fixture_state(f,shared);
        *keys = *part; keys->entity = nmo_ref_from_id(0);
        part->has_shared_anim = 1; part->shared_anim = nmo_ref_from_id(shared);
        part->controllers = NULL; part->controller_count = 0;
    } else if (format == CKOBJANIM_FORMAT_LEGACY) {
        nmo_object_t *object = nmo_object_repository_find_by_id(f->repo,channel);
        object->chunk = nmo_chunk_create(nmo_session_get_arena(f->session));
        ASSERT_NOT_NULL(object->chunk);
        object->chunk->class_id = NMO_CID_OBJECTANIMATION; object->chunk->data_version = 0;
    }
    part->format = format;
}

TEST(engine_animation_binding, loaded_animation_transfers_keep_cross_module_warps_and_copies_bound)
{
    const nmo_objectanimation_format_t formats[] = {CKOBJANIM_FORMAT_CONTROLLERS,
        CKOBJANIM_FORMAT_NEWDATA,CKOBJANIM_FORMAT_LEGACY,CKOBJANIM_FORMAT_SHARED};
    for (size_t i = 0; i < COUNT(formats); i++) {
        engine_fixture_t source; fixture_begin_document(&source);
        nmo_object_id_t old_body, old = character(&source,"Imported",&old_body), channel;
        nmo_object_id_t animation = clip(&source,"Incoming",old,old_body,&channel);
        channel_format(&source,channel,formats[i]);
        nmo_character_state_t *saved_actor = fixture_state(&source,old);
        nmo_array_dispose(&saved_actor->animations);
        ASSERT_EQ(NMO_OK,nmo_array_alloc(&saved_actor->animations,sizeof(nmo_ref_t),1,NULL));
        *(nmo_ref_t *)saved_actor->animations.data = nmo_ref_from_id(animation);
        fixture_begin_edit(&source); fixture_commit_bare(&source,NULL);
        const char *path = NMO_TEST_SCRATCH_FILE("engine_add_animation.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(source.session,path,NULL,NULL));
        engine_fixture_t host; fixture_begin_document(&host);
        nmo_object_id_t body, target = character(&host,"Target",&body), spare = bodypart(&host,target,"Other");
        nmo_object_id_t a_channel, a = clip(&host,"Primary",target,body,&a_channel);
        nmo_object_id_t b_channel, b = clip(&host,"Destination",target,body,&b_channel);
        nmo_object_id_t block = add_block(&host,target,0), input = 0;
        ASSERT_TRUE(block != 0);
        ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(host.tx,BLOCK_PIN(&host,block,0),&input));
        strict_commit(&host);
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
        int32_t file;
        ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"animation.cmo",true,&file)); ASSERT_TRUE(file > 0);
        nmo_object_id_t imported = nmo_engine_object_named(host.engine,"Imported",NMO_CID_CHARACTER);
        nmo_object_id_t incoming = nmo_engine_object_named(host.engine,"Incoming",NMO_CID_KEYEDANIMATION);
        nmo_object_id_t imported_body = eng_character_bodypart(host.engine,imported,0);
        nmo_object_id_t incoming_channel = eng_keyed_animation_part(host.engine,incoming,imported_body);
        ASSERT_TRUE(imported != 0 && incoming != 0 && incoming_channel != 0 && incoming != animation);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,input,&incoming,4));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(target,eng_animation_character(host.engine,incoming)); ASSERT_EQ(body,eng_object_animation_entity(host.engine,incoming_channel));
        ASSERT_EQ(0u,eng_character_animation_count(host.engine,imported)); ASSERT_EQ(1u,eng_character_animation_count(host.engine,target));
        ASSERT_EQ(imported_body,eng_keyed_animation_root_entity(host.engine,incoming));
        ASSERT_TRUE(eng_animation_set_frame(host.engine,incoming,4)); ASSERT_FLOAT_EQ(4,x(host.engine,body),1e-6f);
        eng_animation_source_t data; ASSERT_TRUE(eng_animation_source(host.engine,incoming_channel,&data)); ASSERT_EQ(formats[i],data.state->format);
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(host.engine,target,a));
        ASSERT_TRUE(eng_animation_set_frame(host.engine,a,2));
        ASSERT_TRUE(eng_animation_set_playback(host.engine,a,CKANIMATION_CANBEBREAK,CKANIMATION_CANBEBREAK,1000));
        ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(host.engine,target,incoming,CK_TRANSITION_WARPSTART,2));
        nmo_object_id_t warper = eng_character_warper(host.engine,target);
        ASSERT_EQ(warper,eng_character_active_animation(host.engine,target));
        ASSERT_EQ(imported_body,eng_keyed_animation_root_entity(host.engine,warper));
        ASSERT_TRUE(eng_keyed_animation_part(host.engine,warper,body) != 0);
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(host.engine,target,0));
        ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_set_next_animation(host.engine,target,0,0));
        float identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        ASSERT_EQ(NMO_OK,nmo_engine_set_entity_world_matrix(host.engine,target,identity,false));
        ASSERT_TRUE(eng_animation_set_current_step(host.engine,a,.2f));
        ASSERT_EQ(NMO_OK,eng_object_animation_transition(host.engine,incoming_channel,a_channel,.2f,b_channel,.4f,2,false,false,NULL));
        ASSERT_TRUE(eng_object_animation_set_step(host.engine,incoming_channel,.5f)); ASSERT_FLOAT_EQ(3,x(host.engine,body),1e-6f);
        float entry = -1;
        ASSERT_EQ(NMO_OK,eng_keyed_animation_transition(host.engine,incoming,a,b,CK_TRANSITION_WARPSTART,2,0,&entry));
        ASSERT_EQ(target,eng_animation_character(host.engine,incoming)); ASSERT_EQ(body,eng_keyed_animation_root_entity(host.engine,incoming));
        ASSERT_TRUE(eng_animation_set_frame(host.engine,incoming,1)); ASSERT_FLOAT_EQ(1,x(host.engine,body),1e-6f);
        nmo_object_id_t copy = 0;
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,&incoming,1,false,&copy));
        nmo_object_id_t copy_channel = eng_keyed_animation_part(host.engine,copy,body);
        ASSERT_TRUE(copy_channel != 0 && copy_channel != incoming_channel);
        ASSERT_EQ(target,eng_animation_character(host.engine,copy)); ASSERT_EQ(body,eng_keyed_animation_root_entity(host.engine,copy));
        ASSERT_TRUE(eng_object_animation_set_entity(host.engine,copy_channel,spare));
        ASSERT_TRUE(eng_animation_set_frame(host.engine,copy,.5f)); ASSERT_FLOAT_EQ(1.5f,x(host.engine,spare),1e-6f);
        ASSERT_FLOAT_EQ(1,x(host.engine,body),1e-6f); ASSERT_FLOAT_EQ(0,x(host.engine,imported_body),0);
        nmo_engine_destroy_object(host.engine,body);
        ASSERT_EQ(0u,eng_object_animation_entity(host.engine,incoming_channel)); ASSERT_EQ(spare,eng_object_animation_entity(host.engine,copy_channel));
        ASSERT_EQ(old,nmo_ref_runtime_id(&((const nmo_keyedanimation_state_t *)fixture_state(&source,animation))->base.character));
        ASSERT_EQ(old_body,nmo_ref_runtime_id(&((const nmo_objectanimation_state_t *)fixture_state(&source,channel))->entity));
        fixture_end(&host); fixture_end(&source); remove(path);
    }
}

TEST(engine_animation_binding, empty_target_characters_adopt_an_imported_root_for_their_warper)
{
    engine_fixture_t source; fixture_begin_document(&source);
    nmo_object_id_t old_body, old = character(&source,"Imported",&old_body), channel;
    clip(&source,"Incoming",old,old_body,&channel);
    fixture_begin_edit(&source); fixture_commit_bare(&source,NULL);
    const char *path = NMO_TEST_SCRATCH_FILE("engine_adopt_animation_root.cmo");
    ASSERT_EQ(NMO_OK,nmo_session_save_file(source.session,path,NULL,NULL));
    engine_fixture_t host; fixture_begin_document(&host);
    nmo_object_id_t body, target = character(&host,"Target",&body);
    ((nmo_character_state_t *)fixture_state(&host,target))->root_body_part = nmo_ref_from_id(0);
    ((nmo_3dentity_state_t *)fixture_state(&host,body))->parent = nmo_ref_from_id(0);
    nmo_object_id_t block = add_block(&host,target,0), input = 0;
    ASSERT_TRUE(block != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(host.tx,BLOCK_PIN(&host,block,0),&input));
    strict_commit(&host);
    nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
    int32_t file; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"animation.cmo",true,&file));
    nmo_object_id_t imported = nmo_engine_object_named(host.engine,"Imported",NMO_CID_CHARACTER);
    nmo_object_id_t incoming = nmo_engine_object_named(host.engine,"Incoming",NMO_CID_KEYEDANIMATION);
    nmo_object_id_t imported_body = eng_character_bodypart(host.engine,imported,0);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,input,&incoming,4));
    ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
    nmo_object_id_t warper = eng_character_warper(host.engine,target);
    ASSERT_TRUE(warper != 0); ASSERT_EQ(imported_body,eng_keyed_animation_root_entity(host.engine,warper));
    fixture_end(&host); fixture_end(&source); remove(path);
}

TEST(engine_animation_binding, unlimited_controller_accepts_a_transferred_animation_with_its_new_owner)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character(&f,"Target",&new_body);
    nmo_object_id_t channel, animation = clip(&f,"Clip",old,old_body,&channel);
    f.owner_id = target;
    nmo_object_id_t add = add_block(&f,target,animation);
    nmo_object_id_t control = fixture_block(&f,NMO_GUID(0x59537e89,0x79d02f25),"Controller");
    ASSERT_TRUE(add != 0 && control != 0);
    const nmo_behavior_state_t *state = fixture_state(&f,control);
    nmo_object_id_t array = nmo_behavior_ref_array_get_id(&state->local_parameters,0);
    nmo_object_id_t data = nmo_behavior_ref_array_get_id(&state->local_parameters,1);
    uint8_t record[152] = {0}; memset(record+56,0xff,16);
    nmo_write_u16_le(record+148,2|128); float fps = 1000; uint32_t word; memcpy(&word,&fps,4); nmo_write_u32_le(record+140,word);
    const nmo_object_id_t parameters[] = {array,data};
    const void *values[] = {&animation,record}; const size_t sizes[] = {4,sizeof(record)};
    for (size_t i = 0; i < COUNT(parameters); i++) {
        nmo_parameter_state_t *parameter = fixture_state(&f,parameters[i]);
        nmo_array_dispose(&parameter->buffer_data);
        ASSERT_EQ(NMO_OK,nmo_array_alloc(&parameter->buffer_data,1,sizes[i],NULL));
        memcpy(parameter->buffer_data.data,values[i],sizes[i]); parameter->mode = CKPARAM_MODE_BUFFER; parameter->has_state = true;
    }
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,nmo_behavior_ref_array_get_id(&state->local_parameters,2),"1"));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,nmo_behavior_ref_array_get_id(&state->local_parameters,3),"FALSE"));
    fixture_link(&f,BLOCK_OUT(&f,add,0),BLOCK_IN(&f,control,0),0);
    strict_commit(&f);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(animation,eng_character_active_animation(f.engine,target));
    ASSERT_EQ(target,eng_animation_character(f.engine,animation)); ASSERT_EQ(3u,runs(&f,control));
    ASSERT_EQ(1u,eng_character_animation_count(f.engine,target));
    ASSERT_EQ(old,nmo_ref_runtime_id(&((const nmo_keyedanimation_state_t *)fixture_state(&f,animation))->base.character));
    fixture_end(&f);
}

TEST(engine_animation_binding, global_animation_takeover_stops_the_retargeted_characters_automatic_processing)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t old_body, old = character(&f,"Old",&old_body), new_body, target = character(&f,"Target",&new_body);
    nmo_object_id_t channel, animation = clip(&f,"Clip",old,old_body,&channel);
    nmo_object_id_t add = add_block(&f,target,animation);
    nmo_object_id_t play = fixture_block(&f,NMO_GUID(0x1c9236e1,0x42f40996),"Play Global");
    ASSERT_TRUE(add != 0 && play != 0);
    fixture_input_int(&f,BLOCK_PIN(&f,play,0),(int32_t)animation);
    const float duration = 10; fixture_input_value(&f,BLOCK_PIN(&f,play,1),&duration,4);
    fixture_input_int(&f,BLOCK_PIN(&f,play,3),0);
    fixture_link(&f,BLOCK_OUT(&f,add,0),BLOCK_IN(&f,play,0),0);
    strict_commit(&f);
    ASSERT_TRUE(eng_character_set_automatic(f.engine,old,true)); ASSERT_TRUE(eng_character_set_automatic(f.engine,target,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_TRUE(eng_character_automatic(f.engine,old)); ASSERT_FALSE(eng_character_automatic(f.engine,target));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,5));
    ASSERT_FLOAT_EQ(5,x(f.engine,new_body),1e-6f); ASSERT_FLOAT_EQ(0,x(f.engine,old_body),0);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_animation_binding, live_owner_and_target_changes_drive_playback_without_editing_source_refs);
REGISTER_TEST(engine_animation_binding, copied_runtime_bindings_remap_and_deleted_targets_stay_unbound);
REGISTER_TEST(engine_animation_binding, secondary_gates_follow_live_channel_targets);
REGISTER_TEST(engine_animation_binding, add_animation_transfers_membership_preserves_playback_and_recenters_repeated_adds);
REGISTER_TEST(engine_animation_binding, add_animation_keeps_exact_and_historical_suffix_matching_order);
REGISTER_TEST(engine_animation_binding, duplicate_channel_slots_share_the_exact_match_marker);
REGISTER_TEST(engine_animation_binding, plain_animations_transfer_and_missing_targets_or_animations_keep_control_flow);
REGISTER_TEST(engine_animation_binding, loaded_animation_transfers_keep_cross_module_warps_and_copies_bound);
REGISTER_TEST(engine_animation_binding, empty_target_characters_adopt_an_imported_root_for_their_warper);
REGISTER_TEST(engine_animation_binding, unlimited_controller_accepts_a_transferred_animation_with_its_new_owner);
REGISTER_TEST(engine_animation_binding, global_animation_takeover_stops_the_retargeted_characters_automatic_processing);
TEST_MAIN_END()
