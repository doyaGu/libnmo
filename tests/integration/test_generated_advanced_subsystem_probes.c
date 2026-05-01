#include "test_framework.h"

#include "document/nmo_document_load.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_manifest_json.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "runtime/nmo_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
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

TEST(generated_advanced_probes, sound_and_animation_skeletons_save_load_validate)
{
    const char *output_path = "test_generated_advanced_subsystems.cmo";
    remove(output_path);
    remove("test_generated_advanced_subsystems.cmo.tmp");

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t sound = 0u;
    uint32_t animation = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_document_name(plan, "AdvancedSubsystemProbe"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "ProbeWaveSound",
                  },
                  &sound));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "ProbeObjectAnimation",
                  },
                  &animation));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK,
              nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    char args[1024];
    snprintf(args, sizeof(args), "validate all \"%s\"", output_path);
    assert_cli_success_contains(args, "Result: VALID");

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    assert_named_class_exists(document, "Level", NMO_CID_SCENE);
    assert_named_class_exists(document, "ProbeWaveSound", NMO_CID_WAVESOUND);
    assert_named_class_exists(
        document,
        "ProbeObjectAnimation",
        NMO_CID_OBJECTANIMATION);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_advanced_probes, unproven_manifest_authoring_fields_are_rejected)
{
    static const char *const manifests[] = {
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"managers\":[],"
        "\"scenes\":[]"
        "}",
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{\"name\":\"Level\",\"objects\":[{"
        "\"name\":\"Sound\","
        "\"class\":\"CKWaveSound\","
        "\"sound\":{\"file\":\"tone.wav\"}"
        "}]}]"
        "}",
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{\"name\":\"Level\",\"objects\":[{"
        "\"name\":\"Body\","
        "\"class\":\"CK3dEntity\","
        "\"physics\":{\"collision\":\"mesh\"}"
        "}]}]"
        "}",
    };

    for (size_t i = 0u; i < sizeof(manifests) / sizeof(manifests[0]); ++i) {
        nmo_project_plan_t *plan = NULL;
        nmo_status_t status = nmo_project_manifest_json_read(
            manifests[i],
            strlen(manifests[i]),
            &plan);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, status);
        ASSERT_NULL(plan);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_advanced_probes,
              sound_and_animation_skeletons_save_load_validate);
REGISTER_TEST(generated_advanced_probes,
              unproven_manifest_authoring_fields_are_rejected);
TEST_MAIN_END()
