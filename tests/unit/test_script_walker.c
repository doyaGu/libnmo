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
#include "behavior/nmo_script_walker.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "core/nmo_array.h"
#include "object/nmo_class_ids.h"

#include <string.h>

/* ============================================================================
 * Tests: NULL argument handling
 * ============================================================================ */

TEST(script_walker, find_scripts_null_args) {
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 4, NULL);

    nmo_status_t st = nmo_script_walker_find_scripts(NULL, NULL, &scripts);
    ASSERT_NE(NMO_OK, st);

    nmo_array_dispose(&scripts);
}

TEST(script_walker, walk_null_args) {
    nmo_status_t st = nmo_script_walker_walk(NULL, NULL, 1, NULL, NULL);
    ASSERT_NE(NMO_OK, st);
}

TEST(script_walker, trace_null_args) {
    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_param_chain_step_t), 4, NULL);

    nmo_status_t st = nmo_script_walker_trace_param_chain(
        NULL, NULL, 1, &chain, 32);
    ASSERT_NE(NMO_OK, st);

    nmo_array_dispose(&chain);
}

TEST(script_walker, dump_text_null_args) {
    nmo_status_t st = nmo_script_walker_dump_text(NULL, NULL, 1, NULL);
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
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 4, NULL);

    nmo_status_t st = nmo_script_walker_find_scripts(ctx, session, &scripts);
    ASSERT_EQ(NMO_OK, st);
    /* Nop.cmo should have at least one script */
    ASSERT_TRUE(scripts.count > 0);

    /* Verify entry structure */
    const nmo_script_entry_t *entry =
        (const nmo_script_entry_t *)nmo_array_get(&scripts, 0);
    ASSERT_NOT_NULL(entry);
    ASSERT_NE(0, (long long)entry->script_id);
    ASSERT_NE(0, (long long)entry->owner_id);

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
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 4, NULL);
    nmo_script_walker_find_scripts(ctx, session, &scripts);

    if (scripts.count == 0) {
        nmo_array_dispose(&scripts);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    const nmo_script_entry_t *entry =
        (const nmo_script_entry_t *)nmo_array_get(&scripts, 0);

    /* Walk the first script — NULL visitor should fail */
    nmo_status_t st = nmo_script_walker_walk(
        ctx, session, entry->script_id,
        /* visitor: */ NULL, NULL);
    /* NULL visitor should be caught */
    ASSERT_NE(NMO_OK, st);

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
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 4, NULL);
    nmo_script_walker_find_scripts(ctx, session, &scripts);

    if (scripts.count > 0) {
        const nmo_script_entry_t *entry =
            (const nmo_script_entry_t *)nmo_array_get(&scripts, 0);

        /* Dump to /dev/null to verify no crashes */
        FILE *devnull = fopen("/dev/null", "w");
        if (devnull) {
            nmo_status_t st = nmo_script_walker_dump_text(
                ctx, session, entry->script_id, devnull);
            ASSERT_EQ(NMO_OK, st);
            fclose(devnull);
        }
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
    REGISTER_TEST(script_walker, walk_with_file);
    REGISTER_TEST(script_walker, dump_text_with_file);
TEST_MAIN_END()
