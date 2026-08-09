#include "test_framework.h"

#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__MINGW32__) || defined(__MINGW64__)
#define NMO_POPEN popen
#define NMO_PCLOSE pclose
#elif defined(_WIN32)
#define NMO_POPEN _popen
#define NMO_PCLOSE _pclose
#else
#define NMO_POPEN popen
#define NMO_PCLOSE pclose
#endif

#ifndef NMO_CLI_PATH
#define NMO_CLI_PATH "nmo"
#endif

#ifndef NMO_SOURCE_DIR
#define NMO_SOURCE_DIR "."
#endif

static char *run_cli(const char *args) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);
    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) return NULL;

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        NMO_PCLOSE(pipe);
        return NULL;
    }
    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t clen = strlen(chunk);
        if (len + clen + 1 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                NMO_PCLOSE(pipe);
                return NULL;
            }
            buf = nb;
        }
        memcpy(buf + len, chunk, clen);
        len += clen;
    }
    buf[len] = '\0';
    NMO_PCLOSE(pipe);
    return buf;
}

static yyjson_doc *run_cli_json(const char *args) {
    char full[2048];
    snprintf(full, sizeof(full), "-f json %s", args);
    char *out = run_cli(full);
    if (!out) return NULL;
    yyjson_doc *doc = yyjson_read(out, strlen(out), 0);
    free(out);
    return doc;
}

static yyjson_val *json_data(yyjson_doc *doc) {
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root) return NULL;
    return yyjson_obj_get(root, "data");
}

static int create_rename_fixture_files(const char *file1, const char *file2) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) return 0;

    nmo_session_t *s1 = nmo_session_create(ctx);
    nmo_session_t *s2 = nmo_session_create(ctx);
    if (!s1 || !s2) {
        if (s1) nmo_session_destroy(s1);
        if (s2) nmo_session_destroy(s2);
        nmo_context_release(ctx);
        return 0;
    }

    const nmo_allocator_t *alloc = nmo_context_get_allocator(ctx);
    nmo_object_repository_t *r1 = nmo_session_get_repository(s1);
    nmo_object_repository_t *r2 = nmo_session_get_repository(s2);
    nmo_object_t *o1 = nmo_object_create(alloc, 1, 1);
    nmo_object_t *o2 = nmo_object_create(alloc, 1, 1);
    if (!o1 || !o2) {
        if (o1) nmo_object_destroy(o1);
        if (o2) nmo_object_destroy(o2);
        nmo_session_destroy(s1);
        nmo_session_destroy(s2);
        nmo_context_release(ctx);
        return 0;
    }
    if (nmo_object_set_name(o1, "ObjectBefore") != NMO_OK ||
        nmo_object_set_name(o2, "ObjectAfter") != NMO_OK) {
        nmo_object_destroy(o1);
        nmo_object_destroy(o2);
        nmo_session_destroy(s1);
        nmo_session_destroy(s2);
        nmo_context_release(ctx);
        return 0;
    }

    uint8_t bytes[] = {1, 3, 5, 7, 9, 11, 13, 15};
    nmo_chunk_t *c1 = nmo_chunk_create(nmo_session_get_arena(s1));
    nmo_chunk_t *c2 = nmo_chunk_create(nmo_session_get_arena(s2));
    if (!c1 || !c2) {
        nmo_object_destroy(o1);
        nmo_object_destroy(o2);
        nmo_session_destroy(s1);
        nmo_session_destroy(s2);
        nmo_context_release(ctx);
        return 0;
    }
    if (nmo_chunk_start_write(c1) != NMO_OK ||
        nmo_chunk_start_write(c2) != NMO_OK ||
        nmo_chunk_write_byte_array(c1, bytes, sizeof(bytes)) != NMO_OK ||
        nmo_chunk_write_byte_array(c2, bytes, sizeof(bytes)) != NMO_OK ||
        nmo_object_set_chunk(o1, c1) != NMO_OK ||
        nmo_object_set_chunk(o2, c2) != NMO_OK) {
        nmo_object_destroy(o1);
        nmo_object_destroy(o2);
        nmo_session_destroy(s1);
        nmo_session_destroy(s2);
        nmo_context_release(ctx);
        return 0;
    }
    nmo_chunk_close(c1);
    nmo_chunk_close(c2);

    if (nmo_object_repository_add(r1, &o1) != NMO_OK ||
        nmo_object_repository_add(r2, &o2) != NMO_OK) {
        if (o1) nmo_object_destroy(o1);
        if (o2) nmo_object_destroy(o2);
        nmo_session_destroy(s1);
        nmo_session_destroy(s2);
        nmo_context_release(ctx);
        return 0;
    }

    int ok = (nmo_session_save_file(s1, file1, NULL, NULL) == NMO_OK &&
              nmo_session_save_file(s2, file2, NULL, NULL) == NMO_OK);

    nmo_session_destroy(s1);
    nmo_session_destroy(s2);
    nmo_context_release(ctx);
    return ok;
}

TEST(cli_diff_objects, json_contains_renamed_fields) {
    const char *f1 = "test_cli_diff_objects_before.nmo";
    const char *f2 = "test_cli_diff_objects_after.nmo";
    remove(f1);
    remove(f2);
    ASSERT_TRUE(create_rename_fixture_files(f1, f2));

    char args[1024];
    snprintf(args, sizeof(args), "diff objects \"%s\" \"%s\"", f1, f2);
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_data(doc);
    ASSERT_NOT_NULL(data);

    yyjson_val *renamed_count = yyjson_obj_get(data, "renamed_count");
    yyjson_val *renamed = yyjson_obj_get(data, "renamed");
    ASSERT_NOT_NULL(renamed_count);
    ASSERT_NOT_NULL(renamed);
    ASSERT_EQ(1u, yyjson_get_uint(renamed_count));
    ASSERT_TRUE(yyjson_is_arr(renamed));
    ASSERT_EQ(1u, yyjson_arr_size(renamed));

    yyjson_val *added_count = yyjson_obj_get(data, "added_count");
    yyjson_val *removed_count = yyjson_obj_get(data, "removed_count");
    ASSERT_NOT_NULL(added_count);
    ASSERT_NOT_NULL(removed_count);
    ASSERT_EQ(0u, yyjson_get_uint(added_count));
    ASSERT_EQ(0u, yyjson_get_uint(removed_count));

    yyjson_doc_free(doc);
    remove(f1);
    remove(f2);
}

TEST(cli_diff_objects, text_contains_renamed_section) {
    const char *f1 = "test_cli_diff_objects_before.nmo";
    const char *f2 = "test_cli_diff_objects_after.nmo";
    remove(f1);
    remove(f2);
    ASSERT_TRUE(create_rename_fixture_files(f1, f2));

    char args[1024];
    snprintf(args, sizeof(args), "diff objects \"%s\" \"%s\"", f1, f2);
    char *out = run_cli(args);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "Renamed (1)");
    ASSERT_STR_CONTAINS(out, "1 renamed");
    free(out);

    remove(f1);
    remove(f2);
}

TEST(cli_diff_objects, identical_dataarray_file_has_no_pointer_diffs) {
    const char *fixture =
        NMO_SOURCE_DIR "/data/Ballance/Language.nmo";
    char args[2048];
    snprintf(args, sizeof(args),
             "diff objects \"%s\" \"%s\"", fixture, fixture);
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_data(doc);
    ASSERT_NOT_NULL(data);
    uint64_t object_count = yyjson_get_uint(
        yyjson_obj_get(data, "objects_file1"));
    ASSERT_TRUE(object_count > 0u);
    ASSERT_EQ(0u, yyjson_get_uint(
        yyjson_obj_get(data, "changed_count")));
    ASSERT_EQ(object_count, yyjson_get_uint(
        yyjson_obj_get(data, "identical_count")));

    yyjson_doc_free(doc);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli_diff_objects, json_contains_renamed_fields);
    REGISTER_TEST(cli_diff_objects, text_contains_renamed_section);
    REGISTER_TEST(cli_diff_objects, identical_dataarray_file_has_no_pointer_diffs);
TEST_MAIN_END()

