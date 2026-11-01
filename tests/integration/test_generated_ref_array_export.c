/**
 * @file test_generated_ref_array_export.c
 * @brief CLI views of object-reference arrays show session ids, on a generated file
 *
 * Ref-record arrays (CKGroup object_ids, CK3dEntity mesh_ids, ...) hold nmo_ref_t
 * elements: the id stored in the file and the session id it resolved to. After a
 * load the two differ, and `object show` and `object export` must print the
 * session id, the same one scalar references and `object refs` print. `object
 * import` reads `items` as session ids, so an export whose items are edited and
 * imported without `raw_hex` must give the group exactly the edited members.
 */

#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"

#include "core/nmo_array.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_session.h"
#include "../../src/runtime/runtime_internal.h"
#include "yyjson.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#endif

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

#define TMP_DIR "test_generated_ref_array_tmp"
#define INPUT_FILE TMP_DIR "/group.nmo"
#define EXPORT_FILE TMP_DIR "/group_export.json"
#define EDITED_FILE TMP_DIR "/group_edited.json"
#define OUTPUT_FILE TMP_DIR "/group_imported.nmo"

#define GROUP_NAME "Group_Members"

static const char *const k_member_names[] = {
    "Member_A", "Member_B", "Member_C", "Member_D"
};
enum { MEMBER_COUNT = 4 };

typedef struct cli_run_result {
    char *output;
    int exit_code;
} cli_run_result_t;

static char *format_dup(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (len < 0) {
        return NULL;
    }
    char *text = (char *)malloc((size_t)len + 1u);
    if (!text) {
        return NULL;
    }
    va_start(args, fmt);
    vsnprintf(text, (size_t)len + 1u, fmt, args);
    va_end(args);
    return text;
}

static int normalize_cli_exit_code(int status)
{
    if (status < 0) {
        return status;
    }
#if !defined(_WIN32)
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
#endif
    return status;
}

static cli_run_result_t run_cli_capture(const char *args)
{
    cli_run_result_t result = {NULL, -1};
    char *cmd = format_dup("%s %s 2>&1", NMO_CLI_PATH, args);
    if (!cmd) {
        return result;
    }
    FILE *pipe = NMO_POPEN(cmd, "r");
    free(cmd);
    if (!pipe) {
        return result;
    }

    size_t cap = 4096u;
    size_t len = 0u;
    char *buf = (char *)malloc(cap);
    int ch;
    while (buf && (ch = fgetc(pipe)) != EOF) {
        if (len + 2u > cap) {
            char *next = (char *)realloc(buf, cap * 2u);
            if (!next) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = next;
            cap *= 2u;
        }
        buf[len++] = (char)ch;
    }
    if (buf) {
        buf[len] = '\0';
    }
    result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
    result.output = buf;
    return result;
}

/* Runs the CLI and returns its output, or NULL (after printing it) when it fails. */
static char *run_cli_ok(const char *args)
{
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code == NMO_CLI_EXIT_SUCCESS && result.output) {
        return result.output;
    }
    fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
            args, result.exit_code, result.output ? result.output : "(null)");
    free(result.output);
    return NULL;
}

static void make_dir(const char *path)
{
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

/* Four CK3dObjects and a group holding A, C and D, in that order. */
static bool create_group_fixture(void)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    if (!ctx) {
        return false;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_object_id_t members[MEMBER_COUNT] = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < MEMBER_COUNT; ++i) {
        ok = nmo_session_create_object(session, NMO_CID_3DOBJECT, k_member_names[i],
                                       (nmo_guid_t){0, 0}, &members[i], NULL) == NMO_OK;
    }
    nmo_object_id_t group_id = 0;
    ok = ok && nmo_session_create_object(session, NMO_CID_GROUP, GROUP_NAME,
                                         (nmo_guid_t){0, 0}, &group_id, NULL) == NMO_OK;

    nmo_object_t *group = ok ? nmo_object_repository_find_by_id(
                                   nmo_session_get_repository(session), group_id)
                             : NULL;
    nmo_group_state_t *state = group ? (nmo_group_state_t *)group->state : NULL;
    nmo_ref_t *refs = NULL;
    if (state) {
        nmo_array_clear(&state->object_ids);
        ok = nmo_array_extend(&state->object_ids, 3, (void **)&refs) == NMO_OK;
    }
    if (ok && refs) {
        refs[0] = nmo_ref_from_id(members[0]);
        refs[1] = nmo_ref_from_id(members[2]);
        refs[2] = nmo_ref_from_id(members[3]);
        ok = nmo_session_save_file(session, INPUT_FILE, NULL, NULL) == NMO_OK;
    } else {
        ok = false;
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return ok;
}

typedef struct loaded_group {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_object_id_t group_id;
    nmo_object_id_t member_ids[MEMBER_COUNT];
    const nmo_ref_t *refs;
    size_t ref_count;
} loaded_group_t;

static nmo_object_id_t find_id_by_name(nmo_session_t *session, const char *name)
{
    nmo_object_t *object = nmo_object_repository_find_by_name(
        nmo_session_get_repository(session), name);
    return object ? nmo_object_get_id(object) : NMO_OBJECT_ID_NONE;
}

static void unload_group(loaded_group_t *group)
{
    if (group->session) {
        nmo_session_destroy(group->session);
    }
    if (group->ctx) {
        nmo_context_release(group->ctx);
    }
    memset(group, 0, sizeof(*group));
}

/* Loads the file and finds the group and every member by name. */
static bool load_group(const char *path, loaded_group_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ctx = nmo_context_create(&(nmo_context_desc_t){0});
    out->session = out->ctx ? nmo_session_create(out->ctx) : NULL;
    if (!out->session ||
        nmo_session_load_file(out->session, path, NULL, NULL) != NMO_OK) {
        unload_group(out);
        return false;
    }

    for (size_t i = 0; i < MEMBER_COUNT; ++i) {
        out->member_ids[i] = find_id_by_name(out->session, k_member_names[i]);
    }
    out->group_id = find_id_by_name(out->session, GROUP_NAME);
    nmo_object_t *group = nmo_object_repository_find_by_id(
        nmo_session_get_repository(out->session), out->group_id);
    const nmo_group_state_t *state =
        group ? (const nmo_group_state_t *)group->state : NULL;
    if (!state) {
        unload_group(out);
        return false;
    }
    out->refs = NMO_ARRAY_DATA(nmo_ref_t, &state->object_ids);
    out->ref_count = nmo_array_size(&state->object_ids);
    return true;
}

static yyjson_mut_val *find_object_ids_field(yyjson_mut_val *root)
{
    yyjson_mut_val *data = yyjson_mut_obj_get(root, "data");
    yyjson_mut_val *objects = yyjson_mut_obj_get(data, "objects");
    yyjson_mut_val *object = yyjson_mut_arr_get_first(objects);
    yyjson_mut_val *fields = yyjson_mut_obj_get(object, "fields");
    size_t idx, max;
    yyjson_mut_val *field;
    yyjson_mut_arr_foreach(fields, idx, max, field) {
        const char *name = yyjson_mut_get_str(yyjson_mut_obj_get(field, "name"));
        if (name && strcmp(name, "object_ids") == 0) {
            return field;
        }
    }
    return NULL;
}

TEST(generated_ref_array_export, cli_shows_and_round_trips_session_ids)
{
    make_dir(TMP_DIR);
    remove(INPUT_FILE);
    remove(OUTPUT_FILE);
    ASSERT_TRUE(create_group_fixture());

    loaded_group_t loaded;
    ASSERT_TRUE(load_group(INPUT_FILE, &loaded));
    ASSERT_EQ(3u, loaded.ref_count);
    for (size_t i = 0; i < MEMBER_COUNT; ++i) {
        ASSERT_NE(NMO_OBJECT_ID_NONE, loaded.member_ids[i]);
    }
    const nmo_object_id_t a = loaded.member_ids[0];
    const nmo_object_id_t b = loaded.member_ids[1];
    const nmo_object_id_t c = loaded.member_ids[2];
    const nmo_object_id_t d = loaded.member_ids[3];
    const nmo_object_id_t group_id = loaded.group_id;
    ASSERT_EQ(a, nmo_ref_runtime_id(&loaded.refs[0]));
    ASSERT_EQ(c, nmo_ref_runtime_id(&loaded.refs[1]));
    ASSERT_EQ(d, nmo_ref_runtime_id(&loaded.refs[2]));
    /* The file ids differ from the session ids, or the views could not tell them apart. */
    ASSERT_NE(loaded.refs[0].raw_id, loaded.refs[0].id);
    unload_group(&loaded);

    /* object show: the array preview names the session ids. */
    char *args = format_dup("object show %u \"%s\"", group_id, INPUT_FILE);
    char *expected = format_dup("#%u, #%u, #%u", a, c, d);
    char *output = run_cli_ok(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, expected);
    free(output);
    free(expected);
    free(args);

    /* object show --select on one element resolves the same id. */
    args = format_dup("object show %u \"%s\" --select \"object_ids[1]\"",
                      group_id, INPUT_FILE);
    expected = format_dup("#%u (Member_C)", c);
    output = run_cli_ok(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, expected);
    free(output);
    free(expected);
    free(args);

    /* object export: items are session ids. */
    args = format_dup("-f json object export --id %u \"%s\" > \"%s\"",
                      group_id, INPUT_FILE, EXPORT_FILE);
    output = run_cli_ok(args);
    ASSERT_NOT_NULL(output);
    free(output);
    free(args);

    yyjson_doc *exported = yyjson_read_file(EXPORT_FILE, 0, NULL, NULL);
    ASSERT_NOT_NULL(exported);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(exported, NULL);
    yyjson_doc_free(exported);
    ASSERT_NOT_NULL(doc);
    yyjson_mut_val *field = find_object_ids_field(yyjson_mut_doc_get_root(doc));
    ASSERT_NOT_NULL(field);
    yyjson_mut_val *items = yyjson_mut_obj_get(field, "items");
    ASSERT_EQ(3u, yyjson_mut_arr_size(items));
    ASSERT_EQ(a, (nmo_object_id_t)yyjson_mut_get_int(yyjson_mut_arr_get(items, 0)));
    ASSERT_EQ(c, (nmo_object_id_t)yyjson_mut_get_int(yyjson_mut_arr_get(items, 1)));
    ASSERT_EQ(d, (nmo_object_id_t)yyjson_mut_get_int(yyjson_mut_arr_get(items, 2)));

    /* Edit the membership to [D, B], drop raw_hex, and import. */
    yyjson_mut_obj_remove_key(field, "raw_hex");
    yyjson_mut_val *edited = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_uint(doc, edited, d);
    yyjson_mut_arr_add_uint(doc, edited, b);
    yyjson_mut_obj_put(field, yyjson_mut_str(doc, "items"), edited);
    yyjson_mut_obj_put(field, yyjson_mut_str(doc, "count"), yyjson_mut_uint(doc, 2));
    ASSERT_TRUE(yyjson_mut_write_file(EDITED_FILE, doc, 0, NULL, NULL));
    yyjson_mut_doc_free(doc);

    args = format_dup("object import -f json \"%s\" \"%s\" -o \"%s\"",
                      EDITED_FILE, INPUT_FILE, OUTPUT_FILE);
    output = run_cli_ok(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Fields written");
    free(output);
    free(args);

    ASSERT_TRUE(load_group(OUTPUT_FILE, &loaded));
    ASSERT_EQ(2u, loaded.ref_count);
    ASSERT_EQ(loaded.member_ids[3], nmo_ref_runtime_id(&loaded.refs[0]));
    ASSERT_EQ(loaded.member_ids[1], nmo_ref_runtime_id(&loaded.refs[1]));
    unload_group(&loaded);

    remove(INPUT_FILE);
    remove(EXPORT_FILE);
    remove(EDITED_FILE);
    remove(OUTPUT_FILE);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_ref_array_export, cli_shows_and_round_trips_session_ids);
TEST_MAIN_END()
