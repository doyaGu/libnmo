/**
 * @file test_app_util.c
 * @brief Tests for app utility helpers migrated from tools.
 */

#include "test_framework.h"
#include "nmo.h"

#include "app/nmo_json_util.h"
#include "core/nmo_hex.h"
#include "core/nmo_path.h"
#include "session/nmo_session_util.h"
#include "core/nmo_utils.h"
#include "type/nmo_type_query.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>

TEST(app_util, session_open_close) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256] = {0};

    bool ok = nmo_session_open_file_with_context(
        NMO_TEST_DATA_FILE("Nop.cmo"), &ctx, &session, errbuf, sizeof(errbuf));
    ASSERT_TRUE(ok);
    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(session);

    nmo_session_close_with_context(ctx, session);
}

TEST(app_util, session_open_invalid_args) {
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[64] = {0};

    bool ok = nmo_session_open_file_with_context(NULL, &ctx, &session, errbuf, sizeof(errbuf));
    ASSERT_FALSE(ok);
    ASSERT_STR_EQ("Invalid arguments", errbuf);
}

TEST(app_util, type_query_roundtrip) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const char *name = nmo_type_query_class_name_from_id(nmo_context_get_type_registry(ctx), NMO_CID_OBJECT);
    ASSERT_NOT_NULL(name);
    ASSERT_TRUE(name[0] != '\0');

    nmo_class_id_t class_id = nmo_type_query_class_id_from_name(nmo_context_get_type_registry(ctx), name);
    ASSERT_EQ(NMO_CID_OBJECT, class_id);

    ASSERT_TRUE(nmo_type_query_class_is_derived_from(nmo_context_get_type_registry(ctx), NMO_CID_CAMERA, NMO_CID_OBJECT));
    ASSERT_FALSE(nmo_type_query_class_is_derived_from(nmo_context_get_type_registry(ctx), NMO_CID_OBJECT, NMO_CID_CAMERA));

    nmo_session_t *session = nmo_session_load(ctx, NMO_TEST_DATA_FILE("Nop.cmo"));
    ASSERT_NOT_NULL(session);

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    ASSERT_EQ(NMO_OK, nmo_session_get_objects(session, &objects, &object_count));
    ASSERT_NOT_NULL(objects);
    ASSERT_TRUE(object_count > 0);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_object_t *obj = objects[0];
    ASSERT_TRUE(nmo_type_query_object_is_derived_from_guid(registry, obj, CKPGUID_OBJECT));
    ASSERT_NOT_NULL(nmo_type_query_object_get_ancestor_state_by_guid(registry, obj, CKPGUID_OBJECT));

    nmo_session_destroy(session);

    nmo_context_release(ctx);
}

TEST(app_util, json_util_sanitizes_invalid_utf8) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    ASSERT_NOT_NULL(doc);

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    ASSERT_NOT_NULL(root);
    yyjson_mut_doc_set_root(doc, root);

    const char invalid_name[] = { 'A', (char)0xFF, 'B', '\0' };
    ASSERT_TRUE(nmo_json_add_str_safe(doc, root, "name", invalid_name));

    yyjson_mut_val *name = yyjson_mut_obj_get(root, "name");
    yyjson_mut_val *raw_hex = yyjson_mut_obj_get(root, "name_raw_hex");
    yyjson_mut_val *raw_len = yyjson_mut_obj_get(root, "name_raw_len");
    ASSERT_NOT_NULL(name);
    ASSERT_NOT_NULL(raw_hex);
    ASSERT_NOT_NULL(raw_len);

    yyjson_mut_doc_free(doc);
}

TEST(app_util, hex_util_roundtrip) {
    char byte_hex[2] = {0};
    nmo_hex_write_byte(byte_hex, 0xAF, true);
    ASSERT_EQ('A', byte_hex[0]);
    ASSERT_EQ('F', byte_hex[1]);

    const uint8_t bytes[] = {0x00, 0x12, 0xAB, 0xFF};
    char *hex = nmo_hex_bytes_to_string(bytes, sizeof(bytes), true);
    ASSERT_NOT_NULL(hex);
    ASSERT_STR_EQ("0012ABFF", hex);
    free(hex);
}

TEST(app_util, text_escape_bytes) {
    char *copy = nmo_text_strdup_or_empty(NULL);
    ASSERT_NOT_NULL(copy);
    ASSERT_STR_EQ("", copy);
    free(copy);

    const char raw[] = {'A', '\n', 'B', (char)0xFF, '\0'};
    char *escaped = nmo_text_escape_bytes(raw);
    ASSERT_NOT_NULL(escaped);
    ASSERT_STR_EQ("A\\x0AB\\xFF", escaped);
    free(escaped);
}

TEST(app_util, path_basename) {
    ASSERT_STR_EQ("", nmo_path_basename(NULL));
    ASSERT_STR_EQ("", nmo_path_basename(""));
    ASSERT_STR_EQ("file.cmo", nmo_path_basename("C:\\tmp\\file.cmo"));
    ASSERT_STR_EQ("abc.nmo", nmo_path_basename("/var/data/abc.nmo"));
    ASSERT_STR_EQ("", nmo_path_basename("C:\\tmp\\"));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(app_util, session_open_close);
    REGISTER_TEST(app_util, session_open_invalid_args);
    REGISTER_TEST(app_util, type_query_roundtrip);
    REGISTER_TEST(app_util, json_util_sanitizes_invalid_utf8);
    REGISTER_TEST(app_util, hex_util_roundtrip);
    REGISTER_TEST(app_util, text_escape_bytes);
    REGISTER_TEST(app_util, path_basename);
TEST_MAIN_END()
