#include "test_framework.h"

#include "document/nmo_document.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "runtime/nmo_workspace.h"
#include "object/nmo_object_edit.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_analyze.h"
#include "object/nmo_class_ids.h"
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
    snprintf(target_text, sizeof(target_text), "#%u", target_id);

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






