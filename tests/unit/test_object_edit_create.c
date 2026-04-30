#include "test_framework.h"

#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

typedef struct object_edit_fixture {
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} object_edit_fixture_t;

static void object_edit_fixture_destroy(object_edit_fixture_t *fixture)
{
    if (fixture == NULL) {
        return;
    }
    if (fixture->workspace != NULL) {
        nmo_workspace_destroy(fixture->workspace);
    }
    if (fixture->document != NULL) {
        nmo_document_destroy(fixture->document);
    }
    if (fixture->ctx != NULL) {
        nmo_context_release(fixture->ctx);
    }
}

static void object_edit_fixture_create(object_edit_fixture_t *fixture)
{
    fixture->ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->document = nmo_document_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document, &fixture->workspace));
    ASSERT_NOT_NULL(fixture->workspace);
}

TEST(object_edit_create, commit_keeps_created_object) {
    object_edit_fixture_t fixture = {0};
    object_edit_fixture_create(&fixture);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "create camera", &edit));

    nmo_object_create_desc_t desc = {
        .class_id = NMO_CID_CAMERA,
        .name = "CreatedCamera",
        .type_guid = NMO_GUID_NULL,
    };
    nmo_object_id_t object_id = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(edit, &desc, &object_id));
    ASSERT_TRUE(object_id != 0);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *created = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_object_query_resolve_one(
                  fixture.document,
                  &(nmo_object_selector_t){.name = "CreatedCamera"},
                  &created,
                  NULL));
    ASSERT_NOT_NULL(created);
    ASSERT_EQ(object_id, nmo_object_get_id(created));

    object_edit_fixture_destroy(&fixture);
}

TEST(object_edit_create, rollback_removes_created_object) {
    object_edit_fixture_t fixture = {0};
    object_edit_fixture_create(&fixture);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "create rollback", &edit));

    nmo_object_create_desc_t desc = {
        .class_id = NMO_CID_LIGHT,
        .name = "TransientLight",
        .type_guid = NMO_GUID_NULL,
    };
    nmo_object_id_t object_id = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(edit, &desc, &object_id));
    ASSERT_TRUE(object_id != 0);
    nmo_workspace_edit_rollback(edit);

    size_t count = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_query_count(
                  fixture.document,
                  &(nmo_object_query_t){.name = "TransientLight"},
                  &count));
    ASSERT_EQ(0u, count);

    object_edit_fixture_destroy(&fixture);
}

TEST(object_edit_create, rejects_invalid_arguments) {
    object_edit_fixture_t fixture = {0};
    object_edit_fixture_create(&fixture);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "create invalid", &edit));

    nmo_object_id_t object_id = 99;
    nmo_object_create_desc_t valid_desc = {
        .class_id = NMO_CID_OBJECT,
        .name = "Valid",
        .type_guid = NMO_GUID_NULL,
    };
    nmo_object_create_desc_t invalid_class_desc = {
        .class_id = 0,
        .name = "Invalid",
        .type_guid = NMO_GUID_NULL,
    };

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_edit_create(NULL, &valid_desc, &object_id));
    ASSERT_EQ(0u, object_id);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_edit_create(edit, NULL, &object_id));
    ASSERT_EQ(0u, object_id);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_edit_create(edit, &valid_desc, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_edit_create(edit, &invalid_class_desc, &object_id));
    ASSERT_EQ(0u, object_id);

    nmo_workspace_edit_rollback(edit);
    object_edit_fixture_destroy(&fixture);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(object_edit_create, commit_keeps_created_object);
REGISTER_TEST(object_edit_create, rollback_removes_created_object);
REGISTER_TEST(object_edit_create, rejects_invalid_arguments);
TEST_MAIN_END()
