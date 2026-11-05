#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_animation_schemas.h"

#include <math.h>

#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct callback_observation { unsigned edits, loads, resets; int result; } callback_observation_t;

static int editor_callback(nmo_engine_block_t *block, uint32_t message)
{
    callback_observation_t *seen = nmo_engine_block_user_data(block);
    if (message == NMO_ENGINE_MSG_EDITED) { seen->edits++; return seen->result; }
    if (message == NMO_ENGINE_MSG_LOAD) seen->loads++;
    if (message == NMO_ENGINE_MSG_RESET) seen->resets++;
    return NMO_ENGINE_BR_OK;
}

TEST(engine_share_animation, callback_only_blocks_receive_explicit_edit_notifications_before_and_after_start)
{
    engine_fixture_t f; fixture_begin(&f); nmo_object_id_t block = fixture_block(&f,GUID_NOP,"Editor Only"); ASSERT_TRUE(block != 0); fixture_commit_bare(&f,NULL); callback_observation_t seen = {.result = NMO_ENGINE_BR_PARAMETERERROR};
    nmo_engine_block_desc_t desc = {.guid = GUID_NOP,.name = "Editor Only",.callback = editor_callback,.user_data = &seen}; ASSERT_EQ(NMO_OK,nmo_engine_register_block(f.engine,&desc)); ASSERT_TRUE(nmo_engine_has_block(f.engine,GUID_NOP)); int32_t result = -1;
    ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(f.engine,block,&result)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,result); ASSERT_EQ(1u,seen.edits); ASSERT_EQ(0u,seen.loads); ASSERT_EQ(0u,runs(&f,block));
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(1u,seen.loads); ASSERT_EQ(1u,seen.resets); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,16)); ASSERT_EQ(1u,seen.edits); ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(f.engine,block,NULL)); ASSERT_EQ(2u,seen.edits); fixture_end(&f);
}

TEST(engine_share_animation, registered_blocks_without_functions_report_native_function_errors_instead_of_unknown_blocks)
{
    engine_fixture_t f; fixture_begin(&f); nmo_object_id_t block = fixture_block(&f,GUID_NOP,"Editor Only"); ASSERT_TRUE(block != 0); fixture_link(&f,f.script_in,BLOCK_IN(&f,block,0),0); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit_bare(&f,&options); callback_observation_t seen = {0};
    nmo_engine_block_desc_t desc = {.guid = GUID_NOP,.callback = editor_callback,.user_data = &seen}; ASSERT_EQ(NMO_OK,nmo_engine_register_block(f.engine,&desc)); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_EVENT_EXECUTE,failure.kind); ASSERT_EQ(NMO_ENGINE_BR_GENERICERROR,failure.result); ASSERT_EQ(0u,seen.edits); fixture_end(&f);
}

TEST(engine_share_animation, edit_notifications_validate_the_target_and_allow_registered_blocks_without_callbacks)
{
    engine_fixture_t f; fixture_begin(&f); nmo_object_id_t block = fixture_block(&f,GUID_NOP,"Plain Block"); ASSERT_TRUE(block != 0); fixture_commit(&f,NULL); int32_t result = -1;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,nmo_engine_notify_behavior_edited(NULL,block,&result)); ASSERT_EQ(NMO_ERR_NOT_FOUND,nmo_engine_notify_behavior_edited(f.engine,f.script_id,&result)); ASSERT_EQ(NMO_ERR_NOT_FOUND,nmo_engine_notify_behavior_edited(f.engine,0,&result)); ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(f.engine,block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result);
    nmo_engine_block_desc_t invalid = {.guid = GUID_NOP}; ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,nmo_engine_register_block(f.engine,&invalid)); nmo_engine_destroy_object(f.engine,block); ASSERT_EQ(NMO_ERR_NOT_FOUND,nmo_engine_notify_behavior_edited(f.engine,block,&result)); fixture_end(&f);
}

static nmo_object_id_t channel(engine_fixture_t *f, const char *name, nmo_object_id_t entity,
    uint32_t type, const void *data, uint32_t size, uint32_t count, float length)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_OBJECTANIMATION,name); nmo_objectanimation_state_t *state = fixture_state(f,id); if (state == NULL) return 0;
    state->format = CKOBJANIM_FORMAT_CONTROLLERS; state->entity = nmo_ref_from_id(entity); state->has_length = 1; state->length = length;
    if (type != 0) {
        nmo_arena_t *arena = nmo_session_get_arena(f->session); state->controllers = nmo_arena_alloc(arena,sizeof(*state->controllers),_Alignof(nmo_objanim_controller_t)); void *copy = size != 0 ? nmo_arena_alloc(arena,size,_Alignof(float)) : NULL;
        if (state->controllers == NULL || (size != 0 && copy == NULL)) return 0;
        if (size != 0) memcpy(copy,data,size);
        state->controller_count = 1; *state->controllers = (nmo_objanim_controller_t){type,count,size,copy};
    }
    return id;
}

static nmo_object_id_t position(engine_fixture_t *f, const char *name, nmo_object_id_t entity, float end)
{
    const float values[] = {0,0,0,0,10,end,0,0}; return channel(f,name,entity,0x637c4301u,values,sizeof(values),2,10);
}

static void commit(engine_fixture_t *f)
{
    fixture_begin_edit(f); fixture_commit_bare(f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(f->engine,NMO_ENGINE_START_ALL));
}

static float x(nmo_engine_t *engine, nmo_object_id_t id)
{
    float matrix[16]; return nmo_engine_entity_world_matrix(engine,id,matrix) ? matrix[12] : NAN;
}

TEST(engine_share_animation, key_comparison_is_directional_and_ignores_targets_flags_and_names)
{
    engine_fixture_t f; fixture_begin_document(&f); nmo_object_id_t a = position(&f,"First Name",f.owner_id,10), b = position(&f,"Different Name",f.owner_id,10), empty = channel(&f,"Empty",0,0,NULL,0,0,10), defaults = fixture_create(&f,NMO_CID_OBJECTANIMATION,"Constructor Defaults"), hundred = channel(&f,"Empty Hundred",0,0,NULL,0,0,100); ASSERT_TRUE(a != 0 && b != 0 && empty != 0 && defaults != 0 && hundred != 0); ((nmo_objectanimation_state_t *)fixture_state(&f,b))->flags = CK_OBJECTANIMATION_IGNOREPOS | CK_OBJECTANIMATION_MERGED; commit(&f);
    ASSERT_TRUE(eng_object_animation_compare(f.engine,a,b)); ASSERT_TRUE(eng_object_animation_compare(f.engine,empty,a)); ASSERT_FALSE(eng_object_animation_compare(f.engine,a,empty)); ASSERT_TRUE(eng_object_animation_compare(f.engine,a,a)); ASSERT_FALSE(eng_object_animation_compare(f.engine,a,0));
    ASSERT_TRUE(eng_object_animation_compare(f.engine,defaults,hundred)); ASSERT_TRUE(eng_object_animation_compare(f.engine,hundred,defaults)); ((nmo_objectanimation_state_t *)fixture_state(&f,b))->length = 11; ASSERT_FALSE(eng_object_animation_compare(f.engine,a,b)); fixture_end(&f);
}

TEST(engine_share_animation, fixed_controller_comparisons_use_values_types_counts_and_tcb_metadata)
{
    const uint32_t types[] = {0x637c4301u,0x49ed4002u,0x654a3a04u,0x2f200b08u,0x347e4a01u,0x45b52a02u,0x1b545904u,0x32595908u};
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        engine_fixture_t f; fixture_begin_document(&f); float values[10] = {0,1,2,3,4,5,6,7,8,9}; uint32_t bytes = nmo_objanim_controller_key_size(types[i]); nmo_object_id_t a = channel(&f,"A",f.owner_id,types[i],values,bytes,1,10), b = channel(&f,"B",f.owner_id,types[i],values,bytes,1,10); commit(&f); ASSERT_TRUE(eng_object_animation_compare(f.engine,a,b));
        nmo_objanim_controller_t *c = ((nmo_objectanimation_state_t *)fixture_state(&f,b))->controllers; ((float *)c->data)[0] = -0.0f; ASSERT_TRUE(eng_object_animation_compare(f.engine,a,b)); ((float *)c->data)[1] = 9; ASSERT_FALSE(eng_object_animation_compare(f.engine,a,b)); ((float *)c->data)[1] = 1; ((float *)c->data)[0] = NAN; ASSERT_FALSE(eng_object_animation_compare(f.engine,a,b)); ((float *)c->data)[0] = 0;
        c->key_count = 0; ASSERT_FALSE(eng_object_animation_compare(f.engine,a,b)); c->key_count = 1;
        if (bytes >= 36) { ((float *)c->data)[bytes/4-1] = 99; ASSERT_FALSE(eng_object_animation_compare(f.engine,a,b)); ((float *)c->data)[bytes/4-1] = values[bytes/4-1]; }
        c->data_size--; ASSERT_FALSE(eng_object_animation_compare(f.engine,a,b)); fixture_end(&f);
    }
}

TEST(engine_share_animation, bezier_and_legacy_scale_axis_keys_compare_their_native_semantic_fields)
{
    engine_fixture_t f; fixture_begin_document(&f); uint8_t bezier[44] = {0}; float point[] = {0,1,2,3}; uint32_t flags = 0x00200020u; memcpy(bezier,point,sizeof(point)); memcpy(bezier+16,&flags,4); const float tangents[] = {1,2,3,4,5,6}; memcpy(bezier+20,tangents,sizeof(tangents));
    nmo_object_id_t a = channel(&f,"Bezier A",f.owner_id,0x921ab801u,bezier,sizeof(bezier),1,10), b = channel(&f,"Bezier B",f.owner_id,0x921ab801u,bezier,sizeof(bezier),1,10);
    float modern[] = {0,0,0,0,1}, legacy[] = {0,NAN,0,0,0,1}; nmo_object_id_t axis = channel(&f,"Modern Axis",0,0x2f200b08u,modern,sizeof(modern),1,10), old = channel(&f,"Old Axis",0,0x2f200b08u,legacy,sizeof(legacy),1,10); ((nmo_objectanimation_state_t *)fixture_state(&f,old))->format = CKOBJANIM_FORMAT_NEWDATA; commit(&f);
    ASSERT_TRUE(eng_object_animation_compare(f.engine,a,b)); ASSERT_TRUE(eng_object_animation_compare(f.engine,axis,old)); ASSERT_TRUE(eng_object_animation_compare(f.engine,old,axis)); ((float *)((nmo_objectanimation_state_t *)fixture_state(&f,b))->controllers[0].data)[5] = 99; ASSERT_FALSE(eng_object_animation_compare(f.engine,a,b)); fixture_end(&f);
}

TEST(engine_share_animation, sharing_keeps_old_aliases_alive_when_the_owner_switches_data_or_is_deleted)
{
    engine_fixture_t f; fixture_begin_document(&f); nmo_object_id_t a = position(&f,"Owner",f.owner_id,10), b = position(&f,"Receiver",f.owner_id,20), c = position(&f,"Replacement",f.owner_id,100), saved = channel(&f,"Saved Alias",f.owner_id,0,NULL,0,0,10); nmo_objectanimation_state_t *alias = fixture_state(&f,saved); alias->format = CKOBJANIM_FORMAT_SHARED; alias->has_shared_anim = 1; alias->shared_anim = nmo_ref_from_id(a); commit(&f);
    ASSERT_TRUE(eng_object_animation_share(f.engine,b,a)); const nmo_objectanimation_state_t *old = eng_animation_shared_keys(f.engine,a); ASSERT_TRUE(old == eng_animation_shared_keys(f.engine,b)); ASSERT_TRUE(old == eng_animation_shared_keys(f.engine,saved)); ASSERT_TRUE(eng_object_animation_share(f.engine,a,c)); ASSERT_TRUE(old == eng_animation_shared_keys(f.engine,b)); ASSERT_TRUE(old != eng_animation_shared_keys(f.engine,a)); nmo_engine_destroy_object(f.engine,a); nmo_engine_destroy_object(f.engine,c);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine,b,.5f)); ASSERT_FLOAT_EQ(5,x(f.engine,f.owner_id),1e-6f); ASSERT_TRUE(eng_object_animation_set_step(f.engine,saved,.8f)); ASSERT_FLOAT_EQ(8,x(f.engine,f.owner_id),1e-6f); ASSERT_FALSE(eng_object_animation_share(f.engine,b,b)); ASSERT_TRUE(eng_object_animation_share(f.engine,b,0)); eng_animation_source_t source; ASSERT_TRUE(eng_animation_source(f.engine,b,&source)); ASSERT_FLOAT_EQ(100,source.length,0); ASSERT_TRUE(old == eng_animation_shared_keys(f.engine,saved)); ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS,((nmo_objectanimation_state_t *)fixture_state(&f,b))->format); fixture_end(&f);
}

TEST(engine_share_animation, copied_keys_obey_character_sharing_dependencies_and_keep_payloads_independent_when_cloned)
{
    engine_fixture_t f; fixture_begin_document(&f); nmo_object_id_t a = position(&f,"Owner",f.owner_id,10), b = position(&f,"Receiver",f.owner_id,20); commit(&f); ASSERT_TRUE(eng_object_animation_share(f.engine,b,a)); eng_dependencies_t deps; eng_default_copy_dependencies(&deps); nmo_object_id_t shared = 0, deep = 0;
    deps.classes[NMO_CID_CHARACTER] |= 1u; ASSERT_EQ(NMO_OK,eng_copy_objects(f.engine,&b,1,&deps,false,false,&shared)); deps.classes[NMO_CID_CHARACTER] &= ~1u; ASSERT_EQ(NMO_OK,eng_copy_objects(f.engine,&b,1,&deps,false,false,&deep)); ASSERT_TRUE(eng_animation_shared_keys(f.engine,shared) == eng_animation_shared_keys(f.engine,b)); ASSERT_TRUE(eng_animation_shared_keys(f.engine,deep) != eng_animation_shared_keys(f.engine,b));
    ((float *)((nmo_objectanimation_state_t *)fixture_state(&f,a))->controllers[0].data)[5] = 100; ASSERT_TRUE(eng_object_animation_set_step(f.engine,shared,.5f)); ASSERT_FLOAT_EQ(50,x(f.engine,f.owner_id),1e-6f); ASSERT_TRUE(eng_object_animation_set_step(f.engine,deep,.5f)); ASSERT_FLOAT_EQ(5,x(f.engine,f.owner_id),1e-6f); fixture_end(&f);
}

TEST(engine_share_animation, generated_transitions_update_shared_payloads_without_rebinding_other_animation_targets)
{
    engine_fixture_t f; fixture_begin_document(&f); nmo_object_id_t other = fixture_create(&f,NMO_CID_3DENTITY,"Other Body"), a = position(&f,"Owner",f.owner_id,10), b = position(&f,"Receiver",other,20), c = position(&f,"Source",other,100); commit(&f); ASSERT_TRUE(eng_object_animation_share(f.engine,b,a)); ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,b,c,0,c,1,4,false,false,NULL));
    ASSERT_TRUE(eng_animation_shared_keys(f.engine,a) == eng_animation_shared_keys(f.engine,b)); ASSERT_EQ(f.owner_id,eng_object_animation_entity(f.engine,a)); ASSERT_EQ(other,eng_object_animation_entity(f.engine,b)); ASSERT_TRUE(eng_object_animation_set_step(f.engine,a,.5f)); ASSERT_FLOAT_EQ(50,x(f.engine,f.owner_id),1e-6f); ASSERT_TRUE(eng_object_animation_set_step(f.engine,b,.25f)); ASSERT_FLOAT_EQ(25,x(f.engine,other),1e-6f); fixture_end(&f);
}

TEST(engine_share_animation, morph_comparison_preserves_native_directional_optional_payloads)
{
    engine_fixture_t f; fixture_begin_document(&f); uint8_t blob[32] = {0}; const uint32_t header[] = {1,1,1}; memcpy(blob,header,sizeof(header)); const float positions[] = {0,1,2,3}; memcpy(blob+12,positions,sizeof(positions)); uint32_t normal = 0x00100020u; memcpy(blob+28,&normal,4);
    nmo_object_id_t full = channel(&f,"Morph Full",f.owner_id,NMO_OBJANIM_CONTROLLER_MORPH,blob,sizeof(blob),0,10), optional = channel(&f,"Morph Optional",f.owner_id,0,NULL,0,0,10); nmo_objectanimation_state_t *state = fixture_state(&f,optional); state->format = CKOBJANIM_FORMAT_NEWDATA; state->has_morph_counts = 1; state->morph_vertex_count = state->morph_key_count = state->morph_key_parsed_count = 1; state->morph_keys = nmo_arena_alloc(nmo_session_get_arena(f.session),sizeof(*state->morph_keys),_Alignof(nmo_objanim_morph_key_t)); ASSERT_NOT_NULL(state->morph_keys); memset(state->morph_keys,0,sizeof(*state->morph_keys)); commit(&f);
    ASSERT_TRUE(eng_object_animation_compare(f.engine,optional,full)); ASSERT_FALSE(eng_object_animation_compare(f.engine,full,optional)); state->morph_keys[0].time_step = 1; ASSERT_FALSE(eng_object_animation_compare(f.engine,optional,full)); fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_share_animation, callback_only_blocks_receive_explicit_edit_notifications_before_and_after_start);
REGISTER_TEST(engine_share_animation, registered_blocks_without_functions_report_native_function_errors_instead_of_unknown_blocks);
REGISTER_TEST(engine_share_animation, edit_notifications_validate_the_target_and_allow_registered_blocks_without_callbacks);
REGISTER_TEST(engine_share_animation, key_comparison_is_directional_and_ignores_targets_flags_and_names);
REGISTER_TEST(engine_share_animation, fixed_controller_comparisons_use_values_types_counts_and_tcb_metadata);
REGISTER_TEST(engine_share_animation, bezier_and_legacy_scale_axis_keys_compare_their_native_semantic_fields);
REGISTER_TEST(engine_share_animation, sharing_keeps_old_aliases_alive_when_the_owner_switches_data_or_is_deleted);
REGISTER_TEST(engine_share_animation, copied_keys_obey_character_sharing_dependencies_and_keep_payloads_independent_when_cloned);
REGISTER_TEST(engine_share_animation, generated_transitions_update_shared_payloads_without_rebinding_other_animation_targets);
REGISTER_TEST(engine_share_animation, morph_comparison_preserves_native_directional_optional_payloads);
TEST_MAIN_END()
