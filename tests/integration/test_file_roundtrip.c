/**
 * @file test_file_roundtrip.c
 * @brief Integration test for loading files through the document API
 *
 * Loads the Nop samples of the data directory and checks that a missing file is
 * reported as such. The samples are local, git-ignored files; without them the
 * sample test is skipped.
 */

#include "../test_framework.h"
#include "document/nmo_document_load.h"
#include "runtime/nmo_context.h"

static nmo_context_t *create_context(void) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    return nmo_context_create(&desc);
}

TEST(file_roundtrip, missing_file_is_reported) {
    nmo_context_t *ctx = create_context();
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = NULL;
    const int result = nmo_document_load_file(
        ctx, NMO_TEST_SCRATCH_FILE("nonexistent_file.nmo"), NULL, &document);
    nmo_context_release(ctx);
    ASSERT_EQ(NMO_ERR_FILE_NOT_FOUND, result);
}

TEST(file_roundtrip, nop_samples_load) {
    static const char *const samples[] = {
        NMO_TEST_DATA_FILE("Nop.cmo"),
        NMO_TEST_DATA_FILE("Nop1.cmo"),
        NMO_TEST_DATA_FILE("Nop2.cmo"),
    };

    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        TEST_REQUIRE_FILE(samples[i]);
        nmo_context_t *ctx = create_context();
        ASSERT_NOT_NULL(ctx);

        nmo_document_t *document = NULL;
        const int result = nmo_document_load_file(ctx, samples[i], NULL, &document);
        if (result == NMO_OK) {
            nmo_document_destroy(document);
        } else {
            printf("  %s: load failed with %d\n", samples[i], result);
        }
        nmo_context_release(ctx);
        ASSERT_EQ(NMO_OK, result);
    }
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(file_roundtrip, missing_file_is_reported);
    REGISTER_TEST(file_roundtrip, nop_samples_load);
TEST_MAIN_END()
