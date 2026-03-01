#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
static nmo_object_id_t g_runtime_delete_probe_id = 0;
static int g_runtime_post_delete_called = 0;
static int g_runtime_post_delete_after_remove = 0;
static nmo_object_id_t g_runtime_scene_probe_id = 0;
static int g_runtime_scene_post_delete_called = 0;
static int g_runtime_scene_post_delete_after_remove = 0;

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

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_kernel, execute_create_and_delete);
REGISTER_TEST(runtime_kernel, invalid_execute_arguments);
REGISTER_TEST(runtime_kernel, post_delete_runs_after_remove);
REGISTER_TEST(runtime_kernel, post_delete_runs_after_remove_non_object_type);
TEST_MAIN_END()
