#include "test_framework.h"
#include "document/nmo_document.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
#include "../../src/runtime/runtime_internal.h"

#include <stdio.h>

TEST(runtime_load_pipeline, save_then_load_via_execute_path) {
    const char *temp_file = "test_runtime_load_pipeline_tmp.nmo";

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *writer_document = nmo_document_create(ctx);
    nmo_workspace_t *writer_workspace = NULL;
    ASSERT_NOT_NULL(writer_document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, writer_document, &writer_workspace));
    ASSERT_NOT_NULL(writer_workspace);

    nmo_session_t *writer = nmo_workspace_internal_session(writer_workspace);
    ASSERT_NOT_NULL(writer);

    nmo_object_id_t id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(writer, 1, "pipeline-object", (nmo_guid_t){0, 0}, &id, NULL));
    ASSERT_TRUE(id != 0);

    ASSERT_EQ(NMO_OK, nmo_document_save_file(writer_document, temp_file, NULL));

    nmo_document_t *reader_document = NULL;
    nmo_workspace_t *reader_workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, temp_file, NULL, &reader_document));
    ASSERT_NOT_NULL(reader_document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, reader_document, &reader_workspace));
    ASSERT_NOT_NULL(reader_workspace);

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(reader_document, NULL, &count));
    ASSERT_TRUE(count >= 1);

    nmo_workspace_destroy(reader_workspace);
    nmo_document_destroy(reader_document);
    nmo_workspace_destroy(writer_workspace);
    nmo_document_destroy(writer_document);
    nmo_context_release(ctx);
    remove(temp_file);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_load_pipeline, save_then_load_via_execute_path);
TEST_MAIN_END()



