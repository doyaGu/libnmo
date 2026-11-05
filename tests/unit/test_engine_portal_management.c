#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_PORTAL_MANAGEMENT NMO_GUID(0x55041717,0x30817370)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct portal_fixture {
    engine_fixture_t f;
    nmo_object_id_t places[4], geometry[4], camera, portal[2], block, out;
} portal_fixture_t;

static nmo_object_id_t box(portal_fixture_t *g, const char *name, nmo_object_id_t parent,
    eng_vector_t min, eng_vector_t max)
{
    nmo_object_id_t id = fixture_create(&g->f,NMO_CID_3DENTITY,name); char mesh_name[80]; snprintf(mesh_name,sizeof(mesh_name),"%s Mesh",name);
    nmo_object_id_t mesh = fixture_create(&g->f,NMO_CID_MESH,mesh_name); if (id == 0 || mesh == 0) return 0;
    nmo_3dentity_state_t *e = fixture_state(&g->f,id); e->has_entityndata_chunk = e->has_mesh_chunk = 1; e->parent = nmo_ref_from_id(parent); e->current_mesh = nmo_ref_from_id(mesh);
    nmo_mesh_state_t *m = fixture_state(&g->f,mesh); nmo_arena_t *arena = nmo_session_get_arena(g->f.session);
    m->vertex_count = 8; m->vertices = nmo_arena_alloc(arena,8*sizeof(*m->vertices),_Alignof(nmo_vertex_t)); m->vertex_colors = nmo_arena_alloc(arena,8*sizeof(uint32_t),_Alignof(uint32_t)); m->vertex_specular = nmo_arena_alloc(arena,8*sizeof(uint32_t),_Alignof(uint32_t));
    m->face_count = 2; m->faces = nmo_arena_alloc(arena,2*sizeof(*m->faces),_Alignof(nmo_face_t)); m->face_vertex_indices = nmo_arena_alloc(arena,6*sizeof(uint16_t),_Alignof(uint16_t));
    if (m->vertices == NULL || m->vertex_colors == NULL || m->vertex_specular == NULL || m->faces == NULL || m->face_vertex_indices == NULL) return 0;
    memset(m->vertices,0,8*sizeof(*m->vertices)); memset(m->faces,0,2*sizeof(*m->faces));
    for (int i = 0; i < 8; i++) { m->vertices[i].position = (nmo_vector_t){i & 4 ? max.x : min.x,i & 2 ? max.y : min.y,i & 1 ? max.z : min.z}; m->vertex_colors[i] = 0xffffffffu; m->vertex_specular[i] = 0; }
    const uint16_t indices[] = {0,2,4,2,6,4}; memcpy(m->face_vertex_indices,indices,sizeof(indices)); nmo_mesh_update_bounding_volumes(m); return id;
}

static void begin(portal_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    const char *names[] = {"Place A","Place B","Place C","Unconnected Place"};
    for (size_t i = 0; i < COUNT(names); i++) {
        g->places[i] = fixture_create(&g->f,NMO_CID_PLACE,names[i]); ASSERT_TRUE(g->places[i] != 0);
        ((nmo_3dentity_state_t *)fixture_state(&g->f,g->places[i]))->has_entityndata_chunk = 1;
        float low = i == 0 ? -2 : (float)i*10-5, high = (float)i*10+5;
        g->geometry[i] = box(g,names[i],g->places[i],(eng_vector_t){-4,-4,low},(eng_vector_t){4,4,high}); ASSERT_TRUE(g->geometry[i] != 0);
    }
    g->camera = fixture_create(&g->f,NMO_CID_CAMERA,"Portal Camera"); ASSERT_TRUE(g->camera != 0); nmo_camera_state_t *camera = fixture_state(&g->f,g->camera);
    camera->entity.has_entityndata_chunk = camera->has_cameraonly_chunk = 1; camera->fov = 1.5707963267948966f; camera->aspect_width = camera->aspect_height = 1; camera->near_plane = 1; camera->far_plane = 100;
    for (size_t i = 0; i < COUNT(g->portal); i++) {
        g->portal[i] = fixture_create(&g->f,NMO_CID_3DENTITY,i == 0 ? "Portal AB" : "Portal BC"); ASSERT_TRUE(g->portal[i] != 0); nmo_3dentity_state_t *e = fixture_state(&g->f,g->portal[i]); e->has_entityndata_chunk = 1; e->entity_flags |= CK_3DENTITY_PORTAL; e->world_matrix[10] = .1f; e->world_matrix[14] = (float)i*10+5;
    }
}

static void commit(portal_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); g->f.engine->render.camera = g->camera;
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_FLOAT_EQ(expected.x,actual.x,2e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,2e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,2e-4f);
}

static void rect(nmo_rect_t actual, nmo_rect_t expected)
{
    ASSERT_FLOAT_EQ(expected.left,actual.left,2e-4f); ASSERT_FLOAT_EQ(expected.top,actual.top,2e-4f); ASSERT_FLOAT_EQ(expected.right,actual.right,2e-4f); ASSERT_FLOAT_EQ(expected.bottom,actual.bottom,2e-4f);
}

TEST(engine_portal_management, hierarchy_bounds_exclude_empty_parents_and_follow_live_descendants)
{
    portal_fixture_t g; begin(&g); commit(&g); eng_vector_t low, high;
    ASSERT_TRUE(eng_entity_hierarchical_box(g.f.engine,eng_entity_index(g.f.engine,g.places[1]),&low,&high)); vector(low,(eng_vector_t){-4,-4,5}); vector(high,(eng_vector_t){4,4,15});
    eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.places[1]),&(eng_vector_t){20,30,40},-1,false);
    ASSERT_TRUE(eng_entity_hierarchical_box(g.f.engine,eng_entity_index(g.f.engine,g.places[1]),&low,&high)); vector(low,(eng_vector_t){16,26,45}); vector(high,(eng_vector_t){24,34,55});
    ASSERT_FALSE(eng_entity_hierarchical_box(g.f.engine,eng_entity_index(g.f.engine,g.portal[0]),&low,&high)); nmo_engine_destroy_object(g.f.engine,g.geometry[1]); ASSERT_FALSE(eng_entity_hierarchical_box(g.f.engine,eng_entity_index(g.f.engine,g.places[1]),&low,&high)); fixture_end(&g.f);
}

TEST(engine_portal_management, camera_projection_uses_native_aspect_near_origin_and_runtime_clipping_planes)
{
    portal_fixture_t g; begin(&g); nmo_camera_state_t *camera = fixture_state(&g.f,g.camera); camera->aspect_width = 2; camera->entity.world_matrix[12] = 10; camera->entity.world_matrix[14] = 20; commit(&g);
    eng_matrix_t projection; eng_vector_t origin; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,&origin)); vector(origin,(eng_vector_t){10,0,21}); nmo_rect_t extents;
    ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){9,-1,30},&(eng_vector_t){11,1,30},&extents)); rect(extents,(nmo_rect_t){.45f,.4f,.55f,.6f});
    eng_set_camera_planes(g.f.engine,g.camera,3,50); ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,&origin)); vector(origin,(eng_vector_t){10,0,23}); ASSERT_FALSE(eng_project_box(&projection,&(eng_vector_t){9,-1,22},&(eng_vector_t){11,1,22},&extents)); ASSERT_FALSE(eng_camera_view_projection(g.f.engine,g.places[0],&projection,NULL)); fixture_end(&g.f);
}

TEST(engine_portal_management, orthographic_boxes_use_zoom_and_transformed_camera_axes)
{
    portal_fixture_t g; begin(&g); nmo_camera_state_t *camera = fixture_state(&g.f,g.camera); camera->projection_type = CK_ORTHOGRAPHICPROJECTION; camera->orthographic_zoom = .5f;
    eng_matrix_t pose; eng_matrix_from_euler(&pose,0,1.5707963267948966f,0); memcpy(camera->entity.world_matrix,pose.m,sizeof(pose)); commit(&g); eng_matrix_t projection; eng_vector_t origin;
    ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,&origin)); vector(origin,(eng_vector_t){1,0,0}); nmo_rect_t extents; ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){5,-1,-1},&(eng_vector_t){6,1,1},&extents)); rect(extents,(nmo_rect_t){.25f,.25f,.75f,.75f}); fixture_end(&g.f);
}

TEST(engine_portal_management, projected_boxes_preserve_native_near_plane_expansion_and_reject_separated_frusta)
{
    portal_fixture_t g; begin(&g); commit(&g); eng_matrix_t projection; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,NULL)); nmo_rect_t extents;
    ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){-2,-2,.5f},&(eng_vector_t){2,2,5},&extents)); rect(extents,(nmo_rect_t){0,0,1,1});
    const eng_vector_t low[] = {{10,0,5},{-20,0,5},{0,20,5},{0,-20,5},{0,0,-5},{0,0,101}}, high[] = {{20,1,6},{-10,1,6},{1,30,6},{1,-10,6},{1,1,-2},{1,1,102}};
    for (size_t i = 0; i < COUNT(low); i++) ASSERT_FALSE(eng_project_box(&projection,&low[i],&high[i],&extents)); ASSERT_FALSE(eng_project_box(&projection,&(eng_vector_t){NAN,0,0},&(eng_vector_t){1,1,1},&extents)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_portal_management, hierarchy_bounds_exclude_empty_parents_and_follow_live_descendants);
REGISTER_TEST(engine_portal_management, camera_projection_uses_native_aspect_near_origin_and_runtime_clipping_planes);
REGISTER_TEST(engine_portal_management, orthographic_boxes_use_zoom_and_transformed_camera_axes);
REGISTER_TEST(engine_portal_management, projected_boxes_preserve_native_near_plane_expansion_and_reject_separated_frusta);
TEST_MAIN_END()
