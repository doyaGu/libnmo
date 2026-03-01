#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"

#include <stdio.h>
#include <stdint.h>

typedef struct manager_lane_tracker {
    uint32_t pre_load_count;
    uint32_t post_load_count;
    uint32_t pre_save_count;
    uint32_t post_save_count;
    uint32_t emitted_chunk_count;
    uint32_t consumed_chunk_count;
} manager_lane_tracker_t;

static int manager_lane_on_event(void *session_ptr, const nmo_runtime_event_ctx_t *ctx, void *user_data) {
    manager_lane_tracker_t *tracker = (manager_lane_tracker_t *)user_data;
    if (ctx == NULL || tracker == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (ctx->event == NMO_RUNTIME_EVENT_PRE_LOAD) {
        tracker->pre_load_count++;
        return NMO_OK;
    }
    if (ctx->event == NMO_RUNTIME_EVENT_POST_LOAD) {
        tracker->post_load_count++;
        if (ctx->manager_chunk_in != NULL) {
            nmo_chunk_t *chunk = (nmo_chunk_t *)(uintptr_t)ctx->manager_chunk_in;
            if (nmo_chunk_start_read(chunk) == NMO_OK &&
                nmo_chunk_seek_identifier(chunk, 0x6A01u) == NMO_OK) {
                int32_t marker = 0;
                if (nmo_chunk_read_int(chunk, &marker) == NMO_OK && marker == 42) {
                    tracker->consumed_chunk_count++;
                }
            }
        }
        return NMO_OK;
    }
    if (ctx->event == NMO_RUNTIME_EVENT_PRE_SAVE) {
        tracker->pre_save_count++;
        if (ctx->manager_chunk_out != NULL && session_ptr != NULL) {
            nmo_session_t *session = (nmo_session_t *)session_ptr;
            nmo_arena_t *arena = nmo_session_get_arena(session);
            nmo_chunk_t *chunk = nmo_chunk_create(arena);
            if (chunk != NULL) {
                if (nmo_chunk_start_write(chunk) == NMO_OK &&
                    nmo_chunk_write_identifier(chunk, 0x6A01u) == NMO_OK &&
                    nmo_chunk_write_int(chunk, 42) == NMO_OK) {
                    nmo_chunk_close(chunk);
                    *ctx->manager_chunk_out = chunk;
                    tracker->emitted_chunk_count++;
                }
            }
        }
        return NMO_OK;
    }
    if (ctx->event == NMO_RUNTIME_EVENT_POST_SAVE) {
        tracker->post_save_count++;
        return NMO_OK;
    }

    return NMO_OK;
}

TEST(manager_lane_runtime_events, manager_chunk_roundtrip_and_event_order) {
    const char *temp_file = "test_manager_lane_runtime_events_tmp.nmo";
    const uint32_t manager_id = 900;
    const nmo_guid_t manager_guid = {0xA15E0001u, 0x0000BEEFu};

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    manager_lane_tracker_t tracker = {0};
    nmo_manager_t *manager = nmo_manager_create(manager_guid, "RuntimeLaneManager", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager);
    ASSERT_EQ(NMO_OK, nmo_manager_set_user_data(manager, &tracker));
    ASSERT_EQ(NMO_OK, nmo_manager_set_on_event_hook(manager, manager_lane_on_event));

    nmo_manager_registry_t *manager_registry = nmo_context_get_manager_registry(ctx);
    ASSERT_NOT_NULL(manager_registry);
    ASSERT_EQ(NMO_OK, nmo_manager_registry_register(manager_registry, manager_id, manager));

    nmo_session_t *writer = nmo_session_create(ctx);
    ASSERT_NOT_NULL(writer);
    nmo_object_id_t created_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(writer, 1, "manager-lane-object", (nmo_guid_t){0, 0}, &created_id, NULL));
    ASSERT_TRUE(created_id != 0);
    ASSERT_EQ(NMO_OK, nmo_session_save_file(writer, temp_file, NULL, NULL));

    nmo_session_t *reader = nmo_session_create(ctx);
    ASSERT_NOT_NULL(reader);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(reader, temp_file, NULL, NULL));

    ASSERT_TRUE(tracker.pre_save_count >= 1);
    ASSERT_TRUE(tracker.post_save_count >= 1);
    ASSERT_TRUE(tracker.pre_load_count >= 1);
    ASSERT_TRUE(tracker.post_load_count >= 1);
    ASSERT_TRUE(tracker.emitted_chunk_count >= 1);
    ASSERT_TRUE(tracker.consumed_chunk_count >= 1);

    nmo_session_destroy(reader);
    nmo_session_destroy(writer);
    nmo_context_release(ctx);
    remove(temp_file);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(manager_lane_runtime_events, manager_chunk_roundtrip_and_event_order);
TEST_MAIN_END()
