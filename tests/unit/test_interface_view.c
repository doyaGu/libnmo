#include "test_framework.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_object.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_view.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "core/nmo_arena.h"
#include <string.h>

static nmo_interface_data_t *attach_interface_data(
    nmo_session_t *session,
    nmo_object_id_t behavior_id)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return NULL;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, behavior_id);
    if (object == NULL) {
        return NULL;
    }

    nmo_behavior_state_t *state =
        (nmo_behavior_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return NULL;
    }

    nmo_arena_t *arena = nmo_object_get_storage_arena(object);
    if (arena == NULL) {
        return NULL;
    }

    nmo_interface_data_t *data = (nmo_interface_data_t *)nmo_arena_alloc(
        arena, sizeof(*data), _Alignof(nmo_interface_data_t));
    if (data == NULL) {
        return NULL;
    }
    memset(data, 0, sizeof(*data));

    state->has_interface = true;
    state->interface_data = data;
    return data;
}

TEST(interface_view, summarizes_root_and_nested_behaviors_without_layout_access) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t root_behavior_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "Root", (nmo_guid_t){0, 0}, &root_behavior_id, NULL));

    nmo_object_id_t child_behavior_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "Child", (nmo_guid_t){0, 0}, &child_behavior_id, NULL));

    nmo_interface_data_t *data = attach_interface_data(session, root_behavior_id);
    ASSERT_NOT_NULL(data);
    data->version = NMO_INTERFACE_VERSION_MAX;
    data->format_flags = NMO_INTERFACE_FORMAT_SECTIONED | NMO_INTERFACE_FORMAT_ROOT_GRAPH;
    data->sub_count = 1;
    data->subs = (nmo_interface_behavior_t *)nmo_arena_alloc(
        nmo_object_get_storage_arena(
            nmo_object_repository_find_by_id(nmo_session_get_repository(session), root_behavior_id)),
        sizeof(nmo_interface_behavior_t),
        _Alignof(nmo_interface_behavior_t));
    ASSERT_NOT_NULL(data->subs);
    memset(data->subs, 0, sizeof(nmo_interface_behavior_t));

    data->extra.present = true;
    data->extra.entry_count = 2;
    data->script.behavior_id = root_behavior_id;
    data->script.flags = NMO_INTERFACE_FLAG_FOLDED;
    data->script.has_snapshot = true;
    data->script.body.has_body = true;
    data->script.body.link_count = 2;
    data->script.body.operation_count = 3;
    data->script.body.comment_count = 1;
    data->script.body.has_params = true;
    data->script.body.has_links_section = true;
    data->script.body.has_operations_section = true;
    data->script.body.has_comments_section = true;
    data->script.body.has_unknown_flag_section = true;
    data->script.body.unknown_flag = 77;
    data->script.body.params.local_count = 4;
    data->script.body.params.shared_count = 5;
    data->script.body.has_graph_io = true;
    data->script.body.graph_io = (nmo_interface_graph_io_t *)nmo_arena_alloc(
        nmo_object_get_storage_arena(
            nmo_object_repository_find_by_id(nmo_session_get_repository(session), root_behavior_id)),
        sizeof(nmo_interface_graph_io_t),
        _Alignof(nmo_interface_graph_io_t));
    ASSERT_NOT_NULL(data->script.body.graph_io);
    memset(data->script.body.graph_io, 0, sizeof(nmo_interface_graph_io_t));
    data->script.body.graph_io->inward_input_count = 1;
    data->script.body.graph_io->outward_input_count = 2;
    data->script.body.graph_io->inward_output_count = 3;
    data->script.body.graph_io->outward_output_count = 4;

    data->subs[0].behavior_id = child_behavior_id;
    data->subs[0].flags = NMO_INTERFACE_FLAG_HEADER_ONLY;
    data->subs[0].depth = 1;
    data->subs[0].body.has_body = false;

    nmo_interface_view_t root_view;
    ASSERT_EQ(NMO_OK,
        nmo_interface_view_from_behavior(session, root_behavior_id, &root_view));
    ASSERT_EQ(root_behavior_id, root_view.owner_behavior_id);
    ASSERT_EQ(root_behavior_id, root_view.behavior_id);
    ASSERT_TRUE(root_view.is_root);
    ASSERT_EQ(NMO_INTERFACE_VERSION_MAX, root_view.version);
    ASSERT_EQ(1u, root_view.sub_behavior_count);
    ASSERT_TRUE(root_view.extra_present);
    ASSERT_EQ(2u, root_view.extra_entry_count);
    ASSERT_TRUE(root_view.has_snapshot);
    ASSERT_TRUE(root_view.body.has_body);
    ASSERT_EQ(2u, root_view.body.link_count);
    ASSERT_EQ(3u, root_view.body.operation_count);
    ASSERT_EQ(1u, root_view.body.comment_count);
    ASSERT_TRUE(root_view.body.has_params);
    ASSERT_EQ(4u, root_view.body.local_param_count);
    ASSERT_EQ(5u, root_view.body.shared_param_count);
    ASSERT_TRUE(root_view.body.has_graph_io);
    ASSERT_TRUE(root_view.body.has_links_section);
    ASSERT_TRUE(root_view.body.has_operations_section);
    ASSERT_TRUE(root_view.body.has_comments_section);
    ASSERT_TRUE(root_view.body.has_unknown_flag_section);
    ASSERT_EQ(77, root_view.body.unknown_flag);
    ASSERT_EQ(1u, root_view.body.inward_input_count);
    ASSERT_EQ(2u, root_view.body.outward_input_count);
    ASSERT_EQ(3u, root_view.body.inward_output_count);
    ASSERT_EQ(4u, root_view.body.outward_output_count);

    nmo_interface_view_t child_view;
    ASSERT_EQ(NMO_OK,
        nmo_interface_view_find_behavior(
            session, root_behavior_id, child_behavior_id, &child_view));
    ASSERT_EQ(root_behavior_id, child_view.owner_behavior_id);
    ASSERT_EQ(child_behavior_id, child_view.behavior_id);
    ASSERT_FALSE(child_view.is_root);
    ASSERT_EQ(1u, child_view.depth);
    ASSERT_FALSE(child_view.body.has_body);
    ASSERT_EQ(0u, child_view.body.link_count);
    ASSERT_FALSE(child_view.body.has_links_section);
    ASSERT_FALSE(child_view.body.has_params);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(interface_view, missing_interface_returns_not_found_and_clears_view) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t behavior_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "No Interface", (nmo_guid_t){0, 0}, &behavior_id, NULL));

    nmo_interface_view_t view = {
        .owner_behavior_id = 123,
        .behavior_id = 456,
        .version = 789
    };
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
        nmo_interface_view_from_behavior(session, behavior_id, &view));
    ASSERT_EQ(0u, view.owner_behavior_id);
    ASSERT_EQ(0u, view.behavior_id);
    ASSERT_EQ(0u, view.version);
    ASSERT_FALSE(view.body.has_body);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(interface_view, summarizes_root_and_nested_behaviors_without_layout_access);
    REGISTER_TEST(interface_view, missing_interface_returns_not_found_and_clears_view);
TEST_MAIN_END()
