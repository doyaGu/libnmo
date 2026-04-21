#include "test_framework.h"

#include "behavior/nmo_script_edit.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "format/nmo_object.h"

#include <stdio.h>

static void create_object_or_fail(nmo_session_t *session,
                                  nmo_class_id_t class_id,
                                  const char *name,
                                  nmo_object_id_t *out_id)
{
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, class_id, name, (nmo_guid_t){0, 0}, out_id, NULL));
    ASSERT_TRUE(*out_id != 0);
}

TEST(script_edit_transaction, rollback_restores_original_state_after_validation_failure)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_t *child_obj = NULL;
    nmo_3dentity_state_t *child_state = NULL;
    nmo_object_id_t parent_id = 0;
    nmo_object_id_t child_id = 0;
    char parent_text[32];

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    create_object_or_fail(session, NMO_CID_3DENTITY, "parent", &parent_id);
    create_object_or_fail(session, NMO_CID_3DENTITY, "child", &child_id);

    child_obj = nmo_object_repository_find_by_id(repo, child_id);
    ASSERT_NOT_NULL(child_obj);
    child_state = (nmo_3dentity_state_t *)nmo_object_get_state(child_obj);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(0u, child_state->parent_id);

    ASSERT_EQ(NMO_OK,
              nmo_script_edit_begin(ctx, session, "rollback-test", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_NOT_NULL(nmo_script_edit_session_edit(tx));

    snprintf(parent_text, sizeof(parent_text), "%u", parent_id);
    {
        nmo_session_field_edit_t field = {"parent_id", parent_text};
        ASSERT_EQ(NMO_OK,
                  nmo_session_edit_set_object_fields(
                      nmo_script_edit_session_edit(tx), child_id, &field, 1, NULL));
    }
    ASSERT_EQ(parent_id, child_state->parent_id);

    ASSERT_EQ(NMO_OK,
              nmo_session_edit_snapshot_bytes(
                  nmo_script_edit_session_edit(tx),
                  &child_state->parent_id,
                  sizeof(child_state->parent_id)));
    child_state->parent_id = 999999u;
    nmo_script_edit_mark(
        tx, NMO_SESSION_EDIT_OBJECT_STATE | NMO_SESSION_EDIT_REFERENCES);

    ASSERT_NE(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(999999u, child_state->parent_id);

    nmo_script_edit_rollback(tx);
    ASSERT_EQ(0u, child_state->parent_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_edit_transaction,
                  rollback_restores_original_state_after_validation_failure);
TEST_MAIN_END()
