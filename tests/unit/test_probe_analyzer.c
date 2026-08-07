#include "test_framework.h"

#include "behavior/nmo_probe_analyzer.h"
#include "core/nmo_array.h"
#include "document/nmo_document.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_session.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

typedef struct probe_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} probe_fixture_t;

static void probe_fixture_init(probe_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->session =
        nmo_session_load(fixture->ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(fixture->session);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(fixture->session,
                                                  &fixture->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document,
                                           &fixture->workspace));
}

static void probe_fixture_init_empty(probe_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->session = nmo_session_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->session);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(fixture->session,
                                                  &fixture->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document,
                                           &fixture->workspace));
}

static void probe_fixture_dispose(probe_fixture_t *fixture)
{
    if (fixture->workspace) {
        nmo_workspace_destroy(fixture->workspace);
    }
    if (fixture->document) {
        nmo_document_destroy(fixture->document);
    }
    if (fixture->session) {
        nmo_session_close_with_context(fixture->ctx, fixture->session);
        fixture->ctx = NULL;
    }
    if (fixture->ctx) {
        nmo_context_release(fixture->ctx);
    }
    memset(fixture, 0, sizeof(*fixture));
}

static nmo_behavior_state_t *probe_behavior_state(probe_fixture_t *fixture,
                                                  nmo_object_id_t behavior_id)
{
    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture->session);
    if (repo == NULL) {
        return NULL;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, behavior_id);
    if (object == NULL ||
        nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    return (nmo_behavior_state_t *)nmo_object_get_state(object);
}

static nmo_object_id_t probe_create_behavior(probe_fixture_t *fixture,
                                             const char *name)
{
    nmo_object_id_t id = 0u;
    if (nmo_session_create_object(fixture->session,
                                  NMO_CID_BEHAVIOR,
                                  name,
                                  NMO_GUID_NULL,
                                  &id,
                                  NULL) != NMO_OK) {
        return 0u;
    }
    return id;
}

TEST(probe_analyzer, selects_unique_message_candidate_and_link)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_MESSAGE;
    request.behavior_id = 2172u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_OK,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_AUTO, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED, result.status);
    ASSERT_EQ(1667u, result.selected_node_id);
    ASSERT_EQ(2152u, result.selected_link_id);
    ASSERT_TRUE(result.safe_insertion.selected);
    ASSERT_EQ(1667u, result.safe_insertion.selected_node_id);
    ASSERT_EQ(2152u, result.safe_insertion.remove_link_id);
    ASSERT_TRUE(result.safe_insertion.insert_from_io_id != 0u);
    ASSERT_TRUE(result.safe_insertion.insert_to_io_id != 0u);
    ASSERT_EQ(1u, result.candidate_count);
    ASSERT_NOT_NULL(result.candidates);
    ASSERT_EQ(1667u, result.candidates[0].node_id);
    ASSERT_EQ(2172u, result.candidates[0].parent_id);
    ASSERT_EQ(2172u, result.candidates[0].boundary_behavior_id);
    ASSERT_EQ(NMO_PROBE_CANDIDATE_MESSAGE_SENDER,
              result.candidates[0].role);
    ASSERT_STR_EQ("sender",
                  nmo_probe_candidate_role_name(result.candidates[0].role));
    ASSERT_TRUE(result.from_io_id != 0u);
    ASSERT_TRUE(result.to_io_id != 0u);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, selects_explicit_message_graph_types)
{
    probe_fixture_t fixture;
    probe_fixture_init_empty(&fixture);

    nmo_object_id_t root_id = 0u;
    nmo_object_id_t child_id = 0u;
    nmo_object_id_t root_io_id = 0u;
    nmo_object_id_t child_io_id = 0u;
    nmo_object_id_t link_id = 0u;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed root", CKPGUID_BEHAVIOR,
        &root_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed sender", CKPGUID_BEHAVIOR,
        &child_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, NMO_CID_BEHAVIORIO, "Root IO", NMO_GUID_NULL,
        &root_io_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, NMO_CID_BEHAVIORIO, "Child IO", NMO_GUID_NULL,
        &child_io_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed link", CKPGUID_BEHAVIORLINK,
        &link_id, NULL));

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    const nmo_type_registry_t *registry =
        nmo_context_get_type_registry(fixture.ctx);
    nmo_behavior_state_t *root = (nmo_behavior_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, root_id),
            CKPGUID_BEHAVIOR);
    nmo_behavior_state_t *child = (nmo_behavior_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, child_id),
            CKPGUID_BEHAVIOR);
    nmo_behaviorlink_state_t *link = (nmo_behaviorlink_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, link_id),
            CKPGUID_BEHAVIORLINK);
    ASSERT_NOT_NULL(root);
    ASSERT_NOT_NULL(child);
    ASSERT_NOT_NULL(link);

    child->flags = CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_MESSAGESENDER;
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->sub_behaviors, child_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->sub_behavior_links, link_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &child->outputs, child_io_id, NULL));
    nmo_behaviorlink_set_in_io_id(link, root_io_id);
    nmo_behaviorlink_set_out_io_id(link, child_io_id);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_MESSAGE;
    request.behavior_id = root_id;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_OK,
              nmo_probe_analyze_selector(
                  fixture.workspace, &request, &result));
    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_AUTO, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED, result.status);
    ASSERT_EQ(child_id, result.selected_node_id);
    ASSERT_EQ(link_id, result.selected_link_id);
    ASSERT_EQ(1u, result.candidate_count);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, honors_explicit_behavior_type_precedence)
{
    probe_fixture_t fixture;
    probe_fixture_init_empty(&fixture);

    nmo_object_id_t object_id = 0u;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session,
        NMO_CID_BEHAVIOR,
        "Not a behavior",
        CKPGUID_DATAARRAY,
        &object_id,
        NULL));

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_MESSAGE;
    request.behavior_id = object_id;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(
                  fixture.workspace, &request, &result));
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_NONE, result.status);
    ASSERT_STR_EQ("behavior_not_found", result.rejection_code);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, resolves_explicit_operation_write_site)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 3798u;
    request.dataarray_id = 6067u;
    request.row = 0u;
    request.col = 1u;
    request.has_data_cell = true;
    request.write_operation_id = 3791u;
    request.remove_link_id = 3780u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_OK,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED, result.status);
    ASSERT_EQ(3791u, result.selected_operation_id);
    ASSERT_EQ(3780u, result.selected_link_id);
    ASSERT_TRUE(result.safe_insertion.selected);
    ASSERT_EQ(3791u, result.safe_insertion.selected_operation_id);
    ASSERT_EQ(3780u, result.safe_insertion.remove_link_id);
    ASSERT_EQ(1u, result.candidate_count);
    ASSERT_EQ(6067u, result.candidates[0].dataarray_id);
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_STRING,
                                result.candidates[0].column_type_guid));
    ASSERT_EQ(3789u, result.candidates[0].source_parameter_id);
    ASSERT_EQ(3717u, result.candidates[0].value_parameter_id);
    ASSERT_TRUE(result.from_io_id != 0u);
    ASSERT_TRUE(result.to_io_id != 0u);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, reports_operation_write_site_candidates)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 3798u;
    request.dataarray_id = 6067u;
    request.row = 0u;
    request.col = 1u;
    request.has_data_cell = true;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_AUTO, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS, result.status);
    bool found_operation = false;
    for (size_t i = 0; i < result.candidate_count; ++i) {
        if (result.candidates[i].operation_id == 3791u) {
            found_operation = true;
            ASSERT_EQ(NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION,
                      result.candidates[i].role);
            ASSERT_STR_EQ(
                "data_write_operation",
                nmo_probe_candidate_role_name(result.candidates[i].role));
            ASSERT_EQ(3798u, result.candidates[i].boundary_behavior_id);
            ASSERT_TRUE(result.candidates[i].link_id != 0u);
            ASSERT_EQ(6067u, result.candidates[i].dataarray_id);
            ASSERT_TRUE(nmo_guid_equals(
                CKPGUID_STRING, result.candidates[i].column_type_guid));
            ASSERT_EQ(3789u, result.candidates[i].source_parameter_id);
            ASSERT_EQ(3717u, result.candidates[i].value_parameter_id);
        }
    }
    ASSERT_TRUE(found_operation);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, infers_auto_data_writer_cell_metadata)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 4692u;
    request.dataarray_id = 6067u;
    request.row = 0u;
    request.col = 1u;
    request.has_data_cell = true;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_OK,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_AUTO, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED, result.status);
    ASSERT_EQ(1u, result.candidate_count);
    ASSERT_EQ(NMO_PROBE_CANDIDATE_DATA_WRITER, result.candidates[0].role);
    ASSERT_EQ(6067u, result.candidates[0].dataarray_id);
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_STRING,
                                result.candidates[0].column_type_guid));
    ASSERT_TRUE(result.safe_insertion.selected);
    ASSERT_EQ(4689u, result.safe_insertion.remove_link_id);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, rejects_operation_data_writer_column_type_mismatch)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 3798u;
    request.dataarray_id = 6067u;
    request.row = 0u;
    request.col = 2u;
    request.has_data_cell = true;
    request.write_operation_id = 3791u;
    request.remove_link_id = 3780u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_UNSAFE, result.status);
    ASSERT_STR_EQ("type_mismatch", result.rejection_code);
    ASSERT_EQ(1u, result.candidate_count);
    ASSERT_EQ(6067u, result.candidates[0].dataarray_id);
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_BOOL,
                                result.candidates[0].column_type_guid));
    ASSERT_EQ(3717u, result.candidates[0].value_parameter_id);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, analyzes_explicit_operation_value_flow)
{
    probe_fixture_t fixture;
    probe_fixture_init_empty(&fixture);

    nmo_object_id_t root_id = 0u;
    nmo_object_id_t in_io_id = 0u;
    nmo_object_id_t out_io_id = 0u;
    nmo_object_id_t link_id = 0u;
    nmo_object_id_t input_id = 0u;
    nmo_object_id_t output_id = 0u;
    nmo_object_id_t operation_id = 0u;
    nmo_object_id_t dataarray_id = 0u;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed root", CKPGUID_BEHAVIOR,
        &root_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, NMO_CID_BEHAVIORIO, "Input IO", NMO_GUID_NULL,
        &in_io_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, NMO_CID_BEHAVIORIO, "Output IO", NMO_GUID_NULL,
        &out_io_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed link", CKPGUID_BEHAVIORLINK,
        &link_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed input", CKPGUID_PARAMETERIN,
        &input_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed output", CKPGUID_PARAMETEROUT,
        &output_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed operation", CKPGUID_PARAMETEROPERATION,
        &operation_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session, 0, "Typed array", CKPGUID_DATAARRAY,
        &dataarray_id, NULL));

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    const nmo_type_registry_t *registry =
        nmo_context_get_type_registry(fixture.ctx);
    nmo_behavior_state_t *root = (nmo_behavior_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, root_id),
            CKPGUID_BEHAVIOR);
    nmo_behaviorlink_state_t *link = (nmo_behaviorlink_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, link_id),
            CKPGUID_BEHAVIORLINK);
    nmo_parameterin_state_t *input = (nmo_parameterin_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, input_id),
            CKPGUID_PARAMETERIN);
    nmo_parameterout_state_t *output = (nmo_parameterout_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, output_id),
            CKPGUID_PARAMETEROUT);
    nmo_parameteroperation_state_t *operation =
        (nmo_parameteroperation_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                registry,
                nmo_object_repository_find_by_id(repo, operation_id),
                CKPGUID_PARAMETEROPERATION);
    nmo_dataarray_state_t *dataarray = (nmo_dataarray_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            nmo_object_repository_find_by_id(repo, dataarray_id),
            CKPGUID_DATAARRAY);
    ASSERT_NOT_NULL(root);
    ASSERT_NOT_NULL(link);
    ASSERT_NOT_NULL(input);
    ASSERT_NOT_NULL(output);
    ASSERT_NOT_NULL(operation);
    ASSERT_NOT_NULL(dataarray);

    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->inputs, in_io_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->outputs, out_io_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->sub_behavior_links, link_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->in_parameters, input_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->out_parameters, output_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root->operations, operation_id, NULL));
    nmo_behaviorlink_set_in_io_id(link, in_io_id);
    nmo_behaviorlink_set_out_io_id(link, out_io_id);

    input->type_guid = CKPGUID_INT;
    input->source = nmo_ref_from_id(output_id);
    output->base.type_guid = CKPGUID_INT;
    operation->has_owner = 1u;
    operation->has_in1 = 1u;
    operation->has_out = 1u;
    nmo_parameteroperation_set_owner_id(operation, root_id);
    nmo_parameteroperation_set_in1_id(operation, input_id);
    nmo_parameteroperation_set_out_id(operation, output_id);

    dataarray->column_count = 1u;
    dataarray->column_formats = (nmo_dataarray_column_format_t *)calloc(
        1u, sizeof(*dataarray->column_formats));
    ASSERT_NOT_NULL(dataarray->column_formats);
    dataarray->column_formats[0].type = CKARRAYTYPE_FLOAT;

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = root_id;
    request.dataarray_id = dataarray_id;
    request.col = 0u;
    request.has_data_cell = true;
    request.write_operation_id = operation_id;
    request.remove_link_id = link_id;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(
                  fixture.workspace, &request, &result));
    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_UNSAFE, result.status);
    ASSERT_STR_EQ("type_mismatch", result.rejection_code);
    ASSERT_EQ(1u, result.candidate_count);
    ASSERT_EQ(input_id, result.candidates[0].source_parameter_id);
    ASSERT_EQ(output_id, result.candidates[0].value_parameter_id);
    ASSERT_TRUE(nmo_guid_equals(
        CKPGUID_FLOAT, result.candidates[0].column_type_guid));

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, rejects_operation_write_site_outside_boundary)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 3880u;
    request.write_operation_id = 3791u;
    request.remove_link_id = 3874u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_UNSAFE, result.status);
    ASSERT_STR_EQ("unsafe_probe_insertion", result.rejection_code);
    ASSERT_STR_CONTAINS(result.message,
                        "outside the selected behavior boundary");

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, rejects_operation_write_site_unrelated_io_endpoints)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 3798u;
    request.write_operation_id = 3791u;
    request.from_io_id = 1u;
    request.to_io_id = 2u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_UNSAFE, result.status);
    ASSERT_STR_EQ("unsafe_probe_insertion", result.rejection_code);
    ASSERT_STR_CONTAINS(result.message,
                        "explicit IO endpoints do not touch selected write-operation");

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, dynamic_candidates_are_not_truncated_at_64)
{
    probe_fixture_t fixture;
    probe_fixture_init_empty(&fixture);

    nmo_object_id_t root_id = probe_create_behavior(&fixture, "root");
    nmo_behavior_state_t *root = probe_behavior_state(&fixture, root_id);
    ASSERT_NOT_NULL(root);

    for (uint32_t i = 0u; i < 70u; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "sender_%u", (unsigned)i);
        nmo_object_id_t child_id = probe_create_behavior(&fixture, name);
        nmo_behavior_state_t *child =
            probe_behavior_state(&fixture, child_id);
        ASSERT_NOT_NULL(child);
        child->flags = CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_MESSAGESENDER;
        ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root->sub_behaviors, child_id, NULL));
    }

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_MESSAGE;
    request.behavior_id = root_id;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_AUTO, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS, result.status);
    ASSERT_EQ(70u, result.candidate_count);
    ASSERT_NOT_NULL(result.candidates);
    ASSERT_EQ(NMO_PROBE_CANDIDATE_MESSAGE_SENDER,
              result.candidates[69].role);

    nmo_probe_analysis_dispose(&result);
    probe_fixture_dispose(&fixture);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(probe_analyzer, selects_unique_message_candidate_and_link);
    REGISTER_TEST(probe_analyzer, selects_explicit_message_graph_types);
    REGISTER_TEST(probe_analyzer, honors_explicit_behavior_type_precedence);
    REGISTER_TEST(probe_analyzer, resolves_explicit_operation_write_site);
    REGISTER_TEST(probe_analyzer, reports_operation_write_site_candidates);
    REGISTER_TEST(probe_analyzer, infers_auto_data_writer_cell_metadata);
    REGISTER_TEST(probe_analyzer,
                  rejects_operation_data_writer_column_type_mismatch);
    REGISTER_TEST(probe_analyzer, analyzes_explicit_operation_value_flow);
    REGISTER_TEST(probe_analyzer, rejects_operation_write_site_outside_boundary);
    REGISTER_TEST(probe_analyzer,
                  rejects_operation_write_site_unrelated_io_endpoints);
    REGISTER_TEST(probe_analyzer, dynamic_candidates_are_not_truncated_at_64);
TEST_MAIN_END()
