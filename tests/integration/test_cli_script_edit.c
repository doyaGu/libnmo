#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"
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

typedef struct cli_run_result {
    char *output;
    int exit_code;
} cli_run_result_t;

typedef struct rewrite_manifest {
    uint32_t root_behavior_id;
    uint32_t replace_parent_id;
    uint32_t replace_node_id;
    char replace_guid[64];
    char replace_name[128];
    uint32_t fold_parent_id;
    uint32_t fold_anchor_id;
    uint32_t fold_node_ids[32];
    size_t fold_node_count;
} rewrite_manifest_t;

static cli_run_result_t run_cli_capture(const char *args)
{
    cli_run_result_t result = {0};
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) {
        return result;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buffer = (char *)malloc(cap);
    if (!buffer) {
        result.exit_code = NMO_PCLOSE(pipe);
        return result;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1 > cap) {
            size_t new_cap = cap * 2;
            while (new_cap < len + chunk_len + 1) {
                new_cap *= 2;
            }
            char *new_buffer = (char *)realloc(buffer, new_cap);
            if (!new_buffer) {
                free(buffer);
                result.exit_code = NMO_PCLOSE(pipe);
                return result;
            }
            buffer = new_buffer;
            cap = new_cap;
        }
        memcpy(buffer + len, chunk, chunk_len);
        len += chunk_len;
    }

    buffer[len] = '\0';
    result.output = buffer;
    result.exit_code = NMO_PCLOSE(pipe);
    return result;
}

static const char *get_string_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return yyjson_get_str(val);
}

static yyjson_val *get_object_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_obj(val)) ? val : NULL;
}

static yyjson_val *get_array_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_arr(val)) ? val : NULL;
}

static int build_repo_fixture_path(const char *relative_path,
                                   char *buffer,
                                   size_t buffer_size)
{
    const char *source_path = __FILE__;
    const char *tests_dir = NULL;

    if (!relative_path || !buffer || buffer_size == 0u) {
        return 0;
    }

    tests_dir = strstr(source_path, "tests");
    if (!tests_dir) {
        return 0;
    }

    snprintf(buffer, buffer_size, "%.*s%s",
             (int)(tests_dir - source_path), source_path, relative_path);
    return 1;
}

static char *read_text_file(const char *path)
{
    FILE *fp = NULL;
    long size = 0;
    char *buffer = NULL;

    if (!path) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    if (size > 0) {
        size_t read_size = fread(buffer, 1u, (size_t)size, fp);
        if (read_size != (size_t)size) {
            free(buffer);
            fclose(fp);
            return NULL;
        }
    }
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static int load_rewrite_manifest(rewrite_manifest_t *manifest)
{
    char path[1024];
    char *json = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *replace_bb = NULL;
    yyjson_val *fold = NULL;
    yyjson_val *node_ids = NULL;
    size_t idx = 0;
    size_t max = 0;
    yyjson_val *item = NULL;

    if (!manifest) {
        return 0;
    }
    memset(manifest, 0, sizeof(*manifest));

    if (!build_repo_fixture_path("tests/fixtures/ballance_rewrite_manifest.json",
                                 path, sizeof(path))) {
        return 0;
    }
    json = read_text_file(path);
    if (!json) {
        return 0;
    }

    doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        free(json);
        return 0;
    }
    root = yyjson_doc_get_root(doc);
    if (!root ||
        !yyjson_is_uint(yyjson_obj_get(root, "root_behavior_id"))) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->root_behavior_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "root_behavior_id"));

    replace_bb = get_object_field(root, "replace_bb");
    if (!replace_bb) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->replace_parent_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(replace_bb, "parent_id"));
    manifest->replace_node_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(replace_bb, "node_id"));
    snprintf(manifest->replace_guid, sizeof(manifest->replace_guid), "%s",
             get_string_field(replace_bb, "guid"));
    snprintf(manifest->replace_name, sizeof(manifest->replace_name), "%s",
             get_string_field(replace_bb, "name"));

    fold = get_object_field(root, "fold");
    if (!fold) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->fold_parent_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(fold, "parent_id"));
    manifest->fold_anchor_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(fold, "anchor_id"));

    node_ids = get_array_field(fold, "node_ids");
    if (!node_ids) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    yyjson_arr_foreach(node_ids, idx, max, item) {
        if (manifest->fold_node_count >=
                sizeof(manifest->fold_node_ids) /
                    sizeof(manifest->fold_node_ids[0]) ||
            !yyjson_is_uint(item)) {
            yyjson_doc_free(doc);
            free(json);
            return 0;
        }
        manifest->fold_node_ids[manifest->fold_node_count++] =
            (uint32_t)yyjson_get_uint(item);
    }

    yyjson_doc_free(doc);
    free(json);
    return 1;
}

static char *load_report_contract(void)
{
    char path[1024];

    if (!build_repo_fixture_path("tests/fixtures/script_edit_reports.md",
                                 path, sizeof(path))) {
        return NULL;
    }
    return read_text_file(path);
}

TEST(cli, script_edit_fixture_manifest_contains_locked_ballance_ids)
{
    static const uint32_t expected_fold_nodes[] = {
        4166u, 4140u, 4147u, 4157u, 4165u,
        4153u, 4151u, 4155u, 4143u, 4145u
    };
    rewrite_manifest_t manifest;

    ASSERT_TRUE(load_rewrite_manifest(&manifest));

    ASSERT_EQ(237u, manifest.root_behavior_id);
    ASSERT_EQ(237u, manifest.replace_parent_id);
    ASSERT_EQ(363u, manifest.replace_node_id);
    ASSERT_STR_EQ("{42414C02-10000002}", manifest.replace_guid);
    ASSERT_STR_EQ("Ballance Load NMO Range", manifest.replace_name);
    ASSERT_EQ(4692u, manifest.fold_parent_id);
    ASSERT_EQ(4166u, manifest.fold_anchor_id);
    ASSERT_EQ(sizeof(expected_fold_nodes) / sizeof(expected_fold_nodes[0]),
              manifest.fold_node_count);
    ASSERT_ARRAY_EQ(manifest.fold_node_ids, expected_fold_nodes,
                    manifest.fold_node_count);
}

TEST(cli, script_edit_report_contract_is_checked_in)
{
    char *markdown = load_report_contract();

    ASSERT_NOT_NULL(markdown);
    ASSERT_STR_CONTAINS(markdown, "## `nmo script graph ... -f json`");
    ASSERT_STR_CONTAINS(markdown, "## `nmo script boundary ... -f json`");
    ASSERT_STR_CONTAINS(markdown, "## `nmo script fold ... --dry-run -f json`");
    ASSERT_STR_CONTAINS(markdown, "## `nmo script run ... --dry-run -f json`");
    ASSERT_STR_CONTAINS(markdown, "## CLI To Lua Option Mapping");
    ASSERT_STR_CONTAINS(markdown, "`--parent` -> `{ parent = <id> }`");
    ASSERT_STR_CONTAINS(markdown, "`--node` -> `{ node = <id> }`");
    ASSERT_STR_CONTAINS(markdown, "`--bb-guid` -> `{ guid = \"<guid>\" }`");
    ASSERT_STR_CONTAINS(markdown, "`--name` -> `{ name = \"<name>\" }`");
    ASSERT_STR_CONTAINS(markdown, "`--interface preserve|canonicalize|remove` -> `{ interface = \"...\" }`");
    free(markdown);
}

TEST(cli, script_graph_json_smoke)
{
    rewrite_manifest_t manifest;
    char args[1024];

    ASSERT_TRUE(load_rewrite_manifest(&manifest));

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ("nmo", get_string_field(root, "tool"));
    ASSERT_STR_EQ("script.graph", get_string_field(root, "command"));

    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(manifest.root_behavior_id,
              (uint32_t)yyjson_get_uint(yyjson_obj_get(data,
                                                       "root_behavior_id")));
    ASSERT_TRUE(yyjson_is_bool(yyjson_obj_get(data, "edit_ready")));
    ASSERT_TRUE(yyjson_is_bool(yyjson_obj_get(data, "owner_index_available")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(data, "root_behavior_id")));

    yyjson_val *reference_validation =
        get_object_field(data, "reference_validation");
    ASSERT_NOT_NULL(reference_validation);
    ASSERT_TRUE(yyjson_is_int(yyjson_obj_get(reference_validation, "status")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(reference_validation,
                                              "broken_count")));

    yyjson_val *control_edges = get_array_field(data, "control_edges");
    yyjson_val *data_edges = get_array_field(data, "data_edges");
    ASSERT_NOT_NULL(control_edges);
    ASSERT_NOT_NULL(data_edges);
    ASSERT_TRUE(yyjson_arr_size(control_edges) > 0u);
    ASSERT_TRUE(yyjson_arr_size(data_edges) > 0u);

    yyjson_doc_free(doc);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, script_edit_fixture_manifest_contains_locked_ballance_ids);
    REGISTER_TEST(cli, script_edit_report_contract_is_checked_in);
    REGISTER_TEST(cli, script_graph_json_smoke);
TEST_MAIN_END()
