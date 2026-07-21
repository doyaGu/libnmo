#include "../test_framework.h"
#include "session/nmo_session.h"
#include "runtime/nmo_context.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h" // Include for nmo_object_t definition
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <string.h>

static const char *k_test_filename = "test_io.nmo";
static nmo_context_t *g_save_ctx = NULL;
static nmo_session_t *g_save_session = NULL;
static nmo_context_t *g_load_ctx = NULL;
static nmo_session_t *g_load_session = NULL;

static void teardown_file_io(void) {
    if (g_load_session != NULL) {
        nmo_session_destroy(g_load_session);
        g_load_session = NULL;
    }
    if (g_load_ctx != NULL) {
        nmo_context_release(g_load_ctx);
        g_load_ctx = NULL;
    }
    if (g_save_session != NULL) {
        nmo_session_destroy(g_save_session);
        g_save_session = NULL;
    }
    if (g_save_ctx != NULL) {
        nmo_context_release(g_save_ctx);
        g_save_ctx = NULL;
    }
    remove(k_test_filename);
}

static nmo_status_t attach_test_chunk(
    nmo_session_t *session,
    nmo_object_t *object,
    nmo_class_id_t class_id,
    uint32_t marker)
{
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(session));
    if (chunk == NULL) {
        return NMO_ERR_NOMEM;
    }
    chunk->class_id = class_id;
    chunk->chunk_version = 7;
    chunk->data_version = 7;
    nmo_status_t result = nmo_chunk_start_write(chunk);
    if (result == NMO_OK) {
        result = nmo_chunk_write_dword(chunk, marker);
    }
    if (result != NMO_OK) {
        return result;
    }
    nmo_chunk_close(chunk);
    return nmo_object_set_chunk(object, chunk);
}

// Helper to create a session with a few objects
nmo_session_t* create_test_session(nmo_context_t* ctx) {
    nmo_session_t* session = nmo_session_create(ctx);
    if (!session) {
        fprintf(stderr, "ERROR: Failed to create session\n");
        return NULL;
    }

    nmo_object_repository_t* repo = nmo_session_get_repository(session);
    const nmo_allocator_t *allocator = nmo_context_get_allocator(ctx);

    // Object 1
    nmo_object_t *obj1 = nmo_object_create(allocator, 1, 101);
    if (obj1 == NULL ||
        nmo_object_set_name(obj1, "TestObject1") != NMO_OK ||
        attach_test_chunk(session, obj1, 101, 0x11111111u) != NMO_OK ||
        nmo_object_repository_add(repo, &obj1) != NMO_OK) {
        nmo_object_destroy(obj1);
        nmo_session_destroy(session);
        fprintf(stderr, "ERROR: Failed to add object 1\n");
        return NULL;
    }

    // Object 2
    nmo_object_t *obj2 = nmo_object_create(allocator, 2, 102);
    if (obj2 == NULL ||
        nmo_object_set_name(obj2, "TestObject2") != NMO_OK ||
        attach_test_chunk(session, obj2, 102, 0x22222222u) != NMO_OK ||
        nmo_object_repository_add(repo, &obj2) != NMO_OK) {
        nmo_object_destroy(obj2);
        nmo_session_destroy(session);
        fprintf(stderr, "ERROR: Failed to add object 2\n");
        return NULL;
    }

    return session;
}

/**
 * Test file save and load round trip
 */
TEST(file_io, roundtrip) {
    // 1. Create a context and a session with some data
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    g_save_ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(g_save_ctx);

    g_save_session = create_test_session(g_save_ctx);
    ASSERT_NOT_NULL(g_save_session);

    // 2. Save the session to a file
    int save_result = nmo_save_file(g_save_session, k_test_filename, NULL);
    ASSERT_EQ(save_result, NMO_OK);

    nmo_session_destroy(g_save_session);
    g_save_session = NULL;
    nmo_context_release(g_save_ctx);
    g_save_ctx = NULL;

    // 3. Create a new context and session to load the file into
    nmo_context_desc_t load_desc = {0};  // Zero-initialized for defaults
    g_load_ctx = nmo_context_create(&load_desc);
    ASSERT_NOT_NULL(g_load_ctx);
    g_load_session = nmo_session_create(g_load_ctx);
    ASSERT_NOT_NULL(g_load_session);

    // 4. Load the file
    int load_result = nmo_load_file(g_load_session, k_test_filename, NULL);
    ASSERT_EQ(load_result, NMO_OK);

    // 5. Verify the loaded data
    nmo_object_repository_t* load_repo = nmo_session_get_repository(g_load_session);
    size_t object_count = 0;
    nmo_object_t** loaded_objects = nmo_object_repository_get_all(load_repo, &object_count);
    (void)loaded_objects;

    ASSERT_EQ(object_count, 2);

    nmo_object_t* obj1 = nmo_object_repository_find_by_name(load_repo, "TestObject1");
    nmo_object_t* obj2 = nmo_object_repository_find_by_name(load_repo, "TestObject2");

    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);

    ASSERT_EQ(nmo_object_get_class_id(obj1), 101);
    ASSERT_EQ(nmo_object_get_class_id(obj2), 102);

    // The default serializer only saves the name, so let's check that.
    // We need to deserialize the chunk to verify the name.
    // For now, we'll trust the name from the Header1 object descriptor.
    ASSERT_STR_EQ(nmo_object_get_name(obj1), "TestObject1");
    ASSERT_STR_EQ(nmo_object_get_name(obj2), "TestObject2");

    // Cleanup is handled by the registered teardown, including assertion failures.
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_WITH_TEARDOWN(file_io, roundtrip, teardown_file_io);
TEST_MAIN_END()

