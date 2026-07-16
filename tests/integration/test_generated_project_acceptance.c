#include "test_framework.h"

#include "core/nmo_arena.h"
#include "document/nmo_document_load.h"
#include "format/nmo_object.h"
#include "format/nmo_stb_adapter.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
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

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int write_png_file(const char *path)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (!arena) {
        return 0;
    }
    const uint8_t pixels[] = {
        255u, 255u, 255u, 255u,
        255u, 0u, 0u, 255u,
    };
    size_t png_size = 0u;
    uint8_t *png = nmo_stbi_write_to_memory(
        arena,
        NMO_BITMAP_FORMAT_PNG,
        2,
        1,
        4,
        pixels,
        90,
        &png_size);
    if (!png || png_size == 0u) {
        nmo_arena_destroy(arena);
        return 0;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        nmo_arena_destroy(arena);
        return 0;
    }
    int ok = fwrite(png, 1u, png_size, fp) == png_size;
    fclose(fp);
    nmo_arena_destroy(arena);
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

static nmo_object_t *find_named_object(
    nmo_document_t *document,
    const char *name,
    nmo_class_id_t class_id)
{
    nmo_object_t *object = NULL;
    if (nmo_object_query_find_first(
            document,
            &(nmo_object_query_t){
                .class_id = class_id,
                .name = name,
                .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
            },
            &object,
            NULL) != NMO_OK) {
        return NULL;
    }
    return object;
}

TEST(generated_project_acceptance, c_api_minimal_usage_fixture_generates_level)
{
    const char *output_path = "test_project_acceptance_tmp/c_api_minimal.cmo";
    make_dir("test_project_acceptance_tmp");
    remove(output_path);
    remove("test_project_acceptance_tmp/c_api_minimal.cmo.tmp");

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "CApiUsage"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &cube));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_primitive_mesh(plan, cube, NMO_PRIMITIVE_CUBE));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_material_diffuse(
                  plan,
                  cube,
                  1.0f,
                  0.0f,
                  0.0f,
                  1.0f));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK,
              nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_FALSE(report.dry_run);
    ASSERT_TRUE(nmo_project_report_diff_has_created_scene(&report, "Level"));
    ASSERT_TRUE(nmo_project_report_diff_has_created_object(&report, "Cube"));
    ASSERT_TRUE(nmo_project_report_diff_has_created_asset(&report, "Cube_Mesh"));

    char args[1024];
    snprintf(args, sizeof(args), "validate all \"%s\"", output_path);
    assert_cli_success_contains(args, "Result: VALID");

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);
    assert_named_class_exists(document, "Level", NMO_CID_SCENE);
    assert_named_class_exists(document, "Cube", NMO_CID_3DENTITY);
    assert_named_class_exists(document, "Cube_Mesh", NMO_CID_MESH);
    assert_named_class_exists(document, "Cube_Material", NMO_CID_MATERIAL);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_project_acceptance, cli_minimal_manifest_usage_fixture_dry_run_and_write)
{
    make_dir("test_project_acceptance_tmp");
    const char *manifest_path = "test_project_acceptance_tmp/minimal_usage.json";
    const char *output_path = "test_project_acceptance_tmp/minimal_usage.cmo";
    remove(manifest_path);
    remove(output_path);
    remove("test_project_acceptance_tmp/minimal_usage.cmo.tmp");

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"ManifestUsage\"},"
        "\"scenes\":[{\"name\":\"Level\",\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"primitive\":\"cube\"},"
        "\"material\":{\"color\":[0,1,0,1]}"
        "}]}]"
        "}";
    ASSERT_TRUE(write_text_file(manifest_path, manifest));

    char args[1024];
    snprintf(args, sizeof(args),
             "patch apply --project \"%s\" --dry-run -o \"%s\"",
             manifest_path,
             output_path);
    cli_run_result_t dry_run = run_cli_capture(args);
    if (dry_run.exit_code != 0) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args,
                dry_run.exit_code,
                dry_run.output ? dry_run.output : "(null)");
    }
    ASSERT_EQ(0, dry_run.exit_code);
    ASSERT_FALSE(file_exists(output_path));
    free(dry_run.output);

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
    assert_named_class_exists(document, "Cube", NMO_CID_3DENTITY);
    assert_named_class_exists(document, "Cube_Mesh", NMO_CID_MESH);
    assert_named_class_exists(document, "Cube_Material", NMO_CID_MATERIAL);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    remove(output_path);
    remove(manifest_path);
}

TEST(generated_project_acceptance, cli_generates_valid_cmo_from_manifest)
{
    make_dir("test_project_acceptance_tmp");
    const char *manifest_path = "test_project_acceptance_tmp/project.json";
    const char *obj_path = "test_project_acceptance_tmp/multi_material.obj";
    const char *png_path = "test_project_acceptance_tmp/blue.png";
    const char *sound_path = "test_project_acceptance_tmp/tone.wav";
    const char *output_path = "test_project_acceptance_tmp/project.cmo";
    remove(manifest_path);
    remove(obj_path);
    remove(png_path);
    remove(sound_path);
    remove(output_path);
    remove("test_project_acceptance_tmp/project.cmo.tmp");

    ASSERT_TRUE(write_text_file(
        obj_path,
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 1 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vt 1 1\n"
        "usemtl Red\n"
        "f 1/1 2/2 3/3\n"
        "usemtl Blue\n"
        "f 2/2 4/4 3/3\n"));
    ASSERT_TRUE(write_png_file(png_path));
    FILE *sound_file = fopen(sound_path, "wb");
    ASSERT_NOT_NULL(sound_file);
    fputs("RIFF", sound_file);
    fclose(sound_file);

    const char *manifest =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"GeneratedAcceptance\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"active_camera\":\"main-camera\","
            "\"environment\":{"
                "\"background_color\":[0.1,0.2,0.3,1],"
                "\"ambient_light\":[0.4,0.5,0.6,1],"
                "\"fog\":{\"mode\":\"linear\",\"color\":[0.7,0.8,0.9,1],"
                    "\"start\":12,\"end\":34,\"density\":0.25}"
            "},"
            "\"objects\":["
                "{\"id\":\"main-camera\",\"name\":\"Camera\",\"class\":\"CKTargetCamera\","
                    "\"camera\":{\"fov\":0.75,\"near\":0.25,\"far\":500,"
                        "\"target\":\"look-target\"}},"
                "{\"id\":\"key-light\",\"name\":\"Light\",\"class\":\"CKTargetLight\","
                    "\"light\":{\"diffuse\":[0.1,0.2,0.3,1],"
                        "\"range\":123,\"type\":\"directional\","
                        "\"target\":\"look-target\"}},"
                "{\"id\":\"mesh-child\",\"name\":\"MeshEntity\",\"class\":\"CK3dEntity\","
                    "\"parent\":\"parent\","
                    "\"mesh\":{\"obj\":\"multi_material.obj\"},"
                    "\"materials\":["
                        "{\"name\":\"Red\",\"color\":[1,0,0,1]},"
                        "{\"name\":\"Blue\","
                            "\"diffuse\":[0,1,0,1],"
                            "\"ambient\":[0,0,1,1],"
                            "\"specular\":[1,1,0,1],"
                            "\"emissive\":[1,0,1,1],"
                            "\"specular_power\":12.5,"
                            "\"texture\":\"blue.png\","
                            "\"blend\":{\"texture\":\"modulate\","
                                "\"source\":\"src_alpha\","
                                "\"destination\":\"inv_src_alpha\"},"
                            "\"filter\":{\"min\":\"linear\",\"mag\":\"nearest\"},"
                            "\"wrap\":\"clamp\","
                            "\"alpha\":{\"func\":\"greater_equal\"}}"
                    "],"
                    "\"transform\":{\"position\":[7,8,9],"
                        "\"rotation_euler_deg\":[0,0,0],"
                        "\"scale\":[2,3,4]},"
                    "\"scripts\":[{"
                        "\"name\":\"SceneStartScript\","
                        "\"template\":\"scene_on_start_debug_output\","
                        "\"message\":\"generated script start\""
                    "}]"
                "},"
                "{\"name\":\"LevelSound\",\"class\":\"CKSound\","
                    "\"sound\":{\"file\":\"tone.wav\"}"
                "},"
                "{\"id\":\"parent\",\"name\":\"Parent\",\"class\":\"CK3dEntity\"},"
                "{\"id\":\"look-target\",\"name\":\"LookTarget\",\"class\":\"CK3dEntity\"}"
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
    assert_named_class_exists(document, "Camera", NMO_CID_TARGETCAMERA);
    assert_named_class_exists(document, "Light", NMO_CID_TARGETLIGHT);
    assert_named_class_exists(document, "Parent", NMO_CID_3DENTITY);
    assert_named_class_exists(document, "LookTarget", NMO_CID_3DENTITY);
    assert_named_class_exists(document, "LevelSound", NMO_CID_SOUND);
    assert_named_class_exists(document, "MeshEntity", NMO_CID_3DENTITY);
    assert_named_class_exists(document, "MeshEntity_Mesh", NMO_CID_MESH);
    assert_named_class_exists(document, "MeshEntity_Red_Material", NMO_CID_MATERIAL);
    assert_named_class_exists(document, "MeshEntity_Blue_Material", NMO_CID_MATERIAL);
    assert_named_class_exists(document, "MeshEntity_Blue_Texture", NMO_CID_TEXTURE);
    assert_named_class_exists(document, "SceneStartScript", NMO_CID_BEHAVIOR);

    nmo_object_t *scene_object = find_named_object(document, "Level", NMO_CID_SCENE);
    nmo_object_t *cube_object = find_named_object(document, "MeshEntity", NMO_CID_3DENTITY);
    nmo_object_t *parent_object = find_named_object(document, "Parent", NMO_CID_3DENTITY);
    nmo_object_t *target_object = find_named_object(document, "LookTarget", NMO_CID_3DENTITY);
    nmo_object_t *camera_object = find_named_object(document, "Camera", NMO_CID_TARGETCAMERA);
    nmo_object_t *light_object = find_named_object(document, "Light", NMO_CID_TARGETLIGHT);
    nmo_object_t *sound_object = find_named_object(document, "LevelSound", NMO_CID_SOUND);
    nmo_object_t *mesh_object = find_named_object(document, "MeshEntity_Mesh", NMO_CID_MESH);
    nmo_object_t *red_material_object =
        find_named_object(document, "MeshEntity_Red_Material", NMO_CID_MATERIAL);
    nmo_object_t *blue_material_object =
        find_named_object(document, "MeshEntity_Blue_Material", NMO_CID_MATERIAL);
    nmo_object_t *texture_object =
        find_named_object(document, "MeshEntity_Blue_Texture", NMO_CID_TEXTURE);
    ASSERT_NOT_NULL(scene_object);
    ASSERT_NOT_NULL(cube_object);
    ASSERT_NOT_NULL(parent_object);
    ASSERT_NOT_NULL(target_object);
    ASSERT_NOT_NULL(camera_object);
    ASSERT_NOT_NULL(light_object);
    ASSERT_NOT_NULL(sound_object);
    ASSERT_NOT_NULL(mesh_object);
    ASSERT_NOT_NULL(red_material_object);
    ASSERT_NOT_NULL(blue_material_object);
    ASSERT_NOT_NULL(texture_object);

    nmo_object_id_t mesh_id = nmo_object_get_id(mesh_object);
    nmo_object_id_t red_material_id = nmo_object_get_id(red_material_object);
    nmo_object_id_t blue_material_id = nmo_object_get_id(blue_material_object);
    nmo_object_id_t texture_id = nmo_object_get_id(texture_object);
    nmo_object_id_t target_id = nmo_object_get_id(target_object);

    const nmo_scene_state_t *scene_state =
        (const nmo_scene_state_t *)nmo_object_get_state(scene_object);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(nmo_object_get_id(camera_object),
              nmo_ref_runtime_id(&scene_state->starting_camera));
    ASSERT_EQ(0xFF1A334Du, scene_state->background_color);
    ASSERT_EQ(0xFF668099u, scene_state->ambient_light_color);
    ASSERT_EQ(VXFOG_LINEAR, scene_state->fog_mode);
    ASSERT_EQ(0xFFB3CCE6u, scene_state->fog_color);
    ASSERT_FLOAT_EQ(12.0f, scene_state->fog_start, 0.0001f);
    ASSERT_FLOAT_EQ(34.0f, scene_state->fog_end, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, scene_state->fog_density, 0.0001f);

    const nmo_3dentity_state_t *cube_state =
        (const nmo_3dentity_state_t *)nmo_object_get_state(cube_object);
    ASSERT_NOT_NULL(cube_state);
    ASSERT_EQ(mesh_id, nmo_ref_runtime_id(&cube_state->current_mesh));
    ASSERT_EQ(nmo_object_get_id(parent_object),
              nmo_ref_runtime_id(&cube_state->parent));
    ASSERT_FLOAT_EQ(2.0f, cube_state->world_matrix[0], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, cube_state->world_matrix[5], 0.0001f);
    ASSERT_FLOAT_EQ(4.0f, cube_state->world_matrix[10], 0.0001f);
    ASSERT_FLOAT_EQ(7.0f, cube_state->world_matrix[12], 0.0001f);
    ASSERT_FLOAT_EQ(8.0f, cube_state->world_matrix[13], 0.0001f);
    ASSERT_FLOAT_EQ(9.0f, cube_state->world_matrix[14], 0.0001f);

    const nmo_targetcamera_state_t *target_camera_state =
        (const nmo_targetcamera_state_t *)nmo_object_get_state(camera_object);
    ASSERT_NOT_NULL(target_camera_state);
    ASSERT_TRUE(target_camera_state->has_target);
    ASSERT_EQ(target_id, nmo_ref_runtime_id(&target_camera_state->target));
    const nmo_camera_state_t *camera_state = &target_camera_state->base;
    ASSERT_NOT_NULL(camera_state);
    ASSERT_FLOAT_EQ(0.75f, camera_state->fov, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, camera_state->near_plane, 0.0001f);
    ASSERT_FLOAT_EQ(500.0f, camera_state->far_plane, 0.0001f);

    const nmo_targetlight_state_t *target_light_state =
        (const nmo_targetlight_state_t *)nmo_object_get_state(light_object);
    ASSERT_NOT_NULL(target_light_state);
    ASSERT_TRUE(target_light_state->has_target);
    ASSERT_EQ(target_id, nmo_ref_runtime_id(&target_light_state->target));
    const nmo_light_state_t *light_state = &target_light_state->base;
    ASSERT_NOT_NULL(light_state);
    ASSERT_FLOAT_EQ(26.0f / 255.0f, light_state->light_data.diffuse.r, 0.0001f);
    ASSERT_FLOAT_EQ(51.0f / 255.0f, light_state->light_data.diffuse.g, 0.0001f);
    ASSERT_FLOAT_EQ(77.0f / 255.0f, light_state->light_data.diffuse.b, 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, light_state->light_data.diffuse.a, 0.0001f);
    ASSERT_FLOAT_EQ(123.0f, light_state->light_data.range, 0.0001f);
    ASSERT_EQ(VX_LIGHTDIREC, light_state->light_data.type);

    const nmo_sound_state_t *sound_state =
        (const nmo_sound_state_t *)nmo_object_get_state(sound_object);
    ASSERT_NOT_NULL(sound_state);
    ASSERT_EQ(CKSOUND_INCLUDEORIGINALFILE, sound_state->save_options);
    ASSERT_STR_EQ("tone.wav", sound_state->file_name);

    const nmo_mesh_state_t *mesh_state =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh_state);
    ASSERT_EQ(4u, mesh_state->vertex_count);
    ASSERT_EQ(2u, mesh_state->face_count);
    ASSERT_EQ(2u, mesh_state->material_group_count);
    ASSERT_NOT_NULL(mesh_state->material_groups);
    ASSERT_EQ(red_material_id, mesh_state->material_groups[0].material_id);
    ASSERT_EQ(blue_material_id, mesh_state->material_groups[1].material_id);
    ASSERT_EQ(0u, mesh_state->faces[0].material_group_idx);
    ASSERT_EQ(1u, mesh_state->faces[1].material_group_idx);

    const nmo_material_state_t *material_state =
        (const nmo_material_state_t *)nmo_object_get_state(blue_material_object);
    ASSERT_NOT_NULL(material_state);
    ASSERT_EQ(0xFF00FF00u, material_state->diffuse_color);
    ASSERT_EQ(0xFF0000FFu, material_state->ambient_color);
    ASSERT_EQ(0xFFFFFF00u, material_state->specular_color);
    ASSERT_EQ(0xFFFF00FFu, material_state->emissive_color);
    ASSERT_FLOAT_EQ(12.5f, material_state->specular_power, 0.0001f);
    ASSERT_EQ(texture_id, nmo_material_texture_id(material_state, 0));
    ASSERT_EQ(VXTEXTUREBLEND_MODULATE,
              (VXTEXTURE_BLENDMODE)(material_state->packed_modes & 0xFu));
    ASSERT_EQ(VXTEXTUREFILTER_LINEAR,
              (VXTEXTURE_FILTERMODE)((material_state->packed_modes >> 4) & 0xFu));
    ASSERT_EQ(VXTEXTUREFILTER_NEAREST,
              (VXTEXTURE_FILTERMODE)((material_state->packed_modes >> 8) & 0xFu));
    ASSERT_EQ(VXBLEND_SRCALPHA,
              (VXBLEND_MODE)((material_state->packed_modes >> 12) & 0xFu));
    ASSERT_EQ(VXBLEND_INVSRCALPHA,
              (VXBLEND_MODE)((material_state->packed_modes >> 16) & 0xFu));
    ASSERT_EQ(VXTEXTURE_ADDRESSCLAMP,
              (VXTEXTURE_ADDRESSMODE)((material_state->packed_modes >> 28) & 0xFu));
    ASSERT_EQ(VXCMP_GREATEREQUAL,
              (VXCMPFUNC)((material_state->packed_flags >> 16) & 0x1Fu));

    const nmo_texture_state_t *texture_state =
        (const nmo_texture_state_t *)nmo_object_get_state(texture_object);
    ASSERT_NOT_NULL(texture_state);
    ASSERT_EQ(2, texture_state->reader_width);
    ASSERT_EQ(1, texture_state->reader_height);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    remove(output_path);
    remove(sound_path);
    remove(png_path);
    remove(obj_path);
    remove(manifest_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_project_acceptance, c_api_minimal_usage_fixture_generates_level);
REGISTER_TEST(generated_project_acceptance,
              cli_minimal_manifest_usage_fixture_dry_run_and_write);
REGISTER_TEST(generated_project_acceptance, cli_generates_valid_cmo_from_manifest);
TEST_MAIN_END()
