#include "test_framework.h"

#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_probe_analyzer.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "document/nmo_document.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "../src/runtime/runtime_internal.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "type/nmo_type_guids.h"

#include <math.h>
#include <string.h>

static void create_object_or_fail(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name,
    nmo_object_id_t *out_id)
{
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, class_id, name, (nmo_guid_t){0, 0}, out_id, NULL));
    ASSERT_TRUE(*out_id != 0);
}

static nmo_edit_handle_ref_t edit_plan_test_handle_ref(
    size_t operation_index,
    const char *handle_name)
{
    return (nmo_edit_handle_ref_t){
        .has_ref = true,
        .operation_index = operation_index,
        .handle_name = handle_name,
    };
}

typedef struct edit_plan_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_object_repository_t *repo;
} edit_plan_fixture_t;

static void edit_plan_fixture_init(edit_plan_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->session = nmo_session_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->session);
    fixture->repo = nmo_session_get_repository(fixture->session);
    ASSERT_NOT_NULL(fixture->repo);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(fixture->session, &fixture->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document, &fixture->workspace));
}

static void edit_plan_fixture_dispose(edit_plan_fixture_t *fixture)
{
    if (fixture->workspace != NULL) {
        nmo_workspace_destroy(fixture->workspace);
    }
    if (fixture->document != NULL) {
        nmo_document_destroy(fixture->document);
    }
    if (fixture->session != NULL) {
        nmo_session_destroy(fixture->session);
    }
    if (fixture->ctx != NULL) {
        nmo_context_release(fixture->ctx);
    }
    memset(fixture, 0, sizeof(*fixture));
}

static void create_string_parameter(
    edit_plan_fixture_t *fixture,
    const char *initial,
    nmo_object_id_t *out_param_id,
    nmo_parameter_state_t **out_state)
{
    ASSERT_NOT_NULL(out_state);
    *out_state = NULL;
    create_object_or_fail(fixture->session, NMO_CID_PARAMETER, "param", out_param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(fixture->repo, *out_param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_STRING;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(
                  &state->buffer_data,
                  sizeof(uint8_t),
                  strlen(initial) + 1u,
                  NULL));
    memcpy(state->buffer_data.data, initial, strlen(initial) + 1u);
    *out_state = state;
}

static void create_manager_parameter(
    edit_plan_fixture_t *fixture,
    nmo_object_id_t *out_param_id,
    nmo_parameter_state_t **out_state)
{
    ASSERT_NOT_NULL(out_state);
    *out_state = NULL;
    create_object_or_fail(fixture->session, NMO_CID_PARAMETER,
                          "manager-param", out_param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(fixture->repo, *out_param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_INT;
    state->mode = CKPARAM_MODE_MANAGER;
    state->has_state = true;
    state->manager_guid = nmo_guid_parse("11111111-22222222");
    state->manager_value = 7u;
    *out_state = state;
}

static void create_int_parameter_with_buffer_size(
    edit_plan_fixture_t *fixture,
    size_t buffer_size,
    nmo_object_id_t *out_param_id,
    nmo_parameter_state_t **out_state)
{
    ASSERT_NOT_NULL(out_state);
    *out_state = NULL;
    create_object_or_fail(fixture->session, NMO_CID_PARAMETER, "int-param", out_param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(fixture->repo, *out_param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_INT;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(
                  &state->buffer_data,
                  sizeof(uint8_t),
                  buffer_size,
                  NULL));
    memset(state->buffer_data.data, 0, buffer_size);
    *out_state = state;
}

static void create_object_reference_parameter(
    edit_plan_fixture_t *fixture,
    nmo_object_id_t initial_ref,
    nmo_object_id_t *out_param_id,
    nmo_parameter_state_t **out_state)
{
    ASSERT_NOT_NULL(out_state);
    *out_state = NULL;
    create_object_or_fail(fixture->session, NMO_CID_PARAMETER,
                          "object-param", out_param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(fixture->repo, *out_param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_OBJECT;
    state->mode = CKPARAM_MODE_OBJECT;
    state->has_state = true;
    state->object_ref = nmo_ref_from_id(initial_ref);
    *out_state = state;
}

static nmo_object_id_t find_named_parameter_in_ids(
    nmo_object_repository_t *repo,
    const nmo_array_t *ids,
    const char *name)
{
    for (size_t i = 0; ids != NULL && i < ids->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, id);
        const char *param_name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        if (param_name != NULL && strcmp(param_name, name) == 0) {
            return id;
        }
    }
    return 0u;
}

static bool find_message_manager_value(
    nmo_session_t *session,
    const char *message_name,
    uint32_t *out_value)
{
    const nmo_file_state_t *file_state = nmo_session_get_file_state(session);
    if (!file_state || !file_state->manager_data || !message_name || !out_value) {
        return false;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE) ||
            manager->chunk == NULL) {
            continue;
        }

        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(session));
        if (chunk == NULL ||
            nmo_chunk_start_read(chunk) != NMO_OK ||
            nmo_chunk_seek_identifier(chunk, 0x53u) != NMO_OK) {
            continue;
        }

        int32_t count = 0;
        if (nmo_chunk_read_int(chunk, &count) != NMO_OK || count < 0) {
            continue;
        }
        for (int32_t index = 0; index < count; ++index) {
            char *name = NULL;
            (void)nmo_chunk_read_string(chunk, &name);
            if (name != NULL && strcmp(name, message_name) == 0) {
                *out_value = (uint32_t)index;
                return true;
            }
        }
    }

    return false;
}

static void install_message_manager_or_fail(
    nmo_session_t *session,
    const char *const *names,
    uint32_t name_count)
{
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, (int32_t)name_count));
    for (uint32_t i = 0; i < name_count; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_string(chunk, names[i]));
    }
    nmo_chunk_close(chunk);

    nmo_manager_data_t *manager_data =
        (nmo_manager_data_t *)nmo_arena_alloc(
            arena, sizeof(*manager_data), _Alignof(nmo_manager_data_t));
    ASSERT_NOT_NULL(manager_data);
    memset(manager_data, 0, sizeof(*manager_data));
    manager_data->guid = NMO_MANAGER_GUID_MESSAGE;
    manager_data->chunk = chunk;
    manager_data->data_size = (uint32_t)nmo_chunk_get_size(chunk);
    nmo_session_set_manager_data(session, manager_data, 1u);
}

static bool report_contains_object_id(
    const nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id)
{
    for (size_t i = 0; i < count; ++i) {
        if (items[i].id == id) {
            return true;
        }
    }
    return false;
}

static void assert_named_ids_match_strings(
    nmo_object_repository_t *repo,
    const nmo_array_t *ids,
    const char *const *names,
    uint32_t count)
{
    ASSERT_EQ((size_t)count, ids->count);
    if (count == 0u) {
        return;
    }
    ASSERT_NOT_NULL(ids->data);
    for (uint32_t i = 0; i < count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
        ASSERT_NOT_NULL(obj);
        ASSERT_STR_EQ(names[i], nmo_object_get_name(obj));
    }
}

static void assert_param_ids_match_proto(
    edit_plan_fixture_t *fixture,
    const nmo_array_t *ids,
    const nmo_behavior_param_desc_t *params,
    uint32_t count,
    nmo_class_id_t expected_class,
    bool expect_settings,
    const nmo_edit_report_t *report)
{
    ASSERT_EQ((size_t)count, ids->count);
    if (count == 0u) {
        return;
    }
    ASSERT_NOT_NULL(ids->data);
    for (uint32_t i = 0; i < count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *obj = nmo_object_repository_find_by_id(fixture->repo, id);
        ASSERT_NOT_NULL(obj);
        ASSERT_EQ(expected_class, nmo_object_get_class_id(obj));
        ASSERT_STR_EQ(params[i].name, nmo_object_get_name(obj));
        ASSERT_TRUE(report_contains_object_id(
            report->created_objects, report->created_object_count, id));

        if (expected_class == NMO_CID_PARAMETERIN) {
            nmo_parameterin_state_t *state =
                (nmo_parameterin_state_t *)nmo_object_get_state(obj);
            ASSERT_NOT_NULL(state);
            ASSERT_TRUE(nmo_guid_equals(params[i].type_guid, state->type_guid));
            if (params[i].default_value != NULL && params[i].default_value[0] != '\0') {
                ASSERT_TRUE(nmo_parameterin_source_id(state) != 0u);
                ASSERT_NOT_NULL(nmo_object_repository_find_by_id(
                    fixture->repo, nmo_parameterin_source_id(state)));
                ASSERT_TRUE(report_contains_object_id(
                    report->created_objects,
                    report->created_object_count,
                    nmo_parameterin_source_id(state)));
            }
        } else if (expected_class == NMO_CID_PARAMETEROUT) {
            nmo_parameterout_state_t *state =
                (nmo_parameterout_state_t *)nmo_object_get_state(obj);
            ASSERT_NOT_NULL(state);
            ASSERT_TRUE(nmo_guid_equals(params[i].type_guid, state->base.type_guid));
        } else if (expected_class == NMO_CID_PARAMETERLOCAL) {
            nmo_parameterlocal_state_t *state =
                (nmo_parameterlocal_state_t *)nmo_object_get_state(obj);
            ASSERT_NOT_NULL(state);
            ASSERT_TRUE(nmo_guid_equals(params[i].type_guid, state->base.type_guid));
            ASSERT_EQ(expect_settings ? 1u : 0u, (uint32_t)state->is_setting);
        }
    }
}

static nmo_behavior_state_t *find_first_behavior_by_block_guid(
    nmo_object_repository_t *repo,
    nmo_guid_t guid,
    nmo_object_t **out_object)
{
    if (out_object != NULL) {
        *out_object = NULL;
    }
    if (repo == NULL) {
        return NULL;
    }
    size_t count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (obj == NULL || nmo_object_get_class_id(obj) != NMO_CID_BEHAVIOR) {
            continue;
        }
        nmo_behavior_state_t *state =
            (nmo_behavior_state_t *)nmo_object_get_state(obj);
        if (state != NULL && nmo_guid_equals(state->block_guid, guid)) {
            if (out_object != NULL) {
                *out_object = obj;
            }
            return state;
        }
    }
    return NULL;
}

static nmo_guid_t parameter_type_guid_for_object(nmo_object_t *obj)
{
    if (obj == NULL) {
        return NMO_GUID_NULL;
    }
    switch (nmo_object_get_class_id(obj)) {
    case NMO_CID_PARAMETERIN:
        return ((nmo_parameterin_state_t *)nmo_object_get_state(obj))->type_guid;
    case NMO_CID_PARAMETEROUT:
        return ((nmo_parameterout_state_t *)nmo_object_get_state(obj))->base.type_guid;
    case NMO_CID_PARAMETERLOCAL:
        return ((nmo_parameterlocal_state_t *)nmo_object_get_state(obj))->base.type_guid;
    default:
        return NMO_GUID_NULL;
    }
}

static nmo_object_id_t parameter_source_id_for_object(nmo_object_t *obj)
{
    if (obj == NULL || nmo_object_get_class_id(obj) != NMO_CID_PARAMETERIN) {
        return 0u;
    }
    return nmo_parameterin_source_id(
        (nmo_parameterin_state_t *)nmo_object_get_state(obj));
}

static void assert_named_object_arrays_match(
    nmo_object_repository_t *golden_repo,
    const nmo_array_t *golden_ids,
    nmo_object_repository_t *actual_repo,
    const nmo_array_t *actual_ids)
{
    ASSERT_EQ(golden_ids ? golden_ids->count : 0u,
              actual_ids ? actual_ids->count : 0u);
    for (size_t i = 0; golden_ids != NULL && i < golden_ids->count; ++i) {
        ASSERT_NOT_NULL(golden_ids->data);
        ASSERT_NOT_NULL(actual_ids->data);
        nmo_object_id_t golden_id = nmo_behavior_ref_array_get_id(golden_ids, i);
        nmo_object_id_t actual_id = nmo_behavior_ref_array_get_id(actual_ids, i);
        nmo_object_t *golden_obj =
            nmo_object_repository_find_by_id(golden_repo, golden_id);
        nmo_object_t *actual_obj =
            nmo_object_repository_find_by_id(actual_repo, actual_id);
        ASSERT_NOT_NULL(golden_obj);
        ASSERT_NOT_NULL(actual_obj);
        ASSERT_EQ(nmo_object_get_class_id(golden_obj),
                  nmo_object_get_class_id(actual_obj));
        ASSERT_STR_EQ(nmo_object_get_name(golden_obj),
                      nmo_object_get_name(actual_obj));
    }
}

static void assert_parameter_shape_arrays_match(
    nmo_object_repository_t *golden_repo,
    const nmo_array_t *golden_ids,
    nmo_object_repository_t *actual_repo,
    const nmo_array_t *actual_ids)
{
    ASSERT_EQ(golden_ids ? golden_ids->count : 0u,
              actual_ids ? actual_ids->count : 0u);
    for (size_t i = 0; golden_ids != NULL && i < golden_ids->count; ++i) {
        ASSERT_NOT_NULL(golden_ids->data);
        ASSERT_NOT_NULL(actual_ids->data);
        nmo_object_id_t golden_id = nmo_behavior_ref_array_get_id(golden_ids, i);
        nmo_object_id_t actual_id = nmo_behavior_ref_array_get_id(actual_ids, i);
        nmo_object_t *golden_obj =
            nmo_object_repository_find_by_id(golden_repo, golden_id);
        nmo_object_t *actual_obj =
            nmo_object_repository_find_by_id(actual_repo, actual_id);
        ASSERT_NOT_NULL(golden_obj);
        ASSERT_NOT_NULL(actual_obj);
        ASSERT_EQ(nmo_object_get_class_id(golden_obj),
                  nmo_object_get_class_id(actual_obj));
        ASSERT_STR_EQ(nmo_object_get_name(golden_obj),
                      nmo_object_get_name(actual_obj));
        ASSERT_TRUE(nmo_guid_equals(parameter_type_guid_for_object(golden_obj),
                                    parameter_type_guid_for_object(actual_obj)));
        ASSERT_EQ(parameter_source_id_for_object(golden_obj) != 0u ? 1u : 0u,
                  parameter_source_id_for_object(actual_obj) != 0u ? 1u : 0u);
    }
}

TEST(edit_plan, stores_parameter_value_ops) {
    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(0u, nmo_edit_plan_count(plan));

    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_parameter_value(plan, 42, NULL, "value", NULL));
    ASSERT_EQ(1u, nmo_edit_plan_count(plan));
    const nmo_edit_op_t *op = nmo_edit_plan_get(plan, 0);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_SET_PARAMETER_VALUE, op->kind);
    ASSERT_EQ(42u, op->primary_id);
    ASSERT_STR_EQ("value", op->data.set_value.value);

    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan, stores_full_script_edit_ops_and_clones_plan) {
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_plan_t *clone = NULL;
    nmo_behavior_fold_map_t map = {
        .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
        .old_index = 0,
        .new_index = 1,
        .old_id = 11,
        .new_id = 22,
        .label = "In",
    };
    nmo_object_id_t fold_nodes[] = {101, 102};
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 500,
        .node_ids = fold_nodes,
        .node_count = 2,
        .anchor_id = 101,
        .block_guid = nmo_guid_parse("11111111-22222222"),
        .name = "Folded",
        .preserve_boundary = true,
        .input_maps = &map,
        .input_map_count = 1,
    };
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 600,
        .block_guid = nmo_guid_parse("33333333-44444444"),
        .name = "Replacement",
        .preserve_links = true,
        .preserve_params = true,
    };

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_node(plan, 1, 2, 3));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_io(
        plan, 4, NMO_SCRIPT_EDIT_IO_INPUT, "Input"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rename_io(plan, 5, "Renamed"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_io(plan, 6, true));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_behavior_link(plan, 7, 8, NULL, 9, NULL, 10));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rewire_behavior_link(plan, 11, 12, 13));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_behavior_link_delay(plan, 14, 15));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_behavior_link(plan, 16, 17));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_parameter(
        plan, 18, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_STRING, "Param"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_connect_parameter(plan, 19, 20, NULL));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_disconnect_parameter(plan, 21));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_parameter(plan, 22, true));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_operation(
        plan, 23, nmo_guid_parse("55555555-66666666"), 24, NULL, 25, NULL, 26, NULL));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rewire_operation(
        plan, 27, NMO_SCRIPT_EDIT_OP_SLOT_IN1, 28, NULL, 29, NULL, 30, NULL));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_operation(plan, 31));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_interface_policy(
        plan, 32, NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_data_cell(plan, 33, 1, 2, "cell"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    ASSERT_EQ(19u, nmo_edit_plan_count(plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_clone(plan, &clone));
    ASSERT_EQ(nmo_edit_plan_count(plan), nmo_edit_plan_count(clone));

    const nmo_edit_op_t *op = nmo_edit_plan_get(clone, 17);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_FOLD, op->kind);
    ASSERT_EQ(500u, op->primary_id);
    ASSERT_EQ(2u, op->data.fold.desc.node_count);
    ASSERT_EQ(101u, op->data.fold.node_ids[0]);
    ASSERT_EQ(102u, op->data.fold.node_ids[1]);
    ASSERT_EQ(1u, op->data.fold.input_maps[0].new_index);
    ASSERT_STR_EQ("Folded", op->data.fold.desc.name);

    op = nmo_edit_plan_get(clone, 18);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_REPLACE_BB, op->kind);
    ASSERT_STR_EQ("Replacement", op->data.replace_bb.desc.name);

    nmo_edit_plan_destroy(clone);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan, report_dispose_releases_schema_v2_arrays) {
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));

    ASSERT_EQ(NMO_OK, nmo_edit_report_add_operation_handle(&report, 0, "node", 42));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_created_object(
        &report, 42, NMO_EDIT_OP_ADD_NODE, "behavior"));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_deleted_object(
        &report, 43, NMO_EDIT_OP_REMOVE_NODE, "behavior"));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_changed_object(
        &report, 44, NMO_EDIT_OP_SET_PARAMETER_VALUE, "parameter"));

    ASSERT_EQ(1u, report.created_object_count);
    ASSERT_EQ(42u, report.created_objects[0].id);
    ASSERT_STR_EQ("behavior", report.created_objects[0].role);
    ASSERT_EQ(1u, report.deleted_object_count);
    ASSERT_EQ(43u, report.deleted_objects[0].id);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(44u, report.changed_objects[0].id);

    nmo_edit_report_dispose(&report);
    ASSERT_EQ(0u, report.created_object_count);
    ASSERT_EQ(NULL, report.created_objects);
    ASSERT_EQ(NULL, report.deleted_objects);
    ASSERT_EQ(NULL, report.changed_objects);
}

TEST(edit_plan, report_preserves_distinct_impact_roles) {
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));

    ASSERT_EQ(NMO_OK, nmo_edit_report_add_changed_object(
        &report, 44, NMO_EDIT_OP_CONNECT_PARAMETER, "primary"));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_changed_object(
        &report, 44, NMO_EDIT_OP_CONNECT_PARAMETER, "parameter_edge_target"));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_changed_object(
        &report, 44, NMO_EDIT_OP_CONNECT_PARAMETER, "parameter_edge_target"));

    ASSERT_EQ(2u, report.changed_object_count);
    ASSERT_EQ(44u, report.changed_objects[0].id);
    ASSERT_STR_EQ("primary", report.changed_objects[0].role);
    ASSERT_EQ(44u, report.changed_objects[1].id);
    ASSERT_STR_EQ("parameter_edge_target", report.changed_objects[1].role);

    nmo_edit_report_dispose(&report);
}

TEST(edit_plan, report_owns_schema_v2_output_path) {
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));

    ASSERT_EQ(NMO_OK, nmo_edit_report_set_output_path(&report, "first.cmo"));
    ASSERT_STR_EQ("first.cmo", report.output_path);
    ASSERT_EQ(NMO_OK, nmo_edit_report_set_output_path(&report, "second.cmo"));
    ASSERT_STR_EQ("second.cmo", report.output_path);
    ASSERT_EQ(NMO_OK, nmo_edit_report_set_output_path(&report, NULL));
    ASSERT_EQ(NULL, report.output_path);

    nmo_edit_report_dispose(&report);
}

TEST(edit_plan, executor_commits_parameter_value_plan) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_string_parameter(&fixture, "old", &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, param_id, NULL, "new value", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_FALSE(report.dry_run);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(param_id, report.changed_objects[0].id);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, "new value", strlen("new value") + 1u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_report_carries_probe_selector_analysis) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_string_parameter(&fixture, "old", &param_id, &state);

    nmo_probe_selector_result_t analysis;
    nmo_probe_selector_result_init(&analysis);
    analysis.mode = NMO_PROBE_SELECTOR_MODE_AUTO;
    analysis.status = NMO_PROBE_SELECTOR_STATUS_SELECTED;
    analysis.selected_node_id = 1667u;
    analysis.selected_link_id = 2152u;
    analysis.safe_insertion.selected = true;
    analysis.safe_insertion.selected_node_id = 1667u;
    analysis.safe_insertion.remove_link_id = 2152u;
    ASSERT_EQ(NMO_OK,
              nmo_probe_selector_result_add_candidate(
                  &analysis,
                  &(nmo_probe_selector_candidate_t){
                      .node_id = 1667u,
                      .parent_id = 2172u,
                      .boundary_behavior_id = 2172u,
                      .link_id = 2152u,
                      .confidence = 1.0,
                      .role = NMO_PROBE_CANDIDATE_MESSAGE_SENDER,
                  }));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, param_id, NULL, "new value", NULL));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_set_probe_selector_analysis(plan, &analysis));

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.has_probe_selector_analysis);
    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_AUTO, report.probe_selector_analysis.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED,
              report.probe_selector_analysis.status);
    ASSERT_EQ(1u, report.probe_selector_analysis.candidate_count);
    ASSERT_EQ(1667u, report.probe_selector_analysis.candidates[0].node_id);
    ASSERT_TRUE(report.probe_selector_analysis.safe_insertion.selected);

    nmo_edit_report_dispose(&report);
    nmo_probe_analysis_dispose(&analysis);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_rolls_back_failed_plan) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_string_parameter(&fixture, "old", &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, param_id, NULL, "new value", NULL));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, 999999u, NULL, "bad", NULL));

    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, report.operations[1].status);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, "old", 4));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_rolls_back_created_handle_chain_failure) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_STRING,
                  "Created Before Failure"));
    nmo_edit_handle_ref_t missing_ref =
        edit_plan_test_handle_ref(0u, "missing-parameter-handle");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan,
                  0u,
                  &missing_ref,
                  "bad",
                  NULL));

    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, report.operations[1].status);
    ASSERT_STR_EQ("handle_not_found", report.operations[1].diagnostic_code);
    ASSERT_TRUE(nmo_object_repository_find_by_id(
                    fixture.repo, report.operations[0].result_id) == NULL);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_dry_run_reports_without_persisting) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_string_parameter(&fixture, "old", &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, param_id, NULL, "new value", NULL));
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(fixture.workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, "old", 4));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_writes_manager_parameter_values) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_manager_parameter(&fixture, &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, param_id, NULL, "12345678-9ABCDEF0:77", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_TRUE(nmo_guid_equals(
        nmo_guid_parse("12345678-9ABCDEF0"), state->manager_guid));
    ASSERT_EQ(77u, state->manager_value);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(param_id, report.changed_objects[0].id);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_writes_display_formatted_manager_parameter_values) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_manager_parameter(&fixture, &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, param_id, NULL, "manager{12345678-9ABCDEF0} = 99", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(nmo_guid_equals(
        nmo_guid_parse("12345678-9ABCDEF0"), state->manager_guid));
    ASSERT_EQ(99u, state->manager_value);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_resizes_typed_parameter_values_when_requested) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_int_parameter_with_buffer_size(&fixture, 1u, &param_id, &state);

    nmo_parameter_write_options_t options = {
        .resize = true,
    };
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    int32_t value = 0;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, param_id, NULL, "123", &options));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(state->buffer_data.count >= sizeof(value));
    memcpy(&value, state->buffer_data.data, sizeof(value));
    ASSERT_EQ(123, value);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_rejects_truncated_typed_parameter_values_without_resize) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_int_parameter_with_buffer_size(&fixture, 1u, &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, param_id, NULL, "123", NULL));

    ASSERT_EQ(NMO_ERR_OUT_OF_BOUNDS,
              nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_EQ(1u, state->buffer_data.count);
    ASSERT_EQ(0, ((uint8_t *)state->buffer_data.data)[0]);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_writes_object_reference_display_values) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t target_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR,
                          "Referenced Object", &target_id);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_object_reference_parameter(&fixture, 0u, &param_id, &state);

    char object_ref[64];
    snprintf(object_ref, sizeof(object_ref), "object:%u", target_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, param_id, NULL, object_ref, NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(target_id, nmo_parameter_object_id(state));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);

    char hash_ref[64];
    snprintf(hash_ref, sizeof(hash_ref), "#%u", target_id);
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, param_id, NULL, hash_ref, NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(target_id, nmo_parameter_object_id(state));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_adds_node_with_created_object_report) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Plan 2D Text"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);
    ASSERT_TRUE(report.operations[0].handle_count > 1u);
    ASSERT_STR_EQ("node", report.operations[0].handles[0].name);
    ASSERT_EQ(report.operations[0].result_id, report.operations[0].handles[0].id);
    ASSERT_TRUE(report.created_object_count > 1u);
    ASSERT_EQ(report.operations[0].result_id, report.created_objects[0].id);

    const nmo_object_id_t created_node_id = report.operations[0].result_id;
    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, created_node_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_obj);
    ASSERT_NOT_NULL(node_state);
    ASSERT_EQ(NMO_CID_2DENTITY, node_state->compatible_class_id);
    ASSERT_TRUE(node_state->target_parameter_id != 0u);
    {
        bool found_target = false;
        for (size_t i = 0; i < report.created_object_count; ++i) {
            if (report.created_objects[i].id == node_state->target_parameter_id) {
                found_target = true;
            }
        }
        ASSERT_TRUE(found_target);
    }
    {
        bool found_target_handle = false;
        bool found_input_handle = false;
        bool found_output_handle = false;
        for (size_t i = 0; i < report.operations[0].handle_count; ++i) {
            if (strcmp(report.operations[0].handles[i].name, "target") == 0 &&
                report.operations[0].handles[i].id == node_state->target_parameter_id) {
                found_target_handle = true;
            }
            if (strcmp(report.operations[0].handles[i].name, "input:On") == 0) {
                found_input_handle = true;
            }
            if (strcmp(report.operations[0].handles[i].name, "output:Exit On") == 0) {
                found_output_handle = true;
            }
        }
        ASSERT_TRUE(found_target_handle);
        ASSERT_TRUE(found_input_handle);
        ASSERT_TRUE(found_output_handle);
    }

    const nmo_object_id_t target_parameter_id = node_state->target_parameter_id;
    const nmo_object_id_t first_input_id =
        node_state->inputs.count > 0u
            ? nmo_behavior_ref_array_get_id(&node_state->inputs, 0)
            : 0u;
    ASSERT_TRUE(first_input_id != 0u);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  created_node_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_NODE, report.operations[0].kind);

    bool reported_target_deleted = false;
    bool reported_input_deleted = false;
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == target_parameter_id) {
            reported_target_deleted = true;
        } else if (report.deleted_objects[i].id == first_input_id) {
            reported_input_deleted = true;
        }
    }
    ASSERT_TRUE(reported_target_deleted);
    ASSERT_TRUE(reported_input_deleted);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_building_block_defaults) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Defaulted 2D Text"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);

    nmo_object_id_t caret_id =
        find_named_parameter_in_ids(fixture.repo, &node_state->in_parameters, "Caret Size");
    nmo_object_t *caret_obj =
        nmo_object_repository_find_by_id(fixture.repo, caret_id);
    nmo_parameterin_state_t *caret_state = caret_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(caret_obj)
        : NULL;
    ASSERT_NOT_NULL(caret_state);
    ASSERT_TRUE(nmo_parameterin_source_id(caret_state) != 0u);

    nmo_object_t *source_obj =
        nmo_object_repository_find_by_id(
            fixture.repo, nmo_parameterin_source_id(caret_state));
    nmo_parameter_state_t *source_state = source_obj
        ? nmo_parameter_get_mutable_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(CKPARAM_MODE_BUFFER, source_state->mode);
    ASSERT_TRUE(source_state->buffer_data.count >= sizeof(float));

    float caret_value = 0.0f;
    memcpy(&caret_value, source_state->buffer_data.data, sizeof(caret_value));
    ASSERT_TRUE(fabsf(caret_value - 10.0f) < 0.0001f);

    bool reported_created_source = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id ==
            nmo_parameterin_source_id(caret_state)) {
            reported_created_source = true;
        }
    }
    ASSERT_TRUE(reported_created_source);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_targetable_beobject_target) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("18655B3F-68291DC3"),
                  "Plan Output To Console"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);
    ASSERT_EQ(NMO_CID_BEOBJECT, node_state->compatible_class_id);
    ASSERT_TRUE((node_state->flags & CKBEHAVIOR_TARGETABLE) != 0u);
    ASSERT_TRUE(node_state->target_parameter_id != 0u);

    nmo_object_t *target_obj =
        nmo_object_repository_find_by_id(fixture.repo, node_state->target_parameter_id);
    nmo_parameterin_state_t *target_state = target_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(target_obj)
        : NULL;
    ASSERT_NOT_NULL(target_state);
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_BEOBJECT, target_state->type_guid));

    bool reported_target = false;
    bool handle_target = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id == node_state->target_parameter_id) {
            reported_target = true;
        }
    }
    for (size_t i = 0; i < report.operations[0].handle_count; ++i) {
        if (strcmp(report.operations[0].handles[i].name, "target") == 0 &&
            report.operations[0].handles[i].id == node_state->target_parameter_id) {
            handle_target = true;
        }
    }
    ASSERT_TRUE(reported_target);
    ASSERT_TRUE(handle_target);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_common_building_block_prototypes) {
    static const struct {
        const char *label;
        nmo_guid_t guid;
    } cases[] = {
        {"2D Text", NMO_GUID_INIT(0x055B29FEu, 0x662D5CA0u)},
        {"Output To Console", NMO_GUID_INIT(0x18655B3Fu, 0x68291DC3u)},
        {"Send Message", NMO_GUID_INIT(0xA20E8D5Bu, 0xDF002150u)},
        {"Wait Message", NMO_GUID_INIT(0x4587FFEEu, 0x4587FFDDu)},
        {"Add Row", NMO_GUID_INIT(0x1C7E5DC6u, 0x3F6423C2u)},
        {"Set Cell", NMO_GUID_INIT(0x30ED1C6Du, 0x4A3B7067u)},
        {"Get Cell", NMO_GUID_INIT(0x33B99F51u, 0x07D95C45u)},
        {"Remove Row", NMO_GUID_INIT(0x1FA57136u, 0x14310857u)},
        {"Sort Rows", NMO_GUID_INIT(0x6F623E68u, 0x62BB5A98u)},
        {"Get Current Scene", NMO_GUID_INIT(0x00DC125Fu, 0x592B00A8u)},
        {"Show", NMO_GUID_INIT(0xA85A213Au, 0xEF78D52Au)},
    };

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        edit_plan_fixture_t fixture;
        edit_plan_fixture_init(&fixture);

        const nmo_behavior_proto_t *proto = nmo_behavior_registry_find(
            nmo_context_get_bb_registry(fixture.ctx), cases[c].guid);
        ASSERT_NOT_NULL(proto);
        ASSERT_STR_EQ(cases[c].label, proto->name);

        nmo_object_id_t root_id = 0;
        create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
        const char *const messages[] = {"", "Start", "OnClick"};
        install_message_manager_or_fail(
            fixture.session,
            messages,
            (uint32_t)(sizeof(messages) / sizeof(messages[0])));

        nmo_edit_plan_t *plan = NULL;
        nmo_edit_report_t report;
        ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
        ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
        ASSERT_EQ(NMO_OK,
                  nmo_edit_plan_add_node(plan, root_id, cases[c].guid, proto->name));

        ASSERT_EQ(NMO_OK,
                  nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
        ASSERT_TRUE(report.ok);
        ASSERT_EQ(1u, report.operation_count);
        ASSERT_TRUE(report.operations[0].result_id != 0u);
        ASSERT_TRUE(report_contains_object_id(
            report.created_objects,
            report.created_object_count,
            report.operations[0].result_id));

        nmo_object_t *node_obj = nmo_object_repository_find_by_id(
            fixture.repo, report.operations[0].result_id);
        nmo_behavior_state_t *node_state = node_obj
            ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
            : NULL;
        ASSERT_NOT_NULL(node_state);
        ASSERT_TRUE((node_state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0u);
        ASSERT_TRUE((node_state->flags & CKBEHAVIOR_USEFUNCTION) != 0u);
        ASSERT_TRUE(nmo_guid_equals(proto->guid, node_state->block_guid));
        ASSERT_EQ(proto->version != 0u ? proto->version : 65536u,
                  node_state->block_version);
        ASSERT_EQ(proto->compatible_class_id, node_state->compatible_class_id);

        assert_named_ids_match_strings(
            fixture.repo, &node_state->inputs, proto->inputs, proto->input_count);
        assert_named_ids_match_strings(
            fixture.repo, &node_state->outputs, proto->outputs, proto->output_count);
        assert_param_ids_match_proto(
            &fixture,
            &node_state->in_parameters,
            proto->input_params,
            proto->input_param_count,
            NMO_CID_PARAMETERIN,
            false,
            &report);
        assert_param_ids_match_proto(
            &fixture,
            &node_state->out_parameters,
            proto->output_params,
            proto->output_param_count,
            NMO_CID_PARAMETEROUT,
            false,
            &report);

        ASSERT_EQ((size_t)(proto->local_param_count + proto->setting_count),
                  node_state->local_parameters.count);
        nmo_array_t local_ids = {0};
        nmo_array_t setting_ids = {0};
        local_ids = node_state->local_parameters;
        local_ids.count = proto->local_param_count;
        setting_ids = node_state->local_parameters;
        setting_ids.data = (void *)(
            NMO_ARRAY_DATA(nmo_behavior_ref_t, &node_state->local_parameters) +
            proto->local_param_count);
        setting_ids.count = proto->setting_count;
        if (proto->local_param_count > 0u) {
            assert_param_ids_match_proto(
                &fixture,
                &local_ids,
                proto->local_params,
                proto->local_param_count,
                NMO_CID_PARAMETERLOCAL,
                false,
                &report);
        }
        if (proto->setting_count > 0u) {
            assert_param_ids_match_proto(
                &fixture,
                &setting_ids,
                proto->settings,
                proto->setting_count,
                NMO_CID_PARAMETERLOCAL,
                true,
                &report);
        }

        if ((proto->behavior_flags & CKBEHAVIOR_TARGETABLE) != 0u) {
            ASSERT_TRUE(node_state->target_parameter_id != 0u);
            ASSERT_TRUE(report_contains_object_id(
                report.created_objects,
                report.created_object_count,
                node_state->target_parameter_id));
        } else {
            ASSERT_EQ(0u, node_state->target_parameter_id);
        }

        nmo_edit_report_dispose(&report);
        nmo_edit_plan_destroy(plan);
        edit_plan_fixture_dispose(&fixture);
    }
}

TEST(edit_plan, executor_matches_authored_2d_text_golden_shape) {
    nmo_context_t *golden_ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(golden_ctx);
    nmo_session_t *golden_session =
        nmo_session_load(golden_ctx, NMO_TEST_DATA_FILE("Ballance/2D Text.nmo"));
    ASSERT_NOT_NULL(golden_session);
    nmo_object_repository_t *golden_repo =
        nmo_session_get_repository(golden_session);
    ASSERT_NOT_NULL(golden_repo);

    const nmo_guid_t text_guid = NMO_GUID_INIT(0x055B29FEu, 0x662D5CA0u);
    nmo_behavior_state_t *golden_state =
        find_first_behavior_by_block_guid(golden_repo, text_guid, NULL);
    ASSERT_NOT_NULL(golden_state);

    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  text_guid,
                  "Golden 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    nmo_object_t *actual_obj = nmo_object_repository_find_by_id(
        fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *actual_state = actual_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(actual_obj)
        : NULL;
    ASSERT_NOT_NULL(actual_state);

    const uint32_t behavior_flag_mask =
        CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION |
        CKBEHAVIOR_TARGETABLE;
    ASSERT_EQ(golden_state->flags & behavior_flag_mask,
              actual_state->flags & behavior_flag_mask);
    ASSERT_TRUE(nmo_guid_equals(golden_state->block_guid,
                                actual_state->block_guid));
    ASSERT_EQ(golden_state->block_version, actual_state->block_version);
    ASSERT_EQ(golden_state->compatible_class_id,
              actual_state->compatible_class_id);
    ASSERT_EQ(golden_state->target_parameter_id != 0u ? 1u : 0u,
              actual_state->target_parameter_id != 0u ? 1u : 0u);

    assert_named_object_arrays_match(
        golden_repo, &golden_state->inputs,
        fixture.repo, &actual_state->inputs);
    assert_named_object_arrays_match(
        golden_repo, &golden_state->outputs,
        fixture.repo, &actual_state->outputs);
    assert_parameter_shape_arrays_match(
        golden_repo, &golden_state->in_parameters,
        fixture.repo, &actual_state->in_parameters);
    assert_parameter_shape_arrays_match(
        golden_repo, &golden_state->out_parameters,
        fixture.repo, &actual_state->out_parameters);
    assert_parameter_shape_arrays_match(
        golden_repo, &golden_state->local_parameters,
        fixture.repo, &actual_state->local_parameters);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
    nmo_session_close_with_context(golden_ctx, golden_session);
    nmo_context_release(golden_ctx);
}

TEST(edit_plan, executor_resolves_symbolic_message_default_from_manager_data) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    const char *const messages[] = {"", "Start", "OnClick"};
    install_message_manager_or_fail(
        fixture.session,
        messages,
        (uint32_t)(sizeof(messages) / sizeof(messages[0])));
    uint32_t onclick_value = 0u;
    ASSERT_TRUE(find_message_manager_value(
        fixture.session, "OnClick", &onclick_value));
    ASSERT_TRUE(onclick_value != 0u);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    static const struct {
        const char *guid;
        const char *name;
    } cases[] = {
        {"A20E8D5B-DF002150", "Send Message Probe"},
        {"4587FFEE-4587FFDD", "Wait Message Probe"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        nmo_edit_plan_t *plan = NULL;
        nmo_edit_report_t report;
        ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
        ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
        ASSERT_EQ(NMO_OK,
                  nmo_edit_plan_add_node(
                      plan,
                      root_id,
                      nmo_guid_parse(cases[i].guid),
                      cases[i].name));

        ASSERT_EQ(NMO_OK,
                  nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
        ASSERT_TRUE(report.ok);

        nmo_object_repository_t *repo = fixture.repo;
        ASSERT_NOT_NULL(repo);
        nmo_object_t *node_obj =
            nmo_object_repository_find_by_id(repo, report.operations[0].result_id);
        nmo_behavior_state_t *node_state = node_obj
            ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
            : NULL;
        ASSERT_NOT_NULL(node_state);
        nmo_object_id_t message_input_id =
            find_named_parameter_in_ids(repo, &node_state->in_parameters, "Message");
        ASSERT_TRUE(message_input_id != 0u);
        nmo_object_t *message_input_obj =
            nmo_object_repository_find_by_id(repo, message_input_id);
        nmo_parameterin_state_t *message_input =
            message_input_obj
                ? (nmo_parameterin_state_t *)nmo_object_get_state(message_input_obj)
                : NULL;
        ASSERT_NOT_NULL(message_input);
        ASSERT_TRUE(nmo_parameterin_source_id(message_input) != 0u);

        nmo_object_t *source_obj =
            nmo_object_repository_find_by_id(
                repo, nmo_parameterin_source_id(message_input));
        nmo_parameter_state_t *source_state = source_obj
            ? nmo_parameter_get_mutable_state(source_obj)
            : NULL;
        ASSERT_NOT_NULL(source_state);
        ASSERT_EQ(CKPARAM_MODE_MANAGER, source_state->mode);
        ASSERT_TRUE(nmo_guid_equals(NMO_MANAGER_GUID_MESSAGE,
                                    source_state->manager_guid));
        ASSERT_EQ(onclick_value, source_state->manager_value);

        nmo_edit_report_dispose(&report);
        nmo_edit_plan_destroy(plan);
    }
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_creates_missing_symbolic_message_default_when_opted_in) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "root", &root_id);

    nmo_edit_plan_t *strict_plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&strict_plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  strict_plan,
                  root_id,
                  nmo_guid_parse("A20E8D5B-DF002150"),
                  "Strict Send Message"));

    nmo_edit_report_t strict_report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&strict_report));
    ASSERT_NE(NMO_OK,
              nmo_edit_executor_execute(
                  fixture.workspace, strict_plan,
                  &(nmo_edit_executor_options_t){0}, &strict_report));
    ASSERT_EQ(0u, strict_report.operations[0].result_id);
    uint32_t missing_value = 0;
    ASSERT_FALSE(find_message_manager_value(
        fixture.session, "OnClick", &missing_value));
    nmo_edit_report_dispose(&strict_report);
    nmo_edit_plan_destroy(strict_plan);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node_ex(
                  plan,
                  root_id,
                  nmo_guid_parse("A20E8D5B-DF002150"),
                  "Opt In Send Message",
                  &(nmo_add_node_options_t){
                      .manager_entry.policy =
                          NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
                  }));

    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(
                  fixture.workspace, plan,
                  &(nmo_edit_executor_options_t){0}, &report));

    uint32_t onclick_value = UINT32_MAX;
    ASSERT_TRUE(find_message_manager_value(
        fixture.session, "OnClick", &onclick_value));
    ASSERT_EQ(0u, onclick_value);
    ASSERT_TRUE(report_contains_object_id(
        report.changed_objects,
        report.changed_object_count,
        NMO_EDIT_MANAGER_ENTRY_IMPACT_ID));
    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_resolves_parameter_value_from_prior_handle) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Probe 2D Text"));
    nmo_edit_handle_ref_t alignment_ref =
        edit_plan_test_handle_ref(0u, "input_param_source:Alignment");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, 0u, &alignment_ref, "Top-Left", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_SET_PARAMETER_VALUE, report.operations[1].kind);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);
    ASSERT_EQ(report.operations[1].result_id, report.changed_objects[1].id);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_input_source_for_handle_value) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Probe 2D Text"));
    nmo_edit_handle_ref_t text_ref =
        edit_plan_test_handle_ref(0u, "input_param:Text");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, 0u, &text_ref, "loading trace", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);
    bool reported_created_source = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id == report.operations[1].result_id) {
            reported_created_source = true;
        }
    }
    ASSERT_TRUE(reported_created_source);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);
    nmo_object_id_t text_in_id =
        find_named_parameter_in_ids(fixture.repo, &node_state->in_parameters, "Text");
    nmo_object_t *text_in_obj =
        nmo_object_repository_find_by_id(fixture.repo, text_in_id);
    nmo_parameterin_state_t *text_in_state = text_in_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(text_in_obj)
        : NULL;
    ASSERT_NOT_NULL(text_in_state);
    ASSERT_EQ(report.operations[1].result_id,
              nmo_parameterin_source_id(text_in_state));

    nmo_object_t *source_obj =
        nmo_object_repository_find_by_id(
            fixture.repo, nmo_parameterin_source_id(text_in_state));
    nmo_parameter_state_t *source_state = source_obj
        ? nmo_parameter_get_mutable_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(CKPARAM_MODE_BUFFER, source_state->mode);
    ASSERT_TRUE(source_state->buffer_data.count >= strlen("loading trace") + 1u);
    ASSERT_EQ(0, memcmp(source_state->buffer_data.data,
                        "loading trace",
                        strlen("loading trace") + 1u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_input_source_for_handle_bytes) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    static const uint8_t trace_bytes[] = {
        'r', 'a', 'w', ' ', 't', 'r', 'a', 'c', 'e', '\0',
    };
    const nmo_parameter_write_options_t options = {
        .resize = true,
    };
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Probe 2D Text"));
    nmo_edit_handle_ref_t text_ref =
        edit_plan_test_handle_ref(0u, "input_param:Text");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_bytes(
                  plan,
                  0u,
                  &text_ref,
                  trace_bytes,
                  sizeof(trace_bytes),
                  &options));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);
    nmo_object_id_t text_in_id =
        find_named_parameter_in_ids(fixture.repo, &node_state->in_parameters, "Text");
    nmo_object_t *text_in_obj =
        nmo_object_repository_find_by_id(fixture.repo, text_in_id);
    nmo_parameterin_state_t *text_in_state = text_in_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(text_in_obj)
        : NULL;
    ASSERT_NOT_NULL(text_in_state);
    ASSERT_EQ(report.operations[1].result_id,
              nmo_parameterin_source_id(text_in_state));

    nmo_object_t *source_obj =
        nmo_object_repository_find_by_id(
            fixture.repo, nmo_parameterin_source_id(text_in_state));
    nmo_parameter_state_t *source_state = source_obj
        ? nmo_parameter_get_mutable_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(CKPARAM_MODE_BUFFER, source_state->mode);
    ASSERT_TRUE(source_state->buffer_data.count >= sizeof(trace_bytes));
    ASSERT_EQ(0,
              memcmp(source_state->buffer_data.data,
                     trace_bytes,
                     sizeof(trace_bytes)));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_connects_parameter_to_prior_node_handle) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t source_parameter_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;

    nmo_script_edit_tx_t *seed_tx = NULL;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed source", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_STRING,
                  "Trace Source",
                  &source_parameter_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("18655B3F-68291DC3"),
                  "Parameter Logger"));
    nmo_edit_handle_ref_t target_ref =
        edit_plan_test_handle_ref(0u, "input_param:String");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_connect_parameter(
                  plan, source_parameter_id, 0u, &target_ref));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_CONNECT_PARAMETER, report.operations[1].kind);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);

    nmo_object_t *target_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[1].result_id);
    nmo_parameterin_state_t *target_state = target_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(target_obj)
        : NULL;
    ASSERT_NOT_NULL(target_state);
    ASSERT_EQ(source_parameter_id,
              nmo_parameterin_source_id(target_state));

    bool reported_source_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == source_parameter_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_source") == 0) {
            reported_source_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_source_endpoint);

    const nmo_object_id_t target_parameter_id = report.operations[1].result_id;

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_disconnect_parameter(
                  plan,
                  target_parameter_id));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_DISCONNECT_PARAMETER, report.operations[0].kind);
    reported_source_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == source_parameter_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_source") == 0) {
            reported_source_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_source_endpoint);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_resolves_behavior_link_io_handles) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  root_id,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Enter"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  child_id,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Child In"));
    nmo_edit_handle_ref_t from_ref = edit_plan_test_handle_ref(0u, "io");
    nmo_edit_handle_ref_t to_ref = edit_plan_test_handle_ref(1u, "io");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(
                  plan,
                  root_id,
                  0u,
                  &from_ref,
                  0u,
                  &to_ref,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(3u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_ADD_BEHAVIOR_LINK, report.operations[2].kind);
    ASSERT_EQ(NMO_OK, report.operations[2].status);
    ASSERT_TRUE(report.operations[2].result_id != 0u);

    bool reported_created_link = false;
    bool reported_from_endpoint = false;
    bool reported_to_endpoint = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id == report.operations[2].result_id) {
            reported_created_link = true;
        }
    }
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "control_link_endpoint") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == report.operations[0].result_id) {
            reported_from_endpoint = true;
        } else if (report.changed_objects[i].id == report.operations[1].result_id) {
            reported_to_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_created_link);
    ASSERT_TRUE(reported_from_endpoint);
    ASSERT_TRUE(reported_to_endpoint);

    const nmo_object_id_t link_id = report.operations[2].result_id;
    const nmo_object_id_t from_io_id = report.operations[0].result_id;
    const nmo_object_id_t to_io_id = report.operations[1].result_id;

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_behavior_link(
                  plan,
                  link_id,
                  from_io_id,
                  to_io_id));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK, report.operations[0].kind);
    reported_from_endpoint = false;
    reported_to_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "control_link_endpoint") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == from_io_id) {
            reported_from_endpoint = true;
        } else if (report.changed_objects[i].id == to_io_id) {
            reported_to_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_from_endpoint);
    ASSERT_TRUE(reported_to_endpoint);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_behavior_link(
                  plan,
                  root_id,
                  link_id));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK, report.operations[0].kind);
    reported_from_endpoint = false;
    reported_to_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "control_link_endpoint") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == from_io_id) {
            reported_from_endpoint = true;
        } else if (report.changed_objects[i].id == to_io_id) {
            reported_to_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_from_endpoint);
    ASSERT_TRUE(reported_to_endpoint);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(
                  plan,
                  root_id,
                  from_io_id,
                  NULL,
                  to_io_id,
                  NULL,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    const nmo_object_id_t detach_link_id = report.operations[0].result_id;

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_io(
                  plan,
                  from_io_id,
                  true));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_IO, report.operations[0].kind);
    bool reported_detached_link = false;
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == detach_link_id &&
            report.deleted_objects[i].role != NULL &&
            strcmp(report.deleted_objects[i].role, "detached_control_link") == 0) {
            reported_detached_link = true;
        }
    }
    ASSERT_TRUE(reported_detached_link);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_remove_node_detached_link_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  root_id,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Enter"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  child_id,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Child In"));
    nmo_edit_handle_ref_t from_ref = edit_plan_test_handle_ref(0u, "io");
    nmo_edit_handle_ref_t to_ref = edit_plan_test_handle_ref(1u, "io");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(
                  plan,
                  root_id,
                  0u,
                  &from_ref,
                  0u,
                  &to_ref,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(3u, report.operation_count);
    const nmo_object_id_t linked_child_io_id = report.operations[1].result_id;
    const nmo_object_id_t link_id = report.operations[2].result_id;

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_NODE, report.operations[0].kind);

    bool reported_detached_link = false;
    bool reported_child_io = false;
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == link_id &&
            report.deleted_objects[i].role != NULL &&
            strcmp(report.deleted_objects[i].role, "detached_control_link") == 0) {
            reported_detached_link = true;
        } else if (report.deleted_objects[i].id == linked_child_io_id) {
            reported_child_io = true;
        }
    }
    ASSERT_TRUE(reported_detached_link);
    ASSERT_TRUE(reported_child_io);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_remove_node_parameter_edge_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t source_id = 0;
    nmo_object_id_t target_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed param edge", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Source", &source_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, child_id, NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_INT, "Target", &target_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_connect_parameter(seed_tx, source_id, target_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_NODE, report.operations[0].kind);

    bool reported_source = false;
    bool reported_target_deleted = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == source_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_source") == 0) {
            reported_source = true;
        }
    }
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == target_id) {
            reported_target_deleted = true;
        }
    }
    ASSERT_TRUE(reported_source);
    ASSERT_TRUE(reported_target_deleted);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_detaches_removed_node_parameter_edges) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t source_id = 0;
    nmo_object_id_t target_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed param edge", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_OUT,
                  CKPGUID_INT, "Source", &source_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, child_id, NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_INT, "Target", &target_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_connect_parameter(seed_tx, source_id, target_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));
    nmo_object_t *source_obj =
        nmo_object_repository_find_by_id(fixture.repo, source_id);
    nmo_parameterout_state_t *source_state = source_obj
        ? (nmo_parameterout_state_t *)nmo_object_get_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(1u, source_state->destination_count);
    ASSERT_EQ(target_id, source_state->destination_ids[0]);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    source_obj = nmo_object_repository_find_by_id(fixture.repo, source_id);
    source_state = source_obj
        ? (nmo_parameterout_state_t *)nmo_object_get_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(0u, source_state->destination_count);
    ASSERT_NULL(source_state->destination_ids);
    ASSERT_NULL(nmo_object_repository_find_by_id(fixture.repo, target_id));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_nested_removed_node_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    nmo_object_id_t grandchild_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Grandchild", &grandchild_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_object_t *grandchild_obj =
        nmo_object_repository_find_by_id(fixture.repo, grandchild_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    nmo_behavior_state_t *grandchild_state = grandchild_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(grandchild_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(grandchild_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&child_state->sub_behaviors, grandchild_id, NULL));
    root_state->flags |= 0x00000002u;
    child_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    grandchild_state->owner_id = child_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t grandchild_param_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed nested param", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Nested", &grandchild_param_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    bool reported_grandchild = false;
    bool reported_grandchild_param = false;
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == grandchild_id) {
            reported_grandchild = true;
        }
        if (report.deleted_objects[i].id == grandchild_param_id) {
            reported_grandchild_param = true;
        }
    }
    ASSERT_TRUE(reported_grandchild);
    ASSERT_TRUE(reported_grandchild_param);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_nested_removed_node_parameter_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    nmo_object_id_t grandchild_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Grandchild", &grandchild_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_object_t *grandchild_obj =
        nmo_object_repository_find_by_id(fixture.repo, grandchild_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    nmo_behavior_state_t *grandchild_state = grandchild_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(grandchild_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(grandchild_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&child_state->sub_behaviors, grandchild_id, NULL));
    root_state->flags |= 0x00000002u;
    child_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    grandchild_state->owner_id = child_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t source_id = 0;
    nmo_object_id_t target_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed nested edge", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, child_id, NMO_SCRIPT_EDIT_PARAM_OUT,
                  CKPGUID_INT, "Source", &source_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_INT, "Target", &target_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_connect_parameter(seed_tx, source_id, target_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    bool reported_target = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == target_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_target") == 0) {
            reported_target = true;
        }
    }
    ASSERT_TRUE(reported_target);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_removed_node_operation_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t in1_id = 0;
    nmo_object_id_t in2_id = 0;
    nmo_object_id_t out_id = 0;
    nmo_object_id_t operation_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed child op", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, child_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "A", &in1_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, child_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "B", &in2_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, child_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Out", &out_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_operation(
                  seed_tx,
                  child_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  in1_id,
                  in2_id,
                  out_id,
                  &operation_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_NULL(nmo_object_repository_find_by_id(fixture.repo, operation_id));

    bool reported_operation = false;
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == operation_id &&
            report.deleted_objects[i].role != NULL &&
            strcmp(report.deleted_objects[i].role, "owned_operation") == 0) {
            reported_operation = true;
        }
    }
    ASSERT_TRUE(reported_operation);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_removed_node_control_link_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    nmo_object_id_t grandchild_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Grandchild", &grandchild_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_object_t *grandchild_obj =
        nmo_object_repository_find_by_id(fixture.repo, grandchild_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    nmo_behavior_state_t *grandchild_state = grandchild_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(grandchild_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(grandchild_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&child_state->sub_behaviors, grandchild_id, NULL));
    root_state->flags |= 0x00000002u;
    child_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    grandchild_state->owner_id = child_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t out_io_id = 0;
    nmo_object_id_t in_io_id = 0;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed nested link", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_io(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_IO_OUTPUT, "Out", &out_io_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_io(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_IO_INPUT, "In", &in_io_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_behavior_link(
                  seed_tx, child_id, out_io_id, in_io_id, 0u, &link_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_NULL(nmo_object_repository_find_by_id(fixture.repo, link_id));

    bool reported_link = false;
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == link_id &&
            report.deleted_objects[i].role != NULL &&
            strcmp(report.deleted_objects[i].role, "owned_link") == 0) {
            reported_link = true;
        }
    }
    ASSERT_TRUE(reported_link);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_deletes_nested_removed_node_operations) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    nmo_object_id_t grandchild_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Grandchild", &grandchild_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_object_t *grandchild_obj =
        nmo_object_repository_find_by_id(fixture.repo, grandchild_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    nmo_behavior_state_t *grandchild_state = grandchild_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(grandchild_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(grandchild_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&child_state->sub_behaviors, grandchild_id, NULL));
    root_state->flags |= 0x00000002u;
    child_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    grandchild_state->owner_id = child_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t in1_id = 0;
    nmo_object_id_t in2_id = 0;
    nmo_object_id_t out_id = 0;
    nmo_object_id_t operation_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed nested op", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "A", &in1_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "B", &in2_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Out", &out_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_operation(
                  seed_tx,
                  grandchild_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  in1_id,
                  in2_id,
                  out_id,
                  &operation_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_NULL(nmo_object_repository_find_by_id(fixture.repo, operation_id));

    bool reported_operation = false;
    for (size_t i = 0; i < report.deleted_object_count; ++i) {
        if (report.deleted_objects[i].id == operation_id &&
            report.deleted_objects[i].role != NULL &&
            strcmp(report.deleted_objects[i].role, "owned_operation") == 0) {
            reported_operation = true;
        }
    }
    ASSERT_TRUE(reported_operation);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_detaches_nested_removed_node_control_links) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    nmo_object_id_t grandchild_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Grandchild", &grandchild_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_object_t *grandchild_obj =
        nmo_object_repository_find_by_id(fixture.repo, grandchild_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    nmo_behavior_state_t *grandchild_state = grandchild_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(grandchild_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(grandchild_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&root_state->sub_behaviors, child_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&child_state->sub_behaviors, grandchild_id, NULL));
    root_state->flags |= 0x00000002u;
    child_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    grandchild_state->owner_id = child_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t root_io_id = 0;
    nmo_object_id_t grandchild_io_id = 0;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed nested external link", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_io(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_IO_INPUT, "Enter", &root_io_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_io(
                  seed_tx, grandchild_id, NMO_SCRIPT_EDIT_IO_INPUT, "Nested In", &grandchild_io_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_workspace_edit_t *raw_edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "seed external nested link", &raw_edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(
                  raw_edit,
                  root_id,
                  root_io_id,
                  grandchild_io_id,
                  0,
                  &link_id));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(raw_edit));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(
                  plan,
                  root_id,
                  child_id,
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    root_obj = nmo_object_repository_find_by_id(fixture.repo, root_id);
    root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(root_state);
    for (size_t i = 0; i < root_state->sub_behavior_links.count; ++i) {
        ASSERT_NE(link_id, nmo_behavior_ref_array_get_id(
            &root_state->sub_behavior_links, i));
    }

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);

    nmo_script_edit_tx_t *follow_tx = NULL;
    nmo_object_id_t follow_in1 = 0;
    nmo_object_id_t follow_in2 = 0;
    nmo_object_id_t follow_out = 0;
    nmo_object_id_t follow_operation = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "followup create", &follow_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  follow_tx,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT,
                  "After Delete A",
                  &follow_in1));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  follow_tx,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT,
                  "After Delete B",
                  &follow_in2));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  follow_tx,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT,
                  "After Delete Out",
                  &follow_out));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_operation(
                  follow_tx,
                  root_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  follow_in1,
                  follow_in2,
                  follow_out,
                  &follow_operation));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(follow_tx));
    ASSERT_TRUE(follow_operation != 0u);

    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_rewire_operation_slot_parameter_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    nmo_object_id_t owner_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t in1_id = 0;
    nmo_object_id_t in2_id = 0;
    nmo_object_id_t out_id = 0;
    nmo_object_id_t operation_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed op", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "A", &in1_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "B", &in2_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Out", &out_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_operation(
                  seed_tx,
                  root_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  in1_id,
                  in2_id,
                  out_id,
                  &operation_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT,
                  "Replacement A"));
    nmo_edit_handle_ref_t in1_ref =
        edit_plan_test_handle_ref(0u, "parameter");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_operation(
                  plan,
                  operation_id,
                  NMO_SCRIPT_EDIT_OP_SLOT_IN1,
                  0u,
                  &in1_ref,
                  0u,
                  NULL,
                  0u,
                  NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);

    bool reported_slot_parameter = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == report.operations[0].result_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "operation_slot_parameter") == 0) {
            reported_slot_parameter = true;
        }
    }
    ASSERT_TRUE(reported_slot_parameter);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_add_operation_slot_parameter_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "A"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "B"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Out"));
    nmo_edit_handle_ref_t in1_ref =
        edit_plan_test_handle_ref(0u, "parameter");
    nmo_edit_handle_ref_t in2_ref =
        edit_plan_test_handle_ref(1u, "parameter");
    nmo_edit_handle_ref_t out_ref =
        edit_plan_test_handle_ref(2u, "parameter");
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_operation(
                  plan,
                  root_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  0u,
                  &in1_ref,
                  0u,
                  &in2_ref,
                  0u,
                  &out_ref));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[3].status);

    bool reported_in1 = false;
    bool reported_in2 = false;
    bool reported_out = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "operation_slot_parameter") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == report.operations[0].result_id) {
            reported_in1 = true;
        } else if (report.changed_objects[i].id == report.operations[1].result_id) {
            reported_in2 = true;
        } else if (report.changed_objects[i].id == report.operations[2].result_id) {
            reported_out = true;
        }
    }
    ASSERT_TRUE(reported_in1);
    ASSERT_TRUE(reported_in2);
    ASSERT_TRUE(reported_out);

    const nmo_object_id_t in1_id = report.operations[0].result_id;
    const nmo_object_id_t in2_id = report.operations[1].result_id;
    const nmo_object_id_t out_id = report.operations[2].result_id;
    const nmo_object_id_t operation_id = report.operations[3].result_id;

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_operation(plan, operation_id));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_OPERATION, report.operations[0].kind);
    reported_in1 = false;
    reported_in2 = false;
    reported_out = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "operation_slot_parameter") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == in1_id) {
            reported_in1 = true;
        } else if (report.changed_objects[i].id == in2_id) {
            reported_in2 = true;
        } else if (report.changed_objects[i].id == out_id) {
            reported_out = true;
        }
    }
    ASSERT_TRUE(reported_in1);
    ASSERT_TRUE(reported_in2);
    ASSERT_TRUE(reported_out);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_remove_parameter_operation_slot_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t in1_id = 0;
    nmo_object_id_t in2_id = 0;
    nmo_object_id_t out_id = 0;
    nmo_object_id_t operation_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed op", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "A", &in1_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "B", &in2_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Out", &out_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_operation(
                  seed_tx,
                  root_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  in1_id,
                  in2_id,
                  out_id,
                  &operation_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_parameter(plan, in1_id, true));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_PARAMETER, report.operations[0].kind);

    bool reported_operation_slot_owner = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == operation_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "operation_slot_owner") == 0) {
            reported_operation_slot_owner = true;
        }
    }
    ASSERT_TRUE(reported_operation_slot_owner);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_remove_parameter_edge_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &owner_state->scripts, root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t source_id = 0;
    nmo_object_id_t target_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed edge", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Source", &source_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_INT, "Target", &target_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_connect_parameter(seed_tx, source_id, target_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_parameter(plan, source_id, true));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_PARAMETER, report.operations[0].kind);

    bool reported_target = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == target_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_target") == 0) {
            reported_target = true;
        }
    }
    ASSERT_TRUE(reported_target);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);

    nmo_object_id_t source2_id = 0;
    nmo_object_id_t target2_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed target edge", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Source2", &source2_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_INT, "Target2", &target2_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_connect_parameter(seed_tx, source2_id, target2_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_parameter(plan, target2_id, true));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_PARAMETER, report.operations[0].kind);

    bool reported_source = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == source2_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_source") == 0) {
            reported_source = true;
        }
    }
    ASSERT_TRUE(reported_source);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_runs_script_ops_and_records_validation) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_io(
        plan, root_id, NMO_SCRIPT_EDIT_IO_INPUT, "In"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_parameter(
        plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL, CKPGUID_STRING, "Local"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.validation.final_status);
    ASSERT_EQ(NMO_OK, report.validation.reference_status);
    ASSERT_EQ(NMO_OK, report.validation.behavior_index_status);
    ASSERT_EQ(NMO_OK, report.validation.interface_status);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);
    ASSERT_TRUE(report.operations[1].result_id != 0u);
    ASSERT_EQ(1u, report.operations[0].handle_count);
    ASSERT_STR_EQ("io", report.operations[0].handles[0].name);
    ASSERT_EQ(report.operations[0].result_id, report.operations[0].handles[0].id);
    ASSERT_EQ(1u, report.operations[1].handle_count);
    ASSERT_STR_EQ("parameter", report.operations[1].handles[0].name);
    ASSERT_EQ(report.operations[1].result_id, report.operations[1].handles[0].id);
    ASSERT_TRUE(report.created_object_count >= 2u);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_replaces_leaf_bb_in_transaction) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    nmo_guid_t replacement_guid = nmo_guid_parse("D0B7ADF3-D3FF3CF6");
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 343u,
        .block_guid = replacement_guid,
        .name = "Plan Replaced BB",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };

    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(343u, report.operations[0].result_id);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(343u, report.changed_objects[0].id);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, 343u);
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    ASSERT_NOT_NULL(state);
    ASSERT_TRUE(nmo_guid_equals(replacement_guid, state->block_guid));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_replace_bb_dry_run_rolls_back) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, 343u);
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    ASSERT_NOT_NULL(state);
    nmo_guid_t original_guid = state->block_guid;

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    nmo_guid_t replacement_guid = nmo_guid_parse("D0B7ADF3-D3FF3CF6");
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 343u,
        .block_guid = replacement_guid,
        .name = "Plan Dry Replace",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = true;

    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(343u, report.operations[0].result_id);
    ASSERT_TRUE(nmo_guid_equals(original_guid, state->block_guid));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_folds_closed_graph_in_transaction) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_id_t fold_nodes[] = {
        4166u, 4140u, 4147u, 4157u, 4165u,
        4153u, 4151u, 4155u, 4143u, 4145u,
    };
    nmo_guid_t fold_guid = nmo_guid_parse("42414C07-10000007");
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 4692u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 4166u,
        .block_guid = fold_guid,
        .name = "Plan Fold Small Graph",
        .block_version = 65536u,
        .preserve_boundary = true,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(4166u, report.operations[0].result_id);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor = nmo_object_repository_find_by_id(repo, 4166u);
    nmo_behavior_state_t *state = anchor
        ? (nmo_behavior_state_t *)nmo_object_get_state(anchor)
        : NULL;
    ASSERT_NOT_NULL(state);
    ASSERT_TRUE(nmo_guid_equals(fold_guid, state->block_guid));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, 4140u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_fold_dry_run_rolls_back) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor = nmo_object_repository_find_by_id(repo, 4166u);
    nmo_behavior_state_t *anchor_state = anchor
        ? (nmo_behavior_state_t *)nmo_object_get_state(anchor)
        : NULL;
    ASSERT_NOT_NULL(anchor_state);
    nmo_guid_t original_guid = anchor_state->block_guid;

    nmo_object_id_t fold_nodes[] = {
        4166u, 4140u, 4147u, 4157u, 4165u,
        4153u, 4151u, 4155u, 4143u, 4145u,
    };
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 4692u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 4166u,
        .block_guid = nmo_guid_parse("42414C07-10000007"),
        .name = "Plan Fold Dry Run",
        .block_version = 65536u,
        .preserve_boundary = true,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = true;

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(4166u, report.operations[0].result_id);
    ASSERT_TRUE(nmo_guid_equals(original_guid, anchor_state->block_guid));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 4140u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_fold_dry_run_reports_semantic_risks) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_id_t fold_nodes[] = {237u, 358u};
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 363u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 358u,
        .block_guid = nmo_guid_parse("42414C02-10000002"),
        .name = "Plan Risky Fold",
        .block_version = 65536u,
        .preserve_boundary = false,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = true;

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.semantic_risk_count > 0u);
    bool found_shared = false;
    for (size_t i = 0; i < report.semantic_risk_count; ++i) {
        if (report.semantic_risks[i].code &&
            strcmp(report.semantic_risks[i].code, "shared_parameter") == 0) {
            found_shared = true;
        }
    }
    ASSERT_TRUE(found_shared);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, report_semantic_risk_merge_deduplicates) {
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));

    nmo_behavior_semantic_risk_t risks[] = {
        {
            .severity = NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            .code = "message_flow",
            .message = "Selected behavior participates in message send/wait flow",
            .object_id = 2233u,
        },
        {
            .severity = NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            .code = "message_flow",
            .message = "Selected behavior participates in message send/wait flow",
            .object_id = 2233u,
        },
        {
            .severity = NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            .code = "activation_delay",
            .message = "Boundary control link preserves activation delay",
            .object_id = 1001u,
        },
    };

    ASSERT_EQ(NMO_OK, nmo_edit_report_merge_semantic_risks(
                          &report, risks, sizeof(risks) / sizeof(risks[0])));
    ASSERT_EQ(NMO_OK, nmo_edit_report_merge_semantic_risks(
                          &report, risks, sizeof(risks) / sizeof(risks[0])));

    ASSERT_EQ(2u, report.semantic_risk_count);
    ASSERT_STR_EQ("message_flow", report.semantic_risks[0].code);
    ASSERT_STR_EQ("activation_delay", report.semantic_risks[1].code);

    nmo_edit_report_dispose(&report);
}

TEST(edit_plan, executor_merges_edit_plan_semantic_validation) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 2233u,
        .block_guid = nmo_guid_parse("42414C07-10000007"),
        .name = "Semantic Validator Replace",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));

    bool saw_message_risk = false;
    for (size_t i = 0; i < report.semantic_risk_count; ++i) {
        if (report.semantic_risks[i].code != NULL &&
            strcmp(report.semantic_risks[i].code, "message_flow") == 0 &&
            report.semantic_risks[i].object_id == 2233u) {
            saw_message_risk = true;
        }
    }
    ASSERT_TRUE(saw_message_risk);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_reports_generic_activation_delay_risk) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Nop.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(
                  plan, 6u, 5u, NULL, 2u, NULL, 7u));

    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));

    bool saw_delay_risk = false;
    for (size_t i = 0; i < report.semantic_risk_count; ++i) {
        if (report.semantic_risks[i].code != NULL &&
            strcmp(report.semantic_risks[i].code, "activation_delay") == 0) {
            saw_delay_risk = true;
        }
    }
    ASSERT_TRUE(saw_delay_risk);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_fold_failure_reports_operation_diagnostic) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_id_t fold_nodes[] = {2364u, 2208u};
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
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 4692u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 2364u,
        .block_guid = nmo_guid_parse("42414C07-10000007"),
        .name = "Plan Unclosed Fold",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = sizeof(input_maps) / sizeof(input_maps[0]),
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_NE(NMO_OK, nmo_edit_executor_execute(workspace, plan, NULL, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_STR_EQ("selection_not_closed", report.operations[0].diagnostic_code);
    ASSERT_STR_CONTAINS(report.operations[0].diagnostic_message,
                        "must include child behavior");

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, stores_probe_selector_analysis_metadata) {
    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));

    nmo_probe_selector_result_t analysis;
    nmo_probe_selector_result_init(&analysis);
    analysis.mode = NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION;
    analysis.status = NMO_PROBE_SELECTOR_STATUS_SELECTED;
    analysis.selected_operation_id = 3791u;
    analysis.selected_link_id = 3780u;
    analysis.safe_insertion.selected = true;
    analysis.safe_insertion.selected_operation_id = 3791u;
    analysis.safe_insertion.remove_link_id = 3780u;
    analysis.safe_insertion.insert_from_io_id = 3779u;
    analysis.safe_insertion.insert_to_io_id = 3781u;
    analysis.safe_insertion.has_preserved_delay = true;
    analysis.safe_insertion.preserved_delay = 3u;
    ASSERT_EQ(NMO_OK,
              nmo_probe_selector_result_add_candidate(
                  &analysis,
                  &(nmo_probe_selector_candidate_t){
                      .operation_id = 3791u,
                      .link_id = 3780u,
                      .boundary_behavior_id = 3798u,
                      .source_parameter_id = 3789u,
                      .value_parameter_id = 3790u,
                      .dataarray_id = 6067u,
                      .column_type_guid = CKPGUID_STRING,
                      .confidence = 0.85,
                      .role = NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION,
                  }));

    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_set_probe_selector_analysis(plan, &analysis));

    const nmo_probe_selector_result_t *stored =
        nmo_edit_plan_get_probe_selector_analysis(plan);
    ASSERT_NOT_NULL(stored);
    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, stored->mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED, stored->status);
    ASSERT_EQ(1u, stored->candidate_count);
    ASSERT_EQ(3791u, stored->candidates[0].operation_id);
    ASSERT_EQ(6067u, stored->candidates[0].dataarray_id);
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_STRING,
                                stored->candidates[0].column_type_guid));
    ASSERT_TRUE(stored->safe_insertion.selected);
    ASSERT_EQ(3u, stored->safe_insertion.preserved_delay);

    nmo_edit_plan_t *clone = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_clone(plan, &clone));
    const nmo_probe_selector_result_t *cloned =
        nmo_edit_plan_get_probe_selector_analysis(clone);
    ASSERT_NOT_NULL(cloned);
    ASSERT_EQ(1u, cloned->candidate_count);
    ASSERT_EQ(3789u, cloned->candidates[0].source_parameter_id);
    ASSERT_EQ(3790u, cloned->candidates[0].value_parameter_id);

    nmo_edit_plan_destroy(clone);
    nmo_probe_analysis_dispose(&analysis);
    nmo_edit_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(edit_plan, stores_parameter_value_ops);
REGISTER_TEST(edit_plan, stores_full_script_edit_ops_and_clones_plan);
REGISTER_TEST(edit_plan, report_dispose_releases_schema_v2_arrays);
REGISTER_TEST(edit_plan, report_preserves_distinct_impact_roles);
REGISTER_TEST(edit_plan, report_owns_schema_v2_output_path);
REGISTER_TEST(edit_plan, executor_commits_parameter_value_plan);
REGISTER_TEST(edit_plan, executor_report_carries_probe_selector_analysis);
REGISTER_TEST(edit_plan, executor_rolls_back_failed_plan);
REGISTER_TEST(edit_plan, executor_rolls_back_created_handle_chain_failure);
REGISTER_TEST(edit_plan, executor_dry_run_reports_without_persisting);
REGISTER_TEST(edit_plan, executor_writes_manager_parameter_values);
REGISTER_TEST(edit_plan, executor_writes_display_formatted_manager_parameter_values);
REGISTER_TEST(edit_plan, executor_resizes_typed_parameter_values_when_requested);
REGISTER_TEST(edit_plan, executor_rejects_truncated_typed_parameter_values_without_resize);
REGISTER_TEST(edit_plan, executor_writes_object_reference_display_values);
REGISTER_TEST(edit_plan, executor_adds_node_with_created_object_report);
REGISTER_TEST(edit_plan, executor_materializes_building_block_defaults);
REGISTER_TEST(edit_plan, executor_materializes_targetable_beobject_target);
REGISTER_TEST(edit_plan, executor_materializes_common_building_block_prototypes);
REGISTER_TEST(edit_plan, executor_matches_authored_2d_text_golden_shape);
REGISTER_TEST(edit_plan, executor_resolves_symbolic_message_default_from_manager_data);
REGISTER_TEST(edit_plan, executor_creates_missing_symbolic_message_default_when_opted_in);
REGISTER_TEST(edit_plan, executor_resolves_parameter_value_from_prior_handle);
REGISTER_TEST(edit_plan, executor_materializes_input_source_for_handle_value);
REGISTER_TEST(edit_plan, executor_materializes_input_source_for_handle_bytes);
REGISTER_TEST(edit_plan, executor_connects_parameter_to_prior_node_handle);
REGISTER_TEST(edit_plan, executor_resolves_behavior_link_io_handles);
REGISTER_TEST(edit_plan, executor_reports_remove_node_detached_link_impact);
REGISTER_TEST(edit_plan, executor_reports_remove_node_parameter_edge_impact);
REGISTER_TEST(edit_plan, executor_detaches_removed_node_parameter_edges);
REGISTER_TEST(edit_plan, executor_reports_nested_removed_node_impact);
REGISTER_TEST(edit_plan, executor_reports_nested_removed_node_parameter_impact);
REGISTER_TEST(edit_plan, executor_reports_removed_node_operation_impact);
REGISTER_TEST(edit_plan, executor_reports_removed_node_control_link_impact);
REGISTER_TEST(edit_plan, executor_deletes_nested_removed_node_operations);
REGISTER_TEST(edit_plan, executor_detaches_nested_removed_node_control_links);
REGISTER_TEST(edit_plan, executor_reports_add_operation_slot_parameter_impact);
REGISTER_TEST(edit_plan, executor_reports_rewire_operation_slot_parameter_impact);
REGISTER_TEST(edit_plan, executor_reports_remove_parameter_operation_slot_impact);
REGISTER_TEST(edit_plan, executor_reports_remove_parameter_edge_impact);
REGISTER_TEST(edit_plan, executor_runs_script_ops_and_records_validation);
REGISTER_TEST(edit_plan, executor_replaces_leaf_bb_in_transaction);
REGISTER_TEST(edit_plan, executor_replace_bb_dry_run_rolls_back);
REGISTER_TEST(edit_plan, executor_folds_closed_graph_in_transaction);
REGISTER_TEST(edit_plan, executor_fold_dry_run_rolls_back);
REGISTER_TEST(edit_plan, executor_fold_dry_run_reports_semantic_risks);
REGISTER_TEST(edit_plan, report_semantic_risk_merge_deduplicates);
REGISTER_TEST(edit_plan, executor_merges_edit_plan_semantic_validation);
REGISTER_TEST(edit_plan, executor_reports_generic_activation_delay_risk);
REGISTER_TEST(edit_plan, executor_fold_failure_reports_operation_diagnostic);
REGISTER_TEST(edit_plan, stores_probe_selector_analysis_metadata);
TEST_MAIN_END()
