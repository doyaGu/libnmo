/**
 * @file test_load_options.c
 * @brief Load profile tests
 */

#include "../test_framework.h"

#include "document/nmo_document_load.h"
#include "document/nmo_document.h"
#include "document/nmo_document_save.h"
#include "io/nmo_io_file.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_serializer.h"
#include "session/nmo_session.h"
#include "session/nmo_session_bridge.h"
#include "runtime/nmo_workspace.h"

#include <stdio.h>

static void destroy_ctx_session(nmo_context_t *ctx, nmo_session_t *session) {
    if (session != NULL) {
        nmo_session_destroy(session);
    }
    if (ctx != NULL) {
        nmo_context_release(ctx);
    }
}

static int load_fixture_with_profile(
    const char *path,
    nmo_load_profile_t profile,
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    nmo_load_perf_stats_t *out_stats
) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (ctx == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        nmo_context_release(ctx);
        return NMO_ERR_NOMEM;
    }

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = profile;
    opts.collect_perf_stats = true;
    opts.perf_stats = out_stats;

    int st = nmo_session_load_file(session, path, &opts, NULL);
    if (st != NMO_OK) {
        destroy_ctx_session(ctx, session);
        return st;
    }

    *out_ctx = ctx;
    *out_session = session;
    return NMO_OK;
}

TEST(load_options, metadata_profile_stops_after_header_and_rejects_mutation)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_METADATA,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    nmo_file_info_t info = nmo_session_get_file_info(session);
    ASSERT_TRUE(info.object_count > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_READ].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_PARSE].calls > 0);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_DATA_READ].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_DATA_PARSE].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_OBJECT_DESERIALIZE].calls);
    ASSERT_TRUE(nmo_session_is_partial_load(session));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    ASSERT_EQ((size_t)0, nmo_object_repository_get_count(repo));

    remove("test_metadata_profile_should_not_save.nmo");
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_session_save_file(session,
                                    "test_metadata_profile_should_not_save.nmo",
                                    NULL,
                                    NULL));
    remove("test_metadata_profile_direct_save_should_not_save.nmo");
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_save_file(session,
                            "test_metadata_profile_direct_save_should_not_save.nmo",
                            NULL));

    nmo_object_id_t created_id = 0;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_session_create_object(session,
                                        NMO_CID_OBJECT,
                                        "blocked",
                                        (nmo_guid_t){0, 0},
                                        &created_id,
                                        NULL));

    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_workspace_edit_begin(workspace, "blocked", &edit));
    ASSERT_NULL(edit);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);

    destroy_ctx_session(ctx, session);
}

TEST(load_options, partial_profile_rejects_non_empty_session)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    int st = nmo_load_file(session, "data/Ballance/Gameplay.nmo", NULL);
    if (st != NMO_OK) {
        destroy_ctx_session(ctx, session);
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    size_t original_count = nmo_object_repository_get_count(repo);
    ASSERT_TRUE(original_count > 0);
    ASSERT_FALSE(nmo_session_is_partial_load(session));

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_load_file(session, "data/Ballance/Camera.nmo", &opts));
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    ASSERT_EQ(original_count, nmo_object_repository_get_count(repo));

    destroy_ctx_session(ctx, session);
}

TEST(load_options, partial_profile_rejects_non_object_session_state)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    const char payload[] = "stale";
    ASSERT_EQ(NMO_OK,
              nmo_session_add_included_file(session,
                                            "stale.bin",
                                            payload,
                                            (uint32_t)sizeof(payload)));
    uint32_t included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_load_file(session, "data/Ballance/Camera.nmo", &opts));
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    destroy_ctx_session(ctx, session);
}

TEST(load_options, phased_partial_profile_rejects_non_empty_session)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    int st = nmo_load_file(session, "data/Ballance/Gameplay.nmo", NULL);
    if (st != NMO_OK) {
        destroy_ctx_session(ctx, session);
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    size_t original_count = nmo_object_repository_get_count(repo);
    ASSERT_TRUE(original_count > 0);
    ASSERT_FALSE(nmo_session_is_partial_load(session));

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    nmo_io_interface_t *io = nmo_file_io_open("data/Ballance/Camera.nmo", NMO_IO_READ);
    ASSERT_NOT_NULL(io);
    nmo_deserializer_t *ds = nmo_deserializer_create(session, io, &opts);
    ASSERT_NOT_NULL(ds);

    nmo_status_t parse_result = nmo_deserializer_parse_header(ds);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, parse_result);
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    ASSERT_EQ(original_count, nmo_object_repository_get_count(repo));

    nmo_deserializer_destroy(ds);
    destroy_ctx_session(ctx, session);
}

TEST(load_options, phased_partial_profile_rejects_non_object_session_state)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    const char payload[] = "stale";
    ASSERT_EQ(NMO_OK,
              nmo_session_add_included_file(session,
                                            "stale.bin",
                                            payload,
                                            (uint32_t)sizeof(payload)));
    uint32_t included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    nmo_io_interface_t *io = nmo_file_io_open("data/Ballance/Camera.nmo", NMO_IO_READ);
    ASSERT_NOT_NULL(io);
    nmo_deserializer_t *ds = nmo_deserializer_create(session, io, &opts);
    ASSERT_NOT_NULL(ds);

    nmo_status_t parse_result = nmo_deserializer_parse_header(ds);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, parse_result);
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    nmo_deserializer_destroy(ds);
    destroy_ctx_session(ctx, session);
}

TEST(load_options, two_phase_serializer_rejects_partial_session)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_METADATA,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    ASSERT_TRUE(nmo_session_is_partial_load(session));
    nmo_save_options_t save_opts = nmo_save_options_default();
    nmo_serializer_t *serializer = nmo_serializer_create(session, &save_opts);
    ASSERT_NOT_NULL(serializer);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, nmo_serializer_layout(serializer));

    nmo_serializer_destroy(serializer);
    destroy_ctx_session(ctx, session);
}

TEST(load_options, header_only_profile_stops_after_header)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_HEADER_ONLY,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    nmo_file_info_t info = nmo_session_get_file_info(session);
    ASSERT_TRUE(info.object_count > 0);
    ASSERT_TRUE(nmo_session_get_header(session) != NULL);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_PARSE].calls > 0);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_DATA_READ].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls);
    ASSERT_TRUE(nmo_session_is_partial_load(session));

    destroy_ctx_session(ctx, session);
}

TEST(load_options, full_profile_is_default)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_FULL,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_DATA_READ].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OBJECT_DESERIALIZE].calls > 0);
    ASSERT_FALSE(nmo_session_is_partial_load(session));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    ASSERT_TRUE(nmo_object_repository_get_count(repo) > 0);

    destroy_ctx_session(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(load_options, metadata_profile_stops_after_header_and_rejects_mutation);
    REGISTER_TEST(load_options, partial_profile_rejects_non_empty_session);
    REGISTER_TEST(load_options, partial_profile_rejects_non_object_session_state);
    REGISTER_TEST(load_options, phased_partial_profile_rejects_non_empty_session);
    REGISTER_TEST(load_options, phased_partial_profile_rejects_non_object_session_state);
    REGISTER_TEST(load_options, two_phase_serializer_rejects_partial_session);
    REGISTER_TEST(load_options, header_only_profile_stops_after_header);
    REGISTER_TEST(load_options, full_profile_is_default);
TEST_MAIN_END()


