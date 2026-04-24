#include "test_framework.h"

#include "document/nmo_document.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "format/nmo_object.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_bridge.h"
#include "../../src/runtime/runtime_internal.h"

TEST(object_edit_api, rename_through_workspace_owner) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    nmo_workspace_t *workspace = NULL;
    nmo_session_t *session = NULL;
    ASSERT_NOT_NULL(document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_NOT_NULL(workspace);
    session = nmo_workspace_internal_session(workspace);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t object_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, NMO_CID_OBJECT, "old-name", (nmo_guid_t){0, 0}, &object_id, NULL));

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "rename object", &edit));
    ASSERT_EQ(NMO_OK, nmo_object_edit_rename(edit, object_id, "new-name"));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *renamed = NULL;
    nmo_object_selector_t selector = {
        .name = "new-name"
    };
    ASSERT_EQ(NMO_OK,
              nmo_object_query_resolve_one(document, &selector, &renamed, NULL));
    ASSERT_NOT_NULL(renamed);
    ASSERT_EQ(object_id, nmo_object_get_id(renamed));

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(object_edit_api, rename_through_workspace_owner);
TEST_MAIN_END()



