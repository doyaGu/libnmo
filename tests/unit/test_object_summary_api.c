/**
 * @file test_object_summary_api.c
 * @brief Direct tests for app-level object summary API.
 */

#include "test_framework.h"
#include "nmo.h"

#include "object/nmo_object_summary.h"
#include "session/nmo_session_util.h"
#include "type/nmo_reflection.h"
#include "yyjson.h"

#include <stdalign.h>
#include <string.h>

typedef struct summary_raw_array_state {
    uint32_t item_count;
    uint32_t *items;
} summary_raw_array_state_t;

static const nmo_type_field_t summary_raw_array_fields[] = {
    NMO_FIELD(summary_raw_array_state_t, item_count, CKPGUID_UINT32),
    NMO_FIELD_PTR_ARRAY(summary_raw_array_state_t, items, item_count, CKPGUID_UINT32),
};

static const nmo_guid_t summary_raw_array_guid = NMO_GUID_INIT(0x51A4E001u, 0x00000001u);

static yyjson_mut_val *find_summary_field(yyjson_mut_val *fields, const char *name) {
    yyjson_mut_arr_iter iter;
    yyjson_mut_arr_iter_init(fields, &iter);
    yyjson_mut_val *field = NULL;
    while ((field = yyjson_mut_arr_iter_next(&iter)) != NULL) {
        yyjson_mut_val *field_name = yyjson_mut_obj_get(field, "name");
        if (field_name && strcmp(yyjson_mut_get_str(field_name), name) == 0) {
            return field;
        }
    }
    return NULL;
}

static nmo_object_t *find_reflective_object(nmo_context_t *ctx, nmo_session_t *session) {
    nmo_object_t **objects = NULL;
    size_t count = 0;
    if (nmo_session_get_objects(session, &objects, &count) != NMO_OK || !objects || count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *obj = objects[i];
        if (!obj) {
            continue;
        }
        if (nmo_summary_has_reflection(ctx, nmo_object_get_class_id(obj))) {
            return obj;
        }
    }
    return NULL;
}

TEST(object_summary_api, summarize_to_text_and_json) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    bool ok = nmo_session_open_file_with_context(
        NMO_TEST_DATA_FILE("Nop.cmo"), &ctx, &session, errbuf, sizeof(errbuf));
    ASSERT_TRUE(ok);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_t *obj = find_reflective_object(ctx, session);
    ASSERT_NOT_NULL(obj);

    nmo_summary_output_t text_out = {
        .stream = stdout,
        .json_doc = NULL,
        .json_data = NULL,
        .is_json = false,
        .colorize = false,
        .ctx = ctx,
        .session = session,
    };
    ASSERT_TRUE(nmo_object_summary(obj, &text_out));

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    ASSERT_NOT_NULL(doc);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    ASSERT_NOT_NULL(data);
    yyjson_mut_doc_set_root(doc, data);

    nmo_summary_output_t json_out = {
        .stream = NULL,
        .json_doc = doc,
        .json_data = data,
        .is_json = true,
        .colorize = false,
        .ctx = ctx,
        .session = session,
    };
    ASSERT_TRUE(nmo_object_summary(obj, &json_out));

    yyjson_mut_val *class_id = yyjson_mut_obj_get(data, "class_id");
    yyjson_mut_val *class_name = yyjson_mut_obj_get(data, "class_name");
    yyjson_mut_val *type_guid = yyjson_mut_obj_get(data, "type_guid");
    yyjson_mut_val *type_name = yyjson_mut_obj_get(data, "type_name");
    yyjson_mut_val *has_reflection = yyjson_mut_obj_get(data, "has_reflection");
    ASSERT_NOT_NULL(class_id);
    ASSERT_TRUE(yyjson_mut_is_uint(class_id));
    ASSERT_NOT_NULL(class_name);
    ASSERT_TRUE(yyjson_mut_is_str(class_name));
    ASSERT_NOT_NULL(type_guid);
    ASSERT_TRUE(yyjson_mut_is_str(type_guid));
    ASSERT_NOT_NULL(type_name);
    ASSERT_TRUE(yyjson_mut_is_str(type_name));
    ASSERT_NOT_NULL(has_reflection);
    ASSERT_TRUE(yyjson_mut_is_bool(has_reflection));
    ASSERT_TRUE(yyjson_mut_get_bool(has_reflection));

    yyjson_mut_val *fields = yyjson_mut_obj_get(data, "fields");
    ASSERT_NOT_NULL(fields);

    yyjson_mut_doc_free(doc);
    nmo_session_close_with_context(ctx, session);
}

TEST(object_summary_api, snapshot_raw_pointer_array_emits_full_items) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_type_descriptor_t desc = {
        .guid = summary_raw_array_guid,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = 0,
        .name = "SummaryRawArrayState",
        .description = NULL,
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .size = (uint32_t)sizeof(summary_raw_array_state_t),
        .alignment = (uint32_t)alignof(summary_raw_array_state_t),
        .fields = summary_raw_array_fields,
        .field_count = sizeof(summary_raw_array_fields) / sizeof(summary_raw_array_fields[0]),
        .vtable = NULL,
        .creator_plugin_guid = NMO_NULL_GUID,
        .saver_manager = 0,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
        .version = 0,
        .min_compatible_version = 0,
        .ext = NULL,
    };
    nmo_status_t status = nmo_type_registry_begin_update(nmo_context_get_type_registry(ctx));
    ASSERT_EQ(NMO_OK, status);
    status = nmo_type_registry_register(nmo_context_get_type_registry(ctx), &desc);
    ASSERT_EQ(NMO_OK, status);

    uint32_t item_values[] = {10u, 20u};
    summary_raw_array_state_t state = {
        .item_count = 2u,
        .items = item_values,
    };

    nmo_object_t *obj = nmo_object_create(NULL, 100u, 0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(obj, summary_raw_array_guid));
    ASSERT_EQ(NMO_OK, nmo_object_alloc_state(obj, sizeof(state)));
    memcpy(nmo_object_get_state(obj), &state, sizeof(state));

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    ASSERT_NOT_NULL(doc);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    ASSERT_NOT_NULL(data);
    yyjson_mut_doc_set_root(doc, data);

    nmo_summary_output_t json_out = {
        .stream = NULL,
        .json_doc = doc,
        .json_data = data,
        .is_json = true,
        .colorize = false,
        .ctx = ctx,
        .session = NULL,
    };
    ASSERT_TRUE(nmo_object_summary(obj, &json_out));

    yyjson_mut_val *type_name = yyjson_mut_obj_get(data, "type_name");
    ASSERT_NOT_NULL(type_name);
    ASSERT_STR_EQ("SummaryRawArrayState", yyjson_mut_get_str(type_name));
    yyjson_mut_val *type_guid = yyjson_mut_obj_get(data, "type_guid");
    ASSERT_NOT_NULL(type_guid);
    char guid_buf[32];
    nmo_guid_format(summary_raw_array_guid, guid_buf, sizeof(guid_buf));
    ASSERT_STR_EQ(guid_buf, yyjson_mut_get_str(type_guid));
    yyjson_mut_val *class_id = yyjson_mut_obj_get(data, "class_id");
    ASSERT_NOT_NULL(class_id);
    ASSERT_EQ(0u, yyjson_mut_get_uint(class_id));
    ASSERT_NULL(yyjson_mut_obj_get(data, "class_name"));
    yyjson_mut_val *has_reflection = yyjson_mut_obj_get(data, "has_reflection");
    ASSERT_NOT_NULL(has_reflection);
    ASSERT_TRUE(yyjson_mut_get_bool(has_reflection));

    yyjson_mut_val *fields = yyjson_mut_obj_get(data, "fields");
    ASSERT_NOT_NULL(fields);
    ASSERT_TRUE(yyjson_mut_is_arr(fields));
    yyjson_mut_val *items = find_summary_field(fields, "items");
    ASSERT_NOT_NULL(items);
    yyjson_mut_val *kind = yyjson_mut_obj_get(items, "kind");
    ASSERT_NOT_NULL(kind);
    ASSERT_STR_EQ("array", yyjson_mut_get_str(kind));
    yyjson_mut_val *count = yyjson_mut_obj_get(items, "count");
    ASSERT_NOT_NULL(count);
    ASSERT_EQ(2u, yyjson_mut_get_uint(count));
    yyjson_mut_val *values = yyjson_mut_obj_get(items, "items");
    ASSERT_NOT_NULL(values);
    ASSERT_TRUE(yyjson_mut_is_arr(values));
    ASSERT_EQ(2u, yyjson_mut_arr_size(values));
    ASSERT_EQ(10u, yyjson_mut_get_uint(yyjson_mut_arr_get(values, 0)));
    ASSERT_EQ(20u, yyjson_mut_get_uint(yyjson_mut_arr_get(values, 1)));
    ASSERT_NULL(yyjson_mut_obj_get(items, "preview"));
    ASSERT_NULL(yyjson_mut_obj_get(items, "is_array"));
    ASSERT_NULL(yyjson_mut_obj_get(items, "value_str"));

    yyjson_mut_doc_free(doc);
    nmo_object_destroy(obj);
    nmo_context_release(ctx);
}

TEST(object_summary_api, structured_stats_are_available_without_rendering) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    bool ok = nmo_session_open_file_with_context(
        NMO_TEST_DATA_FILE("Nop.cmo"), &ctx, &session, errbuf, sizeof(errbuf));
    ASSERT_TRUE(ok);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_t *obj = find_reflective_object(ctx, session);
    ASSERT_NOT_NULL(obj);

    nmo_object_summary_stats_t stats;
    ASSERT_EQ(NMO_OK, nmo_object_summary_collect_stats(ctx, obj, &stats));
    ASSERT_TRUE(stats.has_reflection);
    ASSERT_TRUE(stats.total_fields > 0);
    ASSERT_TRUE(stats.total_fields >= stats.array_fields);
    ASSERT_TRUE(stats.total_fields >= stats.reference_fields);
    ASSERT_TRUE(stats.total_fields >= stats.optional_fields);
    ASSERT_TRUE(stats.total_fields >= stats.object_ref_fields);
    ASSERT_NOT_NULL(stats.class_name);
    ASSERT_NOT_NULL(stats.type_name);
    ASSERT_FALSE(nmo_guid_is_null(stats.type_guid));

    nmo_session_close_with_context(ctx, session);
}

TEST(object_summary_api, structured_stats_include_stable_type_metadata_for_typed_object) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_type_descriptor_t desc = {
        .guid = summary_raw_array_guid,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = 0,
        .name = "SummaryRawArrayState",
        .description = NULL,
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .size = (uint32_t)sizeof(summary_raw_array_state_t),
        .alignment = (uint32_t)alignof(summary_raw_array_state_t),
        .fields = summary_raw_array_fields,
        .field_count = sizeof(summary_raw_array_fields) / sizeof(summary_raw_array_fields[0]),
        .vtable = NULL,
        .creator_plugin_guid = NMO_NULL_GUID,
        .saver_manager = 0,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
        .version = 0,
        .min_compatible_version = 0,
        .ext = NULL,
    };
    nmo_status_t status = nmo_type_registry_begin_update(nmo_context_get_type_registry(ctx));
    ASSERT_EQ(NMO_OK, status);
    status = nmo_type_registry_register(nmo_context_get_type_registry(ctx), &desc);
    ASSERT_EQ(NMO_OK, status);

    summary_raw_array_state_t state = {0};
    nmo_object_t *obj = nmo_object_create(NULL, 200u, 0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(obj, summary_raw_array_guid));
    ASSERT_EQ(NMO_OK, nmo_object_alloc_state(obj, sizeof(state)));
    memcpy(nmo_object_get_state(obj), &state, sizeof(state));

    nmo_object_summary_stats_t stats;
    ASSERT_EQ(NMO_OK, nmo_object_summary_collect_stats(ctx, obj, &stats));
    ASSERT_TRUE(nmo_guid_equals(summary_raw_array_guid, stats.type_guid));
    ASSERT_STR_EQ("SummaryRawArrayState", stats.type_name);
    ASSERT_EQ(0u, stats.class_id);
    ASSERT_NULL(stats.class_name);
    ASSERT_TRUE(stats.has_reflection);
    ASSERT_EQ(2u, stats.total_fields);
    ASSERT_EQ(1u, stats.array_fields);
    ASSERT_EQ(0u, stats.reference_fields);
    ASSERT_EQ(0u, stats.optional_fields);
    ASSERT_EQ(0u, stats.object_ref_fields);

    nmo_object_destroy(obj);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_summary_api, summarize_to_text_and_json);
    REGISTER_TEST(object_summary_api, snapshot_raw_pointer_array_emits_full_items);
    REGISTER_TEST(object_summary_api, structured_stats_are_available_without_rendering);
    REGISTER_TEST(object_summary_api, structured_stats_include_stable_type_metadata_for_typed_object);
TEST_MAIN_END()
