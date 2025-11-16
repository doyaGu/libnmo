/**
 * @file test_all_reference_files.c
 * @brief Comprehensive test for all reference Virtools files
 * 
 * Phase 3A: Parse ALL reference files and generate detailed compatibility report
 */

#include "test_framework.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "io/nmo_io_file.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <string.h>
#include <miniz.h>

#define CKFILE_CHUNKCOMPRESSED_OLD 1
#define CKFILE_WHOLECOMPRESSED      8

typedef struct {
    const char* filename;
    int parse_header;
    int parse_header1;
    int parse_data;
    int object_count;
    int manager_count;
    int file_version;
    const char* error_msg;
} file_result_t;

static file_result_t results[20];
static int result_count = 0;

static void record_result(const char* filename, int success_flags, int obj_count, 
                         int mgr_count, int version, const char* error) {
    file_result_t* r = &results[result_count++];
    r->filename = filename;
    r->parse_header = (success_flags & 1) ? 1 : 0;
    r->parse_header1 = (success_flags & 2) ? 1 : 0;
    r->parse_data = (success_flags & 4) ? 1 : 0;
    r->object_count = obj_count;
    r->manager_count = mgr_count;
    r->file_version = version;
    r->error_msg = error;
}

static int test_parse_single_file(const char* filepath) {
    int success_flags = 0;
    int obj_count = 0, mgr_count = 0, version = 0;
    const char* error_msg = NULL;
    
    nmo_io_interface_t* io = nmo_file_io_open(filepath, NMO_IO_READ);
    if (!io) {
        error_msg = "Failed to open file";
        record_result(filepath, success_flags, 0, 0, 0, error_msg);
        return 0;
    }
    
    // Parse header
    nmo_file_header_t header;
    memset(&header, 0, sizeof(header));
    nmo_result_t result = nmo_file_header_parse(io, &header);
    
    if (result.code != NMO_OK) {
        error_msg = "Header parse failed";
        nmo_io_close(io);
        record_result(filepath, success_flags, 0, 0, 0, error_msg);
        return 0;
    }
    
    success_flags |= 1; // Header OK
    version = header.file_version;
    obj_count = header.object_count;
    mgr_count = header.manager_count;
    
    // Validate header
    result = nmo_file_header_validate(&header);
    if (result.code != NMO_OK) {
        error_msg = "Header validation failed";
        nmo_io_close(io);
        record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
        return 0;
    }
    
    // Parse Header1 if present
    if (header.hdr1_unpack_size > 0) {
        nmo_arena_t* arena = nmo_arena_create(NULL, 65536);
        if (!arena) {
            error_msg = "Arena allocation failed";
            nmo_io_close(io);
            record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
            return 0;
        }
        
        // Read packed Header1
        void* packed_buffer = nmo_arena_alloc(arena, header.hdr1_pack_size, 16);
        size_t bytes_read = 0;
        int read_result = nmo_io_read(io, packed_buffer, header.hdr1_pack_size, &bytes_read);
        
        if (read_result != NMO_OK || bytes_read != header.hdr1_pack_size) {
            error_msg = "Header1 read failed";
            nmo_arena_destroy(arena);
            nmo_io_close(io);
            record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
            return 0;
        }
        
        // Decompress if needed
        void* hdr1_buffer = NULL;
        if (header.hdr1_pack_size != header.hdr1_unpack_size) {
            hdr1_buffer = nmo_arena_alloc(arena, header.hdr1_unpack_size, 16);
            mz_ulong dest_len = header.hdr1_unpack_size;
            int uncompress_result = mz_uncompress((unsigned char*)hdr1_buffer, &dest_len,
                                                  (const unsigned char*)packed_buffer,
                                                  header.hdr1_pack_size);
            if (uncompress_result != MZ_OK) {
                error_msg = "Header1 decompression failed";
                nmo_arena_destroy(arena);
                nmo_io_close(io);
                record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
                return 0;
            }
        } else {
            hdr1_buffer = packed_buffer;
        }
        
        // Parse Header1
        nmo_header1_t hdr1;
        memset(&hdr1, 0, sizeof(hdr1));
        hdr1.object_count = header.object_count;
        result = nmo_header1_parse(hdr1_buffer, header.hdr1_unpack_size, &hdr1, arena);
        
        if (result.code != NMO_OK) {
            error_msg = "Header1 parse failed";
            nmo_arena_destroy(arena);
            nmo_io_close(io);
            record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
            return 0;
        }
        
        success_flags |= 2; // Header1 OK
        
        // Parse Data section if present
        if (header.data_pack_size > 0) {
            uint8_t* packed_buffer = (uint8_t*)malloc(header.data_pack_size);
            if (!packed_buffer) {
                error_msg = "Data buffer allocation failed";
                nmo_arena_destroy(arena);
                nmo_io_close(io);
                record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
                return 0;
            }
            
            size_t data_read = 0;
            read_result = nmo_io_read(io, packed_buffer, header.data_pack_size, &data_read);
            
            if (read_result != NMO_OK || data_read != header.data_pack_size) {
                error_msg = "Data section read failed";
                free(packed_buffer);
                nmo_arena_destroy(arena);
                nmo_io_close(io);
                record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
                return 0;
            }
            
            // Decompress if needed
            uint8_t* data_buffer = NULL;
            size_t data_size = 0;
            
            if ((header.file_write_mode & (CKFILE_CHUNKCOMPRESSED_OLD | CKFILE_WHOLECOMPRESSED)) != 0) {
                data_buffer = (uint8_t*)malloc(header.data_unpack_size);
                if (!data_buffer) {
                    error_msg = "Data decompression buffer allocation failed";
                    free(packed_buffer);
                    nmo_arena_destroy(arena);
                    nmo_io_close(io);
                    record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
                    return 0;
                }
                
                mz_ulong dest_len = header.data_unpack_size;
                int uncompress_result = mz_uncompress((unsigned char*)data_buffer, &dest_len,
                                                      (const unsigned char*)packed_buffer,
                                                      header.data_pack_size);
                if (uncompress_result != MZ_OK) {
                    error_msg = "Data decompression failed";
                    free(data_buffer);
                    free(packed_buffer);
                    nmo_arena_destroy(arena);
                    nmo_io_close(io);
                    record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
                    return 0;
                }
                
                data_size = dest_len;
                free(packed_buffer);
            } else {
                data_buffer = packed_buffer;
                data_size = header.data_pack_size;
            }
            
            // Parse Data section
            nmo_data_section_t data_section;
            memset(&data_section, 0, sizeof(data_section));
            data_section.manager_count = header.manager_count;
            data_section.object_count = header.object_count;
            
            result = nmo_data_section_parse(data_buffer, data_size, header.file_version, 
                                          &data_section, NULL, arena);
            
            free(data_buffer);
            
            if (result.code != NMO_OK) {
                error_msg = "Data section parse failed";
                nmo_arena_destroy(arena);
                nmo_io_close(io);
                record_result(filepath, success_flags, obj_count, mgr_count, version, error_msg);
                return 0;
            }
            
            success_flags |= 4; // Data OK
        }
        
        nmo_arena_destroy(arena);
    }
    
    nmo_io_close(io);
    record_result(filepath, success_flags, obj_count, mgr_count, version, NULL);
    return 1;
}

TEST(reference_files, parse_all) {
    const char* files[] = {
        NMO_TEST_DATA_FILE("2D Text.nmo"),
        NMO_TEST_DATA_FILE("base.cmo"),
        NMO_TEST_DATA_FILE("Empty.cmo"),
        NMO_TEST_DATA_FILE("Empty.vmo"),
        NMO_TEST_DATA_FILE("EmptyLevelScript.cmo"),
        NMO_TEST_DATA_FILE("Nop.cmo"),
        NMO_TEST_DATA_FILE("Nop1.cmo"),
        NMO_TEST_DATA_FILE("Nop2.cmo")
    };
    
    int total = sizeof(files) / sizeof(files[0]);
    result_count = 0;
    
    printf("\n========================================\n");
    printf("Phase 3A: Reference File Compatibility Test\n");
    printf("========================================\n\n");
    
    for (int i = 0; i < total; i++) {
        printf("Testing [%d/%d]: %s\n", i+1, total, files[i]);
        test_parse_single_file(files[i]);
    }
    
    // Generate report
    printf("\n========================================\n");
    printf("COMPATIBILITY REPORT\n");
    printf("========================================\n\n");
    
    int full_success = 0;
    int header_only = 0;
    int failed = 0;
    
    for (int i = 0; i < result_count; i++) {
        file_result_t* r = &results[i];
        printf("File: %s\n", r->filename);
        printf("  Version: %d\n", r->file_version);
        printf("  Objects: %d, Managers: %d\n", r->object_count, r->manager_count);
        printf("  Header:  %s\n", r->parse_header ? "OK" : "FAIL");
        printf("  Header1: %s\n", r->parse_header1 ? "OK" : "FAIL");
        printf("  Data:    %s\n", r->parse_data ? "OK" : "FAIL");
        
        if (r->error_msg) {
            printf("  ERROR: %s\n", r->error_msg);
        }
        
        if (r->parse_header && r->parse_header1 && r->parse_data) {
            printf("  Status: FULL SUCCESS ✓✓✓\n");
            full_success++;
        } else if (r->parse_header) {
            printf("  Status: PARTIAL (header only)\n");
            header_only++;
        } else {
            printf("  Status: FAILED\n");
            failed++;
        }
        printf("\n");
    }
    
    printf("========================================\n");
    printf("SUMMARY\n");
    printf("========================================\n");
    printf("Total files:     %d\n", result_count);
    printf("Full success:    %d (%.1f%%)\n", full_success, 100.0*full_success/result_count);
    printf("Partial success: %d (%.1f%%)\n", header_only, 100.0*header_only/result_count);
    printf("Failed:          %d (%.1f%%)\n", failed, 100.0*failed/result_count);
    printf("========================================\n\n");
    
    // Only pass if majority (>50%) fully succeed
    float success_rate = 100.0 * full_success / result_count;
    printf("Success rate: %.1f%%\n", success_rate);
    
    if (success_rate < 50.0) {
        printf("�?FAIL: Less than 50%% success rate\n");
        ASSERT_TRUE(0);
    } else if (success_rate < 90.0) {
        printf("⚠️  WARN: Success rate below 90%%\n");
    } else {
        printf("�?PASS: Good compatibility\n");
    }
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(reference_files, parse_all);
TEST_MAIN_END()
