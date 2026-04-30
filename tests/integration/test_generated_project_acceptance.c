#include "test_framework.h"

#include "document/nmo_document_load.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"

#include <stdio.h>
#include <stdlib.h>
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

static void assert_cli_success_contains(
    const char *args,
    const char *expected_text)
{
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code != 0 ||
        (expected_text && (!result.output ||
                           !strstr(result.output, expected_text)))) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args,
                result.exit_code,
                result.output ? result.output : "(null)");
    }
    ASSERT_EQ(0, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    if (expected_text) {
        ASSERT_STR_CONTAINS(result.output, expected_text);
    }
    free(result.output);
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

TEST(generated_project_acceptance, cli_generates_valid_cmo_from_manifest)
{
    make_dir("test_project_acceptance_tmp");
    const char *manifest_path = "test_project_acceptance_tmp/project.json";
    const char *output_path = "test_project_acceptance_tmp/project.cmo";
    remove(manifest_path);
    remove(output_path);
    remove("test_project_acceptance_tmp/project.cmo.tmp");

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"GeneratedAcceptance\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Camera\",\"class\":\"CKCamera\"},"
                "{\"name\":\"Light\",\"class\":\"CKLight\"},"
                "{\"name\":\"Cube\",\"class\":\"CK3dEntity\","
                    "\"mesh\":{\"primitive\":\"cube\"},"
                    "\"material\":{\"color\":[1,0,0,1]},"
                    "\"scripts\":[{"
                        "\"name\":\"CubeScript\","
                        "\"debug_output\":[\"generated script start\"]"
                    "}]"
                "}"
            "]"
        "}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "patch apply --project \"%s\" -o \"%s\"",
             manifest_path,
             output_path);
    assert_cli_success_contains(args, "Saved to:");

    snprintf(args, sizeof(args), "validate all \"%s\"", output_path);
    assert_cli_success_contains(args, "Result: VALID");

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
    assert_named_class_exists(document, "CubeScript", NMO_CID_BEHAVIOR);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    remove(output_path);
    remove(manifest_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_project_acceptance, cli_generates_valid_cmo_from_manifest);
TEST_MAIN_END()
