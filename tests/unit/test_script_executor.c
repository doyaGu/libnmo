#include "test_framework.h"

#include "app/nmo_save.h"
#include "behavior/nmo_behavior_execute.h"
#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_behavior_index.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_script_executor.h"
#include "behavior/nmo_script_view.h"
#include "core/nmo_array.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <string.h>

#define NMO_SCRIPT_INTERFACE_FIXTURE NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo")
#define NMO_SCRIPT_INTERFACE_TARGET_ID 253u

typedef struct executor_add_io_action {
    nmo_object_id_t root_behavior_id;
    nmo_object_id_t behavior_id;
    const char *name;
    nmo_script_edit_io_kind_t kind;
    nmo_object_id_t io_id;
} executor_add_io_action_t;

typedef struct executor_add_node_action {
    nmo_object_id_t parent_id;
    nmo_object_id_t node_id;
} executor_add_node_action_t;

typedef struct executor_remove_io_action {
    nmo_object_id_t io_id;
    nmo_script_edit_interface_mode_t interface_mode;
} executor_remove_io_action_t;

typedef struct executor_remove_node_action {
    nmo_object_id_t parent_id;
    nmo_object_id_t node_id;
    nmo_script_edit_interface_mode_t interface_mode;
} executor_remove_node_action_t;

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void remove_file_if_exists(const char *path)
{
    if (path != NULL) {
        remove(path);
    }
}

static bool save_session_to_path(nmo_session_t *session, const char *path)
{
    nmo_save_options_t save_opts = nmo_save_options_default();
    remove_file_if_exists(path);
    return nmo_save_file(session, path, &save_opts) == NMO_OK;
}

static nmo_behavior_state_t *find_behavior_state(
    nmo_session_t *session,
    nmo_object_id_t behavior_id,
    nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *obj = NULL;

    if (out_object != NULL) {
        *out_object = NULL;
    }
    repo = nmo_session_get_repository(session);
    obj = repo ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
    if (obj == NULL) {
        return NULL;
    }
    if (out_object != NULL) {
        *out_object = obj;
    }
    return (nmo_behavior_state_t *)nmo_object_get_state(obj);
}

static bool create_interface_sub_fixture(const char *input_path,
                                         const char *output_path,
                                         uint32_t behavior_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_behavior_t *subs = NULL;
    nmo_arena_t *arena = NULL;
    bool ok = false;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    if (!ctx) {
        return false;
    }
    session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }
    if (nmo_session_load_file(session, input_path, NULL, NULL) != NMO_OK ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        goto cleanup;
    }

    state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, &obj);
    if (!state || !obj || !state->interface_data || state->interface_data->sub_count == 0u) {
        goto cleanup;
    }

    arena = nmo_object_get_storage_arena(obj);
    if (!arena) {
        goto cleanup;
    }

    subs = (nmo_interface_behavior_t *)nmo_arena_alloc(
        arena,
        (state->interface_data->sub_count + 1u) * sizeof(*subs),
        alignof(nmo_interface_behavior_t));
    if (!subs) {
        goto cleanup;
    }

    memcpy(subs,
           state->interface_data->subs,
           state->interface_data->sub_count * sizeof(*subs));
    subs[state->interface_data->sub_count] = state->interface_data->subs[0];
    subs[state->interface_data->sub_count].behavior_id = behavior_id;
    state->interface_data->subs = subs;
    state->interface_data->sub_count += 1u;

    ok = save_session_to_path(session, output_path);

cleanup:
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
    return ok;
}

static bool create_interface_graph_io_fixture(const char *input_path,
                                              const char *output_path,
                                              uint32_t owner_behavior_id,
                                              int32_t input_index)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_behavior_t *subs = NULL;
    nmo_interface_behavior_t *target_sub = NULL;
    const nmo_interface_behavior_t *template_sub = NULL;
    int32_t *inputs = NULL;
    nmo_arena_t *arena = NULL;
    bool ok = false;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    if (!ctx) {
        return false;
    }
    session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }
    if (nmo_session_load_file(session, input_path, NULL, NULL) != NMO_OK ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        goto cleanup;
    }

    state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, &obj);
    if (!state || !obj || !state->interface_data) {
        goto cleanup;
    }

    for (size_t i = 0; i < state->interface_data->sub_count; ++i) {
        if (state->interface_data->subs[i].body.has_graph_io &&
            state->interface_data->subs[i].body.graph_io) {
            template_sub = &state->interface_data->subs[i];
            break;
        }
    }
    if (!template_sub) {
        goto cleanup;
    }

    arena = nmo_object_get_storage_arena(obj);
    if (!arena) {
        goto cleanup;
    }

    subs = (nmo_interface_behavior_t *)nmo_arena_alloc(
        arena,
        (state->interface_data->sub_count + 1u) * sizeof(*subs),
        alignof(nmo_interface_behavior_t));
    if (!subs) {
        goto cleanup;
    }

    memcpy(subs,
           state->interface_data->subs,
           state->interface_data->sub_count * sizeof(*subs));
    subs[state->interface_data->sub_count] = *template_sub;
    subs[state->interface_data->sub_count].behavior_id = owner_behavior_id;
    state->interface_data->subs = subs;
    state->interface_data->sub_count += 1u;
    target_sub = &state->interface_data->subs[state->interface_data->sub_count - 1u];

    inputs = (int32_t *)nmo_arena_alloc(
        arena,
        (target_sub->body.graph_io->outward_input_count + 1u) * sizeof(*inputs),
        alignof(int32_t));
    if (!inputs) {
        goto cleanup;
    }

    memcpy(inputs,
           target_sub->body.graph_io->outward_inputs,
           target_sub->body.graph_io->outward_input_count * sizeof(*inputs));
    inputs[target_sub->body.graph_io->outward_input_count] = input_index;
    target_sub->body.graph_io->outward_inputs = inputs;
    target_sub->body.graph_io->outward_input_count += 1u;

    ok = save_session_to_path(session, output_path);

cleanup:
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
    return ok;
}

static int body_contains_outward_input_index(const nmo_interface_body_t *body,
                                             int32_t input_index)
{
    if (!body || !body->has_graph_io || !body->graph_io) {
        return 0;
    }
    for (size_t i = 0; i < body->graph_io->outward_input_count; ++i) {
        if (body->graph_io->outward_inputs[i] == input_index) {
            return 1;
        }
    }
    return 0;
}

static nmo_object_id_t script_interface_root_for_object_session(
    nmo_session_t *session,
    nmo_object_id_t object_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_id_t behavior_id = 0u;
    bool found_parent = false;

    if (!session || object_id == 0u) {
        return 0u;
    }
    if (nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        return 0u;
    }

    index = nmo_session_get_behavior_index(session);
    owner = index ? nmo_behavior_index_find(index, object_id) : NULL;
    if (!owner) {
        return 0u;
    }

    behavior_id = owner->owner_id;
    repo = nmo_session_get_repository(session);
    if (!repo) {
        return behavior_id;
    }

    do {
        found_parent = false;
        for (size_t i = 0; i < nmo_object_repository_get_count(repo); ++i) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
            nmo_behavior_state_t *state = NULL;
            nmo_object_id_t parent_id = 0u;

            if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
                continue;
            }

            state = (nmo_behavior_state_t *)nmo_object_get_state(object);
            if (!state || nmo_array_find(&state->sub_behaviors, &behavior_id, NULL) == 0) {
                continue;
            }

            parent_id = nmo_object_get_id(object);
            if (parent_id != 0u && parent_id != behavior_id) {
                behavior_id = parent_id;
                found_parent = true;
            }
            break;
        }
    } while (found_parent);

    return behavior_id;
}

static void load_root_behavior_counts(const char *path,
                                      nmo_object_id_t *out_behavior_id,
                                      size_t *out_inputs,
                                      size_t *out_outputs)
{
    nmo_context_t *ctx =
        nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    nmo_session_t *session = NULL;
    nmo_script_view_t script_view = {0};
    nmo_behavior_view_t behavior_view = {0};

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(ctx, path);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_script_view_at(session, 0, &script_view));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_view_from_behavior(
                  session, script_view.script_id, &behavior_view));

    if (out_behavior_id != NULL) {
        *out_behavior_id = script_view.script_id;
    }
    if (out_inputs != NULL) {
        *out_inputs = behavior_view.input_count;
    }
    if (out_outputs != NULL) {
        *out_outputs = behavior_view.output_count;
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

static nmo_status_t add_two_ios_action(nmo_script_executor_t *executor, void *user_data)
{
    executor_add_io_action_t *action = (executor_add_io_action_t *)user_data;
    nmo_session_t *session = nmo_script_executor_session(executor);
    nmo_script_edit_tx_t *tx = nmo_script_executor_transaction(executor);
    nmo_script_view_t script_view = {0};
    nmo_object_id_t input_id = 0;
    nmo_object_id_t output_id = 0;

    if (session == NULL || tx == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_script_view_at(session, 0, &script_view) != NMO_OK) {
        return NMO_ERR_NOT_FOUND;
    }

    action->root_behavior_id = script_view.script_id;

    if (nmo_script_edit_add_io(tx,
                               script_view.script_id,
                               NMO_SCRIPT_EDIT_IO_INPUT,
                               "Executor In",
                               &input_id) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }

    if (nmo_script_edit_add_io(tx,
                               script_view.script_id,
                               NMO_SCRIPT_EDIT_IO_OUTPUT,
                               "Executor Out",
                               &output_id) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }

    action->io_id = input_id;
    return NMO_OK;
}

static nmo_status_t add_io_action(nmo_script_executor_t *executor, void *user_data)
{
    executor_add_io_action_t *action = (executor_add_io_action_t *)user_data;
    nmo_script_edit_tx_t *tx = nmo_script_executor_transaction(executor);

    if (executor == NULL || action == NULL || tx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_script_edit_add_io(tx,
                                  action->behavior_id,
                                  action->kind,
                                  action->name,
                                  &action->io_id);
}

static nmo_status_t add_node_action(nmo_script_executor_t *executor, void *user_data)
{
    executor_add_node_action_t *action = (executor_add_node_action_t *)user_data;
    nmo_script_edit_tx_t *tx = nmo_script_executor_transaction(executor);

    if (tx == NULL || action == NULL || action->parent_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_script_edit_add_node(tx,
                                    action->parent_id,
                                    (nmo_guid_t){ 0x42414C07u, 0x10000007u },
                                    "Iface Canon",
                                    &action->node_id);
}

static nmo_status_t remove_io_canonicalize_action(nmo_script_executor_t *executor,
                                                  void *user_data)
{
    executor_remove_io_action_t *action = (executor_remove_io_action_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_id_t interface_behavior_id = 0u;
    nmo_status_t rc = NMO_OK;

    if (executor == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    tx = nmo_script_executor_transaction(executor);
    session = nmo_script_executor_session(executor);
    interface_behavior_id = script_interface_root_for_object_session(session, action->io_id);
    if (interface_behavior_id == 0u) {
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_script_edit_remove_io(tx, action->io_id, false);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX);
    if (rc != NMO_OK) {
        return rc;
    }

    return nmo_script_edit_apply_interface_policy(tx,
                                                  interface_behavior_id,
                                                  action->interface_mode);
}

static nmo_status_t remove_node_canonicalize_action(nmo_script_executor_t *executor,
                                                    void *user_data)
{
    executor_remove_node_action_t *action = (executor_remove_node_action_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    if (executor == NULL || action == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    tx = nmo_script_executor_transaction(executor);
    rc = nmo_script_edit_remove_node(tx, action->parent_id, action->node_id, 0u);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES);
    if (rc != NMO_OK) {
        return rc;
    }
    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX);
    if (rc != NMO_OK) {
        return rc;
    }
    return nmo_script_edit_apply_interface_policy(tx,
                                                  action->parent_id,
                                                  action->interface_mode);
}

static nmo_status_t failing_after_mutation_action(nmo_script_executor_t *executor,
                                                  void *user_data)
{
    nmo_session_t *session = nmo_script_executor_session(executor);
    nmo_script_edit_tx_t *tx = nmo_script_executor_transaction(executor);
    nmo_script_view_t script_view = {0};
    nmo_object_id_t input_id = 0;
    (void)user_data;

    if (session == NULL || tx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_script_view_at(session, 0, &script_view) != NMO_OK) {
        return NMO_ERR_NOT_FOUND;
    }

    if (nmo_script_edit_add_io(tx,
                               script_view.script_id,
                               NMO_SCRIPT_EDIT_IO_INPUT,
                               "Executor Fail",
                               &input_id) != NMO_OK) {
        return NMO_ERR_INVALID_STATE;
    }

    return NMO_ERR_INVALID_ARGUMENT;
}

TEST(script_executor, executes_multiple_actions_and_saves_once) {
    const char *input_path = NMO_TEST_DATA_FILE("Nop.cmo");
    const char *output_path = "test_script_executor_apply.cmo";
    nmo_context_t *ctx =
        nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    nmo_script_executor_options_t options = nmo_script_executor_options_default();
    nmo_script_edit_report_t report = {0};
    executor_add_io_action_t action = {0};
    nmo_object_id_t behavior_id = 0;
    size_t input_count = 0;
    size_t output_count = 0;
    size_t new_input_count = 0;
    size_t new_output_count = 0;

    ASSERT_NOT_NULL(ctx);
    remove(output_path);
    load_root_behavior_counts(input_path, &behavior_id, &input_count, &output_count);

    options.label = "test-script-executor-apply";
    ASSERT_EQ(NMO_OK,
              nmo_script_executor_execute(ctx,
                                          input_path,
                                          output_path,
                                          &options,
                                          add_two_ios_action,
                                          &action,
                                          &report));
    ASSERT_TRUE(file_exists(output_path));
    ASSERT_EQ(behavior_id, action.root_behavior_id);
    ASSERT_EQ(0u, report.errors);

    load_root_behavior_counts(output_path, NULL, &new_input_count, &new_output_count);
    ASSERT_EQ(input_count + 1u, new_input_count);
    ASSERT_EQ(output_count + 1u, new_output_count);

    remove(output_path);
    nmo_context_release(ctx);
}

TEST(script_executor, rolls_back_on_action_error_and_skips_output) {
    const char *input_path = NMO_TEST_DATA_FILE("Nop.cmo");
    const char *output_path = "test_script_executor_fail.cmo";
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_script_executor_options_t options = nmo_script_executor_options_default();
    nmo_script_edit_report_t report = {0};

    ASSERT_NOT_NULL(ctx);
    remove(output_path);

    options.label = "test-script-executor-fail";
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_script_executor_execute(ctx,
                                          input_path,
                                          output_path,
                                          &options,
                                          failing_after_mutation_action,
                                          NULL,
                                          &report));
    ASSERT_FALSE(file_exists(output_path));

    nmo_context_release(ctx);
}

TEST(script_executor, dry_run_rolls_back_after_validation) {
    const char *input_path = NMO_TEST_DATA_FILE("Nop.cmo");
    const char *output_path = "test_script_executor_dry.cmo";
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_script_executor_options_t options = nmo_script_executor_options_default();
    nmo_script_edit_report_t report = {0};
    executor_add_io_action_t action = {0};
    nmo_object_id_t behavior_id = 0;
    size_t input_count = 0;
    size_t output_count = 0;

    ASSERT_NOT_NULL(ctx);
    remove(output_path);
    load_root_behavior_counts(input_path, &behavior_id, &input_count, &output_count);

    options.label = "test-script-executor-dry";
    options.dry_run = true;
    ASSERT_EQ(NMO_OK,
              nmo_script_executor_execute(ctx,
                                          input_path,
                                          output_path,
                                          &options,
                                          add_two_ios_action,
                                          &action,
                                          &report));
    ASSERT_FALSE(file_exists(output_path));
    ASSERT_EQ(behavior_id, action.root_behavior_id);
    ASSERT_EQ(0u, report.errors);

    nmo_context_release(ctx);
}

TEST(script_executor, behavior_execute_owner_wraps_script_executor) {
    const char *input_path = NMO_SCRIPT_INTERFACE_FIXTURE;
    const char *output_path = "test_behavior_execute_owner.cmo";
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_behavior_execute_options_t options = nmo_behavior_execute_options_default();
    nmo_behavior_execute_result_t result = {0};
    executor_add_io_action_t action = {0};

    ASSERT_NOT_NULL(ctx);
    remove_file_if_exists(output_path);
    action.behavior_id = 229u;
    action.kind = NMO_SCRIPT_EDIT_IO_INPUT;
    action.name = "OwnerIo";
    options.label = "behavior-execute-owner";
    ASSERT_EQ(NMO_OK,
              nmo_behavior_execute(ctx,
                                   input_path,
                                   output_path,
                                   &options,
                                   add_io_action,
                                   &action,
                                   &result));
    ASSERT_TRUE(action.io_id != 0u);
    ASSERT_TRUE(file_exists(output_path));
    remove_file_if_exists(output_path);

    nmo_context_release(ctx);
}

TEST(script_executor, executor_remove_io_canonicalize_roundtrips_fixture) {
    const char *io_add_path = "test_script_executor_interface_io_add.cmo";
    const char *iface_io_path = "test_script_executor_interface_io_present.cmo";
    const char *io_remove_path = "test_script_executor_interface_io_remove.cmo";
    const uint32_t owner_behavior_id = 229u;
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_script_executor_options_t options = nmo_script_executor_options_default();
    executor_add_io_action_t add_action = {0};
    executor_remove_io_action_t remove_action = {
        .interface_mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE,
    };
    nmo_script_edit_report_t report = {0};
    nmo_session_t *session = NULL;
    nmo_behavior_state_t *root_state = NULL;
    nmo_interface_body_t *body = NULL;
    nmo_object_t *root_obj = NULL;
    uint32_t io_index = 0u;
    nmo_behavior_state_t *owner_state = NULL;

    ASSERT_NOT_NULL(ctx);
    remove_file_if_exists(io_add_path);
    remove_file_if_exists(iface_io_path);
    remove_file_if_exists(io_remove_path);

    add_action.behavior_id = owner_behavior_id;
    add_action.kind = NMO_SCRIPT_EDIT_IO_INPUT;
    add_action.name = "IfaceIo";
    options.label = "executor io add";
    ASSERT_EQ(NMO_OK,
              nmo_script_executor_execute(ctx,
                                          NMO_SCRIPT_INTERFACE_FIXTURE,
                                          io_add_path,
                                          &options,
                                          add_io_action,
                                          &add_action,
                                          &report));
    ASSERT_TRUE(add_action.io_id != 0u);

    session = nmo_session_load(ctx, io_add_path);
    ASSERT_NOT_NULL(session);
    owner_state = find_behavior_state(session, owner_behavior_id, NULL);
    ASSERT_NOT_NULL(owner_state);
    ASSERT_TRUE(owner_state->inputs.count > 1u);
    {
        nmo_object_id_t *ids = (nmo_object_id_t *)owner_state->inputs.data;
        for (size_t i = 0; i < owner_state->inputs.count; ++i) {
            if (ids[i] == add_action.io_id) {
                io_index = (uint32_t)i;
                break;
            }
        }
    }
    ASSERT_TRUE(io_index != 0u);
    nmo_session_destroy(session);
    session = NULL;

    ASSERT_TRUE(create_interface_graph_io_fixture(io_add_path,
                                                  iface_io_path,
                                                  owner_behavior_id,
                                                  (int32_t)io_index));

    options.label = "executor io remove";
    options.validation_flags = 0u;
    remove_action.io_id = add_action.io_id;
    ASSERT_EQ(NMO_OK,
              nmo_script_executor_execute(ctx,
                                          iface_io_path,
                                          io_remove_path,
                                          &options,
                                          remove_io_canonicalize_action,
                                          &remove_action,
                                          &report));

    session = nmo_session_load(ctx, io_remove_path);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, &root_obj);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(root_obj);
    ASSERT_NOT_NULL(root_state->interface_data);
    for (size_t i = 0; i < root_state->interface_data->sub_count; ++i) {
        if (root_state->interface_data->subs[i].behavior_id == owner_behavior_id) {
            body = &root_state->interface_data->subs[i].body;
            break;
        }
    }
    ASSERT_NOT_NULL(body);
    ASSERT_FALSE(body_contains_outward_input_index(body, (int32_t)io_index));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_executor, executor_remove_node_canonicalize_roundtrips_fixture) {
    const char *node_add_path = "test_script_executor_interface_node_add.cmo";
    const char *iface_node_path = "test_script_executor_interface_node_present.cmo";
    const char *node_remove_path = "test_script_executor_interface_node_remove.cmo";
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_script_executor_options_t options = nmo_script_executor_options_default();
    executor_add_node_action_t add_action = {0};
    executor_remove_node_action_t remove_action = {
        .parent_id = NMO_SCRIPT_INTERFACE_TARGET_ID,
        .interface_mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE,
    };
    nmo_script_edit_report_t report = {0};
    nmo_session_t *session = NULL;
    nmo_behavior_state_t *root_state = NULL;
    int found_removed_sub = 0;

    ASSERT_NOT_NULL(ctx);
    remove_file_if_exists(node_add_path);
    remove_file_if_exists(iface_node_path);
    remove_file_if_exists(node_remove_path);

    add_action.parent_id = NMO_SCRIPT_INTERFACE_TARGET_ID;
    options.label = "executor node add";
    options.validation_flags = nmo_script_executor_options_default().validation_flags;
    ASSERT_EQ(NMO_OK,
              nmo_script_executor_execute(ctx,
                                          NMO_SCRIPT_INTERFACE_FIXTURE,
                                          node_add_path,
                                          &options,
                                          add_node_action,
                                          &add_action,
                                          &report));
    ASSERT_TRUE(add_action.node_id != 0u);
    session = nmo_session_load(ctx, node_add_path);
    ASSERT_NOT_NULL(session);
    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, NULL);
    ASSERT_NOT_NULL(root_state);
    ASSERT_TRUE(nmo_array_find(&root_state->sub_behaviors, &add_action.node_id, NULL) != 0u);
    ASSERT_NOT_NULL(find_behavior_state(session, add_action.node_id, NULL));
    nmo_session_destroy(session);
    session = NULL;
    ASSERT_TRUE(create_interface_sub_fixture(node_add_path, iface_node_path, add_action.node_id));

    options.label = "executor node remove";
    options.validation_flags = 0u;
    remove_action.node_id = add_action.node_id;
    session = nmo_session_load(ctx, iface_node_path);
    ASSERT_NOT_NULL(session);
    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, NULL);
    ASSERT_NOT_NULL(root_state);
    ASSERT_TRUE(nmo_array_find(&root_state->sub_behaviors, &add_action.node_id, NULL) != 0u);
    ASSERT_NOT_NULL(find_behavior_state(session, add_action.node_id, NULL));
    nmo_session_destroy(session);
    session = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_script_executor_execute(ctx,
                                          iface_node_path,
                                          node_remove_path,
                                          &options,
                                          remove_node_canonicalize_action,
                                          &remove_action,
                                          &report));

    session = nmo_session_load(ctx, node_remove_path);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, NULL);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(root_state->interface_data);
    for (size_t i = 0; i < root_state->interface_data->sub_count; ++i) {
        if (root_state->interface_data->subs[i].behavior_id == add_action.node_id) {
            found_removed_sub = 1;
            break;
        }
    }
    ASSERT_FALSE(found_removed_sub);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_executor, behavior_execute_owner_wraps_script_executor);
    REGISTER_TEST(script_executor, executes_multiple_actions_and_saves_once);
    REGISTER_TEST(script_executor, rolls_back_on_action_error_and_skips_output);
    REGISTER_TEST(script_executor, dry_run_rolls_back_after_validation);
    REGISTER_TEST(script_executor, executor_remove_io_canonicalize_roundtrips_fixture);
    REGISTER_TEST(script_executor, executor_remove_node_canonicalize_roundtrips_fixture);
TEST_MAIN_END()
