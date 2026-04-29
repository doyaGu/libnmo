#include "test_framework.h"

#include "document/nmo_document.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "runtime/nmo_workspace.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_value_writer.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_analyze.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_color.h"
#include "core/nmo_math.h"
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
    nmo_document_t *document = NULL;
    nmo_object_t *object = NULL;
    if (nmo_session_borrow_document(session, &document) != NMO_OK) {
        return NULL;
    }
    return nmo_object_query_resolve_one(
               document,
               &(nmo_object_selector_t){.name = name},
               &object,
               NULL) == NMO_OK
        ? object
        : NULL;
}

typedef struct workspace_edit_scope {
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_workspace_edit_t *edit;
} workspace_edit_scope_t;

static void workspace_edit_scope_destroy(workspace_edit_scope_t *scope)
{
    if (scope == NULL) {
        return;
    }
    if (scope->workspace != NULL) {
        nmo_workspace_destroy(scope->workspace);
    }
    if (scope->document != NULL) {
        nmo_document_destroy(scope->document);
    }
    memset(scope, 0, sizeof(*scope));
}

static nmo_status_t begin_workspace_edit_for_session(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const char *label,
    workspace_edit_scope_t *scope,
    nmo_workspace_edit_t **out_edit)
{
    nmo_status_t rc = NMO_OK;

    if (ctx == NULL || session == NULL || scope == NULL || out_edit == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_edit = NULL;
    memset(scope, 0, sizeof(*scope));

    rc = nmo_session_borrow_document(session, &scope->document);
    if (rc != NMO_OK) {
        workspace_edit_scope_destroy(scope);
        return rc;
    }
    rc = nmo_workspace_create(ctx, scope->document, &scope->workspace);
    if (rc != NMO_OK) {
        workspace_edit_scope_destroy(scope);
        return rc;
    }
    rc = nmo_workspace_edit_begin(scope->workspace, label, &scope->edit);
    if (rc != NMO_OK) {
        workspace_edit_scope_destroy(scope);
        return rc;
    }

    *out_edit = scope->edit;
    return NMO_OK;
}

static void rollback_workspace_edit_scope(workspace_edit_scope_t *scope)
{
    if (scope == NULL) {
        return;
    }
    if (scope->edit != NULL) {
        nmo_workspace_edit_rollback(scope->edit);
        scope->edit = NULL;
    }
    workspace_edit_scope_destroy(scope);
}

static nmo_status_t commit_workspace_edit_scope(workspace_edit_scope_t *scope)
{
    nmo_status_t rc = NMO_OK;

    if (scope == NULL || scope->edit == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    rc = nmo_workspace_edit_commit(scope->edit);
    scope->edit = NULL;
    workspace_edit_scope_destroy(scope);
    return rc;
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

TEST(workspace_edit, begin_commit_roundtrip) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    nmo_workspace_t *workspace = NULL;
    ASSERT_NOT_NULL(document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_NOT_NULL(workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "workspace edit", &edit));
    ASSERT_NOT_NULL(edit);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(workspace_edit, set_reference_field_commit_invalidates_ref_graph) {
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
    workspace_edit_scope_t edit_scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "set parent", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_fields(edit, child_id, &field, 1, &result));
    ASSERT_EQ(1u, result.applied);
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&edit_scope));

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

TEST(workspace_edit, set_reference_field_rollback_restores_without_invalidating_cache) {
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
    workspace_edit_scope_t edit_scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "set parent rollback", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_fields(edit, child_id, &field, 1, NULL));
    rollback_workspace_edit_scope(&edit_scope);

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

TEST(workspace_edit, add_behavior_link_rollback_removes_created_link) {
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

    workspace_edit_scope_t edit_scope = {0};

    nmo_workspace_edit_t *edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "add link rollback", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(
                  edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_TRUE(link_id != 0);
    rollback_workspace_edit_scope(&edit_scope);

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_FALSE(behavior_has_link(behavior_obj, link_id));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, link_id));
    ASSERT_TRUE(before_graph == nmo_session_get_ref_graph(session));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, add_behavior_link_commit_invalidates_ref_graph) {
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
    workspace_edit_scope_t edit_scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "add link commit", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(
                  edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&edit_scope));

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_TRUE(behavior_has_link(behavior_obj, link_id));
    ASSERT_TRUE(ref_graph_edge_count(session) > before_edges);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, remove_behavior_link_rollback_restores_parent_array) {
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

    workspace_edit_scope_t add_edit_scope = {0};

    nmo_workspace_edit_t *add_edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "add link", &add_edit_scope, &add_edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(
                  add_edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&add_edit_scope));

    workspace_edit_scope_t remove_edit_scope = {0};

    nmo_workspace_edit_t *remove_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "remove link rollback", &remove_edit_scope, &remove_edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_remove_link(remove_edit, behavior_id, link_id));
    rollback_workspace_edit_scope(&remove_edit_scope);

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_TRUE(behavior_has_link(behavior_obj, link_id));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, link_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, remove_behavior_link_commit_destroys_link) {
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

    workspace_edit_scope_t add_edit_scope = {0};

    nmo_workspace_edit_t *add_edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "add link", &add_edit_scope, &add_edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(
                  add_edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&add_edit_scope));

    workspace_edit_scope_t remove_edit_scope = {0};

    nmo_workspace_edit_t *remove_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "remove link commit", &remove_edit_scope, &remove_edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_remove_link(remove_edit, behavior_id, link_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&remove_edit_scope));

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_FALSE(behavior_has_link(behavior_obj, link_id));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, link_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, remove_behavior_link_commit_runs_delete_hooks) {
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

    workspace_edit_scope_t add_edit_scope = {0};

    nmo_workspace_edit_t *add_edit = NULL;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "add link", &add_edit_scope, &add_edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_add_link(
                  add_edit, behavior_id, from_io_id, to_io_id, 1, &link_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&add_edit_scope));

    workspace_edit_scope_t remove_edit_scope = {0};

    nmo_workspace_edit_t *remove_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "remove link hook", &remove_edit_scope, &remove_edit));
    ASSERT_EQ(NMO_OK,
              nmo_behavior_edit_remove_link(remove_edit, behavior_id, link_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&remove_edit_scope));

    mutable_vtable->post_delete = old_post_delete;
    ASSERT_EQ(1, g_behaviorlink_post_delete_called);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, mark_behavior_interface_requires_interface_data) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_NOT_NULL(arena);

    nmo_object_id_t behavior_id = 0;
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "interface-owner", &behavior_id);
    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    nmo_behavior_state_t *state =
        (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj);
    ASSERT_NOT_NULL(state);

    workspace_edit_scope_t missing_edit_scope = {0};

    nmo_workspace_edit_t *missing_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "missing interface", &missing_edit_scope, &missing_edit));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_behavior_edit_mark_interface(missing_edit, behavior_id));
    rollback_workspace_edit_scope(&missing_edit_scope);

    state->interface_data = (nmo_interface_data_t *)nmo_arena_alloc(
        arena, sizeof(nmo_interface_data_t), _Alignof(nmo_interface_data_t));
    ASSERT_NOT_NULL(state->interface_data);
    memset(state->interface_data, 0, sizeof(nmo_interface_data_t));
    state->interface_data->script.behavior_id = behavior_id;

    workspace_edit_scope_t edit_scope = {0};

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "mark interface", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK, nmo_behavior_edit_mark_interface(edit, behavior_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&edit_scope));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, rename_object_commit_rebuilds_name_index) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t object_id = 0;
    create_object_or_fail(session, NMO_CID_OBJECT, "old-name", &object_id);
    ASSERT_EQ(NMO_OK, nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL));
    ASSERT_NOT_NULL(find_object_by_name_or_null(session, "old-name"));

    workspace_edit_scope_t edit_scope = {0};

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "rename commit", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK, nmo_object_edit_rename(edit, object_id, "new-name"));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&edit_scope));

    ASSERT_NULL(find_object_by_name_or_null(session, "old-name"));
    nmo_object_t *renamed = find_object_by_name_or_null(session, "new-name");
    ASSERT_NOT_NULL(renamed);
    ASSERT_EQ(object_id, nmo_object_get_id(renamed));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, rename_object_rollback_restores_name_without_rebuilding_index) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t object_id = 0;
    create_object_or_fail(session, NMO_CID_OBJECT, "old-name", &object_id);
    ASSERT_EQ(NMO_OK, nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL));
    nmo_object_index_t *before_index = nmo_session_get_object_index(session);
    ASSERT_NOT_NULL(before_index);

    workspace_edit_scope_t edit_scope = {0};

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "rename rollback", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK, nmo_object_edit_rename(edit, object_id, "new-name"));
    rollback_workspace_edit_scope(&edit_scope);

    ASSERT_TRUE(before_index == nmo_session_get_object_index(session));
    nmo_object_t *restored = find_object_by_name_or_null(session, "old-name");
    ASSERT_NOT_NULL(restored);
    ASSERT_EQ(object_id, nmo_object_get_id(restored));
    ASSERT_NULL(find_object_by_name_or_null(session, "new-name"));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, parameter_edit_rollback_restores_buffer) {
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

    workspace_edit_scope_t edit_scope = {0};

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "parameter rollback", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK, nmo_object_edit_set_parameter_value(edit, param_id, "42"));
    rollback_workspace_edit_scope(&edit_scope);

    int32_t restored = 0;
    memcpy(&restored, state->buffer_data.data, sizeof(restored));
    ASSERT_EQ(7, restored);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_resizes_string_parameter_and_nul_terminates) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "string-param", &param_id);
    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_STRING;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, sizeof(uint8_t), 4, NULL));
    memset(state->buffer_data.data, 0xCC, state->buffer_data.count);

    const char *text = "LONGER TEXT";

    workspace_edit_scope_t edit_scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "string resize", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(edit, param_id, text, NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&edit_scope));

    ASSERT_EQ(strlen(text) + 1u, state->buffer_data.count);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, text, strlen(text)));
    ASSERT_EQ(0, ((const uint8_t *)state->buffer_data.data)[strlen(text)]);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_raw_bytes_requires_explicit_resize) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "raw-param", &param_id);
    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_VOIDBUF;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, sizeof(uint8_t), 2, NULL));
    uint8_t initial[2] = {0xAA, 0xBB};
    memcpy(state->buffer_data.data, initial, sizeof(initial));

    uint8_t bytes[4] = {1, 2, 3, 4};
    nmo_value_write_options_t options = nmo_value_write_options_default();

    workspace_edit_scope_t rejected_scope = {0};
    nmo_workspace_edit_t *rejected_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "raw no resize", &rejected_scope, &rejected_edit));
    ASSERT_EQ(NMO_ERR_OUT_OF_BOUNDS,
              nmo_value_writer_set_parameter_bytes(
                  rejected_edit, param_id, bytes, sizeof(bytes), &options));
    rollback_workspace_edit_scope(&rejected_scope);
    ASSERT_EQ(2u, state->buffer_data.count);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, initial, sizeof(initial)));

    options.resize = true;
    workspace_edit_scope_t resized_scope = {0};
    nmo_workspace_edit_t *resized_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "raw resize", &resized_scope, &resized_edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_bytes(
                  resized_edit, param_id, bytes, sizeof(bytes), &options));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&resized_scope));
    ASSERT_EQ(sizeof(bytes), state->buffer_data.count);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, bytes, sizeof(bytes)));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_resize_rollback_restores_buffer_size) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "rollback-string", &param_id);
    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_STRING;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, sizeof(uint8_t), 4, NULL));
    memcpy(state->buffer_data.data, "old", 4);

    workspace_edit_scope_t edit_scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "string resize rollback", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  edit, param_id, "much longer text", NULL));
    rollback_workspace_edit_scope(&edit_scope);

    ASSERT_EQ(4u, state->buffer_data.count);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, "old", 4));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_writes_object_refs_and_rejects_invalid_text) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t param_id = 0;
    nmo_object_id_t target_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "object-param", &param_id);
    create_object_or_fail(session, NMO_CID_BEOBJECT, "target-object", &target_id);

    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_OBJECT;
    state->mode = CKPARAM_MODE_OBJECT;
    state->has_state = true;
    state->object_id = 77u;

    char text[64];
    workspace_edit_scope_t bare_scope = {0};
    nmo_workspace_edit_t *bare_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "object ref bare", &bare_scope, &bare_edit));
    snprintf(text, sizeof(text), "%u", target_id);
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  bare_edit, param_id, text, NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&bare_scope));
    ASSERT_EQ(target_id, state->object_id);

    workspace_edit_scope_t prefixed_scope = {0};
    nmo_workspace_edit_t *prefixed_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "object ref prefix", &prefixed_scope, &prefixed_edit));
    snprintf(text, sizeof(text), "object:%u", target_id + 1u);
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  prefixed_edit, param_id, text, NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&prefixed_scope));
    ASSERT_EQ(target_id + 1u, state->object_id);

    workspace_edit_scope_t hash_scope = {0};
    nmo_workspace_edit_t *hash_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "object ref hash", &hash_scope, &hash_edit));
    snprintf(text, sizeof(text), "#%u", target_id);
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  hash_edit, param_id, text, NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&hash_scope));
    ASSERT_EQ(target_id, state->object_id);

    workspace_edit_scope_t invalid_scope = {0};
    nmo_workspace_edit_t *invalid_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "object ref invalid", &invalid_scope, &invalid_edit));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_value_writer_set_parameter_value(
                  invalid_edit, param_id, "object:not-a-number", NULL));
    rollback_workspace_edit_scope(&invalid_scope);
    ASSERT_EQ(target_id, state->object_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_writes_manager_refs_and_rejects_invalid_text) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "manager-param", &param_id);

    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_INT;
    state->mode = CKPARAM_MODE_MANAGER;
    state->has_state = true;
    state->manager_guid = nmo_guid_parse("11111111-22222222");
    state->manager_value = 7u;

    workspace_edit_scope_t manager_scope = {0};
    nmo_workspace_edit_t *manager_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "manager ref", &manager_scope, &manager_edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  manager_edit,
                  param_id,
                  "manager{33333333-44444444}:99",
                  NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&manager_scope));
    ASSERT_TRUE(nmo_guid_equals(
        nmo_guid_parse("33333333-44444444"), state->manager_guid));
    ASSERT_EQ(99u, state->manager_value);

    workspace_edit_scope_t equals_scope = {0};
    nmo_workspace_edit_t *equals_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "manager ref equals", &equals_scope, &equals_edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  equals_edit,
                  param_id,
                  "{55555555-66666666}=123",
                  NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&equals_scope));
    ASSERT_TRUE(nmo_guid_equals(
        nmo_guid_parse("55555555-66666666"), state->manager_guid));
    ASSERT_EQ(123u, state->manager_value);

    workspace_edit_scope_t invalid_scope = {0};
    nmo_workspace_edit_t *invalid_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "manager ref invalid", &invalid_scope, &invalid_edit));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_value_writer_set_parameter_value(
                  invalid_edit,
                  param_id,
                  "{55555555-66666666}=not-a-number",
                  NULL));
    rollback_workspace_edit_scope(&invalid_scope);
    ASSERT_TRUE(nmo_guid_equals(
        nmo_guid_parse("55555555-66666666"), state->manager_guid));
    ASSERT_EQ(123u, state->manager_value);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_resolves_message_manager_names_with_policy) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "message-param", &param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(nmo_session_get_repository(session), param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_MESSAGE;
    state->mode = CKPARAM_MODE_MANAGER;
    state->has_state = true;
    state->manager_guid = NMO_MANAGER_GUID_MESSAGE;
    state->manager_value = 1u;

    workspace_edit_scope_t missing_scope = {0};
    nmo_workspace_edit_t *missing_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "message manager missing", &missing_scope,
                  &missing_edit));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_value_writer_set_parameter_value(
                  missing_edit, param_id, "CreatedByPolicy", NULL));
    rollback_workspace_edit_scope(&missing_scope);
    ASSERT_EQ(1u, state->manager_value);

    nmo_parameter_write_options_t options = {
        .manager_entry.policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
    };
    workspace_edit_scope_t create_scope = {0};
    nmo_workspace_edit_t *create_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "message manager create", &create_scope,
                  &create_edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  create_edit, param_id, "CreatedByPolicy", &options));
    ASSERT_TRUE(nmo_guid_equals(NMO_MANAGER_GUID_MESSAGE, state->manager_guid));
    ASSERT_EQ(0u, state->manager_value);
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  create_edit, param_id, "CreatedByPolicy", NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&create_scope));
    ASSERT_EQ(0u, state->manager_value);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_uses_manager_entry_key_for_lookup) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "message-param", &param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(nmo_session_get_repository(session), param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_MESSAGE;
    state->mode = CKPARAM_MODE_MANAGER;
    state->has_state = true;
    state->manager_guid = NMO_MANAGER_GUID_MESSAGE;
    state->manager_value = 99u;

    nmo_parameter_write_options_t create_options = {
        .manager_entry = {
            .policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
            .schema = NMO_MANAGER_ENTRY_SCHEMA_MESSAGE,
            .key = "EntryKey",
        },
    };
    workspace_edit_scope_t create_scope = {0};
    nmo_workspace_edit_t *create_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "message manager create by key", &create_scope,
                  &create_edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  create_edit, param_id, "ValueText", &create_options));
    ASSERT_TRUE(nmo_guid_equals(NMO_MANAGER_GUID_MESSAGE, state->manager_guid));
    ASSERT_EQ(0u, state->manager_value);
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  create_edit, param_id, "EntryKey", NULL));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_value_writer_set_parameter_value(
                  create_edit, param_id, "ValueText", NULL));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&create_scope));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_rejects_unsupported_manager_entry_kind) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "message-param", &param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(nmo_session_get_repository(session), param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_MESSAGE;
    state->mode = CKPARAM_MODE_MANAGER;
    state->has_state = true;
    state->manager_guid = NMO_MANAGER_GUID_MESSAGE;
    state->manager_value = 1u;

    nmo_parameter_write_options_t options = {
        .manager_entry = {
            .policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
            .schema = NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE,
        },
    };
    workspace_edit_scope_t scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "unsupported manager", &scope, &edit));
    ASSERT_EQ(NMO_ERR_NOT_SUPPORTED,
              nmo_value_writer_set_parameter_value(
                  edit, param_id, "AttributeName", &options));
    rollback_workspace_edit_scope(&scope);
    ASSERT_EQ(1u, state->manager_value);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_accepts_message_manager_guid_option) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "message-param", &param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(nmo_session_get_repository(session), param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_MESSAGE;
    state->mode = CKPARAM_MODE_MANAGER;
    state->has_state = true;
    state->manager_guid = NMO_MANAGER_GUID_MESSAGE;
    state->manager_value = 1u;

    nmo_parameter_write_options_t options = {
        .manager_entry = {
            .policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
            .schema = NMO_MANAGER_ENTRY_SCHEMA_MESSAGE,
            .manager_guid = NMO_MANAGER_GUID_MESSAGE,
            .key = "GuidMessage",
        },
    };
    workspace_edit_scope_t scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "message manager guid", &scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_value_writer_set_parameter_value(
                  edit, param_id, "IgnoredValue", &options));
    ASSERT_TRUE(nmo_guid_equals(NMO_MANAGER_GUID_MESSAGE, state->manager_guid));
    ASSERT_EQ(0u, state->manager_value);
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&scope));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_writes_structured_parameter_values) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t vector_param_id = 0;
    nmo_object_id_t vector4_param_id = 0;
    nmo_object_id_t color_param_id = 0;
    nmo_object_id_t matrix_param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "vector-param", &vector_param_id);
    create_object_or_fail(session, NMO_CID_PARAMETER, "vector4-param", &vector4_param_id);
    create_object_or_fail(session, NMO_CID_PARAMETER, "color-param", &color_param_id);
    create_object_or_fail(session, NMO_CID_PARAMETER, "matrix-param", &matrix_param_id);

    nmo_object_t *vector_obj = nmo_object_repository_find_by_id(repo, vector_param_id);
    nmo_object_t *vector4_obj = nmo_object_repository_find_by_id(repo, vector4_param_id);
    nmo_object_t *color_obj = nmo_object_repository_find_by_id(repo, color_param_id);
    nmo_object_t *matrix_obj = nmo_object_repository_find_by_id(repo, matrix_param_id);
    ASSERT_NOT_NULL(vector_obj);
    ASSERT_NOT_NULL(vector4_obj);
    ASSERT_NOT_NULL(color_obj);
    ASSERT_NOT_NULL(matrix_obj);

    nmo_parameter_state_t *vector_state = nmo_parameter_get_mutable_state(vector_obj);
    nmo_parameter_state_t *vector4_state = nmo_parameter_get_mutable_state(vector4_obj);
    nmo_parameter_state_t *color_state = nmo_parameter_get_mutable_state(color_obj);
    nmo_parameter_state_t *matrix_state = nmo_parameter_get_mutable_state(matrix_obj);
    ASSERT_NOT_NULL(vector_state);
    ASSERT_NOT_NULL(vector4_state);
    ASSERT_NOT_NULL(color_state);
    ASSERT_NOT_NULL(matrix_state);

    vector_state->type_guid = CKPGUID_VECTOR;
    vector_state->mode = CKPARAM_MODE_BUFFER;
    vector_state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(&vector_state->buffer_data,
                              sizeof(uint8_t),
                              sizeof(nmo_vector_t),
                              NULL));
    memset(vector_state->buffer_data.data, 0, vector_state->buffer_data.count);

    vector4_state->type_guid = CKPGUID_VECTOR4;
    vector4_state->mode = CKPARAM_MODE_BUFFER;
    vector4_state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(&vector4_state->buffer_data,
                              sizeof(uint8_t),
                              sizeof(nmo_vector4_t),
                              NULL));
    memset(vector4_state->buffer_data.data, 0, vector4_state->buffer_data.count);

    color_state->type_guid = CKPGUID_COLOR;
    color_state->mode = CKPARAM_MODE_BUFFER;
    color_state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(&color_state->buffer_data,
                              sizeof(uint8_t),
                              sizeof(nmo_color_t),
                              NULL));
    memset(color_state->buffer_data.data, 0, color_state->buffer_data.count);

    matrix_state->type_guid = CKPGUID_MATRIX;
    matrix_state->mode = CKPARAM_MODE_BUFFER;
    matrix_state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(&matrix_state->buffer_data,
                              sizeof(uint8_t),
                              sizeof(nmo_matrix_t),
                              NULL));
    memset(matrix_state->buffer_data.data, 0, matrix_state->buffer_data.count);

    workspace_edit_scope_t commit_scope = {0};
    nmo_workspace_edit_t *commit_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "structured values", &commit_scope, &commit_edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  commit_edit, vector_param_id, "(1.5, 2.5, 3.5)"));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  commit_edit, vector4_param_id, "(4.5, 5.5, 6.5, 7.5)"));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  commit_edit, color_param_id, "(0.25, 0.5, 0.75, 1)"));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  commit_edit,
                  matrix_param_id,
                  "(1, 2, 3, 4; 5, 6, 7, 8; 9, 10, 11, 12; 13, 14, 15, 16)"));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&commit_scope));

    const nmo_vector_t *vector_value =
        (const nmo_vector_t *)vector_state->buffer_data.data;
    const nmo_vector4_t *vector4_value =
        (const nmo_vector4_t *)vector4_state->buffer_data.data;
    const nmo_color_t *color_value =
        (const nmo_color_t *)color_state->buffer_data.data;
    const nmo_matrix_t *matrix_value =
        (const nmo_matrix_t *)matrix_state->buffer_data.data;
    ASSERT_FLOAT_EQ(1.5f, vector_value->x, 0.001f);
    ASSERT_FLOAT_EQ(2.5f, vector_value->y, 0.001f);
    ASSERT_FLOAT_EQ(3.5f, vector_value->z, 0.001f);
    ASSERT_FLOAT_EQ(4.5f, vector4_value->x, 0.001f);
    ASSERT_FLOAT_EQ(5.5f, vector4_value->y, 0.001f);
    ASSERT_FLOAT_EQ(6.5f, vector4_value->z, 0.001f);
    ASSERT_FLOAT_EQ(7.5f, vector4_value->w, 0.001f);
    ASSERT_FLOAT_EQ(0.25f, color_value->r, 0.001f);
    ASSERT_FLOAT_EQ(0.5f, color_value->g, 0.001f);
    ASSERT_FLOAT_EQ(0.75f, color_value->b, 0.001f);
    ASSERT_FLOAT_EQ(1.0f, color_value->a, 0.001f);
    ASSERT_FLOAT_EQ(1.0f, matrix_value->m[0][0], 0.001f);
    ASSERT_FLOAT_EQ(16.0f, matrix_value->m[3][3], 0.001f);

    nmo_vector_t before_invalid = *vector_value;
    workspace_edit_scope_t invalid_scope = {0};
    nmo_workspace_edit_t *invalid_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "invalid structured value", &invalid_scope, &invalid_edit));
    ASSERT_NE(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  invalid_edit, vector_param_id, "(1, 2)"));
    rollback_workspace_edit_scope(&invalid_scope);
    vector_value = (const nmo_vector_t *)vector_state->buffer_data.data;
    ASSERT_FLOAT_EQ(before_invalid.x, vector_value->x, 0.001f);
    ASSERT_FLOAT_EQ(before_invalid.y, vector_value->y, 0.001f);
    ASSERT_FLOAT_EQ(before_invalid.z, vector_value->z, 0.001f);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, value_writer_writes_enum_and_flag_parameter_values) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    nmo_guid_t enum_guid = NMO_GUID_ENUM_CK_BEHAVIOR_TYPE;
    nmo_guid_t flags_guid = NMO_GUID_ENUM_CK_BEHAVIOR_FLAGS;

    nmo_object_id_t enum_param_id = 0;
    nmo_object_id_t flags_param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "enum-param", &enum_param_id);
    create_object_or_fail(session, NMO_CID_PARAMETER, "flags-param", &flags_param_id);

    nmo_object_t *enum_obj = nmo_object_repository_find_by_id(repo, enum_param_id);
    nmo_object_t *flags_obj = nmo_object_repository_find_by_id(repo, flags_param_id);
    ASSERT_NOT_NULL(enum_obj);
    ASSERT_NOT_NULL(flags_obj);

    nmo_parameter_state_t *enum_state = nmo_parameter_get_mutable_state(enum_obj);
    nmo_parameter_state_t *flags_state = nmo_parameter_get_mutable_state(flags_obj);
    ASSERT_NOT_NULL(enum_state);
    ASSERT_NOT_NULL(flags_state);

    enum_state->type_guid = enum_guid;
    enum_state->mode = CKPARAM_MODE_BUFFER;
    enum_state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(&enum_state->buffer_data,
                              sizeof(uint8_t),
                              sizeof(int32_t),
                              NULL));
    memset(enum_state->buffer_data.data, 0, enum_state->buffer_data.count);

    flags_state->type_guid = flags_guid;
    flags_state->mode = CKPARAM_MODE_BUFFER;
    flags_state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(&flags_state->buffer_data,
                              sizeof(uint8_t),
                              sizeof(uint32_t),
                              NULL));
    memset(flags_state->buffer_data.data, 0, flags_state->buffer_data.count);

    workspace_edit_scope_t commit_scope = {0};
    nmo_workspace_edit_t *commit_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "enum flag values", &commit_scope, &commit_edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  commit_edit, enum_param_id, "CKBEHAVIORTYPE_BEHAVIOR"));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  commit_edit, flags_param_id, "CKBEHAVIOR_ACTIVE|CKBEHAVIOR_TARGETABLE"));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&commit_scope));

    ASSERT_EQ(4, *(const int32_t *)enum_state->buffer_data.data);
    ASSERT_EQ(0x00040001u, *(const uint32_t *)flags_state->buffer_data.data);

    workspace_edit_scope_t numeric_scope = {0};
    nmo_workspace_edit_t *numeric_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "numeric enum flag values", &numeric_scope, &numeric_edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  numeric_edit, enum_param_id, "1"));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  numeric_edit, flags_param_id, "0x3"));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&numeric_scope));

    ASSERT_EQ(1, *(const int32_t *)enum_state->buffer_data.data);
    ASSERT_EQ(3u, *(const uint32_t *)flags_state->buffer_data.data);

    workspace_edit_scope_t invalid_scope = {0};
    nmo_workspace_edit_t *invalid_edit = NULL;
    ASSERT_EQ(NMO_OK,
              begin_workspace_edit_for_session(
                  ctx, session, "invalid enum value", &invalid_scope, &invalid_edit));
    ASSERT_NE(NMO_OK,
              nmo_object_edit_set_parameter_value(
                  invalid_edit, enum_param_id, "MissingName"));
    rollback_workspace_edit_scope(&invalid_scope);
    ASSERT_EQ(1, *(const int32_t *)enum_state->buffer_data.data);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, parameter_bytes_commit_zero_fills_and_rollback_restores) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t param_id = 0;
    create_object_or_fail(session, NMO_CID_PARAMETER, "param-bytes", &param_id);
    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_INT;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, sizeof(uint8_t), 4, NULL));
    uint8_t initial[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    memcpy(state->buffer_data.data, initial, sizeof(initial));

    uint8_t first_write[2] = {0x01, 0x02};
    workspace_edit_scope_t commit_edit_scope = {0};
    nmo_workspace_edit_t *commit_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "parameter bytes commit", &commit_edit_scope, &commit_edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_bytes(
                  commit_edit, param_id, first_write, sizeof(first_write)));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&commit_edit_scope));
    uint8_t expected_commit[4] = {0x01, 0x02, 0x00, 0x00};
    ASSERT_EQ(0, memcmp(state->buffer_data.data, expected_commit, sizeof(expected_commit)));

    uint8_t rollback_write[4] = {0x10, 0x20, 0x30, 0x40};
    workspace_edit_scope_t rollback_edit_scope = {0};
    nmo_workspace_edit_t *rollback_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "parameter bytes rollback", &rollback_edit_scope, &rollback_edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_bytes(
                  rollback_edit, param_id, rollback_write, sizeof(rollback_write)));
    rollback_workspace_edit_scope(&rollback_edit_scope);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, expected_commit, sizeof(expected_commit)));

    uint8_t oversize_write[5] = {0, 1, 2, 3, 4};
    workspace_edit_scope_t oversize_edit_scope = {0};
    nmo_workspace_edit_t *oversize_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "parameter bytes oversize", &oversize_edit_scope, &oversize_edit));
    ASSERT_EQ(NMO_ERR_OUT_OF_BOUNDS,
              nmo_object_edit_set_parameter_bytes(
                  oversize_edit, param_id, oversize_write, sizeof(oversize_write)));
    rollback_workspace_edit_scope(&oversize_edit_scope);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, expected_commit, sizeof(expected_commit)));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, parameterout_object_mode_commit_sets_reference) {
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

    int before_edges = ref_graph_edge_count(session);

    char target_text[32];
    snprintf(target_text, sizeof(target_text), "#%u", target_id);
    workspace_edit_scope_t edit_scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "parameterout object", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_parameter_value(edit, param_id, target_text));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&edit_scope));
    ASSERT_EQ(target_id, state->object_id);
    ASSERT_TRUE(ref_graph_edge_count(session) > before_edges);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, dataarray_ref_graph_handles_missing_row_metadata) {
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

TEST(workspace_edit, dataarray_object_cell_commit_and_rollback) {
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
    snprintf(target_text, sizeof(target_text), "object:%u", target_id);

    workspace_edit_scope_t commit_edit_scope = {0};

    nmo_workspace_edit_t *commit_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "dataarray commit", &commit_edit_scope, &commit_edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_dataarray_cell(commit_edit, dataarray_id, 0, 0, target_text));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&commit_edit_scope));
    ASSERT_EQ(target_id, state->rows[0].cells[0].object_id);
    ASSERT_TRUE(ref_graph_edge_count(session) > before_edges);

    workspace_edit_scope_t rollback_edit_scope = {0};

    nmo_workspace_edit_t *rollback_edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "dataarray rollback", &rollback_edit_scope, &rollback_edit));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_dataarray_cell(rollback_edit, dataarray_id, 0, 0, "#0"));
    rollback_workspace_edit_scope(&rollback_edit_scope);
    ASSERT_EQ(target_id, state->rows[0].cells[0].object_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, behavior_graph_flag_rebuilds_behavior_index) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t parent_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(session, NMO_CID_BEOBJECT, "script-owner", &owner_id);
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "parent-behavior", &parent_id);
    create_object_or_fail(session, NMO_CID_BEHAVIOR, "child-behavior", &child_id);

    nmo_object_t *owner_obj = nmo_object_repository_find_by_id(repo, owner_id);
    ASSERT_NOT_NULL(owner_obj);
    nmo_beobject_state_t *owner_state =
        (nmo_beobject_state_t *)nmo_object_get_state(owner_obj);
    ASSERT_NOT_NULL(owner_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&owner_state->script_ids, &parent_id));

    nmo_behavior_index_t *before_index = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(before_index);
    size_t before_count = nmo_behavior_index_count(before_index);

    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_id);
    ASSERT_NOT_NULL(parent_obj);
    nmo_behavior_state_t *parent_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    ASSERT_NOT_NULL(parent_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&parent_state->sub_behaviors, &child_id));

    ASSERT_EQ(before_count, nmo_behavior_index_count(nmo_session_get_behavior_index(session)));
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_EQ(NMO_OK,
              nmo_workspace_apply_edit_flags(workspace, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH));
    nmo_behavior_index_t *after_index = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(after_index);
    ASSERT_TRUE(nmo_behavior_index_count(after_index) > before_count);

    nmo_workspace_destroy(workspace);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, apply_edit_flags_accepts_known_flags_and_rejects_unknown) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_ref_graph_t *initial_graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(initial_graph);
    ASSERT_EQ(NMO_OK,
              nmo_workspace_apply_edit_flags(workspace, NMO_WORKSPACE_EDIT_REFERENCES));
    nmo_ref_graph_t *after_reference_graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(after_reference_graph);

    ASSERT_EQ(NMO_OK,
              nmo_workspace_apply_edit_flags(workspace, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH));
    ASSERT_NOT_NULL(nmo_session_get_ref_graph(session));

    ASSERT_EQ(NMO_OK,
              nmo_workspace_apply_edit_flags(workspace, NMO_WORKSPACE_EDIT_NAMES));
    ASSERT_EQ(NMO_OK,
              nmo_workspace_apply_edit_flags(workspace, NMO_WORKSPACE_EDIT_RESOURCES));

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_workspace_apply_edit_flags(workspace, 1u << 31));
    ASSERT_NOT_NULL(after_reference_graph);

    nmo_workspace_destroy(workspace);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, snapshot_bytes_rollback_restores_direct_state) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    uint32_t direct_state[3] = {0x11111111u, 0x22222222u, 0x33333333u};
    workspace_edit_scope_t edit_scope = {0};
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "direct snapshot", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK,
              nmo_workspace_edit_snapshot_bytes(edit, direct_state, sizeof(direct_state)));

    direct_state[0] = 0xaaaaaaaau;
    direct_state[1] = 0xbbbbbbbbu;
    direct_state[2] = 0xccccccccu;
    rollback_workspace_edit_scope(&edit_scope);

    ASSERT_EQ(0x11111111u, direct_state[0]);
    ASSERT_EQ(0x22222222u, direct_state[1]);
    ASSERT_EQ(0x33333333u, direct_state[2]);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, track_created_object_rollback_removes_and_commit_keeps) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    workspace_edit_scope_t rollback_edit_scope = {0};

    nmo_workspace_edit_t *rollback_edit = NULL;
    nmo_object_id_t rollback_id = 0;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "created rollback", &rollback_edit_scope, &rollback_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, NMO_CID_3DENTITY, "created-rollback",
                  (nmo_guid_t){0, 0}, &rollback_id, NULL));
    ASSERT_TRUE(rollback_id != 0);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_track_created_object(rollback_edit, rollback_id));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, rollback_id));
    rollback_workspace_edit_scope(&rollback_edit_scope);
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, rollback_id));

    workspace_edit_scope_t commit_edit_scope = {0};

    nmo_workspace_edit_t *commit_edit = NULL;
    nmo_object_id_t commit_id = 0;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "created commit", &commit_edit_scope, &commit_edit));
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, NMO_CID_3DENTITY, "created-commit",
                  (nmo_guid_t){0, 0}, &commit_id, NULL));
    ASSERT_TRUE(commit_id != 0);
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_track_created_object(commit_edit, commit_id));
    ASSERT_EQ(NMO_OK, commit_workspace_edit_scope(&commit_edit_scope));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, commit_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, snapshot_object_chunk_rollback_restores_previous_chunk) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t object_id = 0;
    create_object_or_fail(session, NMO_CID_MESH, "mesh", &object_id);
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    ASSERT_NOT_NULL(object);

    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_chunk_t *old_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(old_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(old_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(old_chunk, 0x11111111u));
    nmo_chunk_close(old_chunk);
    ASSERT_EQ(NMO_OK, nmo_object_set_chunk(object, old_chunk));

    workspace_edit_scope_t edit_scope = {0};

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "chunk rollback", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_snapshot_object_chunk(edit, object_id));

    nmo_chunk_t *new_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(new_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(new_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(new_chunk, 0x22222222u));
    nmo_chunk_close(new_chunk);
    ASSERT_EQ(NMO_OK, nmo_object_set_chunk(object, new_chunk));
    rollback_workspace_edit_scope(&edit_scope);

    nmo_chunk_t *restored = nmo_object_get_chunk(object);
    ASSERT_NOT_NULL(restored);
    size_t data_size = 0;
    const uint32_t *data = (const uint32_t *)nmo_chunk_get_data(restored, &data_size);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(sizeof(uint32_t), data_size);
    ASSERT_EQ(0x11111111u, data[0]);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(workspace_edit, snapshot_object_chunk_rollback_restores_null_chunk) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t object_id = 0;
    create_object_or_fail(session, NMO_CID_MESH, "mesh", &object_id);
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    ASSERT_NOT_NULL(object);
    ASSERT_NULL(nmo_object_get_chunk(object));

    workspace_edit_scope_t edit_scope = {0};

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, begin_workspace_edit_for_session(ctx, session, "chunk null rollback", &edit_scope, &edit));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_snapshot_object_chunk(edit, object_id));

    nmo_chunk_t *new_chunk = nmo_chunk_create(nmo_session_get_arena(session));
    ASSERT_NOT_NULL(new_chunk);
    ASSERT_EQ(NMO_OK, nmo_object_set_chunk(object, new_chunk));
    rollback_workspace_edit_scope(&edit_scope);

    ASSERT_NULL(nmo_object_get_chunk(object));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(workspace_edit, begin_commit_roundtrip);
REGISTER_TEST(workspace_edit, set_reference_field_commit_invalidates_ref_graph);
REGISTER_TEST(workspace_edit, set_reference_field_rollback_restores_without_invalidating_cache);
REGISTER_TEST(workspace_edit, add_behavior_link_rollback_removes_created_link);
REGISTER_TEST(workspace_edit, add_behavior_link_commit_invalidates_ref_graph);
REGISTER_TEST(workspace_edit, remove_behavior_link_rollback_restores_parent_array);
REGISTER_TEST(workspace_edit, remove_behavior_link_commit_destroys_link);
REGISTER_TEST(workspace_edit, remove_behavior_link_commit_runs_delete_hooks);
REGISTER_TEST(workspace_edit, mark_behavior_interface_requires_interface_data);
REGISTER_TEST(workspace_edit, rename_object_commit_rebuilds_name_index);
REGISTER_TEST(workspace_edit, rename_object_rollback_restores_name_without_rebuilding_index);
REGISTER_TEST(workspace_edit, parameter_edit_rollback_restores_buffer);
REGISTER_TEST(workspace_edit, value_writer_resizes_string_parameter_and_nul_terminates);
REGISTER_TEST(workspace_edit, value_writer_raw_bytes_requires_explicit_resize);
REGISTER_TEST(workspace_edit, value_writer_resize_rollback_restores_buffer_size);
REGISTER_TEST(workspace_edit, value_writer_writes_object_refs_and_rejects_invalid_text);
REGISTER_TEST(workspace_edit, value_writer_writes_manager_refs_and_rejects_invalid_text);
REGISTER_TEST(workspace_edit, value_writer_resolves_message_manager_names_with_policy);
REGISTER_TEST(workspace_edit, value_writer_uses_manager_entry_key_for_lookup);
REGISTER_TEST(workspace_edit, value_writer_rejects_unsupported_manager_entry_kind);
REGISTER_TEST(workspace_edit, value_writer_accepts_message_manager_guid_option);
REGISTER_TEST(workspace_edit, value_writer_writes_structured_parameter_values);
REGISTER_TEST(workspace_edit, value_writer_writes_enum_and_flag_parameter_values);
REGISTER_TEST(workspace_edit, parameter_bytes_commit_zero_fills_and_rollback_restores);
REGISTER_TEST(workspace_edit, parameterout_object_mode_commit_sets_reference);
REGISTER_TEST(workspace_edit, dataarray_object_cell_commit_and_rollback);
REGISTER_TEST(workspace_edit, dataarray_ref_graph_handles_missing_row_metadata);
REGISTER_TEST(workspace_edit, behavior_graph_flag_rebuilds_behavior_index);
REGISTER_TEST(workspace_edit, apply_edit_flags_accepts_known_flags_and_rejects_unknown);
REGISTER_TEST(workspace_edit, snapshot_bytes_rollback_restores_direct_state);
REGISTER_TEST(workspace_edit, track_created_object_rollback_removes_and_commit_keeps);
REGISTER_TEST(workspace_edit, snapshot_object_chunk_rollback_restores_previous_chunk);
REGISTER_TEST(workspace_edit, snapshot_object_chunk_rollback_restores_null_chunk);
TEST_MAIN_END()






