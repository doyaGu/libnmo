#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_group_schemas.h"
#include "session/nmo_object_system.h"
#include "session/nmo_load_session.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "core/nmo_allocator.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"

static nmo_object_id_t g_runtime_delete_probe_id = 0;
static int g_runtime_post_delete_called = 0;
static int g_runtime_post_delete_after_remove = 0;
static nmo_object_id_t g_runtime_scene_probe_id = 0;
static int g_runtime_scene_post_delete_called = 0;
static int g_runtime_scene_post_delete_after_remove = 0;
static int g_runtime_finalize_prepare_calls = 0;

static int runtime_contains_id(const nmo_object_id_t *ids, size_t count, nmo_object_id_t id)
{
    for (size_t i = 0; i < count; i++) {
        if (ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

static nmo_status_t runtime_create_fail_hook(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    return NMO_ERR_INVALID_STATE;
}

static nmo_status_t runtime_deserialize_fail_hook(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)chunk;
    (void)type;
    (void)context;
    return NMO_ERR_INVALID_FORMAT;
}

static nmo_status_t runtime_prepare_probe_hook(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    g_runtime_finalize_prepare_calls++;
    return NMO_OK;
}

static void runtime_group_set_members(
    nmo_object_t *group_obj,
    const nmo_object_id_t *member_ids,
    size_t member_count)
{
    ASSERT_NOT_NULL(group_obj);
    ASSERT_NOT_NULL(group_obj->state);

    nmo_group_state_t *group_state = (nmo_group_state_t *)group_obj->state;
    nmo_array_clear(&group_state->object_ids);

    if (member_count == 0) {
        return;
    }

    ASSERT_EQ(NMO_OK, nmo_array_reserve(&group_state->object_ids, member_count));
    nmo_object_id_t *ids = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&group_state->object_ids, member_count, (void **)&ids));
    ASSERT_NOT_NULL(ids);

    for (size_t i = 0; i < member_count; i++) {
        ids[i] = member_ids[i];
    }
}

static void runtime_delete_probe_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL || context == NULL) {
        return;
    }

    g_runtime_post_delete_called = 1;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;
    if (nmo_object_repository_find_by_id(repo, g_runtime_delete_probe_id) == NULL) {
        g_runtime_post_delete_after_remove = 1;
    }
}

static void runtime_scene_delete_probe_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL || context == NULL) {
        return;
    }

    g_runtime_scene_post_delete_called = 1;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;
    if (nmo_object_repository_find_by_id(repo, g_runtime_scene_probe_id) == NULL) {
        g_runtime_scene_post_delete_after_remove = 1;
    }
}

TEST(runtime_kernel, execute_create_and_delete) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_runtime_report_t report = {0};
    nmo_object_id_t created_id = 0;
    int create_result = nmo_session_create_object(
        session,
        1,
        "runtime-kernel-object",
        (nmo_guid_t){0, 0},
        &created_id,
        &report);
    ASSERT_EQ(NMO_OK, create_result);
    ASSERT_TRUE(created_id != 0);
    ASSERT_EQ(1u, report.created_objects);

    int delete_result = nmo_session_destroy_objects(
        session,
        &created_id,
        1,
        NMO_RUNTIME_REQUEST_STRICT,
        &report);
    ASSERT_EQ(NMO_OK, delete_result);
    ASSERT_EQ(1u, report.deleted_objects);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, invalid_execute_arguments) {
    int result = nmo_session_execute(NULL, NULL, NULL);
    ASSERT_NE(NMO_OK, result);
}

TEST(runtime_kernel, post_delete_runs_after_remove) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_descriptor_t *object_type =
        nmo_type_registry_find_by_class_id_inherited(registry, 1);
    ASSERT_NOT_NULL(object_type);
    ASSERT_NOT_NULL(object_type->vtable);

    nmo_type_vtable_t *mutable_vtable = (nmo_type_vtable_t *)(void *)object_type->vtable;
    nmo_type_post_delete_fn old_post_delete = mutable_vtable->post_delete;
    mutable_vtable->post_delete = runtime_delete_probe_post_delete;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    g_runtime_delete_probe_id = 0;
    g_runtime_post_delete_called = 0;
    g_runtime_post_delete_after_remove = 0;

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session,
                                  1,
                                  "delete-probe",
                                  (nmo_guid_t){0, 0},
                                  &g_runtime_delete_probe_id,
                                  &report));
    ASSERT_TRUE(g_runtime_delete_probe_id != 0);

    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(session,
                                    &g_runtime_delete_probe_id,
                                    1,
                                    NMO_RUNTIME_REQUEST_STRICT,
                                    &report));

    mutable_vtable->post_delete = old_post_delete;
    ASSERT_TRUE(g_runtime_post_delete_called != 0);
    ASSERT_TRUE(g_runtime_post_delete_after_remove != 0);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, post_delete_runs_after_remove_non_object_type) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_descriptor_t *scene_type =
        nmo_type_registry_find_by_class_id_inherited(registry, 10);
    ASSERT_NOT_NULL(scene_type);
    ASSERT_NOT_NULL(scene_type->vtable);

    nmo_type_vtable_t *mutable_vtable = (nmo_type_vtable_t *)(void *)scene_type->vtable;
    nmo_type_post_delete_fn old_post_delete = mutable_vtable->post_delete;
    mutable_vtable->post_delete = runtime_scene_delete_probe_post_delete;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    g_runtime_scene_probe_id = 0;
    g_runtime_scene_post_delete_called = 0;
    g_runtime_scene_post_delete_after_remove = 0;

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session,
                                  10,
                                  "delete-probe-scene",
                                  (nmo_guid_t){0, 0},
                                  &g_runtime_scene_probe_id,
                                  &report));
    ASSERT_TRUE(g_runtime_scene_probe_id != 0);

    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(session,
                                    &g_runtime_scene_probe_id,
                                    1,
                                    NMO_RUNTIME_REQUEST_STRICT,
                                    &report));

    mutable_vtable->post_delete = old_post_delete;
    ASSERT_TRUE(g_runtime_scene_post_delete_called != 0);
    ASSERT_TRUE(g_runtime_scene_post_delete_after_remove != 0);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, create_hook_failure_does_not_publish_object) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_descriptor_t *object_type =
        nmo_type_registry_find_by_class_id_inherited(registry, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(object_type);
    ASSERT_NOT_NULL(object_type->vtable);

    nmo_type_vtable_t *mutable_vtable = (nmo_type_vtable_t *)(void *)object_type->vtable;
    nmo_type_create_fn old_create = mutable_vtable->create;
    mutable_vtable->create = runtime_create_fail_hook;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t created_id = 0;
    nmo_runtime_report_t report = {0};
    int create_result = nmo_session_create_object(
        session, NMO_CID_OBJECT, "create-fail", (nmo_guid_t){0, 0}, &created_id, &report);

    mutable_vtable->create = old_create;

    ASSERT_EQ(NMO_ERR_INVALID_STATE, create_result);
    ASSERT_EQ(0u, created_id);
    ASSERT_EQ(0u, nmo_object_repository_get_count(nmo_session_get_repository(session)));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_preserves_internal_group_references) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t member_ids[2] = {0, 0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member-a", (nmo_guid_t){0, 0}, &member_ids[0], NULL));
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member-b", (nmo_guid_t){0, 0}, &member_ids[1], NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    runtime_group_set_members(group_obj, member_ids, 2);
    nmo_group_state_t *group_state = (nmo_group_state_t *)group_obj->state;
    ASSERT_NOT_NULL(group_state);
    ASSERT_EQ(sizeof(nmo_object_id_t), group_state->base.script_ids.element_size);
    ASSERT_EQ(sizeof(nmo_object_id_t), group_state->base.attribute_parameter_ids.element_size);
    ASSERT_EQ(sizeof(uint32_t), group_state->base.attribute_types.element_size);
    ASSERT_EQ(sizeof(nmo_chunk_t *), group_state->base.attribute_chunks.element_size);
    ASSERT_EQ(sizeof(uint8_t), group_state->base.legacy_attributes_raw.element_size);

    nmo_object_id_t copy_ids[3] = {group_id, member_ids[0], member_ids[1]};
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_copy_objects(session, copy_ids, 3, NMO_RUNTIME_REQUEST_STRICT, &report));
    ASSERT_EQ(3u, report.copied_objects);

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    ASSERT_EQ(NMO_OK, nmo_session_get_objects(session, &objects, &object_count));
    ASSERT_EQ(6u, object_count);

    nmo_object_id_t cloned_group_id = 0;
    nmo_object_id_t cloned_member_ids[2] = {0, 0};
    size_t cloned_member_count = 0;
    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects[i];
        ASSERT_NOT_NULL(obj);
        if (obj->id == group_id || obj->id == member_ids[0] || obj->id == member_ids[1]) {
            continue;
        }
        if (obj->class_id == NMO_CID_GROUP) {
            cloned_group_id = obj->id;
            continue;
        }
        if (obj->class_id == NMO_CID_OBJECT && cloned_member_count < 2) {
            cloned_member_ids[cloned_member_count++] = obj->id;
        }
    }

    ASSERT_TRUE(cloned_group_id != 0);
    ASSERT_EQ(2u, cloned_member_count);

    nmo_object_t *cloned_group = nmo_object_repository_find_by_id(repo, cloned_group_id);
    ASSERT_NOT_NULL(cloned_group);
    ASSERT_NOT_NULL(cloned_group->state);

    nmo_group_state_t *cloned_group_state = (nmo_group_state_t *)cloned_group->state;
    ASSERT_EQ(2u, cloned_group_state->object_ids.count);
    const nmo_object_id_t *group_refs =
        NMO_ARRAY_DATA(nmo_object_id_t, &cloned_group_state->object_ids);
    ASSERT_NOT_NULL(group_refs);

    ASSERT_TRUE(runtime_contains_id(group_refs, cloned_group_state->object_ids.count, cloned_member_ids[0]) != 0);
    ASSERT_TRUE(runtime_contains_id(group_refs, cloned_group_state->object_ids.count, cloned_member_ids[1]) != 0);
    ASSERT_TRUE(runtime_contains_id(group_refs, cloned_group_state->object_ids.count, member_ids[0]) == 0);
    ASSERT_TRUE(runtime_contains_id(group_refs, cloned_group_state->object_ids.count, member_ids[1]) == 0);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, delete_safe_detach_prunes_group_references) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member", (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    runtime_group_set_members(group_obj, &member_id, 1);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(
            session,
            &member_id,
            1,
            NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
            &report));
    ASSERT_EQ(1u, report.deleted_objects);

    nmo_object_t *remaining_group = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(remaining_group);
    ASSERT_NOT_NULL(remaining_group->state);
    nmo_group_state_t *group_state = (nmo_group_state_t *)remaining_group->state;
    ASSERT_EQ(0u, group_state->object_ids.count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, delete_cascade_removes_referencing_group) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member", (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    runtime_group_set_members(group_obj, &member_id, 1);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(
            session,
            &member_id,
            1,
            NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_CASCADE,
            &report));
    ASSERT_EQ(2u, report.deleted_objects);
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, member_id));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, group_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, deserialize_failure_does_not_publish_state_for_finalize) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_descriptor_t *object_type =
        nmo_type_registry_find_by_class_id_inherited(registry, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(object_type);
    ASSERT_NOT_NULL(object_type->vtable);

    nmo_type_vtable_t *mutable_vtable = (nmo_type_vtable_t *)(void *)object_type->vtable;
    nmo_type_deserialize_fn old_deserialize = mutable_vtable->deserialize;
    nmo_type_prepare_dependencies_fn old_prepare = mutable_vtable->prepare_dependencies;
    mutable_vtable->deserialize = runtime_deserialize_fail_hook;
    mutable_vtable->prepare_dependencies = runtime_prepare_probe_hook;

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_NOT_NULL(arena);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    ASSERT_NOT_NULL(type_rt);

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_object_t *obj = nmo_object_create(&alloc, NMO_OBJECT_ID_NONE, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(obj);
    nmo_object_t *repo_obj = obj;
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &obj));
    obj = repo_obj;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_set_data_version(chunk, 1);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 123));
    nmo_chunk_close(chunk);
    obj->chunk = chunk;

    nmo_load_session_t *load_session = nmo_load_session_start(repo, 1);
    ASSERT_NOT_NULL(load_session);
    ASSERT_EQ(NMO_OK, nmo_load_session_register(load_session, obj, 0));

    nmo_object_system_deserialize_stats_t stats = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_object_system_deserialize_loaded_objects(
            repo,
            type_rt,
            arena,
            NULL,
            NULL,
            0,
            NULL,
            load_session,
            1,
            &stats));
    ASSERT_EQ(1u, stats.errors);
    ASSERT_NULL(obj->state);
    ASSERT_EQ(0u, obj->state_size);
    ASSERT_NULL(obj->data);

    g_runtime_finalize_prepare_calls = 0;
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_runtime_kernel_finalize_load(session, NULL, &report));
    ASSERT_EQ(0, g_runtime_finalize_prepare_calls);

    mutable_vtable->deserialize = old_deserialize;
    mutable_vtable->prepare_dependencies = old_prepare;

    nmo_load_session_destroy(load_session);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_kernel, execute_create_and_delete);
REGISTER_TEST(runtime_kernel, invalid_execute_arguments);
REGISTER_TEST(runtime_kernel, post_delete_runs_after_remove);
REGISTER_TEST(runtime_kernel, post_delete_runs_after_remove_non_object_type);
REGISTER_TEST(runtime_kernel, create_hook_failure_does_not_publish_object);
REGISTER_TEST(runtime_kernel, copy_preserves_internal_group_references);
REGISTER_TEST(runtime_kernel, delete_safe_detach_prunes_group_references);
REGISTER_TEST(runtime_kernel, delete_cascade_removes_referencing_group);
REGISTER_TEST(runtime_kernel, deserialize_failure_does_not_publish_state_for_finalize);
TEST_MAIN_END()
