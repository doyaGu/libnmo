#include "test_framework.h"
#include "runtime/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_object_system.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
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
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
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

typedef struct runtime_ref_graph_fail_allocator_state {
    int fail_allocations;
} runtime_ref_graph_fail_allocator_state_t;

static void *runtime_ref_graph_fail_alloc(
    void *user_data,
    size_t size,
    size_t alignment)
{
    runtime_ref_graph_fail_allocator_state_t *state =
        (runtime_ref_graph_fail_allocator_state_t *)user_data;
    if (state->fail_allocations) {
        return NULL;
    }
    nmo_allocator_t allocator = nmo_allocator_default();
    return allocator.alloc(allocator.user_data, size, alignment);
}

static void runtime_ref_graph_fail_free(void *user_data, void *ptr) {
    (void)user_data;
    nmo_allocator_t allocator = nmo_allocator_default();
    allocator.free(allocator.user_data, ptr);
}

static nmo_status_t runtime_ref_graph_fail_enumeration(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)instance;
    (void)type;
    (void)visitor;
    (void)user_data;
    return NMO_ERR_INVALID_STATE;
}

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

TEST(runtime_kernel, delete_safe_detach_uses_explicit_object_type) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "member", NMO_NULL_GUID,
        &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "typed-group", CKPGUID_GROUP, &group_id, NULL));
    nmo_object_t *group_obj =
        nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    ASSERT_EQ(0u, nmo_object_get_class_id(group_obj));
    runtime_group_set_members(group_obj, &member_id, 1);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &member_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);

    group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    ASSERT_EQ(0u, ((nmo_group_state_t *)group_obj->state)->object_ids.count);

    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    nmo_arena_t *scratch_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    ASSERT_NOT_NULL(scratch_arena);
    nmo_status_t serialize_status = NMO_ERR_INTERNAL;
    ASSERT_NOT_NULL(nmo_object_system_serialize_object_chunk(
        group_obj, nmo_context_get_type_runtime(ctx), chunk_arena,
        scratch_arena, repo, NULL, NULL, NULL, &serialize_status));
    ASSERT_EQ(NMO_OK, serialize_status);

    nmo_session_destroy(session);
    nmo_arena_destroy(scratch_arena);
    nmo_arena_destroy(chunk_arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_rejects_ambiguous_or_missing_sources_atomically) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t source_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "copy-source", (nmo_guid_t){0, 0},
        &source_id, NULL));
    ASSERT_EQ(1u, nmo_object_repository_get_count(repo));

    nmo_object_id_t duplicate_ids[] = {source_id, source_id};
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_session_copy_objects(
        session, duplicate_ids, 2, NMO_RUNTIME_REQUEST_STRICT, &report));
    ASSERT_EQ(1u, nmo_object_repository_get_count(repo));
    ASSERT_EQ(0u, report.copied_objects);
    ASSERT_EQ(0u, report.affected_objects);

    nmo_object_id_t missing_ids[] = {source_id, 0x7fffff01u};
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_session_copy_objects(
        session, missing_ids, 2, NMO_RUNTIME_REQUEST_STRICT, &report));
    ASSERT_EQ(1u, nmo_object_repository_get_count(repo));
    ASSERT_EQ(0u, report.copied_objects);
    ASSERT_EQ(0u, report.affected_objects);
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, source_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_hook_failure_rolls_back_all_clones) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t source_ids[2] = {0, 0};
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "copy-source-a", (nmo_guid_t){0, 0},
        &source_ids[0], NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "copy-source-b", (nmo_guid_t){0, 0},
        &source_ids[1], NULL));

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_descriptor_t *object_type =
        nmo_type_registry_find_by_class_id_inherited(registry, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(object_type);
    ASSERT_NOT_NULL(object_type->vtable);
    nmo_type_vtable_t *mutable_vtable =
        (nmo_type_vtable_t *)(void *)object_type->vtable;
    nmo_type_prepare_dependencies_fn old_prepare =
        mutable_vtable->prepare_dependencies;
    mutable_vtable->prepare_dependencies = runtime_create_fail_hook;

    nmo_runtime_report_t report = {0};
    nmo_status_t copy_result = nmo_session_copy_objects(
        session, source_ids, 2, NMO_RUNTIME_REQUEST_STRICT, &report);
    mutable_vtable->prepare_dependencies = old_prepare;

    ASSERT_EQ(NMO_ERR_INVALID_STATE, copy_result);
    ASSERT_EQ(2u, nmo_object_repository_get_count(repo));
    ASSERT_EQ(0u, report.copied_objects);
    ASSERT_EQ(0u, report.affected_objects);
    ASSERT_EQ(1u, report.object_hook_errors);
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, source_ids[0]));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, source_ids[1]));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, ref_graph_creation_fails_on_edge_allocation_error) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t member_id = 0, group_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "member", (nmo_guid_t){0, 0},
        &member_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0},
        &group_id, NULL));
    nmo_object_t *group = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group);
    runtime_group_set_members(group, &member_id, 1);

    runtime_ref_graph_fail_allocator_state_t fail_state = {0};
    nmo_allocator_t fail_allocator = nmo_allocator_custom(
        runtime_ref_graph_fail_alloc,
        runtime_ref_graph_fail_free,
        &fail_state);
    nmo_arena_t *arena = nmo_arena_create(&fail_allocator, 1024);
    ASSERT_NOT_NULL(arena);
    fail_state.fail_allocations = 1;

    ASSERT_NULL(nmo_ref_graph_create(
        repo, nmo_context_get_type_registry(ctx), arena));

    fail_state.fail_allocations = 0;
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, ref_graph_creation_propagates_enumerator_error) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", (nmo_guid_t){0, 0},
        &group_id, NULL));

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    const nmo_type_descriptor_t *group_type =
        nmo_type_registry_find_by_class_id_inherited(registry, NMO_CID_GROUP);
    ASSERT_NOT_NULL(group_type);
    ASSERT_NOT_NULL(group_type->vtable);
    nmo_type_vtable_t *mutable_vtable =
        (nmo_type_vtable_t *)(void *)group_type->vtable;
    nmo_type_enumerate_refs_fn old_enumerator = mutable_vtable->enumerate_refs;
    mutable_vtable->enumerate_refs = runtime_ref_graph_fail_enumeration;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, registry, arena);
    mutable_vtable->enumerate_refs = old_enumerator;

    ASSERT_NULL(graph);
    nmo_arena_destroy(arena);
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
    nmo_allocator_stats_t state_stats = {0};
    nmo_allocator_tracking_t state_tracking = {0};
    nmo_allocator_t state_allocator = nmo_allocator_tracking_init(
        &state_tracking, nmo_allocator_default(), &state_stats);

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
    nmo_group_state_t *group_state = (nmo_group_state_t *)group_obj->state;
    ASSERT_NOT_NULL(group_state);
    nmo_array_dispose(&group_state->object_ids);
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &group_state->object_ids, sizeof(nmo_ref_t), 0, &state_allocator));
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
    ASSERT_EQ((size_t)0, state_stats.current_bytes);
    ASSERT_EQ(state_stats.total_allocations, state_stats.total_frees);

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
    nmo_behavior_set_target_parameter_id(behavior, valid_parameter_in);

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

    nmo_ref_t keyed_ids[] = {
        nmo_ref_from_id(valid_animation_a),
        nmo_ref_from_raw(invalid),
        nmo_ref_from_id(valid_animation_b),
    };
    nmo_keyedanimation_subanim_t subanims[3] = {
        {.ref = {.raw_id = valid_animation_a,
                 .id = valid_animation_a,
                 .state = NMO_REF_RESOLVED}},
        {.ref = {.raw_id = invalid,
                 .id = NMO_OBJECT_ID_NONE,
                 .state = NMO_REF_UNRESOLVED}},
        {.ref = {.raw_id = valid_animation_b,
                 .id = valid_animation_b,
                 .state = NMO_REF_RESOLVED}},
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
    ASSERT_EQ(16, (int)changed);
    ASSERT_EQ(2, (int)behavior->inputs.count);
    ASSERT_EQ(valid_a, nmo_behavior_ref_array_get_id(&behavior->inputs, 0));
    ASSERT_EQ(valid_b, nmo_behavior_ref_array_get_id(&behavior->inputs, 1));
    ASSERT_EQ(valid_parameter_in, nmo_behavior_target_parameter_id(behavior));
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
    ASSERT_EQ(valid_animation_a,
              nmo_ref_runtime_id(&keyed->animation_ids[0]));
    ASSERT_EQ(valid_animation_b,
              nmo_ref_runtime_id(&keyed->animation_ids[1]));
    ASSERT_EQ(valid_animation_a,
              nmo_ref_runtime_id(&keyed->subanims[0].ref));
    ASSERT_EQ(valid_animation_b,
              nmo_ref_runtime_id(&keyed->subanims[1].ref));
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

TEST(runtime_kernel, behavior_normalize_validates_lanes_before_mutation) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t behavior_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEHAVIOR, "behavior", NMO_NULL_GUID,
        &behavior_id, NULL));
    nmo_behavior_state_t *behavior = (nmo_behavior_state_t *)
        nmo_object_repository_find_by_id(repo, behavior_id)->state;
    ASSERT_NOT_NULL(behavior);

    behavior->owner = nmo_ref_from_raw(0x7FFFFF62u);
    const size_t saved_element_size = behavior->outputs.element_size;
    behavior->outputs.element_size = 1;

    size_t changed = 0;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_behavior_normalize_references(
                  behavior, repo, &changed));
    ASSERT_EQ(0u, changed);
    ASSERT_EQ(NMO_REF_UNRESOLVED, behavior->owner.state);
    ASSERT_EQ(0x7FFFFF62u, behavior->owner.raw_id);

    behavior->outputs.element_size = saved_element_size;
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, beobject_normalize_validates_attributes_before_mutation) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", NMO_NULL_GUID,
        &group_id, NULL));
    nmo_group_state_t *group = (nmo_group_state_t *)
        nmo_object_repository_find_by_id(repo, group_id)->state;
    ASSERT_NOT_NULL(group);
    nmo_beobject_attribute_t invalid_attribute = {
        .parameter = nmo_ref_from_raw(0x7FFFFF63u),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &group->base.attributes, &invalid_attribute));
    const size_t saved_element_size =
        group->base.legacy_attributes.element_size;
    group->base.legacy_attributes.element_size = 1;

    size_t changed = 0;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_runtime_normalize_invalid_refs(
                  repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(0u, changed);
    ASSERT_EQ(1u, group->base.attributes.count);
    nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &group->base.attributes);
    ASSERT_EQ(NMO_REF_UNRESOLVED, attributes[0].parameter.state);
    ASSERT_EQ(0x7FFFFF63u, attributes[0].parameter.raw_id);

    group->base.legacy_attributes.element_size = saved_element_size;
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_reports_malformed_ref_arrays_before_mutation) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", NMO_NULL_GUID,
        &group_id, NULL));
    nmo_group_state_t *group = (nmo_group_state_t *)
        nmo_object_repository_find_by_id(repo, group_id)->state;
    ASSERT_NOT_NULL(group);
    nmo_beobject_attribute_t invalid_attribute = {
        .parameter = nmo_ref_from_raw(0x7FFFFF64u),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &group->base.attributes, &invalid_attribute));
    const size_t saved_element_size = group->object_ids.element_size;
    group->object_ids.element_size = 1;

    size_t changed = 0;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_runtime_normalize_invalid_refs(
                  repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(0u, changed);
    ASSERT_EQ(1u, group->base.attributes.count);
    nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &group->base.attributes);
    ASSERT_EQ(NMO_REF_UNRESOLVED, attributes[0].parameter.state);
    ASSERT_EQ(0x7FFFFF64u, attributes[0].parameter.raw_id);

    group->object_ids.element_size = saved_element_size;
    nmo_session_destroy(session);
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

TEST(runtime_kernel, normalize_preserves_explicitly_typed_reference_targets) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t entity_id = 0;
    nmo_object_id_t mesh_id = 0;
    nmo_object_id_t group_id = 0;
    nmo_object_id_t parameter_id = 0;
    nmo_object_id_t operation_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "entity", NMO_NULL_GUID,
        &entity_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "typed-mesh", CKPGUID_MESH, &mesh_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GROUP, "group", NMO_NULL_GUID,
        &group_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, 0, "typed-parameter", CKPGUID_PARAMETERIN,
        &parameter_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETEROPERATION, "operation", NMO_NULL_GUID,
        &operation_id, NULL));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *entity_object =
        nmo_object_repository_find_by_id(repo, entity_id);
    ASSERT_NOT_NULL(entity_object);
    nmo_3dentity_state_t *entity =
        (nmo_3dentity_state_t *)entity_object->state;
    ASSERT_NOT_NULL(entity);
    entity->current_mesh = nmo_ref_from_id(mesh_id);
    nmo_group_state_t *group = (nmo_group_state_t *)
        nmo_object_repository_find_by_id(repo, group_id)->state;
    ASSERT_NOT_NULL(group);
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &group->base.attributes, parameter_id, 1u, NULL));
    nmo_parameteroperation_state_t *operation =
        (nmo_parameteroperation_state_t *)
            nmo_object_repository_find_by_id(repo, operation_id)->state;
    ASSERT_NOT_NULL(operation);
    operation->in1.ref = nmo_ref_from_id(parameter_id);
    operation->has_in1 = 1;

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(0u, changed);
    ASSERT_EQ(NMO_REF_RESOLVED, entity->current_mesh.state);
    ASSERT_EQ(mesh_id, entity->current_mesh.id);
    ASSERT_EQ(1u, group->base.attributes.count);
    ASSERT_EQ(parameter_id, nmo_ref_runtime_id(
        &NMO_ARRAY_DATA(
            nmo_beobject_attribute_t, &group->base.attributes)[0].parameter));
    ASSERT_EQ(NMO_REF_RESOLVED, operation->in1.ref.state);
    ASSERT_EQ(parameter_id, operation->in1.ref.id);

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

TEST(runtime_kernel, copy_remap_rejects_malformed_reference_storage) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *group_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_GROUP);
    const nmo_type_descriptor_t *scene_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_SCENE);
    ASSERT_NOT_NULL(group_type);
    ASSERT_NOT_NULL(scene_type);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));

    nmo_group_state_t group = {0};
    group.object_ids.element_size = sizeof(nmo_ref_t);
    group.object_ids.count = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_runtime_remap_copy_refs(
        type_rt, group_type, &group, remap));

    nmo_scene_state_t scene = {0};
    scene.object_descs.element_size = sizeof(nmo_scene_object_desc_t);
    scene.object_descs.count = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_runtime_remap_copy_refs(
        type_rt, scene_type, &scene, remap));

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, safe_detach_removes_scene_members_atomically) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t scene_id = 0;
    nmo_object_id_t deleted_id = 0;
    nmo_object_id_t kept_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_SCENE, "scene", NMO_NULL_GUID,
        &scene_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "deleted", NMO_NULL_GUID,
        &deleted_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "kept", NMO_NULL_GUID,
        &kept_id, NULL));

    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *deleted_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *kept_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(deleted_chunk);
    ASSERT_NOT_NULL(kept_chunk);
    nmo_scene_object_desc_t descs[] = {
        {
            .ref = nmo_ref_from_id(deleted_id),
            .initial_value = deleted_chunk,
            .flags = 11,
        },
        {
            .ref = nmo_ref_from_id(kept_id),
            .initial_value = kept_chunk,
            .flags = 22,
        },
        {
            .ref = nmo_ref_from_raw(0x7FFFFF51u),
            .flags = 33,
        },
    };
    nmo_scene_state_t *scene = (nmo_scene_state_t *)
        nmo_object_repository_find_by_id(repo, scene_id)->state;
    ASSERT_EQ(NMO_OK, nmo_array_append_array(
        &scene->object_descs, descs, 3));

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &deleted_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    ASSERT_EQ(2u, scene->object_descs.count);
    nmo_scene_object_desc_t *remaining = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &scene->object_descs);
    ASSERT_EQ(kept_id, nmo_ref_runtime_id(&remaining[0].ref));
    ASSERT_EQ(kept_chunk, remaining[0].initial_value);
    ASSERT_EQ(22u, remaining[0].flags);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remaining[1].ref.state);
    ASSERT_EQ(0x7FFFFF51u, remaining[1].ref.raw_id);
    ASSERT_EQ(33u, remaining[1].flags);

    nmo_session_destroy(session);
    nmo_arena_destroy(chunk_arena);
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

TEST(runtime_kernel, copy_remap_updates_only_resolved_behavior_records) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *behavior_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_BEHAVIOR);
    ASSERT_NOT_NULL(behavior_type);

    nmo_behavior_state_t state = {0};
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &state.sub_behaviors, sizeof(nmo_behavior_ref_t), 2, NULL));
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *resolved_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *unresolved_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(resolved_chunk);
    ASSERT_NOT_NULL(unresolved_chunk);
    nmo_behavior_ref_t refs[] = {
        {.ref = nmo_ref_from_id(101), .chunk = resolved_chunk},
        {.ref = nmo_ref_from_raw(102), .chunk = unresolved_chunk},
    };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(
        &state.sub_behaviors, refs, 2));
    state.owner = nmo_ref_from_id(103);
    state.target_parameter = nmo_ref_from_raw(104);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    for (nmo_object_id_t old_id = 101; old_id <= 104; ++old_id) {
        ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, old_id, old_id + 100));
    }

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, behavior_type, &state, remap));
    nmo_behavior_ref_t *remapped = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &state.sub_behaviors);
    ASSERT_EQ(201u, nmo_behavior_ref_runtime_id(&remapped[0]));
    ASSERT_EQ(101u, remapped[0].ref.raw_id);
    ASSERT_EQ(resolved_chunk, remapped[0].chunk);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped[1].ref.state);
    ASSERT_EQ(102u, remapped[1].ref.raw_id);
    ASSERT_EQ(unresolved_chunk, remapped[1].chunk);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&state.owner));
    ASSERT_EQ(103u, state.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.target_parameter.state);
    ASSERT_EQ(104u, state.target_parameter.raw_id);

    nmo_array_dispose(&state.sub_behaviors);
    nmo_chunk_destroy(unresolved_chunk);
    nmo_chunk_destroy(resolved_chunk);
    nmo_arena_destroy(chunk_arena);
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

TEST(runtime_kernel, copy_remap_updates_only_resolved_keyedanimation_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *keyed_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_KEYEDANIMATION);
    ASSERT_NOT_NULL(keyed_type);

    nmo_ref_t animation_ids[] = {
        nmo_ref_from_id(101), nmo_ref_from_raw(102),
    };
    nmo_keyedanimation_subanim_t subanims[] = {
        {.ref = {.raw_id = 103, .id = 103,
                 .state = NMO_REF_RESOLVED}},
        {.ref = {.raw_id = 104, .id = NMO_OBJECT_ID_NONE,
                 .state = NMO_REF_UNRESOLVED}},
    };
    nmo_keyedanimation_state_t state = {0};
    state.base.root_entity = nmo_ref_from_id(105);
    state.base.character = nmo_ref_from_raw(106);
    state.animation_count = 2;
    state.animation_ids = animation_ids;
    state.subanim_count = 2;
    state.subanims = subanims;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    for (nmo_object_id_t old_id = 101; old_id <= 106; ++old_id) {
        ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, old_id, old_id + 100));
    }

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, keyed_type, &state, remap));
    ASSERT_EQ(201u, nmo_ref_runtime_id(&animation_ids[0]));
    ASSERT_EQ(101u, animation_ids[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, animation_ids[1].state);
    ASSERT_EQ(102u, animation_ids[1].raw_id);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&subanims[0].ref));
    ASSERT_EQ(103u, subanims[0].ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, subanims[1].ref.state);
    ASSERT_EQ(104u, subanims[1].ref.raw_id);
    ASSERT_EQ(205u, nmo_ref_runtime_id(&state.base.root_entity));
    ASSERT_EQ(105u, state.base.root_entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.base.character.state);
    ASSERT_EQ(106u, state.base.character.raw_id);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_objectanimation_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id(
        type_rt->types, NMO_CID_OBJECTANIMATION);
    ASSERT_NOT_NULL(type);

    nmo_objectanimation_state_t state = {0};
    state.entity = nmo_ref_from_id(101);
    state.anim1 = nmo_ref_from_raw(102);
    state.anim2 = nmo_ref_from_id(103);
    state.shared_anim = nmo_ref_from_raw(104);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    for (nmo_object_id_t old_id = 101; old_id <= 104; ++old_id) {
        ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, old_id, old_id + 100));
    }

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, type, &state, remap));
    ASSERT_EQ(201u, nmo_ref_runtime_id(&state.entity));
    ASSERT_EQ(101u, state.entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.anim1.state);
    ASSERT_EQ(102u, state.anim1.raw_id);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&state.anim2));
    ASSERT_EQ(103u, state.anim2.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.shared_anim.state);
    ASSERT_EQ(104u, state.shared_anim.raw_id);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, safe_detach_keeps_keyedanimation_sections_independent) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t keyed_id = 0;
    nmo_object_id_t animation_a = 0;
    nmo_object_id_t animation_b = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_KEYEDANIMATION, "keyed", (nmo_guid_t){0, 0},
        &keyed_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECTANIMATION, "a", (nmo_guid_t){0, 0},
        &animation_a, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECTANIMATION, "b", (nmo_guid_t){0, 0},
        &animation_b, NULL));

    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *chunk_a = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *chunk_b = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(chunk_a);
    ASSERT_NOT_NULL(chunk_b);
    nmo_ref_t animation_ids[] = {
        nmo_ref_from_id(animation_a), nmo_ref_from_id(animation_b),
    };
    nmo_keyedanimation_subanim_t subanims[] = {
        {.ref = {.raw_id = animation_b, .id = animation_b,
                 .state = NMO_REF_RESOLVED},
         .chunk = chunk_b},
        {.ref = {.raw_id = animation_a, .id = animation_a,
                 .state = NMO_REF_RESOLVED},
         .chunk = chunk_a},
    };
    nmo_keyedanimation_state_t *keyed = (nmo_keyedanimation_state_t *)
        nmo_object_repository_find_by_id(repo, keyed_id)->state;
    ASSERT_NOT_NULL(keyed);
    keyed->animation_count = 2;
    keyed->animation_ids = animation_ids;
    keyed->subanim_count = 2;
    keyed->subanims = subanims;

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &animation_a, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    keyed = (nmo_keyedanimation_state_t *)
        nmo_object_repository_find_by_id(repo, keyed_id)->state;
    ASSERT_EQ(1u, keyed->animation_count);
    ASSERT_EQ(animation_b,
              nmo_ref_runtime_id(&keyed->animation_ids[0]));
    ASSERT_EQ(1u, keyed->subanim_count);
    ASSERT_EQ(animation_b,
              nmo_ref_runtime_id(&keyed->subanims[0].ref));
    ASSERT_EQ(chunk_b, keyed->subanims[0].chunk);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    nmo_arena_destroy(chunk_arena);
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

TEST(runtime_kernel, copy_remap_updates_only_resolved_grid_layers) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *grid_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_GRID);
    ASSERT_NOT_NULL(grid_type);

    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *resolved_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *unresolved_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(resolved_chunk);
    ASSERT_NOT_NULL(unresolved_chunk);
    nmo_grid_layer_t layers[] = {
        {.ref = nmo_ref_from_id(101), .chunk = resolved_chunk},
        {.ref = nmo_ref_from_raw(102), .chunk = unresolved_chunk},
    };
    nmo_grid_state_t state = {0};
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &state.layers, sizeof(nmo_grid_layer_t), 2, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&state.layers, layers, 2));

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, grid_type, &state, remap));
    nmo_grid_layer_t *remapped = NMO_ARRAY_DATA(
        nmo_grid_layer_t, &state.layers);
    ASSERT_EQ(201u, nmo_ref_runtime_id(&remapped[0].ref));
    ASSERT_EQ(101u, remapped[0].ref.raw_id);
    ASSERT_EQ(resolved_chunk, remapped[0].chunk);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped[1].ref.state);
    ASSERT_EQ(102u, remapped[1].ref.raw_id);
    ASSERT_EQ(unresolved_chunk, remapped[1].chunk);

    nmo_array_dispose(&state.layers);
    nmo_chunk_destroy(unresolved_chunk);
    nmo_chunk_destroy(resolved_chunk);
    nmo_arena_destroy(chunk_arena);
    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, rejects_malformed_grid_storage_before_runtime_mutation) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t grid_id = 0;
    nmo_object_id_t victim_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_GRID, "grid", NMO_NULL_GUID,
        &grid_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "victim", NMO_NULL_GUID,
        &victim_id, NULL));
    nmo_grid_state_t *grid = (nmo_grid_state_t *)
        nmo_object_repository_find_by_id(repo, grid_id)->state;
    ASSERT_NOT_NULL(grid);

    ASSERT_NOT_NULL(nmo_session_get_ref_graph(session));
    const size_t saved_element_size = grid->layers.element_size;
    grid->layers.element_size = 1;

    size_t changed = 0;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_runtime_normalize_invalid_refs(
                  repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(0u, changed);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_session_destroy_objects(
        session, &victim_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(0u, report.deleted_objects);
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, victim_id));

    grid->layers.element_size = saved_element_size;
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, safe_detach_prunes_all_beobject_attribute_layouts) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t modern_owner_id = 0;
    nmo_object_id_t legacy_owner_id = 0;
    nmo_object_id_t deleted_parameter_id = 0;
    nmo_object_id_t kept_parameter_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEOBJECT, "modern-owner", NMO_NULL_GUID,
        &modern_owner_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BEOBJECT, "legacy-owner", NMO_NULL_GUID,
        &legacy_owner_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "deleted", NMO_NULL_GUID,
        &deleted_parameter_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "kept", NMO_NULL_GUID,
        &kept_parameter_id, NULL));

    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *deleted_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *kept_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(deleted_chunk);
    ASSERT_NOT_NULL(kept_chunk);
    nmo_beobject_state_t *modern_owner = (nmo_beobject_state_t *)
        nmo_object_repository_find_by_id(repo, modern_owner_id)->state;
    nmo_beobject_state_t *legacy_owner = (nmo_beobject_state_t *)
        nmo_object_repository_find_by_id(repo, legacy_owner_id)->state;
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &modern_owner->attributes, deleted_parameter_id, 11, deleted_chunk));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &modern_owner->attributes, kept_parameter_id, 22, kept_chunk));
    nmo_beobject_legacy_attribute_t legacy[] = {
        {
            .compatible_class_id = 31,
            .name = "deleted",
            .category = "first",
            .parameter = nmo_ref_from_id(deleted_parameter_id),
        },
        {
            .compatible_class_id = 32,
            .name = "kept",
            .category = "second",
            .parameter = nmo_ref_from_id(kept_parameter_id),
        },
        {
            .compatible_class_id = 33,
            .name = "unresolved",
            .category = "third",
            .parameter = nmo_ref_from_raw(0x7FFFFF52u),
        },
    };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(
        &legacy_owner->legacy_attributes, legacy, 3));

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &deleted_parameter_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    ASSERT_EQ(1u, modern_owner->attributes.count);
    nmo_beobject_attribute_t *modern = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &modern_owner->attributes);
    ASSERT_EQ(kept_parameter_id,
              nmo_ref_runtime_id(&modern[0].parameter));
    ASSERT_EQ(22u, modern[0].type_id);
    ASSERT_EQ(kept_chunk, modern[0].chunk);
    ASSERT_EQ(2u, legacy_owner->legacy_attributes.count);
    nmo_beobject_legacy_attribute_t *remaining = NMO_ARRAY_DATA(
        nmo_beobject_legacy_attribute_t, &legacy_owner->legacy_attributes);
    ASSERT_EQ(kept_parameter_id,
              nmo_ref_runtime_id(&remaining[0].parameter));
    ASSERT_EQ(32, remaining[0].compatible_class_id);
    ASSERT_STR_EQ("kept", remaining[0].name);
    ASSERT_STR_EQ("second", remaining[0].category);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remaining[1].parameter.state);
    ASSERT_EQ(0x7FFFFF52u, remaining[1].parameter.raw_id);
    ASSERT_EQ(33, remaining[1].compatible_class_id);

    nmo_session_destroy(session);
    nmo_arena_destroy(chunk_arena);
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

TEST(runtime_kernel, copy_remap_preserves_invalid_character_references) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *character_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_CHARACTER);
    ASSERT_NOT_NULL(character_type);

    nmo_character_state_t state = {0};
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &state.body_parts, sizeof(nmo_character_part_t), 0, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &state.animations, sizeof(nmo_ref_t), 0, NULL));

    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *part_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(part_chunk);
    nmo_character_part_t parts[] = {
        {.ref = nmo_ref_from_id(101), .chunk = part_chunk},
        {.ref = nmo_ref_from_raw(102), .chunk = NULL},
    };
    nmo_ref_t animations[] = {
        nmo_ref_from_id(103),
        nmo_ref_from_raw(104),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&state.body_parts, parts, 2));
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&state.animations, animations, 2));
    state.active_animation = nmo_ref_from_id(105);
    state.anim_dest = nmo_ref_from_raw(106);
    state.root_body_part = nmo_ref_from_id(107);
    state.floor_ref = nmo_ref_from_raw(108);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    for (nmo_object_id_t old_id = 101; old_id <= 108; ++old_id) {
        ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, old_id, old_id + 100));
    }

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, character_type, &state, remap));
    nmo_character_part_t *remapped_parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &state.body_parts);
    nmo_ref_t *remapped_animations = NMO_ARRAY_DATA(
        nmo_ref_t, &state.animations);
    ASSERT_EQ(201u, nmo_ref_runtime_id(&remapped_parts[0].ref));
    ASSERT_EQ(101u, remapped_parts[0].ref.raw_id);
    ASSERT_EQ(part_chunk, remapped_parts[0].chunk);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped_parts[1].ref.state);
    ASSERT_EQ(102u, remapped_parts[1].ref.raw_id);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&remapped_animations[0]));
    ASSERT_EQ(103u, remapped_animations[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped_animations[1].state);
    ASSERT_EQ(104u, remapped_animations[1].raw_id);
    ASSERT_EQ(205u, nmo_ref_runtime_id(&state.active_animation));
    ASSERT_EQ(105u, state.active_animation.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.anim_dest.state);
    ASSERT_EQ(106u, state.anim_dest.raw_id);
    ASSERT_EQ(207u, nmo_ref_runtime_id(&state.root_body_part));
    ASSERT_EQ(107u, state.root_body_part.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.floor_ref.state);
    ASSERT_EQ(108u, state.floor_ref.raw_id);

    nmo_array_dispose(&state.animations);
    nmo_array_dispose(&state.body_parts);
    nmo_chunk_destroy(part_chunk);
    nmo_arena_destroy(chunk_arena);
    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_and_safe_detach_keep_character_parts_atomic) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t character_id = 0;
    nmo_object_id_t bodypart_id = 0;
    nmo_object_id_t wrong_class_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_CHARACTER, "character", (nmo_guid_t){0, 0},
        &character_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_BODYPART, "bodypart", (nmo_guid_t){0, 0},
        &bodypart_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "wrong-class", (nmo_guid_t){0, 0},
        &wrong_class_id, NULL));

    nmo_character_state_t *character = (nmo_character_state_t *)
        nmo_object_repository_find_by_id(repo, character_id)->state;
    ASSERT_NOT_NULL(character);
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *valid_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *unresolved_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *wrong_class_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(valid_chunk);
    ASSERT_NOT_NULL(unresolved_chunk);
    ASSERT_NOT_NULL(wrong_class_chunk);
    nmo_character_part_t parts[] = {
        {.ref = nmo_ref_from_id(bodypart_id), .chunk = valid_chunk},
        {.ref = nmo_ref_from_raw(0x7FFFFF01u), .chunk = unresolved_chunk},
        {.ref = nmo_ref_from_id(wrong_class_id), .chunk = wrong_class_chunk},
    };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&character->body_parts, parts, 3));

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(2u, changed);
    ASSERT_EQ(1u, character->body_parts.count);
    nmo_character_part_t *remaining = NMO_ARRAY_DATA(
        nmo_character_part_t, &character->body_parts);
    ASSERT_EQ(bodypart_id, nmo_ref_runtime_id(&remaining[0].ref));
    ASSERT_EQ(valid_chunk, remaining[0].chunk);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &bodypart_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    character = (nmo_character_state_t *)
        nmo_object_repository_find_by_id(repo, character_id)->state;
    ASSERT_EQ(0u, character->body_parts.count);

    nmo_session_destroy(session);
    nmo_arena_destroy(chunk_arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_mesh_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *mesh_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_MESH);
    ASSERT_NOT_NULL(mesh_type);

    nmo_material_group_t groups[] = {
        {.material = nmo_ref_from_id(101), .padding = 11},
        {.material = nmo_ref_from_raw(102), .padding = 22},
    };
    nmo_material_channel_t channels[] = {
        {.material = nmo_ref_from_id(103), .flags = 33},
        {.material = nmo_ref_from_raw(104), .flags = 44},
    };
    nmo_mesh_state_t state = {
        .material_group_count = 2,
        .material_groups = groups,
        .material_channel_count = 2,
        .material_channels = channels,
    };

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 103, 203));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 104, 204));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, mesh_type, &state, remap));
    ASSERT_EQ(201u, nmo_ref_runtime_id(&groups[0].material));
    ASSERT_EQ(101u, groups[0].material.raw_id);
    ASSERT_EQ(11, groups[0].padding);
    ASSERT_EQ(NMO_REF_UNRESOLVED, groups[1].material.state);
    ASSERT_EQ(102u, groups[1].material.raw_id);
    ASSERT_EQ(22, groups[1].padding);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&channels[0].material));
    ASSERT_EQ(33u, channels[0].flags);
    ASSERT_EQ(NMO_REF_UNRESOLVED, channels[1].material.state);
    ASSERT_EQ(104u, channels[1].material.raw_id);
    ASSERT_EQ(44u, channels[1].flags);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_and_safe_detach_keep_mesh_records_atomic) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t mesh_id = 0;
    nmo_object_id_t material_id = 0;
    nmo_object_id_t wrong_class_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_MESH, "mesh", (nmo_guid_t){0, 0},
        &mesh_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_MATERIAL, "material", (nmo_guid_t){0, 0},
        &material_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "wrong-class", (nmo_guid_t){0, 0},
        &wrong_class_id, NULL));

    nmo_mesh_state_t *mesh = (nmo_mesh_state_t *)
        nmo_object_repository_find_by_id(repo, mesh_id)->state;
    ASSERT_NOT_NULL(mesh);
    nmo_material_group_t groups[] = {
        {.material = nmo_ref_from_id(material_id), .padding = 11},
        {.material = nmo_ref_from_raw(0x7FFFFF11u), .padding = 22},
        {.material = nmo_ref_from_id(wrong_class_id), .padding = 33},
    };
    nmo_material_channel_t channels[] = {
        {.material = nmo_ref_from_id(material_id), .flags = 44},
        {.material = nmo_ref_from_raw(0x7FFFFF12u), .flags = 55},
        {.material = nmo_ref_from_id(wrong_class_id), .flags = 66},
    };
    nmo_face_t faces[] = {
        {.material_group_idx = 0},
        {.material_group_idx = 1},
        {.material_group_idx = 2},
    };
    uint16_t face_indices[9] = {0};
    mesh->material_group_count = 3;
    mesh->material_groups = groups;
    mesh->material_channel_count = 3;
    mesh->material_channels = channels;
    mesh->face_count = 3;
    mesh->faces = faces;
    mesh->face_vertex_indices = face_indices;

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(4u, changed);
    ASSERT_EQ(1u, mesh->material_group_count);
    ASSERT_EQ(material_id,
              nmo_ref_runtime_id(&mesh->material_groups[0].material));
    ASSERT_EQ(11, mesh->material_groups[0].padding);
    ASSERT_EQ(0u, mesh->faces[0].material_group_idx);
    ASSERT_EQ(0u, mesh->faces[1].material_group_idx);
    ASSERT_EQ(0u, mesh->faces[2].material_group_idx);
    ASSERT_EQ(NMO_REF_RESOLVED, mesh->material_channels[0].material.state);
    ASSERT_EQ(44u, mesh->material_channels[0].flags);
    ASSERT_EQ(NMO_REF_NONE, mesh->material_channels[1].material.state);
    ASSERT_EQ(55u, mesh->material_channels[1].flags);
    ASSERT_EQ(NMO_REF_NONE, mesh->material_channels[2].material.state);
    ASSERT_EQ(66u, mesh->material_channels[2].flags);
    nmo_object_t *material_object =
        nmo_object_repository_find_by_id(repo, material_id);
    ASSERT_NOT_NULL(material_object);
    const nmo_object_id_t delete_id = material_object->id;

    nmo_runtime_report_t report = {0};
    nmo_status_t delete_status = nmo_session_destroy_objects(
        session, &delete_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report);
    ASSERT_EQ(NMO_OK, delete_status);
    ASSERT_EQ(1u, report.deleted_objects);
    mesh = (nmo_mesh_state_t *)
        nmo_object_repository_find_by_id(repo, mesh_id)->state;
    ASSERT_EQ(0u, mesh->material_group_count);
    ASSERT_EQ(NMO_REF_NONE, mesh->material_channels[0].material.state);
    ASSERT_EQ(44u, mesh->material_channels[0].flags);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_patchmesh_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *patchmesh_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_PATCHMESH);
    ASSERT_NOT_NULL(patchmesh_type);

    nmo_patchmesh_patch_record_t patches[] = {
        {.material = nmo_ref_from_id(101), .patch = {.type = 11}},
        {.material = nmo_ref_from_raw(102), .patch = {.type = 22}},
    };
    nmo_patchmesh_channel_t channels[] = {
        {.material = nmo_ref_from_id(103), .flags = 33},
        {.material = nmo_ref_from_raw(104), .flags = 44},
    };
    nmo_ref_t legacy_materials[] = {
        nmo_ref_from_id(106),
        nmo_ref_from_raw(107),
    };
    nmo_patchmesh_state_t state = {
        .format = CKPATCHMESH_FORMAT_DATA3,
        .patch_count = 2,
        .patches = patches,
        .channel_count = 2,
        .channels = channels,
        .legacy_default_material = nmo_ref_from_id(105),
        .legacy_material_count = 2,
        .legacy_materials = legacy_materials,
    };

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    for (nmo_object_id_t old_id = 101; old_id <= 107; ++old_id) {
        ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, old_id, old_id + 100));
    }

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, patchmesh_type, &state, remap));
    ASSERT_EQ(201u, nmo_ref_runtime_id(&patches[0].material));
    ASSERT_EQ(101u, patches[0].material.raw_id);
    ASSERT_EQ(11u, patches[0].patch.type);
    ASSERT_EQ(NMO_REF_UNRESOLVED, patches[1].material.state);
    ASSERT_EQ(102u, patches[1].material.raw_id);
    ASSERT_EQ(22u, patches[1].patch.type);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&channels[0].material));
    ASSERT_EQ(103u, channels[0].material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, channels[1].material.state);
    ASSERT_EQ(104u, channels[1].material.raw_id);
    ASSERT_EQ(205u, nmo_ref_runtime_id(&state.legacy_default_material));
    ASSERT_EQ(105u, state.legacy_default_material.raw_id);
    ASSERT_EQ(206u, nmo_ref_runtime_id(&legacy_materials[0]));
    ASSERT_EQ(NMO_REF_UNRESOLVED, legacy_materials[1].state);
    ASSERT_EQ(107u, legacy_materials[1].raw_id);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_and_safe_detach_keep_patchmesh_records_atomic) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t patchmesh_id = 0;
    nmo_object_id_t material_id = 0;
    nmo_object_id_t wrong_class_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PATCHMESH, "patchmesh", (nmo_guid_t){0, 0},
        &patchmesh_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_MATERIAL, "material", (nmo_guid_t){0, 0},
        &material_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "wrong-class", (nmo_guid_t){0, 0},
        &wrong_class_id, NULL));

    nmo_patchmesh_state_t *patchmesh = (nmo_patchmesh_state_t *)
        nmo_object_repository_find_by_id(repo, patchmesh_id)->state;
    ASSERT_NOT_NULL(patchmesh);
    nmo_patchmesh_patch_record_t patches[] = {
        {.material = nmo_ref_from_id(material_id), .patch = {.type = 11}},
        {.material = nmo_ref_from_raw(0x7FFFFF01u), .patch = {.type = 22}},
        {.material = nmo_ref_from_id(wrong_class_id), .patch = {.type = 33}},
    };
    nmo_patchmesh_channel_t channels[] = {
        {.material = nmo_ref_from_id(material_id), .flags = 44},
        {.material = nmo_ref_from_raw(0x7FFFFF02u), .flags = 55},
        {.material = nmo_ref_from_id(wrong_class_id), .flags = 66},
    };
    patchmesh->format = CKPATCHMESH_FORMAT_DATA3;
    patchmesh->patch_count = 3;
    patchmesh->patches = patches;
    patchmesh->channel_count = 3;
    patchmesh->channels = channels;

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(4u, changed);
    ASSERT_EQ(1u, patchmesh->patch_count);
    ASSERT_EQ(material_id,
              nmo_ref_runtime_id(&patchmesh->patches[0].material));
    ASSERT_EQ(11u, patchmesh->patches[0].patch.type);
    ASSERT_EQ(NMO_REF_RESOLVED, patchmesh->channels[0].material.state);
    ASSERT_EQ(44u, patchmesh->channels[0].flags);
    ASSERT_EQ(NMO_REF_NONE, patchmesh->channels[1].material.state);
    ASSERT_EQ(55u, patchmesh->channels[1].flags);
    ASSERT_EQ(NMO_REF_NONE, patchmesh->channels[2].material.state);
    ASSERT_EQ(66u, patchmesh->channels[2].flags);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &material_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    patchmesh = (nmo_patchmesh_state_t *)
        nmo_object_repository_find_by_id(repo, patchmesh_id)->state;
    ASSERT_EQ(0u, patchmesh->patch_count);
    ASSERT_EQ(NMO_REF_NONE, patchmesh->channels[0].material.state);
    ASSERT_EQ(44u, patchmesh->channels[0].flags);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_curve_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *curve_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_CURVE);
    ASSERT_NOT_NULL(curve_type);

    nmo_ref_t control_points[] = {
        nmo_ref_from_id(101), nmo_ref_from_raw(102),
    };
    nmo_curve_point_subchunk_t sub_points[] = {
        {.ref = nmo_ref_from_id(103), .chunk = NULL},
        {.ref = nmo_ref_from_raw(104), .chunk = NULL},
    };
    nmo_curve_state_t state = {0};
    state.control_point_count = 2;
    state.control_point_ids = control_points;
    state.sub_point_count = 2;
    state.sub_points = sub_points;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 103, 203));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 104, 204));

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, curve_type, &state, remap));
    ASSERT_EQ(201u, nmo_ref_runtime_id(&control_points[0]));
    ASSERT_EQ(101u, control_points[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, control_points[1].state);
    ASSERT_EQ(102u, control_points[1].raw_id);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&sub_points[0].ref));
    ASSERT_EQ(103u, sub_points[0].ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, sub_points[1].ref.state);
    ASSERT_EQ(104u, sub_points[1].ref.raw_id);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_place_portals) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *place_type =
        nmo_type_registry_find_by_class_id(type_rt->types, NMO_CID_PLACE);
    ASSERT_NOT_NULL(place_type);

    nmo_place_portal_entry_t entries[] = {
        {.place = nmo_ref_from_id(101), .portal = nmo_ref_from_id(102)},
        {.place = nmo_ref_from_raw(103), .portal = nmo_ref_from_raw(104)},
    };
    nmo_place_state_t state = {0};
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &state.portals, sizeof(nmo_place_portal_entry_t), 2, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&state.portals, entries, 2));

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    for (nmo_object_id_t old_id = 101; old_id <= 104; ++old_id) {
        ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, old_id, old_id + 100));
    }

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, place_type, &state, remap));
    nmo_place_portal_entry_t *remapped = NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &state.portals);
    ASSERT_EQ(201u, nmo_ref_runtime_id(&remapped[0].place));
    ASSERT_EQ(101u, remapped[0].place.raw_id);
    ASSERT_EQ(202u, nmo_ref_runtime_id(&remapped[0].portal));
    ASSERT_EQ(102u, remapped[0].portal.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped[1].place.state);
    ASSERT_EQ(103u, remapped[1].place.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, remapped[1].portal.state);
    ASSERT_EQ(104u, remapped[1].portal.raw_id);

    nmo_array_dispose(&state.portals);
    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_and_graph_include_skin_bones) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *entity_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_3DENTITY);
    ASSERT_NOT_NULL(entity_type);

    nmo_3dentity_skin_bone_t bones[] = {
        {.bone = nmo_ref_from_id(101)},
        {.bone = nmo_ref_from_raw(102)},
    };
    nmo_3dentity_skin_t skin = {
        .bone_count = 2,
        .bones = bones,
    };
    nmo_3dentity_state_t state = {0};
    state.skin = &skin;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 102, 202));
    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, entity_type, &state, remap));
    ASSERT_EQ(201u, nmo_ref_runtime_id(&bones[0].bone));
    ASSERT_EQ(101u, bones[0].bone.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, bones[1].bone.state);
    ASSERT_EQ(102u, bones[1].bone.raw_id);
    nmo_arena_destroy(arena);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_id_t owner_id = 0;
    nmo_object_id_t bone_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "owner", NMO_NULL_GUID,
        &owner_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "bone", NMO_NULL_GUID,
        &bone_id, NULL));
    nmo_3dentity_state_t *owner = (nmo_3dentity_state_t *)
        nmo_object_repository_find_by_id(repo, owner_id)->state;
    nmo_3dentity_skin_bone_t graph_bones[] = {
        {.bone = nmo_ref_from_id(bone_id)},
        {.bone = nmo_ref_from_raw(0x7FFFFF32u)},
    };
    nmo_3dentity_skin_t graph_skin = {
        .bone_count = 2,
        .bones = graph_bones,
    };
    owner->skin = &graph_skin;

    nmo_arena_t *graph_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(graph_arena);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(
        repo, type_rt->types, graph_arena);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *outgoing = NULL;
    size_t outgoing_count = 0;
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_object_edges(
        graph, owner_id, NMO_REF_DIR_OUTGOING,
        &outgoing, &outgoing_count));
    ASSERT_EQ(1u, outgoing_count);
    ASSERT_EQ(bone_id, outgoing[0].to);
    ASSERT_EQ(NMO_REF_KIND_SKIN_BONE, outgoing[0].kind);
    ASSERT_STR_EQ("skin.bones", outgoing[0].field_path);
    ASSERT_EQ(0u, outgoing[0].index);

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(graph_arena);
    owner->skin = NULL;
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, copy_remap_updates_only_resolved_dataarray_refs) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *dataarray_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_DATAARRAY);
    ASSERT_NOT_NULL(dataarray_type);

    nmo_dataarray_column_format_t formats[] = {
        {.type = CKARRAYTYPE_OBJECT},
        {.type = CKARRAYTYPE_OBJECT},
        {.type = CKARRAYTYPE_PARAMETER},
        {.type = CKARRAYTYPE_PARAMETER},
        {.type = CKARRAYTYPE_INT},
    };
    nmo_dataarray_cell_t cells[5] = {0};
    cells[0].object_ref = nmo_ref_from_id(101);
    cells[1].object_ref = nmo_ref_from_raw(102);
    cells[2].parameter.ref = nmo_ref_from_id(103);
    cells[3].parameter.ref = nmo_ref_from_raw(104);
    cells[4].int_value = 42;
    nmo_dataarray_row_t row = {
        .column_count = 5,
        .cells = cells,
    };
    nmo_dataarray_state_t state = {
        .column_count = 5,
        .column_formats = formats,
        .row_count = 1,
        .rows = &row,
    };

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    for (nmo_object_id_t old_id = 101; old_id <= 104; ++old_id) {
        ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, old_id, old_id + 100));
    }

    ASSERT_EQ(NMO_OK, nmo_runtime_remap_copy_refs(
        type_rt, dataarray_type, &state, remap));
    ASSERT_EQ(201u, nmo_ref_runtime_id(&cells[0].object_ref));
    ASSERT_EQ(101u, cells[0].object_ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, cells[1].object_ref.state);
    ASSERT_EQ(102u, cells[1].object_ref.raw_id);
    ASSERT_EQ(203u, nmo_ref_runtime_id(&cells[2].parameter.ref));
    ASSERT_EQ(103u, cells[2].parameter.ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, cells[3].parameter.ref.state);
    ASSERT_EQ(104u, cells[3].parameter.ref.raw_id);
    ASSERT_EQ(42, cells[4].int_value);
    ASSERT_EQ(5u, row.column_count);
    ASSERT_EQ(1u, state.row_count);

    nmo_arena_destroy(arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, dataarray_runtime_edits_validate_before_mutation) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *dataarray_type =
        nmo_type_registry_find_by_class_id(
            type_rt->types, NMO_CID_DATAARRAY);
    ASSERT_NOT_NULL(dataarray_type);

    nmo_dataarray_column_format_t copy_formats[] = {
        {.type = CKARRAYTYPE_OBJECT},
        {.type = (CK_ARRAYTYPE)99},
    };
    nmo_dataarray_cell_t copy_cells[2] = {0};
    copy_cells[0].object_ref = nmo_ref_from_id(101);
    nmo_dataarray_row_t copy_row = {
        .column_count = 2,
        .cells = copy_cells,
    };
    nmo_dataarray_state_t copy_state = {
        .column_count = 2,
        .column_formats = copy_formats,
        .row_count = 1,
        .rows = &copy_row,
    };
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 101, 201));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_runtime_remap_copy_refs(
        type_rt, dataarray_type, &copy_state, remap));
    ASSERT_EQ(101u, nmo_ref_runtime_id(&copy_cells[0].object_ref));
    nmo_arena_destroy(arena);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_id_t dataarray_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_DATAARRAY, "array", NMO_NULL_GUID,
        &dataarray_id, NULL));
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)
        nmo_object_repository_find_by_id(repo, dataarray_id)->state;
    ASSERT_NOT_NULL(state);
    nmo_dataarray_column_format_t normalize_formats[] = {
        {.type = CKARRAYTYPE_OBJECT},
        {.type = (CK_ARRAYTYPE)99},
    };
    nmo_dataarray_cell_t normalize_cells[2] = {0};
    normalize_cells[0].object_ref = nmo_ref_from_raw(0x7FFFFF61u);
    nmo_dataarray_row_t normalize_row = {
        .column_count = 2,
        .cells = normalize_cells,
    };
    state->column_count = 2;
    state->column_formats = normalize_formats;
    state->row_count = 1;
    state->rows = &normalize_row;

    size_t changed = 0;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_runtime_normalize_invalid_refs(repo, type_rt, &changed));
    ASSERT_EQ(0u, changed);
    ASSERT_EQ(NMO_REF_UNRESOLVED, normalize_cells[0].object_ref.state);
    ASSERT_EQ(0x7FFFFF61u, normalize_cells[0].object_ref.raw_id);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_and_safe_detach_preserve_skin_indices) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t valid_bone_id = 0;
    nmo_object_id_t deleted_bone_id = 0;
    nmo_object_id_t wrong_class_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "owner", NMO_NULL_GUID,
        &owner_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "valid-bone", NMO_NULL_GUID,
        &valid_bone_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "deleted-bone", NMO_NULL_GUID,
        &deleted_bone_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "wrong", NMO_NULL_GUID,
        &wrong_class_id, NULL));

    nmo_ref_t wrong_class = nmo_ref_from_id(wrong_class_id);
    wrong_class.state = NMO_REF_CLASS_MISMATCH;
    nmo_3dentity_skin_bone_t bones[] = {
        {.bone = nmo_ref_from_id(valid_bone_id), .bone_flags = 11},
        {.bone = nmo_ref_from_raw(0x7FFFFF33u), .bone_flags = 22},
        {.bone = wrong_class, .bone_flags = 33},
        {.bone = nmo_ref_from_id(deleted_bone_id), .bone_flags = 44},
    };
    uint32_t bone_indices[] = {0, 1, 2, 3};
    float bone_weights[] = {0.4f, 0.3f, 0.2f, 0.1f};
    nmo_3dentity_skin_vertex_t vertex = {
        .bone_count = 4,
        .bone_indices = bone_indices,
        .bone_weights = bone_weights,
    };
    nmo_3dentity_skin_t skin = {
        .bone_count = 4,
        .bones = bones,
        .vertex_count = 1,
        .vertices = &vertex,
    };
    nmo_3dentity_state_t *owner = (nmo_3dentity_state_t *)
        nmo_object_repository_find_by_id(repo, owner_id)->state;
    owner->skin = &skin;

    nmo_arena_t *graph_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(graph_arena);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(
        repo, nmo_context_get_type_runtime(ctx)->types, graph_arena);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *outgoing = NULL;
    size_t outgoing_count = 0;
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_object_edges(
        graph, owner_id, NMO_REF_DIR_OUTGOING,
        &outgoing, &outgoing_count));
    ASSERT_EQ(2u, outgoing_count);
    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(graph_arena);

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(2u, changed);
    ASSERT_EQ(4u, skin.bone_count);
    ASSERT_EQ(valid_bone_id, nmo_ref_runtime_id(&bones[0].bone));
    ASSERT_EQ(NMO_REF_NONE, bones[1].bone.state);
    ASSERT_EQ(NMO_REF_NONE, bones[2].bone.state);
    ASSERT_EQ(deleted_bone_id, nmo_ref_runtime_id(&bones[3].bone));

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &deleted_bone_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    ASSERT_EQ(4u, skin.bone_count);
    ASSERT_EQ(NMO_REF_NONE, bones[3].bone.state);
    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_EQ(i, bone_indices[i]);
        ASSERT_FLOAT_EQ(0.4f - 0.1f * (float)i, bone_weights[i], 0.0001f);
        ASSERT_EQ((i + 1u) * 11u, bones[i].bone_flags);
    }

    owner->skin = NULL;
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_clears_invalid_dataarray_cells) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t dataarray_id = 0;
    nmo_object_id_t object_id = 0;
    nmo_object_id_t parameter_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_DATAARRAY, "array", NMO_NULL_GUID,
        &dataarray_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "object", NMO_NULL_GUID,
        &object_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "parameter", NMO_NULL_GUID,
        &parameter_id, NULL));

    nmo_dataarray_column_format_t formats[] = {
        {.type = CKARRAYTYPE_OBJECT},
        {.type = CKARRAYTYPE_OBJECT},
        {.type = CKARRAYTYPE_PARAMETER},
        {.type = CKARRAYTYPE_PARAMETER},
        {.type = CKARRAYTYPE_INT},
    };
    nmo_dataarray_cell_t cells[5] = {0};
    cells[0].object_ref = nmo_ref_from_id(object_id);
    cells[1].object_ref = nmo_ref_from_raw(0x7FFFFF41u);
    cells[2].parameter.ref = nmo_ref_from_id(parameter_id);
    cells[3].parameter.ref = nmo_ref_from_id(object_id);
    cells[4].int_value = 42;
    nmo_dataarray_row_t row = {
        .column_count = 5,
        .cells = cells,
    };
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)
        nmo_object_repository_find_by_id(repo, dataarray_id)->state;
    state->column_count = 5;
    state->column_formats = formats;
    state->row_count = 1;
    state->rows = &row;

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(2u, changed);
    ASSERT_EQ(object_id, nmo_ref_runtime_id(&cells[0].object_ref));
    ASSERT_EQ(NMO_REF_NONE, cells[1].object_ref.state);
    ASSERT_EQ(parameter_id, nmo_ref_runtime_id(&cells[2].parameter.ref));
    ASSERT_EQ(NMO_REF_NONE, cells[3].parameter.ref.state);
    ASSERT_EQ(42, cells[4].int_value);
    ASSERT_EQ(5u, state->column_count);
    ASSERT_EQ(1u, state->row_count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, safe_detach_clears_dataarray_cells_in_place) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t dataarray_id = 0;
    nmo_object_id_t deleted_object_id = 0;
    nmo_object_id_t kept_object_id = 0;
    nmo_object_id_t deleted_parameter_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_DATAARRAY, "array", NMO_NULL_GUID,
        &dataarray_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "deleted-object", NMO_NULL_GUID,
        &deleted_object_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "kept-object", NMO_NULL_GUID,
        &kept_object_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PARAMETER, "deleted-parameter", NMO_NULL_GUID,
        &deleted_parameter_id, NULL));

    nmo_dataarray_column_format_t formats[] = {
        {.type = CKARRAYTYPE_OBJECT},
        {.type = CKARRAYTYPE_OBJECT},
        {.type = CKARRAYTYPE_PARAMETER},
        {.type = CKARRAYTYPE_OBJECT},
        {.type = CKARRAYTYPE_INT},
    };
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *parameter_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(parameter_chunk);
    nmo_dataarray_cell_t cells[5] = {0};
    cells[0].object_ref = nmo_ref_from_id(deleted_object_id);
    cells[1].object_ref = nmo_ref_from_id(kept_object_id);
    cells[2].parameter.ref = nmo_ref_from_id(deleted_parameter_id);
    cells[2].parameter.chunk = parameter_chunk;
    cells[3].object_ref = nmo_ref_from_raw(0x7FFFFF43u);
    cells[4].int_value = 42;
    nmo_dataarray_row_t row = {
        .column_count = 5,
        .cells = cells,
    };
    nmo_dataarray_state_t *state = (nmo_dataarray_state_t *)
        nmo_object_repository_find_by_id(repo, dataarray_id)->state;
    state->column_count = 5;
    state->column_formats = formats;
    state->row_count = 1;
    state->rows = &row;

    nmo_object_id_t deleted_ids[] = {
        deleted_object_id,
        deleted_parameter_id,
    };
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, deleted_ids, 2,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(2u, report.deleted_objects);
    ASSERT_EQ(NMO_REF_NONE, cells[0].object_ref.state);
    ASSERT_EQ(kept_object_id, nmo_ref_runtime_id(&cells[1].object_ref));
    ASSERT_EQ(NMO_REF_NONE, cells[2].parameter.ref.state);
    ASSERT_EQ(parameter_chunk, cells[2].parameter.chunk);
    ASSERT_EQ(NMO_REF_UNRESOLVED, cells[3].object_ref.state);
    ASSERT_EQ(0x7FFFFF43u, cells[3].object_ref.raw_id);
    ASSERT_EQ(42, cells[4].int_value);
    ASSERT_EQ(5u, state->column_count);
    ASSERT_EQ(1u, state->row_count);
    ASSERT_EQ(5u, row.column_count);

    nmo_session_destroy(session);
    nmo_chunk_destroy(parameter_chunk);
    nmo_arena_destroy(chunk_arena);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_and_safe_detach_keep_place_portals_atomic) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t destination_id = 0;
    nmo_object_id_t portal_id = 0;
    nmo_object_id_t wrong_class_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PLACE, "owner", NMO_NULL_GUID,
        &owner_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_PLACE, "destination", NMO_NULL_GUID,
        &destination_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_3DENTITY, "portal", NMO_NULL_GUID,
        &portal_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "wrong", NMO_NULL_GUID,
        &wrong_class_id, NULL));

    nmo_place_state_t *owner = (nmo_place_state_t *)
        nmo_object_repository_find_by_id(repo, owner_id)->state;
    ASSERT_NOT_NULL(owner);
    const nmo_place_portal_entry_t entries[] = {
        {
            .place = nmo_ref_from_id(destination_id),
            .portal = nmo_ref_from_id(portal_id),
        },
        {
            .place = nmo_ref_from_raw(0x7FFFFF31u),
            .portal = nmo_ref_from_id(portal_id),
        },
        {
            .place = nmo_ref_from_id(wrong_class_id),
            .portal = nmo_ref_from_id(wrong_class_id),
        },
    };
    ASSERT_EQ(NMO_OK, nmo_array_append_array(&owner->portals, entries, 3));

    nmo_arena_t *graph_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(graph_arena);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(
        repo, nmo_context_get_type_runtime(ctx)->types, graph_arena);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *outgoing = NULL;
    size_t outgoing_count = 0;
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_object_edges(
        graph, owner_id, NMO_REF_DIR_OUTGOING,
        &outgoing, &outgoing_count));
    ASSERT_EQ(5u, outgoing_count);
    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(graph_arena);

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(2u, changed);
    ASSERT_EQ(1u, owner->portals.count);
    nmo_place_portal_entry_t *remaining = NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &owner->portals);
    ASSERT_EQ(destination_id, nmo_ref_runtime_id(&remaining[0].place));
    ASSERT_EQ(portal_id, nmo_ref_runtime_id(&remaining[0].portal));

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &portal_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    owner = (nmo_place_state_t *)
        nmo_object_repository_find_by_id(repo, owner_id)->state;
    ASSERT_EQ(0u, owner->portals.count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(runtime_kernel, normalize_and_safe_detach_keep_curve_sections_independent) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t curve_id = 0;
    nmo_object_id_t point_a = 0;
    nmo_object_id_t point_b = 0;
    nmo_object_id_t wrong_class = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_CURVE, "curve", (nmo_guid_t){0, 0},
        &curve_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_CURVEPOINT, "point-a", (nmo_guid_t){0, 0},
        &point_a, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_CURVEPOINT, "point-b", (nmo_guid_t){0, 0},
        &point_b, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "wrong", (nmo_guid_t){0, 0},
        &wrong_class, NULL));

    nmo_curve_state_t *curve = (nmo_curve_state_t *)
        nmo_object_repository_find_by_id(repo, curve_id)->state;
    ASSERT_NOT_NULL(curve);
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);
    nmo_chunk_t *valid_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *unresolved_chunk = nmo_chunk_create(chunk_arena);
    nmo_chunk_t *wrong_chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(valid_chunk);
    ASSERT_NOT_NULL(unresolved_chunk);
    ASSERT_NOT_NULL(wrong_chunk);

    nmo_ref_t control_points[] = {
        nmo_ref_from_id(point_a),
        nmo_ref_from_raw(0x7FFFFF21u),
        nmo_ref_from_id(wrong_class),
    };
    nmo_curve_point_subchunk_t sub_points[] = {
        {.ref = nmo_ref_from_id(point_b), .chunk = valid_chunk},
        {.ref = nmo_ref_from_raw(0x7FFFFF22u), .chunk = unresolved_chunk},
        {.ref = nmo_ref_from_id(wrong_class), .chunk = wrong_chunk},
    };
    curve->control_point_count = 3;
    curve->control_point_ids = control_points;
    curve->sub_point_count = 3;
    curve->sub_points = sub_points;

    size_t changed = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_normalize_invalid_refs(
        repo, nmo_context_get_type_runtime(ctx), &changed));
    ASSERT_EQ(4u, changed);
    ASSERT_EQ(1u, curve->control_point_count);
    ASSERT_EQ(point_a,
              nmo_ref_runtime_id(&curve->control_point_ids[0]));
    ASSERT_EQ(1u, curve->sub_point_count);
    ASSERT_EQ(point_b, nmo_ref_runtime_id(&curve->sub_points[0].ref));
    ASSERT_EQ(valid_chunk, curve->sub_points[0].chunk);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK, nmo_session_destroy_objects(
        session, &point_a, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report));
    ASSERT_EQ(1u, report.deleted_objects);
    curve = (nmo_curve_state_t *)
        nmo_object_repository_find_by_id(repo, curve_id)->state;
    ASSERT_EQ(0u, curve->control_point_count);
    ASSERT_EQ(1u, curve->sub_point_count);
    ASSERT_EQ(point_b, nmo_ref_runtime_id(&curve->sub_points[0].ref));
    ASSERT_EQ(valid_chunk, curve->sub_points[0].chunk);

    nmo_session_destroy(session);
    nmo_arena_destroy(chunk_arena);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_kernel, execute_create_and_delete);
REGISTER_TEST(runtime_kernel, invalid_execute_arguments);
REGISTER_TEST(runtime_kernel, post_delete_runs_after_remove);
REGISTER_TEST(runtime_kernel, post_delete_runs_after_remove_non_object_type);
REGISTER_TEST(runtime_kernel, create_hook_failure_does_not_publish_object);
REGISTER_TEST(runtime_kernel, copy_preserves_internal_group_references);
REGISTER_TEST(runtime_kernel, copy_rejects_ambiguous_or_missing_sources_atomically);
REGISTER_TEST(runtime_kernel, copy_hook_failure_rolls_back_all_clones);
REGISTER_TEST(runtime_kernel, ref_graph_creation_fails_on_edge_allocation_error);
REGISTER_TEST(runtime_kernel, ref_graph_creation_propagates_enumerator_error);
REGISTER_TEST(runtime_kernel, delete_safe_detach_prunes_group_references);
REGISTER_TEST(runtime_kernel, delete_safe_detach_uses_explicit_object_type);
REGISTER_TEST(runtime_kernel, delete_safe_detach_prunes_behavior_links_with_deleted_io);
REGISTER_TEST(runtime_kernel, delete_cascade_removes_referencing_group);
REGISTER_TEST(runtime_kernel, deserialize_failure_does_not_publish_state_for_finalize);
REGISTER_TEST(runtime_kernel, normalize_removes_only_invalid_reference_records);
REGISTER_TEST(runtime_kernel, behavior_normalize_validates_lanes_before_mutation);
REGISTER_TEST(runtime_kernel, beobject_normalize_validates_attributes_before_mutation);
REGISTER_TEST(runtime_kernel, normalize_reports_malformed_ref_arrays_before_mutation);
REGISTER_TEST(runtime_kernel, normalize_clears_raw_scalar_class_mismatch);
REGISTER_TEST(runtime_kernel, normalize_preserves_explicitly_typed_reference_targets);
REGISTER_TEST(runtime_kernel, dependency_remap_preserves_invalid_references);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_scene_members);
REGISTER_TEST(runtime_kernel, copy_remap_rejects_malformed_reference_storage);
REGISTER_TEST(runtime_kernel, safe_detach_removes_scene_members_atomically);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_behaviorlink_endpoints);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_behavior_records);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_material_refs);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_keyedanimation_refs);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_objectanimation_refs);
REGISTER_TEST(runtime_kernel, safe_detach_keeps_keyedanimation_sections_independent);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_beobject_attributes);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_grid_layers);
REGISTER_TEST(runtime_kernel, rejects_malformed_grid_storage_before_runtime_mutation);
REGISTER_TEST(runtime_kernel, safe_detach_prunes_all_beobject_attribute_layouts);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_parameteroperation_refs);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_parameterout_refs);
REGISTER_TEST(runtime_kernel, dependency_remap_preserves_nonreference_state);
REGISTER_TEST(runtime_kernel, serializer_failure_does_not_reuse_raw_chunk);
REGISTER_TEST(runtime_kernel, copy_remap_preserves_invalid_character_references);
REGISTER_TEST(runtime_kernel, normalize_and_safe_detach_keep_character_parts_atomic);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_mesh_refs);
REGISTER_TEST(runtime_kernel, normalize_and_safe_detach_keep_mesh_records_atomic);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_patchmesh_refs);
REGISTER_TEST(runtime_kernel, normalize_and_safe_detach_keep_patchmesh_records_atomic);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_curve_refs);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_place_portals);
REGISTER_TEST(runtime_kernel, copy_remap_and_graph_include_skin_bones);
REGISTER_TEST(runtime_kernel, copy_remap_updates_only_resolved_dataarray_refs);
REGISTER_TEST(runtime_kernel, dataarray_runtime_edits_validate_before_mutation);
REGISTER_TEST(runtime_kernel, normalize_and_safe_detach_preserve_skin_indices);
REGISTER_TEST(runtime_kernel, normalize_clears_invalid_dataarray_cells);
REGISTER_TEST(runtime_kernel, safe_detach_clears_dataarray_cells_in_place);
REGISTER_TEST(runtime_kernel, normalize_and_safe_detach_keep_place_portals_atomic);
REGISTER_TEST(runtime_kernel, normalize_and_safe_detach_keep_curve_sections_independent);
TEST_MAIN_END()

