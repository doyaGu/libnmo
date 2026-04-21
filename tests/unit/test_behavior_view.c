#include "test_framework.h"

#include "behavior/nmo_behavior_view.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"

#include <stdint.h>
#include <string.h>

static nmo_object_id_t test_create_object(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name)
{
    nmo_object_id_t id = 0;
    if (nmo_session_create_object(
            session, class_id, name, (nmo_guid_t){0, 0}, &id, NULL) != NMO_OK) {
        return 0;
    }
    return id;
}

static nmo_object_t *test_find_object(
    nmo_session_t *session,
    nmo_object_id_t object_id)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return NULL;
    }
    return nmo_object_repository_find_by_id(repo, object_id);
}

static nmo_behavior_state_t *test_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t behavior_id)
{
    nmo_object_t *object = test_find_object(session, behavior_id);
    if (object == NULL) {
        return NULL;
    }
    return (nmo_behavior_state_t *)nmo_object_get_state(object);
}

static void test_append_id(nmo_array_t *array, nmo_object_id_t id)
{
    ASSERT_EQ(NMO_OK, nmo_array_append(array, &id));
}

static nmo_interface_data_t *test_attach_interface_data(
    nmo_session_t *session,
    nmo_object_id_t behavior_id)
{
    nmo_object_t *object = test_find_object(session, behavior_id);
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

static nmo_object_id_t test_create_behavior_link(
    nmo_session_t *session,
    nmo_behavior_state_t *owner,
    nmo_object_id_t source_io,
    nmo_object_id_t target_io)
{
    nmo_object_id_t link_id = test_create_object(
        session, NMO_CID_BEHAVIORLINK, "Link");
    if (link_id == 0) {
        return 0;
    }
    nmo_object_t *link_obj = test_find_object(session, link_id);
    if (link_obj == NULL) {
        return 0;
    }

    nmo_behaviorlink_state_t *link =
        (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    if (link == NULL) {
        return 0;
    }

    link->in_io_id = source_io;
    link->out_io_id = target_io;
    link->use_new_format = true;
    link->has_format = true;
    test_append_id(&owner->sub_behavior_links, link_id);
    return link_id;
}

TEST(behavior_view, summarizes_behavior_without_exposing_state_layout) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t behavior_id = test_create_object(session, NMO_CID_BEHAVIOR, "Graph");
    nmo_object_id_t child_id = test_create_object(session, NMO_CID_BEHAVIOR, "Child");
    nmo_object_id_t in_io_id = test_create_object(session, NMO_CID_BEHAVIORIO, "In");
    nmo_object_id_t out_io_id = test_create_object(session, NMO_CID_BEHAVIORIO, "Out");
    nmo_object_id_t in_param_id = test_create_object(session, NMO_CID_PARAMETERIN, "In Param");
    nmo_object_id_t out_param_id = test_create_object(session, NMO_CID_PARAMETEROUT, "Out Param");
    nmo_object_id_t local_param_id = test_create_object(session, NMO_CID_PARAMETERLOCAL, "Local Param");
    ASSERT_TRUE(behavior_id != 0);
    ASSERT_TRUE(child_id != 0);
    ASSERT_TRUE(in_io_id != 0);
    ASSERT_TRUE(out_io_id != 0);
    ASSERT_TRUE(in_param_id != 0);
    ASSERT_TRUE(out_param_id != 0);
    ASSERT_TRUE(local_param_id != 0);

    nmo_behavior_state_t *state = test_behavior_state(session, behavior_id);
    ASSERT_NOT_NULL(state);
    state->target_parameter_id = out_param_id;
    test_append_id(&state->sub_behaviors, child_id);
    test_append_id(&state->inputs, in_io_id);
    test_append_id(&state->outputs, out_io_id);
    test_append_id(&state->in_parameters, in_param_id);
    test_append_id(&state->out_parameters, out_param_id);
    test_append_id(&state->local_parameters, local_param_id);

    nmo_interface_data_t *idata = test_attach_interface_data(session, behavior_id);
    ASSERT_NOT_NULL(idata);
    idata->version = NMO_INTERFACE_VERSION_MAX;
    idata->script.behavior_id = behavior_id;
    idata->script.body.has_body = true;
    idata->script.body.link_count = 4;

    nmo_behavior_view_t view;
    ASSERT_EQ(NMO_OK, nmo_behavior_view_from_behavior(session, behavior_id, &view));
    ASSERT_EQ(behavior_id, view.behavior_id);
    ASSERT_EQ(NMO_CID_BEHAVIOR, view.class_id);
    ASSERT_STR_EQ("Graph", view.name);
    ASSERT_FALSE(view.is_building_block);
    ASSERT_TRUE(view.has_target_parameter);
    ASSERT_EQ(out_param_id, view.target_parameter_id);
    ASSERT_EQ(1u, view.sub_behavior_count);
    ASSERT_EQ(0u, view.link_count);
    ASSERT_EQ(0u, view.operation_count);
    ASSERT_EQ(1u, view.input_count);
    ASSERT_EQ(1u, view.output_count);
    ASSERT_EQ(1u, view.in_parameter_count);
    ASSERT_EQ(1u, view.out_parameter_count);
    ASSERT_EQ(1u, view.local_parameter_count);
    ASSERT_EQ(NMO_OK, view.edit_graph_status);
    ASSERT_TRUE(view.owner_index_available);
    ASSERT_TRUE(view.edit_ready);
    ASSERT_TRUE(view.has_interface);
    ASSERT_TRUE(view.interface_available);
    ASSERT_EQ(NMO_OK, view.interface_status);
    ASSERT_EQ(4u, view.interface_view.body.link_count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(behavior_view, summarizes_boundary_counts_without_exposing_graph_arrays) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t parent = test_create_object(session, NMO_CID_BEHAVIOR, "Parent");
    nmo_object_id_t anchor = test_create_object(session, NMO_CID_BEHAVIOR, "Anchor");
    nmo_object_id_t child = test_create_object(session, NMO_CID_BEHAVIOR, "Child");
    nmo_object_id_t external = test_create_object(session, NMO_CID_BEHAVIOR, "External");
    nmo_object_id_t anchor_in = test_create_object(session, NMO_CID_BEHAVIORIO, "Anchor In");
    nmo_object_id_t child_in = test_create_object(session, NMO_CID_BEHAVIORIO, "Child In");
    nmo_object_id_t child_out = test_create_object(session, NMO_CID_BEHAVIORIO, "Child Out");
    nmo_object_id_t external_in = test_create_object(session, NMO_CID_BEHAVIORIO, "External In");
    ASSERT_TRUE(parent != 0);
    ASSERT_TRUE(anchor != 0);
    ASSERT_TRUE(child != 0);
    ASSERT_TRUE(external != 0);
    ASSERT_TRUE(anchor_in != 0);
    ASSERT_TRUE(child_in != 0);
    ASSERT_TRUE(child_out != 0);
    ASSERT_TRUE(external_in != 0);

    nmo_behavior_state_t *parent_state = test_behavior_state(session, parent);
    nmo_behavior_state_t *anchor_state = test_behavior_state(session, anchor);
    nmo_behavior_state_t *child_state = test_behavior_state(session, child);
    nmo_behavior_state_t *external_state = test_behavior_state(session, external);
    ASSERT_NOT_NULL(parent_state);
    ASSERT_NOT_NULL(anchor_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(external_state);

    test_append_id(&parent_state->sub_behaviors, anchor);
    test_append_id(&parent_state->sub_behaviors, external);
    test_append_id(&anchor_state->inputs, anchor_in);
    test_append_id(&anchor_state->sub_behaviors, child);
    test_append_id(&child_state->inputs, child_in);
    test_append_id(&child_state->outputs, child_out);
    test_append_id(&external_state->inputs, external_in);

    ASSERT_TRUE(test_create_behavior_link(session, anchor_state, anchor_in, child_in) != 0);
    ASSERT_TRUE(test_create_behavior_link(session, anchor_state, child_out, external_in) != 0);

    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));

    nmo_behavior_boundary_view_t boundary;
    ASSERT_EQ(NMO_OK,
        nmo_behavior_view_describe_boundary(session, anchor, UINT32_MAX, &boundary));
    ASSERT_EQ(anchor, boundary.behavior_id);
    ASSERT_TRUE(boundary.internal_node_count >= 2u);
    ASSERT_EQ(0u, boundary.control_in_count);
    ASSERT_EQ(0u, boundary.control_out_count);
    ASSERT_EQ(0u, boundary.parameter_in_count);
    ASSERT_EQ(0u, boundary.parameter_out_count);
    ASSERT_EQ(0u, boundary.broken_links);
    ASSERT_EQ(0u, boundary.missing_nodes);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(behavior_view, summarizes_behavior_without_exposing_state_layout);
    REGISTER_TEST(behavior_view, summarizes_boundary_counts_without_exposing_graph_arrays);
TEST_MAIN_END()
