#include "test_framework.h"

#include "../../src/behavior/behavior_internal.h"
#include "behavior/nmo_behavior_analyze.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"

/* Ballanced/Source/CK2 regressions 765778d and 895d6d6. SAFE_DETACH supplies
 * deletion notification semantics here; IO ownership comes from graph membership. */
typedef struct behavior_delete_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_object_repository_t *repo;
    nmo_object_id_t graph_id, source_id, destination_id, link_id;
    nmo_behavior_state_t *graph;
    nmo_behaviorlink_state_t *link;
} behavior_delete_fixture_t;

static nmo_status_t create_object(
    behavior_delete_fixture_t *f, nmo_class_id_t class_id, nmo_object_id_t *out_id)
{
    return nmo_session_create_object(f->session, class_id, NULL, NMO_NULL_GUID, out_id, NULL);
}

static nmo_status_t fixture_open(behavior_delete_fixture_t *f)
{
    f->ctx = nmo_context_create(NULL);
    if (f->ctx == NULL) return NMO_ERR_NOMEM;
    f->session = nmo_session_create(f->ctx);
    if (f->session == NULL) return NMO_ERR_NOMEM;
    f->repo = nmo_session_get_repository(f->session);
    NMO_RETURN_IF_ERROR(create_object(f, NMO_CID_BEHAVIOR, &f->graph_id));
    NMO_RETURN_IF_ERROR(create_object(f, NMO_CID_BEHAVIORIO, &f->source_id));
    NMO_RETURN_IF_ERROR(create_object(f, NMO_CID_BEHAVIORIO, &f->destination_id));
    NMO_RETURN_IF_ERROR(create_object(f, NMO_CID_BEHAVIORLINK, &f->link_id));
    f->graph = nmo_object_get_state(nmo_object_repository_find_by_id(f->repo, f->graph_id));
    f->link = nmo_object_get_state(nmo_object_repository_find_by_id(f->repo, f->link_id));
    NMO_RETURN_IF_ERROR(nmo_behavior_ref_array_append(&f->graph->inputs, f->source_id, NULL));
    NMO_RETURN_IF_ERROR(nmo_behavior_ref_array_append(
        &f->graph->outputs, f->destination_id, NULL));
    nmo_behaviorlink_set_in_io_id(f->link, f->source_id);
    nmo_behaviorlink_set_out_io_id(f->link, f->destination_id);
    f->link->activation_delay = 3;
    f->link->initial_activation_delay = 7;

    /* Removing membership parks a link without retiring its endpoints. */
    NMO_RETURN_IF_ERROR(nmo_behavior_ref_array_append(
        &f->graph->sub_behavior_links, f->link_id, NULL));
    return nmo_array_remove(&f->graph->sub_behavior_links, 0, NULL);
}

static void fixture_close(behavior_delete_fixture_t *f)
{
    nmo_session_destroy(f->session);
    nmo_context_release(f->ctx);
}

static nmo_status_t destroy_objects(
    behavior_delete_fixture_t *f, const nmo_object_id_t *ids, size_t count)
{
    return nmo_session_destroy_objects(f->session, ids, count,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH, NULL);
}

TEST(behavior_delete, parked_source_clears_only_source)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.source_id, 1));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    ASSERT_EQ(NMO_REF_NONE, f.link->in_io.state);
    ASSERT_EQ(0u, nmo_ref_serialized_id(&f.link->in_io));
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(f.link));
    ASSERT_EQ(0u, f.graph->inputs.count);
    ASSERT_EQ(1u, f.graph->outputs.count);
    ASSERT_EQ(3, f.link->activation_delay);
    ASSERT_EQ(7, f.link->initial_activation_delay);
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.link_id));
    fixture_close(&f);
}

TEST(behavior_delete, parked_destination_preserves_source_until_link_deletion)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.destination_id, 1));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.destination_id));
    ASSERT_EQ(NMO_REF_NONE, f.link->out_io.state);
    ASSERT_EQ(0u, nmo_ref_serialized_id(&f.link->out_io));
    ASSERT_EQ(f.source_id, nmo_behaviorlink_in_io_id(f.link));
    ASSERT_EQ(1u, f.graph->inputs.count);
    ASSERT_EQ(0u, f.graph->outputs.count);
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    fixture_close(&f);
}

TEST(behavior_delete, graph_and_endpoints_batch_clear_parked_link)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    /* libnmo takes an explicit deletion set. Its CASCADE follows incoming
     * references and would also retire the parked link. */
    nmo_object_id_t ids[] = {f.graph_id, f.source_id, f.destination_id};
    ASSERT_EQ(NMO_OK, destroy_objects(&f, ids, 3));
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, ids[i]));
    }
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.link_id));
    ASSERT_EQ(NMO_REF_NONE, f.link->in_io.state);
    ASSERT_EQ(NMO_REF_NONE, f.link->out_io.state);
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    fixture_close(&f);
}

TEST(behavior_delete, both_endpoints_batch_clear_parked_link)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    nmo_object_id_t ids[] = {f.source_id, f.destination_id};
    ASSERT_EQ(NMO_OK, destroy_objects(&f, ids, 2));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.destination_id));
    ASSERT_EQ(NMO_REF_NONE, f.link->in_io.state);
    ASSERT_EQ(NMO_REF_NONE, f.link->out_io.state);
    ASSERT_EQ(0u, f.graph->inputs.count);
    ASSERT_EQ(0u, f.graph->outputs.count);
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    fixture_close(&f);
}

TEST(behavior_delete, source_and_link_batch_preserve_unrelated_link)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    nmo_object_id_t other_source_id = 0, other_link_id = 0;
    ASSERT_EQ(NMO_OK, create_object(&f, NMO_CID_BEHAVIORIO, &other_source_id));
    ASSERT_EQ(NMO_OK, create_object(&f, NMO_CID_BEHAVIORLINK, &other_link_id));
    nmo_behaviorlink_state_t *other_link = nmo_object_get_state(
        nmo_object_repository_find_by_id(f.repo, other_link_id));
    nmo_behaviorlink_set_in_io_id(other_link, other_source_id);
    nmo_behaviorlink_set_out_io_id(other_link, f.destination_id);
    nmo_object_id_t ids[] = {f.source_id, f.link_id};
    ASSERT_EQ(NMO_OK, destroy_objects(&f, ids, 2));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.link_id));
    ASSERT_EQ(other_source_id, nmo_behaviorlink_in_io_id(other_link));
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(other_link));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &other_link_id, 1));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, other_source_id));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.destination_id));
    fixture_close(&f);
}

TEST(behavior_delete, previous_source_deletion_preserves_rebound_source)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    nmo_object_id_t current_source_id = 0;
    ASSERT_EQ(NMO_OK, create_object(&f, NMO_CID_BEHAVIORIO, &current_source_id));
    nmo_behaviorlink_set_in_io_id(f.link, current_source_id);
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.source_id, 1));
    ASSERT_EQ(current_source_id, nmo_behaviorlink_in_io_id(f.link));
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(f.link));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, current_source_id));
    fixture_close(&f);
}

TEST(behavior_delete, detached_io_owner_deletion_invalidates_cached_ownership)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    nmo_behavior_index_t *index = nmo_session_get_behavior_index(f.session);
    ASSERT_NOT_NULL(index);
    const nmo_port_owner_t *source_owner = nmo_behavior_index_find(index, f.source_id);
    const nmo_port_owner_t *destination_owner =
        nmo_behavior_index_find(index, f.destination_id);
    ASSERT_NOT_NULL(source_owner);
    ASSERT_NOT_NULL(destination_owner);
    ASSERT_EQ(f.graph_id, source_owner->owner_id);
    ASSERT_EQ(f.graph_id, destination_owner->owner_id);
    nmo_array_clear(&f.graph->inputs);
    nmo_array_clear(&f.graph->outputs);

    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.graph_id, 1));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.graph_id));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.destination_id));
    index = nmo_session_get_behavior_index(f.session);
    ASSERT_NOT_NULL(index);
    ASSERT_NULL(nmo_behavior_index_find(index, f.source_id));
    ASSERT_NULL(nmo_behavior_index_find(index, f.destination_id));
    ASSERT_EQ(f.source_id, nmo_behaviorlink_in_io_id(f.link));
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(f.link));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    nmo_object_id_t ids[] = {f.source_id, f.destination_id};
    ASSERT_EQ(NMO_OK, destroy_objects(&f, ids, 2));
    fixture_close(&f);
}

TEST(behavior_delete, graph_deletion_preserves_surviving_child_endpoints)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    nmo_object_id_t child_id = 0;
    ASSERT_EQ(NMO_OK, create_object(&f, NMO_CID_BEHAVIOR, &child_id));
    nmo_behavior_state_t *child = nmo_object_get_state(
        nmo_object_repository_find_by_id(f.repo, child_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &f.graph->sub_behaviors, child_id, NULL));
    nmo_array_clear(&f.graph->inputs);
    nmo_array_clear(&f.graph->outputs);
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&child->outputs, f.source_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&child->inputs, f.destination_id, NULL));
    /* Warm the index while the child still belongs to its parent graph. */
    nmo_behavior_index_t *index = nmo_session_get_behavior_index(f.session);
    ASSERT_NOT_NULL(index);
    ASSERT_NOT_NULL(nmo_behavior_index_find(index, child_id));

    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.graph_id, 1));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, child_id));
    index = nmo_session_get_behavior_index(f.session);
    ASSERT_NOT_NULL(index);
    ASSERT_NULL(nmo_behavior_index_find(index, child_id));
    const nmo_port_owner_t *source_owner = nmo_behavior_index_find(index, f.source_id);
    const nmo_port_owner_t *destination_owner =
        nmo_behavior_index_find(index, f.destination_id);
    ASSERT_NOT_NULL(source_owner);
    ASSERT_NOT_NULL(destination_owner);
    ASSERT_EQ(child_id, source_owner->owner_id);
    ASSERT_EQ(child_id, destination_owner->owner_id);
    ASSERT_EQ(f.source_id, nmo_behaviorlink_in_io_id(f.link));
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(f.link));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    fixture_close(&f);
}

TEST(behavior_delete, deferred_source_deletion_clears_endpoint_on_commit)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(f.session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f.ctx, document, &workspace));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "Delete parked source", &edit));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_defer_destroy_objects(edit, &f.source_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    ASSERT_EQ(f.source_id, nmo_behaviorlink_in_io_id(f.link));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    ASSERT_EQ(NMO_REF_NONE, f.link->in_io.state);
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(f.link));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    fixture_close(&f);
}

TEST(behavior_delete, deferred_source_deletion_rollback_preserves_endpoints)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(f.session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f.ctx, document, &workspace));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "Keep parked source", &edit));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_defer_destroy_objects(edit, &f.source_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH));
    nmo_workspace_edit_rollback(edit);
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    ASSERT_EQ(f.source_id, nmo_behaviorlink_in_io_id(f.link));
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(f.link));
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    fixture_close(&f);
}

TEST(behavior_delete, deletion_without_safe_detach_preserves_endpoint_values)
{
    behavior_delete_fixture_t f = {0};
    ASSERT_EQ(NMO_OK, fixture_open(&f));
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(f.session, &f.destination_id, 1,
        NMO_RUNTIME_REQUEST_STRICT, NULL));
    ASSERT_NULL(nmo_object_repository_find_by_id(f.repo, f.destination_id));
    ASSERT_EQ(f.source_id, nmo_behaviorlink_in_io_id(f.link));
    /* Compare stored IDs only: the target no longer exists. */
    ASSERT_EQ(f.destination_id, nmo_behaviorlink_out_io_id(f.link));
    ASSERT_EQ(f.destination_id, nmo_ref_serialized_id(&f.link->out_io));
    ASSERT_EQ(NMO_OK, destroy_objects(&f, &f.link_id, 1));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(f.repo, f.source_id));
    fixture_close(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(behavior_delete, parked_source_clears_only_source);
    REGISTER_TEST(behavior_delete, parked_destination_preserves_source_until_link_deletion);
    REGISTER_TEST(behavior_delete, graph_and_endpoints_batch_clear_parked_link);
    REGISTER_TEST(behavior_delete, both_endpoints_batch_clear_parked_link);
    REGISTER_TEST(behavior_delete, source_and_link_batch_preserve_unrelated_link);
    REGISTER_TEST(behavior_delete, previous_source_deletion_preserves_rebound_source);
    REGISTER_TEST(behavior_delete, detached_io_owner_deletion_invalidates_cached_ownership);
    REGISTER_TEST(behavior_delete, graph_deletion_preserves_surviving_child_endpoints);
    REGISTER_TEST(behavior_delete, deferred_source_deletion_clears_endpoint_on_commit);
    REGISTER_TEST(behavior_delete, deferred_source_deletion_rollback_preserves_endpoints);
    REGISTER_TEST(behavior_delete, deletion_without_safe_detach_preserves_endpoint_values);
TEST_MAIN_END()
