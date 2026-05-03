#include "test_framework.h"

#include "document/nmo_document_load.h"
#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_repository.h"
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

static nmo_object_t *find_named_object(
    nmo_document_t *document,
    const char *name)
{
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    return repo ? nmo_object_repository_find_by_name(repo, name) : NULL;
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

TEST(generated_advanced_probes, wavesound_field_semantics_save_load_validate)
{
    const char *output_path = "test_generated_wavesound_probe.cmo";
    remove(output_path);
    remove("test_generated_wavesound_probe.cmo.tmp");

    nmo_session_field_edit_t sound_fields[] = {
        {.field_name = "has_wave_file_name", .value_str = "true"},
        {.field_name = "wave_file_name", .value_str = "\"tone.wav\""},
        {.field_name = "has_duration", .value_str = "true"},
        {.field_name = "duration", .value_str = "44100"},
        {.field_name = "has_data2", .value_str = "true"},
        {.field_name = "state_flags", .value_str = "1"},
        {.field_name = "priority", .value_str = "0.25"},
        {.field_name = "gain", .value_str = "0.75"},
        {.field_name = "pan", .value_str = "-0.5"},
        {.field_name = "pitch", .value_str = "1.25"},
        {.field_name = "cone_in_angle", .value_str = "30"},
        {.field_name = "cone_out_angle", .value_str = "60"},
        {.field_name = "cone_out_gain", .value_str = "0.125"},
        {.field_name = "min_distance", .value_str = "2"},
        {.field_name = "max_distance", .value_str = "20"},
        {.field_name = "distance_behavior", .value_str = "3"},
        {.field_name = "attached_object_id", .value_str = "3"},
        {.field_name = "position", .value_str = "(1, 2, 3)"},
        {.field_name = "direction", .value_str = "(0, 0, -1)"},
    };

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t anchor = 0u;
    uint32_t sound = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_document_name(plan, "WaveSoundProbe"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "SoundAnchor",
                  },
                  &anchor));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "ProbeWaveSound",
                      .fields = sound_fields,
                      .field_count = sizeof(sound_fields) / sizeof(sound_fields[0]),
                  },
                  &sound));
    ASSERT_TRUE(anchor != 0u);
    ASSERT_TRUE(sound != 0u);

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

    nmo_object_t *anchor_object =
        find_named_object(document, "SoundAnchor");
    nmo_object_t *sound_object =
        find_named_object(document, "ProbeWaveSound");
    ASSERT_NOT_NULL(anchor_object);
    ASSERT_EQ(NMO_CID_3DENTITY, nmo_object_get_class_id(anchor_object));
    ASSERT_NOT_NULL(sound_object);
    ASSERT_EQ(NMO_CID_WAVESOUND, nmo_object_get_class_id(sound_object));
    const nmo_wavesound_state_t *state =
        (const nmo_wavesound_state_t *)nmo_object_get_state(sound_object);
    ASSERT_NOT_NULL(state);

    ASSERT_TRUE(state->has_wave_file_name);
    ASSERT_STR_EQ("tone.wav", state->wave_file_name);
    ASSERT_TRUE(state->has_duration);
    ASSERT_EQ(44100, state->duration);
    ASSERT_TRUE(state->has_data2);
    ASSERT_EQ(1u, state->state_flags);
    ASSERT_FLOAT_EQ(0.25f, state->priority, 0.0001f);
    ASSERT_FLOAT_EQ(0.75f, state->gain, 0.0001f);
    ASSERT_FLOAT_EQ(-0.5f, state->pan, 0.0001f);
    ASSERT_FLOAT_EQ(1.25f, state->pitch, 0.0001f);
    ASSERT_FLOAT_EQ(30.0f, state->cone_in_angle, 0.0001f);
    ASSERT_FLOAT_EQ(60.0f, state->cone_out_angle, 0.0001f);
    ASSERT_FLOAT_EQ(0.125f, state->cone_out_gain, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, state->min_distance, 0.0001f);
    ASSERT_FLOAT_EQ(20.0f, state->max_distance, 0.0001f);
    ASSERT_EQ(3u, state->distance_behavior);
    ASSERT_EQ(nmo_object_get_id(anchor_object), state->attached_object_id);
    ASSERT_FLOAT_EQ(1.0f, state->position.x, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, state->position.y, 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, state->position.z, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, state->direction.x, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, state->direction.y, 0.0001f);
    ASSERT_FLOAT_EQ(-1.0f, state->direction.z, 0.0001f);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_advanced_probes, manifest_wavesound_authoring_save_load_validate)
{
    const char *output_path = "test_generated_wavesound_authoring.cmo";
    const char *sound_path = "test_generated_wavesound_authoring.wav";
    remove(output_path);
    remove("test_generated_wavesound_authoring.cmo.tmp");
    remove(sound_path);
    FILE *sound_file = fopen(sound_path, "wb");
    ASSERT_NOT_NULL(sound_file);
    fputs("RIFF", sound_file);
    fclose(sound_file);

    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"WaveSoundGenerated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":["
        "{\"id\":\"anchor\",\"name\":\"SoundAnchor\",\"class\":\"CK3dEntity\"},"
        "{\"name\":\"ProbeWaveSound\",\"class\":\"CKWaveSound\","
        "\"sound\":{\"file\":\"test_generated_wavesound_authoring.wav\","
        "\"gain\":0.8,\"pan\":0.25,\"pitch\":1.5,"
        "\"attached_object\":\"anchor\","
        "\"position\":[4,5,6],\"direction\":[0,1,0]}}"
        "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_report_t report;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

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

    nmo_object_t *anchor_object =
        find_named_object(document, "SoundAnchor");
    nmo_object_t *sound_object =
        find_named_object(document, "ProbeWaveSound");
    ASSERT_NOT_NULL(anchor_object);
    ASSERT_NOT_NULL(sound_object);
    const nmo_wavesound_state_t *state =
        (const nmo_wavesound_state_t *)nmo_object_get_state(sound_object);
    ASSERT_NOT_NULL(state);
    ASSERT_TRUE(state->has_wave_file_name);
    ASSERT_STR_EQ("test_generated_wavesound_authoring.wav", state->wave_file_name);
    ASSERT_TRUE(state->has_data2);
    ASSERT_FLOAT_EQ(0.8f, state->gain, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, state->pan, 0.0001f);
    ASSERT_FLOAT_EQ(1.5f, state->pitch, 0.0001f);
    ASSERT_EQ(nmo_object_get_id(anchor_object), state->attached_object_id);
    ASSERT_FLOAT_EQ(4.0f, state->position.x, 0.0001f);
    ASSERT_FLOAT_EQ(5.0f, state->position.y, 0.0001f);
    ASSERT_FLOAT_EQ(6.0f, state->position.z, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, state->direction.x, 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, state->direction.y, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, state->direction.z, 0.0001f);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
    remove(sound_path);
}

TEST(generated_advanced_probes, objectanimation_field_semantics_save_load_validate)
{
    const char *output_path = "test_generated_objectanimation_probe.cmo";
    remove(output_path);
    remove("test_generated_objectanimation_probe.cmo.tmp");

    nmo_session_field_edit_t animation_fields[] = {
        {.field_name = "format", .value_str = "2"},
        {.field_name = "root_pos", .value_str = "(1, 2, 3)"},
        {.field_name = "flags", .value_str = "1"},
        {.field_name = "entity_id", .value_str = "3"},
        {.field_name = "has_length", .value_str = "1"},
        {.field_name = "length", .value_str = "12.5"},
    };

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t entity = 0u;
    uint32_t animation = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_document_name(plan, "ObjectAnimationProbe"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "AnimatedEntity",
                  },
                  &entity));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "ProbeObjectAnimation",
                      .fields = animation_fields,
                      .field_count = sizeof(animation_fields) / sizeof(animation_fields[0]),
                  },
                  &animation));
    ASSERT_TRUE(entity != 0u);
    ASSERT_TRUE(animation != 0u);

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

    nmo_object_t *entity_object =
        find_named_object(document, "AnimatedEntity");
    nmo_object_t *animation_object =
        find_named_object(document, "ProbeObjectAnimation");
    ASSERT_NOT_NULL(entity_object);
    ASSERT_NOT_NULL(animation_object);
    const nmo_objectanimation_state_t *state =
        (const nmo_objectanimation_state_t *)nmo_object_get_state(animation_object);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS, state->format);
    ASSERT_TRUE(state->has_root_pos);
    ASSERT_FLOAT_EQ(1.0f, state->root_pos.x, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, state->root_pos.y, 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, state->root_pos.z, 0.0001f);
    ASSERT_EQ(1u, state->flags);
    ASSERT_EQ(nmo_object_get_id(entity_object), state->entity_id);
    ASSERT_TRUE(state->has_length);
    ASSERT_FLOAT_EQ(12.5f, state->length, 0.0001f);
    ASSERT_EQ(0u, state->controller_count);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_advanced_probes, material_packed_flag_semantics_save_load_validate)
{
    const char *output_path = "test_generated_material_flags_probe.cmo";
    remove(output_path);
    remove("test_generated_material_flags_probe.cmo.tmp");

    nmo_session_field_edit_t material_fields[] = {
        {.field_name = "packed_modes", .value_str = "858149412"},
        {.field_name = "packed_flags", .value_str = "17171465"},
    };

    nmo_project_plan_t *plan = NULL;
    uint32_t material = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_document_name(plan, "MaterialFlagProbe"));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_MATERIAL,
                      .name = "ProbeMaterial",
                      .fields = material_fields,
                      .field_count =
                          sizeof(material_fields) / sizeof(material_fields[0]),
                  },
                  &material));
    ASSERT_TRUE(material != 0u);

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

    nmo_object_t *material_object = find_named_object(document, "ProbeMaterial");
    ASSERT_NOT_NULL(material_object);
    ASSERT_EQ(NMO_CID_MATERIAL, nmo_object_get_class_id(material_object));
    const nmo_material_state_t *state =
        (const nmo_material_state_t *)nmo_object_get_state(material_object);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(858149412u, state->packed_modes);
    ASSERT_EQ(17171465u, state->packed_flags);

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
        "\"name\":\"Animation\","
        "\"class\":\"CKObjectAnimation\","
        "\"animation\":{\"file\":\"walk.anim\"}"
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
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{\"name\":\"Level\",\"objects\":[{"
        "\"name\":\"Body\","
        "\"class\":\"CK3dEntity\","
        "\"collision\":{\"type\":\"mesh\"}"
        "}]}]"
        "}",
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{\"name\":\"Level\",\"objects\":[{"
        "\"name\":\"Body\","
        "\"class\":\"CK3dEntity\","
        "\"manager\":{\"guid\":\"00000000-00000000\"}"
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
              wavesound_field_semantics_save_load_validate);
REGISTER_TEST(generated_advanced_probes,
              manifest_wavesound_authoring_save_load_validate);
REGISTER_TEST(generated_advanced_probes,
              objectanimation_field_semantics_save_load_validate);
REGISTER_TEST(generated_advanced_probes,
              material_packed_flag_semantics_save_load_validate);
REGISTER_TEST(generated_advanced_probes,
              unproven_manifest_authoring_fields_are_rejected);
TEST_MAIN_END()
