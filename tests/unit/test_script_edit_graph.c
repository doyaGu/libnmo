#include "../test_framework.h"

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_script_edit_graph.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_query.h"

#include <stdint.h>

static bool open_test_file(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session)
{
    char errbuf[256] = {0};
    return nmo_session_open_file_with_context(path, out_ctx, out_session,
                                              errbuf, sizeof(errbuf));
}

TEST(script_edit_graph, build_reports_edit_ready_graph_for_ballance_root)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_script_edit_graph_t *graph = NULL;
    nmo_status_t rc = nmo_script_edit_graph_build(workspace, 237u,
                                                  UINT32_MAX, &graph);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_NOT_NULL(graph);
    ASSERT_EQ(237u, nmo_script_edit_graph_root_behavior_id(graph));
    ASSERT_GT(nmo_script_edit_graph_node_count(graph), 0u);
    ASSERT_TRUE(nmo_script_edit_graph_owner_index_available(graph));
    ASSERT_TRUE(nmo_script_edit_graph_edit_ready(graph));

    const nmo_behavior_index_t *index = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(index);

    size_t control_edge_count = 0;
    const nmo_script_edit_control_edge_t *control_edges =
        nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    ASSERT_NOT_NULL(control_edges);
    ASSERT_GT(control_edge_count, 0u);

    for (size_t i = 0; i < control_edge_count; ++i) {
        const nmo_script_edit_control_edge_t *edge = &control_edges[i];
        const nmo_port_owner_t *source_owner =
            nmo_behavior_index_find(index, edge->source.object_id);
        const nmo_port_owner_t *target_owner =
            nmo_behavior_index_find(index, edge->target.object_id);

        ASSERT_NOT_NULL(source_owner);
        ASSERT_NOT_NULL(target_owner);
        ASSERT_EQ(edge->source.owner_behavior_id, source_owner->owner_id);
        ASSERT_EQ(edge->target.owner_behavior_id, target_owner->owner_id);
        ASSERT_EQ(edge->source.owner_index, source_owner->index);
        ASSERT_EQ(edge->target.owner_index, target_owner->index);
    }

    size_t broken_ref_count = 0;
    nmo_status_t ref_status =
        nmo_script_edit_graph_reference_validation_status(graph,
                                                         &broken_ref_count);
    ASSERT_EQ(NMO_OK, ref_status);
    ASSERT_EQ(0u, broken_ref_count);

    nmo_script_edit_graph_destroy(graph);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(script_edit_graph, preserves_explicit_parameter_types)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t input_id = 0;
    nmo_object_id_t output_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEOBJECT, "Owner", (nmo_guid_t){0, 0},
        &owner_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "Typed behavior", CKPGUID_BEHAVIOR,
        &behavior_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "Typed input", CKPGUID_PARAMETERIN,
        &input_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "Typed output", CKPGUID_PARAMETEROUT,
        &output_id, NULL));

    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(repo);
    ASSERT_NOT_NULL(registry);

    nmo_beobject_state_t *owner = (nmo_beobject_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, nmo_object_repository_find_by_id(repo, owner_id),
            CKPGUID_BEOBJECT);
    nmo_behavior_state_t *behavior = (nmo_behavior_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, nmo_object_repository_find_by_id(repo, behavior_id),
            CKPGUID_BEHAVIOR);
    nmo_parameterin_state_t *input = (nmo_parameterin_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, nmo_object_repository_find_by_id(repo, input_id),
            CKPGUID_PARAMETERIN);
    nmo_parameterout_state_t *output = (nmo_parameterout_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, nmo_object_repository_find_by_id(repo, output_id),
            CKPGUID_PARAMETEROUT);
    ASSERT_NOT_NULL(owner);
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(input);
    ASSERT_NOT_NULL(output);

    const nmo_guid_t value_type = {0x5A5716FDu, 0x44E276D7u};
    input->type_guid = value_type;
    output->base.type_guid = value_type;
    nmo_parameterin_set_source_id(input, output_id);
    nmo_parameterin_set_owner_id(input, behavior_id);
    nmo_parameterout_set_owner_id(output, behavior_id);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner->scripts, behavior_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->in_parameters, input_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->out_parameters, output_id, NULL));

    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    nmo_script_edit_graph_t *graph = NULL;
    ASSERT_EQ(NMO_OK, nmo_script_edit_graph_build(
        workspace, behavior_id, UINT32_MAX, &graph));
    ASSERT_NOT_NULL(graph);

    size_t edge_count = 0;
    const nmo_script_edit_data_edge_t *edges =
        nmo_script_edit_graph_data_edges(graph, &edge_count);
    ASSERT_NOT_NULL(edges);
    ASSERT_GT(edge_count, 0u);
    bool found = false;
    for (size_t i = 0; i < edge_count; ++i) {
        if (edges[i].source_parameter_id == output_id &&
            edges[i].target_parameter_id == input_id) {
            ASSERT_TRUE(nmo_guid_equals(value_type, edges[i].type_guid));
            ASSERT_EQ(behavior_id, edges[i].source_owner_id);
            ASSERT_EQ(behavior_id, edges[i].target_owner_id);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    nmo_script_edit_graph_destroy(graph);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_edit_graph, build_reports_edit_ready_graph_for_ballance_root);
    REGISTER_TEST(script_edit_graph, preserves_explicit_parameter_types);
TEST_MAIN_END()

