#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "type/nmo_operations.h"

#include <math.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_BILLBOARD NMO_GUID(0x641822b0,0x15aa42d7)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct billboard_fixture {
    engine_fixture_t f;
    nmo_object_id_t target, camera, child, mesh, material, texture, block, out, entry;
} billboard_fixture_t;

static void begin(billboard_fixture_t *g, uint32_t class_id)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->target = fixture_create(&g->f,(nmo_class_id_t)class_id,"Billboard Target"); g->camera = fixture_create(&g->f,NMO_CID_CAMERA,"Camera"); g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    ASSERT_TRUE(g->target != 0 && g->camera != 0 && g->child != 0);
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child); nmo_camera_state_t *camera = fixture_state(&g->f,g->camera);
    target->has_entityndata_chunk = child->has_entityndata_chunk = camera->entity.has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4; target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 10;
    child->parent = nmo_ref_from_id(g->target); child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 14;
    camera->has_cameraonly_chunk = 1; camera->fov = PI_F/2; camera->near_plane = 1; camera->far_plane = 101; camera->projection_type = CK_PERSPECTIVEPROJECTION;
}

static void commit(billboard_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); g->f.engine->render.camera = g->camera;
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,2e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,2e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,2e-4f);
}

static void matrix(const eng_matrix_t *actual, const eng_matrix_t *expected)
{
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { ASSERT_TRUE(isfinite(actual->m[i][j])); ASSERT_FLOAT_EQ(expected->m[i][j],actual->m[i][j],2e-4f); }
}

TEST(engine_billboard, matrix_uses_raw_local_axes_and_preserves_position_without_mutating_entity)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); commit(&g); eng_matrix_t actual, before = g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world;
    eng_entity_billboard_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&(eng_vector_t){0,0,1},&(eng_vector_t){0,1,0},&actual);
    const eng_matrix_t expected = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,10,1}}}; matrix(&actual,&expected);
    eng_entity_billboard_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&(eng_vector_t){1,2,3},&(eng_vector_t){4,5,6},&actual);
    const eng_matrix_t raw = {{{-3,4,-1,0},{6,5,-2,0},{-3,6,-3,0},{3,4,10,1}}}; matrix(&actual,&raw); matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&before); fixture_end(&g.f);
}

TEST(engine_billboard, matrix_follows_normalized_live_camera_axes_and_uses_default_eye_after_deletion)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); commit(&g);
    const eng_matrix_t camera = {{{0,0,-2,0},{0,3,0,0},{4,0,0,0},{7,8,9,1}}}; ASSERT_EQ(NMO_OK,nmo_engine_set_entity_world_matrix(g.f.engine,g.camera,(const float *)camera.m,false));
    eng_matrix_t actual; eng_entity_billboard_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&(eng_vector_t){0,0,1},&(eng_vector_t){0,1,0},&actual);
    const eng_matrix_t expected = {{{0,0,1,0},{0,1,0,0},{-1,0,0,0},{3,4,10,1}}}; matrix(&actual,&expected);
    nmo_engine_destroy_object(g.f.engine,g.camera); eng_render_viewpoint(g.f.engine,&actual); eng_matrix_t identity; eng_matrix_identity(&identity); matrix(&actual,&identity); fixture_end(&g.f);
}

TEST(engine_billboard, perspective_projection_matches_render_context_pixels_depth_and_zero_w)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); commit(&g); eng_vector_t screen;
    eng_render_project(g.f.engine,&(eng_vector_t){2,1,10},&screen); vector(screen,(eng_vector_t){384,208,.909f});
    eng_render_project(g.f.engine,&(eng_vector_t){2,1,-10},&screen); vector(screen,(eng_vector_t){256,272,1.111f});
    eng_render_project(g.f.engine,&(eng_vector_t){2,1,0},&screen); vector(screen,(eng_vector_t){0});
    eng_set_camera_planes(g.f.engine,g.camera,2,102); eng_render_project(g.f.engine,&(eng_vector_t){2,1,10},&screen); ASSERT_FLOAT_EQ(.816f,screen.z,2e-4f); fixture_end(&g.f);
}

TEST(engine_billboard, orthographic_projection_uses_zoom_screen_mode_and_live_camera_transform)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); nmo_camera_state_t *camera = fixture_state(&g.f,g.camera); camera->projection_type = CK_ORTHOGRAPHICPROJECTION; camera->orthographic_zoom = .5f; commit(&g);
    eng_vector_t screen; eng_render_project(g.f.engine,&(eng_vector_t){2,1,10},&screen); vector(screen,(eng_vector_t){640,80,.09f});
    const eng_matrix_t eye = {{{0,0,-2,0},{0,3,0,0},{4,0,0,0},{10,20,30,1}}}; ASSERT_EQ(NMO_OK,nmo_engine_set_entity_world_matrix(g.f.engine,g.camera,(const float *)eye.m,false));
    eng_vector_t world; eng_vector_transform(&world,&eye,&(eng_vector_t){2,1,10}); eng_render_project(g.f.engine,&world,&screen); vector(screen,(eng_vector_t){640,80,.09f});
    g.f.engine->render.screen_mode = 2; const eng_screen_mode_t *mode = &eng_screen_modes[2]; eng_render_project(g.f.engine,&world,&screen); vector(screen,(eng_vector_t){(float)mode->width,(float)mode->height*.5f-(float)mode->width*.25f,.09f}); fixture_end(&g.f);
}

static void texture(billboard_fixture_t *g)
{
    g->material = fixture_create(&g->f,NMO_CID_MATERIAL,"Material"); g->texture = fixture_create(&g->f,NMO_CID_TEXTURE,"Texture"); ASSERT_TRUE(g->material != 0 && g->texture != 0);
    nmo_material_state_t *material = fixture_state(&g->f,g->material); material->has_material_data = 1; material->textures[0] = nmo_ref_from_id(g->texture);
    nmo_texture_state_t *state = fixture_state(&g->f,g->texture); state->slot_count = 1; state->bitmap_kind = CKTEXTURE_BITMAP_RAW;
    nmo_arena_t *arena = nmo_session_get_arena(g->f.session); state->raw_slots = nmo_arena_alloc(arena,sizeof(*state->raw_slots),_Alignof(nmo_texture_raw_slot_t)); ASSERT_NOT_NULL(state->raw_slots); memset(state->raw_slots,0,sizeof(*state->raw_slots));
    nmo_texture_raw_slot_t *slot = state->raw_slots; slot->width = 32; slot->height = 64; slot->bits_per_pixel = 32;
    slot->red_size = slot->green_size = slot->blue_size = 32*64;
    slot->red_data = nmo_arena_alloc(arena,32*64,1); slot->green_data = nmo_arena_alloc(arena,32*64,1); slot->blue_data = nmo_arena_alloc(arena,32*64,1); ASSERT_TRUE(slot->red_data != NULL && slot->green_data != NULL && slot->blue_data != NULL);
    memset(slot->red_data,255,32*64); memset(slot->green_data,255,32*64); memset(slot->blue_data,255,32*64);
}

static void mesh(billboard_fixture_t *g, bool textured)
{
    if (textured) texture(g);
    g->mesh = fixture_create(&g->f,NMO_CID_MESH,"Mesh"); ASSERT_TRUE(g->mesh != 0); nmo_mesh_state_t *state = fixture_state(&g->f,g->mesh); nmo_arena_t *arena = nmo_session_get_arena(g->f.session);
    state->vertex_count = 3; state->face_count = 1; state->vertices = nmo_arena_alloc(arena,3*sizeof(nmo_vertex_t),_Alignof(nmo_vertex_t)); state->faces = nmo_arena_alloc(arena,sizeof(nmo_face_t),_Alignof(nmo_face_t)); state->face_vertex_indices = nmo_arena_alloc(arena,3*sizeof(uint16_t),_Alignof(uint16_t)); ASSERT_TRUE(state->vertices != NULL && state->faces != NULL && state->face_vertex_indices != NULL);
    const nmo_vertex_t vertices[] = {{{-1,-1,0},{0,0,1},{0,1}},{{1,-1,0},{0,0,1},{1,1}},{{1,1,0},{0,0,1},{1,0}}}; memcpy(state->vertices,vertices,sizeof(vertices)); *state->faces = (nmo_face_t){.normal = {0,0,1}};
    for (int i = 0; i < 3; i++) state->face_vertex_indices[i] = (uint16_t)i;
    if (textured) { state->material_group_count = 1; state->has_material_groups = 1; state->material_groups = nmo_arena_alloc(arena,sizeof(nmo_material_group_t),_Alignof(nmo_material_group_t)); ASSERT_NOT_NULL(state->material_groups); *state->material_groups = (nmo_material_group_t){.material = nmo_ref_from_id(g->material)}; }
    nmo_3dentity_state_t *entity = fixture_state(&g->f,g->target); entity->current_mesh = nmo_ref_from_id(g->mesh); entity->mesh_count = 1; entity->mesh_ids = nmo_arena_alloc(arena,sizeof(nmo_ref_t),_Alignof(nmo_ref_t)); ASSERT_NOT_NULL(entity->mesh_ids); entity->mesh_ids[0] = nmo_ref_from_id(g->mesh);
}

static void sprite(billboard_fixture_t *g)
{
    texture(g); nmo_sprite3d_state_t *state = fixture_state(&g->f,g->target); state->has_data = 1; state->mode = VXSPRITE3D_ORIENTABLE; state->half_width = 2; state->half_height = 3; state->material = nmo_ref_from_id(g->material);
    state->offset = (nmo_vector2_t){.5f,-.25f}; state->uv_rect = (nmo_rect_t){.2f,.3f,.8f,.9f};
}

static void graph_begin(billboard_fixture_t *g, uint32_t class_id)
{
    begin(g,class_id); g->f.script_id = fixture_add_script(&g->f,"Billboard Script",&g->f.script_in); fixture_begin_edit(&g->f);
    nmo_status_t status = nmo_script_edit_add_node(g->f.tx,g->f.script_id,GUID_BILLBOARD,"Billboard",&g->block);
    g->out = fixture_block(&g->f,GUID_NOP,"After Billboard"); ASSERT_EQ(NMO_OK,status); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target); ASSERT_EQ(NMO_OK,nmo_script_edit_add_behavior_link(g->f.tx,g->f.script_id,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0,&g->entry)); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void graph_commit(billboard_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_BILLBOARD));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); g->f.engine->render.camera = g->camera;
}

static void trigger(billboard_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

static nmo_object_id_t source(billboard_fixture_t *g, nmo_object_id_t pin)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,pin));
}

static void output_matrix(billboard_fixture_t *g, const eng_matrix_t *expected)
{
    eng_matrix_t actual; read_value(&g->f,BLOCK_POUT(&g->f,g->block,0),&actual,sizeof(actual)); matrix(&actual,expected);
}

TEST(engine_billboard, default_block_applies_native_matrix_moves_children_and_preserves_output_without_scheduling)
{
    billboard_fixture_t g; graph_begin(&g,NMO_CID_3DENTITY); graph_commit(&g); eng_matrix_t sentinel; eng_matrix_identity(&sentinel); sentinel.m[3][0] = -99;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&sentinel,sizeof(sentinel))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    const eng_matrix_t expected = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,10,1}}}; matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&expected); output_matrix(&g,&sentinel);
    vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.child),-1),(eng_vector_t){2,5,9}); ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block)); const nmo_3dentity_state_t *original = fixture_state(&g.f,g.target); ASSERT_FLOAT_EQ(2,original->world_matrix[0],0); fixture_end(&g.f);
}

TEST(engine_billboard, output_only_uses_raw_axes_and_disconnected_defaults_without_moving_entities)
{
    for (int disconnected = 0; disconnected < 2; disconnected++) {
        billboard_fixture_t g; graph_begin(&g,NMO_CID_3DENTITY); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),0);
        if (disconnected) for (int i = 0; i < 4; i++) ASSERT_EQ(NMO_OK,nmo_script_edit_disconnect_parameter(g.f.tx,BLOCK_PIN(&g.f,g.block,i)));
        else { fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&(eng_vector_t){1,2,3},sizeof(eng_vector_t)); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,2),&(eng_vector_t){4,5,6},sizeof(eng_vector_t)); }
        graph_commit(&g); eng_matrix_t before = g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world; ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&before);
        const eng_matrix_t raw = {{{-3,4,-1,0},{6,5,-2,0},{-3,6,-3,0},{3,4,10,1}}}, fallback = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,10,1}}}; output_matrix(&g,disconnected ? &fallback : &raw);
        vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.child),-1),(eng_vector_t){5,7,14}); fixture_end(&g.f);
    }
}

TEST(engine_billboard, mesh_faces_select_uv_or_edge_up_ignore_axis_inputs_and_follow_live_geometry)
{
    for (int kind = 0; kind < 3; kind++) {
        billboard_fixture_t g; graph_begin(&g,NMO_CID_3DENTITY); mesh(&g,kind != 0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),kind == 2 ? 10 : 0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),0);
        fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,3),&(float){0},sizeof(float)); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&(eng_vector_t){5,7,9},sizeof(eng_vector_t)); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        const eng_matrix_t uv = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,10,1}}}, edge = {{{0,-1,0,0},{-1,0,0,0},{0,0,-1,0},{3,4,10,1}}}; output_matrix(&g,kind == 0 ? &edge : &uv);
        if (kind == 1) {
            eng_mesh_copy_t *copy = eng_mesh_copy(g.f.engine,g.mesh); ASSERT_NOT_NULL(copy); copy->vertices[0].position = (nmo_vector_t){0,-1,-1}; copy->vertices[1].position = (nmo_vector_t){0,-1,1}; copy->vertices[2].position = (nmo_vector_t){0,1,1}; copy->geometry_changed = true; trigger(&g);
            const eng_matrix_t changed = {{{0,0,1,0},{0,1,0,0},{-1,0,0,0},{3,4,10,1}}}; output_matrix(&g,&changed);
        }
        fixture_end(&g.f);
    }
}

TEST(engine_billboard, texture_motion_uses_actual_projected_geometry_and_only_applies_when_requested)
{
    for (int is_sprite = 0; is_sprite < 2; is_sprite++) for (int apply = 0; apply < 2; apply++) {
        billboard_fixture_t g; graph_begin(&g,is_sprite ? NMO_CID_SPRITE3D : NMO_CID_3DENTITY); if (is_sprite) sprite(&g); else { mesh(&g,true); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0); }
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),apply); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,5),0); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,3),&(float){.5f},sizeof(float)); graph_commit(&g);
        eng_matrix_t before = g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world; ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float z = is_sprite ? (apply ? 60.0f : 180.0f) : (apply ? 20.0f : 60.0f);
        eng_matrix_t expected = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,z,1}}};
        if (apply) matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&expected); else { matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&before); output_matrix(&g,&expected); }
        if (is_sprite) { const eng_sprite3d_t *s = eng_sprite3d(g.f.engine,g.target); ASSERT_FLOAT_EQ(4,s->max.x-s->min.x,2e-4f); ASSERT_FLOAT_EQ(6,s->max.y-s->min.y,2e-4f); } fixture_end(&g.f);
    }
}

TEST(engine_billboard, sprite_texture_scaling_preserves_offset_and_resizes_even_in_output_only_mode)
{
    for (int apply = 0; apply < 2; apply++) {
        billboard_fixture_t g; graph_begin(&g,NMO_CID_SPRITE3D); sprite(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),apply); graph_commit(&g); eng_matrix_t before = g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world; ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        const eng_sprite3d_t *s = eng_sprite3d(g.f.engine,g.target); float divisor = apply ? 3.0f : 9.0f; ASSERT_FLOAT_EQ(4/divisor,s->max.x-s->min.x,2e-4f); ASSERT_FLOAT_EQ(6/divisor,s->max.y-s->min.y,2e-4f); ASSERT_FLOAT_EQ(.5f,s->offset.x,0); ASSERT_FLOAT_EQ(-.25f,s->offset.y,0); ASSERT_FLOAT_EQ(.2f,s->uv_rect.left,0);
        if (!apply) { matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&before); } vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.target),-1),(eng_vector_t){3,4,10}); fixture_end(&g.f);
    }
}

TEST(engine_billboard, mesh_scale_instead_of_move_keeps_the_native_noop_for_texture_scaling)
{
    billboard_fixture_t g; graph_begin(&g,NMO_CID_3DENTITY); mesh(&g,true); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,3),&(float){3},sizeof(float)); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    const eng_matrix_t expected = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,10,1}}}; matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&expected); nmo_engine_mesh_t desc; ASSERT_TRUE(nmo_engine_mesh(g.f.engine,g.mesh,&desc)); ASSERT_FLOAT_EQ(-1,desc.vertices[0].position.x,0); fixture_end(&g.f);
}

static void count_operations(const nmo_engine_event_t *event, void *data)
{
    if (event->kind == NMO_ENGINE_EVENT_OPERATION && event->result == NMO_OK) (*(size_t *)data)++;
}

TEST(engine_billboard, face_mode_and_invalid_targets_skip_lazy_axis_operations)
{
    billboard_fixture_t g; graph_begin(&g,NMO_CID_3DENTITY); mesh(&g,false); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),0);
    nmo_object_id_t a = 0, b = 0, operation = 0;
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_VECTOR,"A",&a)); ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_VECTOR,"B",&b));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,a,"0,0,1")); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,b,"0,0,0")); ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g.f.tx,g.f.script_id,NMO_OP_GUID_ADD,a,b,0,&operation));
    const nmo_parameteroperation_state_t *state = fixture_state(&g.f,operation); ASSERT_NOT_NULL(state); ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,nmo_parameteroperation_out_id(state),BLOCK_PIN(&g.f,g.block,1)));
    nmo_object_id_t face = source(&g,BLOCK_PIN(&g.f,g.block,0)), target = source(&g,fixture_target(&g.f,g.block)); graph_commit(&g); size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0u,operations); const int32_t axes = -1; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,face,&axes,sizeof(axes))); trigger(&g); ASSERT_EQ(1u,operations);
    const nmo_object_id_t absent = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,target,&absent,sizeof(absent))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,operations); fixture_end(&g.f);
}

TEST(engine_billboard, invalid_targets_consume_input_activate_output_and_preserve_matrix_output)
{
    for (int kind = 0; kind < 3; kind++) {
        billboard_fixture_t g; graph_begin(&g,NMO_CID_3DENTITY); nmo_object_id_t wrong = fixture_create(&g.f,NMO_CID_GROUP,"Wrong"), target = source(&g,fixture_target(&g.f,g.block)); graph_commit(&g);
        nmo_object_id_t bad = kind == 0 ? 0 : kind == 1 ? g.target : wrong; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,target,&bad,sizeof(bad))); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        eng_matrix_t sentinel; eng_matrix_identity(&sentinel); sentinel.m[3][0] = -99; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&sentinel,sizeof(sentinel)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); output_matrix(&g,&sentinel);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

static void rendered_matrix(nmo_engine_t *engine, nmo_object_id_t object, const eng_matrix_t *expected)
{
    eng_show(engine,object,1); nmo_engine_frame_t frame; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(engine,&frame)); bool found = false;
    for (int32_t i = 0; i < frame.draw_count; i++) if (frame.draws[i].object == object) {
        const nmo_engine_draw_t *draw = &frame.draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_SPRITE3D) {
            const eng_sprite3d_t *s = eng_sprite3d(engine,object); ASSERT_NOT_NULL(s); ASSERT_EQ(6,draw->vertex_count);
            const eng_vector_t corners[] = {{s->min.x,s->min.y,0},{s->min.x,s->max.y,0},{s->max.x,s->max.y,0},{s->max.x,s->min.y,0}}; const int order[] = {0,1,2,0,2,3};
            for (int k = 0; k < 6; k++) { eng_vector_t position; eng_vector_transform(&position,expected,&corners[order[k]]); const nmo_engine_vertex_t *v = &frame.vertices[draw->first_vertex+k]; vector((eng_vector_t){v->x,v->y,v->z},position); }
        } else { eng_matrix_t actual; memcpy(actual.m,draw->world,sizeof(actual.m)); matrix(&actual,expected); }
        found = true;
    }
    ASSERT_TRUE(found);
}

TEST(engine_billboard, output_matrix_links_apply_the_corrected_pose_and_render_it_in_the_same_frame)
{
    billboard_fixture_t g; graph_begin(&g,NMO_CID_3DENTITY); mesh(&g,true); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,5),0); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,3),&(float){.5f},sizeof(float));
    nmo_object_id_t setter = fixture_block(&g.f,NMO_GUID(0xaa4aa6f0,0xddefdef4),"Apply Billboard Matrix"); ASSERT_TRUE(setter != 0); fixture_input_int(&g.f,fixture_target(&g.f,setter),(int32_t)g.target); fixture_input_int(&g.f,BLOCK_PIN(&g.f,setter,1),1);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,g.block,0),BLOCK_PIN(&g.f,setter,0))); fixture_link(&g.f,BLOCK_OUT(&g.f,g.out,0),BLOCK_IN(&g.f,setter,0),0); graph_commit(&g);
    const float distances[] = {60,20,20};
    for (size_t i = 0; i < COUNT(distances); i++) {
        if (i == 0) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); else trigger(&g);
        const eng_matrix_t expected = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,distances[i],1}}}; output_matrix(&g,&expected); matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&expected);
        vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.child),-1),(eng_vector_t){2,5,distances[i]-1}); rendered_matrix(g.f.engine,g.target,&expected);
    }
    ASSERT_EQ(3u,runs(&g.f,setter)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(3u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_billboard, camera_switches_viewport_changes_and_live_materials_update_sprite_pixel_sizing)
{
    billboard_fixture_t g; graph_begin(&g,NMO_CID_SPRITE3D); sprite(&g); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,3),&(float){1},sizeof(float)); nmo_object_id_t factor = source(&g,BLOCK_PIN(&g.f,g.block,3));
    nmo_object_id_t second = fixture_create(&g.f,NMO_CID_CAMERA,"Second Camera"); ASSERT_TRUE(second != 0); nmo_camera_state_t *camera = fixture_state(&g.f,second); camera->has_cameraonly_chunk = camera->entity.has_entityndata_chunk = 1; camera->projection_type = CK_ORTHOGRAPHICPROJECTION; camera->orthographic_zoom = .5f; camera->near_plane = 1; camera->far_plane = 100;
    const eng_matrix_t eye = {{{0,0,-1,0},{0,1,0,0},{1,0,0,0},{-7,4,10,1}}}; /* in front of the sprite, which is culled otherwise */ memcpy(camera->entity.world_matrix,eye.m,sizeof(eye.m));
    nmo_object_id_t activate = fixture_block(&g.f,NMO_GUID(0x368f0ab1,0x2d8957e4),"Use Second Camera"); ASSERT_TRUE(activate != 0); fixture_input_int(&g.f,fixture_target(&g.f,activate),(int32_t)second); fixture_link(&g.f,BLOCK_OUT(&g.f,g.out,0),BLOCK_IN(&g.f,activate,0),0); graph_commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const eng_sprite3d_t *s = eng_sprite3d(g.f.engine,g.target); ASSERT_FLOAT_EQ(2,s->max.y-s->min.y,2e-4f); ASSERT_EQ(second,g.f.engine->render.camera);
    trigger(&g); s = eng_sprite3d(g.f.engine,g.target); ASSERT_FLOAT_EQ(.4f,s->max.y-s->min.y,2e-4f); const eng_matrix_t expected = {{{0,0,1,0},{0,1,0,0},{-1,0,0,0},{3,4,10,1}}}; rendered_matrix(g.f.engine,g.target,&expected);
    g.f.engine->render.screen_mode = 2; const float two = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,factor,&two,sizeof(two))); trigger(&g); s = eng_sprite3d(g.f.engine,g.target); ASSERT_FLOAT_EQ(.64f,s->max.y-s->min.y,2e-4f);
    eng_material_t *material = eng_material(g.f.engine,g.material); ASSERT_NOT_NULL(material); material->desc.texture = 0; const float four = 4; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,factor,&four,sizeof(four))); trigger(&g); s = eng_sprite3d(g.f.engine,g.target); ASSERT_FLOAT_EQ(.64f,s->max.y-s->min.y,2e-4f);
    ASSERT_EQ(4u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(4u,runs(&g.f,g.block)); fixture_end(&g.f);
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t runtime_source(nmo_engine_t *engine, nmo_object_id_t block, int pos)
{
    nmo_object_t *object = eng_find_object(engine,block); const nmo_behavior_state_t *behavior = object != NULL ? nmo_object_get_state(object) : NULL; const eng_module_t *module = eng_module_of(engine,block,NULL);
    if (behavior == NULL || module == NULL) return 0;
    nmo_object_id_t pin = pos < 0 ? nmo_behavior_target_parameter_id(behavior) : nmo_behavior_ref_array_get_id(&behavior->in_parameters,(size_t)pos); object = eng_find_object(engine,eng_module_id(module,pin)); const nmo_parameterin_state_t *input = object != NULL ? nmo_object_get_state(object) : NULL;
    return input != NULL ? eng_module_id(module,nmo_parameterin_source_id(input)) : 0;
}

TEST(engine_billboard, loaded_and_copied_graphs_remap_cameras_sprites_and_textures_and_keep_runtime_sizes_independent)
{
    billboard_fixture_t g; graph_begin(&g,NMO_CID_SPRITE3D); sprite(&g); nmo_object_id_t activate = fixture_block(&g.f,NMO_GUID(0x368f0ab1,0x2d8957e4),"Use Billboard Camera"); ASSERT_TRUE(activate != 0); fixture_input_int(&g.f,fixture_target(&g.f,activate),(int32_t)g.camera);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,g.entry)); fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,activate,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,activate,0),BLOCK_IN(&g.f,g.block,0),0);
    char text[32]; snprintf(text,sizeof(text),"%u",g.target); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,source(&g,fixture_target(&g.f,g.block)),text)); snprintf(text,sizeof(text),"%u",g.camera); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,source(&g,fixture_target(&g.f,activate)),text)); fixture_commit_bare(&g.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *file_path = NMO_TEST_SCRATCH_FILE("engine_billboard.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,file_path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)file_path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"billboard.cmo",true,&file)); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t block = nmo_engine_object_named(host.engine,"Billboard",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0); const eng_module_t *module = eng_module_of(host.engine,block,NULL);
        const char *names[] = {"Owner","Billboard Target","Child","Camera","Material","Texture","Billboard Script","Billboard","After Billboard","Use Billboard Camera"}; nmo_object_id_t ids[14] = {0}, copies[14] = {0};
        for (size_t i = 0; i < COUNT(names); i++) { nmo_object_t *object = nmo_object_repository_find_by_name(module->repo,names[i]); ASSERT_NOT_NULL(object); ids[i] = eng_module_id(module,nmo_object_get_id(object)); ASSERT_TRUE(ids[i] != 0); }
        ids[10] = runtime_source(host.engine,ids[7],-1); ids[11] = runtime_source(host.engine,ids[7],4); ids[12] = runtime_source(host.engine,ids[7],3); ids[13] = runtime_source(host.engine,ids[9],-1); for (size_t i = 10; i < COUNT(ids); i++) ASSERT_TRUE(ids[i] != 0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); const eng_sprite3d_t *s = eng_sprite3d(host.engine,ids[1]); ASSERT_NOT_NULL(s); ASSERT_FLOAT_EQ(2,s->max.y-s->min.y,2e-4f); ASSERT_EQ(ids[3],host.engine->render.camera);
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies)); for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
        s = eng_sprite3d(host.engine,copies[1]); ASSERT_NOT_NULL(s); ASSERT_EQ(copies[4],s->material); ASSERT_EQ(copies[5],eng_material(host.engine,copies[4])->desc.texture); ASSERT_FLOAT_EQ(2,s->max.y-s->min.y,2e-4f);
        nmo_object_id_t reference = 0; read_value(&host,copies[10],&reference,sizeof(reference)); ASSERT_EQ(copies[1],reference); read_value(&host,copies[13],&reference,sizeof(reference)); ASSERT_EQ(copies[3],reference);
        const float factor = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[12],&factor,sizeof(factor))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        s = eng_sprite3d(host.engine,copies[1]); ASSERT_FLOAT_EQ(4,s->max.y-s->min.y,2e-4f); s = eng_sprite3d(host.engine,ids[1]); ASSERT_FLOAT_EQ(2,s->max.y-s->min.y,2e-4f); ASSERT_EQ(copies[3],host.engine->render.camera);
        nmo_engine_destroy_object(host.engine,ids[1]); for (size_t i = 2; i < 6; i++) nmo_engine_destroy_object(host.engine,ids[i]); nmo_engine_destroy_object(host.engine,ids[0]);
        eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[3]),&(eng_vector_t){0,0,-10},-1,false); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[6],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); s = eng_sprite3d(host.engine,copies[1]); ASSERT_FLOAT_EQ(8,s->max.y-s->min.y,2e-4f);
        const eng_matrix_t expected = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,10,1}}}; rendered_matrix(host.engine,copies[1],&expected); ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[2])); ASSERT_EQ(2u,runs(&host,copies[8])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); ASSERT_EQ(2u,runs(&host,copies[7])); fixture_end(&host);
    }
    const nmo_sprite3d_state_t *original = fixture_state(&g.f,g.target); ASSERT_FLOAT_EQ(3,original->half_height,0); fixture_end(&g.f); remove(file_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_billboard, matrix_uses_raw_local_axes_and_preserves_position_without_mutating_entity);
REGISTER_TEST(engine_billboard, matrix_follows_normalized_live_camera_axes_and_uses_default_eye_after_deletion);
REGISTER_TEST(engine_billboard, perspective_projection_matches_render_context_pixels_depth_and_zero_w);
REGISTER_TEST(engine_billboard, orthographic_projection_uses_zoom_screen_mode_and_live_camera_transform);
REGISTER_TEST(engine_billboard, default_block_applies_native_matrix_moves_children_and_preserves_output_without_scheduling);
REGISTER_TEST(engine_billboard, output_only_uses_raw_axes_and_disconnected_defaults_without_moving_entities);
REGISTER_TEST(engine_billboard, mesh_faces_select_uv_or_edge_up_ignore_axis_inputs_and_follow_live_geometry);
REGISTER_TEST(engine_billboard, texture_motion_uses_actual_projected_geometry_and_only_applies_when_requested);
REGISTER_TEST(engine_billboard, sprite_texture_scaling_preserves_offset_and_resizes_even_in_output_only_mode);
REGISTER_TEST(engine_billboard, mesh_scale_instead_of_move_keeps_the_native_noop_for_texture_scaling);
REGISTER_TEST(engine_billboard, face_mode_and_invalid_targets_skip_lazy_axis_operations);
REGISTER_TEST(engine_billboard, invalid_targets_consume_input_activate_output_and_preserve_matrix_output);
REGISTER_TEST(engine_billboard, output_matrix_links_apply_the_corrected_pose_and_render_it_in_the_same_frame);
REGISTER_TEST(engine_billboard, camera_switches_viewport_changes_and_live_materials_update_sprite_pixel_sizing);
REGISTER_TEST(engine_billboard, loaded_and_copied_graphs_remap_cameras_sprites_and_textures_and_keep_runtime_sizes_independent);
TEST_MAIN_END()
