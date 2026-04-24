/**
 * @file test_builder.c
 * @brief Unit tests for builder object staging APIs
 */

#include "test_framework.h"
#include "session/nmo_builder.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_type_runtime.h"
#include <stdio.h>

static nmo_builder_t *create_builder_with_context_path(nmo_context_t **out_ctx, const char *path) {
    if (out_ctx == NULL) {
        return NULL;
    }

    *out_ctx = NULL;

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (ctx == NULL) {
        return NULL;
    }

    const nmo_type_runtime_t *runtime = nmo_context_get_type_runtime(ctx);
    if (runtime == NULL) {
        nmo_context_release(ctx);
        return NULL;
    }

    nmo_builder_t *builder = nmo_builder_create(path ? path : "dummy.nmo", runtime);
    if (builder == NULL) {
        nmo_context_release(ctx);
        return NULL;
    }

    *out_ctx = ctx;
    return builder;
}

static nmo_builder_t *create_builder_with_context(nmo_context_t **out_ctx) {
    return create_builder_with_context_path(out_ctx, "dummy.nmo");
}

TEST(builder, add_object_success_and_dedup) {
    nmo_context_t *ctx = NULL;
    nmo_builder_t *builder = create_builder_with_context(&ctx);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(builder);

    const uint8_t payload[] = { 0x10, 0x20, 0x30, 0x40 };
    nmo_status_t status = nmo_builder_add_object(builder, 1, 42, payload, sizeof(payload));
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1, (int)nmo_builder_get_object_count(builder));
    ASSERT_NULL(nmo_builder_get_error(builder));

    status = nmo_builder_add_object(builder, 1, 42, payload, sizeof(payload));
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1, (int)nmo_builder_get_object_count(builder));

    nmo_builder_destroy(builder);
    nmo_context_release(ctx);
}

TEST(builder, add_object_invalid_payload) {
    nmo_context_t *ctx = NULL;
    nmo_builder_t *builder = create_builder_with_context(&ctx);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(builder);

    nmo_status_t status = nmo_builder_add_object(builder, 7, 99, NULL, 8);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, status);
    ASSERT_EQ(0, (int)nmo_builder_get_object_count(builder));

    const char *error = nmo_builder_get_error(builder);
    ASSERT_NOT_NULL(error);
    ASSERT_STR_CONTAINS(error, "data must be non-NULL");

    nmo_builder_destroy(builder);
    nmo_context_release(ctx);
}

TEST(builder, add_object_large_id_grows_mask) {
    nmo_context_t *ctx = NULL;
    nmo_builder_t *builder = create_builder_with_context(&ctx);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(builder);

    const uint32_t large_id = 5000;
    nmo_status_t status = nmo_builder_add_object(builder, large_id, 3, NULL, 0);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1, (int)nmo_builder_get_object_count(builder));

    status = nmo_builder_add_object(builder, large_id, 3, NULL, 0);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1, (int)nmo_builder_get_object_count(builder));

    nmo_builder_destroy(builder);
    nmo_context_release(ctx);
}

TEST(builder, add_object_rejects_reserved_ids) {
    nmo_context_t *ctx = NULL;
    nmo_builder_t *builder = create_builder_with_context(&ctx);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(builder);

    nmo_status_t status = nmo_builder_add_object(builder, NMO_OBJECT_ID_NONE, 1, NULL, 0);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, status);

    status = nmo_builder_add_object(builder, NMO_OBJECT_ID_INVALID, 1, NULL, 0);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, status);

    nmo_builder_destroy(builder);
    nmo_context_release(ctx);
}

TEST(builder, add_object_ex_writes_class_and_name) {
    const char *out_path = "test_builder_add_object_ex.nmo";

    nmo_context_t *ctx = NULL;
    nmo_builder_t *builder = create_builder_with_context_path(&ctx, out_path);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(builder);

    nmo_guid_t guid = {0x11223344u, 0x55667788u};
    nmo_status_t status = nmo_builder_add_object_ex(
        builder, 1, 0, (uint32_t)NMO_CID_SCENE, "TestObject", guid, 0, NULL, 0);
    ASSERT_EQ(NMO_OK, status);

    status = nmo_builder_finish(builder);
    ASSERT_EQ(NMO_OK, status);

    nmo_builder_destroy(builder);

    nmo_session_t *session = nmo_session_load(ctx, out_path);
    ASSERT_NOT_NULL(session);

    nmo_object_t **objects = NULL;
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_session_get_objects(session, &objects, &count));
    ASSERT_EQ(1, (int)count);
    ASSERT_NOT_NULL(objects);

    nmo_object_t *obj = objects[0];
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_CID_SCENE, nmo_object_get_class_id(obj));
    ASSERT_STR_EQ("TestObject", nmo_object_get_name(obj));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    (void)remove(out_path);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(builder, add_object_success_and_dedup);
    REGISTER_TEST(builder, add_object_invalid_payload);
    REGISTER_TEST(builder, add_object_large_id_grows_mask);
    REGISTER_TEST(builder, add_object_rejects_reserved_ids);
    REGISTER_TEST(builder, add_object_ex_writes_class_and_name);
TEST_MAIN_END()

