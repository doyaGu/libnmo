/**
 * @file test_nmo_baseline.c
 * @brief Baseline test for parsing real .nmo files
 *
 * This test validates that we can successfully parse real Virtools .nmo/.cmo files
 * from data/ directory, verifying file header structure and basic integrity.
 * Also tests Header1 parsing (object descriptors and plugin dependencies).
 */

#include "test_framework.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_compressed.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Use miniz for decompression */
#include "miniz.h"

/* File write mode flags (from CKEnums.h) */
#define CKFILE_CHUNKCOMPRESSED_OLD 1
#define CKFILE_WHOLECOMPRESSED      8

/**
 * @brief Test parsing a single .nmo file
 */
static void test_parse_file(const char* filepath, const char* expected_signature) {
    printf("\n=== Testing: %s ===\n", filepath);

    // Open file
    nmo_io_interface_t* io = nmo_file_io_open(filepath, NMO_IO_READ);
    if (io == NULL) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        ASSERT_TRUE(0); // Force failure
    }
    printf("File opened successfully\n");

    // Parse header
    nmo_file_header_t header;
    memset(&header, 0, sizeof(header));

    nmo_result_t result = nmo_file_header_parse(io, &header);
    ASSERT_EQ(NMO_OK, result.code);
    printf("Header parsed successfully\n");

    // Validate signature
    printf("Signature: %.8s\n", header.signature);
    ASSERT_STR_EQ(expected_signature, header.signature);

    // Validate header
    result = nmo_file_header_validate(&header);
    ASSERT_EQ(NMO_OK, result.code);
    printf("Header validated successfully\n");

    // Print header information
    printf("--- Header Info ---\n");
    printf("  CRC:              0x%08X\n", header.crc);
    printf("  CK Version:       0x%08X\n", header.ck_version);
    printf("  File Version:     %u\n", header.file_version);
    printf("  File Write Mode:  0x%08X\n", header.file_write_mode);
    printf("  HDR1 Pack Size:   %u bytes\n", header.hdr1_pack_size);

    // Check for Part1 (file_version >= 5)
    if (header.file_version >= 5) {
        printf("--- Part1 Info (version >= 5) ---\n");
        printf("  Data Pack Size:   %u bytes\n", header.data_pack_size);
        printf("  Data Unpack Size: %u bytes\n", header.data_unpack_size);
        printf("  Manager Count:    %u\n", header.manager_count);
        printf("  Object Count:     %u\n", header.object_count);
        printf("  Max ID Saved:     %u\n", header.max_id_saved);
        printf("  Product Version:  %u\n", header.product_version);
        printf("  Product Build:    %u\n", header.product_build);
        printf("  HDR1 Unpack Size: %u bytes\n", header.hdr1_unpack_size);
    }

    // Check compression flags
    if (header.file_write_mode & NMO_FILE_WRITE_COMPRESS_HEADER) {
        printf("  Compression: Header1 is compressed\n");
    }
    if (header.file_write_mode & NMO_FILE_WRITE_COMPRESS_DATA) {
        printf("  Compression: Data section is compressed\n");
    }
    if (!(header.file_write_mode & NMO_FILE_WRITE_COMPRESS_BOTH)) {
        printf("  Compression: None\n");
    }

    // Validate file version range
    ASSERT_TRUE(header.file_version >= 2 && header.file_version <= 9);

    // Parse Header1 if present
    if (header.hdr1_unpack_size > 0) {
        printf("\n--- Parsing Header1 ---\n");

        // Create arena for Header1 data
        nmo_arena_t* arena = nmo_arena_create(NULL, 65536);
        ASSERT_NOT_NULL(arena);

        // Read compressed Header1 data
        void* packed_buffer = nmo_arena_alloc(arena, header.hdr1_pack_size, 16);
        ASSERT_NOT_NULL(packed_buffer);

        size_t bytes_read = 0;
        int read_result = nmo_io_read(io, packed_buffer, header.hdr1_pack_size, &bytes_read);
        ASSERT_EQ(NMO_OK, read_result);
        ASSERT_EQ(header.hdr1_pack_size, bytes_read);

        printf("  Header1 packed data read: %zu bytes\n", bytes_read);

        // Decompress if needed
        void* hdr1_buffer = NULL;
        if (header.hdr1_pack_size != header.hdr1_unpack_size) {
            printf("  Decompressing Header1: %u -> %u bytes\n",
                   header.hdr1_pack_size, header.hdr1_unpack_size);

            hdr1_buffer = nmo_arena_alloc(arena, header.hdr1_unpack_size, 16);
            ASSERT_NOT_NULL(hdr1_buffer);

            mz_ulong dest_len = header.hdr1_unpack_size;
            int uncompress_result = mz_uncompress((unsigned char*)hdr1_buffer, &dest_len,
                                                  (const unsigned char*)packed_buffer,
                                                  header.hdr1_pack_size);
            ASSERT_EQ(MZ_OK, uncompress_result);
            ASSERT_EQ(header.hdr1_unpack_size, dest_len);

            printf("  Decompression successful: %lu bytes\n", dest_len);
        } else {
            // Already uncompressed
            hdr1_buffer = packed_buffer;
        }

        // Parse Header1
        nmo_header1_t hdr1;
        memset(&hdr1, 0, sizeof(hdr1));
        hdr1.object_count = header.object_count;  // Set from file header
        nmo_result_t hdr1_result = nmo_header1_parse(hdr1_buffer, header.hdr1_unpack_size, &hdr1, arena);
        ASSERT_EQ(NMO_OK, hdr1_result.code);

        printf("  Objects parsed: %u\n", hdr1.object_count);
        printf("  Plugin dependencies: %u\n", hdr1.plugin_dep_count);

        // Display first few objects
        uint32_t display_count = hdr1.object_count < 5 ? hdr1.object_count : 5;
        if (display_count > 0) {
            printf("\n  First %u objects:\n", display_count);
            for (uint32_t i = 0; i < display_count; i++) {
                nmo_object_desc_t* obj = &hdr1.objects[i];
                printf("    [%u] ID=%u, ClassID=0x%08X, FileIndex=%u, Name=\"%s\"\n",
                       i, obj->file_id, obj->class_id, obj->file_index,
                       obj->name ? obj->name : "(null)");
            }
        }

        // Display plugin dependencies
        if (hdr1.plugin_dep_count > 0) {
            printf("\n  Plugin Dependencies:\n");
            for (uint32_t i = 0; i < hdr1.plugin_dep_count; i++) {
                nmo_plugin_dep_t* dep = &hdr1.plugin_deps[i];
                printf("    [%u] Category=%u, GUID={0x%08X,0x%08X}\n",
                       i, dep->category, dep->guid.d1, dep->guid.d2);
            }
        }

        printf("  Header1 parsed successfully\n");

        // Parse Data section (if present)
        /* Note: Data section parsing must be inside arena scope */
        if (header.data_pack_size > 0) {
            printf("\n--- Parsing Data Section ---\n");

            /* Read packed data */
            uint8_t* packed_buffer = (uint8_t*)malloc(header.data_pack_size);
            ASSERT_NOT_NULL(packed_buffer);

            size_t data_read = 0;
            int read_result = nmo_io_read(io, packed_buffer, header.data_pack_size, &data_read);
            ASSERT_EQ(0, read_result);
            ASSERT_EQ(header.data_pack_size, data_read);
            printf("  Data section read: %zu bytes\n", data_read);

            /* Decompress if needed */
            uint8_t* data_buffer = NULL;
            size_t data_size = 0;

            if ((header.file_write_mode & (CKFILE_CHUNKCOMPRESSED_OLD | CKFILE_WHOLECOMPRESSED)) != 0) {
                printf("  Decompressing Data: %u -> %u bytes\n",
                       header.data_pack_size, header.data_unpack_size);

                data_buffer = (uint8_t*)malloc(header.data_unpack_size);
                ASSERT_NOT_NULL(data_buffer);

                mz_ulong dest_len = header.data_unpack_size;
                int uncompress_result = mz_uncompress((unsigned char*)data_buffer, &dest_len,
                                                      (const unsigned char*)packed_buffer,
                                                      header.data_pack_size);
                ASSERT_EQ(MZ_OK, uncompress_result);
                ASSERT_EQ(header.data_unpack_size, dest_len);

                data_size = dest_len;
                free(packed_buffer);
                printf("  Decompression successful: %lu bytes\n", dest_len);
            } else {
                // Already uncompressed
                data_buffer = packed_buffer;
                data_size = header.data_pack_size;
            }

            /* Parse Data section */
            nmo_data_section_t data_section;
            memset(&data_section, 0, sizeof(data_section));
            data_section.manager_count = header.manager_count;
            data_section.object_count = header.object_count;

                nmo_result_t data_result = nmo_data_section_parse(data_buffer, data_size,
                                                                   header.file_version, &data_section, NULL, arena);
            ASSERT_EQ(NMO_OK, data_result.code);

            printf("  Managers parsed: %u\n", data_section.manager_count);
            printf("  Objects parsed: %u\n", data_section.object_count);

            /* Display first few managers */
            uint32_t mgr_display = data_section.manager_count < 3 ? data_section.manager_count : 3;
            if (mgr_display > 0) {
                printf("\n  First %u managers:\n", mgr_display);
                for (uint32_t i = 0; i < mgr_display; i++) {
                    nmo_manager_data_t* mgr = &data_section.managers[i];
                    printf("    [%u] GUID={0x%08X,0x%08X}, DataSize=%u\n",
                           i, mgr->guid.d1, mgr->guid.d2, mgr->data_size);
                }
            }

            /* Display first few objects */
            uint32_t obj_display = data_section.object_count < 3 ? data_section.object_count : 3;
            if (obj_display > 0) {
                printf("\n  First %u objects:\n", obj_display);
                for (uint32_t i = 0; i < obj_display; i++) {
                    nmo_object_data_t* obj = &data_section.objects[i];
                    printf("    [%u] DataSize=%u\n", i, obj->data_size);
                }
            }

            free(data_buffer);
            printf("  Data section parsed successfully\n");
        }

        // Clean up arena
        nmo_arena_destroy(arena);
    }

    // Close file
    nmo_io_close(io);
    printf("File closed successfully\n");

    printf("=== PASS: %s ===\n", filepath);
}

TEST(nmo_baseline, empty_cmo) {
    test_parse_file("data/Empty.cmo", "Nemo Fi\0");
}

TEST(nmo_baseline, empty_vmo) {
    test_parse_file("data/Empty.vmo", "Nemo Fi\0");
}

TEST(nmo_baseline, text_2d_nmo) {
    test_parse_file("data/2D Text.nmo", "Nemo Fi\0");
}

TEST(nmo_baseline, nop_cmo) {
    test_parse_file("data/Nop.cmo", "Nemo Fi\0");
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(nmo_baseline, empty_cmo);
    REGISTER_TEST(nmo_baseline, empty_vmo);
    REGISTER_TEST(nmo_baseline, text_2d_nmo);
    REGISTER_TEST(nmo_baseline, nop_cmo);
TEST_MAIN_END()
