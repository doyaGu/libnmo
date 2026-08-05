/**
 * @file test_object_query.c
 * @brief Unit tests for library-level object query API
 */

#include "test_framework.h"
#include "document/nmo_document.h"
#include "object/nmo_object_query.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"
#include "../../src/runtime/runtime_internal.h"
#include "core/nmo_guid.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "type/nmo_type_system.h"

static nmo_context_t *g_ctx;
static nmo_object_repository_t *g_repo;
static nmo_object_query_index_t *g_query_index;
static nmo_object_t *g_base;
static nmo_object_t *g_entity;
static nmo_object_t *g_camera;
static nmo_object_t *g_mesh;

static const nmo_guid_t TEST_GUID_SHARED = NMO_GUID_INIT(0x11111111u, 0x22222222u);
static const nmo_guid_t TEST_GUID_ENTITY = NMO_GUID_INIT(0x33333333u, 0x44444444u);
static const nmo_guid_t TEST_GUID_MISSING = NMO_GUID_INIT(0x55555555u, 0x66666666u);

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
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(entity, TEST_GUID_ENTITY));
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(camera, TEST_GUID_SHARED));
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(mesh, TEST_GUID_SHARED));

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

    g_query_index = nmo_object_query_index_create(
        g_repo,
        nmo_context_get_type_registry(g_ctx),
        &allocator);
    ASSERT_NOT_NULL(g_query_index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(g_query_index));
}

static void teardown_objects(void)
{
    nmo_object_repository_destroy(g_repo);
    g_repo = NULL;
    nmo_object_query_index_destroy(g_query_index);
    g_query_index = NULL;
    g_base = NULL;
    g_entity = NULL;
    g_camera = NULL;
    g_mesh = NULL;
    nmo_context_release(g_ctx);
    g_ctx = NULL;
}

static nmo_object_query_context_t query_ctx(void)
{
    nmo_object_query_context_t ctx = {
        .repository = g_repo,
        .index = g_query_index,
        .registry = nmo_context_get_type_registry(g_ctx)
    };
    return ctx;
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

typedef struct query_index_capture {
    size_t indexes[4];
    size_t count;
} query_index_capture_t;

static bool capture_query_index(size_t object_index, nmo_object_t *object, void *user_data)
{
    query_index_capture_t *capture = (query_index_capture_t *)user_data;
    (void)object;
    if (capture->count >= 4) {
        return false;
    }
    capture->indexes[capture->count++] = object_index;
    return true;
}

static void count_repository_mutation(
    nmo_object_repository_t *repository,
    uint32_t flags,
    void *user_data)
{
    (void)repository;
    if (flags != 0 && user_data != NULL) {
        size_t *count = (size_t *)user_data;
        (*count)++;
    }
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

    nmo_object_query_t exact = {
        .class_id = NMO_CID_3DENTITY,
        .include_derived_classes = false
    };
    nmo_object_query_result_t result = {0};
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &exact, NULL, NULL, &result));
    ASSERT_EQ(4, result.total);
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t derived = {
        .class_id = NMO_CID_3DENTITY,
        .include_derived_classes = true
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &derived, NULL, NULL, &result));
    ASSERT_EQ(2, result.matched);

    teardown_objects();
}

TEST(object_query, derived_class_query_without_registry_reports_error)
{
    setup_objects();
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_query_index_t *index =
        nmo_object_query_index_create(g_repo, NULL, &allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    nmo_object_query_context_t ctx = {
        .repository = g_repo,
        .index = index,
        .registry = NULL
    };
    nmo_object_query_t derived = {
        .class_id = NMO_CID_3DENTITY,
        .include_derived_classes = true
    };
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_object_query_iterate(&ctx, &derived, NULL, NULL, &result));

    nmo_object_query_index_destroy(index);
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
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &contains, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t wildcard = {
        .name = "player*",
        .name_mode = NMO_OBJECT_QUERY_NAME_WILDCARD,
        .name_case_insensitive = true
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &wildcard, NULL, NULL, &result));
    ASSERT_EQ(2, result.matched);

    nmo_object_query_t regex = {
        .name = "^Main.*ra$",
        .name_mode = NMO_OBJECT_QUERY_NAME_REGEX,
        .name_case_insensitive = false
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &regex, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    teardown_objects();
}

TEST(object_query, guid_matching_filters_exact_type_guid)
{
    setup_objects();

    nmo_object_query_t query = {
        .has_type_guid = true,
        .type_guid = TEST_GUID_SHARED
    };
    bool matches = true;
    ASSERT_EQ(NMO_OK, nmo_object_query_matches(g_entity, &query, NULL, &matches));
    ASSERT_FALSE(matches);
    ASSERT_EQ(NMO_OK, nmo_object_query_matches(g_camera, &query, NULL, &matches));
    ASSERT_TRUE(matches);

    nmo_object_query_result_t result = {0};
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &query, NULL, NULL, &result));
    ASSERT_EQ(4, result.total);
    ASSERT_EQ(2, result.matched);

    teardown_objects();
}

TEST(object_query, null_guid_query_matches_no_objects)
{
    setup_objects();

    nmo_object_query_t query = {
        .has_type_guid = true,
        .type_guid = NMO_GUID_NULL
    };
    nmo_object_query_result_t result = {0};
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &query, NULL, NULL, &result));
    ASSERT_EQ(4, result.total);
    ASSERT_EQ(0, result.matched);

    teardown_objects();
}

TEST(object_query, guid_query_combines_with_class_and_name_filters)
{
    setup_objects();

    nmo_object_query_t query = {
        .class_id = NMO_CID_CAMERA,
        .include_derived_classes = false,
        .has_type_guid = true,
        .type_guid = TEST_GUID_SHARED,
        .name = "MainCamera",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
    };
    query_index_capture_t capture = {0};
    nmo_object_query_result_t result = {0};
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(
        &ctx, &query, capture_query_index, &capture, &result));
    ASSERT_EQ(1, result.matched);
    ASSERT_EQ(1, capture.count);
    ASSERT_EQ(2, capture.indexes[0]);

    nmo_object_query_t missing = query;
    missing.type_guid = TEST_GUID_MISSING;
    capture = (query_index_capture_t){0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(
        &ctx, &missing, capture_query_index, &capture, &result));
    ASSERT_EQ(0, result.matched);
    ASSERT_EQ(0, capture.count);

    teardown_objects();
}

TEST(object_query, owner_find_first_filters_by_guid)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *target = make_object(&allocator, 10, NMO_CID_OBJECT, "GuidTarget");
    ASSERT_NOT_NULL(target);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(target, TEST_GUID_SHARED));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &target));

    nmo_object_query_t query = {
        .has_type_guid = true,
        .type_guid = TEST_GUID_SHARED
    };
    nmo_object_t *found = NULL;
    size_t found_index = SIZE_MAX;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(document, &query, &found, &found_index));
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(10, nmo_object_get_id(found));
    ASSERT_EQ(0, found_index);

    nmo_object_query_t missing = {
        .has_type_guid = true,
        .type_guid = TEST_GUID_MISSING
    };
    found = (nmo_object_t *)0x1;
    found_index = 42;
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_object_query_find_first(document, &missing, &found, &found_index));
    ASSERT_NULL(found);

    nmo_document_destroy(document);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(object_query, text_reducer_marks_do_not_poison_later_queries)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *target = make_object(&allocator, 1, NMO_CID_OBJECT, "ABCDEF_target");
    nmo_object_t *partial = make_object(&allocator, 2, NMO_CID_OBJECT, "ABCDE_partial");
    nmo_object_t *abc = make_object(&allocator, 3, NMO_CID_OBJECT, "ABC_only");
    nmo_object_t *bcd = make_object(&allocator, 4, NMO_CID_OBJECT, "BCD_only");
    nmo_object_t *def = make_object(&allocator, 5, NMO_CID_OBJECT, "DEF_only");
    ASSERT_NOT_NULL(target);
    ASSERT_NOT_NULL(partial);
    ASSERT_NOT_NULL(abc);
    ASSERT_NOT_NULL(bcd);
    ASSERT_NOT_NULL(def);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &target));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &partial));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &abc));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &bcd));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &def));

    nmo_object_query_index_t *index = nmo_object_query_index_create(
        repo,
        nmo_context_get_type_registry(ctx),
        &allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    nmo_object_query_context_t qctx = {
        .repository = repo,
        .index = index,
        .registry = nmo_context_get_type_registry(ctx)
    };
    nmo_object_query_t substring = {
        .name = "abcdef",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&qctx, &substring, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t class_query = {
        .class_id = NMO_CID_OBJECT
    };
    result = (nmo_object_query_result_t){0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&qctx, &class_query, NULL, NULL, &result));
    ASSERT_EQ(5, result.matched);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
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
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_collect(
        &ctx, &query, arena, &objects, &count, &result));
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
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(
        &ctx, NULL, stop_after_first, &seen, &result));
    ASSERT_EQ(4, result.total);
    ASSERT_EQ(1, result.matched);
    ASSERT_EQ(1, result.visited);
    ASSERT_TRUE(result.stopped_early);
    ASSERT_EQ(1, seen);

    teardown_objects();
}

TEST(object_query, visitor_receives_repository_index)
{
    setup_objects();

    nmo_object_query_t query = {
        .name = "player*",
        .name_mode = NMO_OBJECT_QUERY_NAME_WILDCARD,
        .name_case_insensitive = true
    };
    query_index_capture_t capture = {0};
    nmo_object_query_result_t result = {0};
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(
        &ctx, &query, capture_query_index, &capture, &result));
    ASSERT_EQ(2, result.matched);
    ASSERT_EQ(2, capture.count);
    ASSERT_EQ(1, capture.indexes[0]);
    ASSERT_EQ(3, capture.indexes[1]);

    teardown_objects();
}

TEST(object_query, indexed_query_handles_duplicate_names_and_rename)
{
    setup_objects();
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_t *duplicate = make_object(&allocator, 5, NMO_CID_MATERIAL, "MainCamera");
    ASSERT_NOT_NULL(duplicate);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(g_repo, &duplicate));
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(g_query_index));

    nmo_object_query_t exact = {
        .name = "MainCamera",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
        .name_case_insensitive = false
    };
    nmo_object_query_result_t result = {0};
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &exact, NULL, NULL, &result));
    ASSERT_EQ(2, result.matched);

    ASSERT_EQ(NMO_OK, nmo_object_repository_rename(g_repo, 3, "RenamedCamera"));
    nmo_object_query_index_invalidate(g_query_index, NMO_OBJECT_QUERY_INDEX_NAMES);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(g_query_index));

    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &exact, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t renamed = {
        .name = "renamedcamera",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
        .name_case_insensitive = true
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &renamed, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    teardown_objects();
}

TEST(object_query, indexed_text_reducers_preserve_matches)
{
    setup_objects();

    nmo_object_query_t substring = {
        .name = "ayer_",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };
    nmo_object_query_result_t result = {0};
    nmo_object_query_context_t ctx = query_ctx();
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &substring, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t case_sensitive_substring = {
        .name = "Main",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = false
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &case_sensitive_substring, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t wildcard = {
        .name = "*Camera",
        .name_mode = NMO_OBJECT_QUERY_NAME_WILDCARD,
        .name_case_insensitive = true
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &wildcard, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    nmo_object_query_t regex = {
        .name = "Main.*ra",
        .name_mode = NMO_OBJECT_QUERY_NAME_REGEX,
        .name_case_insensitive = false
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &regex, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    teardown_objects();
}

TEST(object_query, indexed_text_single_trigram_deduplicates_repeated_names)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *object = make_object(&allocator, 1, NMO_CID_OBJECT, "aaaaaa");
    ASSERT_NOT_NULL(object);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &object));

    nmo_object_query_index_t *index = nmo_object_query_index_create(
        repo,
        nmo_context_get_type_registry(ctx),
        &allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    nmo_object_query_context_t qctx = {
        .repository = repo,
        .index = index,
        .registry = nmo_context_get_type_registry(ctx)
    };
    nmo_object_query_t query = {
        .name = "aaa",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };
    query_index_capture_t capture = {0};
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(
        &qctx, &query, capture_query_index, &capture, &result));
    ASSERT_EQ(1, result.matched);
    ASSERT_EQ(1, capture.count);
    ASSERT_EQ(0, capture.indexes[0]);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST(object_query, owner_query_api_tracks_direct_repository_mutation)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj = make_object(&allocator, 10, NMO_CID_OBJECT, "SessionObject");
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &obj));

    nmo_object_query_t query = {
        .name = "SessionObject",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
    };
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &query, &count));
    ASSERT_EQ(1u, count);

    nmo_object_query_context_t first_query_ctx = {0};
    nmo_object_query_context_t second_query_ctx = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_document_internal_init_object_query_context(
            document, &first_query_ctx));
    ASSERT_EQ(
        NMO_OK,
        nmo_document_internal_init_object_query_context(
            document, &second_query_ctx));
    ASSERT_NOT_NULL(first_query_ctx.index);
    ASSERT_EQ(first_query_ctx.index, second_query_ctx.index);

    nmo_object_t *added = make_object(&allocator, 11, NMO_CID_OBJECT, "AddedObject");
    ASSERT_NOT_NULL(added);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &added));

    nmo_object_query_t added_query = {
        .name = "AddedObject",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &added_query, &count));
    ASSERT_EQ(1u, count);

    ASSERT_EQ(NMO_OK, nmo_object_repository_rename(repo, 10, "RenamedSessionObject"));
    nmo_object_query_t renamed_query = {
        .name = "RenamedSessionObject",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &renamed_query, &count));
    ASSERT_EQ(1u, count);
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &query, &count));
    ASSERT_EQ(0u, count);

    ASSERT_EQ(NMO_OK, nmo_object_repository_remove(repo, 10));
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &renamed_query, &count));
    ASSERT_EQ(0u, count);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(object_query, owner_query_api_tracks_type_guid_mutation)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj = make_object(&allocator, 20, NMO_CID_OBJECT, "GuidObject");
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(obj, TEST_GUID_SHARED));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &obj));

    nmo_object_query_t old_guid = {
        .has_type_guid = true,
        .type_guid = TEST_GUID_SHARED
    };
    nmo_object_query_t new_guid = {
        .has_type_guid = true,
        .type_guid = TEST_GUID_MISSING
    };
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &old_guid, &count));
    ASSERT_EQ(1u, count);

    ASSERT_EQ(NMO_OK, nmo_object_repository_set_type_guid(repo, 20, TEST_GUID_MISSING));

    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &old_guid, &count));
    ASSERT_EQ(0u, count);
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &new_guid, &count));
    ASSERT_EQ(1u, count);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(object_query, stable_owner_count_and_find_first_facades)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *alpha = make_object(&allocator, 31, NMO_CID_OBJECT, "AlphaStable");
    nmo_object_t *beta = make_object(&allocator, 32, NMO_CID_OBJECT, "BetaStable");
    ASSERT_NOT_NULL(alpha);
    ASSERT_NOT_NULL(beta);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &alpha));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &beta));

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, NULL, &count));
    ASSERT_EQ(2u, count);

    nmo_object_query_t query = {
        .name = "BetaStable",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
        .name_case_insensitive = false
    };
    nmo_object_t *found = NULL;
    size_t found_index = SIZE_MAX;
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(document, &query, &found, &found_index));
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(32u, nmo_object_get_id(found));
    ASSERT_EQ(1u, found_index);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(object_query, stable_owner_resolve_one_matches_selector)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *alpha = make_object(&allocator, 41, NMO_CID_OBJECT, "Alpha");
    nmo_object_t *beta = make_object(&allocator, 42, NMO_CID_CAMERA, "Beta");
    ASSERT_NOT_NULL(alpha);
    ASSERT_NOT_NULL(beta);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &alpha));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &beta));

    nmo_object_selector_t by_name = {
        .name = "Beta",
        .required_base_class = NMO_CID_3DENTITY
    };
    nmo_object_t *found = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_resolve_one(document, &by_name, &found, NULL));
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(42u, nmo_object_get_id(found));

    nmo_object_selector_t by_id = {
        .has_id = true,
        .id = 41
    };
    found = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_resolve_one(document, &by_id, &found, NULL));
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(41u, nmo_object_get_id(found));

    nmo_object_selector_t wrong_class = {
        .has_id = true,
        .id = 41,
        .required_base_class = NMO_CID_CAMERA
    };
    found = NULL;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_object_query_resolve_one(document, &wrong_class, &found, NULL));
    ASSERT_NULL(found);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(object_query, attached_query_index_tracks_repository_mutation)
{
    setup_objects();
    ASSERT_EQ(NMO_OK, nmo_object_query_index_attach_repository_observer(g_query_index));

    nmo_object_query_context_t ctx = query_ctx();
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_t *added = make_object(&allocator, 6, NMO_CID_OBJECT, "ObserverObject");
    ASSERT_NOT_NULL(added);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(g_repo, &added));

    nmo_object_query_t added_query = {
        .name = "ObserverObject",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
    };
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &added_query, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    ASSERT_EQ(NMO_OK, nmo_object_repository_rename(g_repo, 6, "RenamedObserverObject"));
    nmo_object_query_t renamed_query = {
        .name = "RenamedObserverObject",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &renamed_query, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &added_query, NULL, NULL, &result));
    ASSERT_EQ(0, result.matched);

    ASSERT_EQ(NMO_OK, nmo_object_repository_remove(g_repo, 6));
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &renamed_query, NULL, NULL, &result));
    ASSERT_EQ(0, result.matched);

    nmo_object_query_index_detach_repository_observer(g_query_index);
    teardown_objects();
}

TEST(object_query, attached_query_index_tracks_type_guid_mutation)
{
    setup_objects();
    ASSERT_EQ(NMO_OK, nmo_object_query_index_attach_repository_observer(g_query_index));

    nmo_object_query_context_t ctx = query_ctx();
    nmo_object_query_t shared_guid = {
        .has_type_guid = true,
        .type_guid = TEST_GUID_SHARED
    };
    nmo_object_query_t changed_guid = {
        .has_type_guid = true,
        .type_guid = TEST_GUID_MISSING
    };
    nmo_object_query_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &shared_guid, NULL, NULL, &result));
    ASSERT_EQ(2, result.matched);

    ASSERT_EQ(NMO_OK, nmo_object_repository_set_type_guid(g_repo, 3, TEST_GUID_MISSING));

    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&ctx, &shared_guid, NULL, NULL, &result));
    ASSERT_EQ(1, result.matched);

    query_index_capture_t capture = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(
        &ctx, &changed_guid, capture_query_index, &capture, &result));
    ASSERT_EQ(1, result.matched);
    ASSERT_EQ(1, capture.count);
    ASSERT_EQ(2, capture.indexes[0]);

    nmo_object_query_index_detach_repository_observer(g_query_index);
    teardown_objects();
}

TEST(object_query, query_index_detach_preserves_other_repository_observers)
{
    setup_objects();
    ASSERT_EQ(NMO_OK, nmo_object_query_index_attach_repository_observer(g_query_index));

    size_t mutation_count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_repository_add_mutation_observer(
        g_repo,
        count_repository_mutation,
        &mutation_count));
    nmo_object_query_index_detach_repository_observer(g_query_index);

    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_t *added = make_object(&allocator, 7, NMO_CID_OBJECT, "ReplacementObserver");
    ASSERT_NOT_NULL(added);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(g_repo, &added));
    ASSERT_EQ(1, mutation_count);

    nmo_object_repository_remove_mutation_observer(
        g_repo,
        count_repository_mutation,
        &mutation_count);
    teardown_objects();
}

TEST_MAIN_BEGIN()
REGISTER_TEST(object_query, filters_by_object_id);
REGISTER_TEST(object_query, exact_and_derived_class_matching);
REGISTER_TEST(object_query, derived_class_query_without_registry_reports_error);
REGISTER_TEST(object_query, name_modes_share_case_rules);
REGISTER_TEST(object_query, guid_matching_filters_exact_type_guid);
REGISTER_TEST(object_query, null_guid_query_matches_no_objects);
REGISTER_TEST(object_query, guid_query_combines_with_class_and_name_filters);
REGISTER_TEST(object_query, owner_find_first_filters_by_guid);
REGISTER_TEST(object_query, text_reducer_marks_do_not_poison_later_queries);
REGISTER_TEST(object_query, predicate_and_collect);
REGISTER_TEST(object_query, visitor_can_stop_early);
REGISTER_TEST(object_query, visitor_receives_repository_index);
REGISTER_TEST(object_query, indexed_query_handles_duplicate_names_and_rename);
REGISTER_TEST(object_query, indexed_text_reducers_preserve_matches);
REGISTER_TEST(object_query, indexed_text_single_trigram_deduplicates_repeated_names);
REGISTER_TEST(object_query, owner_query_api_tracks_direct_repository_mutation);
REGISTER_TEST(object_query, owner_query_api_tracks_type_guid_mutation);
REGISTER_TEST(object_query, stable_owner_count_and_find_first_facades);
REGISTER_TEST(object_query, stable_owner_resolve_one_matches_selector);
REGISTER_TEST(object_query, attached_query_index_tracks_repository_mutation);
REGISTER_TEST(object_query, attached_query_index_tracks_type_guid_mutation);
REGISTER_TEST(object_query, query_index_detach_preserves_other_repository_observers);
TEST_MAIN_END()



