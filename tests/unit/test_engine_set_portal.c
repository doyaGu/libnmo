#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_operations.h"

#include <math.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_SET_PORTAL NMO_GUID(0x465c4732,0x30996468)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct portal_fixture {
    engine_fixture_t f;
    nmo_object_id_t places[3], portal, other, child, block, out;
} portal_fixture_t;

static void begin(portal_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    const char *names[] = {"Place A","Place B","Place C"};
    for (size_t i = 0; i < 3; i++) { g->places[i] = fixture_create(&g->f,NMO_CID_PLACE,names[i]); ASSERT_TRUE(g->places[i] != 0); ((nmo_place_state_t *)fixture_state(&g->f,g->places[i]))->base.has_entityndata_chunk = 1; }
    g->portal = fixture_create(&g->f,NMO_CID_3DENTITY,"Portal"); g->other = fixture_create(&g->f,NMO_CID_3DENTITY,"Other Portal"); g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Portal Child"); ASSERT_TRUE(g->portal != 0 && g->other != 0 && g->child != 0);
    nmo_3dentity_state_t *portal = fixture_state(&g->f,g->portal), *child = fixture_state(&g->f,g->child); portal->has_entityndata_chunk = child->has_entityndata_chunk = 1;
    portal->entity_flags = CK_3DENTITY_DUMMY; portal->world_matrix[12] = 7; portal->world_matrix[13] = 8; portal->world_matrix[14] = 9;
    child->parent = nmo_ref_from_id(g->portal); child->world_matrix[12] = 8; child->world_matrix[13] = 9; child->world_matrix[14] = 10;
}

static void commit(portal_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static uint32_t flags(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine,id); return index >= 0 ? engine->entities.data[index].entity_flags : 0;
}

static void mark(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine,id); ASSERT_TRUE(index >= 0); engine->entities.data[index].entity_flags |= CK_3DENTITY_PORTAL;
}

static void links(nmo_engine_t *engine, nmo_object_id_t id, const eng_portal_link_t *expected, size_t count)
{
    const eng_portal_list_t *actual = eng_place_portals(engine,id); ASSERT_NOT_NULL(actual); ASSERT_EQ(count,actual->count);
    for (size_t i = 0; i < count; i++) { ASSERT_EQ(expected[i].place,actual->data[i].place); ASSERT_EQ(expected[i].portal,actual->data[i].portal); }
}

static void saved_link(portal_fixture_t *g, nmo_object_id_t from, nmo_object_id_t to, nmo_object_id_t portal)
{
    nmo_place_state_t *state = fixture_state(&g->f,from); nmo_place_portal_entry_t *entry = NULL; state->has_portals = 1;
    ASSERT_EQ(NMO_OK,nmo_array_extend(&state->portals,1,(void **)&entry)); *entry = (nmo_place_portal_entry_t){nmo_ref_from_id(to),nmo_ref_from_id(portal)};
}

TEST(engine_set_portal, runtime_flags_and_bidirectional_links_preserve_document_and_other_engines)
{
    portal_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(CK_3DENTITY_DUMMY,flags(g.f.engine,g.portal));
    ASSERT_FALSE(eng_place_add_portal(g.f.engine,g.places[0],g.places[1],g.portal)); mark(g.f.engine,g.portal); mark(g.f.engine,g.other);
    ASSERT_TRUE(eng_place_add_portal(g.f.engine,g.places[0],g.places[1],g.portal)); ASSERT_TRUE(eng_place_add_portal(g.f.engine,g.places[0],g.places[1],g.portal));
    ASSERT_TRUE(eng_place_add_portal(g.f.engine,g.places[0],g.places[1],g.other)); links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal},{g.places[1],g.other}},2); links(g.f.engine,g.places[1],(eng_portal_link_t[]){{g.places[0],g.portal},{g.places[0],g.other}},2);
    ASSERT_FALSE(eng_place_add_portal(g.f.engine,0,g.places[1],g.portal)); ASSERT_FALSE(eng_place_add_portal(g.f.engine,g.places[0],g.places[0],g.portal)); ASSERT_FALSE(eng_place_add_portal(g.f.engine,g.portal,g.places[1],g.portal));
    const nmo_place_state_t *place = fixture_state(&g.f,g.places[0]); ASSERT_EQ(0u,place->portals.count); ASSERT_EQ(CK_3DENTITY_DUMMY,((nmo_3dentity_state_t *)fixture_state(&g.f,g.portal))->entity_flags);
    nmo_engine_t *second = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&second)); links(second,g.places[0],NULL,0); ASSERT_EQ(CK_3DENTITY_DUMMY,flags(second,g.portal)); nmo_engine_destroy(second); fixture_end(&g.f);
}

TEST(engine_set_portal, loaded_wildcards_suppress_new_links_and_asymmetric_reverse_lists_keep_native_duplicates)
{
    for (int wildcard = 0; wildcard < 2; wildcard++) {
        portal_fixture_t g; begin(&g); if (wildcard) saved_link(&g,g.places[0],g.places[1],0); else saved_link(&g,g.places[1],g.places[0],g.portal); commit(&g); mark(g.f.engine,g.portal);
        ASSERT_TRUE(eng_place_add_portal(g.f.engine,g.places[0],g.places[1],g.portal));
        if (wildcard) { links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],0}},1); links(g.f.engine,g.places[1],NULL,0); }
        else { links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal}},1); links(g.f.engine,g.places[1],(eng_portal_link_t[]){{g.places[0],g.portal},{g.places[0],g.portal}},2); }
        nmo_object_id_t copy = 0; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&g.places[0],1,false,&copy)); links(g.f.engine,copy,NULL,0);
        fixture_end(&g.f);
    }
}

TEST(engine_set_portal, copies_obey_place_dependency_options_and_remap_live_links_and_portal_flags)
{
    portal_fixture_t g; begin(&g); commit(&g); mark(g.f.engine,g.portal); ASSERT_TRUE(eng_place_add_portal(g.f.engine,g.places[0],g.places[1],g.portal));
    const nmo_object_id_t originals[] = {g.places[0],g.places[1],g.portal}; nmo_object_id_t defaults[3] = {0}, copies[3] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,originals,3,false,defaults)); links(g.f.engine,defaults[0],NULL,0); links(g.f.engine,defaults[1],NULL,0); ASSERT_TRUE((flags(g.f.engine,defaults[2]) & CK_3DENTITY_PORTAL) != 0);
    eng_dependencies_t deps; eng_default_copy_dependencies(&deps); deps.classes[NMO_CID_PLACE] = 1; ASSERT_EQ(NMO_OK,eng_copy_objects(g.f.engine,originals,3,&deps,false,false,copies));
    links(g.f.engine,copies[0],(eng_portal_link_t[]){{copies[1],copies[2]}},1); links(g.f.engine,copies[1],(eng_portal_link_t[]){{copies[0],copies[2]}},1);
    nmo_object_id_t single = 0; ASSERT_EQ(NMO_OK,eng_copy_objects(g.f.engine,&g.places[0],1,&deps,false,false,&single)); const eng_portal_list_t *list = eng_place_portals(g.f.engine,single); ASSERT_NOT_NULL(list); ASSERT_EQ(1u,list->count); ASSERT_EQ(g.places[1],list->data[0].place); ASSERT_TRUE(list->data[0].portal != g.portal && eng_entity_index(g.f.engine,list->data[0].portal) >= 0);
    for (size_t i = 0; i < COUNT(originals); i++) { nmo_engine_destroy_object(g.f.engine,originals[i]); } links(g.f.engine,copies[0],(eng_portal_link_t[]){{copies[1],copies[2]}},1); links(g.f.engine,single,NULL,0); fixture_end(&g.f);
}

TEST(engine_set_portal, deletion_prunes_cached_and_unread_connections_without_turning_deleted_geometry_into_a_wildcard)
{
    for (int cached = 0; cached < 2; cached++) {
        portal_fixture_t g; begin(&g); saved_link(&g,g.places[0],g.places[1],g.portal); saved_link(&g,g.places[1],g.places[0],g.portal); saved_link(&g,g.places[0],g.places[2],0); commit(&g);
        if (cached) links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal},{g.places[2],0}},2);
        nmo_engine_destroy_object(g.f.engine,g.portal); links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[2],0}},1); links(g.f.engine,g.places[1],NULL,0);
        nmo_engine_destroy_object(g.f.engine,g.places[2]); links(g.f.engine,g.places[0],NULL,0); ASSERT_TRUE(eng_place_portals(g.f.engine,g.places[2]) == NULL); fixture_end(&g.f);
    }
}

TEST(engine_set_portal, initial_conditions_restore_entity_flags_while_place_connections_persist)
{
    portal_fixture_t g; begin(&g); commit(&g); eng_save_ic(g.f.engine,g.portal); eng_save_ic(g.f.engine,g.places[0]); mark(g.f.engine,g.portal); ASSERT_TRUE(eng_place_add_portal(g.f.engine,g.places[0],g.places[1],g.portal));
    eng_restore_ic(g.f.engine,g.portal); ASSERT_EQ(CK_3DENTITY_DUMMY,flags(g.f.engine,g.portal)); eng_restore_ic(g.f.engine,g.places[0]); links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal}},1);
    mark(g.f.engine,g.portal); eng_save_ic(g.f.engine,g.portal); g.f.engine->entities.data[eng_entity_index(g.f.engine,g.portal)].entity_flags = 0; eng_restore_ic(g.f.engine,g.portal); ASSERT_EQ(CK_3DENTITY_DUMMY | CK_3DENTITY_PORTAL,flags(g.f.engine,g.portal)); fixture_end(&g.f);
}

static nmo_object_id_t geometry(portal_fixture_t *g, nmo_object_id_t place, const char *name, const eng_vector_t *points, size_t count, nmo_object_id_t *out_mesh)
{
    char mesh_name[96]; snprintf(mesh_name,sizeof(mesh_name),"%s Mesh",name);
    nmo_object_id_t entity = fixture_create(&g->f,NMO_CID_3DENTITY,name), mesh = fixture_create(&g->f,NMO_CID_MESH,mesh_name);
    if (entity == 0 || mesh == 0) return 0;
    nmo_arena_t *arena = nmo_session_get_arena(g->f.session); nmo_3dentity_state_t *e = fixture_state(&g->f,entity); nmo_mesh_state_t *m = fixture_state(&g->f,mesh);
    e->has_entityndata_chunk = e->has_mesh_chunk = 1; e->parent = nmo_ref_from_id(place); e->current_mesh = nmo_ref_from_id(mesh); e->mesh_count = 1;
    e->mesh_ids = nmo_arena_alloc(arena,sizeof(nmo_ref_t),_Alignof(nmo_ref_t)); if (e->mesh_ids == NULL) return 0; e->mesh_ids[0] = nmo_ref_from_id(mesh);
    m->vertex_count = (uint32_t)count; m->vertices = count != 0 ? nmo_arena_alloc(arena,count*sizeof(nmo_vertex_t),_Alignof(nmo_vertex_t)) : NULL;
    m->vertex_colors = count != 0 ? nmo_arena_alloc(arena,count*sizeof(*m->vertex_colors),_Alignof(uint32_t)) : NULL; m->vertex_specular = count != 0 ? nmo_arena_alloc(arena,count*sizeof(*m->vertex_specular),_Alignof(uint32_t)) : NULL;
    if (count != 0 && (m->vertices == NULL || m->vertex_colors == NULL || m->vertex_specular == NULL)) return 0;
    if (count != 0) { memset(m->vertices,0,count*sizeof(nmo_vertex_t)); memset(m->vertex_colors,0xff,count*sizeof(*m->vertex_colors)); memset(m->vertex_specular,0,count*sizeof(*m->vertex_specular)); }
    for (size_t i = 0; i < count; i++) m->vertices[i].position = (nmo_vector_t){points[i].x,points[i].y,points[i].z};
    nmo_mesh_update_bounding_volumes(m); if (out_mesh != NULL) *out_mesh = mesh; return entity;
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z)); ASSERT_FLOAT_EQ(expected.x,actual.x,3e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,3e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,3e-4f);
}

static void matrix(const eng_matrix_t *actual, const eng_matrix_t *expected)
{
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { ASSERT_TRUE(isfinite(actual->m[r][c])); ASSERT_FLOAT_EQ(expected->m[r][c],actual->m[r][c],3e-4f); }
}

static const eng_vector_t face[] = {{-2,-1,0},{2,-1,0},{2,1,0},{-2,1,0}};

TEST(engine_set_portal, fitting_uses_common_hierarchical_vertices_live_transforms_and_runtime_geometry)
{
    portal_fixture_t g; begin(&g); eng_vector_t a[5], b[5]; memcpy(a,face,sizeof(face)); memcpy(b,face,sizeof(face)); a[4] = (eng_vector_t){9,9,9}; b[4] = (eng_vector_t){-9,-9,-9}; nmo_object_id_t mesh_b = 0;
    ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",a,5,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",b,5,&mesh_b) != 0); commit(&g);
    eng_matrix_t box; ASSERT_TRUE(eng_place_best_fit(g.f.engine,g.places[0],g.places[1],&box)); eng_matrix_t expected = {{{2.1f,0,0,0},{0,1.1f,0,0},{0,0,.1f,0},{0,0,0,1}}}; matrix(&box,&expected);
    eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.places[0]),&(eng_vector_t){10,20,30},-1,false); eng_matrix_t before = box; ASSERT_FALSE(eng_place_best_fit(g.f.engine,g.places[0],g.places[1],&box)); matrix(&box,&before);
    eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.places[1]),&(eng_vector_t){10,20,30},-1,false); ASSERT_TRUE(eng_place_best_fit(g.f.engine,g.places[0],g.places[1],&box)); expected.m[3][0] = 10; expected.m[3][1] = 20; expected.m[3][2] = 30; matrix(&box,&expected);
    eng_mesh_copy_t *copy = eng_mesh_copy(g.f.engine,mesh_b); ASSERT_NOT_NULL(copy); for (size_t i = 0; i < 4; i++) { copy->vertices[i].position.x += .009f; copy->vertices[i].position.y += .009f; copy->vertices[i].position.z += .009f; }
    ASSERT_TRUE(eng_place_best_fit(g.f.engine,g.places[0],g.places[1],&box)); vector((eng_vector_t){box.m[3][0],box.m[3][1],box.m[3][2]},(eng_vector_t){10.009f,20.009f,30.009f}); fixture_end(&g.f);
}

TEST(engine_set_portal, fitted_pca_boxes_follow_rotated_faces_with_native_border_and_right_handed_axes)
{
    portal_fixture_t g; begin(&g); eng_vector_t points[4];
    for (size_t i = 0; i < 4; i++) points[i] = (eng_vector_t){3+.6f*face[i].x-.8f*face[i].y,5+.8f*face[i].x+.6f*face[i].y,7};
    ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",points,4,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",points,4,NULL) != 0); commit(&g); eng_matrix_t box; ASSERT_TRUE(eng_place_best_fit(g.f.engine,g.places[0],g.places[1],&box));
    vector((eng_vector_t){box.m[3][0],box.m[3][1],box.m[3][2]},(eng_vector_t){3,5,7}); eng_vector_t axes[3]; float lengths[3];
    for (int r = 0; r < 3; r++) { axes[r] = (eng_vector_t){box.m[r][0],box.m[r][1],box.m[r][2]}; lengths[r] = eng_vector_magnitude(&axes[r]); eng_vector_normalize(&axes[r]); }
    vector(eng_vector_cross(&axes[0],&axes[1]),axes[2]);
    for (int r = 0; r < 3; r++) for (int s = r+1; s < 3; s++) if (lengths[r] > lengths[s]) { float value = lengths[r]; lengths[r] = lengths[s]; lengths[s] = value; }
    ASSERT_FLOAT_EQ(.1f,lengths[0],3e-4f); ASSERT_FLOAT_EQ(1.1f,lengths[1],3e-4f); ASSERT_FLOAT_EQ(2.1f,lengths[2],3e-4f);
    eng_matrix_t inverse; eng_matrix_identity(&inverse); eng_matrix_inverse(&inverse,&box);
    for (size_t i = 0; i < 4; i++) { eng_vector_t local; eng_vector_transform(&local,&inverse,&points[i]); ASSERT_TRUE(fabsf(local.x) <= 1 && fabsf(local.y) <= 1 && fabsf(local.z) <= 1); } fixture_end(&g.f);
}

TEST(engine_set_portal, matching_uses_per_axis_tolerance_and_deduplicates_common_points_in_query_order)
{
    for (int reverse = 0; reverse < 2; reverse++) {
        portal_fixture_t g; begin(&g); const eng_vector_t a[] = {{0,0,0},{.05f,.05f,.05f},{3,0,0}}, b[] = {{.009f,.009f,.009f},{.05f,.05f,.05f}}; eng_vector_t queries[] = {b[reverse],b[1-reverse]};
        ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",a,3,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",queries,2,NULL) != 0); commit(&g); eng_matrix_t box; ASSERT_TRUE(eng_place_best_fit(g.f.engine,g.places[0],g.places[1],&box));
        const eng_matrix_t expected = {{{.1f,0,0,0},{0,.1f,0,0},{0,0,.1f,0},{queries[0].x,queries[0].y,queries[0].z,1}}}; matrix(&box,&expected); fixture_end(&g.f);
    }
}

TEST(engine_set_portal, missing_nonmatching_nonfinite_and_excessive_geometry_leave_the_fit_output_unchanged)
{
    for (int kind = 0; kind < 4; kind++) {
        portal_fixture_t g; begin(&g); eng_vector_t a = {0,0,0}, b = {.02f,0,0};
        if (kind == 2) b.x = NAN;
        if (kind == 3) a.x = b.x = 2e6f;
        if (kind != 0) { ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",&a,1,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",&b,1,NULL) != 0); }
        commit(&g); eng_matrix_t box; eng_matrix_identity(&box); box.m[3][0] = 99; eng_matrix_t before = box;
        ASSERT_FALSE(eng_place_best_fit(g.f.engine,g.places[0],g.places[1],&box)); matrix(&box,&before); ASSERT_FALSE(eng_place_best_fit(g.f.engine,g.portal,g.places[1],&box)); ASSERT_FALSE(eng_place_best_fit(g.f.engine,0,g.places[1],&box)); matrix(&box,&before); fixture_end(&g.f);
    }
}

static void graph_begin(portal_fixture_t *g)
{
    begin(g); g->f.script_id = fixture_add_script(&g->f,"Portal Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_SET_PORTAL,"Set Portal"); g->out = fixture_block(&g->f,GUID_NOP,"After Portal"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->portal); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->places[0]); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,1),(int32_t)g->places[1]);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void graph_commit(portal_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_SET_PORTAL)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static nmo_object_id_t source(portal_fixture_t *g, nmo_object_id_t pin)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,pin));
}

static void trigger(portal_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

TEST(engine_set_portal, block_sets_flags_adds_connections_and_switches_targets_without_scheduling_or_removing_old_links)
{
    portal_fixture_t g; graph_begin(&g); nmo_object_id_t target = source(&g,fixture_target(&g.f,g.block)), second = source(&g,BLOCK_PIN(&g.f,g.block,1)); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(CK_3DENTITY_DUMMY | CK_3DENTITY_PORTAL,flags(g.f.engine,g.portal)); links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal}},1); vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.portal),-1),(eng_vector_t){7,8,9});
    trigger(&g); links(g.f.engine,g.places[1],(eng_portal_link_t[]){{g.places[0],g.portal}},1);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,second,&g.places[2],sizeof(g.places[2]))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,target,&g.other,sizeof(g.other))); trigger(&g);
    ASSERT_TRUE((flags(g.f.engine,g.other) & CK_3DENTITY_PORTAL) != 0); links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal},{g.places[2],g.other}},2); links(g.f.engine,g.places[2],(eng_portal_link_t[]){{g.places[0],g.other}},1);
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(3u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(3u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_set_portal, auto_fit_applies_native_bounds_moves_children_and_entity_ic_does_not_remove_connections)
{
    portal_fixture_t g; graph_begin(&g); ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",face,4,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",face,4,NULL) != 0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1); graph_commit(&g); eng_save_ic(g.f.engine,g.portal);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const eng_matrix_t expected = {{{2.1f,0,0,0},{0,1.1f,0,0},{0,0,.1f,0},{0,0,0,1}}}; matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.portal)].world,&expected);
    vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.child),-1),(eng_vector_t){2.1f,1.1f,.1f}); ASSERT_EQ(1u,runs(&g.f,g.out));
    eng_restore_ic(g.f.engine,g.portal); ASSERT_EQ(CK_3DENTITY_DUMMY,flags(g.f.engine,g.portal)); vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.portal),-1),(eng_vector_t){7,8,9}); links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal}},1); fixture_end(&g.f);
}

TEST(engine_set_portal, failed_geometric_fit_still_completes_and_keeps_connection_flags_and_original_pose)
{
    for (int missing = 0; missing < 2; missing++) {
        portal_fixture_t g; graph_begin(&g);
        if (!missing) { eng_vector_t moved[4]; for (size_t i = 0; i < 4; i++) moved[i] = (eng_vector_t){face[i].x,face[i].y,4}; ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",face,4,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",moved,4,NULL) != 0); }
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1); graph_commit(&g); eng_matrix_t before = g.f.engine->entities.data[eng_entity_index(g.f.engine,g.portal)].world; ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.portal)].world,&before);
        ASSERT_TRUE((flags(g.f.engine,g.portal) & CK_3DENTITY_PORTAL) != 0); links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal}},1); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_set_portal, equal_or_missing_places_preserve_native_partial_flags_and_io_before_parameter_errors)
{
    for (int kind = 0; kind < 5; kind++) {
        portal_fixture_t g; graph_begin(&g); nmo_object_id_t first = kind == 0 || kind == 4 ? g.places[0] : 0, second = kind == 0 ? g.places[0] : kind == 1 || kind == 4 ? 0 : g.places[1];
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)first); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)second); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),kind >= 3); graph_commit(&g);
        nmo_status_t expected = kind == 2 ? NMO_OK : NMO_ERR_VALIDATION_FAILED; ASSERT_EQ(expected,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(CK_3DENTITY_DUMMY | CK_3DENTITY_PORTAL,flags(g.f.engine,g.portal)); links(g.f.engine,g.places[0],NULL,0); links(g.f.engine,g.places[1],NULL,0);
        if (expected != NMO_OK) { nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.out)); }
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

typedef struct fit_observation { portal_fixture_t *g; size_t operations; bool ordered; } fit_observation_t;

static void observe_fit_input(const nmo_engine_event_t *event, void *data)
{
    fit_observation_t *seen = data;
    if (event->kind != NMO_ENGINE_EVENT_OPERATION || event->result != NMO_OK) return;
    seen->operations++; portal_fixture_t *g = seen->g; const eng_portal_list_t *list = eng_place_portals(g->f.engine,g->places[0]);
    seen->ordered = (flags(g->f.engine,g->portal) & CK_3DENTITY_PORTAL) != 0 && list != NULL && list->count == 1 && g->f.engine->entities.data[eng_entity_index(g->f.engine,g->portal)].world.m[3][0] == 7;
}

TEST(engine_set_portal, fit_input_runs_after_connection_creation_and_is_not_evaluated_after_earlier_errors)
{
    for (int kind = 0; kind < 3; kind++) {
        portal_fixture_t g; graph_begin(&g); ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",face,4,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",face,4,NULL) != 0);
        nmo_object_id_t a = 0, b = 0, operation = 0;
        ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_BOOL,"A",&a)); ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_BOOL,"B",&b));
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,a,"TRUE")); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,b,"TRUE")); ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g.f.tx,g.f.script_id,NMO_OP_GUID_AND,a,b,0,&operation)); const nmo_parameteroperation_state_t *state = fixture_state(&g.f,operation); ASSERT_NOT_NULL(state);
        ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,nmo_parameteroperation_out_id(state),BLOCK_PIN(&g.f,g.block,2)));
        if (kind == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.places[0]);
        if (kind == 2) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        graph_commit(&g); fit_observation_t seen = {.g = &g}; nmo_engine_set_trace(g.f.engine,observe_fit_input,&seen); ASSERT_EQ(kind == 0 ? NMO_OK : NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(kind == 0 ? 1u : 0u,seen.operations);
        if (kind == 0) { ASSERT_TRUE(seen.ordered); vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.portal),-1),(eng_vector_t){0,0,0}); } fixture_end(&g.f);
    }
}

TEST(engine_set_portal, invalid_targets_activate_output_without_marking_or_connecting_objects)
{
    for (int kind = 0; kind < 3; kind++) {
        portal_fixture_t g; graph_begin(&g); nmo_object_id_t wrong = fixture_create(&g.f,NMO_CID_GROUP,"Wrong Target"), target = source(&g,fixture_target(&g.f,g.block)); graph_commit(&g); nmo_object_id_t bad = kind == 0 ? 0 : kind == 1 ? g.portal : wrong;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,target,&bad,sizeof(bad))); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.portal);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_EQ(0u,flags(g.f.engine,g.other)); links(g.f.engine,g.places[0],NULL,0); links(g.f.engine,g.places[1],NULL,0); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_set_portal, fitted_bounds_drive_mimic_and_hierarchy_in_the_same_frame_without_duplicate_links)
{
    portal_fixture_t g; graph_begin(&g); ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",face,4,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",face,4,NULL) != 0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1);
    nmo_object_id_t mimic = fixture_block(&g.f,NMO_GUID(0x12db7b06,0x74c514c3),"Follow Portal Bounds"); ASSERT_TRUE(mimic != 0); fixture_input_int(&g.f,fixture_target(&g.f,mimic),(int32_t)g.other); fixture_input_int(&g.f,BLOCK_PIN(&g.f,mimic,0),(int32_t)g.portal);
    const nmo_behavior_state_t *mimic_state = fixture_state(&g.f,mimic); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,nmo_behavior_ref_array_get_id(&mimic_state->local_parameters,0),"FALSE"));
    for (int i = 1; i < 7; i++) { if ((i & 1) != 0) fixture_input_int(&g.f,BLOCK_PIN(&g.f,mimic,i),1); else fixture_input_value(&g.f,BLOCK_PIN(&g.f,mimic,i),&(float){1},sizeof(float)); }
    fixture_link(&g.f,BLOCK_OUT(&g.f,g.out,0),BLOCK_IN(&g.f,mimic,0),0); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    eng_matrix_t expected = {{{2.1f,0,0,0},{0,1.1f,0,0},{0,0,.1f,0},{0,0,0,1}}}; matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.other)].world,&expected);
    for (int i = 0; i < 2; i++) eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.places[i]),&(eng_vector_t){10,20,30},-1,false);
    trigger(&g); expected.m[3][0] = 10; expected.m[3][1] = 20; expected.m[3][2] = 30; matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.other)].world,&expected); vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.child),-1),(eng_vector_t){12.1f,21.1f,30.1f});
    links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.portal}},1); ASSERT_EQ(2u,runs(&g.f,mimic)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_set_portal, same_frame_deletion_prunes_the_removed_portal_and_preserves_later_connections)
{
    portal_fixture_t g; graph_begin(&g); nmo_object_id_t deleter = fixture_block(&g.f,NMO_GUID(0x74120ded,0x76524673),"Delete First Portal"), next = fixture_block(&g.f,GUID_SET_PORTAL,"Set Other Portal"); ASSERT_TRUE(deleter != 0 && next != 0);
    const nmo_behavior_state_t *state = fixture_state(&g.f,deleter); nmo_object_id_t object_pin = state->in_parameters.count > 1 ? BLOCK_PIN(&g.f,deleter,0) : fixture_target(&g.f,deleter); fixture_input_int(&g.f,object_pin,(int32_t)g.portal);
    fixture_input_int(&g.f,fixture_target(&g.f,next),(int32_t)g.other); fixture_input_int(&g.f,BLOCK_PIN(&g.f,next,0),(int32_t)g.places[0]); fixture_input_int(&g.f,BLOCK_PIN(&g.f,next,1),(int32_t)g.places[1]); fixture_link(&g.f,BLOCK_OUT(&g.f,g.out,0),BLOCK_IN(&g.f,deleter,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,deleter,0),BLOCK_IN(&g.f,next,0),0);
    graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(eng_entity_index(g.f.engine,g.portal) < 0); ASSERT_TRUE((flags(g.f.engine,g.other) & CK_3DENTITY_PORTAL) != 0);
    links(g.f.engine,g.places[0],(eng_portal_link_t[]){{g.places[1],g.other}},1); links(g.f.engine,g.places[1],(eng_portal_link_t[]){{g.places[0],g.other}},1); ASSERT_EQ(1u,runs(&g.f,next)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
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

TEST(engine_set_portal, loaded_and_copied_graphs_remap_live_connections_and_fit_independent_place_hierarchies)
{
    portal_fixture_t g; graph_begin(&g); ASSERT_TRUE(geometry(&g,g.places[0],"Geometry A",face,4,NULL) != 0); ASSERT_TRUE(geometry(&g,g.places[1],"Geometry B",face,4,NULL) != 0);
    const nmo_object_id_t values[] = {g.portal,g.places[0],g.places[1]};
    for (int pos = -1; pos < 2; pos++) { char text[32]; snprintf(text,sizeof(text),"%u",values[pos+1]); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,source(&g,pos < 0 ? fixture_target(&g.f,g.block) : BLOCK_PIN(&g.f,g.block,pos)),text)); }
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,source(&g,BLOCK_PIN(&g.f,g.block,2)),"TRUE")); fixture_commit_bare(&g.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *file_path = NMO_TEST_SCRATCH_FILE("engine_set_portal.cmo"); nmo_status_t saved = nmo_session_save_file(g.f.session,file_path,NULL,NULL); if (saved != NMO_OK) fprintf(stderr,"Portal graph save: %s\n",nmo_last_error_message()); ASSERT_EQ(NMO_OK,saved);
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)file_path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"set-portal.cmo",true,&file)); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t block = nmo_engine_object_named(host.engine,"Set Portal",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0); const eng_module_t *module = eng_module_of(host.engine,block,NULL);
        const char *names[] = {"Owner","Place A","Place B","Portal","Portal Child","Portal Script","Set Portal","After Portal","Geometry A","Geometry B","Geometry A Mesh","Geometry B Mesh"}; nmo_object_id_t ids[16] = {0}, copies[16] = {0};
        for (size_t i = 0; i < COUNT(names); i++) { nmo_object_t *object = nmo_object_repository_find_by_name(module->repo,names[i]); ASSERT_NOT_NULL(object); ids[i] = eng_module_id(module,nmo_object_get_id(object)); ASSERT_TRUE(ids[i] != 0); }
        for (int pos = -1; pos < 3; pos++) { ids[13+pos] = runtime_source(host.engine,block,pos); ASSERT_TRUE(ids[13+pos] != 0); }
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); links(host.engine,ids[1],(eng_portal_link_t[]){{ids[2],ids[3]}},1); eng_matrix_t expected = {{{2.1f,0,0,0},{0,1.1f,0,0},{0,0,.1f,0},{0,0,0,1}}}; matrix(&host.engine->entities.data[eng_entity_index(host.engine,ids[3])].world,&expected);
        eng_dependencies_t deps; eng_default_copy_dependencies(&deps); deps.classes[NMO_CID_PLACE] = 1; ASSERT_EQ(NMO_OK,eng_copy_objects(host.engine,ids,COUNT(ids),&deps,false,false,copies)); for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
        links(host.engine,copies[1],(eng_portal_link_t[]){{copies[2],copies[3]}},1); links(host.engine,copies[2],(eng_portal_link_t[]){{copies[1],copies[3]}},1); ASSERT_TRUE((flags(host.engine,copies[3]) & CK_3DENTITY_PORTAL) != 0);
        for (int pos = 0; pos < 3; pos++) { nmo_object_id_t reference = 0; read_value(&host,copies[12+pos],&reference,sizeof(reference)); ASSERT_EQ(copies[pos == 0 ? 3 : pos],reference); }
        for (int i = 1; i < 3; i++) eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[i]),&(eng_vector_t){10,20,30},-1,false);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); expected.m[3][0] = 10; expected.m[3][1] = 20; expected.m[3][2] = 30; matrix(&host.engine->entities.data[eng_entity_index(host.engine,copies[3])].world,&expected); vector(eng_entity_position(host.engine,eng_entity_index(host.engine,ids[3]),-1),(eng_vector_t){0,0,0});
        for (size_t i = 0; i < 5; i++) { nmo_engine_destroy_object(host.engine,ids[i]); } for (size_t i = 8; i < 12; i++) nmo_engine_destroy_object(host.engine,ids[i]);
        for (int i = 1; i < 3; i++) eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[i]),&(eng_vector_t){-10,3,4},-1,false);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); expected.m[3][0] = -10; expected.m[3][1] = 3; expected.m[3][2] = 4; matrix(&host.engine->entities.data[eng_entity_index(host.engine,copies[3])].world,&expected); vector(eng_entity_position(host.engine,eng_entity_index(host.engine,copies[4]),-1),(eng_vector_t){-7.9f,4.1f,4.1f});
        links(host.engine,copies[1],(eng_portal_link_t[]){{copies[2],copies[3]}},1); ASSERT_EQ(2u,runs(&host,copies[7])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); ASSERT_EQ(2u,runs(&host,copies[6])); fixture_end(&host);
    }
    ASSERT_EQ(CK_3DENTITY_DUMMY,((nmo_3dentity_state_t *)fixture_state(&g.f,g.portal))->entity_flags); fixture_end(&g.f); remove(file_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_set_portal, runtime_flags_and_bidirectional_links_preserve_document_and_other_engines);
REGISTER_TEST(engine_set_portal, loaded_wildcards_suppress_new_links_and_asymmetric_reverse_lists_keep_native_duplicates);
REGISTER_TEST(engine_set_portal, copies_obey_place_dependency_options_and_remap_live_links_and_portal_flags);
REGISTER_TEST(engine_set_portal, deletion_prunes_cached_and_unread_connections_without_turning_deleted_geometry_into_a_wildcard);
REGISTER_TEST(engine_set_portal, initial_conditions_restore_entity_flags_while_place_connections_persist);
REGISTER_TEST(engine_set_portal, fitting_uses_common_hierarchical_vertices_live_transforms_and_runtime_geometry);
REGISTER_TEST(engine_set_portal, fitted_pca_boxes_follow_rotated_faces_with_native_border_and_right_handed_axes);
REGISTER_TEST(engine_set_portal, matching_uses_per_axis_tolerance_and_deduplicates_common_points_in_query_order);
REGISTER_TEST(engine_set_portal, missing_nonmatching_nonfinite_and_excessive_geometry_leave_the_fit_output_unchanged);
REGISTER_TEST(engine_set_portal, block_sets_flags_adds_connections_and_switches_targets_without_scheduling_or_removing_old_links);
REGISTER_TEST(engine_set_portal, auto_fit_applies_native_bounds_moves_children_and_entity_ic_does_not_remove_connections);
REGISTER_TEST(engine_set_portal, failed_geometric_fit_still_completes_and_keeps_connection_flags_and_original_pose);
REGISTER_TEST(engine_set_portal, equal_or_missing_places_preserve_native_partial_flags_and_io_before_parameter_errors);
REGISTER_TEST(engine_set_portal, fit_input_runs_after_connection_creation_and_is_not_evaluated_after_earlier_errors);
REGISTER_TEST(engine_set_portal, invalid_targets_activate_output_without_marking_or_connecting_objects);
REGISTER_TEST(engine_set_portal, fitted_bounds_drive_mimic_and_hierarchy_in_the_same_frame_without_duplicate_links);
REGISTER_TEST(engine_set_portal, same_frame_deletion_prunes_the_removed_portal_and_preserves_later_connections);
REGISTER_TEST(engine_set_portal, loaded_and_copied_graphs_remap_live_connections_and_fit_independent_place_hierarchies);
TEST_MAIN_END()
