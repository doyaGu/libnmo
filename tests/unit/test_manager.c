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

static int test_load_data(void* session, const nmo_chunk_t* chunk, void* user_data) {
    (void)session;
    (void)chunk;
    (void)user_data;
    load_data_called = 1;
    return NMO_OK;
}

static nmo_chunk_t* test_save_data(void* session, void* user_data) {
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
TEST(manager, create_destroy) {
    nmo_guid_t guid = {0x12345678, 0x9ABCDEF0};
    const char* name = "TestManager";

    nmo_manager_t* mgr = nmo_manager_create(guid, name, NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(mgr);

    nmo_guid_t retrieved_guid = nmo_manager_get_guid(mgr);
    ASSERT_EQ(retrieved_guid.d1, guid.d1);
    ASSERT_EQ(retrieved_guid.d2, guid.d2);

    const char* retrieved_name = nmo_manager_get_name(mgr);
    ASSERT_NOT_NULL(retrieved_name);
    ASSERT_STR_EQ(retrieved_name, name);

    nmo_plugin_category_t category = nmo_manager_get_category(mgr);
    ASSERT_EQ(category, NMO_PLUGIN_MANAGER_DLL);

    nmo_manager_destroy(mgr);
}

// Test: Set and invoke hooks
TEST(manager, hooks) {
    reset_hooks();

    nmo_guid_t guid = {0x11111111, 0x22222222};
    nmo_manager_t* mgr = nmo_manager_create(guid, "HookTest", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(mgr);

    // Set hooks
    ASSERT_EQ(nmo_manager_set_pre_load_hook(mgr, test_pre_load), NMO_OK);
    ASSERT_EQ(nmo_manager_set_post_load_hook(mgr, test_post_load), NMO_OK);
    ASSERT_EQ(nmo_manager_set_load_data_hook(mgr, test_load_data), NMO_OK);
    ASSERT_EQ(nmo_manager_set_save_data_hook(mgr, test_save_data), NMO_OK);
    ASSERT_EQ(nmo_manager_set_pre_save_hook(mgr, test_pre_save), NMO_OK);
    ASSERT_EQ(nmo_manager_set_post_save_hook(mgr, test_post_save), NMO_OK);

    // Invoke hooks
    ASSERT_EQ(nmo_manager_invoke_pre_load(mgr, NULL), NMO_OK);
    ASSERT_TRUE(pre_load_called);

    ASSERT_EQ(nmo_manager_invoke_post_load(mgr, NULL), NMO_OK);
    ASSERT_TRUE(post_load_called);

    // Create dummy chunk for load_data test
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = (nmo_chunk_t*)nmo_arena_alloc(arena, sizeof(nmo_chunk_t), sizeof(void*));
    ASSERT_NOT_NULL(chunk);
    memset(chunk, 0, sizeof(nmo_chunk_t));

    ASSERT_EQ(nmo_manager_invoke_load_data(mgr, NULL, chunk), NMO_OK);
    ASSERT_TRUE(load_data_called);

    ASSERT_EQ(nmo_manager_invoke_pre_save(mgr, NULL), NMO_OK);
    ASSERT_TRUE(pre_save_called);

    nmo_chunk_t* save_result = nmo_manager_invoke_save_data(mgr, NULL);
    (void)save_result;  // Can be NULL
    ASSERT_TRUE(save_data_called);

    ASSERT_EQ(nmo_manager_invoke_post_save(mgr, NULL), NMO_OK);
    ASSERT_TRUE(post_save_called);

    nmo_manager_destroy(mgr);
    nmo_arena_destroy(arena);
}

// Test: User data
TEST(manager, user_data) {
    nmo_guid_t guid = {0xAAAAAAAA, 0xBBBBBBBB};
    nmo_manager_t* mgr = nmo_manager_create(guid, NULL, NMO_PLUGIN_BEHAVIOR_DLL);
    ASSERT_NOT_NULL(mgr);

    int test_data = 12345;
    ASSERT_EQ(nmo_manager_set_user_data(mgr, &test_data), NMO_OK);

    // Verify user_data is accessible (we'd need a getter or access through hooks)
    // For now just verify set operation succeeded
    nmo_manager_destroy(mgr);
}

// Test: Hooks not set (should return OK but not crash)
TEST(manager, no_hooks) {
    reset_hooks();

    nmo_guid_t guid = {0xCCCCCCCC, 0xDDDDDDDD};
    nmo_manager_t* mgr = nmo_manager_create(guid, "NoHooks", NMO_PLUGIN_RENDER_DLL);
    ASSERT_NOT_NULL(mgr);

    // Invoke hooks that aren't set - should return OK without calling anything
    ASSERT_EQ(nmo_manager_invoke_pre_load(mgr, NULL), NMO_OK);
    ASSERT_FALSE(pre_load_called);

    ASSERT_EQ(nmo_manager_invoke_post_load(mgr, NULL), NMO_OK);
    ASSERT_FALSE(post_load_called);

    nmo_manager_destroy(mgr);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(manager, create_destroy);
    REGISTER_TEST(manager, hooks);
    REGISTER_TEST(manager, user_data);
    REGISTER_TEST(manager, no_hooks);
TEST_MAIN_END()
