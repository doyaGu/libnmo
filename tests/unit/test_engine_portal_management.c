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

TEST(engine_portal_management, entities_without_scripts_keep_start_runtime_and_copy_scene_activity)
{
    for (int all = 0; all < 2; all++) {
        portal_fixture_t g; begin(&g); nmo_beobject_state_t *portal = fixture_state(&g.f,g.portal[0]); portal->has_single_activity = 1; portal->single_activity_flags = CK_SCENEOBJECT_START_DEACTIVATE;
        fixture_begin_edit(&g.f); fixture_commit(&g.f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,all ? NMO_ENGINE_START_ALL : NMO_ENGINE_START_SCENE_DEFAULT));
        ASSERT_EQ(all != 0,nmo_engine_object_is_active(g.f.engine,g.portal[0])); ASSERT_EQ(NMO_OK,nmo_engine_activate_object(g.f.engine,g.portal[0],false)); ASSERT_FALSE(nmo_engine_object_is_active(g.f.engine,g.portal[0]));
        nmo_object_id_t copy = 0; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&g.portal[0],1,false,&copy)); ASSERT_TRUE(copy != 0); ASSERT_FALSE(nmo_engine_object_is_active(g.f.engine,copy));
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&g.portal[0],1,true,&copy)); ASSERT_TRUE(nmo_engine_object_is_active(g.f.engine,copy)); ASSERT_EQ(NMO_OK,nmo_engine_activate_object(g.f.engine,g.portal[0],true)); ASSERT_TRUE(nmo_engine_object_is_active(g.f.engine,g.portal[0])); fixture_end(&g.f);
    }
}

static void graph_begin(portal_fixture_t *g)
{
    begin(g); g->f.script_id = fixture_add_script(&g->f,"Portal Management Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_PORTAL_MANAGEMENT,"Portal Management"); g->out = fixture_block(&g->f,GUID_NOP,"After Portal Management"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void graph_commit(portal_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_PORTAL_MANAGEMENT)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); g->f.engine->render.camera = g->camera;
}

static void connect(portal_fixture_t *g, bool wildcard)
{
    ASSERT_TRUE(eng_place_add_portal(g->f.engine,g->places[0],g->places[1],wildcard ? 0 : g->portal[0])); ASSERT_TRUE(eng_place_add_portal(g->f.engine,g->places[1],g->places[2],wildcard ? 0 : g->portal[1]));
}

static void collection(nmo_engine_t *engine, nmo_object_id_t id, const nmo_object_id_t *expected, size_t count)
{
    const void *data = NULL; size_t size = 0; ASSERT_EQ(NMO_OK,nmo_engine_param_value(engine,id,&data,&size,NULL)); ASSERT_EQ(count*sizeof(nmo_object_id_t),size);
    for (size_t i = 0; i < count; i++) { nmo_object_id_t value = 0; memcpy(&value,(const uint8_t *)data+i*sizeof(value),sizeof(value)); ASSERT_EQ(expected[i],value); }
}

static void outputs(portal_fixture_t *g, int pos, const nmo_object_id_t *expected, size_t count)
{
    collection(g->f.engine,BLOCK_POUT(&g->f,g->block,pos),expected,count);
}

static void frame(portal_fixture_t *g)
{
    nmo_engine_frame_t result; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g->f.engine,&result));
}

static void activate(portal_fixture_t *g, int pos)
{
    nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; nmo_engine_block_activate_input(&block,pos,true);
}

static void move(portal_fixture_t *g, nmo_object_id_t id, eng_vector_t position)
{
    eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,id),&position,-1,false);
}

TEST(engine_portal_management, rendering_updates_collections_and_visibility_using_live_portals_before_each_frame)
{
    portal_fixture_t g; graph_begin(&g); graph_commit(&g); connect(&g,false); eng_show(g.f.engine,g.places[3],2); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&g.places[3],sizeof(g.places[3]))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g,0,g.places+3,1); ASSERT_TRUE(nmo_engine_object_is_visible(g.f.engine,g.places[1])); ASSERT_EQ(1u,runs(&g.f,g.out));
    frame(&g); outputs(&g,0,g.places,1); outputs(&g,1,g.places,3); ASSERT_FALSE(nmo_engine_object_is_visible(g.f.engine,g.places[3])); float half = .5f/4.9f; rect(eng_place_viewport_clip(g.f.engine,g.places[1]),(nmo_rect_t){.5f-half,.5f-half,.5f+half,.5f+half});
    move(&g,g.portal[0],(eng_vector_t){100,0,5}); frame(&g); outputs(&g,0,g.places,1); outputs(&g,1,g.places,1); ASSERT_FALSE(nmo_engine_object_is_visible(g.f.engine,g.places[1])); ASSERT_FALSE(nmo_engine_object_is_visible(g.f.engine,g.places[2])); ASSERT_EQ(1u,runs(&g.f,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); ASSERT_EQ(2u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_portal_management, inactive_portals_block_traversal_while_null_portals_ignore_projection_and_cycles)
{
    for (int wildcard = 0; wildcard < 2; wildcard++) {
        portal_fixture_t g; graph_begin(&g); graph_commit(&g); connect(&g,wildcard); ASSERT_EQ(NMO_OK,nmo_engine_activate_object(g.f.engine,g.portal[0],false)); move(&g,g.portal[0],(eng_vector_t){100,0,-10}); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g); outputs(&g,1,g.places,wildcard ? 3 : 1);
        if (!wildcard) { move(&g,g.portal[0],(eng_vector_t){0,0,5}); ASSERT_EQ(NMO_OK,nmo_engine_activate_object(g.f.engine,g.portal[0],true)); }
        ASSERT_TRUE(eng_place_add_portal(g.f.engine,g.places[2],g.places[0],0)); frame(&g); outputs(&g,1,g.places,3); fixture_end(&g.f);
    }
}

TEST(engine_portal_management, traversal_depth_counts_the_starting_place_and_negative_depth_remains_unlimited)
{
    const int32_t depths[] = {1,2,3,0,-1};
    for (size_t i = 0; i < COUNT(depths); i++) {
        portal_fixture_t g; graph_begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),depths[i]); graph_commit(&g); connect(&g,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g); outputs(&g,0,g.places,1); outputs(&g,1,g.places,depths[i] > 0 ? (size_t)depths[i] : 3); fixture_end(&g.f);
    }
}

TEST(engine_portal_management, missing_cameras_empty_debug_groups_and_outside_cameras_have_distinct_fallbacks)
{
    for (int kind = 0; kind < 3; kind++) {
        portal_fixture_t g; graph_begin(&g);
        if (kind == 1) { nmo_object_id_t cameras = fixture_create(&g.f,NMO_CID_GROUP,"Empty Debug Cameras"); ASSERT_TRUE(cameras != 0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)cameras); }
        graph_commit(&g); connect(&g,true); if (kind == 0) g.f.engine->render.camera = 0; if (kind == 2) move(&g,g.camera,(eng_vector_t){100,0,0}); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g);
        outputs(&g,0,NULL,0); outputs(&g,1,g.places,kind == 2 ? 3 : 0); for (int i = 0; i < 3; i++) ASSERT_EQ(kind == 2,nmo_engine_object_is_visible(g.f.engine,g.places[i])); fixture_end(&g.f);
    }
}

TEST(engine_portal_management, trans_place_groups_are_filtered_by_hierarchy_bounds_and_portal_screen_extents)
{
    for (int invalid = 0; invalid < 2; invalid++) {
        portal_fixture_t g; graph_begin(&g); nmo_object_id_t trans = fixture_create(&g.f,NMO_CID_GROUP,"Trans Place Objects"), inside = box(&g,"Trans Inside",0,(eng_vector_t){-.2f,-.2f,9.8f},(eng_vector_t){.2f,.2f,10.2f}), outside = box(&g,"Trans Outside",0,(eng_vector_t){3.3f,-.2f,.8f},(eng_vector_t){3.7f,.2f,1.2f}); ASSERT_TRUE(trans != 0 && inside != 0 && outside != 0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)trans); graph_commit(&g); connect(&g,false); ASSERT_TRUE(eng_group_add(g.f.engine,trans,inside)); ASSERT_TRUE(eng_group_add(g.f.engine,trans,outside));
        if (invalid) { nmo_object_id_t wrong = eng_create_object(g.f.engine,NMO_CID_GROUP,"Invalid Trans Member"); ASSERT_TRUE(wrong != 0); ASSERT_TRUE(eng_group_add(g.f.engine,trans,wrong)); }
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g); outputs(&g,2,&inside,invalid ? 0 : 1); ASSERT_TRUE(nmo_engine_object_is_visible(g.f.engine,inside)); ASSERT_EQ(invalid != 0,nmo_engine_object_is_visible(g.f.engine,outside));
        if (!invalid) { move(&g,g.portal[0],(eng_vector_t){100,0,5}); frame(&g); outputs(&g,2,NULL,0); ASSERT_FALSE(nmo_engine_object_is_visible(g.f.engine,inside)); }
        fixture_end(&g.f);
    }
}

TEST(engine_portal_management, off_restores_places_but_retains_viewport_and_hidden_trans_place_objects)
{
    portal_fixture_t g; graph_begin(&g); nmo_object_id_t trans = fixture_create(&g.f,NMO_CID_GROUP,"Trans Objects"), object = box(&g,"Hidden Trans",0,(eng_vector_t){3.3f,0,.8f},(eng_vector_t){3.7f,.2f,1.2f}); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)trans); graph_commit(&g); connect(&g,false); ASSERT_TRUE(eng_group_add(g.f.engine,trans,object)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g);
    nmo_rect_t viewport = eng_place_viewport_clip(g.f.engine,g.places[1]); ASSERT_FALSE(nmo_engine_object_is_visible(g.f.engine,object)); activate(&g,1); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block));
    for (int i = 0; i < 3; i++) ASSERT_TRUE(nmo_engine_object_is_visible(g.f.engine,g.places[i])); rect(eng_place_viewport_clip(g.f.engine,g.places[1]),viewport); ASSERT_FALSE(nmo_engine_object_is_visible(g.f.engine,object)); move(&g,g.portal[0],(eng_vector_t){100,0,5}); frame(&g); outputs(&g,1,g.places,3); fixture_end(&g.f);
}

TEST(engine_portal_management, script_deactivation_keeps_the_manager_running_and_global_reset_restores_places_and_trans_objects)
{
    portal_fixture_t g; graph_begin(&g); nmo_object_id_t trans = fixture_create(&g.f,NMO_CID_GROUP,"Trans Objects"), object = box(&g,"Hidden Trans",0,(eng_vector_t){3.3f,0,.8f},(eng_vector_t){3.7f,.2f,1.2f}); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)trans); graph_commit(&g); connect(&g,false); ASSERT_TRUE(eng_group_add(g.f.engine,trans,object)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,false,false)); move(&g,g.portal[0],(eng_vector_t){100,0,5}); frame(&g); outputs(&g,1,g.places,1); ASSERT_FALSE(nmo_engine_object_is_visible(g.f.engine,object));
    eng_managers_reset(g.f.engine); for (int i = 0; i < 3; i++) { ASSERT_TRUE(nmo_engine_object_is_visible(g.f.engine,g.places[i])); rect(eng_place_viewport_clip(g.f.engine,g.places[i]),(nmo_rect_t){0}); } ASSERT_TRUE(nmo_engine_object_is_visible(g.f.engine,object)); frame(&g); ASSERT_TRUE(nmo_engine_object_is_visible(g.f.engine,g.places[1])); fixture_end(&g.f);
}

TEST(engine_portal_management, automatic_generation_fits_adjacent_hierarchies_and_does_not_duplicate_existing_connections)
{
    portal_fixture_t g; graph_begin(&g); const nmo_behavior_state_t *state = fixture_state(&g.f,g.block); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,nmo_behavior_ref_array_get_id(&state->local_parameters,0),"TRUE")); graph_commit(&g); size_t before = nmo_engine_entity_count(g.f.engine); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(before+3,nmo_engine_entity_count(g.f.engine));
    for (int i = 0; i < 3; i++) {
        char name[80]; snprintf(name,sizeof(name),"Portal %s-%s",nmo_engine_object_name(g.f.engine,g.places[i]),nmo_engine_object_name(g.f.engine,g.places[i+1])); nmo_object_id_t id = nmo_engine_object_named(g.f.engine,name,NMO_CID_3DENTITY); ASSERT_TRUE(id != 0); int32_t entity = eng_entity_index(g.f.engine,id); ASSERT_TRUE((g.f.engine->entities.data[entity].entity_flags & CK_3DENTITY_PORTAL) != 0); vector(eng_entity_position(g.f.engine,entity,-1),(eng_vector_t){0,0,(float)i*10+5}); ASSERT_TRUE(nmo_engine_object_is_active(g.f.engine,id)); ASSERT_TRUE(eng_in_scene(g.f.engine,id));
    }
    frame(&g); outputs(&g,1,g.places,4); activate(&g,0); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(before+3,nmo_engine_entity_count(g.f.engine)); const eng_portal_list_t *links = eng_place_portals(g.f.engine,g.places[1]); ASSERT_NOT_NULL(links); ASSERT_EQ(2u,links->count); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_portal_management, hierarchy_bounds_exclude_empty_parents_and_follow_live_descendants);
REGISTER_TEST(engine_portal_management, camera_projection_uses_native_aspect_near_origin_and_runtime_clipping_planes);
REGISTER_TEST(engine_portal_management, orthographic_boxes_use_zoom_and_transformed_camera_axes);
REGISTER_TEST(engine_portal_management, projected_boxes_preserve_native_near_plane_expansion_and_reject_separated_frusta);
REGISTER_TEST(engine_portal_management, entities_without_scripts_keep_start_runtime_and_copy_scene_activity);
REGISTER_TEST(engine_portal_management, rendering_updates_collections_and_visibility_using_live_portals_before_each_frame);
REGISTER_TEST(engine_portal_management, inactive_portals_block_traversal_while_null_portals_ignore_projection_and_cycles);
REGISTER_TEST(engine_portal_management, traversal_depth_counts_the_starting_place_and_negative_depth_remains_unlimited);
REGISTER_TEST(engine_portal_management, missing_cameras_empty_debug_groups_and_outside_cameras_have_distinct_fallbacks);
REGISTER_TEST(engine_portal_management, trans_place_groups_are_filtered_by_hierarchy_bounds_and_portal_screen_extents);
REGISTER_TEST(engine_portal_management, off_restores_places_but_retains_viewport_and_hidden_trans_place_objects);
REGISTER_TEST(engine_portal_management, script_deactivation_keeps_the_manager_running_and_global_reset_restores_places_and_trans_objects);
REGISTER_TEST(engine_portal_management, automatic_generation_fits_adjacent_hierarchies_and_does_not_duplicate_existing_connections);
TEST_MAIN_END()
