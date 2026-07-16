/**
 * @file test_behavior_rewrite.c
 * @brief Unit tests for behavior rewrite planning APIs
 */

#include "../test_framework.h"

#include "behavior/nmo_behavior_edit.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"

#include <stdint.h>

static bool open_test_file(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session)
{
    char errbuf[256] = {0};
    return nmo_session_open_file_with_context(path, out_ctx, out_session,
                                              errbuf, sizeof(errbuf));
}

static nmo_object_t *test_find_object(nmo_session_t *session,
                                      nmo_object_id_t id)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    return repo ? nmo_object_repository_find_by_id(repo, id) : NULL;
}

static nmo_behavior_state_t *test_behavior_state(nmo_session_t *session,
                                                 nmo_object_id_t id)
{
    nmo_object_t *object = test_find_object(session, id);
    return object ? (nmo_behavior_state_t *)nmo_object_get_state(object)
                  : NULL;
}

static nmo_behaviorlink_state_t *test_behavior_link_state(
    nmo_session_t *session,
    nmo_object_id_t id)
{
    nmo_object_t *object = test_find_object(session, id);
    return object
        ? (nmo_behaviorlink_state_t *)nmo_object_get_state(object)
        : NULL;
}

static nmo_object_id_t test_create_object(nmo_session_t *session,
                                          nmo_class_id_t class_id,
                                          const char *name)
{
    nmo_runtime_report_t report = {0};
    nmo_object_id_t id = 0;
    if (nmo_session_create_object(session, class_id, name,
                                  (nmo_guid_t){0, 0}, &id,
                                  &report) != NMO_OK ||
        id == 0) {
        return 0;
    }
    return id;
}

static void test_append_id(nmo_array_t *array, nmo_object_id_t id)
{
    nmo_status_t status = NMO_ERR_INVALID_ARGUMENT;
    if (array->element_size == sizeof(nmo_behavior_ref_t)) {
        status = nmo_behavior_ref_array_append(array, id, NULL);
    } else if (array->element_size == sizeof(nmo_ref_t)) {
        status = nmo_beobject_script_array_append(array, id);
    } else {
        status = nmo_array_append(array, &id);
    }
    ASSERT_EQ(NMO_OK, status);
}

static nmo_status_t test_behavior_fold_analyze(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report)
{
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_status_t rc = nmo_session_borrow_document(session, &document);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_workspace_create(ctx, document, &workspace);
    if (rc == NMO_OK) {
        rc = nmo_behavior_edit_fold_analyze(workspace, desc, report);
    }
    if (workspace) {
        nmo_workspace_destroy(workspace);
    }
    nmo_document_destroy(document);
    return rc;
}

static nmo_status_t test_behavior_fold_apply(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report)
{
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_status_t rc = nmo_session_borrow_document(session, &document);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_workspace_create(ctx, document, &workspace);
    if (rc == NMO_OK) {
        rc = nmo_behavior_edit_fold_apply(workspace, desc, report);
    }
    if (workspace) {
        nmo_workspace_destroy(workspace);
    }
    nmo_document_destroy(document);
    return rc;
}

static nmo_status_t test_behavior_fold(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report)
{
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_status_t rc = nmo_session_borrow_document(session, &document);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_workspace_create(ctx, document, &workspace);
    if (rc == NMO_OK) {
        rc = nmo_behavior_edit_fold(workspace, desc, report);
    }
    if (workspace) {
        nmo_workspace_destroy(workspace);
    }
    nmo_document_destroy(document);
    return rc;
}

static nmo_object_id_t test_create_behavior_link(nmo_session_t *session,
                                                 nmo_behavior_state_t *owner,
                                                 nmo_object_id_t source_io,
                                                 nmo_object_id_t target_io)
{
    nmo_object_id_t link_id = test_create_object(
        session, NMO_CID_BEHAVIORLINK, "Link");
    nmo_behaviorlink_state_t *link =
        test_behavior_link_state(session, link_id);
    if (!link) {
        return 0;
    }
    link->in_io_id = source_io;
    link->out_io_id = target_io;
    link->use_new_format = true;
    link->has_format = true;
    test_append_id(&owner->sub_behavior_links, link_id);
    return link_id;
}

static bool create_control_output_fold_fixture(
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    nmo_object_id_t *out_parent,
    nmo_object_id_t *out_anchor,
    nmo_object_id_t *out_child,
    nmo_object_id_t *out_anchor_output,
    nmo_object_id_t *out_external_link)
{
    nmo_context_t *ctx =
        nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    if (!ctx) {
        return false;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_object_id_t parent = test_create_object(
        session, NMO_CID_BEHAVIOR, "Parent");
    nmo_object_id_t owner = test_create_object(
        session, NMO_CID_BEOBJECT, "Owner");
    nmo_object_id_t anchor = test_create_object(
        session, NMO_CID_BEHAVIOR, "Anchor Graph");
    nmo_object_id_t child = test_create_object(
        session, NMO_CID_BEHAVIOR, "Internal Child");
    nmo_object_id_t external = test_create_object(
        session, NMO_CID_BEHAVIOR, "External Target");
    nmo_object_id_t anchor_in = test_create_object(
        session, NMO_CID_BEHAVIORIO, "Anchor In");
    nmo_object_id_t anchor_out = test_create_object(
        session, NMO_CID_BEHAVIORIO, "Anchor Out");
    nmo_object_id_t child_in = test_create_object(
        session, NMO_CID_BEHAVIORIO, "Child In");
    nmo_object_id_t child_out = test_create_object(
        session, NMO_CID_BEHAVIORIO, "Child Out");
    nmo_object_id_t external_in = test_create_object(
        session, NMO_CID_BEHAVIORIO, "External In");

    nmo_behavior_state_t *parent_state =
        test_behavior_state(session, parent);
    nmo_beobject_state_t *owner_state =
        (nmo_beobject_state_t *)nmo_object_get_state(
            test_find_object(session, owner));
    nmo_behavior_state_t *anchor_state =
        test_behavior_state(session, anchor);
    nmo_behavior_state_t *child_state =
        test_behavior_state(session, child);
    nmo_behavior_state_t *external_state =
        test_behavior_state(session, external);
    if (!parent_state || !owner_state || !anchor_state || !child_state ||
        !external_state) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    test_append_id(&owner_state->scripts, parent);
    test_append_id(&parent_state->sub_behaviors, anchor);
    test_append_id(&parent_state->sub_behaviors, external);
    test_append_id(&anchor_state->inputs, anchor_in);
    test_append_id(&anchor_state->outputs, anchor_out);
    test_append_id(&anchor_state->sub_behaviors, child);
    test_append_id(&child_state->inputs, child_in);
    test_append_id(&child_state->outputs, child_out);
    test_append_id(&external_state->inputs, external_in);

    (void)test_create_behavior_link(session, anchor_state, anchor_in,
                                    child_in);
    nmo_object_id_t external_link = test_create_behavior_link(
        session, parent_state, child_out, external_in);

    if (external_link == 0 ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_session = session;
    *out_parent = parent;
    *out_anchor = anchor;
    *out_child = child;
    *out_anchor_output = anchor_out;
    *out_external_link = external_link;
    return true;
}

static bool create_parameter_output_fold_fixture(
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    nmo_object_id_t *out_parent,
    nmo_object_id_t *out_anchor,
    nmo_object_id_t *out_child,
    nmo_object_id_t *out_anchor_output_parameter,
    nmo_object_id_t *out_external_input_parameter)
{
    nmo_context_t *ctx =
        nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    if (!ctx) {
        return false;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_object_id_t parent = test_create_object(
        session, NMO_CID_BEHAVIOR, "Parent");
    nmo_object_id_t owner = test_create_object(
        session, NMO_CID_BEOBJECT, "Owner");
    nmo_object_id_t anchor = test_create_object(
        session, NMO_CID_BEHAVIOR, "Anchor Graph");
    nmo_object_id_t child = test_create_object(
        session, NMO_CID_BEHAVIOR, "Internal Child");
    nmo_object_id_t external = test_create_object(
        session, NMO_CID_BEHAVIOR, "External Target");
    nmo_object_id_t anchor_out_param = test_create_object(
        session, NMO_CID_PARAMETEROUT, "Anchor Out Param");
    nmo_object_id_t child_out_param = test_create_object(
        session, NMO_CID_PARAMETEROUT, "Child Out Param");
    nmo_object_id_t external_in_param = test_create_object(
        session, NMO_CID_PARAMETERIN, "External In Param");

    nmo_behavior_state_t *parent_state =
        test_behavior_state(session, parent);
    nmo_beobject_state_t *owner_state =
        (nmo_beobject_state_t *)nmo_object_get_state(
            test_find_object(session, owner));
    nmo_behavior_state_t *anchor_state =
        test_behavior_state(session, anchor);
    nmo_behavior_state_t *child_state =
        test_behavior_state(session, child);
    nmo_behavior_state_t *external_state =
        test_behavior_state(session, external);
    nmo_parameterout_state_t *anchor_out =
        (nmo_parameterout_state_t *)nmo_object_get_state(
            test_find_object(session, anchor_out_param));
    nmo_parameterout_state_t *child_out =
        (nmo_parameterout_state_t *)nmo_object_get_state(
            test_find_object(session, child_out_param));
    nmo_parameterin_state_t *external_in =
        (nmo_parameterin_state_t *)nmo_object_get_state(
            test_find_object(session, external_in_param));
    if (!parent_state || !owner_state || !anchor_state || !child_state ||
        !external_state || !anchor_out || !child_out || !external_in) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    const nmo_guid_t type_guid = {0x5A5716FDu, 0x44E276D7u};
    anchor_out->base.type_guid = type_guid;
    child_out->base.type_guid = type_guid;
    external_in->type_guid = type_guid;
    external_in->source_id = child_out_param;
    external_in->owner_id = external;

    test_append_id(&owner_state->scripts, parent);
    test_append_id(&parent_state->sub_behaviors, anchor);
    test_append_id(&parent_state->sub_behaviors, external);
    test_append_id(&anchor_state->sub_behaviors, child);
    test_append_id(&anchor_state->out_parameters, anchor_out_param);
    test_append_id(&child_state->out_parameters, child_out_param);
    test_append_id(&external_state->in_parameters, external_in_param);

    if (nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_session = session;
    *out_parent = parent;
    *out_anchor = anchor;
    *out_child = child;
    *out_anchor_output_parameter = anchor_out_param;
    *out_external_input_parameter = external_in_param;
    return true;
}

static bool create_parameter_input_fold_fixture(
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    nmo_object_id_t *out_parent,
    nmo_object_id_t *out_anchor,
    nmo_object_id_t *out_child,
    nmo_object_id_t *out_anchor_input_parameter,
    nmo_object_id_t *out_external_output_parameter)
{
    nmo_context_t *ctx =
        nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    if (!ctx) {
        return false;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_object_id_t parent = test_create_object(
        session, NMO_CID_BEHAVIOR, "Parent");
    nmo_object_id_t owner = test_create_object(
        session, NMO_CID_BEOBJECT, "Owner");
    nmo_object_id_t anchor = test_create_object(
        session, NMO_CID_BEHAVIOR, "Anchor Graph");
    nmo_object_id_t child = test_create_object(
        session, NMO_CID_BEHAVIOR, "Internal Child");
    nmo_object_id_t external = test_create_object(
        session, NMO_CID_BEHAVIOR, "External Source");
    nmo_object_id_t anchor_in_param = test_create_object(
        session, NMO_CID_PARAMETERIN, "Anchor In Param");
    nmo_object_id_t child_in_param = test_create_object(
        session, NMO_CID_PARAMETERIN, "Child In Param");
    nmo_object_id_t external_out_param = test_create_object(
        session, NMO_CID_PARAMETEROUT, "External Out Param");

    nmo_behavior_state_t *parent_state =
        test_behavior_state(session, parent);
    nmo_beobject_state_t *owner_state =
        (nmo_beobject_state_t *)nmo_object_get_state(
            test_find_object(session, owner));
    nmo_behavior_state_t *anchor_state =
        test_behavior_state(session, anchor);
    nmo_behavior_state_t *child_state =
        test_behavior_state(session, child);
    nmo_behavior_state_t *external_state =
        test_behavior_state(session, external);
    nmo_parameterin_state_t *anchor_in =
        (nmo_parameterin_state_t *)nmo_object_get_state(
            test_find_object(session, anchor_in_param));
    nmo_parameterin_state_t *child_in =
        (nmo_parameterin_state_t *)nmo_object_get_state(
            test_find_object(session, child_in_param));
    nmo_parameterout_state_t *external_out =
        (nmo_parameterout_state_t *)nmo_object_get_state(
            test_find_object(session, external_out_param));
    if (!parent_state || !owner_state || !anchor_state || !child_state ||
        !external_state || !anchor_in || !child_in || !external_out) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    const nmo_guid_t type_guid = {0x5A5716FDu, 0x44E276D7u};
    anchor_in->type_guid = type_guid;
    child_in->type_guid = type_guid;
    external_out->base.type_guid = type_guid;
    child_in->source_id = external_out_param;
    child_in->owner_id = child;

    test_append_id(&owner_state->scripts, parent);
    test_append_id(&parent_state->sub_behaviors, anchor);
    test_append_id(&parent_state->sub_behaviors, external);
    test_append_id(&anchor_state->sub_behaviors, child);
    test_append_id(&anchor_state->in_parameters, anchor_in_param);
    test_append_id(&child_state->in_parameters, child_in_param);
    test_append_id(&external_state->out_parameters, external_out_param);

    if (nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_session = session;
    *out_parent = parent;
    *out_anchor = anchor;
    *out_child = child;
    *out_anchor_input_parameter = anchor_in_param;
    *out_external_output_parameter = external_out_param;
    return true;
}

TEST(beh_rewrite, fold_analyze_reports_selected_boundary_plan)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 2u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_TRUE(report.analysis_only);
    ASSERT_FALSE(report.can_write);
    ASSERT_EQ(1u, report.write_blocker_count);
    ASSERT_NOT_NULL(report.write_blockers);
    ASSERT_STR_EQ("analysis_only", report.write_blockers[0].code);
    ASSERT_NOT_NULL(report.write_blockers[0].message);
    ASSERT_EQ(4692u, report.parent_id);
    ASSERT_EQ(2364u, report.representative_id);
    ASSERT_EQ(2u, report.selected_node_count);
    ASSERT_EQ(2u, report.boundary.internal_node_count);
    ASSERT_EQ(1u, report.control_links_to_delete_count);
    ASSERT_EQ(2357u, report.control_links_to_delete[0].link_id);
    ASSERT_EQ(1u, report.nodes_to_delete_count);
    ASSERT_EQ(2208u, report.nodes_to_delete[0]);
    ASSERT_TRUE(report.boundary.control_in_count > 0);
    ASSERT_TRUE(report.boundary.control_out_count > 0);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_apply_retargets_control_out_to_anchor_output)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_id_t parent = 0;
    nmo_object_id_t anchor = 0;
    nmo_object_id_t child = 0;
    nmo_object_id_t anchor_output = 0;
    nmo_object_id_t external_link = 0;
    ASSERT_TRUE(create_control_output_fold_fixture(
        &ctx, &session, &parent, &anchor, &child, &anchor_output,
        &external_link));

    nmo_object_id_t nodes[] = {anchor, child};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = parent,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = anchor,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Folded Control Out",
        .block_version = 65536u,
        .preserve_boundary = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_apply(ctx, session, &desc,
                                              &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(1u, report.boundary.control_out_count);

    nmo_behaviorlink_state_t *link =
        test_behavior_link_state(session, external_link);
    ASSERT_NOT_NULL(link);
    ASSERT_EQ(anchor_output, link->in_io_id);
    ASSERT_NULL(test_find_object(session, child));

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_apply_retargets_parameter_out_to_anchor_output_parameter)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_id_t parent = 0;
    nmo_object_id_t anchor = 0;
    nmo_object_id_t child = 0;
    nmo_object_id_t anchor_output_parameter = 0;
    nmo_object_id_t external_input_parameter = 0;
    ASSERT_TRUE(create_parameter_output_fold_fixture(
        &ctx, &session, &parent, &anchor, &child,
        &anchor_output_parameter, &external_input_parameter));

    nmo_object_id_t nodes[] = {anchor, child};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = parent,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = anchor,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Folded Parameter Out",
        .block_version = 65536u,
        .preserve_boundary = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_apply(ctx, session, &desc,
                                              &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(1u, report.boundary.parameter_out_count);

    nmo_parameterin_state_t *external_in =
        (nmo_parameterin_state_t *)nmo_object_get_state(
            test_find_object(session, external_input_parameter));
    ASSERT_NOT_NULL(external_in);
    ASSERT_EQ(anchor_output_parameter, external_in->source_id);
    ASSERT_NULL(test_find_object(session, child));

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_apply_retargets_parameter_in_to_anchor_input_parameter)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_id_t parent = 0;
    nmo_object_id_t anchor = 0;
    nmo_object_id_t child = 0;
    nmo_object_id_t anchor_input_parameter = 0;
    nmo_object_id_t external_output_parameter = 0;
    ASSERT_TRUE(create_parameter_input_fold_fixture(
        &ctx, &session, &parent, &anchor, &child,
        &anchor_input_parameter, &external_output_parameter));

    nmo_object_id_t nodes[] = {anchor, child};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = parent,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = anchor,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Folded Parameter In",
        .block_version = 65536u,
        .preserve_boundary = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_apply(ctx, session, &desc,
                                              &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(1u, report.boundary.parameter_in_count);

    nmo_parameterin_state_t *anchor_in =
        (nmo_parameterin_state_t *)nmo_object_get_state(
            test_find_object(session, anchor_input_parameter));
    ASSERT_NOT_NULL(anchor_in);
    ASSERT_EQ(external_output_parameter, anchor_in->source_id);
    ASSERT_NULL(test_find_object(session, child));

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_blocks_write_for_missing_control_output_target)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_id_t parent = 0;
    nmo_object_id_t anchor = 0;
    nmo_object_id_t child = 0;
    nmo_object_id_t anchor_output = 0;
    nmo_object_id_t external_link = 0;
    ASSERT_TRUE(create_control_output_fold_fixture(
        &ctx, &session, &parent, &anchor, &child, &anchor_output,
        &external_link));

    nmo_object_id_t nodes[] = {anchor, child};
    nmo_behavior_fold_map_t output_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 0u,
            .new_index = 99u,
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = parent,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = anchor,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Folded Missing Control Out",
        .block_version = 65536u,
        .preserve_boundary = true,
        .output_maps = output_maps,
        .output_map_count = 1u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_FALSE(report.rejected);
    ASSERT_FALSE(report.can_write);
    ASSERT_TRUE(report.analysis_only);
    ASSERT_TRUE(report.write_blocker_count > 0u);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_write_rejects_until_supported)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2367u, 2370u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_map_t output_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Two Leaf Fold",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 2u,
        .output_maps = output_maps,
        .output_map_count = 2u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold(ctx, session, &desc, &report);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, rc);
    ASSERT_FALSE(report.can_write);
    ASSERT_EQ(1u, report.write_blocker_count);
    ASSERT_STR_EQ("analysis_only", report.write_blockers[0].code);
    ASSERT_STR_EQ("analysis_only", report.diagnostic_code);
    ASSERT_NOT_NULL(report.diagnostic_message);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_apply_rejects_until_supported)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2367u, 2370u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_map_t output_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Two Leaf Fold",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 2u,
        .output_maps = output_maps,
        .output_map_count = 2u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_apply(ctx, session, &desc,
                                              &report);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, rc);
    ASSERT_FALSE(report.can_write);
    ASSERT_EQ(1u, report.write_blocker_count);
    ASSERT_STR_EQ("analysis_only", report.write_blockers[0].code);
    ASSERT_STR_EQ("analysis_only", report.diagnostic_code);
    ASSERT_NOT_NULL(report.diagnostic_message);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_apply_requires_preserve_boundary)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_apply(ctx, session, &desc,
                                              &report);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, rc);
    ASSERT_TRUE(report.rejected);
    ASSERT_STR_EQ("preserve_boundary_required", report.diagnostic_code);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_uses_explicit_anchor)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2208u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(2208u, report.anchor_id);
    ASSERT_EQ(2208u, report.representative_id);
    ASSERT_EQ(1u, report.nodes_to_delete_count);
    ASSERT_EQ(2364u, report.nodes_to_delete[0]);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_preserve_boundary_enables_edges)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 2u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_TRUE(report.preserve_boundary);
    ASSERT_TRUE(report.preserve_links);
    ASSERT_TRUE(report.preserve_params);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_rejects_ambiguous_input_without_map)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, rc);
    ASSERT_TRUE(report.rejected);
    ASSERT_STR_EQ("input_map_required", report.diagnostic_code);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_reports_maps)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 3u,
            .label = "In",
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 7u,
            .label = "Next",
        },
    };
    nmo_behavior_fold_map_t output_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 0u,
            .new_index = 1u,
            .label = "Out",
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 2u,
        .output_maps = output_maps,
        .output_map_count = 1u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(2u, report.input_map_count);
    ASSERT_EQ(NMO_BEHAVIOR_FOLD_MAP_INPUT, report.input_maps[0].kind);
    ASSERT_EQ(0u, report.input_maps[0].old_index);
    ASSERT_EQ(3u, report.input_maps[0].new_index);
    ASSERT_STR_EQ("In", report.input_maps[0].label);
    ASSERT_EQ(1u, report.output_map_count);
    ASSERT_EQ(NMO_BEHAVIOR_FOLD_MAP_OUTPUT, report.output_maps[0].kind);
    ASSERT_EQ(0u, report.output_maps[0].old_index);
    ASSERT_EQ(1u, report.output_maps[0].new_index);
    ASSERT_STR_EQ("Out", report.output_maps[0].label);
    ASSERT_EQ(0u, report.parameter_map_count);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_rejects_duplicate_input_map_target)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 0u,
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 2u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, rc);
    ASSERT_TRUE(report.rejected);
    ASSERT_STR_EQ("input_map_invalid", report.diagnostic_code);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_reports_interface_mode)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 2u,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE,
              report.interface_mode);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_rejects_anchor_outside_selection)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2178u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, rc);
    ASSERT_TRUE(report.rejected);
    ASSERT_STR_EQ("anchor_not_selected", report.diagnostic_code);
    ASSERT_EQ(2178u, report.anchor_id);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_rejects_parent_in_selected_nodes)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {4692u};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 1u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Invalid Self Fold",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = test_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, rc);
    ASSERT_TRUE(report.rejected);
    ASSERT_STR_EQ("parent_selected", report.diagnostic_code);
    ASSERT_EQ(4692u, report.parent_id);

    nmo_behavior_edit_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(beh_rewrite, fold_apply_retargets_parameter_in_to_anchor_input_parameter);
    REGISTER_TEST(beh_rewrite, fold_apply_retargets_parameter_out_to_anchor_output_parameter);
    REGISTER_TEST(beh_rewrite, fold_apply_retargets_control_out_to_anchor_output);
    REGISTER_TEST(beh_rewrite, fold_analyze_blocks_write_for_missing_control_output_target);
    REGISTER_TEST(beh_rewrite, fold_analyze_reports_selected_boundary_plan);
    REGISTER_TEST(beh_rewrite, fold_write_rejects_until_supported);
    REGISTER_TEST(beh_rewrite, fold_apply_rejects_until_supported);
    REGISTER_TEST(beh_rewrite, fold_apply_requires_preserve_boundary);
    REGISTER_TEST(beh_rewrite, fold_analyze_uses_explicit_anchor);
    REGISTER_TEST(beh_rewrite, fold_analyze_preserve_boundary_enables_edges);
    REGISTER_TEST(beh_rewrite, fold_analyze_rejects_ambiguous_input_without_map);
    REGISTER_TEST(beh_rewrite, fold_analyze_reports_maps);
    REGISTER_TEST(beh_rewrite, fold_analyze_rejects_duplicate_input_map_target);
    REGISTER_TEST(beh_rewrite, fold_analyze_reports_interface_mode);
    REGISTER_TEST(beh_rewrite, fold_analyze_rejects_anchor_outside_selection);
    REGISTER_TEST(beh_rewrite, fold_analyze_rejects_parent_in_selected_nodes);
TEST_MAIN_END()

