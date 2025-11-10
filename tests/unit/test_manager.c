/**
 * @file test_manager.c
 * @brief Unit tests for manager plugin interface
 */

#include "../test_framework.h"
#include "format/nmo_manager.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <string.h>

// Hook tracking variables
static int pre_load_called = 0;
static int post_load_called = 0;
static int load_data_called = 0;
static int pre_save_called = 0;
static int post_save_called = 0;
static int save_data_called = 0;

// Test hooks
static int test_pre_load(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    pre_load_called = 1;
    return NMO_OK;
}

static int test_post_load(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    post_load_called = 1;
    return NMO_OK;
}

static int test_load_data(void* session, const nmo_chunk* chunk, void* user_data) {
    (void)session;
    (void)chunk;
    (void)user_data;
    load_data_called = 1;
    return NMO_OK;
}

static nmo_chunk* test_save_data(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    save_data_called = 1;
    return NULL;  // Would normally return a chunk
}

static int test_pre_save(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    pre_save_called = 1;
    return NMO_OK;
}

static int test_post_save(void* session, void* user_data) {
    (void)session;
    (void)user_data;
    post_save_called = 1;
    return NMO_OK;
}

// Reset hook tracking
static void reset_hooks(void) {
    pre_load_called = 0;
    post_load_called = 0;
    load_data_called = 0;
    pre_save_called = 0;
    post_save_called = 0;
    save_data_called = 0;
}

// Test: Create and destroy manager
static void test_manager_create_destroy(void) {
    nmo_guid guid = {0x12345678, 0x9ABCDEF0};
    const char* name = "TestManager";

    nmo_manager* mgr = nmo_manager_create(guid, name, NMO_PLUGIN_MANAGER_DLL);
    if (mgr == NULL) {
        printf("FAIL: test_manager_create_destroy - manager creation failed\n");
        return;
    }

    nmo_guid retrieved_guid = nmo_manager_get_guid(mgr);
    if (retrieved_guid.d1 != guid.d1 || retrieved_guid.d2 != guid.d2) {
        printf("FAIL: test_manager_create_destroy - GUID mismatch\n");
        nmo_manager_destroy(mgr);
        return;
    }

    const char* retrieved_name = nmo_manager_get_name(mgr);
    if (retrieved_name == NULL || strcmp(retrieved_name, name) != 0) {
        printf("FAIL: test_manager_create_destroy - name mismatch\n");
        nmo_manager_destroy(mgr);
        return;
    }

    nmo_plugin_category category = nmo_manager_get_category(mgr);
    if (category != NMO_PLUGIN_MANAGER_DLL) {
        printf("FAIL: test_manager_create_destroy - category mismatch\n");
        nmo_manager_destroy(mgr);
        return;
    }

    nmo_manager_destroy(mgr);
    printf("PASS: test_manager_create_destroy\n");
}

// Test: Set and invoke hooks
static void test_manager_hooks(void) {
    reset_hooks();

    nmo_guid guid = {0x11111111, 0x22222222};
    nmo_manager* mgr = nmo_manager_create(guid, "HookTest", NMO_PLUGIN_MANAGER_DLL);
    if (mgr == NULL) {
        printf("FAIL: test_manager_hooks - manager creation failed\n");
        return;
    }

    // Set hooks
    if (nmo_manager_set_pre_load_hook(mgr, test_pre_load) != NMO_OK) {
        printf("FAIL: test_manager_hooks - set_pre_load_hook failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (nmo_manager_set_post_load_hook(mgr, test_post_load) != NMO_OK) {
        printf("FAIL: test_manager_hooks - set_post_load_hook failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (nmo_manager_set_load_data_hook(mgr, test_load_data) != NMO_OK) {
        printf("FAIL: test_manager_hooks - set_load_data_hook failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (nmo_manager_set_save_data_hook(mgr, test_save_data) != NMO_OK) {
        printf("FAIL: test_manager_hooks - set_save_data_hook failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (nmo_manager_set_pre_save_hook(mgr, test_pre_save) != NMO_OK) {
        printf("FAIL: test_manager_hooks - set_pre_save_hook failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (nmo_manager_set_post_save_hook(mgr, test_post_save) != NMO_OK) {
        printf("FAIL: test_manager_hooks - set_post_save_hook failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    // Invoke hooks
    if (nmo_manager_invoke_pre_load(mgr, NULL) != NMO_OK) {
        printf("FAIL: test_manager_hooks - invoke_pre_load failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (!pre_load_called) {
        printf("FAIL: test_manager_hooks - pre_load hook not called\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (nmo_manager_invoke_post_load(mgr, NULL) != NMO_OK) {
        printf("FAIL: test_manager_hooks - invoke_post_load failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (!post_load_called) {
        printf("FAIL: test_manager_hooks - post_load hook not called\n");
        nmo_manager_destroy(mgr);
        return;
    }

    // Create dummy chunk for load_data test
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    nmo_chunk* chunk = (nmo_chunk*)nmo_arena_alloc(arena, sizeof(nmo_chunk), sizeof(void*));
    memset(chunk, 0, sizeof(nmo_chunk));

    if (nmo_manager_invoke_load_data(mgr, NULL, chunk) != NMO_OK) {
        printf("FAIL: test_manager_hooks - invoke_load_data failed\n");
        nmo_manager_destroy(mgr);
        nmo_arena_destroy(arena);
        return;
    }

    if (!load_data_called) {
        printf("FAIL: test_manager_hooks - load_data hook not called\n");
        nmo_manager_destroy(mgr);
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_manager_invoke_pre_save(mgr, NULL) != NMO_OK) {
        printf("FAIL: test_manager_hooks - invoke_pre_save failed\n");
        nmo_manager_destroy(mgr);
        nmo_arena_destroy(arena);
        return;
    }

    if (!pre_save_called) {
        printf("FAIL: test_manager_hooks - pre_save hook not called\n");
        nmo_manager_destroy(mgr);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk* save_result = nmo_manager_invoke_save_data(mgr, NULL);
    (void)save_result;  // Can be NULL

    if (!save_data_called) {
        printf("FAIL: test_manager_hooks - save_data hook not called\n");
        nmo_manager_destroy(mgr);
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_manager_invoke_post_save(mgr, NULL) != NMO_OK) {
        printf("FAIL: test_manager_hooks - invoke_post_save failed\n");
        nmo_manager_destroy(mgr);
        nmo_arena_destroy(arena);
        return;
    }

    if (!post_save_called) {
        printf("FAIL: test_manager_hooks - post_save hook not called\n");
        nmo_manager_destroy(mgr);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_manager_destroy(mgr);
    nmo_arena_destroy(arena);
    printf("PASS: test_manager_hooks\n");
}

// Test: User data
static void test_manager_user_data(void) {
    nmo_guid guid = {0xAAAAAAAA, 0xBBBBBBBB};
    nmo_manager* mgr = nmo_manager_create(guid, NULL, NMO_PLUGIN_BEHAVIOR_DLL);
    if (mgr == NULL) {
        printf("FAIL: test_manager_user_data - manager creation failed\n");
        return;
    }

    int test_data = 12345;
    if (nmo_manager_set_user_data(mgr, &test_data) != NMO_OK) {
        printf("FAIL: test_manager_user_data - set_user_data failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    // Verify user_data is accessible (we'd need a getter or access through hooks)
    // For now just verify the set operation succeeded
    nmo_manager_destroy(mgr);
    printf("PASS: test_manager_user_data\n");
}

// Test: Hooks not set (should return OK but not crash)
static void test_manager_no_hooks(void) {
    reset_hooks();

    nmo_guid guid = {0xCCCCCCCC, 0xDDDDDDDD};
    nmo_manager* mgr = nmo_manager_create(guid, "NoHooks", NMO_PLUGIN_RENDER_DLL);
    if (mgr == NULL) {
        printf("FAIL: test_manager_no_hooks - manager creation failed\n");
        return;
    }

    // Invoke hooks that aren't set - should return OK without calling anything
    if (nmo_manager_invoke_pre_load(mgr, NULL) != NMO_OK) {
        printf("FAIL: test_manager_no_hooks - invoke_pre_load failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (pre_load_called) {
        printf("FAIL: test_manager_no_hooks - pre_load should not have been called\n");
        nmo_manager_destroy(mgr);
        return;
    }

    if (nmo_manager_invoke_post_load(mgr, NULL) != NMO_OK) {
        printf("FAIL: test_manager_no_hooks - invoke_post_load failed\n");
        nmo_manager_destroy(mgr);
        return;
    }

    nmo_manager_destroy(mgr);
    printf("PASS: test_manager_no_hooks\n");
}

int main(void) {
    printf("Running manager plugin tests...\n\n");

    test_manager_create_destroy();
    test_manager_hooks();
    test_manager_user_data();
    test_manager_no_hooks();

    printf("\nAll manager plugin tests completed!\n");
    return 0;
}
