/**
 * @file test_round_trip.c
 * @brief Integration test for complete save/load round-trip
 */

#include "nmo.h"
#include "app/nmo_parser.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Global counters for manager hooks */
static int pre_save_called = 0;
static int post_save_called = 0;
static int pre_load_called = 0;
static int post_load_called = 0;

/* Manager hook implementations */
static int test_pre_save_hook(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    pre_save_called++;
    return NMO_OK;
}

static int test_post_save_hook(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    post_save_called++;
    return NMO_OK;
}

static int test_pre_load_hook(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    pre_load_called++;
    return NMO_OK;
}

static int test_post_load_hook(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    post_load_called++;
    return NMO_OK;
}

/**
 * Test basic round-trip
 */
static int test_basic_round_trip(void) {
    const char* test_file = "/tmp/test_round_trip_basic.nmo";
    unlink(test_file);

    /* Create context */
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = NULL,  /* Use default logger */
        .thread_pool_size = 1,
    };

    nmo_context* ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) return 1;

    /* Save phase */
    nmo_session* save_session = nmo_session_create(ctx);
    if (save_session == NULL) {
        nmo_context_release(ctx);
        return 1;
    }

    nmo_object_repository* save_repo = nmo_session_get_repository(save_session);
    nmo_arena* save_arena = nmo_session_get_arena(save_session);

    /* Create test objects */
    for (size_t i = 0; i < 5; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(save_arena, sizeof(nmo_object), sizeof(void*));
        if (obj == NULL) {
            nmo_session_destroy(save_session);
            nmo_context_release(ctx);
            return 1;
        }

        memset(obj, 0, sizeof(nmo_object));
        obj->class_id = 0x10000000 + (uint32_t)i;
        obj->name = "TestObject";
        obj->arena = save_arena;

        nmo_object_repository_add(save_repo, obj);
    }

    nmo_file_info file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = 5,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(save_session, &file_info);

    if (nmo_save_file(save_session, test_file, NMO_SAVE_DEFAULT) != NMO_OK) {
        nmo_session_destroy(save_session);
        nmo_context_release(ctx);
        unlink(test_file);
        return 1;
    }

    nmo_session_destroy(save_session);

    /* Load phase */
    nmo_session* load_session = nmo_session_create(ctx);
    if (load_session == NULL) {
        nmo_context_release(ctx);
        unlink(test_file);
        return 1;
    }

    if (nmo_load_file(load_session, test_file, NMO_LOAD_DEFAULT) != NMO_OK) {
        nmo_session_destroy(load_session);
        nmo_context_release(ctx);
        unlink(test_file);
        return 1;
    }

    /* Verify objects were loaded */
    nmo_object_repository* load_repo = nmo_session_get_repository(load_session);
    size_t loaded_count;
    nmo_object_repository_get_all(load_repo, &loaded_count);

    nmo_session_destroy(load_session);
    nmo_context_release(ctx);
    unlink(test_file);

    return (loaded_count == 5) ? 0 : 1;
}

/**
 * Test with manager hooks
 */
static int test_manager_hooks(void) {
    const char* test_file = "/tmp/test_round_trip_hooks.nmo";
    unlink(test_file);

    pre_save_called = 0;
    post_save_called = 0;
    pre_load_called = 0;
    post_load_called = 0;

    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = NULL,  /* Use default logger */
        .thread_pool_size = 1,
    };

    nmo_context* ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) return 1;

    /* Register manager with hooks */
    nmo_manager_registry_t* manager_reg = nmo_context_get_manager_registry(ctx);
    nmo_guid test_guid = {0xAABBCCDD, 0x11223344};
    nmo_manager* manager = nmo_manager_create(test_guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);

    nmo_manager_set_pre_save_hook(manager, test_pre_save_hook);
    nmo_manager_set_post_save_hook(manager, test_post_save_hook);
    nmo_manager_set_pre_load_hook(manager, test_pre_load_hook);
    nmo_manager_set_post_load_hook(manager, test_post_load_hook);

    nmo_manager_registry_register(manager_reg, 1, manager);

    /* Save phase */
    nmo_session* save_session = nmo_session_create(ctx);
    nmo_object_repository* save_repo = nmo_session_get_repository(save_session);
    nmo_arena* save_arena = nmo_session_get_arena(save_session);

    nmo_object* obj = (nmo_object*)nmo_arena_alloc(save_arena, sizeof(nmo_object), sizeof(void*));
    memset(obj, 0, sizeof(nmo_object));
    obj->class_id = 0x99887766;
    obj->name = "HookedObject";
    obj->arena = save_arena;
    nmo_object_repository_add(save_repo, obj);

    nmo_file_info file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = 1,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(save_session, &file_info);

    nmo_save_file(save_session, test_file, NMO_SAVE_DEFAULT);
    nmo_session_destroy(save_session);

    /* Load phase */
    nmo_session* load_session = nmo_session_create(ctx);
    nmo_load_file(load_session, test_file, NMO_LOAD_DEFAULT);
    nmo_session_destroy(load_session);

    nmo_context_release(ctx);
    unlink(test_file);

    /* Verify hooks were called */
    return (pre_save_called == 1 && post_save_called == 1 &&
            pre_load_called == 1 && post_load_called == 1) ? 0 : 1;
}

int main(void) {
    int failed = 0;

    printf("Running round-trip integration tests...\n\n");

    printf("Test 1: Basic save/load round-trip... ");
    fflush(stdout);
    if (test_basic_round_trip() == 0) {
        printf("PASSED\n");
    } else {
        printf("FAILED\n");
        failed++;
    }

    printf("Test 2: Manager hooks integration... ");
    fflush(stdout);
    if (test_manager_hooks() == 0) {
        printf("PASSED\n");
    } else {
        printf("FAILED\n");
        failed++;
    }

    printf("\n");
    if (failed == 0) {
        printf("All integration tests PASSED!\n");
        return 0;
    } else {
        printf("%d test(s) FAILED!\n", failed);
        return 1;
    }
}
