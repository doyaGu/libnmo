#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_group_schemas.h"

#include <math.h>

#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)
#define GUID_SHARE NMO_GUID(0x1c0d5e24,0xde47b706)

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

typedef struct share_fixture {
    engine_fixture_t f;
    nmo_object_id_t source, target, source_body, target_body, source_part, target_part;
    nmo_object_id_t source_animation, target_animation, group, block, target_value, group_value;
} share_fixture_t;

static nmo_object_id_t body(engine_fixture_t *f, nmo_object_id_t character, nmo_object_id_t parent, const char *name)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_BODYPART,name); nmo_bodypart_state_t *part = fixture_state(f,id); nmo_character_state_t *actor = fixture_state(f,character); if (part == NULL || actor == NULL) return 0;
    part->base.entity.has_entityndata_chunk = 1; part->base.entity.parent = nmo_ref_from_id(parent); part->has_character = 1; part->character = nmo_ref_from_id(character);
    if (actor->body_parts.element_size != sizeof(nmo_character_part_t)) { nmo_array_dispose(&actor->body_parts); if (nmo_array_init(&actor->body_parts,sizeof(nmo_character_part_t),0,NULL) != NMO_OK) return 0; }
    nmo_character_part_t entry = {.ref = nmo_ref_from_id(id)}; return nmo_array_append(&actor->body_parts,&entry) == NMO_OK ? id : 0;
}

static nmo_object_id_t animation(engine_fixture_t *f, const char *name, nmo_object_id_t actor, nmo_object_id_t entity, nmo_object_id_t part)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,name); nmo_keyedanimation_state_t *state = fixture_state(f,id); if (state == NULL) return 0;
    state->base.has_data = state->base.has_character = state->base.has_root_entity = state->base.has_length = 1; state->base.character = nmo_ref_from_id(actor); state->base.root_entity = nmo_ref_from_id(entity); state->base.length = 10; state->base.frame_rate = 1000; state->base.flags = CKANIMATION_LINKTOFRAMERATE;
    state->animation_count = 1; state->animation_ids = nmo_arena_alloc(nmo_session_get_arena(f->session),sizeof(nmo_ref_t),_Alignof(nmo_ref_t)); if (state->animation_ids == NULL) return 0; state->animation_ids[0] = nmo_ref_from_id(part);
    nmo_character_state_t *character = fixture_state(f,actor); nmo_ref_t ref = nmo_ref_from_id(id); return nmo_array_append(&character->animations,&ref) == NMO_OK ? id : 0;
}

static void begin(share_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f); g->source = fixture_create(&g->f,NMO_CID_CHARACTER,"Source Character"); g->target = fixture_create(&g->f,NMO_CID_CHARACTER,"Target Character"); ASSERT_TRUE(g->source != 0 && g->target != 0);
    g->source_body = body(&g->f,g->source,g->source,"Source Bone"); g->target_body = body(&g->f,g->target,g->target,"Different Bone"); ASSERT_TRUE(g->source_body != 0 && g->target_body != 0); ((nmo_character_state_t *)fixture_state(&g->f,g->source))->root_body_part = nmo_ref_from_id(g->source_body); ((nmo_character_state_t *)fixture_state(&g->f,g->target))->root_body_part = nmo_ref_from_id(g->target_body);
    g->source_part = position(&g->f,"Source Keys",g->source_body,10); g->target_part = position(&g->f,"Different Keys",g->target_body,10); g->source_animation = animation(&g->f,"Source Clip",g->source,g->source_body,g->source_part); g->target_animation = animation(&g->f,"Different Clip",g->target,g->target_body,g->target_part); g->group = fixture_create(&g->f,NMO_CID_GROUP,"Receiver Characters"); ASSERT_TRUE(g->source_animation != 0 && g->target_animation != 0 && g->group != 0);
    g->f.script_id = fixture_add_script(&g->f,"Share Script",&g->f.script_in); fixture_begin_edit(&g->f); g->block = fixture_block(&g->f,GUID_SHARE,"Share Character Animation Keys"); ASSERT_TRUE(g->block != 0);
    nmo_object_id_t target = fixture_target(&g->f,g->block), input = BLOCK_PIN(&g->f,g->block,0); fixture_input_int(&g->f,target,(int32_t)g->source); fixture_input_int(&g->f,input,(int32_t)g->group); g->target_value = nmo_parameterin_source_id(fixture_state(&g->f,target)); g->group_value = nmo_parameterin_source_id(fixture_state(&g->f,input));
}

static void ready(share_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_SHARE)); ASSERT_TRUE(eng_group_add(g->f.engine,g->group,g->target)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static int32_t shared_count(share_fixture_t *g)
{
    int32_t value = -1; read_value(&g->f,BLOCK_POUT(&g->f,g->block,0),&value,sizeof(value)); return value;
}

static void edit(share_fixture_t *g)
{
    int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g->f.engine,g->block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result);
}

TEST(engine_share_animation, sharing_block_is_editor_only_and_counts_object_channels_outside_target_animation_lists)
{
    share_fixture_t g; begin(&g); nmo_object_id_t orphan = position(&g.f,"Unlisted Keys",g.target_body,10); ASSERT_TRUE(orphan != 0); ready(&g); const nmo_behavior_state_t *state = fixture_state(&g.f,g.block); ASSERT_EQ(0u,state->inputs.count); ASSERT_EQ(0u,state->outputs.count); int32_t sentinel = 99; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&sentinel,sizeof(sentinel))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); ASSERT_EQ(99,shared_count(&g)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,g.target_part) == NULL);
    edit(&g); ASSERT_EQ(2,shared_count(&g)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,g.target_part) == eng_animation_shared_keys(g.f.engine,g.source_part)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,orphan) == eng_animation_shared_keys(g.f.engine,g.source_part)); edit(&g); ASSERT_EQ(2,shared_count(&g)); ASSERT_EQ(0u,runs(&g.f,g.block)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); ASSERT_EQ(2,shared_count(&g)); fixture_end(&g.f);
}

TEST(engine_share_animation, shared_characters_keep_independent_targets_flags_and_playback_clocks)
{
    share_fixture_t g; begin(&g); ((nmo_objectanimation_state_t *)fixture_state(&g.f,g.target_part))->flags = CK_OBJECTANIMATION_IGNOREPOS; ready(&g); edit(&g); ASSERT_EQ(1,shared_count(&g)); ASSERT_EQ(g.source_body,eng_object_animation_entity(g.f.engine,g.source_part)); ASSERT_EQ(g.target_body,eng_object_animation_entity(g.f.engine,g.target_part)); ASSERT_TRUE(eng_object_animation_set_step(g.f.engine,g.source_part,.4f)); ASSERT_FLOAT_EQ(4,x(g.f.engine,g.source_body),1e-6f); ASSERT_TRUE(eng_object_animation_set_step(g.f.engine,g.target_part,.7f)); ASSERT_FLOAT_EQ(0,x(g.f.engine,g.target_body),0);
    ASSERT_TRUE(eng_animation_set_current_step(g.f.engine,g.source_animation,.2f)); ASSERT_TRUE(eng_animation_set_current_step(g.f.engine,g.target_animation,.8f)); float a = 0, b = 0; ASSERT_TRUE(eng_animation_current_step(g.f.engine,g.source_animation,&a)); ASSERT_TRUE(eng_animation_current_step(g.f.engine,g.target_animation,&b)); ASSERT_FLOAT_EQ(.2f,a,0); ASSERT_FLOAT_EQ(.8f,b,0); ASSERT_EQ(CK_OBJECTANIMATION_IGNOREPOS,((nmo_objectanimation_state_t *)fixture_state(&g.f,g.target_part))->flags); fixture_end(&g.f);
}

TEST(engine_share_animation, invalid_targets_and_groups_preserve_output_while_valid_empty_groups_write_zero)
{
    for (int kind = 0; kind < 7; kind++) {
        share_fixture_t g; begin(&g); nmo_object_id_t empty = fixture_create(&g.f,NMO_CID_GROUP,"Empty Group"); ASSERT_TRUE(empty != 0); ready(&g); int32_t sentinel = 99; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&sentinel,sizeof(sentinel))); nmo_object_id_t value = kind == 0 || kind == 3 ? 0 : kind == 1 || kind == 4 ? g.f.owner_id : kind == 2 ? g.source : kind == 5 ? g.group : empty;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,kind < 3 ? g.target_value : g.group_value,&value,sizeof(value))); if (kind == 2) nmo_engine_destroy_object(g.f.engine,g.source); if (kind == 5) nmo_engine_destroy_object(g.f.engine,g.group); edit(&g); ASSERT_EQ(kind == 6 ? 0 : 99,shared_count(&g)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,g.target_part) == NULL); fixture_end(&g.f);
    }
}

TEST(engine_share_animation, hierarchy_mismatches_are_skipped_until_live_parenting_matches)
{
    share_fixture_t g; begin(&g); nmo_object_id_t extra = body(&g.f,g.target,g.target,"Extra Bone"); ASSERT_TRUE(extra != 0); ready(&g); edit(&g); ASSERT_EQ(0,shared_count(&g)); ASSERT_TRUE(eng_entity_set_parent(g.f.engine,eng_entity_index(g.f.engine,extra),-1,true)); edit(&g); ASSERT_EQ(1,shared_count(&g)); fixture_end(&g.f);
}

TEST(engine_share_animation, hierarchy_matching_preserves_native_reuse_of_a_source_subtree)
{
    share_fixture_t g; begin(&g); nmo_object_id_t branch = body(&g.f,g.source,g.source,"Branch"), leaf = body(&g.f,g.target,g.target,"Leaf"), child = body(&g.f,g.source,branch,"Branch Child"); ASSERT_TRUE(branch != 0 && leaf != 0 && child != 0); nmo_object_id_t reverse = fixture_create(&g.f,NMO_CID_GROUP,"Reverse Group"); ready(&g); ASSERT_TRUE(eng_group_add(g.f.engine,reverse,g.source)); edit(&g); ASSERT_EQ(1,shared_count(&g));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&g.target,sizeof(g.target))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.group_value,&reverse,sizeof(reverse))); edit(&g); ASSERT_EQ(0,shared_count(&g)); fixture_end(&g.f);
}

TEST(engine_share_animation, candidate_selection_uses_body_ownership_live_bindings_and_key_values)
{
    share_fixture_t g; begin(&g); nmo_object_id_t mismatch = position(&g.f,"Source Keys",g.target_body,20), generic = position(&g.f,"Generic Entity Keys",g.f.owner_id,10); ASSERT_TRUE(mismatch != 0 && generic != 0); ready(&g); ASSERT_TRUE(eng_group_add(g.f.engine,g.group,g.source)); ASSERT_TRUE(eng_group_add(g.f.engine,g.group,g.f.owner_id)); ASSERT_TRUE(eng_object_animation_set_entity(g.f.engine,g.target_part,g.source_body)); edit(&g); ASSERT_EQ(0,shared_count(&g)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,mismatch) == NULL);
    ASSERT_TRUE(eng_object_animation_set_entity(g.f.engine,g.target_part,g.target_body)); edit(&g); ASSERT_EQ(1,shared_count(&g)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,g.target_part) == eng_animation_shared_keys(g.f.engine,g.source_part)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,mismatch) != eng_animation_shared_keys(g.f.engine,g.source_part)); fixture_end(&g.f);
}

TEST(engine_share_animation, repeated_source_channels_count_every_matching_candidate_and_keep_the_last_shared_payload)
{
    share_fixture_t g; begin(&g); nmo_object_id_t second = position(&g.f,"Second Source Keys",g.source_body,10), orphan = position(&g.f,"Unlisted Receiver",g.target_body,10); ASSERT_TRUE(second != 0 && orphan != 0); ASSERT_TRUE(animation(&g.f,"Second Source Clip",g.source,g.source_body,second) != 0); ready(&g); edit(&g); ASSERT_EQ(4,shared_count(&g)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,g.target_part) == eng_animation_shared_keys(g.f.engine,second)); ASSERT_TRUE(eng_animation_shared_keys(g.f.engine,g.target_part) != eng_animation_shared_keys(g.f.engine,g.source_part)); edit(&g); ASSERT_EQ(4,shared_count(&g)); fixture_end(&g.f);
}

TEST(engine_share_animation, sharing_before_start_and_owner_deletion_are_isolated_between_engines_and_from_the_document)
{
    share_fixture_t g; begin(&g); ready(&g); edit(&g); const nmo_objectanimation_state_t *original = fixture_state(&g.f,g.target_part); ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS,original->format); ASSERT_FALSE(original->has_shared_anim); ASSERT_EQ(0u,nmo_ref_runtime_id(&original->shared_anim));
    nmo_engine_t *second = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&second)); ASSERT_EQ(NMO_OK,nmo_bb_virtools_register(second)); ASSERT_TRUE(eng_animation_shared_keys(second,g.target_part) == NULL); ASSERT_TRUE(eng_group_add(second,g.group,g.target)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(second,g.target_value,&g.source,sizeof(g.source))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(second,g.group_value,&g.group,sizeof(g.group))); int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(second,g.block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_TRUE(eng_animation_shared_keys(second,g.target_part) != eng_animation_shared_keys(g.f.engine,g.target_part));
    nmo_engine_destroy_object(g.f.engine,g.source_part); nmo_engine_destroy_object(g.f.engine,g.source); ASSERT_TRUE(eng_object_animation_set_step(g.f.engine,g.target_part,.6f)); ASSERT_FLOAT_EQ(6,x(g.f.engine,g.target_body),1e-6f); ASSERT_TRUE(eng_object_animation_set_step(second,g.source_part,.3f)); ASSERT_FLOAT_EQ(3,x(second,g.source_body),1e-6f); ASSERT_FLOAT_EQ(0,x(second,g.target_body),0); ASSERT_FALSE(original->has_shared_anim); nmo_engine_destroy(second); fixture_end(&g.f);
}

TEST(engine_share_animation, deep_hierarchies_compare_live_structure_without_using_the_c_call_stack)
{
    share_fixture_t g; begin(&g); nmo_object_id_t a = g.source_body, b = g.target_body;
    for (int i = 0; i < 384; i++) {
        nmo_object_id_t x_id = fixture_create(&g.f,NMO_CID_3DENTITY,"Source Chain"), y_id = fixture_create(&g.f,NMO_CID_3DENTITY,"Target Chain"); ASSERT_TRUE(x_id != 0 && y_id != 0); nmo_3dentity_state_t *left = fixture_state(&g.f,x_id), *right = fixture_state(&g.f,y_id); left->has_entityndata_chunk = right->has_entityndata_chunk = 1; left->parent = nmo_ref_from_id(a); right->parent = nmo_ref_from_id(b); a = x_id; b = y_id;
    }
    nmo_object_id_t extra = fixture_create(&g.f,NMO_CID_3DENTITY,"Extra Source Tail"); ASSERT_TRUE(extra != 0); nmo_3dentity_state_t *tail = fixture_state(&g.f,extra); tail->has_entityndata_chunk = 1; tail->parent = nmo_ref_from_id(a); ready(&g); edit(&g); ASSERT_EQ(0,shared_count(&g)); ASSERT_TRUE(eng_entity_set_parent(g.f.engine,eng_entity_index(g.f.engine,extra),-1,true)); edit(&g); ASSERT_EQ(1,shared_count(&g)); fixture_end(&g.f);
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t runtime_source(nmo_engine_t *engine, nmo_object_id_t behavior, int pos)
{
    int32_t index = eng_map_get(&engine->node_map,behavior); if (index < 0) return 0; const eng_node_t *node = &engine->nodes.data[index];
    eng_param_t *pin = pos < 0 ? (node->target < 0 ? NULL : &engine->params.data[node->target]) : eng_node_param(engine,node->first_pin,node->pin_count,pos);
    eng_param_t *source = eng_param_real_source(engine,pin); return source != NULL ? source->id : 0;
}

TEST(engine_share_animation, loaded_and_copied_edit_callbacks_remap_inputs_and_preserve_keys_after_source_deletion)
{
    share_fixture_t g; begin(&g); nmo_group_state_t *group = fixture_state(&g.f,g.group); nmo_ref_t ref = nmo_ref_from_id(g.target); group->has_group_data = 1; ASSERT_EQ(NMO_OK,nmo_array_append(&group->object_ids,&ref)); char text[32]; snprintf(text,sizeof(text),"%u",g.source); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.target_value,text)); snprintf(text,sizeof(text),"%u",g.group); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.group_value,text)); fixture_commit_bare(&g.f,NULL);
    const char *path = NMO_TEST_SCRATCH_FILE("engine_share_character_keys.cmo"); nmo_status_t saved = nmo_session_save_file(g.f.session,path,NULL,NULL); if (saved != NMO_OK) fprintf(stderr,"Share Character Animation Keys save: %s\n",nmo_last_error_message()); ASSERT_EQ(NMO_OK,saved);
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"share-keys.cmo",true,&file));
        nmo_object_id_t block = nmo_engine_object_named(host.engine,"Share Character Animation Keys",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0); const eng_module_t *module = eng_module_of(host.engine,block,NULL); const char *names[] = {"Source Character","Target Character","Source Bone","Different Bone","Source Keys","Different Keys","Source Clip","Different Clip","Receiver Characters","Share Character Animation Keys","Share Script","Owner"}; nmo_object_id_t ids[14] = {0}, copies[14] = {0};
        for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) { nmo_object_t *object = nmo_object_repository_find_by_name(module->repo,names[i]); ASSERT_NOT_NULL(object); ids[i] = eng_module_id(module,nmo_object_get_id(object)); ASSERT_TRUE(ids[i] != 0); }
        ids[12] = runtime_source(host.engine,block,-1); ids[13] = runtime_source(host.engine,block,0); ASSERT_TRUE(ids[12] != 0 && ids[13] != 0); ASSERT_TRUE(eng_animation_shared_keys(host.engine,ids[5]) == NULL); int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(host.engine,block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_NOT_NULL(eng_animation_shared_keys(host.engine,ids[5])); ASSERT_TRUE(eng_animation_shared_keys(host.engine,ids[5]) == eng_animation_shared_keys(host.engine,ids[4])); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        eng_dependencies_t deps; eng_default_copy_dependencies(&deps); deps.classes[NMO_CID_CHARACTER] |= 1u; ASSERT_EQ(NMO_OK,eng_copy_objects(host.engine,ids,14,&deps,false,false,copies)); for (size_t i = 0; i < 14; i++) ASSERT_TRUE(copies[i] != 0); nmo_object_id_t target = 0, input = 0; read_value(&host,copies[12],&target,sizeof(target)); read_value(&host,copies[13],&input,sizeof(input)); ASSERT_EQ(copies[0],target); ASSERT_EQ(copies[8],input); ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(host.engine,copies[9],&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result);
        nmo_engine_block_t handle = {host.engine,eng_map_get(&host.engine->node_map,copies[9])}; int32_t count = 0; ASSERT_EQ(NMO_OK,nmo_engine_block_get_output_value(&handle,0,&count,sizeof(count))); ASSERT_EQ(1,count); ASSERT_TRUE(eng_animation_shared_keys(host.engine,copies[5]) == eng_animation_shared_keys(host.engine,copies[4]));
        for (size_t i = 0; i < 14; i++) { nmo_engine_destroy_object(host.engine,ids[i]); } ASSERT_TRUE(eng_object_animation_set_step(host.engine,copies[5],.7f)); ASSERT_FLOAT_EQ(7,x(host.engine,copies[3]),1e-6f); ASSERT_TRUE(eng_object_animation_set_step(host.engine,copies[4],.2f)); ASSERT_FLOAT_EQ(2,x(host.engine,copies[2]),1e-6f); ASSERT_FLOAT_EQ(7,x(host.engine,copies[3]),1e-6f); ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(host.engine,copies[9],&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result);
        nmo_engine_destroy_object(host.engine,copies[4]); nmo_engine_destroy_object(host.engine,copies[0]); ASSERT_TRUE(eng_object_animation_set_step(host.engine,copies[5],.9f)); ASSERT_FLOAT_EQ(9,x(host.engine,copies[3]),1e-6f); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,16)); ASSERT_EQ(0u,runs(&host,copies[9])); fixture_end(&host);
    }
    ASSERT_FALSE(((nmo_objectanimation_state_t *)fixture_state(&g.f,g.target_part))->has_shared_anim); fixture_end(&g.f); remove(path);
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
REGISTER_TEST(engine_share_animation, sharing_block_is_editor_only_and_counts_object_channels_outside_target_animation_lists);
REGISTER_TEST(engine_share_animation, shared_characters_keep_independent_targets_flags_and_playback_clocks);
REGISTER_TEST(engine_share_animation, invalid_targets_and_groups_preserve_output_while_valid_empty_groups_write_zero);
REGISTER_TEST(engine_share_animation, hierarchy_mismatches_are_skipped_until_live_parenting_matches);
REGISTER_TEST(engine_share_animation, hierarchy_matching_preserves_native_reuse_of_a_source_subtree);
REGISTER_TEST(engine_share_animation, candidate_selection_uses_body_ownership_live_bindings_and_key_values);
REGISTER_TEST(engine_share_animation, repeated_source_channels_count_every_matching_candidate_and_keep_the_last_shared_payload);
REGISTER_TEST(engine_share_animation, sharing_before_start_and_owner_deletion_are_isolated_between_engines_and_from_the_document);
REGISTER_TEST(engine_share_animation, deep_hierarchies_compare_live_structure_without_using_the_c_call_stack);
REGISTER_TEST(engine_share_animation, loaded_and_copied_edit_callbacks_remap_inputs_and_preserve_keys_after_source_deletion);
TEST_MAIN_END()
