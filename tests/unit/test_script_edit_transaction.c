#include "test_framework.h"

#include "document/nmo_document.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_registry.h"
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
#include "object/nmo_manager_guids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_statesave_ids.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "format/nmo_object.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
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
            ASSERT_NOT_NULL(nmo_behavior_index_find(
                index, nmo_behaviorlink_in_io_id(state)));
            ASSERT_NOT_NULL(nmo_behavior_index_find(
                index, nmo_behaviorlink_out_io_id(state)));
        }

        if (nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            nmo_object_t *source = NULL;
            ASSERT_NOT_NULL(state);
            if (!nmo_behavior_index_find(index, nmo_object_get_id(object))) {
                continue;
            }
            const nmo_object_id_t source_id =
                nmo_parameterin_source_id(state);
            if (source_id != 0) {
                source = nmo_object_repository_find_by_id(repo, source_id);
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
                    nmo_parameterout_destination_id(state, j);
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
    io_state->old_flags = 0u;
    if ((flags & CK_BEHAVIORIO_IN) != 0u) {
        io_state->old_flags |= NMO_BEHAVIORIO_OLD_IN;
    }
    if ((flags & CK_BEHAVIORIO_OUT) != 0u) {
        io_state->old_flags |= NMO_BEHAVIORIO_OLD_OUT;
    }
    if ((flags & CK_BEHAVIORIO_ACTIVE) != 0u) {
        io_state->old_flags |= NMO_BEHAVIORIO_OLD_ACTIVE;
    }
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
              nmo_beobject_script_array_append(
                  &owner_state->scripts, fixture->root_behavior_id));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(&root_state->sub_behaviors,
                                            fixture->source_behavior_id, NULL));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(&root_state->sub_behaviors,
                                            fixture->target_behavior_id, NULL));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(
                  &root_state->inputs, fixture->root_input_id, NULL));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(
                  &root_state->outputs, fixture->root_output_id, NULL));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(
                  &source_state->outputs, fixture->source_output_id, NULL));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(
                  &target_state->inputs, fixture->target_input_id, NULL));

    root_state->flags |= 0x00000002u;
    nmo_behavior_set_owner_id(root_state, owner_id);
    root_state->has_save_flags = true;
    root_state->save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV |
                              CK_STATESAVE_BEHAVIORINPUTS |
                              CK_STATESAVE_BEHAVIOROUTPUTS;

    nmo_behavior_set_owner_id(source_state, fixture->root_behavior_id);
    source_state->has_save_flags = true;
    source_state->save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;

    nmo_behavior_set_owner_id(target_state, fixture->root_behavior_id);
    target_state->has_save_flags = true;
    target_state->save_flags |= CK_STATESAVE_BEHAVIORINPUTS;

    set_io_direction_or_fail(session, fixture->root_input_id, CK_BEHAVIORIO_IN);
    set_io_direction_or_fail(session, fixture->root_output_id, CK_BEHAVIORIO_OUT);
    set_io_direction_or_fail(session, fixture->source_output_id, CK_BEHAVIORIO_OUT);
    set_io_direction_or_fail(session, fixture->target_input_id, CK_BEHAVIORIO_IN);
}

static void install_cross_section_manager_or_fail(
    nmo_session_t *session,
    bool attribute_manager)
{
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(session));
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, attribute_manager ? 0x52u : 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    if (attribute_manager) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 8u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, 0x44434241u));
    if (attribute_manager) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 17u));
    }
    nmo_chunk_close(chunk);

    nmo_manager_data_t *manager = (nmo_manager_data_t *)nmo_arena_alloc(
        nmo_session_get_arena(session), sizeof(*manager),
        _Alignof(nmo_manager_data_t));
    ASSERT_NOT_NULL(manager);
    *manager = (nmo_manager_data_t){
        .guid = attribute_manager
            ? NMO_MANAGER_GUID_ATTRIBUTE
            : NMO_MANAGER_GUID_MESSAGE,
        .data_size = (uint32_t)nmo_chunk_get_size(chunk),
        .chunk = chunk,
        .flags = 0u,
    };
    nmo_session_set_manager_data(session, manager, 1u);
}

static nmo_object_id_t find_named_parameter_in_ids(
    nmo_object_repository_t *repo,
    const nmo_array_t *ids,
    const char *name)
{
    for (size_t i = 0; ids && i < ids->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, id);
        const char *param_name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        if (param_name && strcmp(param_name, name) == 0) {
            return id;
        }
    }
    return 0;
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
    ASSERT_EQ(NMO_REF_NONE, child_state->parent.state);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "rollback-test", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_NOT_NULL(nmo_script_edit_workspace_edit(tx));

    snprintf(parent_text, sizeof(parent_text), "%u", parent_id);
    {
        nmo_session_field_edit_t field = {"parent", parent_text};
        ASSERT_EQ(NMO_OK,
                  nmo_object_edit_set_fields(
                      nmo_script_edit_workspace_edit(tx), child_id, &field, 1, NULL));
    }
    ASSERT_EQ(parent_id, nmo_ref_runtime_id(&child_state->parent));

    ASSERT_EQ(NMO_OK,
              nmo_workspace_edit_snapshot_bytes(
                  nmo_script_edit_workspace_edit(tx),
                  &child_state->parent,
                  sizeof(child_state->parent)));
    child_state->parent = nmo_ref_from_id(999999u);
    nmo_script_edit_mark(
        tx, NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);

    ASSERT_NE(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(999999u, nmo_ref_runtime_id(&child_state->parent));

    nmo_script_edit_rollback(tx);
    ASSERT_EQ(NMO_REF_NONE, child_state->parent.state);

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
    ASSERT_TRUE(nmo_behavior_target_parameter_id(node_state) != 0u);

    {
        nmo_object_t *target_param_obj =
            nmo_object_repository_find_by_id(
                repo, nmo_behavior_target_parameter_id(node_state));
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
        for (size_t i = 0; i < node_state->local_parameters.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->local_parameters, i);
            nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, id);
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
    {
        nmo_object_id_t caret_id =
            find_named_parameter_in_ids(repo, &node_state->in_parameters, "Caret Size");
        nmo_object_t *caret_obj =
            caret_id ? nmo_object_repository_find_by_id(repo, caret_id) : NULL;
        nmo_parameterin_state_t *caret_state = caret_obj
            ? (nmo_parameterin_state_t *)nmo_object_get_state(caret_obj)
            : NULL;
        float caret_value = 0.0f;
        ASSERT_TRUE(caret_id != 0u);
        ASSERT_NOT_NULL(caret_state);
        ASSERT_TRUE(nmo_parameterin_source_id(caret_state) != 0u);
        nmo_object_t *source_obj =
            nmo_object_repository_find_by_id(
                repo, nmo_parameterin_source_id(caret_state));
        nmo_parameter_state_t *source_state = source_obj
            ? nmo_parameter_get_mutable_state(source_obj)
            : NULL;
        ASSERT_NOT_NULL(source_state);
        ASSERT_EQ(CKPARAM_MODE_BUFFER, source_state->mode);
        ASSERT_TRUE(source_state->buffer_data.count >= sizeof(caret_value));
        memcpy(&caret_value, source_state->buffer_data.data, sizeof(caret_value));
        ASSERT_TRUE(fabsf(caret_value - 10.0f) < 0.0001f);
    }
    {
        ASSERT_TRUE(node_state->inputs.count > 0u);
        for (size_t i = 0; i < node_state->inputs.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->inputs, i);
            nmo_object_t *io_obj = nmo_object_repository_find_by_id(repo, id);
            nmo_behaviorio_state_t *io_state = io_obj
                ? (nmo_behaviorio_state_t *)nmo_object_get_state(io_obj)
                : NULL;
            ASSERT_NOT_NULL(io_state);
            ASSERT_EQ(NMO_BEHAVIORIO_OLD_IN, io_state->old_flags);
            ASSERT_TRUE(io_state->has_flags);
        }
    }
    {
        ASSERT_TRUE(node_state->outputs.count > 0u);
        for (size_t i = 0; i < node_state->outputs.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->outputs, i);
            nmo_object_t *io_obj = nmo_object_repository_find_by_id(repo, id);
            nmo_behaviorio_state_t *io_state = io_obj
                ? (nmo_behaviorio_state_t *)nmo_object_get_state(io_obj)
                : NULL;
            ASSERT_NOT_NULL(io_state);
            ASSERT_EQ(NMO_BEHAVIORIO_OLD_OUT, io_state->old_flags);
            ASSERT_TRUE(io_state->has_flags);
        }
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
        for (size_t i = 0; i < node_state->inputs.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->inputs, i);
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, id));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, id));
        }
    }
    {
        for (size_t i = 0; i < node_state->outputs.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->outputs, i);
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, id));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, id));
        }
    }
    {
        for (size_t i = 0; i < node_state->in_parameters.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->in_parameters, i);
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, id));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, id));
        }
    }
    {
        for (size_t i = 0; i < node_state->out_parameters.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->out_parameters, i);
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, id));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, id));
        }
    }
    {
        for (size_t i = 0; i < node_state->local_parameters.count; ++i) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &node_state->local_parameters, i);
            ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, id));
            ASSERT_NOT_NULL(nmo_behavior_index_find(index, id));
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
    nmo_document_destroy(document);
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

TEST(script_edit_transaction, add_node_materializes_targetable_beobject_target)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    script_control_fixture_t fixture;
    nmo_object_repository_t *repo = NULL;
    nmo_object_id_t node_id = 0;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    setup_script_control_fixture(session, &fixture);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "targetable-beobject", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_node(
                  tx,
                  fixture.root_behavior_id,
                  nmo_guid_parse("18655B3F-68291DC3"),
                  "Output To Console",
                  &node_id));

    nmo_object_t *node_obj = nmo_object_repository_find_by_id(repo, node_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);
    ASSERT_TRUE((node_state->flags & CKBEHAVIOR_TARGETABLE) != 0u);
    ASSERT_TRUE(nmo_behavior_target_parameter_id(node_state) != 0u);

    nmo_object_t *target_obj =
        nmo_object_repository_find_by_id(
            repo, nmo_behavior_target_parameter_id(node_state));
    nmo_parameterin_state_t *target_state = target_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(target_obj)
        : NULL;
    ASSERT_NOT_NULL(target_state);
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_BEOBJECT, target_state->type_guid));

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
     remove_io_detaches_behavior_links)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_t *root_object = NULL;
    nmo_object_t *link_object = NULL;
    nmo_behavior_state_t *root_state = NULL;
    nmo_behaviorlink_state_t *link_state = NULL;
    script_control_fixture_t fixture;
    nmo_object_id_t link_id = 0;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    setup_script_control_fixture(session, &fixture);

    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(session,
                                        0,
                                        "Typed behavior link",
                                        CKPGUID_BEHAVIORLINK,
                                        &link_id,
                                        NULL));
    ASSERT_TRUE(link_id != 0u);
    repo = nmo_session_get_repository(session);
    registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(repo);
    ASSERT_NOT_NULL(registry);
    root_object = nmo_object_repository_find_by_id(
        repo, fixture.root_behavior_id);
    link_object = nmo_object_repository_find_by_id(repo, link_id);
    ASSERT_NOT_NULL(root_object);
    ASSERT_NOT_NULL(link_object);
    root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_object);
    link_state = (nmo_behaviorlink_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, link_object, CKPGUID_BEHAVIORLINK);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(link_state);
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(
                  &root_state->sub_behavior_links, link_id, NULL));
    nmo_behaviorlink_set_in_io_id(link_state, fixture.source_output_id);
    nmo_behaviorlink_set_out_io_id(link_state, fixture.target_input_id);
    ASSERT_EQ(0, nmo_object_get_class_id(link_object));

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "remove-linked-io", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));
    nmo_behaviorlink_set_out_io_id(link_state, fixture.root_behavior_id);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));
    nmo_behaviorlink_set_out_io_id(link_state, fixture.target_input_id);
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_remove_io(tx, fixture.source_output_id, false));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_remove_io(tx, fixture.source_output_id, true));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));

    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(tx));
    ASSERT_NULL(nmo_object_repository_find_by_id(
        nmo_session_get_repository(session), link_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction,
     reference_validation_allows_value_only_parameter_edit)
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
    ASSERT_EQ(NMO_OK,
              nmo_ref_graph_validate(ref_graph, &broken_edges, &broken_count));
    ASSERT_EQ(0u, broken_count);

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

TEST(script_edit_transaction, connects_parameters_across_explicit_parent_graph)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *root_object = NULL;
    nmo_object_t *source_behavior_object = NULL;
    nmo_object_t *target_behavior_object = NULL;
    nmo_object_t *source_object = NULL;
    nmo_object_t *target_object = NULL;
    nmo_behavior_state_t *source_behavior = NULL;
    nmo_behavior_state_t *target_behavior = NULL;
    nmo_parameterout_state_t *source = NULL;
    nmo_parameterin_state_t *target = NULL;
    script_control_fixture_t fixture;
    nmo_object_id_t source_id = 0u;
    nmo_object_id_t target_id = 0u;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    setup_script_control_fixture(session, &fixture);
    create_object_or_fail(
        session, NMO_CID_PARAMETEROUT, "Source value", &source_id);
    create_object_or_fail(
        session, NMO_CID_PARAMETERIN, "Target value", &target_id);

    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    root_object = nmo_object_repository_find_by_id(
        repo, fixture.root_behavior_id);
    source_behavior_object = nmo_object_repository_find_by_id(
        repo, fixture.source_behavior_id);
    target_behavior_object = nmo_object_repository_find_by_id(
        repo, fixture.target_behavior_id);
    source_object = nmo_object_repository_find_by_id(repo, source_id);
    target_object = nmo_object_repository_find_by_id(repo, target_id);
    ASSERT_NOT_NULL(root_object);
    ASSERT_NOT_NULL(source_behavior_object);
    ASSERT_NOT_NULL(target_behavior_object);
    ASSERT_NOT_NULL(source_object);
    ASSERT_NOT_NULL(target_object);

    source_behavior = (nmo_behavior_state_t *)
        nmo_object_get_state(source_behavior_object);
    target_behavior = (nmo_behavior_state_t *)
        nmo_object_get_state(target_behavior_object);
    source = (nmo_parameterout_state_t *)nmo_object_get_state(source_object);
    target = (nmo_parameterin_state_t *)nmo_object_get_state(target_object);
    ASSERT_NOT_NULL(source_behavior);
    ASSERT_NOT_NULL(target_behavior);
    ASSERT_NOT_NULL(source);
    ASSERT_NOT_NULL(target);

    ASSERT_EQ(NMO_OK,
              nmo_object_repository_set_type_guid(
                  repo, fixture.root_behavior_id, CKPGUID_BEHAVIOR));
    root_object->class_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(
                  &source_behavior->out_parameters, source_id, NULL));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_ref_array_append(
                  &target_behavior->in_parameters, target_id, NULL));
    source->base.type_guid = CKPGUID_INT;
    nmo_parameterout_set_owner_id(source, fixture.source_behavior_id);
    target->type_guid = CKPGUID_INT;
    nmo_parameterin_set_owner_id(target, fixture.target_behavior_id);
    nmo_parameterin_set_source_id(target, 0u);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(
                  ctx, session, "connect across typed parent", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_connect_parameter(tx, source_id, target_id));
    ASSERT_EQ(source_id, nmo_parameterin_source_id(target));
    ASSERT_EQ(1u, source->destination_count);
    ASSERT_EQ(target_id, nmo_parameterout_destination_id(source, 0u));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction, add_node_rejects_cross_section_manager_strings)
{
    for (size_t attribute_manager = 0u; attribute_manager < 2u;
         ++attribute_manager) {
        nmo_context_t *ctx = nmo_context_create(
            &(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
        ASSERT_NOT_NULL(ctx);
        nmo_session_t *session = nmo_session_create(ctx);
        ASSERT_NOT_NULL(session);
        script_control_fixture_t fixture;
        setup_script_control_fixture(session, &fixture);
        install_cross_section_manager_or_fail(
            session, attribute_manager != 0u);

        const nmo_behavior_param_desc_t input_param = {
            .name = "Manager value",
            .type_guid = attribute_manager != 0u
                ? CKPGUID_ATTRIBUTE
                : CKPGUID_MESSAGE,
            .default_value = "ABCD",
        };
        const nmo_guid_t proto_guid = {
            0x12340000u + (uint32_t)attribute_manager,
            0x56780000u,
        };
        const nmo_behavior_proto_t proto = {
            .guid = proto_guid,
            .name = "Manager boundary probe",
            .input_params = &input_param,
            .input_param_count = 1u,
        };
        ASSERT_EQ(NMO_OK, nmo_behavior_registry_add(
            nmo_context_get_bb_registry(ctx), &proto));

        nmo_script_edit_tx_t *tx = NULL;
        ASSERT_EQ(NMO_OK, begin_test_script_edit(
            ctx, session, "manager section boundary", &tx));
        nmo_object_id_t node_id = 0u;
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_script_edit_add_node(
            tx, fixture.root_behavior_id, proto_guid,
            "Manager boundary node", &node_id));
        ASSERT_EQ(0u, node_id);

        nmo_script_edit_rollback(tx);
        nmo_session_destroy(session);
        nmo_context_release(ctx);
    }
}

TEST(script_edit_transaction, writes_explicit_parameter_values)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_repository_t *repo = NULL;
    const nmo_type_registry_t *registry = NULL;
    nmo_behavior_state_t *root_state = NULL;
    nmo_parameterlocal_state_t *local_state = NULL;
    nmo_parameterout_state_t *output_state = NULL;
    script_control_fixture_t fixture;
    nmo_object_id_t local_id = 0u;
    nmo_object_id_t output_id = 0u;
    nmo_object_id_t conflicting_id = 0u;
    const uint8_t output_bytes[] = {1u, 2u, 3u, 4u};
    const uint8_t zero_bytes[sizeof(output_bytes)] = {0};
    int32_t local_value = 0;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    setup_script_control_fixture(session, &fixture);
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "Typed local", CKPGUID_PARAMETERLOCAL, &local_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "Typed output", CKPGUID_PARAMETEROUT, &output_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "Conflicting type", CKPGUID_BEHAVIORIO,
        &conflicting_id, NULL));

    repo = nmo_session_get_repository(session);
    registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(repo);
    ASSERT_NOT_NULL(registry);

    nmo_object_t *root_object = nmo_object_repository_find_by_id(
        repo, fixture.root_behavior_id);
    nmo_object_t *local_object =
        nmo_object_repository_find_by_id(repo, local_id);
    nmo_object_t *output_object =
        nmo_object_repository_find_by_id(repo, output_id);
    ASSERT_NOT_NULL(root_object);
    ASSERT_NOT_NULL(local_object);
    ASSERT_NOT_NULL(output_object);

    root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_object);
    local_state = (nmo_parameterlocal_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, local_object, CKPGUID_PARAMETERLOCAL);
    output_state = (nmo_parameterout_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, output_object, CKPGUID_PARAMETEROUT);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(local_state);
    ASSERT_NOT_NULL(output_state);
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root_state->local_parameters, local_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root_state->out_parameters, output_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root_state->local_parameters, conflicting_id, NULL));
    root_state->save_flags |= CK_STATESAVE_BEHAVIORLOCALPARAMS |
                              CK_STATESAVE_BEHAVIOROUTPARAMS;

    local_state->base.type_guid = CKPGUID_INT;
    local_state->base.mode = CKPARAM_MODE_BUFFER;
    local_state->base.has_state = true;
    nmo_parameterlocal_set_owner_id(local_state, fixture.root_behavior_id);
    ASSERT_EQ(NMO_OK, nmo_array_alloc(
        &local_state->base.buffer_data, sizeof(uint8_t),
        sizeof(local_value), NULL));
    memset(local_state->base.buffer_data.data, 0,
           local_state->base.buffer_data.count);

    output_state->base.type_guid = CKPGUID_VOIDBUF;
    output_state->base.mode = CKPARAM_MODE_BUFFER;
    output_state->base.has_state = true;
    nmo_parameterout_set_owner_id(output_state, fixture.root_behavior_id);
    ASSERT_EQ(NMO_OK, nmo_array_alloc(
        &output_state->base.buffer_data, sizeof(uint8_t),
        sizeof(output_bytes), NULL));
    memset(output_state->base.buffer_data.data, 0,
           output_state->base.buffer_data.count);

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(
                  ctx, session, "write typed parameter values", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_set_parameter_value(tx, local_id, "42"));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_set_parameter_bytes(
                  tx, output_id, output_bytes, sizeof(output_bytes)));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_script_edit_set_parameter_value(
                  tx, conflicting_id, "13"));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_script_edit_set_parameter_bytes(
                  tx, conflicting_id, output_bytes, sizeof(output_bytes)));
    memcpy(&local_value, local_state->base.buffer_data.data,
           sizeof(local_value));
    ASSERT_EQ(42, local_value);
    ASSERT_EQ(0, memcmp(output_state->base.buffer_data.data,
                        output_bytes, sizeof(output_bytes)));

    nmo_script_edit_rollback(tx);
    memcpy(&local_value, local_state->base.buffer_data.data,
           sizeof(local_value));
    ASSERT_EQ(0, local_value);
    ASSERT_EQ(0, memcmp(output_state->base.buffer_data.data,
                        zero_bytes, sizeof(zero_bytes)));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction, validates_explicit_parameter_connections)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    script_control_fixture_t fixture;
    nmo_object_id_t input_id = 0u;
    nmo_object_id_t output_id = 0u;
    nmo_object_id_t operation_id = 0u;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    setup_script_control_fixture(session, &fixture);
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "Typed input", CKPGUID_PARAMETERIN, &input_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "Typed output", CKPGUID_PARAMETEROUT, &output_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session,
        0,
        "Typed operation",
        CKPGUID_PARAMETEROPERATION,
        &operation_id,
        NULL));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_t *root_object = nmo_object_repository_find_by_id(
        repo, fixture.root_behavior_id);
    nmo_object_t *input_object =
        nmo_object_repository_find_by_id(repo, input_id);
    nmo_object_t *output_object =
        nmo_object_repository_find_by_id(repo, output_id);
    nmo_object_t *operation_object =
        nmo_object_repository_find_by_id(repo, operation_id);
    nmo_behavior_state_t *root_state = root_object
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_object)
        : NULL;
    nmo_parameterin_state_t *input_state = (nmo_parameterin_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, input_object, CKPGUID_PARAMETERIN);
    nmo_parameterout_state_t *output_state = (nmo_parameterout_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, output_object, CKPGUID_PARAMETEROUT);
    nmo_parameteroperation_state_t *operation_state =
        (nmo_parameteroperation_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                registry, operation_object, CKPGUID_PARAMETEROPERATION);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(input_state);
    ASSERT_NOT_NULL(output_state);
    ASSERT_NOT_NULL(operation_state);
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root_state->in_parameters, input_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root_state->out_parameters, output_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &root_state->operations, operation_id, NULL));

    input_state->type_guid = CKPGUID_INT;
    nmo_parameterin_set_owner_id(input_state, fixture.root_behavior_id);
    nmo_parameterin_set_source_id(input_state, fixture.root_input_id);
    output_state->base.type_guid = CKPGUID_INT;
    nmo_parameterout_set_owner_id(output_state, fixture.root_behavior_id);
    output_state->destination_ids = (nmo_ref_t *)nmo_arena_alloc(
        nmo_session_get_arena(session),
        sizeof(*output_state->destination_ids),
        _Alignof(nmo_ref_t));
    ASSERT_NOT_NULL(output_state->destination_ids);
    output_state->destination_ids[0] =
        nmo_ref_from_id(fixture.root_output_id);
    output_state->destination_count = 1u;
    nmo_parameteroperation_set_owner_id(
        operation_state, fixture.root_behavior_id);
    operation_state->has_owner = 1u;
    nmo_parameteroperation_set_in1_id(
        operation_state, fixture.root_input_id);
    operation_state->has_in1 = 1u;

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(ctx, session, "validate typed params", &tx));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_validate(
                  tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));

    nmo_parameterin_set_source_id(input_state, 0u);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_validate(
                  tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));

    output_state->destination_ids = NULL;
    output_state->destination_count = 0u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_script_edit_validate(
                  tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));

    nmo_parameteroperation_set_in1_id(operation_state, input_id);
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(
                  tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction, ignores_conflicting_raw_behavior_link)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_id_t object_id = 0u;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session,
                  NMO_CID_BEHAVIORLINK,
                  "Conflicting raw link",
                  CKPGUID_MATERIAL,
                  &object_id,
                  NULL));
    nmo_object_t *object = nmo_object_repository_find_by_id(
        nmo_session_get_repository(session), object_id);
    ASSERT_NOT_NULL(object);
    ASSERT_EQ(NMO_CID_BEHAVIORLINK, nmo_object_get_class_id(object));
    ASSERT_TRUE(nmo_guid_equals(
        CKPGUID_MATERIAL, nmo_object_get_type_guid(object)));

    ASSERT_EQ(NMO_OK,
              begin_test_script_edit(
                  ctx, session, "ignore raw link conflict", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(
                  tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
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
                  add_node_rejects_cross_section_manager_strings);
    REGISTER_TEST(script_edit_transaction,
                  add_node_materializes_targetable_beobject_target);
    REGISTER_TEST(script_edit_transaction,
                  remove_link_then_add_link_keeps_validation_green_within_transaction);
    REGISTER_TEST(script_edit_transaction,
                  add_behavior_link_rejects_reversed_child_endpoint_directions);
    REGISTER_TEST(script_edit_transaction,
                  rewire_behavior_link_rejects_reversed_child_endpoint_directions);
    REGISTER_TEST(script_edit_transaction,
                  remove_io_detaches_behavior_links);
    REGISTER_TEST(script_edit_transaction,
                  reference_validation_allows_value_only_parameter_edit);
    REGISTER_TEST(script_edit_transaction,
                  reference_validation_rejects_new_broken_ref_beyond_preexisting_baseline);
    REGISTER_TEST(script_edit_transaction,
                  interface_validation_allows_preexisting_diagnostics_for_value_only_parameter_edit);
    REGISTER_TEST(script_edit_transaction,
                  connects_parameters_across_explicit_parent_graph);
    REGISTER_TEST(script_edit_transaction,
                  writes_explicit_parameter_values);
    REGISTER_TEST(script_edit_transaction,
                  validates_explicit_parameter_connections);
    REGISTER_TEST(script_edit_transaction,
                  ignores_conflicting_raw_behavior_link);
TEST_MAIN_END()







