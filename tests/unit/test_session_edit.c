#include "test_framework.h"

#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"

#include <string.h>

static int ref_graph_edge_count(nmo_session_t *session)
{
    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    if (graph == NULL) {
        return -1;
    }
    nmo_ref_graph_stats_t stats = {0};
    nmo_ref_graph_get_stats(graph, &stats);
    return (int)stats.total_edges;
}

static int behavior_has_link(nmo_object_t *behavior_obj, nmo_object_id_t link_id)
{
    if (behavior_obj == NULL) {
        return 0;
    }
    nmo_behavior_state_t *state = (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj);
    if (state == NULL) {
        return 0;
    }
    size_t idx = 0;
    return nmo_array_find(&state->sub_behavior_links, &link_id, &idx) != 0;
}

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

static nmo_object_t *find_object_by_name_or_null(
    nmo_session_t *session,
    const char *name)
{
    nmo_object_t *object = NULL;
    return nmo_session_find_object_by_name(session, name, &object) == NMO_OK
        ? object
        : NULL;
}

static int g_behaviorlink_post_delete_called = 0;

static void behaviorlink_post_delete_probe(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    g_behaviorlink_post_delete_called++;
}

TEST(session_edit, set_reference_field_commit_invalidates_ref_graph) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t parent_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(session, NMO_CID_3DENTITY, "parent", &parent_id);
    create_object_or_fail(session, NMO_CID_3DENTITY, "child", &child_id);

    int before_edges = ref_graph_edge_count(session);

    char parent_text[32];
    snprintf(parent_text, sizeof(parent_text), "%u", parent_id);
    nmo_session_field_edit_t field = {"parent_id", parent_text};
    nmo_session_field_edit_result_t result = {0};
    nmo_session_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "set parent", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_set_object_fields(edit, child_id, &field, 1, &result));
    ASSERT_EQ(1u, result.applied);
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(edit));

    nmo_object_t *child_obj = nmo_object_repository_find_by_id(repo, child_id);
    ASSERT_NOT_NULL(child_obj);
    nmo_3dentity_state_t *child_state =
        (nmo_3dentity_state_t *)nmo_object_get_state(child_obj);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(parent_id, child_state->parent_id);
    ASSERT_TRUE(ref_graph_edge_count(session) > before_edges);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, set_reference_field_rollback_restores_without_invalidating_cache) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t parent_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(session, NMO_CID_3DENTITY, "parent", &parent_id);
    create_object_or_fail(session, NMO_CID_3DENTITY, "child", &child_id);

    nmo_ref_graph_t *before_graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(before_graph);

    char parent_text[32];
    snprintf(parent_text, sizeof(parent_text), "%u", parent_id);
    nmo_session_field_edit_t field = {"parent_id", parent_text};
    nmo_session_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "set parent rollback", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_set_object_fields(edit, child_id, &field, 1, NULL));
    nmo_session_edit_rollback(edit);

    nmo_object_t *child_obj = nmo_object_repository_find_by_id(repo, child_id);
    ASSERT_NOT_NULL(child_obj);
    nmo_3dentity_state_t *child_state =
        (nmo_3dentity_state_t *)nmo_object_get_state(child_obj);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(0u, child_state->parent_id);
    ASSERT_TRUE(before_graph == nmo_session_get_ref_graph(session));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, add_behavior_link_rollback_removes_created_link) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_io_id = 0;
    nmo_object_id_t to_io_id = 0;
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "graph", &behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "out", &from_io_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "in", &to_io_id);

    nmo_ref_graph_t *before_graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(before_graph);

    nmo_session_edit_t *edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "add link rollback", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_add_behavior_link(
                  edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_TRUE(link_id != 0);
    nmo_session_edit_rollback(edit);

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_FALSE(behavior_has_link(behavior_obj, link_id));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, link_id));
    ASSERT_TRUE(before_graph == nmo_session_get_ref_graph(session));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, add_behavior_link_commit_invalidates_ref_graph) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_io_id = 0;
    nmo_object_id_t to_io_id = 0;
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "graph", &behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "out", &from_io_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "in", &to_io_id);

    int before_edges = ref_graph_edge_count(session);
    nmo_session_edit_t *edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "add link commit", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_add_behavior_link(
                  edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(edit));

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_TRUE(behavior_has_link(behavior_obj, link_id));
    ASSERT_TRUE(ref_graph_edge_count(session) > before_edges);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, remove_behavior_link_rollback_restores_parent_array) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_io_id = 0;
    nmo_object_id_t to_io_id = 0;
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "graph", &behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "out", &from_io_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "in", &to_io_id);

    nmo_session_edit_t *add_edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "add link", &add_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_add_behavior_link(
                  add_edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(add_edit));

    nmo_session_edit_t *remove_edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "remove link rollback", &remove_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_remove_behavior_link(remove_edit, behavior_id, link_id));
    nmo_session_edit_rollback(remove_edit);

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_TRUE(behavior_has_link(behavior_obj, link_id));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, link_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, remove_behavior_link_commit_destroys_link) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_io_id = 0;
    nmo_object_id_t to_io_id = 0;
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "graph", &behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "out", &from_io_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "in", &to_io_id);

    nmo_session_edit_t *add_edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "add link", &add_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_add_behavior_link(
                  add_edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(add_edit));

    nmo_session_edit_t *remove_edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "remove link commit", &remove_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_remove_behavior_link(remove_edit, behavior_id, link_id));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(remove_edit));

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_FALSE(behavior_has_link(behavior_obj, link_id));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, link_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, remove_behavior_link_commit_runs_delete_hooks) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_descriptor_t *link_type =
        nmo_type_registry_find_by_class_id_inherited(registry, NMO_CID_BEHAVIORLINK);
    ASSERT_NOT_NULL(link_type);
    ASSERT_NOT_NULL(link_type->vtable);
    nmo_type_vtable_t *mutable_vtable = (nmo_type_vtable_t *)(void *)link_type->vtable;
    nmo_type_post_delete_fn old_post_delete = mutable_vtable->post_delete;
    mutable_vtable->post_delete = behaviorlink_post_delete_probe;
    g_behaviorlink_post_delete_called = 0;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_io_id = 0;
    nmo_object_id_t to_io_id = 0;
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "graph", &behavior_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "out", &from_io_id);
    create_object_or_fail(session, NMO_CID_BEHAVIORIO, "in", &to_io_id);

    nmo_session_edit_t *add_edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "add link", &add_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_add_behavior_link(
                  add_edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(add_edit));

    nmo_session_edit_t *remove_edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "remove link hook", &remove_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_remove_behavior_link(remove_edit, behavior_id, link_id));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(remove_edit));

    mutable_vtable->post_delete = old_post_delete;
    ASSERT_EQ(1, g_behaviorlink_post_delete_called);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, rename_object_commit_rebuilds_name_index) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t object_id = 0;
    create_object_or_fail(session, NMO_CID_OBJECT, "old-name", &object_id);
    ASSERT_EQ(NMO_OK, nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL));
    ASSERT_NOT_NULL(find_object_by_name_or_null(session, "old-name"));

    nmo_session_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "rename commit", &edit));
    ASSERT_EQ(NMO_OK, nmo_session_edit_rename_object(edit, object_id, "new-name"));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(edit));

    ASSERT_NULL(find_object_by_name_or_null(session, "old-name"));
    nmo_object_t *renamed = find_object_by_name_or_null(session, "new-name");
    ASSERT_NOT_NULL(renamed);
    ASSERT_EQ(object_id, nmo_object_get_id(renamed));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, rename_object_rollback_restores_name_without_rebuilding_index) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t object_id = 0;
    create_object_or_fail(session, NMO_CID_OBJECT, "old-name", &object_id);
    ASSERT_EQ(NMO_OK, nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL));
    nmo_object_index_t *before_index = nmo_session_get_object_index(session);
    ASSERT_NOT_NULL(before_index);

    nmo_session_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "rename rollback", &edit));
    ASSERT_EQ(NMO_OK, nmo_session_edit_rename_object(edit, object_id, "new-name"));
    nmo_session_edit_rollback(edit);

    ASSERT_TRUE(before_index == nmo_session_get_object_index(session));
    nmo_object_t *restored = find_object_by_name_or_null(session, "old-name");
    ASSERT_NOT_NULL(restored);
    ASSERT_EQ(object_id, nmo_object_get_id(restored));
    ASSERT_NULL(find_object_by_name_or_null(session, "new-name"));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, parameter_edit_rollback_restores_buffer) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "param", &param_id);
    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_INT;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, sizeof(uint8_t), sizeof(int32_t), NULL));
    int32_t initial = 7;
    memcpy(state->buffer_data.data, &initial, sizeof(initial));

    nmo_session_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "parameter rollback", &edit));
    ASSERT_EQ(NMO_OK, nmo_session_edit_set_parameter_value(edit, param_id, "42"));
    nmo_session_edit_rollback(edit);

    int32_t restored = 0;
    memcpy(&restored, state->buffer_data.data, sizeof(restored));
    ASSERT_EQ(7, restored);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, parameterout_object_mode_commit_sets_reference) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t target_id = 0;
    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_OBJECT, "target", &target_id);
    create_object_or_fail(session, NMO_CID_PARAMETEROUT, "param-out", &param_id);

    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->mode = CKPARAM_MODE_OBJECT;
    state->object_id = 0;

    char target_text[32];
    snprintf(target_text, sizeof(target_text), "#%u", target_id);
    nmo_session_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "parameterout object", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_set_parameter_value(edit, param_id, target_text));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(edit));
    ASSERT_EQ(target_id, state->object_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, dataarray_ref_graph_handles_missing_row_metadata) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t dataarray_id = 0;
    create_object_or_fail(session, NMO_CID_DATAARRAY, "partial-data", &dataarray_id);
    nmo_object_t *data_obj = nmo_object_repository_find_by_id(repo, dataarray_id);
    ASSERT_NOT_NULL(data_obj);
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)nmo_object_get_state(data_obj);
    ASSERT_NOT_NULL(state);
    state->column_count = 1;
    state->row_count = 1;
    state->column_formats = NULL;
    state->rows = NULL;

    nmo_session_invalidate_ref_graph(session);
    ASSERT_NOT_NULL(nmo_session_get_ref_graph(session));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session_edit, dataarray_object_cell_commit_and_rollback) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_NOT_NULL(arena);

    nmo_object_id_t target_id = 0;
    nmo_object_id_t dataarray_id = 0;
    create_object_or_fail(session, NMO_CID_OBJECT, "target", &target_id);
    create_object_or_fail(session, NMO_CID_DATAARRAY, "data", &dataarray_id);
    nmo_object_t *data_obj = nmo_object_repository_find_by_id(repo, dataarray_id);
    ASSERT_NOT_NULL(data_obj);
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)nmo_object_get_state(data_obj);
    ASSERT_NOT_NULL(state);
    state->column_count = 1;
    state->row_count = 1;
    state->column_formats = (nmo_dataarray_column_format_t *)nmo_arena_alloc(
        arena, sizeof(nmo_dataarray_column_format_t), _Alignof(nmo_dataarray_column_format_t));
    ASSERT_NOT_NULL(state->column_formats);
    state->column_formats[0].name = "object";
    state->column_formats[0].type = CKARRAYTYPE_OBJECT;
    state->column_formats[0].parameter_type_guid = NMO_GUID_NULL;
    state->rows = (nmo_dataarray_row_t *)nmo_arena_alloc(
        arena, sizeof(nmo_dataarray_row_t), _Alignof(nmo_dataarray_row_t));
    ASSERT_NOT_NULL(state->rows);
    state->rows[0].column_count = 1;
    state->rows[0].cells = (nmo_dataarray_cell_t *)nmo_arena_alloc(
        arena, sizeof(nmo_dataarray_cell_t), _Alignof(nmo_dataarray_cell_t));
    ASSERT_NOT_NULL(state->rows[0].cells);
    memset(state->rows[0].cells, 0, sizeof(nmo_dataarray_cell_t));

    int before_edges = ref_graph_edge_count(session);
    char target_text[32];
    snprintf(target_text, sizeof(target_text), "#%u", target_id);

    nmo_session_edit_t *commit_edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "dataarray commit", &commit_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_set_dataarray_cell(commit_edit, dataarray_id, 0, 0, target_text));
    ASSERT_EQ(NMO_OK, nmo_session_edit_commit(commit_edit));
    ASSERT_EQ(target_id, state->rows[0].cells[0].object_id);
    ASSERT_TRUE(ref_graph_edge_count(session) > before_edges);

    nmo_session_edit_t *rollback_edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_edit_begin(session, "dataarray rollback", &rollback_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_edit_set_dataarray_cell(rollback_edit, dataarray_id, 0, 0, "#0"));
    nmo_session_edit_rollback(rollback_edit);
    ASSERT_EQ(target_id, state->rows[0].cells[0].object_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(session_edit, set_reference_field_commit_invalidates_ref_graph);
REGISTER_TEST(session_edit, set_reference_field_rollback_restores_without_invalidating_cache);
REGISTER_TEST(session_edit, add_behavior_link_rollback_removes_created_link);
REGISTER_TEST(session_edit, add_behavior_link_commit_invalidates_ref_graph);
REGISTER_TEST(session_edit, remove_behavior_link_rollback_restores_parent_array);
REGISTER_TEST(session_edit, remove_behavior_link_commit_destroys_link);
REGISTER_TEST(session_edit, remove_behavior_link_commit_runs_delete_hooks);
REGISTER_TEST(session_edit, rename_object_commit_rebuilds_name_index);
REGISTER_TEST(session_edit, rename_object_rollback_restores_name_without_rebuilding_index);
REGISTER_TEST(session_edit, parameter_edit_rollback_restores_buffer);
REGISTER_TEST(session_edit, parameterout_object_mode_commit_sets_reference);
REGISTER_TEST(session_edit, dataarray_object_cell_commit_and_rollback);
REGISTER_TEST(session_edit, dataarray_ref_graph_handles_missing_row_metadata);
TEST_MAIN_END()
