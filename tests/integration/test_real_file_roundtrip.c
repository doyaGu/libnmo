/**
 * @file test_real_file_roundtrip.c
 * @brief Test loading a real NMO/CMO file, saving it, and loading it again
 */

#include "nmo.h"
#include "app/nmo_parser.h"
#include "format/nmo_data.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define TEMP_FILE "C:\\Users\\kakut\\Works\\Virtools\\NeMo2\\CK2\\libnmo\\build\\test_roundtrip_temp.cmo"
#else
#define TEMP_FILE "/tmp/test_roundtrip_temp.cmo"
#endif

/**
 * Compare two file info structures
 */
static int compare_file_info(const nmo_file_info_t* info1, const nmo_file_info_t* info2) {
    if (info1->object_count != info2->object_count) {
        printf("    Object count mismatch: %u vs %u\n",
               info1->object_count, info2->object_count);
        return 0;
    }
    if (info1->manager_count != info2->manager_count) {
        printf("    Manager count mismatch: %u vs %u\n",
               info1->manager_count, info2->manager_count);
        return 0;
    }
    if (info1->file_version != info2->file_version) {
        printf("    File version mismatch: %u vs %u\n",
               info1->file_version, info2->file_version);
        return 0;
    }
    return 1;
}

/**
 * Test round-trip for a real file
 */
static int test_file_roundtrip(const char* input_file) {
    printf("Testing round-trip for: %s\n", input_file);

    /* Create context */
    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = NULL,  /* Use default logger */
        .thread_pool_size = 1,
    };

    nmo_context_t* ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        printf("  FAILED: Could not create context\n");
        return 1;
    }

    /* === FIRST LOAD: Load original file === */
    nmo_session_t* load1_session = nmo_session_create(ctx);
    if (load1_session == NULL) {
        printf("  FAILED: Could not create load1 session\n");
        nmo_context_release(ctx);
        return 1;
    }

    int result = nmo_load_file(load1_session, input_file, NMO_LOAD_DEFAULT);
    if (result != NMO_OK) {
        printf("  FAILED: Could not load original file (error %d)\n", result);
        nmo_session_destroy(load1_session);
        nmo_context_release(ctx);
        return 1;
    }

    /* Get file info from first load */
    nmo_file_info_t load1_info = nmo_session_get_file_info(load1_session);

    printf("  Original file:\n");
    printf("    Objects: %u\n", load1_info.object_count);
    printf("    Managers: %u\n", load1_info.manager_count);
    printf("    Version: %u\n", load1_info.file_version);

    /* Get object repository */
    nmo_object_repository_t* load1_repo = nmo_session_get_repository(load1_session);
    size_t load1_obj_count = 0;
    nmo_object_t** load1_objects = nmo_object_repository_get_all(load1_repo, &load1_obj_count);

    /* === SAVE: Save to temporary file === */
    result = nmo_save_file(load1_session, TEMP_FILE, NMO_SAVE_DEFAULT);
    if (result != NMO_OK) {
        printf("  FAILED: Could not save file (error %d)\n", result);
        nmo_session_destroy(load1_session);
        nmo_context_release(ctx);
        remove(TEMP_FILE);
        return 1;
    }
    printf("  Saved to temporary file\n");

    /* Clean up first session */
    nmo_session_destroy(load1_session);

    /* === SECOND LOAD: Load saved file === */
    nmo_session_t* load2_session = nmo_session_create(ctx);
    if (load2_session == NULL) {
        printf("  FAILED: Could not create load2 session\n");
        nmo_context_release(ctx);
        remove(TEMP_FILE);
        return 1;
    }

    result = nmo_load_file(load2_session, TEMP_FILE, NMO_LOAD_DEFAULT);
    if (result != NMO_OK) {
        printf("  FAILED: Could not load saved file (error %d)\n", result);
        nmo_session_destroy(load2_session);
        nmo_context_release(ctx);
        remove(TEMP_FILE);
        return 1;
    }

    /* Get file info from second load */
    nmo_file_info_t load2_info = nmo_session_get_file_info(load2_session);

    printf("  Reloaded file:\n");
    printf("    Objects: %u\n", load2_info.object_count);
    printf("    Managers: %u\n", load2_info.manager_count);
    printf("    Version: %u\n", load2_info.file_version);

    /* Get object repository from second load */
    nmo_object_repository_t* load2_repo = nmo_session_get_repository(load2_session);
    size_t load2_obj_count = 0;
    nmo_object_t** load2_objects = nmo_object_repository_get_all(load2_repo, &load2_obj_count);

    /* === VERIFICATION === */
    int passed = 1;

    /* Compare file info */
    if (!compare_file_info(&load1_info, &load2_info)) {
        printf("  FAILED: File info mismatch\n");
        passed = 0;
    }

    /* Compare object counts */
    if (load1_obj_count != load2_obj_count) {
        printf("  FAILED: Object count mismatch: %zu vs %zu\n",
               load1_obj_count, load2_obj_count);
        passed = 0;
    }

    /* Compare object data */
    if (passed && load1_objects != NULL && load2_objects != NULL) {
        for (size_t i = 0; i < load1_obj_count && i < load2_obj_count; i++) {
            nmo_object_t* obj1 = load1_objects[i];
            nmo_object_t* obj2 = load2_objects[i];

            if (obj1->class_id != obj2->class_id) {
                printf("  FAILED: Object %zu class_id mismatch: 0x%08X vs 0x%08X\n",
                       i, obj1->class_id, obj2->class_id);
                passed = 0;
                break;
            }

            /* Compare chunk data sizes */
            if (obj1->chunk != NULL && obj2->chunk != NULL) {
                if (obj1->chunk->raw_size != obj2->chunk->raw_size) {
                    printf("  FAILED: Object %zu chunk size mismatch: %zu vs %zu\n",
                           i, obj1->chunk->raw_size, obj2->chunk->raw_size);
                    passed = 0;
                    break;
                }

                /* Compare raw chunk data */
                if (obj1->chunk->raw_data != NULL && obj2->chunk->raw_data != NULL) {
                    if (memcmp(obj1->chunk->raw_data, obj2->chunk->raw_data,
                               obj1->chunk->raw_size) != 0) {
                        printf("  FAILED: Object %zu chunk data mismatch\n", i);
                        passed = 0;
                        break;
                    }
                }
            } else if (obj1->chunk != obj2->chunk) {
                printf("  FAILED: Object %zu chunk presence mismatch\n", i);
                passed = 0;
                break;
            }
        }
    }

    /* Clean up */
    nmo_session_destroy(load2_session);
    nmo_context_release(ctx);
    remove(TEMP_FILE);

    if (passed) {
        printf("  PASSED: Round-trip successful, data matches\n");
    }

    return passed ? 0 : 1;
}

int main(void) {
    int failed = 0;

    printf("=== Real File Round-Trip Tests ===\n\n");

    /* Test with Empty.cmo */
    if (test_file_roundtrip("../../../data/Empty.cmo") != 0) {
        failed++;
    }
    printf("\n");

    /* Test with Empty.vmo */
    if (test_file_roundtrip("../../../data/Empty.vmo") != 0) {
        failed++;
    }
    printf("\n");

    /* Test with 2D Text.nmo */
    if (test_file_roundtrip("../../../data/2D Text.nmo") != 0) {
        failed++;
    }
    printf("\n");

    printf("=== Summary ===\n");
    if (failed == 0) {
        printf("All round-trip tests PASSED!\n");
        return 0;
    } else {
        printf("%d test(s) FAILED!\n", failed);
        return 1;
    }
}
