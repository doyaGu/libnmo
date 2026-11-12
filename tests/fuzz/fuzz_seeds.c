/**
 * @file fuzz_seeds.c
 * @brief Write seed inputs for the fuzz harnesses.
 *
 * Usage: fuzz_seeds <out-dir> [composition file...]
 *
 * <out-dir>/load  files for fuzz_load and fuzz_roundtrip: one object of every
 *                 class the library writes and a small scene with a script, both
 *                 built here, plus each composition file named on the command
 *                 line. All are saved without compression so that mutations land
 *                 in the chunks; the scene is also saved compressed once.
 * <out-dir>/chunk every object chunk of those files, for fuzz_chunk.
 * <out-dir>/obj   a small Wavefront OBJ, for fuzz_obj.
 *
 * Only the generated files are fit to commit or to share: seeds made from
 * composition files carry those files' content.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* setenv */
#endif

#include "fuzz_common.h"

#include "document/nmo_document_save.h"
#include "format/nmo_chunk.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
#include "runtime/nmo_document.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_serializer.h"
#include "../../src/runtime/runtime_internal.h"
#include "../../src/session/session_internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define fuzz_mkdir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define fuzz_mkdir(path) mkdir((path), 0755)
#endif

/* Ids 7, 14, 17 and 44 are not classes (see test_generated_chunk_roundtrip.c). */
#define FIRST_CLASS_ID 1
#define LAST_CLASS_ID 53

static const char k_obj_seed[] =
    "# unit cube\n"
    "mtllib cube.mtl\n"
    "o Cube\n"
    "v -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv -0.5 0.5 0.5\nv 0.5 0.5 0.5\n"
    "v -0.5 0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\n"
    "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\n"
    "vn 0 0 1\nvn 0 1 0\nvn 0 0 -1\nvn 0 -1 0\nvn 1 0 0\nvn -1 0 0\n"
    "g front\nusemtl Red\ns 1\n"
    "f 1/1/1 2/2/1 3/3/1\nf 3/3/1 2/2/1 4/4/1\n"
    "g rest\nusemtl Blue\ns off\n"
    "f 3/1/2 4/2/2 6/4/2 5/3/2\nf 5//3 6//3 8//3 7//3\nf 7/1 8/2 2/4 1/3\n"
    "f -6 -5 -3\nf 8 6 4 2\n"
    "l 1 2 3\np 4 5\n";

static char *format_path(const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (needed < 0) {
        va_end(copy);
        return NULL;
    }
    char *path = (char *)malloc((size_t)needed + 1u);
    if (path != NULL) {
        vsnprintf(path, (size_t)needed + 1u, format, copy);
    }
    va_end(copy);
    return path;
}

static int make_dir(const char *path) {
    if (fuzz_mkdir(path) == 0 || errno == EEXIST) {
        return 1;
    }
    fprintf(stderr, "cannot create %s\n", path);
    return 0;
}

static int write_file(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    size_t written = size > 0 ? fwrite(data, 1, size, file) : 0;
    int ok = written == size && fclose(file) == 0;
    if (!ok) {
        fprintf(stderr, "cannot write %s\n", path);
    }
    return ok;
}

static nmo_save_options_t save_options(int compressed) {
    nmo_save_options_t options = nmo_save_options_default();
    options.compress_header = compressed != 0;
    options.compress_data = compressed != 0;
    options.durability = NMO_SAVE_DURABILITY_FAST;
    return options;
}

static nmo_status_t generate_all_classes(nmo_context_t *ctx, const char *path) {
    nmo_document_t *document = nmo_document_create(ctx);
    if (document == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_workspace_t *workspace = NULL;
    nmo_status_t status = nmo_workspace_create(ctx, document, &workspace);
    for (int class_id = FIRST_CLASS_ID; status == NMO_OK && class_id <= LAST_CLASS_ID; class_id++) {
        if (class_id == 7 || class_id == 14 || class_id == 17 || class_id == 44) {
            continue;
        }
        char *name = format_path("obj_%d", class_id);
        if (name == NULL) {
            status = NMO_ERR_NOMEM;
            break;
        }
        nmo_object_id_t id = 0;
        status = nmo_session_create_object(nmo_workspace_internal_session(workspace),
                                           (nmo_class_id_t)class_id, name,
                                           (nmo_guid_t){0, 0}, &id, NULL);
        free(name);
    }
    nmo_save_options_t options = save_options(0);
    if (status == NMO_OK) {
        status = nmo_document_save_file(document, path, &options);
    }
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    return status;
}

/* A level with a camera, a light, a group, a parent and a child entity and a
 * script on the child. */
static nmo_status_t generate_scene(const char *path) {
    nmo_project_plan_t *plan = NULL;
    nmo_status_t status = nmo_project_plan_create(&plan);
    uint32_t scene = 0, camera = 0, light = 0, parent = 0, child = 0, script = 0, group = 0;
    if (status == NMO_OK) status = nmo_project_plan_set_document_name(plan, "FuzzSeed");
    if (status == NMO_OK) status = nmo_project_plan_add_scene(plan, "Scene_Main", &scene);
    nmo_project_object_spec_t spec = {.scene_handle = scene,
                                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE};
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_CAMERA;
        spec.name = "Camera_Main";
        status = nmo_project_plan_add_object(plan, &spec, &camera);
    }
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_LIGHT;
        spec.name = "Light_Key";
        status = nmo_project_plan_add_object(plan, &spec, &light);
    }
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_3DENTITY;
        spec.name = "Parent";
        status = nmo_project_plan_add_object(plan, &spec, &parent);
    }
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_3DENTITY;
        spec.name = "Child";
        status = nmo_project_plan_add_object(plan, &spec, &child);
    }
    if (status == NMO_OK) status = nmo_project_plan_add_group(plan, "Group_All", &group);
    if (status == NMO_OK) status = nmo_project_plan_add_group_member(plan, group, parent);
    if (status == NMO_OK) status = nmo_project_plan_add_group_member(plan, group, child);
    if (status == NMO_OK) status = nmo_project_plan_set_scene_active_camera(plan, scene, camera);
    if (status == NMO_OK) status = nmo_project_plan_set_object_parent(plan, child, parent);
    if (status == NMO_OK) status = nmo_project_plan_set_object_position(plan, child, 1.0f, 2.0f, 3.0f);
    if (status == NMO_OK) status = nmo_project_plan_add_object_script(plan, child, "ChildScript", &script);
    if (status == NMO_OK) {
        status = nmo_project_plan_script_add_on_start_debug_output(plan, script, "fuzz seed");
    }
    nmo_project_report_t report;
    nmo_project_report_init(&report);
    if (status == NMO_OK) status = nmo_project_executor_execute_to_file(plan, path, &report);
    if (status == NMO_OK && !report.ok) status = NMO_ERR_VALIDATION_FAILED;
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    (void)light;
    return status;
}

/* Load a composition file and save it again (compressed or not). */
static nmo_status_t resave(nmo_context_t *ctx, const char *source, const char *target,
                           int compressed) {
    size_t size = 0;
    uint8_t *data = fuzz_read_file(source, &size);
    if (data == NULL) {
        return NMO_ERR_FILE_NOT_FOUND;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    nmo_status_t status = session != NULL ? fuzz_load_memory(session, data, size, NULL)
                                          : NMO_ERR_NOMEM;
    nmo_save_options_t options = save_options(compressed);
    if (status == NMO_OK) {
        status = nmo_session_save_file(session, target, &options, NULL);
    }
    nmo_session_destroy(session);
    free(data);
    return status;
}

/* Write each object chunk of a composition file as nmo_chunk_serialize writes it. */
static size_t write_chunks(nmo_context_t *ctx, const char *source, const char *chunk_dir,
                           const char *stem) {
    size_t size = 0;
    uint8_t *data = fuzz_read_file(source, &size);
    if (data == NULL) {
        return 0;
    }
    size_t written = 0;
    nmo_session_t *session = nmo_session_create(ctx);
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (session != NULL && arena != NULL &&
        fuzz_load_memory(session, data, size, NULL) == NMO_OK) {
        nmo_object_repository_t *repository = nmo_session_get_repository(session);
        size_t count = nmo_object_repository_get_count(repository);
        for (size_t i = 0; i < count; i++) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repository, i);
            if (object == NULL || object->chunk == NULL) {
                continue;
            }
            void *bytes = NULL;
            size_t byte_count = 0;
            if (nmo_chunk_serialize(object->chunk, &bytes, &byte_count, arena) != NMO_OK) {
                continue;
            }
            char *path = format_path("%s/%s_%04zu.chunk", chunk_dir, stem, i);
            if (path != NULL && write_file(path, bytes, byte_count)) {
                written++;
            }
            free(path);
        }
    }
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    free(data);
    return written;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <out-dir> [composition file...]\n", argv[0]);
        return 2;
    }
    const char *out = argv[1];
    char *load_dir = format_path("%s/load", out);
    char *chunk_dir = format_path("%s/chunk", out);
    char *obj_dir = format_path("%s/obj", out);
    char *obj_path = format_path("%s/cube.obj", obj_dir != NULL ? obj_dir : "");
    if (load_dir == NULL || chunk_dir == NULL || obj_dir == NULL || obj_path == NULL ||
        !make_dir(out) || !make_dir(load_dir) || !make_dir(chunk_dir) || !make_dir(obj_dir)) {
        return 1;
    }
    if (!write_file(obj_path, k_obj_seed, sizeof(k_obj_seed) - 1u)) {
        return 1;
    }

    /* The project executor makes its own context, which finds the type tables
     * through NMO_DATA_DIR. */
#ifdef _WIN32
    if (getenv("NMO_DATA_DIR") == NULL) _putenv_s("NMO_DATA_DIR", NMO_FUZZ_DATA_DIR);
#else
    setenv("NMO_DATA_DIR", NMO_FUZZ_DATA_DIR, 0);
#endif

    nmo_context_t *ctx = fuzz_context_create();
    if (ctx == NULL) {
        fprintf(stderr, "cannot create a context\n");
        return 1;
    }

    int failed = 0;
    size_t files = 0;
    size_t chunks = 0;

    char *all_classes = format_path("%s/generated_all_classes.cmo", load_dir);
    char *scene_packed = format_path("%s/scene.tmp", out);
    char *scene = format_path("%s/generated_scene.cmo", load_dir);
    char *scene_compressed = format_path("%s/generated_scene_compressed.cmo", load_dir);
    if (all_classes == NULL || scene_packed == NULL || scene == NULL || scene_compressed == NULL) {
        return 1;
    }
    nmo_status_t status = generate_all_classes(ctx, all_classes);
    if (status != NMO_OK) {
        fprintf(stderr, "cannot generate %s: %s\n", all_classes, nmo_error_string(status));
        failed = 1;
    } else {
        files++;
        chunks += write_chunks(ctx, all_classes, chunk_dir, "generated_all_classes");
    }
    status = generate_scene(scene_packed);
    const char *step = "build";
    if (status == NMO_OK) {
        step = "resave";
        status = resave(ctx, scene_packed, scene, 0);
    }
    if (status == NMO_OK) status = resave(ctx, scene_packed, scene_compressed, 1);
    remove(scene_packed);
    if (status != NMO_OK) {
        fprintf(stderr, "cannot generate %s (%s): %s\n", scene, step, nmo_error_string(status));
        failed = 1;
    } else {
        files += 2;
        chunks += write_chunks(ctx, scene, chunk_dir, "generated_scene");
    }

    for (int i = 2; i < argc; i++) {
        char *target = format_path("%s/input_%03d.nmo", load_dir, i - 1);
        char *stem = format_path("input_%03d", i - 1);
        if (target == NULL || stem == NULL) {
            return 1;
        }
        status = resave(ctx, argv[i], target, 0);
        if (status != NMO_OK) {
            /* A file libnmo cannot load is still a useful seed, as it is. */
            size_t size = 0;
            uint8_t *data = fuzz_read_file(argv[i], &size);
            if (data == NULL || !write_file(target, data, size)) {
                fprintf(stderr, "cannot copy %s\n", argv[i]);
                failed = 1;
            } else {
                files++;
            }
            free(data);
        } else {
            files++;
            chunks += write_chunks(ctx, target, chunk_dir, stem);
        }
        free(target);
        free(stem);
    }

    printf("%zu files, %zu chunks, 1 OBJ in %s\n", files, chunks, out);
    nmo_context_release(ctx);
    free(all_classes);
    free(scene_packed);
    free(scene);
    free(scene_compressed);
    free(load_dir);
    free(chunk_dir);
    free(obj_dir);
    free(obj_path);
    return failed;
}
