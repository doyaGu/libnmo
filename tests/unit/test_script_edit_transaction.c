#include "test_framework.h"

#include "document/nmo_document.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_behavior_analyze.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "../../src/runtime/runtime_internal.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_statesave_ids.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"

#include <stdio.h>

static void create_object_or_fail(nmo_session_t *session,
                                  nmo_class_id_t class_id,
                                  const char *name,
                                  nmo_object_id_t *out_id)
{
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, class_id, name, (nmo_guid_t){0, 0}, out_id, NULL));
    ASSERT_TRUE(*out_id != 0);
}

typedef struct test_workspace_seed_scope {
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_workspace_edit_t *edit;
} test_workspace_seed_scope_t;

static void destroy_test_workspace_seed_scope(
    test_workspace_seed_scope_t *scope)
{
    if (!scope) {
        return;
    }
    if (scope->workspace) {
        nmo_workspace_destroy(scope->workspace);
    }
    if (scope->document) {
        nmo_document_destroy(scope->document);
    }
    memset(scope, 0, sizeof(*scope));
}

static nmo_status_t begin_test_workspace_seed_edit(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const char *label,
    test_workspace_seed_scope_t *scope)
{
    nmo_status_t rc = NMO_OK;

    if (!ctx || !session || !label || !scope) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(scope, 0, sizeof(*scope));
    rc = nmo_session_borrow_document(session, &scope->document);
    if (rc != NMO_OK) {
        destroy_test_workspace_seed_scope(scope);
        return rc;
    }
    rc = nmo_workspace_create(ctx, scope->document, &scope->workspace);
    if (rc != NMO_OK) {
        destroy_test_workspace_seed_scope(scope);
        return rc;
    }
    rc = nmo_workspace_edit_begin(scope->workspace, label, &scope->edit);
    if (rc != NMO_OK) {
        destroy_test_workspace_seed_scope(scope);
        return rc;
    }
    return NMO_OK;
}

static nmo_status_t begin_test_script_edit(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const char *label,
    nmo_script_edit_tx_t **out_tx)
{
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_status_t rc = NMO_OK;

    if (!ctx || !session || !label || !out_tx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_tx = NULL;
    rc = nmo_session_borrow_document(session, &document);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_workspace_create(ctx, document, &workspace);
    if (rc != NMO_OK) {
        nmo_document_destroy(document);
        return rc;
    }
    rc = nmo_script_edit_begin(workspace, label, out_tx);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    return rc;
}

static void assert_behavior_owner_checks_green(nmo_object_repository_t *repo,
                                               const nmo_behavior_index_t *index)
{
    size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (!object) {
            continue;
        }

        if (nmo_object_get_class_id(object) == NMO_CID_BEHAVIORLINK) {
            const nmo_behaviorlink_state_t *state =
                (const nmo_behaviorlink_state_t *)nmo_object_get_state(object);
            ASSERT_NOT_NULL(state);
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, nmo_object_get_id(object)));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, state->in_io_id));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, state->out_io_id));
        }

        if (nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            nmo_object_t *source = NULL;
            ASSERT_NOT_NULL(state);
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            if (state->source_id != 0) {
                source = nmo_object_repository_find_by_id(repo, state->source_id);
                ASSERT_NOT_NULL(source);
                if (state->is_shared) {
                    ASSERT_EQ(NMO_CID_PARAMETERIN, nmo_object_get_class_id(source));
                } else {
                    nmo_class_id_t source_class = nmo_object_get_class_id(source);
                    ASSERT_TRUE(source_class == NMO_CID_PARAMETER ||
                                source_class == NMO_CID_PARAMETERIN ||
                                source_class == NMO_CID_PARAMETEROUT ||
                                source_class == NMO_CID_PARAMETERLOCAL ||
                                source_class == NMO_CID_PARAMETEROPERATION);
                }
            }
        }

        if (nmo_object_get_class_id(object) == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *state =
                (const nmo_parameterout_state_t *)nmo_object_get_state(object);
            ASSERT_NOT_NULL(state);
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            for (uint32_t j = 0; j < state->destination_count; ++j) {
                nmo_object_id_t destination_id =
                    state->destination_ids ? state->destination_ids[j] : 0;
                nmo_object_t *destination = NULL;
                if (destination_id == 0) {
                    continue;
                }
                destination = nmo_object_repository_find_by_id(repo, destination_id);
                ASSERT_NOT_NULL(destination);
                {
                    nmo_class_id_t destination_class =
                        nmo_object_get_class_id(destination);
                    ASSERT_TRUE(destination_class == NMO_CID_PARAMETER ||
                                destination_class == NMO_CID_PARAMETERIN ||
                                destination_class == NMO_CID_PARAMETEROUT ||
                                destination_class == NMO_CID_PARAMETERLOCAL ||
                                destination_class == NMO_CID_PARAMETEROPERATION);
                }
            }
        }
    }
}

typedef struct script_control_fixture {
    nmo_object_id_t root_behavior_id;
    nmo_object_id_t source_behavior_id;
    nmo_object_id_t target_behavior_id;
    nmo_object_id_t root_input_id;
    nmo_object_id_t root_output_id;
    nmo_object_id_t source_output_id;
    nmo_object_id_t target_input_id;
} script_control_fixture_t;

static void set_io_direction_or_fail(nmo_session_t *session,
                                     nmo_object_id_t io_id,
                                     uint32_t flags)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *io_obj = repo ? nmo_object_repository_find_by_id(repo, io_id) : NULL;
    nmo_behaviorio_state_t *io_state = io_obj
        ? (nmo_behaviorio_state_t *)nmo_object_get_state(io_obj)
        : NULL;

    ASSERT_NOT_NULL(io_state);
    io_state->old_flags = flags | CK_BEHAVIORIO_ACTIVE;
    io_state->has_flags = true;
}

static void setup_script_control_fixture(nmo_session_t *session,
                                         script_control_fixture_t *fixture)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *owner_obj = NULL;
    nmo_object_t *root_obj = NULL;
    nmo_object_t *source_obj = NULL;
    nmo_object_t *target_obj = NULL;
    nmo_beobject_state_t *owner_state = NULL;
    nmo_behavior_state_t *root_state = NULL;
    nmo_behavior_state_t *source_state = NULL;
    nmo_behavior_state_t *target_state = NULL;

    ASSERT_NOT_NULL(session);
    ASSERT_NOT_NULL(fixture);
    memset(fixture, 0, sizeof(*fixture));

    nmo_object_id_t owner_id = 0;

    create_object_or_fail(session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "Graph", &fixture->root_behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "Source", &fixture->source_behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "Target", &fixture->target_behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "Graph In", &fixture->root_input_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "Graph Out", &fixture->root_output_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "Source Out", &fixture->source_output_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "Target In", &fixture->target_input_id);

    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    owner_obj = nmo_object_repository_find_by_id(repo, owner_id);
    root_obj = nmo_object_repository_find_by_id(repo, fixture->root_behavior_id);
    source_obj = nmo_object_repository_find_by_id(repo, fixture->source_behavior_id);
    target_obj = nmo_object_repository_find_by_id(repo, fixture->target_behavior_id);
    ASSERT_NOT_NULL(owner_obj);
    ASSERT_NOT_NULL(root_obj);
    ASSERT_NOT_NULL(source_obj);
    ASSERT_NOT_NULL(target_obj);

    owner_state = (nmo_beobject_state_t *)nmo_object_get_state(owner_obj);
    root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_obj);
    source_state = (nmo_behavior_state_t *)nmo_object_get_state(source_obj);
    target_state = (nmo_behavior_state_t *)nmo_object_get_state(target_obj);
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(source_state);
    ASSERT_NOT_NULL(target_state);

    ASSERT_EQ(NMO_OK,
              nmo_array_append(&owner_state->script_ids,
                               &fixture->root_behavior_id));
    ASSERT_EQ(NMO_OK,
              nmo_array_append(&root_state->sub_behaviors,
                               &fixture->source_behavior_id));
    ASSERT_EQ(NMO_OK,
              nmo_array_append(&root_state->sub_behaviors,
                               &fixture->target_behavior_id));
    ASSERT_EQ(NMO_OK,
              nmo_array_append(&root_state->inputs, &fixture->root_input_id));
    ASSERT_EQ(NMO_OK,
              nmo_array_append(&root_state->outputs, &fixture->root_output_id));
    ASSERT_EQ(NMO_OK,
              nmo_array_append(&source_state->outputs, &fixture->source_output_id));
    ASSERT_EQ(NMO_OK,
              nmo_array_append(&target_state->inputs, &fixture->target_input_id));

    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    root_state->has_save_flags = true;
    root_state->save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV |
                              CK_STATESAVE_BEHAVIORINPUTS |
                              CK_STATESAVE_BEHAVIOROUTPUTS;

    source_state->owner_id = fixture->root_behavior_id;
    source_state->has_save_flags = true;
    source_state->save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;

    target_state->owner_id = fixture->root_behavior_id;
    target_state->has_save_flags = true;
    target_state->save_flags |= CK_STATESAVE_BEHAVIORINPUTS;

    set_io_direction_or_fail(session, fixture->root_input_id, CK_BEHAVIORIO_IN);
    set_io_direction_or_fail(session, fixture->root_output_id, CK_BEHAVIORIO_OUT);
    set_io_direction_or_fail(session, fixture->source_output_id, CK_BEHAVIORIO_OUT);
    set_io_direction_or_fail(session, fixture->target_input_id, CK_BEHAVIORIO_IN);
}

TEST(script_edit_transaction, rollback_restores_original_state_after_validation_failure)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_t *child_obj = NULL;
    nmo_3dentity_state_t *child_state = NULL;
    nmo_object_id_t parent_id = 0;
    nmo_object_id_t child_id = 0;
    char parent_text[32];

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    create_object_or_fail(session, NMO_CID_3DENTITY, "parent", &parent_id);
    create_object_or_fail(session, NMO_CID_3DENTITY, "child", &child_id);

    child_obj = nmo_object_repository_find_by_id(repo, child_id);
    ASSERT_NOT_NULL(child_obj);
    child_state = (nmo_3dentity_state_t *)nmo_object_get_state(child_obj);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(0u, child_state->parent_id);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "rollback-test", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_NOT_NULL(nmo_script_edit_workspace_edit(tx));

    snprintf(parent_text, sizeof(parent_text), "%u", parent_id);
    {
        nmo_session_field_edit_t field = {"parent_id", parent_text};
        ASSERT_EQ(NMO_OK,
                  nmo_object_edit_set_fields(
                      nmo_script_edit_workspace_edit(tx), child_id, &field, 1, NULL));
    }
    ASSERT_EQ(parent_id, child_state->parent_id);

    ASSERT_EQ(NMO_OK,
              nmo_workspace_edit_snapshot_bytes(
                  nmo_script_edit_workspace_edit(tx),
                  &child_state->parent_id,
                  sizeof(child_state->parent_id)));
    child_state->parent_id = 999999u;
    nmo_script_edit_mark(
        tx, NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);

    ASSERT_NE(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(999999u, child_state->parent_id);

    nmo_script_edit_rollback(tx);
    ASSERT_EQ(0u, child_state->parent_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction, behavior_edit_add_link_through_workspace_owner)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_session_t *session = NULL;
    nmo_workspace_edit_t *edit = NULL;
    script_control_fixture_t fixture;
    nmo_object_id_t link_id = 0;

    ASSERT_NOT_NULL(ctx);
    document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_NOT_NULL(workspace);
    session = nmo_workspace_internal_session(workspace);
    ASSERT_NOT_NULL(session);

    setup_script_control_fixture(session, &fixture);

    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "seed-link", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(edit,
                                         fixture.root_behavior_id,
                                         fixture.source_output_id,
                                         fixture.target_input_id,
                                         1,
                                         &link_id));
    ASSERT_TRUE(link_id != 0u);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction, add_node_keeps_ballance_script_edit_validation_green)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *node_obj = NULL;
    nmo_behavior_state_t *node_state = NULL;
    const nmo_behavior_index_t *index = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0;
    nmo_object_id_t node_id = 0;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "add-node-test", &tx));
    ASSERT_NOT_NULL(tx);

    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_node(
                  tx,
                  237u,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Test 2D Text",
                  &node_id));
    ASSERT_TRUE(node_id != 0u);
    node_obj = nmo_object_repository_find_by_id(repo, node_id);
    ASSERT_NOT_NULL(node_obj);
    node_state = (nmo_behavior_state_t *)nmo_object_get_state(node_obj);
    ASSERT_NOT_NULL(node_state);
    ASSERT_EQ(NMO_CID_2DENTITY, node_state->compatible_class_id);
    ASSERT_TRUE(node_state->target_parameter_id != 0u);

    {
        nmo_object_t *target_param_obj =
            nmo_object_repository_find_by_id(repo, node_state->target_parameter_id);
        nmo_parameterin_state_t *target_param_state = target_param_obj
            ? (nmo_parameterin_state_t *)nmo_object_get_state(target_param_obj)
            : NULL;
        ASSERT_NOT_NULL(target_param_obj);
        ASSERT_EQ(NMO_CID_PARAMETERIN, nmo_object_get_class_id(target_param_obj));
        ASSERT_NOT_NULL(target_param_state);
        ASSERT_TRUE(nmo_guid_equals(CKPGUID_2DENTITY, target_param_state->type_guid));
    }

    {
        bool found_text_properties = false;
        const nmo_object_id_t *ids =
            (const nmo_object_id_t *)node_state->local_parameters.data;
        for (size_t i = 0; ids && i < node_state->local_parameters.count; ++i) {
            nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, ids[i]);
            nmo_parameterlocal_state_t *param_state = param_obj
                ? (nmo_parameterlocal_state_t *)nmo_object_get_state(param_obj)
                : NULL;
            const char *param_name = param_obj ? nmo_object_get_name(param_obj) : NULL;
            if (param_state && param_name && strcmp(param_name, "Text Properties") == 0) {
                ASSERT_EQ(1u, param_state->is_setting);
                found_text_properties = true;
            }
        }
        ASSERT_TRUE(found_text_properties);
    }

    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY));
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_EQ(NMO_OK,
              nmo_workspace_apply_edit_flags(
                  workspace,
                  NMO_WORKSPACE_EDIT_OBJECT_STATE |
                      NMO_WORKSPACE_EDIT_REFERENCES |
                      NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                      NMO_WORKSPACE_EDIT_NAMES |
                      NMO_WORKSPACE_EDIT_RESOURCES));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    index = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(index);

    {
        const nmo_object_id_t *ids = (const nmo_object_id_t *)node_state->inputs.data;
        for (size_t i = 0; ids && i < node_state->inputs.count; ++i) {
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, ids[i]));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, ids[i]));
        }
    }
    {
        const nmo_object_id_t *ids = (const nmo_object_id_t *)node_state->outputs.data;
        for (size_t i = 0; ids && i < node_state->outputs.count; ++i) {
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, ids[i]));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, ids[i]));
        }
    }
    {
        const nmo_object_id_t *ids =
            (const nmo_object_id_t *)node_state->in_parameters.data;
        for (size_t i = 0; ids && i < node_state->in_parameters.count; ++i) {
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, ids[i]));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, ids[i]));
        }
    }
    {
        const nmo_object_id_t *ids =
            (const nmo_object_id_t *)node_state->out_parameters.data;
        for (size_t i = 0; ids && i < node_state->out_parameters.count; ++i) {
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, ids[i]));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, ids[i]));
        }
    }
    {
        const nmo_object_id_t *ids =
            (const nmo_object_id_t *)node_state->local_parameters.data;
        for (size_t i = 0; ids && i < node_state->local_parameters.count; ++i) {
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, ids[i]));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, ids[i]));
        }
    }
    assert_behavior_owner_checks_green(repo, index);
    ref_graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(ref_graph);
    if (nmo_ref_graph_validate(ref_graph, &broken_edges, &broken_count) != NMO_OK) {
        for (size_t i = 0; i < broken_count; ++i) {
            fprintf(stderr, "broken edge: %u -> %u via %s\n",
                    broken_edges[i].from,
                    broken_edges[i].to,
                    broken_edges[i].field_path ? broken_edges[i].field_path : "(null)");
        }
    }
    ASSERT_EQ(NMO_OK, nmo_ref_graph_validate(ref_graph, &broken_edges, &broken_count));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_INTERFACE));

    nmo_script_edit_rollback(tx);
    nmo_workspace_destroy(workspace);
    nmo_session_close_with_context(ctx, session);
}

TEST(script_edit_transaction, add_node_rejects_unknown_building_block)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    script_control_fixture_t fixture;
    nmo_object_id_t node_id = 0;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    setup_script_control_fixture(session, &fixture);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "unknown-bb", &tx));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_script_edit_add_node(
                  tx,
                  fixture.root_behavior_id,
                  nmo_guid_parse("11111111-22222222"),
                  "Unknown BB",
                  &node_id));
    ASSERT_EQ(0u, node_id);

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction,
     remove_link_then_add_link_keeps_validation_green_within_transaction)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    script_control_fixture_t fixture;
    test_workspace_seed_scope_t seed_scope = {0};
    nmo_object_id_t original_link_id = 0;
    nmo_object_id_t new_link_id = 0;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    setup_script_control_fixture(session, &fixture);

    ASSERT_EQ(NMO_OK,
              begin_test_workspace_seed_edit(
                  ctx, session, "seed-link", &seed_scope));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(seed_scope.edit,
                                         fixture.root_behavior_id,
                                         fixture.source_output_id,
                                         fixture.target_input_id,
                                         1,
                                         &original_link_id));
    ASSERT_TRUE(original_link_id != 0u);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(seed_scope.edit));
    destroy_test_workspace_seed_scope(&seed_scope);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "remove-add-link", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_remove_behavior_link(tx,
                                                   fixture.root_behavior_id,
                                                   original_link_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_behavior_link(tx,
                                                fixture.root_behavior_id,
                                                fixture.root_input_id,
                                                fixture.target_input_id,
                                                0u,
                                                &new_link_id));
    ASSERT_TRUE(new_link_id != 0u);
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction,
     add_behavior_link_rejects_reversed_child_endpoint_directions)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    script_control_fixture_t fixture;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    setup_script_control_fixture(session, &fixture);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "reject-reversed-link", &tx));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_add_behavior_link(tx,
                                                fixture.root_behavior_id,
                                                fixture.target_input_id,
                                                fixture.source_output_id,
                                                1u,
                                                NULL));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction,
     rewire_behavior_link_rejects_reversed_child_endpoint_directions)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    script_control_fixture_t fixture;
    test_workspace_seed_scope_t seed_scope = {0};
    nmo_object_id_t link_id = 0;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    setup_script_control_fixture(session, &fixture);

    ASSERT_EQ(NMO_OK,
              begin_test_workspace_seed_edit(
                  ctx, session, "seed-link", &seed_scope));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(seed_scope.edit,
                                         fixture.root_behavior_id,
                                         fixture.source_output_id,
                                         fixture.target_input_id,
                                         1,
                                         &link_id));
    ASSERT_TRUE(link_id != 0u);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(seed_scope.edit));
    destroy_test_workspace_seed_scope(&seed_scope);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "reject-reversed-rewire", &tx));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_rewire_behavior_link(tx,
                                                   link_id,
                                                   fixture.target_input_id,
                                                   fixture.source_output_id));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction,
     reference_validation_allows_preexisting_broken_refs_for_value_only_parameter_edit)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0u;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    ASSERT_NOT_NULL(session);

    ref_graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(ref_graph);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_ref_graph_validate(ref_graph, &broken_edges, &broken_count));
    ASSERT_EQ(2u, broken_count);

    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "param-edit", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(tx, 46u, "520"));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));

    nmo_script_edit_rollback(tx);
    nmo_session_close_with_context(ctx, session);
}

TEST(script_edit_transaction,
     reference_validation_rejects_new_broken_ref_beyond_preexisting_baseline)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    ASSERT_NOT_NULL(session);

    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "param-edit-invalid", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(tx, 46u, "999999"));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));

    nmo_script_edit_rollback(tx);
    nmo_session_close_with_context(ctx, session);
}

TEST(script_edit_transaction,
     interface_validation_allows_preexisting_diagnostics_for_value_only_parameter_edit)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_session_behavior_interface_diagnostics_t diag = {0};

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    ASSERT_NOT_NULL(session);

    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    nmo_session_get_behavior_interface_diagnostics(session, &diag);
    ASSERT_TRUE(diag.attempted);
    ASSERT_NE(NMO_OK, diag.status);

    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "param-edit-iface", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(tx, 46u, "520"));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_INTERFACE));

    nmo_script_edit_rollback(tx);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_edit_transaction,
                  behavior_edit_add_link_through_workspace_owner);
    REGISTER_TEST(script_edit_transaction,
                  rollback_restores_original_state_after_validation_failure);
    REGISTER_TEST(script_edit_transaction,
                  add_node_keeps_ballance_script_edit_validation_green);
    REGISTER_TEST(script_edit_transaction,
                  add_node_rejects_unknown_building_block);
    REGISTER_TEST(script_edit_transaction,
                  remove_link_then_add_link_keeps_validation_green_within_transaction);
    REGISTER_TEST(script_edit_transaction,
                  add_behavior_link_rejects_reversed_child_endpoint_directions);
    REGISTER_TEST(script_edit_transaction,
                  rewire_behavior_link_rejects_reversed_child_endpoint_directions);
    REGISTER_TEST(script_edit_transaction,
                  reference_validation_allows_preexisting_broken_refs_for_value_only_parameter_edit);
    REGISTER_TEST(script_edit_transaction,
                  reference_validation_rejects_new_broken_ref_beyond_preexisting_baseline);
    REGISTER_TEST(script_edit_transaction,
                  interface_validation_allows_preexisting_diagnostics_for_value_only_parameter_edit);
TEST_MAIN_END()







