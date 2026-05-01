#include "test_framework.h"

#include "core/nmo_arena.h"
#include "document/nmo_document_load.h"
#include "format/nmo_object.h"
#include "format/nmo_stb_adapter.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
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

TEST(generated_project_acceptance, cli_generates_valid_cmo_from_manifest)
{
    make_dir("test_project_acceptance_tmp");
    const char *manifest_path = "test_project_acceptance_tmp/project.json";
    const char *obj_path = "test_project_acceptance_tmp/triangle.obj";
    const char *png_path = "test_project_acceptance_tmp/triangle.png";
    const char *output_path = "test_project_acceptance_tmp/project.cmo";
    remove(manifest_path);
    remove(obj_path);
    remove(png_path);
    remove(output_path);
    remove("test_project_acceptance_tmp/project.cmo.tmp");

    ASSERT_TRUE(write_text_file(
        obj_path,
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n"));
    ASSERT_TRUE(write_png_file(png_path));

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
                    "\"mesh\":{\"obj\":\"triangle.obj\"},"
                    "\"material\":{\"texture\":\"triangle.png\"},"
                    "\"transform\":{\"position\":[7,8,9]},"
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
    assert_named_class_exists(document, "Cube_Texture", NMO_CID_TEXTURE);
    assert_named_class_exists(document, "CubeScript", NMO_CID_BEHAVIOR);

    nmo_object_t *cube_object = find_named_object(document, "Cube", NMO_CID_3DENTITY);
    nmo_object_t *mesh_object = find_named_object(document, "Cube_Mesh", NMO_CID_MESH);
    nmo_object_t *material_object = find_named_object(document, "Cube_Material", NMO_CID_MATERIAL);
    nmo_object_t *texture_object = find_named_object(document, "Cube_Texture", NMO_CID_TEXTURE);
    ASSERT_NOT_NULL(cube_object);
    ASSERT_NOT_NULL(mesh_object);
    ASSERT_NOT_NULL(material_object);
    ASSERT_NOT_NULL(texture_object);

    nmo_object_id_t mesh_id = nmo_object_get_id(mesh_object);
    nmo_object_id_t material_id = nmo_object_get_id(material_object);
    nmo_object_id_t texture_id = nmo_object_get_id(texture_object);

    const nmo_3dentity_state_t *cube_state =
        (const nmo_3dentity_state_t *)nmo_object_get_state(cube_object);
    ASSERT_NOT_NULL(cube_state);
    ASSERT_EQ(mesh_id, cube_state->current_mesh_id);
    ASSERT_FLOAT_EQ(7.0f, cube_state->world_matrix[12], 0.0001f);
    ASSERT_FLOAT_EQ(8.0f, cube_state->world_matrix[13], 0.0001f);
    ASSERT_FLOAT_EQ(9.0f, cube_state->world_matrix[14], 0.0001f);

    const nmo_mesh_state_t *mesh_state =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh_state);
    ASSERT_EQ(3u, mesh_state->vertex_count);
    ASSERT_EQ(1u, mesh_state->face_count);
    ASSERT_EQ(1u, mesh_state->material_group_count);
    ASSERT_NOT_NULL(mesh_state->material_groups);
    ASSERT_EQ(material_id, mesh_state->material_groups[0].material_id);

    const nmo_material_state_t *material_state =
        (const nmo_material_state_t *)nmo_object_get_state(material_object);
    ASSERT_NOT_NULL(material_state);
    ASSERT_EQ(0xFFFFFFFFu, material_state->diffuse_color);
    ASSERT_EQ(texture_id, material_state->texture_ids[0]);

    const nmo_texture_state_t *texture_state =
        (const nmo_texture_state_t *)nmo_object_get_state(texture_object);
    ASSERT_NOT_NULL(texture_state);
    ASSERT_EQ(2, texture_state->reader_width);
    ASSERT_EQ(1, texture_state->reader_height);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    remove(output_path);
    remove(png_path);
    remove(obj_path);
    remove(manifest_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_project_acceptance, cli_generates_valid_cmo_from_manifest);
TEST_MAIN_END()
