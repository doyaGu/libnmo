#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_character.h"
#include "../../src/engine/engine_transform.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))

static nmo_object_id_t curve(engine_fixture_t *f, const eng_vector_t *points, size_t count,
    const float *world, nmo_object_id_t *ids)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CURVE,"Path");
    nmo_curve_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    if (world != NULL) memcpy(state->base.world_matrix,world,16*sizeof(float));
    state->control_point_count = (uint32_t)count;
    state->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(f->session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    if (count != 0 && state->control_point_ids == NULL) return 0;
    for (size_t i = 0; i < count; i++) {
        nmo_object_id_t point = fixture_create(f,NMO_CID_CURVEPOINT,"Point");
        nmo_curvepoint_state_t *p = fixture_state(f,point);
        if (p == NULL) return 0;
        const float *m = state->base.world_matrix;
        nmo_curvepoint_set_position(p,points[i].x*m[0]+points[i].y*m[4]+points[i].z*m[8]+m[12],
            points[i].x*m[1]+points[i].y*m[5]+points[i].z*m[9]+m[13],
            points[i].x*m[2]+points[i].y*m[6]+points[i].z*m[10]+m[14]);
        p->base.parent = nmo_ref_from_id(id); p->base.has_entityndata_chunk = 1;
        p->curve = nmo_ref_from_id(id); p->linear = 1;
        state->control_point_ids[i] = nmo_ref_from_id(point);
        if (ids != NULL) ids[i] = point;
    }
    return id;
}

static nmo_object_id_t character(engine_fixture_t *f, nmo_object_id_t *root)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CHARACTER,"Actor");
    *root = fixture_create(f,NMO_CID_BODYPART,"Root");
    nmo_character_state_t *actor = fixture_state(f,id);
    nmo_bodypart_state_t *body = fixture_state(f,*root);
    if (actor == NULL || body == NULL) return 0;
    actor->root_body_part = nmo_ref_from_id(*root);
    ((nmo_3dentity_state_t *)body)->parent = nmo_ref_from_id(id);
    ((nmo_3dentity_state_t *)body)->has_entityndata_chunk = 1;
    body->has_character = 1; body->character = nmo_ref_from_id(id);
    return id;
}

static void position(nmo_engine_t *engine, nmo_object_id_t entity, float x, float y, float z)
{
    float m[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,entity,m));
    ASSERT_FLOAT_EQ(x,m[12],1e-5f); ASSERT_FLOAT_EQ(y,m[13],1e-5f); ASSERT_FLOAT_EQ(z,m[14],1e-5f);
}

TEST(engine_character_curve, curve_length_uses_fitted_local_points_and_live_geometry)
{
    engine_fixture_t f; fixture_begin_document(&f);
    const eng_vector_t points[] = {{0,0,0},{3,4,0},{3,4,12}};
    const float world[] = {2,0,0,0, 0,3,0,0, 0,0,4,0, 10,20,30,1};
    nmo_object_id_t ids[3], path = curve(&f,points,COUNT(points),world,ids); ASSERT_TRUE(path != 0);
    nmo_curve_state_t *state = fixture_state(&f,path);
    nmo_object_id_t empty = curve(&f,NULL,0,NULL,NULL);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    float length = -1; ASSERT_TRUE(eng_curve_length(f.engine,path,&length)); ASSERT_FLOAT_EQ(17,length,1e-5f);
    eng_vector_t point; ASSERT_TRUE(eng_curve_position(f.engine,path,5.0f/17,&point));
    ASSERT_FLOAT_EQ(16,point.x,1e-5f); ASSERT_FLOAT_EQ(32,point.y,1e-5f); ASSERT_FLOAT_EQ(30,point.z,1e-5f);
    state->opened = 0; ASSERT_TRUE(eng_curve_length(f.engine,path,&length)); ASSERT_FLOAT_EQ(17+sqrtf(169),length,1e-5f);
    state->opened = 1; state->fitting_coeff = 1;
    ASSERT_TRUE(eng_curve_length(f.engine,path,&length)); ASSERT_FLOAT_EQ(8.5f,length,1e-5f);
    state->fitting_coeff = 0;
    const eng_vector_t moved = {10,20,30}; eng_entity_set_position(f.engine,eng_entity_index(f.engine,ids[1]),&moved,-1,false);
    ASSERT_TRUE(eng_curve_length(f.engine,path,&length)); ASSERT_FLOAT_EQ(13,length,1e-5f);
    ASSERT_TRUE(eng_curve_length(f.engine,empty,&length)); ASSERT_FLOAT_EQ(0,length,0);
    ASSERT_FALSE(eng_curve_position(f.engine,empty,0,&point));
    ASSERT_FALSE(eng_curve_length(f.engine,f.owner_id,&length));
    nmo_engine_destroy_object(f.engine,path); ASSERT_FALSE(eng_curve_length(f.engine,path,&length));
    fixture_end(&f);
}

TEST(engine_character_curve, aligning_a_character_keeps_root_pose_and_recenters_active_keys)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_3dentity_state_t *a = fixture_state(&f,actor), *b = fixture_state(&f,body);
    a->world_matrix[12] = 5; a->world_matrix[13] = 6; a->world_matrix[14] = 7;
    b->world_matrix[12] = 8; b->world_matrix[13] = 9; b->world_matrix[14] = 10;
    nmo_object_id_t channel = fixture_create(&f,NMO_CID_OBJECTANIMATION,"Root keys");
    nmo_objectanimation_state_t *part = fixture_state(&f,channel);
    float keys[] = {0,2,0,0,8,10,0,0};
    nmo_objanim_controller_t controller = {0x637c4301u,2,sizeof(keys),keys};
    part->format = CKOBJANIM_FORMAT_CONTROLLERS; part->has_length = 1; part->length = 8;
    part->entity = nmo_ref_from_id(body); part->controller_count = 1; part->controllers = &controller;
    nmo_object_id_t animation = fixture_create(&f,NMO_CID_KEYEDANIMATION,"Active");
    nmo_keyedanimation_state_t *clip = fixture_state(&f,animation);
    nmo_ref_t ref = nmo_ref_from_id(channel); clip->animation_ids = &ref; clip->animation_count = 1;
    clip->base.has_length = clip->base.has_character = clip->base.has_root_entity = 1;
    clip->base.length = 8; clip->base.character = nmo_ref_from_id(actor); clip->base.root_entity = nmo_ref_from_id(body);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_TRUE(eng_character_align_root(f.engine,actor)); position(f.engine,actor,8,9,10); position(f.engine,body,8,9,10);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,animation));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,animation,4)); position(f.engine,body,14,9,10);
    ASSERT_TRUE(eng_character_align_root(f.engine,actor)); position(f.engine,actor,14,9,10); position(f.engine,body,14,9,10);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,animation,4)); position(f.engine,body,14,9,10);
    ASSERT_FLOAT_EQ(2,keys[1],0); ASSERT_FALSE(eng_character_align_root(f.engine,f.owner_id));
    nmo_engine_destroy_object(f.engine,actor); ASSERT_FALSE(eng_character_align_root(f.engine,actor));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_character_curve, curve_length_uses_fitted_local_points_and_live_geometry);
REGISTER_TEST(engine_character_curve, aligning_a_character_keeps_root_pose_and_recenters_active_keys);
TEST_MAIN_END()
