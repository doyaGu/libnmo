#include "test_framework.h"

#include "behavior/nmo_behavior_query.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"

TEST(behavior_query, count_scripts_through_owner_api)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    size_t count = 0u;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    document = (nmo_document_t *)nmo_session_load(
        ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(document);

    ASSERT_EQ(NMO_OK, nmo_behavior_query_count_scripts(document, &count));
    ASSERT_TRUE(count > 0u);

    nmo_session_destroy((nmo_session_t *)document);
    nmo_context_release(ctx);
}

TEST(behavior_query, script_at_uses_owner_view_type)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_behavior_script_view_t script = {0};

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    document = (nmo_document_t *)nmo_session_load(
        ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(document);

    ASSERT_EQ(NMO_OK, nmo_behavior_query_script_at(document, 0u, &script));
    ASSERT_TRUE(script.script_id != 0u);
    ASSERT_TRUE(script.owner_id != 0u);

    nmo_session_destroy((nmo_session_t *)document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(behavior_query, count_scripts_through_owner_api);
    REGISTER_TEST(behavior_query, script_at_uses_owner_view_type);
TEST_MAIN_END()
