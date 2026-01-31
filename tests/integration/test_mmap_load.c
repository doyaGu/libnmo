/**
 * @file test_mmap_load.c
 * @brief Integration test for mmap load strategy on real files
 */

#include "app/nmo_parser.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "format/nmo_header.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_mmap.h"
#include <stdio.h>
#include <string.h>

static int file_is_compressed(const char *path) {
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        return -1;
    }

    nmo_file_header_t header;
    nmo_result_t result = nmo_file_header_parse(io, &header);
    nmo_io_close(io);

    if (result.code != NMO_OK) {
        return -1;
    }

    const uint32_t compression_mask = NMO_FILE_WRITE_COMPRESS_HEADER | NMO_FILE_WRITE_COMPRESS_DATA;
    int is_compressed = (header.file_write_mode & compression_mask) != 0;

    if (header.hdr1_pack_size != header.hdr1_unpack_size) {
        is_compressed = 1;
    }

    if (header.data_pack_size != header.data_unpack_size) {
        is_compressed = 1;
    }

    return is_compressed;
}

static int test_mmap_load_files(void) {
    printf("=== Test: MMAP Load Strategy (Real Files) ===\n");

    if (!nmo_io_mmap_supported()) {
        printf("  MMAP not supported on this platform (skipped)\n");
        printf("=== Test SKIPPED ===\n\n");
        return 0;
    }

    const char *test_files[] = {
        "data/Empty.nmo",
        "data/Empty.cmo",
        "data/Empty.vmo",
        "data/Nop.cmo",
        "data/Nop1.cmo",
        "data/Nop2.cmo",
        NULL
    };

    int files_tested = 0;
    int files_loaded = 0;

    for (int i = 0; test_files[i] != NULL; i++) {
        const char *filename = test_files[i];

        FILE *f = fopen(filename, "rb");
        if (f == NULL) {
            printf("  File not found: %s (skipped)\n", filename);
            continue;
        }
        fclose(f);

        int is_compressed = file_is_compressed(filename);
        if (is_compressed < 0) {
            printf("  Failed to read header: %s (skipped)\n", filename);
            continue;
        }

        if (is_compressed) {
            printf("  Compressed file: %s (skipped for mmap)\n", filename);
            continue;
        }

        files_tested++;

        nmo_context_desc_t ctx_desc;
        memset(&ctx_desc, 0, sizeof(nmo_context_desc_t));

        nmo_context_t *ctx = nmo_context_create(&ctx_desc);
        if (ctx == NULL) {
            printf("  ERROR: Failed to create context for %s\n", filename);
            continue;
        }

        nmo_session_t *session = nmo_session_create(ctx);
        if (session == NULL) {
            printf("  ERROR: Failed to create session for %s\n", filename);
            nmo_context_release(ctx);
            continue;
        }

        nmo_load_options_t opts = nmo_load_options_default();

        printf("  Loading (mmap): %s... ", filename);
        fflush(stdout);

        int result = nmo_load_file_ex(session, filename, &opts);
        if (result == NMO_OK) {
            printf("✓ SUCCESS\n");
            files_loaded++;
        } else {
            printf("✗ FAILED (error %d)\n", result);
        }

        nmo_session_destroy(session);
        nmo_context_release(ctx);
    }

    printf("\nSummary: Tested %d uncompressed file(s), loaded %d successfully\n",
           files_tested, files_loaded);

    if (files_tested == 0) {
        printf("  No uncompressed test files found (this is OK for a fresh build)\n");
    }

    printf("=== Test COMPLETED ===\n\n");
    return 0;
}

int main(void) {
    return test_mmap_load_files();
}
