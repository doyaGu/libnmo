/**
 * @file test_file_checksum.c
 * @brief The file header checksum is computed on save and compared on load
 *
 * CK2 refuses a file whose header checksum differs from the one computed over
 * the file bytes (CKERR_FILECRCERROR). libnmo opens such a file, records the
 * difference in the file info, and fails only under NMO_LOAD_VERIFY_CRC.
 */

#include "../test_framework.h"

#include "runtime/nmo_document.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "object/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_serializer.h"
#include "session/nmo_session.h"
#include "../../src/runtime/runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH_FILE "test_file_checksum_scratch.nmo"

/* The only corpus file whose stored checksum is wrong: a repeated independent
 * computation (Adler32 over both header parts, packed Header1 and packed data)
 * gives 0x196CA3FC, not the 0xC6C7A400 in its header. */
#define STALE_CRC_FILE "Ballance/base.cmo"

static nmo_context_t *make_context(void)
{
    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    return nmo_context_create(&desc);
}

static int load_info(nmo_context_t *ctx, const char *path, uint32_t flags,
                     nmo_file_info_t *out_info)
{
    nmo_load_options_t options = nmo_load_options_default();
    options.flags = (nmo_load_flags_t)flags;
    nmo_document_t *document = NULL;
    int status = nmo_document_load_file(ctx, path, &options, &document);
    if (status == NMO_OK) {
        if (out_info != NULL) {
            *out_info = nmo_document_get_file_info(document);
        }
        nmo_document_destroy(document);
    }
    return status;
}

static unsigned char *read_whole_file(const char *path, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *bytes = (unsigned char *)malloc((size_t)size);
    if (bytes != NULL && fread(bytes, 1, (size_t)size, fp) != (size_t)size) {
        free(bytes);
        bytes = NULL;
    }
    fclose(fp);
    *out_size = (size_t)size;
    return bytes;
}

static int write_whole_file(const char *path, const unsigned char *bytes, size_t size)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return -1;
    int ok = fwrite(bytes, 1, size, fp) == size;
    fclose(fp);
    return ok ? 0 : -1;
}

/* A document holding one object, saved with the defaults. */
static int save_generated(nmo_context_t *ctx, const char *path)
{
    nmo_document_t *document = nmo_document_create(ctx);
    if (document == NULL) return NMO_ERR_NOMEM;
    nmo_workspace_t *workspace = NULL;
    int status = nmo_workspace_create(ctx, document, &workspace);
    if (status == NMO_OK) {
        nmo_object_id_t id = 0;
        status = nmo_session_create_object(nmo_workspace_internal_session(workspace), 1,
                                           "checksum-object", (nmo_guid_t){0, 0}, &id, NULL);
    }
    if (status == NMO_OK) {
        status = nmo_document_save_file(document, path, NULL);
    }
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    return status;
}

/* Flip one byte at `offset` of a saved file and return the path of the copy. */
static void write_tampered_copy(size_t offset)
{
    size_t size = 0;
    unsigned char *bytes = read_whole_file(SCRATCH_FILE, &size);
    ASSERT_NOT_NULL(bytes);
    ASSERT_GT(size, offset);
    bytes[offset] ^= 0x5Au;
    ASSERT_EQ(0, write_whole_file(SCRATCH_FILE ".bad", bytes, size));
    free(bytes);
}

TEST(file_checksum, saved_file_carries_a_checksum_that_verifies)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(NMO_OK, save_generated(ctx, SCRATCH_FILE));

    nmo_file_info_t info;
    memset(&info, 0, sizeof(info));
    ASSERT_EQ(NMO_OK, load_info(ctx, SCRATCH_FILE, NMO_LOAD_VERIFY_CRC, &info));
    ASSERT_EQ(NMO_CRC_OK, info.crc_status);
    ASSERT_EQ(info.crc_stored, info.crc_computed);
    ASSERT_NE(0u, info.crc_stored);
    nmo_context_release(ctx);
}

TEST(file_checksum, a_changed_field_the_checksum_covers_is_reported_and_refused_only_on_request)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(NMO_OK, save_generated(ctx, SCRATCH_FILE));

    /* The product build in the second header part: the loader does not use it, the
     * checksum covers it, so the file still loads. */
    write_tampered_copy(56);
    nmo_file_info_t info;
    memset(&info, 0, sizeof(info));
    ASSERT_EQ(NMO_OK, load_info(ctx, SCRATCH_FILE ".bad", NMO_LOAD_DEFAULT, &info));
    ASSERT_EQ(NMO_CRC_MISMATCH, info.crc_status);
    ASSERT_NE(info.crc_stored, info.crc_computed);
    ASSERT_EQ(NMO_ERR_CHECKSUM_MISMATCH,
              load_info(ctx, SCRATCH_FILE ".bad", NMO_LOAD_VERIFY_CRC, NULL));

    /* The stored checksum itself. */
    write_tampered_copy(8);
    ASSERT_EQ(NMO_OK, load_info(ctx, SCRATCH_FILE ".bad", NMO_LOAD_DEFAULT, &info));
    ASSERT_EQ(NMO_CRC_MISMATCH, info.crc_status);
    ASSERT_EQ(NMO_ERR_CHECKSUM_MISMATCH,
              load_info(ctx, SCRATCH_FILE ".bad", NMO_LOAD_VERIFY_CRC, NULL));

    remove(SCRATCH_FILE ".bad");
    remove(SCRATCH_FILE);
    nmo_context_release(ctx);
}

typedef struct corpus_crc {
    nmo_context_t *ctx;
    size_t files;
    size_t verified;
    size_t mismatched;
    size_t unexpected;
    size_t load_errors;
} corpus_crc_t;

static int ends_with(const char *text, const char *suffix)
{
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static void check_corpus_file(const char *path, void *user)
{
    corpus_crc_t *stats = (corpus_crc_t *)user;
    stats->files++;
    nmo_file_info_t info;
    memset(&info, 0, sizeof(info));
    if (load_info(stats->ctx, path, NMO_LOAD_DEFAULT, &info) != NMO_OK) {
        stats->load_errors++;
        return;
    }
    if (info.crc_status == NMO_CRC_OK) {
        stats->verified++;
    } else if (info.crc_status == NMO_CRC_MISMATCH) {
        stats->mismatched++;
        if (!ends_with(path, STALE_CRC_FILE)) {
            stats->unexpected++;
            printf("  Unexpected checksum mismatch: %s (stored %08X, computed %08X)\n",
                   path, (unsigned)info.crc_stored, (unsigned)info.crc_computed);
        }
    } else {
        stats->unexpected++;
        printf("  Checksum not checked: %s\n", path);
    }
}

TEST(file_checksum, corpus_files_carry_the_checksum_the_engine_computes)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE(STALE_CRC_FILE);

    corpus_crc_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = make_context();
    ASSERT_NOT_NULL(stats.ctx);
    ASSERT_EQ(0, test_corpus_walk(NMO_TEST_DATA_DIR, check_corpus_file, &stats));
    nmo_context_release(stats.ctx);

    printf("  Checksums: files=%zu verified=%zu mismatched=%zu load_errors=%zu\n",
           stats.files, stats.verified, stats.mismatched, stats.load_errors);
    ASSERT_GE(stats.files, 1u);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.unexpected);
    /* The one file with a wrong checksum is the reason the load only warns. */
    ASSERT_EQ(1u, stats.mismatched);
    ASSERT_EQ(stats.files, stats.verified + stats.mismatched);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(file_checksum, saved_file_carries_a_checksum_that_verifies);
    REGISTER_TEST(file_checksum, a_changed_field_the_checksum_covers_is_reported_and_refused_only_on_request);
    REGISTER_TEST(file_checksum, corpus_files_carry_the_checksum_the_engine_computes);
TEST_MAIN_END()
