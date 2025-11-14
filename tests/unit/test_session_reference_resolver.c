#include "test_framework.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "session/nmo_reference_resolver.h"

TEST(session_reference_resolver, ensure_and_reset) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    ASSERT_NULL(nmo_session_get_reference_resolver(session));

    nmo_reference_resolver_t *resolver = nmo_session_ensure_reference_resolver(session);
    ASSERT_NOT_NULL(resolver);
    ASSERT_EQ(resolver, nmo_session_get_reference_resolver(session));

    nmo_session_reset_reference_resolver(session);
    ASSERT_NULL(nmo_session_get_reference_resolver(session));

    nmo_reference_resolver_t *resolver_again = nmo_session_ensure_reference_resolver(session);
    ASSERT_NOT_NULL(resolver_again);
    ASSERT_EQ(resolver_again, nmo_session_get_reference_resolver(session));

    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(session_reference_resolver, ensure_and_reset);
TEST_MAIN_END()
