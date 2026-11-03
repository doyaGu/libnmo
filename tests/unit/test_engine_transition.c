#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_module.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_character.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "engine/nmo_engine_render.h"
#include "../../src/engine/engine_scene.h"

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
    ASSERT_FLOAT_EQ(sinf(56.25f*(float)acos(-1)/180),runtime.vertices[0].normal.x,.001f);
    ASSERT_FLOAT_EQ(0,runtime.vertices[0].normal.y,.001f);
    ASSERT_FLOAT_EQ(cosf(56.25f*(float)acos(-1)/180),runtime.vertices[0].normal.z,.001f);
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

typedef struct warp_rig {
    engine_fixture_t f;
    nmo_object_id_t actor, root, limb, from, to, third, plain, warper, secondary;
} warp_rig_t;

static void warp_rig_begin(warp_rig_t *r, float source_rate, bool breakable, float target_rate, bool linked)
{
    r->warper = 0;
    engine_fixture_t *f = &r->f;
    fixture_begin_document(f);
    r->actor = fixture_create(f,NMO_CID_CHARACTER,"Actor");
    r->root = fixture_create(f,NMO_CID_BODYPART,"Root");
    r->limb = fixture_create(f,NMO_CID_BODYPART,"Limb");
    nmo_character_state_t *actor = fixture_state(f,r->actor);
    actor->root_body_part = nmo_ref_from_id(r->root);
    nmo_object_id_t entities[] = {r->actor,r->root,r->limb};
    for (size_t i = 0; i < COUNT(entities); i++) {
        nmo_3dentity_state_t *entity = fixture_state(f,entities[i]);
        entity->world_matrix[12] = 100;
        entity->moveable_flags &= ~VX_MOVEABLE_WORLDALIGNED;
        if (i != 0) {
            entity->parent = nmo_ref_from_id(i == 1 ? r->actor : r->root);
            nmo_bodypart_state_t *body = fixture_state(f,entities[i]);
            body->has_character = 1; body->character = nmo_ref_from_id(r->actor);
        }
    }
    nmo_object_id_t a[] = {curve(f,r->root,10,5,15),curve(f,r->limb,10,10,20)};
    nmo_object_id_t b[] = {curve(f,r->root,10,100,120),curve(f,r->limb,10,20,40)};
    nmo_object_id_t c[] = {curve(f,r->root,10,200,230),curve(f,r->limb,10,40,60)};
    r->from = group(f,r->actor,r->root,a,COUNT(a),10,source_rate);
    r->to = group(f,r->actor,r->root,b,COUNT(b),10,target_rate);
    r->third = group(f,r->actor,r->root,c,COUNT(c),10,1000);
    nmo_object_id_t overlay = curve(f,r->limb,8,100,180);
    r->secondary = group(f,r->actor,r->root,&overlay,1,8,target_rate);
    if (!breakable) ((nmo_keyedanimation_state_t *)fixture_state(f,r->from))->base.flags &= ~CKANIMATION_CANBEBREAK;
    nmo_keyedanimation_state_t *to = fixture_state(f,r->to);
    to->base.flags &= ~CKANIMATION_CANBEBREAK;
    if (!linked) to->base.flags &= ~CKANIMATION_LINKTOFRAMERATE;
    r->plain = fixture_create(f,NMO_CID_ANIMATION,"Plain");
    nmo_animation_state_t *plain = fixture_state(f,r->plain);
    plain->has_character = plain->has_length = plain->has_data = 1;
    plain->character = nmo_ref_from_id(r->actor); plain->length = 10; plain->frame_rate = 1000;
    fixture_begin_edit(f); fixture_commit_bare(f,NULL);
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(f->engine,&frame));
    ASSERT_TRUE(eng_keyed_animation_center(f->engine,r->from,0));
    ASSERT_TRUE(eng_animation_set_frame(f->engine,r->from,0));
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f->engine,r->actor,r->from));
}

static void warp_tick(warp_rig_t *r, float delta)
{
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(r->f.engine,&frame));
    ASSERT_EQ(NMO_OK,eng_character_process(r->f.engine,r->actor,delta));
}

static void warp_pose(warp_rig_t *r, float world, float local)
{
    ASSERT_FLOAT_EQ(world,x(r->f.engine,r->actor),.0001f);
    ASSERT_FLOAT_EQ(world,x(r->f.engine,r->root),.0001f);
    ASSERT_FLOAT_EQ(world+local,x(r->f.engine,r->limb),.0001f);
    float matrix[16];
    ASSERT_TRUE(nmo_engine_entity_local_matrix(r->f.engine,r->root,matrix));
    ASSERT_FLOAT_EQ(0,matrix[12],.0001f);
    ASSERT_TRUE(nmo_engine_entity_local_matrix(r->f.engine,r->limb,matrix));
    ASSERT_FLOAT_EQ(local,matrix[12],.0001f);
    ASSERT_EQ(r->actor,nmo_engine_entity_parent(r->f.engine,r->root));
    ASSERT_EQ(r->root,nmo_engine_entity_parent(r->f.engine,r->limb));
}

static void warp_start(warp_rig_t *r)
{
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r->f.engine,r->actor,r->to,CK_TRANSITION_WARPSAMEPOS,2));
    nmo_object_id_t warper = eng_character_warper(r->f.engine,r->actor);
    ASSERT_EQ(warper,eng_character_active_animation(r->f.engine,r->actor));
    uint32_t mode = 0; nmo_object_id_t source = 0, target = 0; float from = NAN, to = NAN;
    ASSERT_TRUE(eng_character_warper_parameters(r->f.engine,r->actor,&mode,&source,&from,&target,&to));
    ASSERT_EQ(CK_TRANSITION_WARPSAMEPOS,mode); ASSERT_EQ(r->from,source); ASSERT_EQ(r->to,target);
    ASSERT_FLOAT_EQ(2,from,1e-6f); ASSERT_FLOAT_EQ(2,to,1e-6f);
    r->warper = warper;
}

static void warp_clock(warp_rig_t *r, nmo_object_id_t id, bool linked, float rate)
{
    nmo_animation_state_t *state = (nmo_animation_state_t *)eng_animation_state(r->f.engine,id);
    state->flags = (state->flags & ~CKANIMATION_LINKTOFRAMERATE) | (linked ? CKANIMATION_LINKTOFRAMERATE : 0);
    state->frame_rate = rate;
}

TEST(engine_transition, primary_warps_transfer_root_motion_and_signed_or_paused_target_remainders)
{
    const float rates[] = {1000,-1000,0}, frames[] = {4,0,2}, worlds[] = {203,195,199}, locals[] = {28,20,24};
    for (size_t i = 0; i < COUNT(rates); i++) {
        warp_rig_t r;
        warp_rig_begin(&r,1000,true,rates[i],true);
        warp_tick(&r,2); warp_pose(&r,102,12);
        warp_start(&r);
        nmo_object_id_t warper = r.warper;
        warp_clock(&r,warper,true,1000);
        warp_pose(&r,102,12);
        warp_tick(&r,1); warp_pose(&r,150.5f,18);
        warp_tick(&r,3); warp_pose(&r,worlds[i],locals[i]);
        ASSERT_EQ(r.to,eng_character_active_animation(r.f.engine,r.actor));
        ASSERT_EQ(0,eng_character_next_animation(r.f.engine,r.actor));
        float frame;
        ASSERT_TRUE(eng_animation_frame(r.f.engine,r.to,&frame));
        ASSERT_FLOAT_EQ(frames[i],frame,1e-6f);
        ASSERT_TRUE(eng_animation_frame(r.f.engine,r.from,&frame));
        ASSERT_FLOAT_EQ(2,frame,1e-6f);
        fixture_end(&r.f);
    }
}

TEST(engine_transition, reverse_warps_use_the_actual_entry_and_mixed_clocks_share_unused_time)
{
    for (size_t reverse = 0; reverse < 2; reverse++) {
        warp_rig_t r;
        warp_rig_begin(&r,1000,true,reverse ? -1000 : 1000,true);
        warp_tick(&r,2);
        warp_start(&r);
        nmo_object_id_t warper = r.warper;
        warp_tick(&r,1); warp_pose(&r,150.5f,18);
        warp_clock(&r,warper,true,-1000);
        warp_tick(&r,2); warp_pose(&r,reverse ? 100 : 104,reverse ? 22 : 26);
        float frame; ASSERT_TRUE(eng_animation_frame(r.f.engine,r.to,&frame));
        ASSERT_FLOAT_EQ(reverse ? 1 : 3,frame,1e-6f);
        fixture_end(&r.f);
    }
    warp_rig_t r;
    warp_rig_begin(&r,1000,true,1000,false);
    warp_tick(&r,2);
    warp_start(&r);
    nmo_object_id_t warper = r.warper;
    warp_clock(&r,warper,true,1000);
    warp_tick(&r,1); warp_tick(&r,2); warp_pose(&r,200,25);
    fixture_end(&r.f);
    warp_rig_begin(&r,1000,true,1000,true);
    warp_tick(&r,2); warp_start(&r); warper = r.warper;
    warp_tick(&r,1.5f); warp_pose(&r,174.75f,21);
    warp_clock(&r,warper,false,0);
    warp_tick(&r,4); warp_pose(&r,203,28);
    fixture_end(&r.f);
}

TEST(engine_transition, warp_requests_respect_source_readiness_and_preserve_plain_target_cursors)
{
    warp_rig_t r;
    warp_rig_begin(&r,1000,false,1000,true);
    warp_tick(&r,2);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.from,eng_character_active_animation(r.f.engine,r.actor));
    ASSERT_EQ(0,eng_character_next_animation(r.f.engine,r.actor));
    warp_pose(&r,102,12);
    ASSERT_TRUE(eng_animation_set_current_step(r.f.engine,r.plain,.3f));
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.plain,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.from,eng_character_active_animation(r.f.engine,r.actor));
    warp_tick(&r,7.8f);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.plain,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.plain,eng_character_active_animation(r.f.engine,r.actor));
    float frame; ASSERT_TRUE(eng_animation_frame(r.f.engine,r.plain,&frame)); ASSERT_FLOAT_EQ(3,frame,1e-6f);
    ASSERT_FLOAT_EQ(109.8f,x(r.f.engine,r.actor),.0001f);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.to,eng_character_active_animation(r.f.engine,r.actor));
    ASSERT_TRUE(eng_animation_frame(r.f.engine,r.to,&frame)); ASSERT_FLOAT_EQ(0,frame,0);
    fixture_end(&r.f);
}

TEST(engine_transition, pending_unbreakable_targets_win_and_in_place_retargeting_keeps_the_current_pose)
{
    warp_rig_t r;
    warp_rig_begin(&r,1000,true,1000,true);
    warp_tick(&r,2);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.from,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,NAN));
    warp_pose(&r,102,12);
    warp_start(&r);
    nmo_object_id_t warper = r.warper;
    warp_tick(&r,1); warp_pose(&r,150.5f,18);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.third,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.to,eng_character_next_animation(r.f.engine,r.actor));
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.plain,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(warper,eng_character_active_animation(r.f.engine,r.actor));
    ((nmo_animation_state_t *)eng_animation_state(r.f.engine,r.to))->flags |= CKANIMATION_CANBEBREAK;
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.third,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(warper,eng_character_active_animation(r.f.engine,r.actor));
    ASSERT_EQ(r.third,eng_character_next_animation(r.f.engine,r.actor));
    warp_pose(&r,150.5f,18);
    warp_tick(&r,1); warp_pose(&r,222.75f,29);
    fixture_end(&r.f);
}

TEST(engine_transition, unbreakable_reverse_sources_wait_for_zero_and_forward_sources_can_warp_near_the_end)
{
    warp_rig_t r;
    warp_rig_begin(&r,-1000,false,1000,true);
    ASSERT_TRUE(eng_keyed_animation_center(r.f.engine,r.from,10));
    ASSERT_TRUE(eng_animation_set_frame(r.f.engine,r.from,10));
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.from,eng_character_active_animation(r.f.engine,r.actor));
    warp_tick(&r,2);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.from,eng_character_active_animation(r.f.engine,r.actor));
    warp_tick(&r,8); warp_pose(&r,90,10);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(r.to,eng_character_active_animation(r.f.engine,r.actor));
    ASSERT_FLOAT_EQ(90,x(r.f.engine,r.actor),.0001f);
    fixture_end(&r.f);
    warp_rig_begin(&r,1000,false,1000,true);
    warp_tick(&r,9.8f);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(eng_character_warper(r.f.engine,r.actor),eng_character_active_animation(r.f.engine,r.actor));
    ASSERT_EQ(r.to,eng_character_next_animation(r.f.engine,r.actor));
    ASSERT_FLOAT_EQ(109.8f,x(r.f.engine,r.actor),.0001f);
    fixture_end(&r.f);
}

TEST(engine_transition, velocity_warps_integrate_root_motion_and_failed_builds_preserve_playback)
{
    warp_rig_t r;
    warp_rig_begin(&r,1000,true,1000,true);
    warp_tick(&r,2);
    ASSERT_EQ(NMO_ERR_NOT_SUPPORTED,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,
        CK_TRANSITION_WARPSAMEPOS|CK_TRANSITION_USEVELOCITY,FLT_MAX));
    ASSERT_EQ(r.from,eng_character_active_animation(r.f.engine,r.actor));
    ASSERT_EQ(0,eng_character_next_animation(r.f.engine,r.actor));
    warp_pose(&r,102,12);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,
        CK_TRANSITION_WARPSAMEPOS|CK_TRANSITION_USEVELOCITY,2));
    warp_tick(&r,1); warp_pose(&r,102.4f,18);
    warp_tick(&r,1); warp_pose(&r,103,24);
    ASSERT_EQ(r.to,eng_character_active_animation(r.f.engine,r.actor));
    ASSERT_TRUE((((const nmo_keyedanimation_state_t *)fixture_state(&r.f,r.to))->base.flags & CKANIMATION_CANBEBREAK) == 0);
    fixture_end(&r.f);
    warp_rig_begin(&r,1000,true,1000,true);
    warp_tick(&r,2);
    nmo_object_id_t warper = eng_character_warper(r.f.engine,r.actor);
    ASSERT_TRUE(warper != 0);
    nmo_engine_destroy_object(r.f.engine,warper);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_set_next_animation_warp(r.f.engine,r.actor,r.to,CK_TRANSITION_WARPSTART,2));
    ASSERT_EQ(0,eng_character_warper(r.f.engine,r.actor));
    warp_pose(&r,102,12);
    fixture_end(&r.f);
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name;
    return data;
}

TEST(engine_transition, loaded_character_warps_keep_module_bindings_and_copied_transitions_advance_independently)
{
    const nmo_objectanimation_format_t formats[] = {CKOBJANIM_FORMAT_CONTROLLERS,
        CKOBJANIM_FORMAT_NEWDATA,CKOBJANIM_FORMAT_LEGACY,CKOBJANIM_FORMAT_SHARED};
    for (size_t c = 0; c < COUNT(formats); c++) {
        warp_rig_t saved;
        warp_rig_begin(&saved,1000,true,1000,true);
        engine_fixture_t *f = &saved.f;
        nmo_character_state_t *character = fixture_state(f,saved.actor);
        character->active_animation = nmo_ref_from_id(saved.from);
        character->base.entity_flags |= CK_3DENTITY_CHARACTERDOPROCESS;
        character->base.world_matrix[12] = 102;
        nmo_beobject_state_t *activity = (nmo_beobject_state_t *)character;
        activity->has_single_activity = 1; activity->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        ((nmo_3dentity_state_t *)fixture_state(f,saved.root))->world_matrix[12] = 102;
        ((nmo_3dentity_state_t *)fixture_state(f,saved.limb))->world_matrix[12] = 114;
        nmo_keyedanimation_state_t *source = fixture_state(f,saved.from);
        source->base.has_current_step = 1; source->base.current_step = .2f;
        nmo_object_id_t root_channel = nmo_ref_runtime_id(&source->animation_ids[0]);
        nmo_objectanimation_state_t *root = fixture_state(f,root_channel);
        root->has_root_pos = 1; root->root_pos.x = -7; root->format = formats[c];
        if (formats[c] == CKOBJANIM_FORMAT_SHARED) {
            nmo_object_id_t shared = curve(f,0,10,5,15);
            root->has_shared_anim = 1; root->shared_anim = nmo_ref_from_id(shared);
            root->controllers = NULL; root->controller_count = 0;
        } else if (formats[c] == CKOBJANIM_FORMAT_LEGACY) {
            nmo_object_t *object = nmo_object_repository_find_by_id(f->repo,root_channel);
            object->chunk = nmo_chunk_create(nmo_session_get_arena(f->session));
            ASSERT_NOT_NULL(object->chunk);
            object->chunk->class_id = NMO_CID_OBJECTANIMATION; object->chunk->data_version = 0;
        }
        ASSERT_EQ(NMO_OK,nmo_object_set_name(nmo_object_repository_find_by_id(f->repo,saved.from),"Source"));
        ASSERT_EQ(NMO_OK,nmo_object_set_name(nmo_object_repository_find_by_id(f->repo,saved.to),"Target"));
        const char *path = NMO_TEST_SCRATCH_FILE("engine_transition.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(f->session,path,NULL,NULL));
        engine_fixture_t host;
        fixture_begin_document(&host); fixture_begin_edit(&host); fixture_commit_bare(&host,NULL);
        ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
        int32_t file = -1;
        ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"transition.cmo",true,&file));
        ASSERT_TRUE(file > 0);
        nmo_object_id_t actor = nmo_engine_object_named(host.engine,"Actor",NMO_CID_CHARACTER);
        nmo_object_id_t body = nmo_engine_object_named(host.engine,"Root",NMO_CID_BODYPART);
        nmo_object_id_t limb = nmo_engine_object_named(host.engine,"Limb",NMO_CID_BODYPART);
        nmo_object_id_t from = nmo_engine_object_named(host.engine,"Source",NMO_CID_KEYEDANIMATION);
        nmo_object_id_t to = nmo_engine_object_named(host.engine,"Target",NMO_CID_KEYEDANIMATION);
        ASSERT_TRUE(actor != 0 && actor != saved.actor && body != 0 && limb != 0 && from != 0 && to != 0);
        ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(host.engine,actor,to,CK_TRANSITION_WARPSAMEPOS,2));
        nmo_object_id_t warper = eng_character_warper(host.engine,actor);
        ASSERT_TRUE(warper != 0 && warper != from && warper != to);
        nmo_engine_frame_t frame;
        ASSERT_EQ(NMO_OK,nmo_engine_render_frame(host.engine,&frame));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,1));
        ASSERT_FLOAT_EQ(150.5f,x(host.engine,actor),.0001f);
        ASSERT_FLOAT_EQ(168.5f,x(host.engine,limb),.0001f);
        nmo_object_id_t originals[] = {actor,body,limb,warper,to}, copies[COUNT(originals)] = {0};
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,originals,COUNT(originals),false,copies));
        ASSERT_EQ(copies[3],eng_character_active_animation(host.engine,copies[0]));
        ASSERT_EQ(copies[4],eng_character_next_animation(host.engine,copies[0]));
        ASSERT_EQ(NMO_OK,nmo_engine_render_frame(host.engine,&frame));
        ASSERT_EQ(NMO_OK,eng_character_process(host.engine,copies[0],1));
        ASSERT_EQ(copies[4],eng_character_active_animation(host.engine,copies[0]));
        ASSERT_FLOAT_EQ(199,x(host.engine,copies[0]),.0001f);
        ASSERT_FLOAT_EQ(199,x(host.engine,copies[1]),.0001f);
        ASSERT_FLOAT_EQ(223,x(host.engine,copies[2]),.0001f);
        ASSERT_FLOAT_EQ(150.5f,x(host.engine,actor),.0001f);
        ASSERT_FLOAT_EQ(168.5f,x(host.engine,limb),.0001f);
        ASSERT_EQ(warper,eng_character_active_animation(host.engine,actor));
        eng_animation_source_t loaded;
        ASSERT_TRUE(eng_animation_source(host.engine,eng_keyed_animation_root(host.engine,from),&loaded));
        ASSERT_EQ(formats[c],loaded.state->format);
        ASSERT_FLOAT_EQ(-7,loaded.state->root_pos.x,0);
        ASSERT_FLOAT_EQ(.2f,eng_animation_state(host.engine,from)->current_step,0);
        ASSERT_FLOAT_EQ(0,x(host.engine,host.owner_id),0);
        ASSERT_FLOAT_EQ(-7,root->root_pos.x,0);
        ASSERT_FLOAT_EQ(.2f,source->base.current_step,0);
        ASSERT_FLOAT_EQ(102,character->base.world_matrix[12],0);
        fixture_end(&host); fixture_end(f); remove(path);
    }
}

TEST(engine_transition, generated_warps_and_their_channels_follow_dynamic_character_cleanup)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t actor = fixture_create(&f,NMO_CID_CHARACTER,"Actor");
    nmo_object_id_t root = fixture_create(&f,NMO_CID_BODYPART,"Root");
    ((nmo_character_state_t *)fixture_state(&f,actor))->root_body_part = nmo_ref_from_id(root);
    ((nmo_3dentity_state_t *)fixture_state(&f,root))->parent = nmo_ref_from_id(actor);
    nmo_object_id_t a = curve(&f,root,10,0,10), b = curve(&f,root,10,20,30);
    nmo_object_id_t from = group(&f,actor,root,&a,1,10,1000), to = group(&f,actor,root,&b,1,10,1000);
    nmo_object_id_t remove = fixture_block(&f,NMO_GUID(0x0cba3195,0x53440e4c),"Delete Dynamic Objects");
    fixture_link(&f,f.script_in,BLOCK_IN(&f,remove,0),0);
    fixture_commit(&f,NULL);
    eng_mark_dynamic(f.engine,actor);
    ASSERT_TRUE(eng_animation_set_current_step(f.engine,from,.5f));
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,from));
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation_warp(f.engine,actor,to,CK_TRANSITION_WARPSTART,2));
    nmo_object_id_t warper = eng_character_warper(f.engine,actor);
    nmo_object_id_t generated = eng_keyed_animation_root(f.engine,warper);
    nmo_object_id_t kept = eng_create_related_object(f.engine,to,NMO_CID_OBJECTANIMATION);
    ASSERT_TRUE(warper != 0 && generated != 0 && kept != 0);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(0,eng_character_active_animation(f.engine,actor));
    ASSERT_TRUE(eng_animation_state(f.engine,warper) == NULL);
    nmo_object_id_t *live = NULL;
    size_t count = eng_objects_of_class(f.engine,NMO_CID_OBJECTANIMATION,&live);
    bool found_kept = false;
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE(live[i] != generated);
        if (live[i] == kept) found_kept = true;
    }
    free(live);
    ASSERT_TRUE(found_kept);
    fixture_end(&f);
}

static void secondary_rig_begin(warp_rig_t *r, float rate, bool linked)
{
    warp_rig_begin(r,1000,true,1000,true);
    nmo_keyedanimation_state_t *main = fixture_state(&r->f,r->from);
    main->base.length = 100;
    for (uint32_t i = 0; i < main->animation_count; i++) {
        nmo_objectanimation_state_t *part = fixture_state(&r->f,nmo_ref_runtime_id(&main->animation_ids[i]));
        part->length = 100;
        float *keys = part->controllers[0].data;
        keys[4] = 100; keys[5] = keys[1]+100;
    }
    nmo_keyedanimation_state_t *overlay = fixture_state(&r->f,r->secondary);
    overlay->base.frame_rate = rate;
    if (!linked) overlay->base.flags &= ~CKANIMATION_LINKTOFRAMERATE;
    nmo_keyedanimation_state_t *other = fixture_state(&r->f,r->third);
    other->animation_ids++; other->animation_count = 1;
    warp_tick(r,2);
}

static float animation_frame(nmo_engine_t *engine, nmo_object_id_t animation)
{
    float value = NAN;
    eng_animation_frame(engine,animation,&value);
    return value;
}

TEST(engine_transition, secondary_entry_holds_its_endpoint_before_handing_off_and_reuses_the_private_warp)
{
    warp_rig_t r;
    secondary_rig_begin(&r,1000,true);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,2,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
    nmo_object_id_t warper = eng_character_secondary_warper(r.f.engine,r.actor,0);
    ASSERT_TRUE(warper != 0);
    ASSERT_TRUE(eng_animation_state(r.f.engine,warper)->flags & CKANIMATION_SECONDARYWARPER);
    ASSERT_EQ(r.secondary,eng_character_secondary_animation(r.f.engine,r.actor,0));
    ASSERT_FLOAT_EQ(0,animation_frame(r.f.engine,r.secondary),0);
    warp_pose(&r,102,12);
    warp_tick(&r,2); warp_pose(&r,104,66);
    warp_tick(&r,2); warp_pose(&r,106,120);
    ASSERT_FLOAT_EQ(4,animation_frame(r.f.engine,warper),0);
    ASSERT_FLOAT_EQ(0,animation_frame(r.f.engine,r.secondary),0);
    warp_tick(&r,1); warp_pose(&r,107,130);
    ASSERT_FLOAT_EQ(3,animation_frame(r.f.engine,r.secondary),1e-6f);
    ASSERT_FLOAT_EQ(4,animation_frame(r.f.engine,warper),0);
    warp_tick(&r,1); warp_pose(&r,108,140);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(r.f.engine,r.actor,r.secondary));
    warp_tick(&r,1); warp_pose(&r,109,19);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,1,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,2,0));
    ASSERT_EQ(warper,eng_character_secondary_warper(r.f.engine,r.actor,0));
    warp_tick(&r,1); warp_pose(&r,110,64.5f);
    fixture_end(&r.f);
}

TEST(engine_transition, secondary_entry_converts_remaining_time_for_reverse_paused_and_mixed_clocks)
{
    const float rates[] = {500,-500,0}, frames[] = {4,2,3};
    for (size_t i = 0; i < COUNT(rates); i++) {
        warp_rig_t r;
        secondary_rig_begin(&r,rates[i],true);
        ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,3,
            CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
        nmo_object_id_t warper = eng_character_secondary_warper(r.f.engine,r.actor,0);
        warp_clock(&r,warper,true,1000);
        warp_tick(&r,6);
        ASSERT_FLOAT_EQ(frames[i],animation_frame(r.f.engine,r.secondary),1e-6f);
        warp_pose(&r,108,100+10*frames[i]);
        fixture_end(&r.f);
    }
    warp_rig_t r;
    secondary_rig_begin(&r,-1000,false);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,3,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
    nmo_object_id_t warper = eng_character_secondary_warper(r.f.engine,r.actor,0);
    ASSERT_FLOAT_EQ(1000,eng_animation_state(r.f.engine,warper)->frame_rate,0);
    warp_clock(&r,warper,true,1000);
    warp_tick(&r,6); warp_pose(&r,108,100+10*(3+1.0f/3));
    ASSERT_FLOAT_EQ(-1000,eng_animation_state(r.f.engine,r.secondary)->frame_rate,0);
    fixture_end(&r.f);
    secondary_rig_begin(&r,500,true);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,3,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
    warper = eng_character_secondary_warper(r.f.engine,r.actor,0);
    warp_clock(&r,warper,true,1000); warp_tick(&r,3.5f);
    warp_clock(&r,warper,false,-1000); warp_tick(&r,6);
    ASSERT_FLOAT_EQ(4.5f,animation_frame(r.f.engine,r.secondary),1e-6f);
    warp_pose(&r,111.5f,145);
    fixture_end(&r.f);
}

TEST(engine_transition, unlinked_secondary_entries_use_calls_and_missing_or_identical_primaries_play_directly)
{
    warp_rig_t r;
    secondary_rig_begin(&r,-1000,false);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,3,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
    nmo_object_id_t warper = eng_character_secondary_warper(r.f.engine,r.actor,0);
    for (size_t i = 0; i < 4; i++) warp_tick(&r,0);
    ASSERT_FLOAT_EQ(4,animation_frame(r.f.engine,warper),0);
    ASSERT_FLOAT_EQ(0,animation_frame(r.f.engine,r.secondary),0);
    warp_tick(&r,0); warp_pose(&r,102,140);
    ASSERT_FLOAT_EQ(4,animation_frame(r.f.engine,r.secondary),0);
    eng_character_flush_secondary(r.f.engine,r.actor);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(r.f.engine,r.actor,0));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,2,
        CKSECONDARYANIMATION_DOWARP,4,0));
    ASSERT_EQ(0,eng_character_secondary_warper(r.f.engine,r.actor,0));
    ASSERT_FLOAT_EQ(2,animation_frame(r.f.engine,r.secondary),0);
    eng_character_flush_secondary(r.f.engine,r.actor);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(r.f.engine,r.actor,r.secondary));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,5,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
    ASSERT_EQ(0,eng_character_secondary_warper(r.f.engine,r.actor,0));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,7,0,1,0));
    ASSERT_FLOAT_EQ(5,animation_frame(r.f.engine,r.secondary),0);
    ASSERT_EQ(1,eng_character_secondary_count(r.f.engine,r.actor));
    fixture_end(&r.f);
}

TEST(engine_transition, overlapping_secondary_entry_warps_release_only_their_own_bones)
{
    warp_rig_t r;
    secondary_rig_begin(&r,1000,true);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.secondary,2,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
    nmo_object_id_t first = eng_character_secondary_warper(r.f.engine,r.actor,0);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary_warp(r.f.engine,r.actor,r.third,5,
        CKSECONDARYANIMATION_DOWARP|CKSECONDARYANIMATION_LOOP,4,0));
    nmo_object_id_t second = eng_character_secondary_warper(r.f.engine,r.actor,1);
    ASSERT_TRUE(first != 0 && second != 0 && first != second);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(r.f.engine,r.actor,r.secondary));
    warp_tick(&r,2); warp_pose(&r,104,31);
    eng_character_flush_secondary(r.f.engine,r.actor);
    warp_tick(&r,1); warp_pose(&r,105,15);
    fixture_end(&r.f);
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
REGISTER_TEST(engine_transition, primary_warps_transfer_root_motion_and_signed_or_paused_target_remainders);
REGISTER_TEST(engine_transition, reverse_warps_use_the_actual_entry_and_mixed_clocks_share_unused_time);
REGISTER_TEST(engine_transition, warp_requests_respect_source_readiness_and_preserve_plain_target_cursors);
REGISTER_TEST(engine_transition, pending_unbreakable_targets_win_and_in_place_retargeting_keeps_the_current_pose);
REGISTER_TEST(engine_transition, unbreakable_reverse_sources_wait_for_zero_and_forward_sources_can_warp_near_the_end);
REGISTER_TEST(engine_transition, velocity_warps_integrate_root_motion_and_failed_builds_preserve_playback);
REGISTER_TEST(engine_transition, loaded_character_warps_keep_module_bindings_and_copied_transitions_advance_independently);
REGISTER_TEST(engine_transition, generated_warps_and_their_channels_follow_dynamic_character_cleanup);
REGISTER_TEST(engine_transition, secondary_entry_holds_its_endpoint_before_handing_off_and_reuses_the_private_warp);
REGISTER_TEST(engine_transition, secondary_entry_converts_remaining_time_for_reverse_paused_and_mixed_clocks);
REGISTER_TEST(engine_transition, unlinked_secondary_entries_use_calls_and_missing_or_identical_primaries_play_directly);
REGISTER_TEST(engine_transition, overlapping_secondary_entry_warps_release_only_their_own_bones);
TEST_MAIN_END()
