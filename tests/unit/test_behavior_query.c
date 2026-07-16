#include "test_framework.h"

#include "behavior/nmo_behavior_query.h"
#include "document/nmo_document.h"
#include "document/nmo_document_load.h"
#include "session/nmo_session.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session_pipeline.h"
#include "../../src/runtime/runtime_internal.h"
#include "runtime/nmo_context.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"

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

TEST(behavior_query, count_scripts_through_owner_api)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    size_t count = 0u;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(
        NMO_OK,
        nmo_document_load_file(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"), NULL, &document));
    ASSERT_NOT_NULL(document);

    ASSERT_EQ(NMO_OK, nmo_behavior_query_count_scripts(document, &count));
    ASSERT_TRUE(count > 0u);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(behavior_query, script_at_uses_owner_view_type)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_behavior_script_view_t script = {0};

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(
        NMO_OK,
        nmo_document_load_file(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"), NULL, &document));
    ASSERT_NOT_NULL(document);

    ASSERT_EQ(NMO_OK, nmo_behavior_query_script_at(document, 0u, &script));
    ASSERT_TRUE(script.script_id != 0u);
    ASSERT_TRUE(script.owner_id != 0u);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(behavior_query, synthetic_document_exposes_owner_and_script_summary)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_document_t *document = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *owner = NULL;
    nmo_beobject_state_t *owner_state = NULL;
    nmo_object_id_t owner_id = 0;
    nmo_object_id_t script_a = 0;
    nmo_object_id_t script_b = 0;
    size_t count = 0;
    nmo_behavior_script_view_t first = {0};
    nmo_behavior_script_view_t second = {0};
    nmo_behavior_script_view_t found = {0};

    ASSERT_NOT_NULL(ctx);
    document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    owner_id = create_object_or_zero(session, NMO_CID_BEOBJECT, "Owner");
    script_a = create_object_or_zero(session, NMO_CID_BEHAVIOR, "Script A");
    script_b = create_object_or_zero(session, NMO_CID_BEHAVIOR, "Script B");
    ASSERT_TRUE(owner_id != 0u);
    ASSERT_TRUE(script_a != 0u);
    ASSERT_TRUE(script_b != 0u);

    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    owner = nmo_object_repository_find_by_id(repo, owner_id);
    ASSERT_NOT_NULL(owner);

    owner_state = (nmo_beobject_state_t *)nmo_object_get_state(owner);
    ASSERT_NOT_NULL(owner_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, script_a));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, script_b));

    ASSERT_EQ(NMO_OK, nmo_behavior_query_count_scripts(document, &count));
    ASSERT_EQ(2u, count);

    ASSERT_EQ(NMO_OK, nmo_behavior_query_script_at(document, 0u, &first));
    ASSERT_EQ(owner_id, first.owner_id);
    ASSERT_EQ(NMO_CID_BEOBJECT, first.owner_class_id);
    ASSERT_STR_EQ("Owner", first.owner_name);
    ASSERT_TRUE(first.script_id == script_a || first.script_id == script_b);
    ASSERT_TRUE(first.script_name != NULL);

    ASSERT_EQ(NMO_OK, nmo_behavior_query_script_at(document, 1u, &second));
    ASSERT_EQ(owner_id, second.owner_id);
    ASSERT_TRUE(second.script_id != first.script_id);

    ASSERT_EQ(
        NMO_OK,
        nmo_behavior_query_script_from_script_id(document, script_b, &found));
    ASSERT_EQ(script_b, found.script_id);
    ASSERT_EQ(owner_id, found.owner_id);
    ASSERT_STR_EQ("Script B", found.script_name);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(behavior_query, missing_script_returns_not_found_and_clears_view)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    size_t count = 0;
    nmo_behavior_script_view_t missing = {
        .script_id = 123u,
        .owner_id = 456u
    };

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(
        NMO_OK,
        nmo_document_load_file(ctx, NMO_TEST_DATA_FILE("Nop.cmo"), NULL, &document));
    ASSERT_NOT_NULL(document);

    ASSERT_EQ(NMO_OK, nmo_behavior_query_count_scripts(document, &count));
    ASSERT_TRUE(count > 0u);

    ASSERT_EQ(
        NMO_ERR_NOT_FOUND,
        nmo_behavior_query_script_from_script_id(document, 999999u, &missing));
    ASSERT_EQ(0u, missing.script_id);
    ASSERT_EQ(0u, missing.owner_id);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(behavior_query, count_scripts_through_owner_api);
    REGISTER_TEST(behavior_query, script_at_uses_owner_view_type);
    REGISTER_TEST(behavior_query, synthetic_document_exposes_owner_and_script_summary);
    REGISTER_TEST(behavior_query, missing_script_returns_not_found_and_clears_view);
TEST_MAIN_END()


