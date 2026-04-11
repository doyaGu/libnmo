/**
 * @file test_object_summary_api.c
 * @brief Direct tests for app-level object summary API.
 */

#include "test_framework.h"
#include "nmo.h"

#include "app/nmo_object_summary.h"
#include "session/nmo_session_util.h"
#include "yyjson.h"

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

    yyjson_mut_val *fields = yyjson_mut_obj_get(data, "fields");
    ASSERT_NOT_NULL(fields);

    yyjson_mut_doc_free(doc);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_summary_api, summarize_to_text_and_json);
TEST_MAIN_END()
