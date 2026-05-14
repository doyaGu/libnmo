#include "test_framework.h"

#include "document/nmo_document_load.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/wait.h>
#else
#include <direct.h>
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

typedef struct cli_run_result {
    char *output;
    int exit_code;
} cli_run_result_t;

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
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) {
        return result;
    }

    size_t cap = 4096u;
    size_t len = 0u;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
        return result;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1u > cap) {
            size_t next_cap = cap * 2u;
            while (next_cap < len + chunk_len + 1u) {
                next_cap *= 2u;
            }
            char *next = (char *)realloc(buf, next_cap);
            if (!next) {
                free(buf);
                result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
                return result;
            }
            buf = next;
            cap = next_cap;
        }
        memcpy(buf + len, chunk, chunk_len);
        len += chunk_len;
    }
    buf[len] = '\0';
    result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
    result.output = buf;
    return result;
}

static void make_dir(const char *path)
{
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    size_t len = strlen(text);
    int ok = fwrite(text, 1u, len, fp) == len;
    fclose(fp);
    return ok;
}

static const char *get_string_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return yyjson_get_str(val);
}

static yyjson_val *get_object_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_obj(val) ? val : NULL;
}

static yyjson_val *get_array_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_arr(val) ? val : NULL;
}

static bool get_bool_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_bool(val) && yyjson_get_bool(val);
}

static uint64_t get_uint_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_uint(val) ? yyjson_get_uint(val) : 0u;
}

static double get_real_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_num(val) ? yyjson_get_real(val) : 0.0;
}

static int array_contains_string(yyjson_val *arr, const char *needle)
{
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *item = NULL;
    yyjson_arr_foreach(arr, idx, max, item) {
        const char *value = yyjson_get_str(item);
        if (value && strcmp(value, needle) == 0) {
            return 1;
        }
    }
    return 0;
}

static void parse_cli_json_result(cli_run_result_t *result, yyjson_doc **out_doc)
{
    ASSERT_NOT_NULL(result);
    ASSERT_NOT_NULL(out_doc);
    *out_doc = NULL;
    ASSERT_NOT_NULL(result->output);
    yyjson_doc *doc = yyjson_read(result->output, strlen(result->output), 0);
    if (!doc) {
        fprintf(stderr, "\nCLI output was not JSON:\n%s\n", result->output);
    }
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ("patch.apply", get_string_field(root, "command"));
    ASSERT_NOT_NULL(get_object_field(root, "data"));
    *out_doc = doc;
}

static void assert_named_class_exists(
    nmo_document_t *document,
    const char *name,
    nmo_class_id_t class_id)
{
    nmo_object_query_t query = {0};
    query.name = name;
    query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    query.class_id = class_id;

    size_t count = 0u;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &query, &count));
    ASSERT_EQ(1u, count);
}

TEST(generated_project_manifest, cli_dry_run_reports_project_diagnostics)
{
    make_dir("test_project_manifest_tmp");
    const char *manifest_path = "test_project_manifest_tmp/project_dry_run.json";
    const char *output_path = "test_project_manifest_tmp/project_dry_run.cmo";
    remove(manifest_path);
    remove(output_path);

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"GeneratedDryRun\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Camera\",\"class\":\"CKCamera\"},"
                "{\"name\":\"Cube\",\"class\":\"CK3dEntity\","
                    "\"mesh\":{\"primitive\":\"cube\"},"
                    "\"material\":{\"color\":[1,0,0,1]}},"
                "{\"name\":\"Animation\",\"class\":\"CKObjectAnimation\","
                    "\"animation\":{"
                        "\"target\":\"Cube\","
                        "\"format\":\"newdata\","
                        "\"controllers\":[{"
                            "\"type\":1669088001,"
                            "\"keys\":[[0,1,2,3],[1,4,5,6]]"
                        "}],"
                        "\"morph_keys\":[{\"time\":0.5,\"data\":[1,2,3]}],"
                        "\"length\":1.25"
                    "}"
                "}"
            "]"
        "}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json patch apply --project \"%s\" --dry-run -o \"%s\"",
             manifest_path,
             output_path);
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code != 0) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args,
                result.exit_code,
                result.output ? result.output : "(null)");
    }
    ASSERT_EQ(0, result.exit_code);
    ASSERT_FALSE(file_exists(output_path));

    yyjson_doc *doc = NULL;
    parse_cli_json_result(&result, &doc);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_STR_EQ(manifest_path, get_string_field(data, "manifest"));
    ASSERT_STR_EQ(output_path, get_string_field(data, "output"));

    yyjson_val *diffs = get_object_field(data, "diffs");
    ASSERT_NOT_NULL(diffs);
    yyjson_val *created = get_object_field(diffs, "created");
    ASSERT_NOT_NULL(created);
    ASSERT_TRUE(array_contains_string(get_array_field(created, "documents"),
                                      "GeneratedDryRun"));
    ASSERT_TRUE(array_contains_string(get_array_field(created, "scenes"),
                                      "Level"));
    ASSERT_TRUE(array_contains_string(get_array_field(created, "objects"),
                                      "Cube"));
    ASSERT_TRUE(array_contains_string(get_array_field(created, "assets"),
                                      "Cube_Mesh"));

    yyjson_val *validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_TRUE(get_bool_field(validation, "ok"));
    ASSERT_EQ(0u, yyjson_arr_size(get_array_field(validation, "issues")));

    yyjson_val *evidence = get_object_field(data, "evidence");
    ASSERT_NOT_NULL(evidence);
    ASSERT_FALSE(get_bool_field(evidence, "post_load_checked"));
    ASSERT_FALSE(get_bool_field(evidence, "post_load_ok"));

    yyjson_val *objects = get_array_field(evidence, "generated_objects");
    ASSERT_NOT_NULL(objects);
    ASSERT_EQ(3u, yyjson_arr_size(objects));
    yyjson_val *object = yyjson_arr_get(objects, 1);
    ASSERT_STR_EQ("Cube", get_string_field(object, "name"));
    ASSERT_EQ(0u, get_uint_field(object, "id"));
    ASSERT_EQ((uint64_t)NMO_CID_3DENTITY, get_uint_field(object, "class_id"));

    yyjson_val *asset_bindings = get_array_field(evidence, "asset_bindings");
    ASSERT_NOT_NULL(asset_bindings);
    ASSERT_EQ(2u, yyjson_arr_size(asset_bindings));
    yyjson_val *binding = yyjson_arr_get(asset_bindings, 0);
    ASSERT_STR_EQ("Cube", get_string_field(binding, "owner"));
    ASSERT_STR_EQ("Cube_Mesh", get_string_field(binding, "asset"));
    ASSERT_STR_EQ("primitive_mesh", get_string_field(binding, "kind"));

    yyjson_doc_free(doc);
    free(result.output);
    remove(manifest_path);
}

TEST(generated_project_manifest, cli_json_failure_reports_project_source)
{
    make_dir("test_project_manifest_tmp");
    const char *manifest_path = "test_project_manifest_tmp/project_bad.json";
    const char *mesh_path = "test_project_manifest_tmp/project_bad.obj";
    const char *output_path = "test_project_manifest_tmp/project_bad.cmo";
    remove(manifest_path);
    remove(mesh_path);
    remove(output_path);

    ASSERT_TRUE(write_text_file(mesh_path,
                                "v 0 0 0\n"
                                "v 1 0 0\n"
                                "v 0 1 0\n"
                                "usemtl missing_texture_group\n"
                                "f 1 2 3\n"));
    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"BadProject\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":[{"
                "\"name\":\"MeshEntity\","
                "\"class\":\"CK3dEntity\","
                "\"mesh\":{\"obj\":\"project_bad.obj\"},"
                "\"materials\":[{"
                    "\"name\":\"missing_texture_group\","
                    "\"texture\":\"missing_texture.png\""
                "}]"
            "}]"
        "}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json patch apply --project \"%s\" -o \"%s\"",
             manifest_path,
             output_path);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(0, result.exit_code);
    ASSERT_FALSE(file_exists(output_path));

    yyjson_doc *doc = NULL;
    parse_cli_json_result(&result, &doc);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_FALSE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));

    yyjson_val *validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_FALSE(get_bool_field(validation, "ok"));
    yyjson_val *issues = get_array_field(validation, "issues");
    ASSERT_NOT_NULL(issues);
    ASSERT_EQ(1u, yyjson_arr_size(issues));
    yyjson_val *issue = yyjson_arr_get(issues, 0);
    ASSERT_STR_EQ("missing_obj_material_texture_file",
                  get_string_field(issue, "code"));
    ASSERT_STR_EQ("object", get_string_field(issue, "subject_kind"));
    ASSERT_STR_EQ("MeshEntity", get_string_field(issue, "subject_name"));
    ASSERT_STR_EQ("scenes[0].objects[0].materials[0].texture",
                  get_string_field(issue, "source_path"));

    yyjson_doc_free(doc);
    free(result.output);
    remove(mesh_path);
    remove(manifest_path);
}

TEST(generated_project_manifest, cli_dry_run_reports_animation_payload_gaps)
{
    make_dir("test_project_manifest_tmp");
    const char *manifest_path = "test_project_manifest_tmp/project_bad_animation.json";
    const char *output_path = "test_project_manifest_tmp/project_bad_animation.cmo";
    remove(manifest_path);
    remove(output_path);

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"BadAnimationProject\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"id\":\"target\",\"name\":\"Target\",\"class\":\"CK3dEntity\"},"
                "{\"name\":\"Animation\",\"class\":\"CKObjectAnimation\","
                    "\"animation\":{"
                        "\"target\":\"target\","
                        "\"format\":\"newdata\","
                        "\"controllers\":[{"
                            "\"type\":2451224577,"
                            "\"keys\":[[0,1,2,3,4,5,6,7,8,9,10]]"
                        "}],"
                        "\"morph_keys\":[{\"time\":0,\"data\":[1,2,3]}]"
                    "}"
                "}"
            "]"
        "}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json patch apply --project \"%s\" --dry-run -o \"%s\"",
             manifest_path,
             output_path);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(0, result.exit_code);
    ASSERT_FALSE(file_exists(output_path));

    yyjson_doc *doc = NULL;
    parse_cli_json_result(&result, &doc);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_FALSE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_STR_EQ(output_path, get_string_field(data, "output"));

    yyjson_val *validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_FALSE(get_bool_field(validation, "ok"));
    yyjson_val *issues = get_array_field(validation, "issues");
    ASSERT_NOT_NULL(issues);
    ASSERT_EQ(1u, yyjson_arr_size(issues));
    yyjson_val *issue = yyjson_arr_get(issues, 0);
    ASSERT_STR_EQ("unsupported_animation_controller_type",
                  get_string_field(issue, "code"));
    ASSERT_STR_EQ("object", get_string_field(issue, "subject_kind"));
    ASSERT_STR_EQ("Animation", get_string_field(issue, "subject_name"));
    ASSERT_STR_EQ("scenes[0].objects[1]",
                  get_string_field(issue, "source_path"));

    yyjson_doc_free(doc);
    free(result.output);
    remove(manifest_path);
}

TEST(generated_project_manifest, cli_dry_run_reports_midisound_gate)
{
    make_dir("test_project_manifest_tmp");
    const char *manifest_path = "test_project_manifest_tmp/project_midi_sound.json";
    const char *output_path = "test_project_manifest_tmp/project_midi_sound.cmo";
    remove(manifest_path);
    remove(output_path);

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"MidiSoundProject\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":[{"
                "\"name\":\"MidiSound\","
                "\"class\":\"CKMidiSound\","
                "\"sound\":{\"file\":\"song.mid\"}"
            "}]"
        "}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json patch apply --project \"%s\" --dry-run -o \"%s\"",
             manifest_path,
             output_path);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(0, result.exit_code);
    ASSERT_FALSE(file_exists(output_path));

    yyjson_doc *doc = NULL;
    parse_cli_json_result(&result, &doc);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_FALSE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));

    yyjson_val *validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    yyjson_val *issues = get_array_field(validation, "issues");
    ASSERT_NOT_NULL(issues);
    ASSERT_EQ(1u, yyjson_arr_size(issues));
    yyjson_val *issue = yyjson_arr_get(issues, 0);
    ASSERT_STR_EQ("unsupported_midisound_file_authoring",
                  get_string_field(issue, "code"));
    ASSERT_STR_EQ("MidiSound", get_string_field(issue, "subject_name"));
    ASSERT_STR_EQ("scenes[0].objects[0].sound.file",
                  get_string_field(issue, "source_path"));

    yyjson_doc_free(doc);
    free(result.output);
    remove(manifest_path);
}

TEST(generated_project_manifest, cli_json_write_reports_project_evidence)
{
    make_dir("test_project_manifest_tmp");
    const char *manifest_path = "test_project_manifest_tmp/project_evidence.json";
    const char *output_path = "test_project_manifest_tmp/project_evidence.cmo";
    remove(manifest_path);
    remove(output_path);

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"GeneratedEvidence\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Camera\",\"class\":\"CKCamera\"},"
                "{\"name\":\"Cube\",\"class\":\"CK3dEntity\","
                    "\"mesh\":{\"primitive\":\"cube\"},"
                    "\"material\":{\"color\":[1,0,0,1]}},"
                "{\"name\":\"Animation\",\"class\":\"CKObjectAnimation\","
                    "\"animation\":{"
                        "\"target\":\"Cube\","
                        "\"format\":\"newdata\","
                        "\"controllers\":[{"
                            "\"type\":1669088001,"
                            "\"keys\":[[0,1,2,3],[1,4,5,6]]"
                        "}],"
                        "\"morph_keys\":[{\"time\":0.5,\"data\":[1,2,3]}],"
                        "\"length\":1.25"
                    "}"
                "}"
            "]"
        "}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json patch apply --project \"%s\" -o \"%s\"",
             manifest_path,
             output_path);
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code != 0) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args,
                result.exit_code,
                result.output ? result.output : "(null)");
    }
    ASSERT_EQ(0, result.exit_code);
    ASSERT_TRUE(file_exists(output_path));

    yyjson_doc *doc = NULL;
    parse_cli_json_result(&result, &doc);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));

    yyjson_val *evidence = get_object_field(data, "evidence");
    ASSERT_NOT_NULL(evidence);
    ASSERT_TRUE(get_bool_field(evidence, "post_load_checked"));
    ASSERT_TRUE(get_bool_field(evidence, "post_load_ok"));

    yyjson_val *objects = get_array_field(evidence, "generated_objects");
    ASSERT_NOT_NULL(objects);
    ASSERT_EQ(3u, yyjson_arr_size(objects));
    yyjson_val *cube = yyjson_arr_get(objects, 1);
    ASSERT_STR_EQ("Cube", get_string_field(cube, "name"));
    ASSERT_GT(get_uint_field(cube, "id"), 0u);
    ASSERT_EQ((uint64_t)NMO_CID_3DENTITY, get_uint_field(cube, "class_id"));

    yyjson_val *scripts = get_array_field(evidence, "scripts");
    ASSERT_NOT_NULL(scripts);
    ASSERT_EQ(0u, yyjson_arr_size(scripts));

    yyjson_val *animation_bindings =
        get_array_field(evidence, "animation_bindings");
    ASSERT_NOT_NULL(animation_bindings);
    ASSERT_EQ(1u, yyjson_arr_size(animation_bindings));
    yyjson_val *animation = yyjson_arr_get(animation_bindings, 0);
    ASSERT_STR_EQ("Animation", get_string_field(animation, "name"));
    ASSERT_STR_EQ("Cube", get_string_field(animation, "target"));
    ASSERT_EQ(1u, get_uint_field(animation, "controller_count"));
    yyjson_val *controllers = get_array_field(animation, "controllers");
    ASSERT_NOT_NULL(controllers);
    ASSERT_EQ(1u, yyjson_arr_size(controllers));
    yyjson_val *controller = yyjson_arr_get(controllers, 0);
    ASSERT_EQ(1669088001u, get_uint_field(controller, "type"));
    ASSERT_EQ(32u, get_uint_field(controller, "data_size"));
    ASSERT_EQ(1u, get_uint_field(animation, "morph_key_count"));
    yyjson_val *morph_keys = get_array_field(animation, "morph_keys");
    ASSERT_NOT_NULL(morph_keys);
    ASSERT_EQ(1u, yyjson_arr_size(morph_keys));
    yyjson_val *morph_key = yyjson_arr_get(morph_keys, 0);
    ASSERT_FLOAT_EQ(0.5f, (float)get_real_field(morph_key, "time"), 0.0001f);
    ASSERT_EQ(12u, get_uint_field(morph_key, "data_size"));

    yyjson_doc_free(doc);
    free(result.output);
    remove(output_path);
    remove(manifest_path);
}

TEST(generated_project_manifest, cli_replays_project_manifest)
{
    make_dir("test_project_manifest_tmp");
    const char *manifest_path = "test_project_manifest_tmp/project.json";
    const char *output_path = "test_project_manifest_tmp/project.cmo";
    remove(manifest_path);
    remove(output_path);

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Camera\",\"class\":\"CKCamera\"},"
                "{\"name\":\"Light\",\"class\":\"CKLight\"},"
                "{\"name\":\"Cube\",\"class\":\"CK3dEntity\","
                    "\"mesh\":{\"primitive\":\"cube\"},"
                    "\"material\":{\"color\":[1,0,0,1]}}"
            "]"
        "}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "patch apply --project \"%s\" -o \"%s\"",
             manifest_path,
             output_path);
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code != 0) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args,
                result.exit_code,
                result.output ? result.output : "(null)");
    }
    ASSERT_EQ(0, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    ASSERT_STR_CONTAINS(result.output, "Saved to:");
    free(result.output);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    assert_named_class_exists(document, "Level", NMO_CID_SCENE);
    assert_named_class_exists(document, "Camera", NMO_CID_CAMERA);
    assert_named_class_exists(document, "Light", NMO_CID_LIGHT);
    assert_named_class_exists(document, "Cube", NMO_CID_3DENTITY);
    assert_named_class_exists(document, "Cube_Mesh", NMO_CID_MESH);
    assert_named_class_exists(document, "Cube_Material", NMO_CID_MATERIAL);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    remove(output_path);
    remove(manifest_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_project_manifest, cli_dry_run_reports_project_diagnostics);
REGISTER_TEST(generated_project_manifest, cli_json_failure_reports_project_source);
REGISTER_TEST(generated_project_manifest, cli_dry_run_reports_animation_payload_gaps);
REGISTER_TEST(generated_project_manifest, cli_dry_run_reports_midisound_gate);
REGISTER_TEST(generated_project_manifest, cli_json_write_reports_project_evidence);
REGISTER_TEST(generated_project_manifest, cli_replays_project_manifest);
TEST_MAIN_END()
