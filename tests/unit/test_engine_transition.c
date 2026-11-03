#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_module.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_character.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "engine/nmo_engine_render.h"

#include <math.h>
#include <float.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define POSITION 0x637c4301u
#define ROTATION 0x49ed4002u
#define SCALE 0x654a3a04u
#define AXIS 0x2f200b08u

static nmo_object_id_t channel(engine_fixture_t *f, nmo_object_id_t entity, float length,
                              uint32_t type, const float *values, uint32_t floats)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_OBJECTANIMATION,"Channel");
    nmo_objectanimation_state_t *state = fixture_state(f,id);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->entity = nmo_ref_from_id(entity);
    state->has_length = 1; state->length = length;
    state->controllers = nmo_arena_alloc(arena,4*sizeof(*state->controllers),_Alignof(nmo_objanim_controller_t));
    float *keys = nmo_arena_alloc(arena,floats*sizeof(float),_Alignof(float));
    if (state->controllers == NULL || keys == NULL) return 0;
    memcpy(keys,values,floats*sizeof(float));
    state->controller_count = 1;
    state->controllers[0] = (nmo_objanim_controller_t){type,2,floats*sizeof(float),keys};
    return id;
}

static nmo_object_id_t curve(engine_fixture_t *f, nmo_object_id_t entity, float length, float start, float end)
{
    float keys[] = {0,start,0,0,length,end,0,0};
    return channel(f,entity,length,POSITION,keys,COUNT(keys));
}

static void add_channel(engine_fixture_t *f, nmo_object_id_t id, uint32_t type, const float *values, uint32_t floats)
{
    nmo_objectanimation_state_t *state = fixture_state(f,id);
    float *keys = nmo_arena_alloc(nmo_session_get_arena(f->session),floats*sizeof(float),_Alignof(float));
    ASSERT_NOT_NULL(keys);
    memcpy(keys,values,floats*sizeof(float));
    state->controllers[state->controller_count++] = (nmo_objanim_controller_t){type,2,floats*sizeof(float),keys};
}

static float x(nmo_engine_t *engine, nmo_object_id_t id)
{
    float matrix[16];
    return nmo_engine_entity_world_matrix(engine,id,matrix) ? matrix[12] : NAN;
}

TEST(engine_transition, generated_pose_channels_sample_raw_frames_and_preserve_the_borrowed_model)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = curve(&f,f.owner_id,8,0,8), b = curve(&f,f.owner_id,20,10,30);
    const float sa[] = {0,1,2,3,8,3,4,5}, sb[] = {0,2,4,6,20,4,6,8};
    const float ra[] = {0,0,0,0,1,8,0,1,0,0}, rb[] = {0,0,0,0,1,20,0,0,1,0};
    add_channel(&f,a,SCALE,sa,COUNT(sa)); add_channel(&f,b,SCALE,sb,COUNT(sb));
    add_channel(&f,a,ROTATION,ra,COUNT(ra)); add_channel(&f,b,ROTATION,rb,COUNT(rb));
    add_channel(&f,a,AXIS,ra,COUNT(ra)); add_channel(&f,b,AXIS,rb,COUNT(rb));
    const nmo_objectanimation_state_t *saved = fixture_state(&f,a);
    nmo_objanim_controller_t original = saved->controllers[0];
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_object_id_t result = eng_create_related_object(f.engine,b,NMO_CID_OBJECTANIMATION);
    ASSERT_TRUE(result != 0 && result != a && result != b);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,a,.25f,b,.75f,4,false,false,NULL));
    eng_animation_pose_t first = eng_animation_sample_pose(f.engine,result,0), last = eng_animation_sample_pose(f.engine,result,4);
    ASSERT_EQ(15u,first.present); ASSERT_EQ(15u,last.present);
    ASSERT_FLOAT_EQ(2,first.position.x,0); ASSERT_FLOAT_EQ(25,last.position.x,0);
    ASSERT_FLOAT_EQ(1.5f,first.scale.x,0); ASSERT_FLOAT_EQ(3.5f,last.scale.x,0);
    ASSERT_FLOAT_EQ(sinf(0.125f*(float)acos(-1)),first.rotation.y,1e-6f);
    ASSERT_FLOAT_EQ(sinf(0.375f*(float)acos(-1)),last.axis.z,1e-6f);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine,result,.5f));
    ASSERT_FLOAT_EQ(13.5f,x(f.engine,f.owner_id),1e-6f);
    ASSERT_EQ(0,memcmp(&original,&saved->controllers[0],sizeof(original)));
    ASSERT_FLOAT_EQ(8,saved->length,0);
    fixture_end(&f);
}

TEST(engine_transition, velocity_sampling_handles_fractional_duration_and_in_place_rebuilds)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = curve(&f,f.owner_id,10,0,10), b = curve(&f,f.owner_id,10,0,30);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_object_id_t result = eng_create_related_object(f.engine,b,NMO_CID_OBJECTANIMATION);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,a,.5f,b,.5f,2.5f,true,false,NULL));
    eng_animation_pose_t end = eng_animation_sample_pose(f.engine,result,2.5f);
    ASSERT_FLOAT_EQ(1.2f,end.position.x,1e-6f);
    ASSERT_FLOAT_EQ(.4f,eng_animation_sample_pose(f.engine,result,1.25f).position.x,1e-6f);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,a,.5f,b,.5f,.5f,true,false,NULL));
    ASSERT_FLOAT_EQ(.2f,eng_animation_sample_pose(f.engine,result,.5f).position.x,1e-6f);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,result,.5f,b,.5f,3,false,false,NULL));
    ASSERT_FLOAT_EQ(.1f,eng_animation_sample_pose(f.engine,result,0).position.x,1e-6f);
    ASSERT_FLOAT_EQ(15,eng_animation_sample_pose(f.engine,result,3).position.x,1e-6f);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_object_animation_transition(f.engine,result,a,0,b,0,NAN,false,false,NULL));
    ASSERT_EQ(NMO_ERR_NOT_SUPPORTED,eng_object_animation_transition(f.engine,result,a,0,b,0,FLT_MAX,true,false,NULL));
    ASSERT_FLOAT_EQ(15,eng_animation_sample_pose(f.engine,result,3).position.x,1e-6f);
    fixture_end(&f);
}

static nmo_object_id_t morph(engine_fixture_t *f, nmo_object_id_t entity, uint32_t vertices, float start, float end)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_OBJECTANIMATION,"Morph");
    nmo_objectanimation_state_t *state = fixture_state(f,id);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    uint32_t bytes = 20+vertices*32;
    uint8_t *data = nmo_arena_alloc(arena,bytes,_Alignof(uint32_t));
    state->controllers = nmo_arena_alloc(arena,sizeof(*state->controllers),_Alignof(nmo_objanim_controller_t));
    if (data == NULL || state->controllers == NULL) return 0;
    uint32_t header[] = {2,vertices,1}; memcpy(data,header,sizeof(header));
    uint8_t *cursor = data+sizeof(header);
    for (uint32_t k = 0; k < 2; k++) {
        float time = k == 0 ? 0 : 8; memcpy(cursor,&time,4); cursor += 4;
        for (uint32_t v = 0; v < vertices; v++) {
            nmo_vector_t value = {k == 0 ? start : end,0,0}; memcpy(cursor,&value,12); cursor += 12;
        }
        for (uint32_t v = 0; v < vertices; v++) { uint32_t normal = k == 0 ? 0 : 0x10000000u; memcpy(cursor,&normal,4); cursor += 4; }
    }
    state->format = CKOBJANIM_FORMAT_CONTROLLERS; state->has_length = 1; state->length = 8;
    state->entity = nmo_ref_from_id(entity); state->controller_count = 1;
    state->controllers[0] = (nmo_objanim_controller_t){NMO_OBJANIM_CONTROLLER_MORPH,0,bytes,data};
    return id;
}

static float morph_x(nmo_engine_t *engine, nmo_object_id_t id, uint32_t key)
{
    eng_animation_source_t source;
    if (!eng_animation_source(engine,id,&source)) return NAN;
    for (uint32_t i = 0; i < source.keys->controller_count; i++) {
        const nmo_objanim_controller_t *c = &source.keys->controllers[i];
        nmo_objanim_morph_info_t info; const float *positions;
        if (c->type == NMO_OBJANIM_CONTROLLER_MORPH && nmo_objanim_morph_controller_info(c,&info) &&
            nmo_objanim_morph_controller_key(c,&info,key,NULL,&positions,NULL)) return positions[0];
    }
    return NAN;
}

TEST(engine_transition, morph_endpoints_survive_in_place_rebuilds_and_mismatched_vertex_sets)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = morph(&f,f.owner_id,1,0,8), b = morph(&f,f.owner_id,1,20,40);
    nmo_object_id_t mismatch = morph(&f,f.owner_id,2,50,60);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_object_id_t result = eng_create_related_object(f.engine,b,NMO_CID_OBJECTANIMATION);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,a,.25f,b,.5f,4,false,false,NULL));
    ASSERT_FLOAT_EQ(2,morph_x(f.engine,result,0),0); ASSERT_FLOAT_EQ(30,morph_x(f.engine,result,1),0);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,result,.5f,b,1,2,false,false,NULL));
    ASSERT_FLOAT_EQ(16,morph_x(f.engine,result,0),0); ASSERT_FLOAT_EQ(40,morph_x(f.engine,result,1),0);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,a,0,mismatch,0,10,false,false,NULL));
    ASSERT_FLOAT_EQ(16,morph_x(f.engine,result,0),0); ASSERT_FLOAT_EQ(40,morph_x(f.engine,result,1),0);
    fixture_end(&f);
}

TEST(engine_transition, root_heading_is_removed_and_ignored_channels_do_not_generate_keys)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = curve(&f,f.owner_id,8,0,8), b = curve(&f,f.owner_id,8,10,18);
    const float s = .2588190451f, c = .9659258263f;
    const float ra[] = {0,0,s,0,c,8,0,c,0,s}, rb[] = {0,0,0,0,1,8,0,0,0,1};
    const float scale[] = {0,1,1,1,8,2,2,2};
    add_channel(&f,a,ROTATION,ra,COUNT(ra)); add_channel(&f,b,ROTATION,rb,COUNT(rb));
    add_channel(&f,a,SCALE,scale,COUNT(scale)); add_channel(&f,b,SCALE,scale,COUNT(scale));
    ((nmo_objectanimation_state_t *)fixture_state(&f,a))->flags |= CK_OBJECTANIMATION_IGNORESCALE;
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_object_id_t result = eng_create_related_object(f.engine,b,NMO_CID_OBJECTANIMATION);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,a,.5f,b,0,2,false,true,NULL));
    eng_animation_pose_t pose = eng_animation_sample_pose(f.engine,result,0);
    ASSERT_EQ(ENG_ANIM_POSITION|ENG_ANIM_ROTATION,pose.present);
    ASSERT_FLOAT_EQ(-.5f,pose.rotation.y,1e-6f);
    ASSERT_FLOAT_EQ(.8660254f,pose.rotation.w,1e-6f);
    fixture_end(&f);
}

TEST(engine_transition, a_missing_source_morph_starts_from_the_current_runtime_mesh)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = fixture_create(&f,NMO_CID_MESH,"Mesh");
    nmo_mesh_state_t *state = fixture_state(&f,mesh);
    state->vertex_count = 1;
    state->vertices = nmo_arena_alloc(nmo_session_get_arena(f.session),sizeof(*state->vertices),_Alignof(nmo_vertex_t));
    ASSERT_NOT_NULL(state->vertices);
    memset(state->vertices,0,sizeof(*state->vertices));
    state->vertices[0].position.x = 7; state->vertices[0].normal.z = 1;
    state->vertex_colors = nmo_arena_alloc(nmo_session_get_arena(f.session),sizeof(uint32_t),_Alignof(uint32_t));
    state->vertex_specular = nmo_arena_alloc(nmo_session_get_arena(f.session),sizeof(uint32_t),_Alignof(uint32_t));
    ASSERT_NOT_NULL(state->vertex_colors); ASSERT_NOT_NULL(state->vertex_specular);
    state->vertex_colors[0] = 0xffffffffu; state->vertex_specular[0] = 0;
    ((nmo_3dentity_state_t *)fixture_state(&f,f.owner_id))->current_mesh = nmo_ref_from_id(mesh);
    nmo_object_id_t a = curve(&f,f.owner_id,8,0,8), b = morph(&f,f.owner_id,1,20,40);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine,b,.25f));
    nmo_object_id_t result = eng_create_related_object(f.engine,b,NMO_CID_OBJECTANIMATION);
    ASSERT_TRUE(result != 0);
    ASSERT_EQ(NMO_OK,eng_object_animation_transition(f.engine,result,a,0,b,1,2,false,false,NULL));
    ASSERT_FLOAT_EQ(25,morph_x(f.engine,result,0),0);
    ASSERT_FLOAT_EQ(40,morph_x(f.engine,result,1),0);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine,result,.5f));
    nmo_engine_mesh_t runtime;
    ASSERT_TRUE(nmo_engine_mesh(f.engine,mesh,&runtime));
    ASSERT_FLOAT_EQ(32.5f,runtime.vertices[0].position.x,1e-6f);
    ASSERT_FLOAT_EQ(7,state->vertices[0].position.x,0);
    fixture_end(&f);
}

static nmo_object_id_t group(engine_fixture_t *f, nmo_object_id_t character, nmo_object_id_t root,
                             const nmo_object_id_t *parts, size_t count, float length, float fps)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,"Group");
    nmo_keyedanimation_state_t *state = fixture_state(f,id);
    state->base.has_data = state->base.has_length = state->base.has_character = state->base.has_root_entity = 1;
    state->base.length = length; state->base.frame_rate = fps;
    state->base.flags = CKANIMATION_LINKTOFRAMERATE|CKANIMATION_CANBEBREAK;
    state->base.character = nmo_ref_from_id(character); state->base.root_entity = nmo_ref_from_id(root);
    state->animation_count = (uint32_t)count;
    state->animation_ids = nmo_arena_alloc(nmo_session_get_arena(f->session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    if (count != 0 && state->animation_ids == NULL) return 0;
    for (size_t i = 0; i < count; i++) state->animation_ids[i] = nmo_ref_from_id(parts[i]);
    return id;
}

TEST(engine_transition, keyed_transitions_keep_signed_entry_frames_and_the_targets_clock_direction)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = curve(&f,f.owner_id,8,0,8), b = curve(&f,f.owner_id,20,10,30);
    nmo_object_id_t from = group(&f,0,f.owner_id,&a,1,8,1000), to = group(&f,0,f.owner_id,&b,1,20,-60);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_object_id_t result = eng_create_related_object(f.engine,to,NMO_CID_KEYEDANIMATION);
    ASSERT_TRUE(result != 0);
    ASSERT_TRUE(eng_animation_set_current_step(f.engine,from,.25f));
    float entry = NAN;
    ASSERT_EQ(NMO_OK,eng_keyed_animation_transition(f.engine,result,from,to,CK_TRANSITION_WARPTOSAMEPOS,.25f,0,&entry));
    ASSERT_FLOAT_EQ(5,entry,0);
    const nmo_animation_state_t *state = eng_animation_state(f.engine,result);
    ASSERT_FLOAT_EQ(1,eng_animation_length(state),0); ASSERT_FLOAT_EQ(60,state->frame_rate,0);
    ASSERT_TRUE(state->flags & CKANIMATION_LINKTOFRAMERATE);
    nmo_object_id_t root = eng_keyed_animation_root(f.engine,result);
    ASSERT_FLOAT_EQ(2,eng_animation_sample_pose(f.engine,root,0).position.x,0);
    ASSERT_FLOAT_EQ(15,eng_animation_sample_pose(f.engine,root,1).position.x,0);
    ASSERT_TRUE(eng_animation_set_current_step(f.engine,from,-.25f));
    ASSERT_EQ(NMO_OK,eng_keyed_animation_transition(f.engine,result,from,to,CK_TRANSITION_WARPTOSAMEPOS,2,0,&entry));
    ASSERT_FLOAT_EQ(-5,entry,0);
    ASSERT_FLOAT_EQ(0,x(f.engine,f.owner_id),0);
    ASSERT_FLOAT_EQ(0,((const nmo_keyedanimation_state_t *)fixture_state(&f,to))->base.current_step,0);
    fixture_end(&f);
}

TEST(engine_transition, best_entry_sums_all_matching_channels_and_keeps_the_earliest_tie)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t limb = fixture_create(&f,NMO_CID_3DENTITY,"Limb");
    nmo_object_id_t a[] = {curve(&f,f.owner_id,8,0,0),curve(&f,limb,8,4,4)};
    nmo_object_id_t b[] = {curve(&f,f.owner_id,8,0,16),curve(&f,limb,8,5,-3)};
    nmo_object_id_t from = group(&f,0,f.owner_id,a,COUNT(a),8,1000), to = group(&f,0,f.owner_id,b,COUNT(b),8,1000);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_object_id_t result = eng_create_related_object(f.engine,to,NMO_CID_KEYEDANIMATION);
    float entry = NAN;
    ASSERT_EQ(NMO_OK,eng_keyed_animation_transition(f.engine,result,from,to,CK_TRANSITION_WARPTOBEST,2,6,&entry));
    ASSERT_FLOAT_EQ(0,entry,0);
    const eng_animation_channel_t *parts; size_t count;
    ASSERT_TRUE(eng_keyed_animation_channels(f.engine,result,&parts,&count));
    ASSERT_EQ(2,count);
    ASSERT_FLOAT_EQ(0,eng_animation_sample_pose(f.engine,parts[0].id,2).position.x,0);
    ASSERT_FLOAT_EQ(5,eng_animation_sample_pose(f.engine,parts[1].id,2).position.x,0);
    entry = 99;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_keyed_animation_transition(f.engine,result,from,from,0,2,0,&entry));
    ASSERT_FLOAT_EQ(99,entry,0);
    fixture_end(&f);
}

TEST(engine_transition, channel_matching_uses_the_last_source_and_clears_unmatched_and_reused_outputs)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t limb = fixture_create(&f,NMO_CID_3DENTITY,"Limb");
    nmo_object_id_t a[] = {curve(&f,f.owner_id,10,0,10),curve(&f,f.owner_id,10,0,20)};
    nmo_object_id_t b[] = {curve(&f,f.owner_id,10,100,120),curve(&f,limb,10,20,30),0};
    nmo_object_id_t from = group(&f,0,f.owner_id,a,COUNT(a),10,1000), to = group(&f,0,f.owner_id,b,COUNT(b),10,1000);
    nmo_object_id_t small = group(&f,0,f.owner_id,b,1,10,1000);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    nmo_object_id_t result = eng_create_related_object(f.engine,to,NMO_CID_KEYEDANIMATION);
    ASSERT_TRUE(eng_animation_set_current_step(f.engine,from,.5f));
    float entry;
    ASSERT_EQ(NMO_OK,eng_keyed_animation_transition(f.engine,result,from,to,0,4,0,&entry));
    const eng_animation_channel_t *parts; size_t count;
    ASSERT_TRUE(eng_keyed_animation_channels(f.engine,result,&parts,&count));
    ASSERT_EQ(3,count);
    nmo_object_id_t root = parts[0].id;
    ASSERT_FLOAT_EQ(10,eng_animation_sample_pose(f.engine,root,0).position.x,0);
    ASSERT_EQ(0u,eng_animation_sample_pose(f.engine,parts[1].id,4).present);
    ASSERT_EQ(0u,eng_animation_sample_pose(f.engine,parts[2].id,4).present);
    ASSERT_TRUE(eng_animation_set_current_step(f.engine,result,.5f));
    ASSERT_EQ(NMO_OK,eng_keyed_animation_transition(f.engine,result,result,small,0,2,0,&entry));
    ASSERT_EQ(root,eng_keyed_animation_root(f.engine,result));
    ASSERT_FLOAT_EQ(55,eng_animation_sample_pose(f.engine,root,0).position.x,1e-6f);
    ASSERT_FLOAT_EQ(100,eng_animation_sample_pose(f.engine,root,2).position.x,0);
    ASSERT_TRUE(eng_keyed_animation_channels(f.engine,result,&parts,&count));
    ASSERT_EQ(3,count);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_transition, generated_pose_channels_sample_raw_frames_and_preserve_the_borrowed_model);
REGISTER_TEST(engine_transition, velocity_sampling_handles_fractional_duration_and_in_place_rebuilds);
REGISTER_TEST(engine_transition, morph_endpoints_survive_in_place_rebuilds_and_mismatched_vertex_sets);
REGISTER_TEST(engine_transition, root_heading_is_removed_and_ignored_channels_do_not_generate_keys);
REGISTER_TEST(engine_transition, a_missing_source_morph_starts_from_the_current_runtime_mesh);
REGISTER_TEST(engine_transition, keyed_transitions_keep_signed_entry_frames_and_the_targets_clock_direction);
REGISTER_TEST(engine_transition, best_entry_sums_all_matching_channels_and_keeps_the_earliest_tie);
REGISTER_TEST(engine_transition, channel_matching_uses_the_last_source_and_clears_unmatched_and_reused_outputs);
TEST_MAIN_END()
