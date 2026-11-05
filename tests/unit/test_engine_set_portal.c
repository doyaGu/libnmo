#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

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
    for (size_t i = 0; i < COUNT(originals); i++) nmo_engine_destroy_object(g.f.engine,originals[i]); links(g.f.engine,copies[0],(eng_portal_link_t[]){{copies[1],copies[2]}},1); links(g.f.engine,single,NULL,0); fixture_end(&g.f);
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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_set_portal, runtime_flags_and_bidirectional_links_preserve_document_and_other_engines);
REGISTER_TEST(engine_set_portal, loaded_wildcards_suppress_new_links_and_asymmetric_reverse_lists_keep_native_duplicates);
REGISTER_TEST(engine_set_portal, copies_obey_place_dependency_options_and_remap_live_links_and_portal_flags);
REGISTER_TEST(engine_set_portal, deletion_prunes_cached_and_unread_connections_without_turning_deleted_geometry_into_a_wildcard);
REGISTER_TEST(engine_set_portal, initial_conditions_restore_entity_flags_while_place_connections_persist);
TEST_MAIN_END()
