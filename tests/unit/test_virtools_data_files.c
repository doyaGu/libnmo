/**
 * @file test_virtools_data_files.c
 * @brief Checks for tracked Virtools JSON data hygiene.
 */

#include "test_framework.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_text_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *text = (char *)malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    if (read_size != (size_t)size) {
        free(text);
        return NULL;
    }

    text[size] = '\0';
    return text;
}

TEST(virtools_data_files, plugin_dll_fields_are_portable_names) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/virtools_plugins.json", NMO_TEST_DATA_DIR);
    char *json = read_text_file(path);
    ASSERT_NOT_NULL(json);

    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_TRUE(yyjson_is_arr(root));

    size_t idx, max;
    yyjson_val *entry;
    yyjson_arr_foreach(root, idx, max, entry) {
        yyjson_val *dll_val = yyjson_obj_get(entry, "dll");
        const char *dll = yyjson_get_str(dll_val);
        if (dll == NULL || dll[0] == '\0') {
            continue;
        }

        ASSERT_TRUE(strchr(dll, ':') == NULL);
        ASSERT_TRUE(strchr(dll, '/') == NULL);
        ASSERT_TRUE(strchr(dll, '\\') == NULL);
    }

    yyjson_doc_free(doc);
    free(json);
}

static int starts_with_local_absolute_path(const char *value) {
    if (value == NULL) {
        return 0;
    }
    if (((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z')) &&
        value[1] == ':' &&
        (value[2] == '\\' || value[2] == '/')) {
        return 1;
    }
    return strncmp(value, "/Users/", 7) == 0 || strncmp(value, "/home/", 6) == 0;
}

static int json_value_has_local_absolute_path(yyjson_val *value) {
    if (yyjson_is_str(value)) {
        return starts_with_local_absolute_path(yyjson_get_str(value));
    }

    if (yyjson_is_arr(value)) {
        size_t idx, max;
        yyjson_val *entry;
        yyjson_arr_foreach(value, idx, max, entry) {
            if (json_value_has_local_absolute_path(entry)) {
                return 1;
            }
        }
        return 0;
    }

    if (yyjson_is_obj(value)) {
        size_t idx, max;
        yyjson_val *key, *entry;
        yyjson_obj_foreach(value, idx, max, key, entry) {
            (void)key;
            if (json_value_has_local_absolute_path(entry)) {
                return 1;
            }
        }
    }

    return 0;
}

static void assert_file_has_no_local_absolute_path(const char *name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_DATA_DIR, name);
    char *text = read_text_file(path);
    ASSERT_NOT_NULL(text);

    yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
    ASSERT_NOT_NULL(doc);
    ASSERT_FALSE(json_value_has_local_absolute_path(yyjson_doc_get_root(doc)));

    yyjson_doc_free(doc);
    free(text);
}

TEST(virtools_data_files, tracked_json_has_no_local_absolute_paths) {
    assert_file_has_no_local_absolute_path("virtools_plugins.json");
    assert_file_has_no_local_absolute_path("virtools_building_blocks.json");
    assert_file_has_no_local_absolute_path("virtools_building_blocks_ext.json");
    assert_file_has_no_local_absolute_path("virtools_operation_types.json");
    assert_file_has_no_local_absolute_path("virtools_parameter_types.json");
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(virtools_data_files, plugin_dll_fields_are_portable_names);
    REGISTER_TEST(virtools_data_files, tracked_json_has_no_local_absolute_paths);
TEST_MAIN_END()
