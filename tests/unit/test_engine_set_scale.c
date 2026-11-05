#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/nmo_serialize_context.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_SET_SCALE NMO_GUID(0x7a7a7a7a,0x7a7a7a7a)
#define GUID_SCALE NMO_GUID(0x41236987,0xa54a87a6)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

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

TEST(engine_set_scale, sprites_stop_drawing_after_their_cached_material_is_deleted)
{
    sprite_fixture_t g; sprite_begin(&g); sprite_commit(&g);
    ASSERT_TRUE(eng_sprite3d_set_size(g.f.engine,g.sprite,(nmo_vector2_t){8,2})); quad(g.f.engine,g.sprite,-1,.25f,15,6.25f);
    nmo_engine_destroy_object(g.f.engine,g.material);
    nmo_engine_frame_t frame; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&frame));
    for (int32_t i = 0; i < frame.draw_count; i++) ASSERT_TRUE(frame.draws[i].object != g.sprite);
    sprite_size(g.f.engine,g.sprite,8,2); fixture_end(&g.f);
}

typedef struct scale_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, grandchild, block, out;
} scale_fixture_t;

static nmo_object_id_t input_source(engine_fixture_t *f, nmo_object_id_t pin)
{
    return nmo_parameterin_source_id(fixture_state(f,pin));
}

static void begin(scale_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Scaled Entity");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Grandchild");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *target = fixture_state(&g->f,g->target);
    nmo_3dentity_state_t *child = fixture_state(&g->f,g->child), *grand = fixture_state(&g->f,g->grandchild);
    ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(grand);
    parent->has_entityndata_chunk = target->has_entityndata_chunk = child->has_entityndata_chunk = grand->has_entityndata_chunk = 1;
    parent->world_matrix[0] = 2; parent->world_matrix[5] = 3; parent->world_matrix[10] = 4;
    parent->world_matrix[12] = 10; parent->world_matrix[13] = 20; parent->world_matrix[14] = 30;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    grand->parent = nmo_ref_from_id(g->child); grand->world_matrix[12] = 6; grand->world_matrix[13] = 9; grand->world_matrix[14] = 12;
    g->f.script_id = fixture_add_script(&g->f,"Scale Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_SET_SCALE,"Set Scale"); g->out = fixture_block(&g->f,GUID_NOP,"After Scale"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(scale_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_SET_SCALE));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void trigger(engine_fixture_t *f)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f->engine,f->script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f->engine,0));
}

static void position(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t expected)
{
    vector(eng_entity_position(engine,eng_entity_index(engine,id),-1),expected);
}

static void basis(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float m[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,m));
    vector((eng_vector_t){m[0],m[1],m[2]},x); vector((eng_vector_t){m[4],m[5],m[6]},y); vector((eng_vector_t){m[8],m[9],m[10]},z);
}

TEST(engine_set_scale, defaults_set_absolute_scale_and_move_descendants_once_without_scheduling)
{
    scale_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    basis(g.f.engine,g.target,(eng_vector_t){2,0,0},(eng_vector_t){0,2,0},(eng_vector_t){0,0,2}); position(g.f.engine,g.target,(eng_vector_t){3,4,5});
    position(g.f.engine,g.child,(eng_vector_t){5,6,7}); position(g.f.engine,g.grandchild,(eng_vector_t){6,4+10.0f/3,8.5f});
    trigger(&g.f); position(g.f.engine,g.child,(eng_vector_t){5,6,7}); ASSERT_EQ(2u,runs(&g.f,g.out));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_set_scale, hierarchy_can_keep_children_in_place_while_changing_their_parent_scale)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        scale_fixture_t g; begin(&g); const eng_vector_t value = {4,6,8};
        fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&value,sizeof(value)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),hierarchy); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(g.f.engine,g.target,(eng_vector_t){4,0,0},(eng_vector_t){0,6,0},(eng_vector_t){0,0,8});
        position(g.f.engine,g.child,hierarchy ? (eng_vector_t){7,10,13} : (eng_vector_t){5,7,9});
        position(g.f.engine,g.grandchild,hierarchy ? (eng_vector_t){9,14,19} : (eng_vector_t){6,9,12});
        trigger(&g.f); position(g.f.engine,g.child,hierarchy ? (eng_vector_t){7,10,13} : (eng_vector_t){5,7,9}); fixture_end(&g.f);
    }
}

TEST(engine_set_scale, local_axes_under_nonuniform_parents_keep_the_world_pivot)
{
    scale_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
    target->parent = nmo_ref_from_id(g.parent); target->world_matrix[0] = 4; target->world_matrix[5] = 9; target->world_matrix[10] = 16;
    const eng_vector_t value = {3,2,1}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&value,sizeof(value)); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(g.f.engine,g.target,(eng_vector_t){6,0,0},(eng_vector_t){0,6,0},(eng_vector_t){0,0,4});
    vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),true),value); position(g.f.engine,g.target,(eng_vector_t){3,4,5});
    position(g.f.engine,g.child,(eng_vector_t){6,6,6}); position(g.f.engine,g.grandchild,(eng_vector_t){7.5f,4+10.0f/3,6.75f}); fixture_end(&g.f);
}

TEST(engine_set_scale, sheared_axes_keep_their_direction_and_extreme_finite_axes_are_normalized)
{
    for (int kind = 0; kind < 3; kind++) {
        scale_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
        if (kind == 0) { target->world_matrix[1] = 1; target->world_matrix[4] = .5f; }
        else target->world_matrix[0] = target->world_matrix[5] = target->world_matrix[10] = kind == 1 ? 1e-35f : 1e35f;
        const eng_vector_t value = {4,6,8}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&value,sizeof(value)); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (kind == 0) basis(g.f.engine,g.target,(eng_vector_t){8/sqrtf(5),4/sqrtf(5),0},(eng_vector_t){3/sqrtf(9.25f),18/sqrtf(9.25f),0},(eng_vector_t){0,0,8});
        else basis(g.f.engine,g.target,(eng_vector_t){4,0,0},(eng_vector_t){0,6,0},(eng_vector_t){0,0,8});
        fixture_end(&g.f);
    }
}

TEST(engine_set_scale, zero_requests_use_native_epsilon_and_negative_requests_reverse_the_current_axis_each_time)
{
    scale_fixture_t g; begin(&g); const eng_vector_t value = {-4,0,8}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&value,sizeof(value)); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(g.f.engine,g.target,(eng_vector_t){-4,0,0},(eng_vector_t){0,1e-6f,0},(eng_vector_t){0,0,8});
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix)); ASSERT_FLOAT_EQ(1e-6f,matrix[5],0);
    vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),true),(eng_vector_t){4,1e-6f,8});
    trigger(&g.f); basis(g.f.engine,g.target,(eng_vector_t){4,0,0},(eng_vector_t){0,1e-6f,0},(eng_vector_t){0,0,8}); fixture_end(&g.f);
}

TEST(engine_set_scale, disconnected_vectors_use_unit_scale_and_collapsed_axes_stay_collapsed)
{
    for (int collapsed = 0; collapsed < 2; collapsed++) {
        scale_fixture_t g; begin(&g);
        if (collapsed) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[0] = 0;
        ASSERT_EQ(NMO_OK,nmo_script_edit_disconnect_parameter(g.f.tx,BLOCK_PIN(&g.f,g.block,0))); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(g.f.engine,g.target,(eng_vector_t){collapsed ? 0 : 1,0,0},(eng_vector_t){0,1,0},(eng_vector_t){0,0,1}); fixture_end(&g.f);
    }
}

TEST(engine_set_scale, sprites_use_dimensions_for_both_absolute_blocks_and_relative_scale_ignoring_z_and_hierarchy)
{
    for (int kind = 0; kind < 3; kind++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        sprite_fixture_t g; sprite_begin(&g);
        g.f.script_id = fixture_add_script(&g.f,"Sprite Scale Script",&g.f.script_in); fixture_begin_edit(&g.f);
        nmo_object_id_t block = fixture_block(&g.f,kind == 0 ? GUID_SET_SCALE : GUID_SCALE,"Scale Sprite"); ASSERT_TRUE(block != 0);
        fixture_input_int(&g.f,fixture_target(&g.f,block),(int32_t)g.sprite);
        const eng_vector_t value = kind < 2 ? (eng_vector_t){8,2,0} : (eng_vector_t){2,.5f,-1000};
        fixture_input_value(&g.f,BLOCK_PIN(&g.f,block,0),&value,sizeof(value)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,block,1),hierarchy);
        if (kind != 0) fixture_input_int(&g.f,BLOCK_PIN(&g.f,block,2),kind == 1);
        fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,block,0),0); fixture_commit(&g.f,NULL);
        ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); eng_show(g.f.engine,g.sprite,1);
        float world[16], child[16], after[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.sprite,world)); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.child,child));
        for (int activation = 1; activation <= 2; activation++) {
            trigger(&g.f); float width = kind < 2 ? 8 : 4*powf(2,(float)activation), height = kind < 2 ? 2 : 6*powf(.5f,(float)activation);
            sprite_size(g.f.engine,g.sprite,width,height); quad(g.f.engine,g.sprite,3-width*.5f,4-height*1.875f,3+width*1.5f,4+height*1.125f);
            ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.sprite,after)); ASSERT_EQ(0,memcmp(world,after,sizeof(after)));
            ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.child,after)); ASSERT_EQ(0,memcmp(child,after,sizeof(after)));
        }
        const eng_vector_t changed = {0,-4,99}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,input_source(&g.f,BLOCK_PIN(&g.f,block,0)),&changed,sizeof(changed)));
        if (kind != 0) { const int32_t yes = 1; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,input_source(&g.f,BLOCK_PIN(&g.f,block,2)),&yes,sizeof(yes))); }
        trigger(&g.f); sprite_size(g.f.engine,g.sprite,0,-4); quad(g.f.engine,g.sprite,3,11.5f,3,-.5f);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(3u,runs(&g.f,block)); fixture_end(&g.f);
    }
}

TEST(engine_set_scale, missing_deleted_and_nonentity_targets_leave_input_active_before_owner_error)
{
    for (int kind = 0; kind < 3; kind++) {
        scale_fixture_t g; begin(&g); if (kind != 1) fixture_input_int(&g.f,fixture_target(&g.f,g.block),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_set_scale, sprite_sizes_update_drawn_geometry_boxes_and_radius_without_changing_transforms_or_document);
REGISTER_TEST(engine_set_scale, sprite_zero_and_negative_sizes_keep_signed_native_extents);
REGISTER_TEST(engine_set_scale, copied_sprites_keep_runtime_sizes_and_remap_materials_independently);
REGISTER_TEST(engine_set_scale, sprite_initial_values_restore_runtime_and_serialized_sizes_with_their_offset_and_mode);
REGISTER_TEST(engine_set_scale, missing_deleted_and_non_sprite_objects_do_not_create_sprite_state);
REGISTER_TEST(engine_set_scale, sprites_stop_drawing_after_their_cached_material_is_deleted);
REGISTER_TEST(engine_set_scale, defaults_set_absolute_scale_and_move_descendants_once_without_scheduling);
REGISTER_TEST(engine_set_scale, hierarchy_can_keep_children_in_place_while_changing_their_parent_scale);
REGISTER_TEST(engine_set_scale, local_axes_under_nonuniform_parents_keep_the_world_pivot);
REGISTER_TEST(engine_set_scale, sheared_axes_keep_their_direction_and_extreme_finite_axes_are_normalized);
REGISTER_TEST(engine_set_scale, zero_requests_use_native_epsilon_and_negative_requests_reverse_the_current_axis_each_time);
REGISTER_TEST(engine_set_scale, disconnected_vectors_use_unit_scale_and_collapsed_axes_stay_collapsed);
REGISTER_TEST(engine_set_scale, sprites_use_dimensions_for_both_absolute_blocks_and_relative_scale_ignoring_z_and_hierarchy);
REGISTER_TEST(engine_set_scale, missing_deleted_and_nonentity_targets_leave_input_active_before_owner_error);
TEST_MAIN_END()
