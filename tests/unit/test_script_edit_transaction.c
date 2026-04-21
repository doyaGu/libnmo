#include "test_framework.h"

#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_behavior_index.h"
#include "session/nmo_context.h"
#include "session/nmo_session_edit.h"
#include "session/nmo_session.h"
#include "session/nmo_session_util.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "format/nmo_object.h"

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
              nmo_script_edit_begin(ctx, session, "rollback-test", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_NOT_NULL(nmo_script_edit_session_edit(tx));

    snprintf(parent_text, sizeof(parent_text), "%u", parent_id);
    {
        nmo_session_field_edit_t field = {"parent_id", parent_text};
        ASSERT_EQ(NMO_OK,
                  nmo_session_edit_set_object_fields(
                      nmo_script_edit_session_edit(tx), child_id, &field, 1, NULL));
    }
    ASSERT_EQ(parent_id, child_state->parent_id);

    ASSERT_EQ(NMO_OK,
              nmo_session_edit_snapshot_bytes(
                  nmo_script_edit_session_edit(tx),
                  &child_state->parent_id,
                  sizeof(child_state->parent_id)));
    child_state->parent_id = 999999u;
    nmo_script_edit_mark(
        tx, NMO_SESSION_EDIT_OBJECT_STATE | NMO_SESSION_EDIT_REFERENCES);

    ASSERT_NE(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(999999u, child_state->parent_id);

    nmo_script_edit_rollback(tx);
    ASSERT_EQ(0u, child_state->parent_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_transaction, add_node_keeps_ballance_script_edit_validation_green)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *node_obj = NULL;
    nmo_behavior_state_t *node_state = NULL;
    const nmo_behavior_index_t *index = NULL;
    nmo_ref_graph_t *ref_graph = NULL;
    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0;
    nmo_object_id_t node_id = 0;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    ASSERT_EQ(NMO_OK,
              nmo_script_edit_begin(ctx, session, "add-node-test", &tx));
    ASSERT_NOT_NULL(tx);

    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_node(
                  tx,
                  237u,
                  nmo_guid_parse("42414C07-10000007"),
                  "Test BB",
                  &node_id));
    ASSERT_TRUE(node_id != 0u);
    node_obj = nmo_object_repository_find_by_id(repo, node_id);
    ASSERT_NOT_NULL(node_obj);
    node_state = (nmo_behavior_state_t *)nmo_object_get_state(node_obj);
    ASSERT_NOT_NULL(node_state);

    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY));
    ASSERT_EQ(NMO_OK,
              nmo_session_apply_edit_flags(
                  session,
                  NMO_SESSION_EDIT_OBJECT_STATE |
                      NMO_SESSION_EDIT_REFERENCES |
                      NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                      NMO_SESSION_EDIT_NAMES |
                      NMO_SESSION_EDIT_RESOURCES));
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
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_edit_transaction,
                  rollback_restores_original_state_after_validation_failure);
    REGISTER_TEST(script_edit_transaction,
                  add_node_keeps_ballance_script_edit_validation_green);
TEST_MAIN_END()
