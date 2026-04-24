/**
 * @file test_script_walker.c
 * @brief Unit tests for script walker API (nmo_script_walker.h)
 *
 * Tests the script discovery, behavior tree walking, and parameter tracing
 * APIs. Since these depend on loaded file data, some tests use synthetic
 * contexts while others require test data files.
 */

#include "test_framework.h"
#include "nmo.h"
#include "behavior/nmo_behavior_analyze.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "core/nmo_allocator.h"
#include "core/nmo_array.h"
#include "object/nmo_class_ids.h"

#include <string.h>

typedef struct fail_after_alloc_ctx {
    size_t calls;
    size_t fail_after;
} fail_after_alloc_ctx_t;

static void *fail_after_alloc(void *user_data, size_t size, size_t alignment)
{
    (void)alignment;
    fail_after_alloc_ctx_t *ctx = (fail_after_alloc_ctx_t *)user_data;
    if (ctx && ctx->calls++ >= ctx->fail_after) {
        return NULL;
    }
    return malloc(size);
}

static void fail_after_free(void *user_data, void *ptr)
{
    (void)user_data;
    free(ptr);
}

static nmo_status_t collect_scripts_from_session(
    nmo_session_t *session,
    nmo_array_t *scripts)
{
    nmo_document_t *document = NULL;
    nmo_status_t st = nmo_session_borrow_document(session, &document);
    if (st != NMO_OK) {
        return st;
    }
    st = nmo_behavior_query_collect_scripts(document, scripts);
    nmo_document_destroy(document);
    return st;
}

static nmo_status_t open_workspace_from_session(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_document_t **out_document,
    nmo_workspace_t **out_workspace)
{
    nmo_status_t st = nmo_session_borrow_document(session, out_document);
    if (st != NMO_OK) {
        return st;
    }
    st = nmo_workspace_create(ctx, *out_document, out_workspace);
    if (st != NMO_OK) {
        nmo_document_destroy(*out_document);
        *out_document = NULL;
    }
    return st;
}

/* ============================================================================
 * Tests: NULL argument handling
 * ============================================================================ */

TEST(script_walker, find_scripts_null_args) {
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 4, NULL);

    nmo_status_t st = nmo_behavior_query_collect_scripts(NULL, &scripts);
    ASSERT_NE(NMO_OK, st);

    nmo_array_dispose(&scripts);
}

TEST(script_walker, walk_null_args) {
    nmo_status_t st = nmo_behavior_walk(NULL, 1, NULL, NULL);
    ASSERT_NE(NMO_OK, st);
}

TEST(script_walker, trace_null_args) {
    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 4, NULL);

    nmo_status_t st = nmo_behavior_analyze_trace_param_chain(
        NULL, 1, &chain, 32);
    ASSERT_NE(NMO_OK, st);

    nmo_array_dispose(&chain);
}

TEST(script_walker, dump_text_null_args) {
    nmo_status_t st = nmo_behavior_analyze_dump_text(NULL, 1, NULL);
    ASSERT_NE(NMO_OK, st);
}

/* ============================================================================
 * Tests: script discovery with real file (if available)
 * ============================================================================ */

TEST(script_walker, find_scripts_with_file) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    const char *file = NMO_TEST_DATA_FILE("Nop.cmo");
    bool ok = nmo_session_open_file_with_context(
        file, &ctx, &session, errbuf, sizeof(errbuf));
    if (!ok) {
        /* Test data not available - skip gracefully */
        return;
    }

    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 4, NULL);

    nmo_status_t st = collect_scripts_from_session(session, &scripts);
    ASSERT_EQ(NMO_OK, st);
    /* Nop.cmo should have at least one script */
    ASSERT_TRUE(scripts.count > 0);

    /* Verify entry structure */
    const nmo_behavior_script_view_t *entry =
        (const nmo_behavior_script_view_t *)nmo_array_get(&scripts, 0);
    ASSERT_NOT_NULL(entry);
    ASSERT_NE(0, (long long)entry->script_id);
    ASSERT_NE(0, (long long)entry->owner_id);

    nmo_array_dispose(&scripts);
    nmo_session_close_with_context(ctx, session);
}

TEST(script_walker, find_scripts_reports_append_oom) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    const char *file = NMO_TEST_DATA_FILE("Nop.cmo");
    bool ok = nmo_session_open_file_with_context(
        file, &ctx, &session, errbuf, sizeof(errbuf));
    if (!ok) {
        return;
    }

    fail_after_alloc_ctx_t alloc_ctx = {0, 0};
    nmo_allocator_t fail_alloc =
        nmo_allocator_custom(fail_after_alloc, fail_after_free, &alloc_ctx);

    nmo_array_t scripts;
    ASSERT_EQ(NMO_OK, nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 0, &fail_alloc));

    nmo_status_t st = collect_scripts_from_session(session, &scripts);
    ASSERT_EQ(NMO_ERR_NOMEM, st);
    ASSERT_EQ(0u, scripts.count);

    nmo_array_dispose(&scripts);
    nmo_session_close_with_context(ctx, session);
}

TEST(script_walker, walk_with_file) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    const char *file = NMO_TEST_DATA_FILE("Nop.cmo");
    bool ok = nmo_session_open_file_with_context(
        file, &ctx, &session, errbuf, sizeof(errbuf));
    if (!ok) return;

    /* Find scripts first */
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 4, NULL);
    collect_scripts_from_session(session, &scripts);

    if (scripts.count == 0) {
        nmo_array_dispose(&scripts);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    const nmo_behavior_script_view_t *entry =
        (const nmo_behavior_script_view_t *)nmo_array_get(&scripts, 0);

    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, open_workspace_from_session(ctx, session, &document, &workspace));

    /* Walk the first script; NULL visitor should fail */
    nmo_status_t st = nmo_behavior_walk(
        workspace, entry->script_id, NULL, NULL);
    /* NULL visitor should be caught */
    ASSERT_NE(NMO_OK, st);

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_array_dispose(&scripts);
    nmo_session_close_with_context(ctx, session);
}

TEST(script_walker, dump_text_with_file) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    const char *file = NMO_TEST_DATA_FILE("Nop.cmo");
    bool ok = nmo_session_open_file_with_context(
        file, &ctx, &session, errbuf, sizeof(errbuf));
    if (!ok) return;

    /* Find scripts */
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 4, NULL);
    collect_scripts_from_session(session, &scripts);

    if (scripts.count > 0) {
        const nmo_behavior_script_view_t *entry =
            (const nmo_behavior_script_view_t *)nmo_array_get(&scripts, 0);

        nmo_document_t *document = NULL;
        nmo_workspace_t *workspace = NULL;
        ASSERT_EQ(NMO_OK, open_workspace_from_session(ctx, session, &document, &workspace));

        /* Dump to /dev/null to verify no crashes */
        FILE *devnull = fopen("/dev/null", "w");
        if (devnull) {
            nmo_status_t st = nmo_behavior_analyze_dump_text(
                workspace, entry->script_id, devnull);
            ASSERT_EQ(NMO_OK, st);
            fclose(devnull);
        }

        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
    }

    nmo_array_dispose(&scripts);
    nmo_session_close_with_context(ctx, session);
}

/* ============================================================================
 * Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_walker, find_scripts_null_args);
    REGISTER_TEST(script_walker, walk_null_args);
    REGISTER_TEST(script_walker, trace_null_args);
    REGISTER_TEST(script_walker, dump_text_null_args);
    REGISTER_TEST(script_walker, find_scripts_with_file);
    REGISTER_TEST(script_walker, find_scripts_reports_append_oom);
    REGISTER_TEST(script_walker, walk_with_file);
    REGISTER_TEST(script_walker, dump_text_with_file);
TEST_MAIN_END()

