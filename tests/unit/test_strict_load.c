#include "test_framework.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_reference_resolver.h"

/**
 * Loading a valid NMO file with NMO_LOAD_STRICT should succeed
 * (well-formed files have no unresolved references).
 */
TEST(strict_load, clean_file_succeeds) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_load_options_t opts = nmo_load_options_default();
    opts.flags |= NMO_LOAD_STRICT;

    nmo_runtime_report_t report = {0};
    int result = nmo_session_load_file(session, "data/Ballance/P_Modul_01.nmo", &opts, &report);
    if (result == NMO_ERR_FILE_NOT_FOUND || result == NMO_ERR_CANT_OPEN_FILE) {
        /* Skip if test data not available */
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    ASSERT_EQ(NMO_OK, result);

    /* Verify load stats show zero unresolved */
    nmo_runtime_load_stats_t stats = {0};
    if (nmo_session_get_runtime_load_stats(session, &stats) == NMO_OK) {
        ASSERT_EQ(0u, stats.references.unresolved);
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Non-strict loading with a valid file also succeeds (regression guard).
 */
TEST(strict_load, non_strict_default_succeeds) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_runtime_report_t report = {0};
    int result = nmo_session_load_file(session, "data/Ballance/P_Modul_01.nmo", NULL, &report);
    if (result == NMO_ERR_FILE_NOT_FOUND || result == NMO_ERR_CANT_OPEN_FILE) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * When unresolved references exist and NMO_LOAD_STRICT is set,
 * finalize_load must return NMO_ERR_VALIDATION_FAILED.
 *
 * Uses the low-level API to inject an unresolvable reference,
 * then calls finalize_load with STRICT options.
 */
TEST(strict_load, unresolved_refs_fails) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Register a reference that cannot be resolved (no object with ID 99999) */
    nmo_reference_resolver_t *resolver = nmo_session_ensure_reference_resolver(session);
    ASSERT_NOT_NULL(resolver);

    nmo_object_ref_t ref = {0};
    ref.id = 99999;
    ref.class_id = 1;
    nmo_reference_resolver_register_reference(resolver, &ref);

    /* Build a finalize_load request with STRICT */
    nmo_load_options_t opts = nmo_load_options_default();
    opts.flags |= NMO_LOAD_STRICT;

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_LOAD;
    request.payload.load.options = &opts;

    nmo_runtime_report_t report = {0};
    int result = nmo_runtime_kernel_finalize_load(session, &request, &report);

    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Same unresolved reference but without STRICT should succeed.
 */
TEST(strict_load, unresolved_refs_non_strict_succeeds) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_reference_resolver_t *resolver = nmo_session_ensure_reference_resolver(session);
    ASSERT_NOT_NULL(resolver);

    nmo_object_ref_t ref = {0};
    ref.id = 99999;
    ref.class_id = 1;
    nmo_reference_resolver_register_reference(resolver, &ref);

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_LOAD;
    /* No STRICT flag â€?options is NULL */

    nmo_runtime_report_t report = {0};
    int result = nmo_runtime_kernel_finalize_load(session, &request, &report);

    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(strict_load, clean_file_succeeds);
    REGISTER_TEST(strict_load, non_strict_default_succeeds);
    REGISTER_TEST(strict_load, unresolved_refs_fails);
    REGISTER_TEST(strict_load, unresolved_refs_non_strict_succeeds);
TEST_MAIN_END()

