#include "test_framework.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"

#include <stdio.h>

TEST(runtime_load_pipeline, save_then_load_via_execute_path) {
    const char *temp_file = "test_runtime_load_pipeline_tmp.nmo";

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *writer = nmo_session_create(ctx);
    ASSERT_NOT_NULL(writer);

    nmo_object_id_t id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(writer, 1, "pipeline-object", (nmo_guid_t){0, 0}, &id, NULL));
    ASSERT_TRUE(id != 0);

    ASSERT_EQ(NMO_OK, nmo_session_save_file(writer, temp_file, NULL, NULL));

    nmo_session_t *reader = nmo_session_create(ctx);
    ASSERT_NOT_NULL(reader);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(reader, temp_file, NULL, NULL));

    nmo_object_t **objects = NULL;
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_session_get_objects(reader, &objects, &count));
    ASSERT_TRUE(count >= 1);

    nmo_session_destroy(reader);
    nmo_session_destroy(writer);
    nmo_context_release(ctx);
    remove(temp_file);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_load_pipeline, save_then_load_via_execute_path);
TEST_MAIN_END()
