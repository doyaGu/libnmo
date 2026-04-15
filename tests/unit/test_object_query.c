/**
 * @file test_object_query.c
 * @brief Unit tests for library-level object query API
 */

#include "test_framework.h"
#include "object/nmo_object_query.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "session/nmo_context.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "type/nmo_type_system.h"

static nmo_context_t *g_ctx;
static nmo_object_repository_t *g_repo;
static nmo_object_t *g_base;
static nmo_object_t *g_entity;
static nmo_object_t *g_camera;
static nmo_object_t *g_mesh;

static nmo_object_t *make_object(
    const nmo_allocator_t *allocator,
    nmo_object_id_t id,
    nmo_class_id_t class_id,
    const char *name)
{
    nmo_object_t *obj = nmo_object_create(allocator, id, class_id);
    if (obj == NULL) {
        return NULL;
    }
    if (name != NULL && nmo_object_set_name(obj, name) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    return obj;
}

static void setup_objects(void)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    g_ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(g_ctx);

    g_repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(g_repo);

    nmo_object_t *base = make_object(&allocator, 1, NMO_CID_OBJECT, "RootObject");
    nmo_object_t *entity = make_object(&allocator, 2, NMO_CID_3DENTITY, "PlayerEntity");
    nmo_object_t *camera = make_object(&allocator, 3, NMO_CID_CAMERA, "MainCamera");
    nmo_object_t *mesh = make_object(&allocator, 4, NMO_CID_MESH, "player_mesh");
    ASSERT_NOT_NULL(base);
    ASSERT_NOT_NULL(entity);
    ASSERT_NOT_NULL(camera);
    ASSERT_NOT_NULL(mesh);

    ASSERT_EQ(NMO_OK, nmo_object_repository_add(g_repo, &base));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(g_repo, &entity));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(g_repo, &camera));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(g_repo, &mesh));

    g_base = nmo_object_repository_find_by_id(g_repo, 1);
    g_entity = nmo_object_repository_find_by_id(g_repo, 2);
    g_camera = nmo_object_repository_find_by_id(g_repo, 3);
    g_mesh = nmo_object_repository_find_by_id(g_repo, 4);
    ASSERT_NOT_NULL(g_base);
    ASSERT_NOT_NULL(g_entity);
    ASSERT_NOT_NULL(g_camera);
    ASSERT_NOT_NULL(g_mesh);
}

static void teardown_objects(void)
{
    nmo_object_repository_destroy(g_repo);
    g_repo = NULL;
    g_base = NULL;
    g_entity = NULL;
    g_camera = NULL;
    g_mesh = NULL;
    nmo_context_release(g_ctx);
    g_ctx = NULL;
}

static bool only_non_mesh(const nmo_object_t *object, void *user_data)
{
    (void)user_data;
    return nmo_object_get_class_id(object) != NMO_CID_MESH;
}

static bool stop_after_first(size_t match_index, nmo_object_t *object, void *user_data)
{
    size_t *seen = (size_t *)user_data;
    (void)match_index;
    (void)object;
    (*seen)++;
    return false;
}

TEST(object_query, filters_by_object_id)
{
    setup_objects();

    nmo_object_query_t query = {
        .object_id = nmo_object_get_id(g_camera)
    };
    bool matches = true;

    ASSERT_EQ(NMO_OK, nmo_object_query_matches(g_entity, &query, NULL, &matches));
    ASSERT_FALSE(matches);
    ASSERT_EQ(NMO_OK, nmo_object_query_matches(g_camera, &query, NULL, &matches));
    ASSERT_TRUE(matches);

    teardown_objects();
}

TEST(object_query, exact_and_derived_class_matching)
{
    setup_objects();
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(g_ctx);

    nmo_object_query_t exact = {
        .class_id = NMO_CID_3DENTITY,
        .include_derived_classes = false
    };
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(g_repo, &exact, registry, NULL, NULL, &result));
    ASSERT_EQ(4, result.total);
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t derived = {
        .class_id = NMO_CID_3DENTITY,
        .include_derived_classes = true
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(g_repo, &derived, registry, NULL, NULL, &result));
    ASSERT_EQ(2, result.matched);

    teardown_objects();
}

TEST(object_query, name_modes_share_case_rules)
{
    setup_objects();

    nmo_object_query_t contains = {
        .name = "camera",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(g_repo, &contains, NULL, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t wildcard = {
        .name = "player*",
        .name_mode = NMO_OBJECT_QUERY_NAME_WILDCARD,
        .name_case_insensitive = true
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(g_repo, &wildcard, NULL, NULL, NULL, &result));
    ASSERT_EQ(2, result.matched);

    nmo_object_query_t regex = {
        .name = "^Main.*ra$",
        .name_mode = NMO_OBJECT_QUERY_NAME_REGEX,
        .name_case_insensitive = false
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(g_repo, &regex, NULL, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    teardown_objects();
}

TEST(object_query, predicate_and_collect)
{
    setup_objects();
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_object_query_t query = {
        .name = "player",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true,
        .predicate = only_non_mesh
    };
    nmo_object_t **objects = NULL;
    size_t count = 0;
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_collect(
        g_repo, &query, NULL, arena, &objects, &count, &result));
    ASSERT_EQ(1, count);
    ASSERT_EQ(1, result.matched);
    ASSERT_EQ(g_entity, objects[0]);

    nmo_arena_destroy(arena);
    teardown_objects();
}

TEST(object_query, visitor_can_stop_early)
{
    setup_objects();

    size_t seen = 0;
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(
        g_repo, NULL, NULL, stop_after_first, &seen, &result));
    ASSERT_EQ(4, result.total);
    ASSERT_EQ(1, result.matched);
    ASSERT_EQ(1, result.visited);
    ASSERT_TRUE(result.stopped_early);
    ASSERT_EQ(1, seen);

    teardown_objects();
}

TEST_MAIN_BEGIN()
REGISTER_TEST(object_query, filters_by_object_id);
REGISTER_TEST(object_query, exact_and_derived_class_matching);
REGISTER_TEST(object_query, name_modes_share_case_rules);
REGISTER_TEST(object_query, predicate_and_collect);
REGISTER_TEST(object_query, visitor_can_stop_early);
TEST_MAIN_END()
