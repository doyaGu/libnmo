#include "test_framework.h"

#include "document/nmo_document.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_context.h"

TEST(workspace_edit, begin_commit_roundtrip) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    nmo_workspace_t *workspace = NULL;
    ASSERT_NOT_NULL(document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_NOT_NULL(workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "workspace edit", &edit));
    ASSERT_NOT_NULL(edit);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(workspace_edit, begin_commit_roundtrip);
TEST_MAIN_END()
