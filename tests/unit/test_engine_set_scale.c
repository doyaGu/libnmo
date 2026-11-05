#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/nmo_serialize_context.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))

typedef struct sprite_fixture {
    engine_fixture_t f;
    nmo_object_id_t sprite, material, child;
} sprite_fixture_t;

static void sprite_begin(sprite_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->sprite = fixture_create(&g->f,NMO_CID_SPRITE3D,"Sprite"); g->material = fixture_create(&g->f,NMO_CID_MATERIAL,"Material");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    nmo_sprite3d_state_t *sprite = fixture_state(&g->f,g->sprite); nmo_3dentity_state_t *child = fixture_state(&g->f,g->child);
    ASSERT_NOT_NULL(sprite); ASSERT_NOT_NULL(child); ASSERT_TRUE(g->material != 0);
    sprite->base.has_entityndata_chunk = child->has_entityndata_chunk = 1;
    sprite->base.world_matrix[0] = 2; sprite->base.world_matrix[5] = 3; sprite->base.world_matrix[10] = 4;
    sprite->base.world_matrix[12] = 3; sprite->base.world_matrix[13] = 4; sprite->base.world_matrix[14] = 5;
    sprite->has_data = 1; sprite->mode = VXSPRITE3D_ORIENTABLE; sprite->half_width = 2; sprite->half_height = 3;
    sprite->offset = (nmo_vector2_t){.5f,-.25f}; sprite->uv_rect = (nmo_rect_t){.1f,.2f,.9f,.8f}; sprite->material = nmo_ref_from_id(g->material);
    child->parent = nmo_ref_from_id(g->sprite); child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
}

static void sprite_commit(sprite_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
    eng_show(g->f.engine,g->sprite,1);
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void sprite_size(nmo_engine_t *engine, nmo_object_id_t id, float width, float height)
{
    const eng_sprite3d_t *sprite = eng_sprite3d(engine,id); ASSERT_NOT_NULL(sprite);
    ASSERT_TRUE(isfinite(sprite->min.x) && isfinite(sprite->min.y) && isfinite(sprite->max.x) && isfinite(sprite->max.y));
    ASSERT_FLOAT_EQ(width,sprite->max.x-sprite->min.x,0); ASSERT_FLOAT_EQ(height,sprite->max.y-sprite->min.y,0);
}

static void quad(nmo_engine_t *engine, nmo_object_id_t id, float left, float bottom, float right, float top)
{
    nmo_engine_frame_t frame; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(engine,&frame));
    const nmo_engine_draw_t *draw = NULL;
    for (int32_t i = 0; i < frame.draw_count; i++) if (frame.draws[i].object == id && frame.draws[i].kind == NMO_ENGINE_DRAW_SPRITE3D) {
        ASSERT_TRUE(draw == NULL); draw = &frame.draws[i];
    }
    ASSERT_NOT_NULL(draw); ASSERT_EQ(6,draw->vertex_count);
    const float x[] = {left,left,right,left,right,right}, y[] = {bottom,top,top,bottom,top,bottom};
    const float u[] = {.1f,.1f,.9f,.1f,.9f,.9f}, v[] = {.8f,.2f,.2f,.8f,.2f,.8f};
    for (int i = 0; i < 6; i++) {
        const nmo_engine_vertex_t *vertex = &frame.vertices[draw->first_vertex+i];
        vector((eng_vector_t){vertex->x,vertex->y,vertex->z},(eng_vector_t){x[i],y[i],5});
        ASSERT_FLOAT_EQ(u[i],vertex->u,1e-6f); ASSERT_FLOAT_EQ(v[i],vertex->v,1e-6f);
    }
}

TEST(engine_set_scale, sprite_sizes_update_drawn_geometry_boxes_and_radius_without_changing_transforms_or_document)
{
    sprite_fixture_t g; sprite_begin(&g); sprite_commit(&g); sprite_size(g.f.engine,g.sprite,4,6); quad(g.f.engine,g.sprite,1,-7.25f,9,10.75f);
    float before[16], child[16], after[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.sprite,before));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.child,child));
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){8,2})); sprite_size(g.f.engine,g.sprite,8,2);
    quad(g.f.engine,g.sprite,-1,.25f,15,6.25f);
    eng_vector_t min, max; int32_t index = eng_entity_index(g.f.engine,g.sprite);
    ASSERT_TRUE(eng_entity_local_box(g.f.engine,index,&min,&max)); vector(min,(eng_vector_t){-2,-1.25f,0}); vector(max,(eng_vector_t){6,.75f,0});
    ASSERT_TRUE(eng_entity_world_box(g.f.engine,index,&min,&max)); vector(min,(eng_vector_t){-1,.25f,5}); vector(max,(eng_vector_t){15,6.25f,5});
    ASSERT_FLOAT_EQ(8,eng_entity_radius(g.f.engine,index),0);
    ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.sprite,after)); ASSERT_EQ(0,memcmp(before,after,sizeof(after)));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.child,after)); ASSERT_EQ(0,memcmp(child,after,sizeof(after)));
    const nmo_sprite3d_state_t *state = fixture_state(&g.f,g.sprite); ASSERT_FLOAT_EQ(2,state->half_width,0); ASSERT_FLOAT_EQ(3,state->half_height,0);
    nmo_engine_t *second = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&second)); sprite_size(second,g.sprite,4,6); nmo_engine_destroy(second); fixture_end(&g.f);
}

TEST(engine_set_scale, sprite_zero_and_negative_sizes_keep_signed_native_extents)
{
    sprite_fixture_t g; sprite_begin(&g); sprite_commit(&g);
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){0,-4})); sprite_size(g.f.engine,g.sprite,0,-4);
    quad(g.f.engine,g.sprite,3,11.5f,3,-.5f);
    eng_vector_t min, max; ASSERT_TRUE(eng_entity_local_box(g.f.engine,eng_entity_index(g.f.engine,g.sprite),&min,&max));
    vector(min,(eng_vector_t){0,2.5f,0}); vector(max,(eng_vector_t){0,-1.5f,0});
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){-8,2})); sprite_size(g.f.engine,g.sprite,-8,2); quad(g.f.engine,g.sprite,7,.25f,-9,6.25f); fixture_end(&g.f);
}

TEST(engine_set_scale, copied_sprites_keep_runtime_sizes_and_remap_materials_independently)
{
    sprite_fixture_t g; sprite_begin(&g); sprite_commit(&g);
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){8,2}));
    const nmo_object_id_t originals[] = {g.sprite,g.material}; nmo_object_id_t copies[COUNT(originals)] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,originals,COUNT(originals),false,copies)); ASSERT_TRUE(copies[0] != 0 && copies[1] != 0);
    const eng_sprite3d_t *sprite = eng_sprite3d(g.f.engine,copies[0]); ASSERT_NOT_NULL(sprite); ASSERT_EQ(copies[1],sprite->material);
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){9,4})); sprite_size(g.f.engine,copies[0],8,2);
    nmo_engine_destroy_object(g.f.engine,g.sprite); nmo_engine_destroy_object(g.f.engine,g.material);
    eng_show(g.f.engine,copies[0],1); quad(g.f.engine,copies[0],-1,.25f,15,6.25f);
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,copies[0],(nmo_vector2_t){4,6})); quad(g.f.engine,copies[0],1,-7.25f,9,10.75f); fixture_end(&g.f);
}

TEST(engine_set_scale, sprite_initial_values_restore_runtime_and_serialized_sizes_with_their_offset_and_mode)
{
    sprite_fixture_t g; sprite_begin(&g); sprite_commit(&g);
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){8,2})); eng_save_ic(g.f.engine,g.sprite);
    eng_sprite3d_t snapshot = *eng_sprite3d(g.f.engine,g.sprite);
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){1,1}));
    eng_sprite3d_t *runtime = eng_sprite3d(g.f.engine,g.sprite); runtime->mode = VXSPRITE3D_BILLBOARD; runtime->offset = (nmo_vector2_t){0,0}; runtime->material = 0;
    eng_restore_ic(g.f.engine,g.sprite); runtime = eng_sprite3d(g.f.engine,g.sprite);
    ASSERT_EQ(0,memcmp(&snapshot,runtime,sizeof(snapshot))); quad(g.f.engine,g.sprite,-1,.25f,15,6.25f);
    nmo_sprite3d_state_t saved = *(nmo_sprite3d_state_t *)fixture_state(&g.f,g.sprite);
    saved.half_width = 3; saved.half_height = 4; saved.offset = (nmo_vector2_t){-.5f,.25f}; saved.mode = VXSPRITE3D_XROTATE;
    nmo_arena_t *arena = nmo_session_get_arena(g.f.session); nmo_chunk_t *chunk = nmo_chunk_create(arena); ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_SPRITE3D; ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk));
    nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena,g.f.repo,CK_STATESAVE_ALL);
    ASSERT_EQ(NMO_OK,nmo_sprite3d_serialize(&saved,chunk,NULL,&context)); nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.sprite,chunk)); eng_restore_ic(g.f.engine,g.sprite); sprite_size(g.f.engine,g.sprite,6,8);
    runtime = eng_sprite3d(g.f.engine,g.sprite); ASSERT_EQ(VXSPRITE3D_XROTATE,runtime->mode);
    ASSERT_FLOAT_EQ(-.5f,runtime->offset.x,0); ASSERT_FLOAT_EQ(.25f,runtime->offset.y,0); quad(g.f.engine,g.sprite,-6,-5,6,19); fixture_end(&g.f);
}

TEST(engine_set_scale, missing_deleted_and_non_sprite_objects_do_not_create_sprite_state)
{
    sprite_fixture_t g; sprite_begin(&g); sprite_commit(&g);
    ASSERT_NULL(eng_sprite3d(g.f.engine,0)); ASSERT_NULL(eng_sprite3d(g.f.engine,g.f.owner_id));
    ASSERT_FALSE(eng_sprite3d_set_size(g.f.engine,g.f.owner_id,(nmo_vector2_t){1,1}));
    ASSERT_NOT_NULL(eng_sprite3d(g.f.engine,g.sprite)); nmo_engine_destroy_object(g.f.engine,g.sprite);
    ASSERT_NULL(eng_sprite3d(g.f.engine,g.sprite)); ASSERT_FALSE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){1,1})); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_set_scale, sprite_sizes_update_drawn_geometry_boxes_and_radius_without_changing_transforms_or_document);
REGISTER_TEST(engine_set_scale, sprite_zero_and_negative_sizes_keep_signed_native_extents);
REGISTER_TEST(engine_set_scale, copied_sprites_keep_runtime_sizes_and_remap_materials_independently);
REGISTER_TEST(engine_set_scale, sprite_initial_values_restore_runtime_and_serialized_sizes_with_their_offset_and_mode);
REGISTER_TEST(engine_set_scale, missing_deleted_and_non_sprite_objects_do_not_create_sprite_state);
TEST_MAIN_END()
