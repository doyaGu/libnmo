#include "test_framework.h"
#include "runtime/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_system.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_interfaceobjectmanager_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/builtin/nmo_messagemanager_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_synchro_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_id_mapping.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "format/nmo_object.h"
#include "core/nmo_allocator.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"

static int test_id_lookup(void *ctx, nmo_object_id_t file_index,
                          nmo_object_id_t *out_id) {
    return nmo_id_mapping_get_runtime_id(
        (const nmo_id_mapping_t *)ctx, file_index, out_id);
}

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
    nmo_ref_t *refs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&group_state->object_ids, member_count, (void **)&refs));
    ASSERT_NOT_NULL(refs);

    for (size_t i = 0; i < member_count; i++) {
        refs[i] = nmo_ref_from_id(member_ids[i]);
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
    ASSERT_EQ(sizeof(nmo_ref_t), group_state->base.scripts.element_size);
    ASSERT_EQ(sizeof(nmo_beobject_attribute_t),
              group_state->base.attributes.element_size);

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
    const nmo_ref_t *group_refs =
        NMO_ARRAY_DATA(nmo_ref_t, &cloned_group_state->object_ids);
    ASSERT_NOT_NULL(group_refs);

    ASSERT_EQ(cloned_member_ids[0], nmo_ref_runtime_id(&group_refs[0]));
    ASSERT_EQ(cloned_member_ids[1], nmo_ref_runtime_id(&group_refs[1]));
    ASSERT_TRUE(runtime_contains_id(member_ids, 2, nmo_ref_runtime_id(&group_refs[0])) == 0);
    ASSERT_TRUE(runtime_contains_id(member_ids, 2, nmo_ref_runtime_id(&group_refs[1])) == 0);

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

TEST(runtime_kernel, delete_safe_detach_prunes_behavior_links_with_deleted_io) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t source_io_id = 0;
    nmo_object_id_t target_io_id = 0;
    nmo_object_id_t link_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_BEHAVIOR, "behavior", (nmo_guid_t){0, 0}, &behavior_id, NULL));
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_BEHAVIORIO, "source", (nmo_guid_t){0, 0}, &source_io_id, NULL));
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_BEHAVIORIO, "target", (nmo_guid_t){0, 0}, &target_io_id, NULL));
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_BEHAVIORLINK, "link", (nmo_guid_t){0, 0}, &link_id, NULL));

    nmo_object_t *behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_NOT_NULL(link_obj);
    nmo_behavior_state_t *behavior_state = (nmo_behavior_state_t *)behavior_obj->state;
    nmo_behaviorlink_state_t *link_state = (nmo_behaviorlink_state_t *)link_obj->state;
    ASSERT_NOT_NULL(behavior_state);
    ASSERT_NOT_NULL(link_state);
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior_state->sub_behavior_links, link_id, NULL));
    nmo_behaviorlink_set_in_io_id(link_state, target_io_id);
    nmo_behaviorlink_set_out_io_id(link_state, source_io_id);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(
            session,
            &target_io_id,
            1,
            NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
            &report));
    ASSERT_EQ(1u, report.deleted_objects);

    behavior_obj = nmo_object_repository_find_by_id(repo, behavior_id);
    link_obj = nmo_object_repository_find_by_id(repo, link_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_NOT_NULL(link_obj);
    behavior_state = (nmo_behavior_state_t *)behavior_obj->state;
    link_state = (nmo_behaviorlink_state_t *)link_obj->state;
    ASSERT_NOT_NULL(behavior_state);
    ASSERT_NOT_NULL(link_state);
    ASSERT_EQ(0u, behavior_state->sub_behavior_links.count);
    ASSERT_EQ(0u, nmo_behaviorlink_in_io_id(link_state));
    ASSERT_EQ(source_io_id, nmo_behaviorlink_out_io_id(link_state));

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

    nmo_id_mapping_t *load_session = nmo_id_mapping_create(repo, 1);
    ASSERT_NOT_NULL(load_session);
    ASSERT_EQ(NMO_OK, nmo_id_mapping_register(load_session, obj, 0));

    nmo_object_system_deserialize_stats_t stats = {0};
    nmo_load_diagnostics_t diagnostics;
    nmo_load_diagnostics_init(&diagnostics);
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
            test_id_lookup,
            load_session,
            1,
            &diagnostics,
            &stats));
    ASSERT_EQ(1u, stats.errors);
    ASSERT_EQ((size_t)1, diagnostics.count);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, diagnostics.issues[0].status);
    ASSERT_EQ(obj->id, diagnostics.issues[0].object_id);
    ASSERT_NULL(obj->state);
    ASSERT_EQ(0u, obj->state_size);

    g_runtime_finalize_prepare_calls = 0;
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_runtime_kernel_finalize_load(session, NULL, &report));
    ASSERT_EQ(0, g_runtime_finalize_prepare_calls);

    mutable_vtable->deserialize = old_deserialize;
    mutable_vtable->prepare_dependencies = old_prepare;

    nmo_id_mapping_destroy(load_session);
    nmo_load_diagnostics_destroy(&diagnostics);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_removes_only_invalid_reference_records) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t valid_a = 0, valid_b = 0, behavior_id = 0, group_id = 0;
    nmo_object_id_t grid_id = 0;
    nmo_object_id_t keyed_id = 0;
    nmo_object_id_t valid_animation_a = 0, valid_animation_b = 0;
    nmo_object_id_t valid_parameter = 0, valid_parameter_in = 0;
    nmo_object_id_t parameter_out_id = 0;
    nmo_object_id_t entity3d_id = 0, valid_mesh = 0;
    nmo_object_id_t synchro_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "a", (nmo_guid_t){0, 0}, &valid_a, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "b", (nmo_guid_t){0, 0}, &valid_b, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "behavior", (nmo_guid_t){0, 0},
        &behavior_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0}, &group_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GRID, "grid", (nmo_guid_t){0, 0}, &grid_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_KEYEDANIMATION, "keyed", (nmo_guid_t){0, 0},
        &keyed_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECTANIMATION, "animation-a", (nmo_guid_t){0, 0},
        &valid_animation_a, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECTANIMATION, "animation-b", (nmo_guid_t){0, 0},
        &valid_animation_b, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "parameter", (nmo_guid_t){0, 0},
        &valid_parameter, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETERIN, "parameter-in", (nmo_guid_t){0, 0},
        &valid_parameter_in, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETEROUT, "parameter-out", (nmo_guid_t){0, 0},
        &parameter_out_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "entity", (nmo_guid_t){0, 0},
        &entity3d_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_MESH, "mesh", (nmo_guid_t){0, 0},
        &valid_mesh, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_SYNCHRO, "synchro", (nmo_guid_t){0, 0},
        &synchro_id, NULL));

    nmo_behavior_state_t *behavior = (nmo_behavior_state_t *)
        nmo_object_repository_find_by_id(repo, behavior_id)->state;
    nmo_group_state_t *group = (nmo_group_state_t *)
        nmo_object_repository_find_by_id(repo, group_id)->state;
    nmo_grid_state_t *grid = (nmo_grid_state_t *)
        nmo_object_repository_find_by_id(repo, grid_id)->state;
    nmo_keyedanimation_state_t *keyed = (nmo_keyedanimation_state_t *)
        nmo_object_repository_find_by_id(repo, keyed_id)->state;
    nmo_3dentity_state_t *entity3d = (nmo_3dentity_state_t *)
        nmo_object_repository_find_by_id(repo, entity3d_id)->state;
    nmo_synchro_state_t *synchro = (nmo_synchro_state_t *)
        nmo_object_repository_find_by_id(repo, synchro_id)->state;
    nmo_parameterout_state_t *parameter_out = (nmo_parameterout_state_t *)
        nmo_object_repository_find_by_id(repo, parameter_out_id)->state;
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(group);
    ASSERT_NOT_NULL(grid);
    ASSERT_NOT_NULL(keyed);
    ASSERT_NOT_NULL(entity3d);
    ASSERT_NOT_NULL(synchro);
    ASSERT_NOT_NULL(parameter_out);
    nmo_object_id_t invalid = 0x7FFFFFF0u;
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->inputs, valid_a, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->inputs, invalid, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->inputs, valid_b, NULL));
    behavior->target_parameter_id = valid_parameter;

    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *chunk_a = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *chunk_invalid = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *chunk_b = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(chunk_a);
    ASSERT_NOT_NULL(chunk_invalid);
    ASSERT_NOT_NULL(chunk_b);
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->sub_behaviors, valid_a, chunk_a));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->sub_behaviors, invalid, chunk_invalid));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &behavior->sub_behaviors, valid_b, chunk_b));

    nmo_object_id_t keyed_ids[] = {
        valid_animation_a, invalid, valid_animation_b
    };
    nmo_keyedanimation_subanim_t subanims[3] = {
        {.object_id = 101}, {.object_id = 202}, {.object_id = 303},
    };
    keyed->animation_ids = keyed_ids;
    keyed->animation_count = 3;
    keyed->subanims = subanims;
    keyed->subanim_count = 3;

    nmo_ref_t entity_meshes[] = {
        nmo_ref_from_id(valid_mesh),
        nmo_ref_from_raw(invalid),
        nmo_ref_from_id(valid_a),
    };
    entity3d->mesh_ids = entity_meshes;
    entity3d->mesh_count = 3;
    nmo_ref_t entity_animations[] = {
        nmo_ref_from_id(valid_animation_a),
        nmo_ref_from_raw(invalid),
        nmo_ref_from_id(valid_animation_b),
    };
    entity3d->animation_ids = entity_animations;
    entity3d->animation_count = 3;

    uint32_t type_a = 11, type_invalid = 22, type_b = 33;
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &group->base.attributes, valid_parameter, type_a, NULL));
    nmo_beobject_attribute_t invalid_attribute = {
        .parameter = nmo_ref_from_raw(invalid),
        .type_id = type_invalid,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &group->base.attributes, &invalid_attribute));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &group->base.attributes, valid_parameter_in, type_b, NULL));
    nmo_beobject_legacy_attribute_t legacy_attributes[] = {
        {
            .compatible_class_id = 1,
            .name = "valid-a",
            .parameter = nmo_ref_from_id(valid_parameter),
        },
        {
            .compatible_class_id = 2,
            .name = "unresolved",
            .parameter = nmo_ref_from_raw(invalid),
        },
        {
            .compatible_class_id = 3,
            .name = "wrong-class",
            .parameter = nmo_ref_from_id(valid_a),
        },
        {
            .compatible_class_id = 4,
            .name = "valid-b",
            .parameter = nmo_ref_from_id(valid_parameter_in),
        },
    };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(
        &group->base.legacy_attributes, legacy_attributes, 4));
    group->base.has_legacy_attributes = 1;
    nmo_grid_layer_t valid_layer = {
        .ref = {.raw_id = valid_a, .id = valid_a, .state = NMO_REF_RESOLVED},
    };
    nmo_grid_layer_t invalid_layer = {
        .ref = {.raw_id = invalid, .id = 0, .state = NMO_REF_UNRESOLVED},
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&grid->layers, &valid_layer));
    ASSERT_EQ(NMO_OK, nmo_array_append(&grid->layers, &invalid_layer));
    nmo_ref_t valid_ref_a = nmo_ref_from_id(valid_a);
    nmo_ref_t valid_ref_b = nmo_ref_from_id(valid_b);
    nmo_ref_t invalid_ref = nmo_ref_from_raw(invalid);
    ASSERT_EQ(NMO_OK, nmo_array_append(&synchro->arrived_ids, &valid_ref_a));
    ASSERT_EQ(NMO_OK, nmo_array_append(&synchro->arrived_ids, &invalid_ref));
    ASSERT_EQ(NMO_OK, nmo_array_append(&synchro->arrived_ids, &valid_ref_b));
    ASSERT_EQ(NMO_OK, nmo_array_append(&synchro->passed_ids, &invalid_ref));
    ASSERT_EQ(NMO_OK, nmo_array_append(&synchro->passed_ids, &valid_ref_b));
    nmo_ref_t parameter_destinations[] = {
        nmo_ref_from_id(valid_parameter_in),
        nmo_ref_from_raw(invalid),
        nmo_ref_from_id(valid_parameter),
    };
    parameter_out->owner = nmo_ref_from_id(valid_parameter);
    parameter_out->destination_ids = parameter_destinations;
    parameter_out->destination_count = 3;

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(15, (int)changed);
    ASSERT_EQ(2, (int)behavior->inputs.count);
    ASSERT_EQ(valid_a, nmo_behavior_ref_array_get_id(&behavior->inputs, 0));
    ASSERT_EQ(valid_b, nmo_behavior_ref_array_get_id(&behavior->inputs, 1));
    ASSERT_EQ(valid_parameter, behavior->target_parameter_id);
    ASSERT_EQ(2, (int)behavior->sub_behaviors.count);
    nmo_behavior_ref_t *sub_refs = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &behavior->sub_behaviors);
    ASSERT_EQ(valid_a, nmo_behavior_ref_runtime_id(&sub_refs[0]));
    ASSERT_EQ(valid_b, nmo_behavior_ref_runtime_id(&sub_refs[1]));
    ASSERT_EQ(chunk_a, sub_refs[0].chunk);
    ASSERT_EQ(chunk_b, sub_refs[1].chunk);
    ASSERT_EQ(2, (int)group->base.attributes.count);
    nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &group->base.attributes);
    ASSERT_EQ(valid_parameter,
              nmo_ref_runtime_id(&attributes[0].parameter));
    ASSERT_EQ(valid_parameter_in,
              nmo_ref_runtime_id(&attributes[1].parameter));
    ASSERT_EQ(type_a, attributes[0].type_id);
    ASSERT_EQ(type_b, attributes[1].type_id);
    ASSERT_EQ(2u, group->base.legacy_attributes.count);
    nmo_beobject_legacy_attribute_t *normalized_legacy = NMO_ARRAY_DATA(
        nmo_beobject_legacy_attribute_t,
        &group->base.legacy_attributes);
    ASSERT_EQ(valid_parameter,
              nmo_ref_runtime_id(&normalized_legacy[0].parameter));
    ASSERT_EQ(valid_parameter_in,
              nmo_ref_runtime_id(&normalized_legacy[1].parameter));
    ASSERT_STR_EQ("valid-a", normalized_legacy[0].name);
    ASSERT_STR_EQ("valid-b", normalized_legacy[1].name);
    ASSERT_EQ(1, (int)grid->layers.count);
    nmo_grid_layer_t *layers = NMO_ARRAY_DATA(nmo_grid_layer_t, &grid->layers);
    ASSERT_EQ(valid_a, layers[0].ref.id);
    ASSERT_EQ(2, (int)keyed->animation_count);
    ASSERT_EQ(2, (int)keyed->subanim_count);
    ASSERT_EQ(valid_animation_a, keyed->animation_ids[0]);
    ASSERT_EQ(valid_animation_b, keyed->animation_ids[1]);
    ASSERT_EQ(101, keyed->subanims[0].object_id);
    ASSERT_EQ(303, keyed->subanims[1].object_id);
    ASSERT_EQ(1u, entity3d->mesh_count);
    ASSERT_EQ(valid_mesh, nmo_ref_runtime_id(&entity3d->mesh_ids[0]));
    ASSERT_EQ(2u, entity3d->animation_count);
    ASSERT_EQ(valid_animation_a,
              nmo_ref_runtime_id(&entity3d->animation_ids[0]));
    ASSERT_EQ(valid_animation_b,
              nmo_ref_runtime_id(&entity3d->animation_ids[1]));
    ASSERT_EQ(2u, synchro->arrived_ids.count);
    ASSERT_EQ(valid_a, nmo_ref_runtime_id(
                           &NMO_ARRAY_DATA(
                               nmo_ref_t, &synchro->arrived_ids)[0]));
    ASSERT_EQ(valid_b, nmo_ref_runtime_id(
                           &NMO_ARRAY_DATA(
                               nmo_ref_t, &synchro->arrived_ids)[1]));
    ASSERT_EQ(1u, synchro->passed_ids.count);
    ASSERT_EQ(valid_b, nmo_ref_runtime_id(
                           &NMO_ARRAY_DATA(
                               nmo_ref_t, &synchro->passed_ids)[0]));
    ASSERT_EQ(NMO_REF_NONE, parameter_out->owner.state);
    ASSERT_EQ(1u, parameter_out->destination_count);
    ASSERT_EQ(valid_parameter_in,
              nmo_parameterout_destination_id(parameter_out, 0));

    nmo_session_destroy(session);
    nmo_arena_destroy(chunk_arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_clears_raw_scalar_class_mismatch) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t entity_id = 0;
    nmo_object_id_t material_id = 0;
    nmo_object_id_t camera_id = 0;
    nmo_object_id_t entity2d_id = 0;
    nmo_object_id_t level_id = 0;
    nmo_object_id_t scene_id = 0;
    nmo_object_id_t behavior_io_id = 0;
    nmo_object_id_t behavior_link_id = 0;
    nmo_object_id_t texture_id = 0;
    nmo_object_id_t parameter_id = 0;
    nmo_object_id_t operation_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "entity", (nmo_guid_t){0, 0},
        &entity_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_MATERIAL, "material", (nmo_guid_t){0, 0},
        &material_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_TARGETCAMERA, "camera", (nmo_guid_t){0, 0},
        &camera_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_2DENTITY, "entity2d", (nmo_guid_t){0, 0},
        &entity2d_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_LEVEL, "level", (nmo_guid_t){0, 0},
        &level_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_SCENE, "scene", (nmo_guid_t){0, 0},
        &scene_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIORIO, "io", (nmo_guid_t){0, 0},
        &behavior_io_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIORLINK, "link", (nmo_guid_t){0, 0},
        &behavior_link_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_TEXTURE, "texture", (nmo_guid_t){0, 0},
        &texture_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "parameter", (nmo_guid_t){0, 0},
        &parameter_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETEROPERATION, "operation", (nmo_guid_t){0, 0},
        &operation_id, NULL));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)
        nmo_object_repository_find_by_id(repo, entity_id)->state;
    ASSERT_NOT_NULL(entity);
    entity->current_mesh = nmo_ref_from_id(material_id);
    entity->parent = nmo_ref_from_id(material_id);
    nmo_2dentity_state_t *entity2d = (nmo_2dentity_state_t *)
        nmo_object_repository_find_by_id(repo, entity2d_id)->state;
    ASSERT_NOT_NULL(entity2d);
    entity2d->has_parent = true;
    entity2d->parent = nmo_ref_from_id(entity_id);
    nmo_targetcamera_state_t *camera = (nmo_targetcamera_state_t *)
        nmo_object_repository_find_by_id(repo, camera_id)->state;
    ASSERT_NOT_NULL(camera);
    camera->has_target = 1;
    camera->target = nmo_ref_from_id(material_id);
    nmo_level_state_t *level = (nmo_level_state_t *)
        nmo_object_repository_find_by_id(repo, level_id)->state;
    ASSERT_NOT_NULL(level);
    level->current_scene = nmo_ref_from_id(material_id);
    level->level_scene = nmo_ref_from_id(scene_id);
    nmo_ref_t valid_scene = nmo_ref_from_id(scene_id);
    nmo_ref_t wrong_scene = nmo_ref_from_id(material_id);
    nmo_ref_t unresolved_scene = nmo_ref_from_raw(0x7FFFFFD0u);
    ASSERT_EQ(NMO_OK, nmo_array_append(&level->scene_ids, &valid_scene));
    ASSERT_EQ(NMO_OK, nmo_array_append(&level->scene_ids, &wrong_scene));
    ASSERT_EQ(NMO_OK, nmo_array_append(&level->scene_ids, &unresolved_scene));
    nmo_scene_state_t *scene = (nmo_scene_state_t *)
        nmo_object_repository_find_by_id(repo, scene_id)->state;
    ASSERT_NOT_NULL(scene);
    scene->level = nmo_ref_from_id(material_id);
    scene->background_texture = nmo_ref_from_id(material_id);
    scene->starting_camera = nmo_ref_from_id(material_id);
    nmo_scene_object_desc_t valid_desc = {
        .ref = {.raw_id = entity_id, .id = entity_id,
                .state = NMO_REF_RESOLVED},
    };
    nmo_scene_object_desc_t invalid_desc = {
        .ref = {.raw_id = 0x7FFFFFC0u, .id = 0,
                .state = NMO_REF_UNRESOLVED},
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&scene->object_descs, &valid_desc));
    ASSERT_EQ(NMO_OK, nmo_array_append(&scene->object_descs, &invalid_desc));
    nmo_behaviorlink_state_t *link = (nmo_behaviorlink_state_t *)
        nmo_object_repository_find_by_id(repo, behavior_link_id)->state;
    ASSERT_NOT_NULL(link);
    link->in_io = nmo_ref_from_id(material_id);
    link->out_io = nmo_ref_from_id(behavior_io_id);
    nmo_material_state_t *material = (nmo_material_state_t *)
        nmo_object_repository_find_by_id(repo, material_id)->state;
    ASSERT_NOT_NULL(material);
    material->textures[0] = nmo_ref_from_id(entity_id);
    material->textures[1] = nmo_ref_from_id(texture_id);
    material->effect_parameter = nmo_ref_from_id(camera_id);
    material->has_effect = 1;
    material->has_effect_param = 1;
    nmo_parameteroperation_state_t *operation =
        (nmo_parameteroperation_state_t *)
            nmo_object_repository_find_by_id(repo, operation_id)->state;
    ASSERT_NOT_NULL(operation);
    operation->owner = nmo_ref_from_id(material_id);
    operation->in1.ref = nmo_ref_from_id(texture_id);
    operation->in2.ref = nmo_ref_from_id(parameter_id);
    operation->out.ref = nmo_ref_from_raw(0x7FFFFFB0u);
    operation->has_owner = 1;
    operation->has_in1 = 1;
    operation->has_in2 = 1;
    operation->has_out = 1;

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ((size_t)17, changed);
    ASSERT_EQ(NMO_REF_NONE, entity->current_mesh.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, entity->current_mesh.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, entity->current_mesh.id);
    ASSERT_EQ(NMO_REF_NONE, entity->parent.state);
    ASSERT_EQ(NMO_REF_NONE, entity2d->parent.state);
    ASSERT_EQ(NMO_REF_NONE, camera->target.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, camera->target.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, camera->target.id);
    ASSERT_EQ(NMO_REF_NONE, level->current_scene.state);
    ASSERT_EQ(NMO_REF_RESOLVED, level->level_scene.state);
    ASSERT_EQ(scene_id, level->level_scene.id);
    ASSERT_EQ(1u, level->scene_ids.count);
    ASSERT_EQ(scene_id, nmo_ref_runtime_id(
                            &NMO_ARRAY_DATA(nmo_ref_t, &level->scene_ids)[0]));
    ASSERT_EQ(NMO_REF_NONE, scene->level.state);
    ASSERT_EQ(NMO_REF_NONE, scene->background_texture.state);
    ASSERT_EQ(NMO_REF_NONE, scene->starting_camera.state);
    ASSERT_EQ(1u, scene->object_descs.count);
    ASSERT_EQ(entity_id, nmo_ref_runtime_id(
                             &NMO_ARRAY_DATA(
                                 nmo_scene_object_desc_t,
                                 &scene->object_descs)[0].ref));
    ASSERT_EQ(NMO_REF_NONE, link->in_io.state);
    ASSERT_EQ(behavior_io_id, nmo_behaviorlink_out_io_id(link));
    ASSERT_EQ(NMO_REF_NONE, material->textures[0].state);
    ASSERT_EQ(texture_id, nmo_material_texture_id(material, 1));
    ASSERT_EQ(NMO_REF_NONE, material->effect_parameter.state);
    ASSERT_EQ(NMO_REF_NONE, operation->owner.state);
    ASSERT_EQ(NMO_REF_NONE, operation->in1.ref.state);
    ASSERT_EQ(parameter_id, nmo_parameteroperation_in2_id(operation));
    ASSERT_EQ(NMO_REF_NONE, operation->out.ref.state);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, dependency_remap_preserves_invalid_references) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t valid_id = 0, group_id = 0, link_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "valid", (nmo_guid_t){0, 0},
        &valid_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0},
        &group_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIORLINK, "link", (nmo_guid_t){0, 0},
        &link_id, NULL));

    nmo_group_state_t *group = (nmo_group_state_t *)
        nmo_object_repository_find_by_id(repo, group_id)->state;
    nmo_behaviorlink_state_t *link = (nmo_behaviorlink_state_t *)
        nmo_object_repository_find_by_id(repo, link_id)->state;
    ASSERT_NOT_NULL(group);
    ASSERT_NOT_NULL(link);

    const nmo_object_id_t invalid_id = 0x7FFFFFE0u;
    nmo_ref_t valid_ref = nmo_ref_from_id(valid_id);
    nmo_ref_t invalid_ref = nmo_ref_from_raw(invalid_id);
    ASSERT_EQ(NMO_OK, nmo_array_append(&group->object_ids, &valid_ref));
    ASSERT_EQ(NMO_OK, nmo_array_append(&group->object_ids, &invalid_ref));
    ASSERT_EQ(NMO_OK, nmo_array_append(&group->object_ids, &valid_ref));
    link->in_io = nmo_ref_from_raw(invalid_id);
    link->out_io = nmo_ref_from_id(valid_id);

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_all_refs(
        repo, nmo_context_get_type_runtime(ctx), 0));

    ASSERT_EQ(3, (int)group->object_ids.count);
    const nmo_ref_t *members = NMO_ARRAY_DATA(nmo_ref_t, &group->object_ids);
    ASSERT_EQ(valid_id, nmo_ref_runtime_id(&members[0]));
    ASSERT_EQ(invalid_id, members[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, members[1].state);
    ASSERT_EQ(valid_id, nmo_ref_runtime_id(&members[2]));
    ASSERT_EQ(invalid_id, link->in_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, link->in_io.state);
    ASSERT_EQ(valid_id, nmo_behaviorlink_out_io_id(link));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_scene_members) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t scene_id = 0;
    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_SCENE, "scene", (nmo_guid_t){0, 0},
        &scene_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "member", (nmo_guid_t){0, 0},
        &member_id, NULL));
    nmo_scene_state_t *scene = (nmo_scene_state_t *)
        nmo_object_repository_find_by_id(repo, scene_id)->state;
    ASSERT_NOT_NULL(scene);
    nmo_scene_object_desc_t resolved = {
        .ref = {.raw_id = member_id, .id = member_id,
                .state = NMO_REF_RESOLVED},
        .flags = 1,
    };
    nmo_scene_object_desc_t unresolved = {
        .ref = {.raw_id = 0x7FFFFFB0u, .id = 0,
                .state = NMO_REF_UNRESOLVED},
        .flags = 2,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&scene->object_descs, &resolved));
    ASSERT_EQ(NMO_OK, nmo_array_append(&scene->object_descs, &unresolved));

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, member_id, 0x12345u));
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *scene_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_SCENE);
    ASSERT_NOT_NULL(scene_type);
    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, scene_type, scene, remap));

    const nmo_scene_object_desc_t *descs = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &scene->object_descs);
    ASSERT_EQ(2u, scene->object_descs.count);
    ASSERT_EQ(0x12345u, descs[0].ref.id);
    ASSERT_EQ(member_id, descs[0].ref.raw_id);
    ASSERT_EQ(1u, descs[0].flags);
    ASSERT_EQ(0x7FFFFFB0u, descs[1].ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, descs[1].ref.state);
    ASSERT_EQ(2u, descs[1].flags);

    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_behaviorlink_endpoints) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *link_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_BEHAVIORLINK);
    ASSERT_NOT_NULL(link_type);

    nmo_behaviorlink_state_t link = {0};
    link.in_io = nmo_ref_from_id(101);
    link.out_io = nmo_ref_from_raw(202);
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 301));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 202, 302));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, link_type, &link, remap));
    ASSERT_EQ(301u, nmo_behaviorlink_in_io_id(&link));
    ASSERT_EQ(101u, link.in_io.raw_id);
    ASSERT_EQ(202u, link.out_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, link.out_io.state);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_material_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *material_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_MATERIAL);
    ASSERT_NOT_NULL(material_type);

    nmo_material_state_t material = {0};
    material.textures[0] = nmo_ref_from_id(101);
    material.textures[1] = nmo_ref_from_raw(102);
    material.effect_parameter = nmo_ref_from_id(103);
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 103, 203));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, material_type, &material, remap));
    ASSERT_EQ(201u, nmo_material_texture_id(&material, 0));
    ASSERT_EQ(101u, material.textures[0].raw_id);
    ASSERT_EQ(102u, material.textures[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, material.textures[1].state);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&material.effect_parameter));
    ASSERT_EQ(103u, material.effect_parameter.raw_id);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_beobject_attributes) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *beobject_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_BEOBJECT);
    ASSERT_NOT_NULL(beobject_type);

    nmo_beobject_state_t state = {0};
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &state.attributes, sizeof(nmo_beobject_attribute_t), 0, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &state.legacy_attributes,
        sizeof(nmo_beobject_legacy_attribute_t), 0, NULL));
    nmo_beobject_attribute_t modern[] = {
        {.parameter = nmo_ref_from_id(101), .type_id = 1},
        {.parameter = nmo_ref_from_raw(102), .type_id = 2},
    };
    nmo_beobject_legacy_attribute_t legacy[] = {
        {.parameter = nmo_ref_from_id(103), .compatible_class_id = 3},
        {.parameter = nmo_ref_from_raw(104), .compatible_class_id = 4},
    };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(
        &state.attributes, modern, 2));
    ASSERT_EQ(NMO_OK, nmo_array_append_array(
        &state.legacy_attributes, legacy, 2));

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 103, 203));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 104, 204));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, beobject_type, &state, remap));
    nmo_beobject_attribute_t *remapped_modern = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &state.attributes);
    nmo_beobject_legacy_attribute_t *remapped_legacy = NMO_ARRAY_DATA(
        nmo_beobject_legacy_attribute_t, &state.legacy_attributes);
    ASSERT_EQ(201u, nmo_ref_runtime_id(&remapped_modern[0].parameter));
    ASSERT_EQ(101u, remapped_modern[0].parameter.raw_id);
    ASSERT_EQ(102u, remapped_modern[1].parameter.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped_modern[1].parameter.state);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&remapped_legacy[0].parameter));
    ASSERT_EQ(103u, remapped_legacy[0].parameter.raw_id);
    ASSERT_EQ(104u, remapped_legacy[1].parameter.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped_legacy[1].parameter.state);

    nmo_array_dispose(&state.attributes);
    nmo_array_dispose(&state.legacy_attributes);
    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_parameteroperation_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *operation_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_PARAMETEROPERATION);
    ASSERT_NOT_NULL(operation_type);

    nmo_parameteroperation_state_t operation = {0};
    operation.owner = nmo_ref_from_id(100);
    operation.in1.ref = nmo_ref_from_id(101);
    operation.in2.ref = nmo_ref_from_raw(102);
    operation.out.ref = nmo_ref_from_id(103);
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 100, 200));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 103, 203));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, operation_type, &operation, remap));
    ASSERT_EQ(200u, nmo_parameteroperation_owner_id(&operation));
    ASSERT_EQ(201u, nmo_parameteroperation_in1_id(&operation));
    ASSERT_EQ(102u, operation.in2.ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, operation.in2.ref.state);
    ASSERT_EQ(203u, nmo_parameteroperation_out_id(&operation));

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_parameterout_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *parameterout_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_PARAMETEROUT);
    ASSERT_NOT_NULL(parameterout_type);

    nmo_ref_t destinations[3] = {
        nmo_ref_from_id(101),
        nmo_ref_from_raw(102),
        nmo_ref_from_id(103)
    };
    nmo_parameterout_state_t parameterout = {0};
    parameterout.owner = nmo_ref_from_id(100);
    parameterout.destination_ids = destinations;
    parameterout.destination_count = 3;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 100, 200));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 103, 203));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, parameterout_type, &parameterout, remap));
    ASSERT_EQ(200u, nmo_parameterout_owner_id(&parameterout));
    ASSERT_EQ(201u, nmo_parameterout_destination_id(&parameterout, 0));
    ASSERT_EQ(102u, parameterout.destination_ids[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, parameterout.destination_ids[1].state);
    ASSERT_EQ(203u, nmo_parameterout_destination_id(&parameterout, 2));

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, dependency_remap_preserves_nonreference_state) {
    nmo_animation_state_t animation = {0};
    animation.frame_rate = -3.0f;
    animation.length = -4.0f;
    animation.current_step = -5.0f;
    ASSERT_EQ(NMO_OK, nmo_animation_remap_dependencies(
        &animation, NULL, NULL));
    ASSERT_FLOAT_EQ(-3.0f, animation.frame_rate, 0.0f);
    ASSERT_FLOAT_EQ(-4.0f, animation.length, 0.0f);
    ASSERT_FLOAT_EQ(-5.0f, animation.current_step, 0.0f);

    nmo_camera_state_t camera = {0};
    camera.projection_type = 99u;
    camera.fov = -1.0f;
    camera.width = -2;
    camera.height = 70000;
    camera.near_plane = -3.0f;
    camera.far_plane = -4.0f;
    ASSERT_EQ(NMO_OK, nmo_camera_remap_dependencies(&camera, NULL, NULL));
    ASSERT_EQ(99u, camera.projection_type);
    ASSERT_FLOAT_EQ(-1.0f, camera.fov, 0.0f);
    ASSERT_EQ(-2, camera.width);
    ASSERT_EQ(70000, camera.height);
    ASSERT_FLOAT_EQ(-3.0f, camera.near_plane, 0.0f);
    ASSERT_FLOAT_EQ(-4.0f, camera.far_plane, 0.0f);

    nmo_light_state_t light = {0};
    light.light_data.type = (VXLIGHT_TYPE)99;
    light.light_data.range = -6.0f;
    light.light_power = -7.0f;
    light.has_light_power_chunk = 1;
    ASSERT_EQ(NMO_OK, nmo_light_remap_dependencies(&light, NULL, NULL));
    ASSERT_EQ(99, (int)light.light_data.type);
    ASSERT_FLOAT_EQ(-6.0f, light.light_data.range, 0.0f);
    ASSERT_FLOAT_EQ(-7.0f, light.light_power, 0.0f);
    ASSERT_EQ(1, light.has_light_power_chunk);

    nmo_behaviorio_state_t io = {0};
    io.has_flags = false;
    io.old_flags = 0xA5A5A5A5u;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_remap_dependencies(&io, NULL, NULL));
    ASSERT_EQ(0xA5A5A5A5u, io.old_flags);

    nmo_texture_state_t texture = {0};
    texture.has_desired_video_format = 1;
    texture.desired_video_format = 0xFFFFFFFFu;
    texture.has_current_slot = 1;
    texture.current_slot = -8;
    ASSERT_EQ(NMO_OK, nmo_texture_remap_dependencies(&texture, NULL, NULL));
    ASSERT_EQ(1, texture.has_desired_video_format);
    ASSERT_EQ(0xFFFFFFFFu, texture.desired_video_format);
    ASSERT_EQ(1, texture.has_current_slot);
    ASSERT_EQ(-8, texture.current_slot);

    nmo_sound_state_t sound = {0};
    sound.save_options = 0xFFFFFFFFu;
    sound.file_name = "";
    ASSERT_EQ(NMO_OK, nmo_sound_remap_dependencies(&sound, NULL, NULL));
    ASSERT_EQ(0xFFFFFFFFu, sound.save_options);
    ASSERT_NOT_NULL(sound.file_name);
    ASSERT_EQ('\0', sound.file_name[0]);

    nmo_messagemanager_state_t messages = {0};
    const char *sentinel_names[] = {"sentinel"};
    messages.message_type_names = sentinel_names;
    ASSERT_EQ(NMO_OK, nmo_messagemanager_remap_dependencies(
        &messages, NULL, NULL));
    ASSERT_EQ(sentinel_names, messages.message_type_names);

    nmo_attributemanager_state_t attributes = {0};
    nmo_attribute_category_t sentinel_category = {0};
    nmo_attribute_descriptor_t sentinel_attribute = {0};
    attributes.categories = &sentinel_category;
    attributes.attributes = &sentinel_attribute;
    ASSERT_EQ(NMO_OK, nmo_attributemanager_remap_dependencies(
        &attributes, NULL, NULL));
    ASSERT_EQ(&sentinel_category, attributes.categories);
    ASSERT_EQ(&sentinel_attribute, attributes.attributes);

    nmo_interfaceobjectmanager_state_t interfaces = {0};
    interfaces.chunk_count = -1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_interfaceobjectmanager_remap_dependencies(
                  &interfaces, NULL, NULL));
    ASSERT_EQ(-1, interfaces.chunk_count);
}

TEST(runtime_kernel, serializer_failure_does_not_reuse_raw_chunk) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t behavior_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "behavior", (nmo_guid_t){0, 0},
        &behavior_id, NULL));
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(obj);
    nmo_behavior_state_t *state = (nmo_behavior_state_t *)obj->state;
    ASSERT_NOT_NULL(state);
    nmo_object_id_t unmapped_id = 0x7FFFFF01u;
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(
        &state->local_parameters, unmapped_id, NULL));

    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *scratch = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    ASSERT_NOT_NULL(scratch);
    nmo_chunk_t *raw_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(raw_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(raw_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(raw_chunk, 0xDEADBEEFu));
    nmo_chunk_close(raw_chunk);
    obj->chunk = raw_chunk;
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(scratch);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
        .repository = repo,
    };
    nmo_status_t serialize_status = NMO_OK;
    nmo_chunk_t *serialized = nmo_object_system_serialize_object_chunk(
        obj, nmo_context_get_type_runtime(ctx), arena, scratch, repo, NULL,
        NULL, &file_context, &serialize_status);
    ASSERT_NULL(serialized);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, serialize_status);
    ASSERT_EQ(raw_chunk, obj->chunk);

    nmo_session_destroy(session);
    nmo_arena_destroy(scratch);
    nmo_arena_destroy(arena);
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
REGISTER_TEST(runtime_kernel, delete_safe_detach_prunes_behavior_links_with_deleted_io);
REGISTER_TEST(runtime_kernel, delete_cascade_removes_referencing_group);
REGISTER_TEST(runtime_kernel, deserialize_failure_does_not_publish_state_for_finalize);
REGISTER_TEST(runtime_kernel, normalize_removes_only_invalid_reference_records);
REGISTER_TEST(runtime_kernel, normalize_clears_raw_scalar_class_mismatch);
REGISTER_TEST(runtime_kernel, dependency_remap_preserves_invalid_references);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_scene_members);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_behaviorlink_endpoints);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_material_refs);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_beobject_attributes);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_parameteroperation_refs);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_parameterout_refs);
REGISTER_TEST(runtime_kernel, dependency_remap_preserves_nonreference_state);
REGISTER_TEST(runtime_kernel, serializer_failure_does_not_reuse_raw_chunk);
TEST_MAIN_END()

