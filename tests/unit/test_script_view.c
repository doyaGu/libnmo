#include "test_framework.h"

#include "behavior/nmo_script_view.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_util.h"

static nmo_object_id_t create_object_or_zero(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name)
{
    nmo_object_id_t id = 0;
    if (nmo_session_create_object(session, class_id, name,
            (nmo_guid_t){0, 0}, &id, NULL) != NMO_OK) {
        return 0;
    }
    return id;
}

TEST(script_view, synthetic_session_exposes_owner_and_script_summary) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t owner_id = create_object_or_zero(session, NMO_CID_BEOBJECT, "Owner");
    nmo_object_id_t script_a = create_object_or_zero(session, NMO_CID_BEHAVIOR, "Script A");
    nmo_object_id_t script_b = create_object_or_zero(session, NMO_CID_BEHAVIOR, "Script B");
    ASSERT_TRUE(owner_id != 0);
    ASSERT_TRUE(script_a != 0);
    ASSERT_TRUE(script_b != 0);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    nmo_object_t *owner = nmo_object_repository_find_by_id(repo, owner_id);
    ASSERT_NOT_NULL(owner);

    nmo_beobject_state_t *owner_state =
        (nmo_beobject_state_t *)nmo_object_get_state(owner);
    ASSERT_NOT_NULL(owner_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&owner_state->script_ids, &script_a));
    ASSERT_EQ(NMO_OK, nmo_array_append(&owner_state->script_ids, &script_b));

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_script_view_count(session, &count));
    ASSERT_EQ(2u, count);

    nmo_script_view_t first;
    ASSERT_EQ(NMO_OK, nmo_script_view_at(session, 0, &first));
    ASSERT_EQ(owner_id, first.owner_id);
    ASSERT_EQ(NMO_CID_BEOBJECT, first.owner_class_id);
    ASSERT_STR_EQ("Owner", first.owner_name);
    ASSERT_TRUE(first.script_id == script_a || first.script_id == script_b);
    ASSERT_TRUE(first.script_name != NULL);

    nmo_script_view_t second;
    ASSERT_EQ(NMO_OK, nmo_script_view_at(session, 1, &second));
    ASSERT_EQ(owner_id, second.owner_id);
    ASSERT_TRUE(second.script_id != first.script_id);

    nmo_script_view_t found;
    ASSERT_EQ(NMO_OK, nmo_script_view_from_script_id(session, script_b, &found));
    ASSERT_EQ(script_b, found.script_id);
    ASSERT_EQ(owner_id, found.owner_id);
    ASSERT_STR_EQ("Script B", found.script_name);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_view, file_session_reports_not_found_for_missing_script) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    if (!nmo_session_open_file_with_context(
            NMO_TEST_DATA_FILE("Nop.cmo"), &ctx, &session, errbuf, sizeof(errbuf))) {
        return;
    }

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_script_view_count(session, &count));
    ASSERT_TRUE(count > 0);

    nmo_script_view_t missing = {
        .script_id = 123,
        .owner_id = 456
    };
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
        nmo_script_view_from_script_id(session, 999999u, &missing));
    ASSERT_EQ(0u, missing.script_id);
    ASSERT_EQ(0u, missing.owner_id);

    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_view, synthetic_session_exposes_owner_and_script_summary);
    REGISTER_TEST(script_view, file_session_reports_not_found_for_missing_script);
TEST_MAIN_END()
