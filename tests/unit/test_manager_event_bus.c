#include "test_framework.h"
#include "format/nmo_manager.h"
#include "core/nmo_guid.h"

typedef struct event_counter {
    uint32_t pre_load;
    uint32_t post_save;
} event_counter_t;

static int test_on_event(void *session, const nmo_runtime_event_ctx_t *ctx, void *user_data) {
    (void)session;
    event_counter_t *counter = (event_counter_t *)user_data;
    if (counter == NULL || ctx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->event == NMO_RUNTIME_EVENT_PRE_LOAD) {
        counter->pre_load++;
    } else if (ctx->event == NMO_RUNTIME_EVENT_POST_SAVE) {
        counter->post_save++;
    }
    return NMO_OK;
}

TEST(manager_event_bus, invoke_event_callback) {
    nmo_manager_t *manager = nmo_manager_create(
        nmo_guid_create(0x11223344, 0x55667788),
        "event-bus-manager",
        NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager);

    event_counter_t counter = {0};
    ASSERT_EQ(NMO_OK, nmo_manager_set_user_data(manager, &counter));
    ASSERT_EQ(NMO_OK, nmo_manager_set_on_event_hook(manager, test_on_event));

    nmo_runtime_event_ctx_t pre_load = {.event = NMO_RUNTIME_EVENT_PRE_LOAD};
    nmo_runtime_event_ctx_t post_save = {.event = NMO_RUNTIME_EVENT_POST_SAVE};
    ASSERT_EQ(NMO_OK, nmo_manager_invoke_event(manager, NULL, &pre_load));
    ASSERT_EQ(NMO_OK, nmo_manager_invoke_event(manager, NULL, &post_save));

    ASSERT_EQ(1u, counter.pre_load);
    ASSERT_EQ(1u, counter.post_save);
    nmo_manager_destroy(manager);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(manager_event_bus, invoke_event_callback);
TEST_MAIN_END()
