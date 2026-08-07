#include "test_framework.h"

#include "document/nmo_document_save.h"
#include "behavior/nmo_script_edit.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_object_guids.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "runtime/nmo_workspace.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "type/nmo_type_query.h"

#include <stdio.h>
#include <string.h>

#define NMO_SCRIPT_INTERFACE_FIXTURE NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo")
#define NMO_SCRIPT_INTERFACE_TARGET_ID 253u

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

TEST(script_edit_interface, remove_io_canonicalize_updates_interface_data_in_memory)
{
    const char *io_add_path = "test_script_edit_interface_io_add.cmo";
    const char *iface_io_path = "test_script_edit_interface_io_present.cmo";
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_behavior_state_t *owner_state = NULL;
    nmo_behavior_state_t *root_state = NULL;
    nmo_object_t *root_obj = NULL;
    nmo_object_id_t io_id = 0u;
    uint32_t io_index = 0u;
    nmo_interface_body_t *body = NULL;

    remove_file_if_exists(io_add_path);
    remove_file_if_exists(iface_io_path);

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, NMO_SCRIPT_INTERFACE_FIXTURE, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "io add", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_io(tx, 229u, NMO_SCRIPT_EDIT_IO_INPUT, "IfaceIo", &io_id));
    ASSERT_TRUE(io_id != 0u);
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(tx));
    tx = NULL;
    ASSERT_TRUE(save_session_to_path(session, io_add_path));

    owner_state = find_behavior_state(session, 229u, NULL);
    ASSERT_NOT_NULL(owner_state);
    ASSERT_TRUE(owner_state->inputs.count > 1u);
    {
        for (size_t i = 0; i < owner_state->inputs.count; ++i) {
            if (nmo_behavior_ref_array_get_id(&owner_state->inputs, i) == io_id) {
                io_index = (uint32_t)i;
                break;
            }
        }
    }
    ASSERT_TRUE(io_index != 0u);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    session = NULL;
    ctx = NULL;

    ASSERT_TRUE(create_interface_graph_io_fixture(io_add_path, iface_io_path, 229u, (int32_t)io_index));

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, iface_io_path, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "io remove", &tx));
    ASSERT_EQ(NMO_OK, nmo_script_edit_remove_io(tx, io_id, false));
    ASSERT_EQ(NMO_OK, nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY));
    ASSERT_EQ(NMO_OK, nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(NMO_OK, nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_apply_interface_policy(tx,
                                                     NMO_SCRIPT_INTERFACE_TARGET_ID,
                                                     NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE));

    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, &root_obj);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(root_obj);
    ASSERT_NOT_NULL(root_state->interface_data);
    for (size_t i = 0; i < root_state->interface_data->sub_count; ++i) {
        if (root_state->interface_data->subs[i].behavior_id == 229u) {
            body = &root_state->interface_data->subs[i].body;
            break;
        }
    }
    ASSERT_NOT_NULL(body);
    ASSERT_FALSE(body_contains_outward_input_index(body, (int32_t)io_index));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_interface, remove_node_canonicalize_roundtrips_after_save)
{
    const char *node_add_path = "test_script_edit_interface_node_add.cmo";
    const char *iface_node_path = "test_script_edit_interface_node_present.cmo";
    const char *node_remove_path = "test_script_edit_interface_node_remove.cmo";
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_behavior_state_t *root_state = NULL;
    nmo_object_id_t node_id = 0u;
    int found_removed_sub = 0;

    remove_file_if_exists(node_add_path);
    remove_file_if_exists(iface_node_path);
    remove_file_if_exists(node_remove_path);

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, NMO_SCRIPT_INTERFACE_FIXTURE, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "node add", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_node(tx,
                                       NMO_SCRIPT_INTERFACE_TARGET_ID,
                                       (nmo_guid_t){ 0x18655B3Fu, 0x68291DC3u },
                                       "Iface Canon",
                                       &node_id));
    ASSERT_TRUE(node_id != 0u);
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(tx));
    tx = NULL;
    ASSERT_TRUE(save_session_to_path(session, node_add_path));
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    session = NULL;
    ctx = NULL;

    ASSERT_TRUE(create_interface_sub_fixture(node_add_path, iface_node_path, node_id));

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, iface_node_path, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "node remove", &tx));
    ASSERT_EQ(NMO_OK, nmo_script_edit_remove_node(tx, NMO_SCRIPT_INTERFACE_TARGET_ID, node_id, 0u));
    ASSERT_EQ(NMO_OK, nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY));
    ASSERT_EQ(NMO_OK, nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(NMO_OK, nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_apply_interface_policy(tx,
                                                     NMO_SCRIPT_INTERFACE_TARGET_ID,
                                                     NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE));

    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, NULL);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(root_state->interface_data);
    for (size_t i = 0; i < root_state->interface_data->sub_count; ++i) {
        if (root_state->interface_data->subs[i].behavior_id == node_id) {
            found_removed_sub = 1;
            break;
        }
    }
    ASSERT_FALSE(found_removed_sub);

    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(tx));
    tx = NULL;
    ASSERT_TRUE(save_session_to_path(session, node_remove_path));
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    session = NULL;
    ctx = NULL;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, node_remove_path, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, NULL);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(root_state->interface_data);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_interface, canonicalize_converts_raw_interface_ids_to_runtime_ids)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_behavior_state_t *root_state = NULL;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, NMO_SCRIPT_INTERFACE_FIXTURE, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));

    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, NULL);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(root_state->interface_data);
    ASSERT_FALSE(root_state->interface_ids_are_runtime);
    ASSERT_EQ((nmo_object_id_t)250u, root_state->interface_data->script.behavior_id);

    ASSERT_EQ(NMO_OK, begin_test_script_edit(ctx, session, "raw interface ids", &tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate_interface_refs(tx, NMO_SCRIPT_INTERFACE_TARGET_ID));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_apply_interface_policy(tx,
                                                     NMO_SCRIPT_INTERFACE_TARGET_ID,
                                                     NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE));

    root_state = find_behavior_state(session, NMO_SCRIPT_INTERFACE_TARGET_ID, NULL);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(root_state->interface_data);
    ASSERT_TRUE(root_state->interface_ids_are_runtime);
    ASSERT_EQ(NMO_SCRIPT_INTERFACE_TARGET_ID,
              root_state->interface_data->script.behavior_id);
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate_interface_refs(tx, NMO_SCRIPT_INTERFACE_TARGET_ID));

    nmo_script_edit_rollback(tx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_edit_interface, removes_interface_from_explicit_behavior_type)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t behavior_id = 0u;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session,
        0,
        "Typed behavior",
        CKPGUID_BEHAVIOR,
        &behavior_id,
        NULL));
    nmo_object_t *object = nmo_object_repository_find_by_id(
        nmo_session_get_repository(session), behavior_id);
    nmo_behavior_state_t *state = (nmo_behavior_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            nmo_context_get_type_registry(ctx),
            object,
            CKPGUID_BEHAVIOR);
    ASSERT_NOT_NULL(state);
    state->has_interface = true;

    nmo_script_edit_tx_t *tx = NULL;
    ASSERT_EQ(NMO_OK, begin_test_script_edit(
        ctx, session, "remove typed interface", &tx));
    ASSERT_EQ(NMO_OK, nmo_script_edit_apply_interface_policy(
        tx, behavior_id, NMO_SCRIPT_EDIT_INTERFACE_REMOVE));
    ASSERT_FALSE(state->has_interface);
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(tx));
    ASSERT_EQ(0, nmo_object_get_class_id(object));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_edit_interface,
                  remove_io_canonicalize_updates_interface_data_in_memory);
    REGISTER_TEST(script_edit_interface,
                  remove_node_canonicalize_roundtrips_after_save);
    REGISTER_TEST(script_edit_interface,
                  canonicalize_converts_raw_interface_ids_to_runtime_ids);
    REGISTER_TEST(script_edit_interface,
                  removes_interface_from_explicit_behavior_type);
TEST_MAIN_END()


