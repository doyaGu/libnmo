/**
 * @file test_mmap_load.c
 * @brief Integration test for the mmap load path on real files
 *
 * The loader maps a file into memory when neither of its sections is
 * compressed. Almost every sample is compressed; the two below are not. They are
 * local, git-ignored files; without them the test is skipped.
 */

#include "../test_framework.h"
#include "document/nmo_document_load.h"
#include "object/nmo_context.h"
#include "format/nmo_header.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_mmap.h"

static int file_is_compressed(const char *path) {
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        return -1;
    }

    nmo_file_header_t header;
    nmo_status_t result = nmo_file_header_parse(io, &header);
    nmo_io_close(io);

    if (result != NMO_OK) {
        return -1;
    }

    const uint32_t compression_mask =
        NMO_FILE_WRITE_CHUNK_COMPRESSED_OLD |
        NMO_FILE_WRITE_WHOLE_COMPRESSED;
    int is_compressed = (header.file_write_mode & compression_mask) != 0;

    if (header.hdr1_pack_size != header.hdr1_unpack_size) {
        is_compressed = 1;
    }

    if (header.data_pack_size != header.data_unpack_size) {
        is_compressed = 1;
    }

    return is_compressed;
}

TEST(mmap_load, uncompressed_samples_load) {
    if (!nmo_io_mmap_supported()) {
        TEST_SKIP("mmap is not supported on this platform");
    }

    static const char *const samples[] = {
        NMO_TEST_DATA_FILE("TechnicalSamples/VSL/Documentation samples/Hello World.cmo"),
        NMO_TEST_DATA_FILE("BBSamples/Lights/Static Lightmap.cmo"),
    };

    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        TEST_REQUIRE_FILE(samples[i]);
        /* A compressed file would take the buffered path instead. */
        ASSERT_EQ(0, file_is_compressed(samples[i]));

        nmo_context_t *ctx = nmo_context_create(NULL);
        ASSERT_NOT_NULL(ctx);

        nmo_load_options_t opts = nmo_load_options_default();
        nmo_document_t *document = NULL;
        const int result = nmo_document_load_file(ctx, samples[i], &opts, &document);
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
    REGISTER_TEST(mmap_load, uncompressed_samples_load);
TEST_MAIN_END()
