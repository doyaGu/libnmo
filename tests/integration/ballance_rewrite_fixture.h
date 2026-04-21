#ifndef BALLANCE_REWRITE_FIXTURE_H
#define BALLANCE_REWRITE_FIXTURE_H

#include "yyjson.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *read_fixture_text_file(const char *path)
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
    json = read_fixture_text_file(path);
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

    replace_bb = yyjson_obj_get(root, "replace_bb");
    if (!replace_bb || !yyjson_is_obj(replace_bb)) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->replace_parent_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(replace_bb, "parent_id"));
    manifest->replace_node_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(replace_bb, "node_id"));
    snprintf(manifest->replace_guid, sizeof(manifest->replace_guid), "%s",
             yyjson_get_str(yyjson_obj_get(replace_bb, "guid")));
    snprintf(manifest->replace_name, sizeof(manifest->replace_name), "%s",
             yyjson_get_str(yyjson_obj_get(replace_bb, "name")));

    fold = yyjson_obj_get(root, "fold");
    if (!fold || !yyjson_is_obj(fold)) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->fold_parent_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(fold, "parent_id"));
    manifest->fold_anchor_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(fold, "anchor_id"));

    node_ids = yyjson_obj_get(fold, "node_ids");
    if (!node_ids || !yyjson_is_arr(node_ids)) {
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

static void rewrite_manifest_cli_guid(const char *manifest_guid,
                                      char *buffer,
                                      size_t buffer_size)
{
    size_t len = 0;

    if (!buffer || buffer_size == 0u) {
        return;
    }
    buffer[0] = '\0';
    if (!manifest_guid) {
        return;
    }

    len = strlen(manifest_guid);
    if (len >= 2u && manifest_guid[0] == '{' &&
        manifest_guid[len - 1u] == '}') {
        snprintf(buffer, buffer_size, "%.*s",
                 (int)(len - 2u), manifest_guid + 1);
        return;
    }
    snprintf(buffer, buffer_size, "%s", manifest_guid);
}

static int rewrite_manifest_fold_nodes_csv(const rewrite_manifest_t *manifest,
                                           char *buffer,
                                           size_t buffer_size)
{
    size_t len = 0;

    if (!manifest || !buffer || buffer_size == 0u) {
        return 0;
    }
    buffer[0] = '\0';

    for (size_t i = 0; i < manifest->fold_node_count; ++i) {
        int written = snprintf(buffer + len, buffer_size - len,
                               i == 0 ? "%u" : ",%u",
                               manifest->fold_node_ids[i]);
        if (written < 0 || (size_t)written >= buffer_size - len) {
            buffer[0] = '\0';
            return 0;
        }
        len += (size_t)written;
    }

    return manifest->fold_node_count > 0u;
}

#endif
