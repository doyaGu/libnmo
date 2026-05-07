#include "test_framework.h"

#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_entity_edit.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

typedef struct entity_edit_fixture {
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} entity_edit_fixture_t;

static void entity_edit_fixture_destroy(entity_edit_fixture_t *fixture)
{
    if (!fixture) {
        return;
    }
    nmo_workspace_destroy(fixture->workspace);
    nmo_document_destroy(fixture->document);
    nmo_context_release(fixture->ctx);
}

static void entity_edit_fixture_create(entity_edit_fixture_t *fixture)
{
    fixture->ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->document = nmo_document_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->document);
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(fixture->ctx,
                                   fixture->document,
                                   &fixture->workspace));
}

static nmo_object_t *find_object(nmo_document_t *document, nmo_object_id_t id)
{
    nmo_object_t *object = NULL;
    if (nmo_object_query_find_first(
            document,
            &(nmo_object_query_t){.object_id = id},
            &object,
            NULL) != NMO_OK) {
        return NULL;
    }
    return object;
}

TEST(entity_edit, sets_parent)
{
    entity_edit_fixture_t fixture = {0};
    entity_edit_fixture_create(&fixture);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "parent", &edit));

    nmo_object_id_t parent_id = 0;
    nmo_object_id_t child_id = 0;
    nmo_object_id_t material_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Parent",
                  },
                  &parent_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Child",
                  },
                  &child_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MATERIAL,
                      .name = "Material",
                  },
                  &material_id));

    ASSERT_EQ(NMO_OK,
              nmo_entity_edit_set_parent(edit, child_id, parent_id));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_entity_edit_set_parent(edit, material_id, parent_id));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_entity_edit_set_parent(edit, child_id, material_id));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_entity_edit_set_parent(edit, child_id, 999999u));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *child_object = find_object(fixture.document, child_id);
    ASSERT_NOT_NULL(child_object);
    const nmo_3dentity_state_t *child =
        (const nmo_3dentity_state_t *)nmo_object_get_state(child_object);
    ASSERT_NOT_NULL(child);
    ASSERT_EQ(parent_id, child->parent_id);
    ASSERT_FALSE(child->has_parent_chunk);

    entity_edit_fixture_destroy(&fixture);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(entity_edit, sets_parent);
TEST_MAIN_END()
