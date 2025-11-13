#include "../test_framework.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h" // Include for nmo_object_t definition
#include "core/nmo_arena.h"   // Include for nmo_arena_alloc
#include "core/nmo_error.h"
#include <stdio.h>
#include <string.h>

// Helper to create a session with a few objects
nmo_session_t* create_test_session(nmo_context_t* ctx) {
    nmo_session_t* session = nmo_session_create(ctx);
    if (!session) {
        fprintf(stderr, "ERROR: Failed to create session\n");
        return NULL;
    }

    nmo_object_repository_t* repo = nmo_session_get_repository(session);
    nmo_arena_t* arena = nmo_session_get_arena(session);

    // Object 1
    nmo_object_t* obj1 = nmo_arena_alloc(arena, sizeof(nmo_object_t), 1);
    obj1->class_id = 101;
    obj1->name = "TestObject1";
    obj1->chunk = NULL;
    nmo_object_repository_add(repo, obj1);

    // Object 2
    nmo_object_t* obj2 = nmo_arena_alloc(arena, sizeof(nmo_object_t), 1);
    obj2->class_id = 102;
    obj2->name = "TestObject2";
    obj2->chunk = NULL;
    nmo_object_repository_add(repo, obj2);

    return session;
}

/**
 * Test file save and load round trip
 */
static void test_file_io_roundtrip(void) {
    const char* test_filename = "test_io.nmo";

    // 1. Create a context and a session with some data
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* save_ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(save_ctx);

    nmo_session_t* save_session = create_test_session(save_ctx);
    ASSERT_NOT_NULL(save_session);

    // 2. Save the session to a file
    int save_result = nmo_save_file(save_session, test_filename, 0);
    ASSERT_EQ(save_result, NMO_OK);

    nmo_session_destroy(save_session);
    nmo_context_release(save_ctx);

    // 3. Create a new context and session to load the file into
    nmo_context_desc_t load_desc = {0};  // Zero-initialized for defaults
    nmo_context_t* load_ctx = nmo_context_create(&load_desc);
    ASSERT_NOT_NULL(load_ctx);
    nmo_session_t* load_session = nmo_session_create(load_ctx);
    ASSERT_NOT_NULL(load_session);

    // 4. Load the file
    int load_result = nmo_load_file(load_session, test_filename, 0);
    ASSERT_EQ(load_result, NMO_OK);

    // 5. Verify the loaded data
    nmo_object_repository_t* load_repo = nmo_session_get_repository(load_session);
    size_t object_count = 0;
    nmo_object_t** loaded_objects = nmo_object_repository_get_all(load_repo, &object_count);

    ASSERT_EQ(object_count, 2);

    nmo_object_t* obj1 = nmo_object_repository_find_by_name(load_repo, "TestObject1");
    nmo_object_t* obj2 = nmo_object_repository_find_by_name(load_repo, "TestObject2");

    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);

    ASSERT_EQ(obj1->class_id, 101);
    ASSERT_EQ(obj2->class_id, 102);

    // The default serializer only saves the name, so let's check that.
    // We need to deserialize the chunk to verify the name.
    // For now, we'll trust the name from the Header1 object descriptor.
    ASSERT_STR_EQ(obj1->name, "TestObject1");
    ASSERT_STR_EQ(obj2->name, "TestObject2");

    // 6. Cleanup
    nmo_session_destroy(load_session);
    nmo_context_release(load_ctx);
    remove(test_filename);

    test_add_result("file_io", "roundtrip", 1, NULL, __FILE__, __LINE__);
}

int main(void) {
    test_framework_init();

    test_file_io_roundtrip();

    return test_framework_run();
}
