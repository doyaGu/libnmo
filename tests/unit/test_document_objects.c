/**
 * @file test_document_objects.c
 * @brief The objects of a document by id
 */

#include "../test_framework.h"

#include "document/nmo_document_load.h"
#include "document/nmo_document_objects.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "yyjson.h"

#include <string.h>

#define BOX "data/Ballance/P_Box.nmo"

static nmo_document_t *load(nmo_context_t *ctx, nmo_load_profile_t profile)
{
    nmo_load_options_t options = nmo_load_options_default();
    options.profile = profile;
    nmo_document_t *document = NULL;
    return nmo_document_load_file(ctx, BOX, &options, &document) == NMO_OK ? document : NULL;
}

/* The value of a field in an object's JSON, as text: "" when absent */
static void field_value(nmo_document_t *document, nmo_object_id_t id, const char *name,
                        char *out, size_t size)
{
    out[0] = '\0';
    char *json = NULL;
    size_t length = 0;
    if (nmo_document_object_json(document, id, &json, &length) != NMO_OK) return;
    yyjson_doc *doc = yyjson_read(json, length, 0);
    yyjson_val *field;
    size_t i, max;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc), "fields"), i, max, field) {
        if (strcmp(yyjson_get_str(yyjson_obj_get(field, "name")), name) == 0) {
            yyjson_val *value = yyjson_obj_get(field, "value");
            if (yyjson_is_int(value)) snprintf(out, size, "%lld", (long long)yyjson_get_sint(value));
            else if (yyjson_is_str(value)) snprintf(out, size, "%s", yyjson_get_str(value));
        }
    }
    yyjson_doc_free(doc);
    nmo_document_free_json(json);
}

static const char priority_five[] =
    "{\"objects\":[{\"id\":%u,\"class_name\":\"CK3dObject\",\"name\":\"P_Box_MF\","
    "\"fields\":[{\"name\":\"priority\",\"owner_type_guid\":\"{71D80779-402F42F3}\","
    "\"type_guid\":\"{5A5716FD-44E276D7}\",\"kind\":\"scalar\",\"value\":5}]}]}";

TEST(document_objects, names_classes_fields_and_edits_by_id)
{
    TEST_REQUIRE_FILE(BOX);
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = load(ctx, NMO_LOAD_PROFILE_FULL);
    ASSERT_NOT_NULL(document);

    const size_t count = nmo_document_object_count(document);
    ASSERT_EQ(6u, count);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_document_object_at(document, count));
    nmo_object_id_t box = NMO_OBJECT_ID_NONE;
    for (size_t i = 0; i < count; ++i) {
        const nmo_object_id_t id = nmo_document_object_at(document, i);
        ASSERT_TRUE(id != NMO_OBJECT_ID_NONE);
        if (nmo_document_object_class(document, id) == NMO_CID_3DOBJECT) box = id;
    }
    ASSERT_TRUE(box != NMO_OBJECT_ID_NONE);
    ASSERT_STR_EQ("P_Box_MF", nmo_document_object_name(document, box));
    ASSERT_STR_EQ("CK3dObject", nmo_document_class_name(document, NMO_CID_3DOBJECT));
    ASSERT_EQ((nmo_class_id_t)NMO_CID_3DENTITY, nmo_document_class_id(document, "CK3dEntity"));
    ASSERT_EQ(0u, nmo_document_class_id(document, "NoSuchClass"));
    ASSERT_TRUE(nmo_document_class_is_derived(document, NMO_CID_3DOBJECT, NMO_CID_3DENTITY));
    ASSERT_TRUE(nmo_document_class_is_derived(document, NMO_CID_3DOBJECT, NMO_CID_3DOBJECT));
    ASSERT_FALSE(nmo_document_class_is_derived(document, NMO_CID_MESH, NMO_CID_3DENTITY));

    char value[128];
    field_value(document, box, "priority", value, sizeof(value));
    ASSERT_STR_EQ("0", value);
    char edit[512];
    snprintf(edit, sizeof(edit), priority_five, (unsigned)box);
    size_t written = 0;
    ASSERT_EQ(NMO_OK, nmo_document_import_json(document, edit, strlen(edit), &written));
    ASSERT_EQ(1u, written);
    field_value(document, box, "priority", value, sizeof(value));
    ASSERT_STR_EQ("5", value);

    /* An id that names no object */
    const nmo_object_id_t missing = 0x7FFFFFFFu;
    ASSERT_NULL(nmo_document_object_name(document, missing));
    ASSERT_EQ(0u, nmo_document_object_class(document, missing));
    char *json = (char *)1;
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_document_object_json(document, missing, &json, NULL));
    ASSERT_NULL(json);
    nmo_document_free_json(NULL);
    ASSERT_EQ(0u, nmo_document_object_count(NULL));

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(document_objects, a_partial_document_is_not_edited)
{
    TEST_REQUIRE_FILE(BOX);
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = load(ctx, NMO_LOAD_PROFILE_OBJECTS);
    ASSERT_NOT_NULL(document);
    char edit[512];
    snprintf(edit, sizeof(edit), priority_five, 5u);
    size_t written = 99;
    ASSERT_EQ(NMO_ERR_INVALID_STATE, nmo_document_import_json(document, edit, strlen(edit), &written));
    ASSERT_EQ(0u, written);
    /* Its objects have no state until one is asked for */
    char value[128];
    field_value(document, 5u, "priority", value, sizeof(value));
    ASSERT_STR_EQ("", value);
    ASSERT_EQ(NMO_OK, nmo_document_load_object_state(document, 5u));
    field_value(document, 5u, "priority", value, sizeof(value));
    ASSERT_STR_EQ("0", value);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(document_objects, names_classes_fields_and_edits_by_id);
    REGISTER_TEST(document_objects, a_partial_document_is_not_edited);
TEST_MAIN_END()
