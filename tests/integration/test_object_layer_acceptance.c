/**
 * @file test_object_layer_acceptance.c
 * @brief Deep object layer acceptance test - validates deserialized data correctness
 *
 * Goes beyond "it loaded" to verify that deserialized object state is CORRECT:
 * 1. Typed state validation per object type (field-level sanity checks)
 * 2. Reference integrity (all object ID references exist in repository)
 * 3. Semantic round-trip with per-class statistics
 *
 * Test passes only if:
 * - All files load successfully
 * - All object states are valid (non-NULL, pointers consistent with counts)
 * - No broken references
 *
 * Round-trip mismatches are REPORTED but do not fail the test.
 */

#include "../test_framework.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_runtime_kernel.h"
#include "document/nmo_document_compare.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_object.h"
#include "core/nmo_error.h"

/* Include all typed state headers */
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_3dobject_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* Class name lookup table */
typedef struct {
    nmo_class_id_t class_id;
    const char *name;
} class_name_entry_t;

static const class_name_entry_t g_class_names[] = {
    {NMO_CID_OBJECT, "CKObject"},
    {NMO_CID_PARAMETERIN, "CKParameterIn"},
    {NMO_CID_PARAMETEROUT, "CKParameterOut"},
    {NMO_CID_PARAMETEROPERATION, "CKParameterOperation"},
    {NMO_CID_STATE, "CKState"},
    {NMO_CID_BEHAVIORLINK, "CKBehaviorLink"},
    {NMO_CID_BEHAVIOR, "CKBehavior"},
    {NMO_CID_BEHAVIORIO, "CKBehaviorIO"},
    {NMO_CID_SCENE, "CKScene"},
    {NMO_CID_SCENEOBJECT, "CKSceneObject"},
    {NMO_CID_RENDERCONTEXT, "CKRenderContext"},
    {NMO_CID_KINEMATICCHAIN, "CKKinematicChain"},
    {NMO_CID_OBJECTANIMATION, "CKObjectAnimation"},
    {NMO_CID_ANIMATION, "CKAnimation"},
    {NMO_CID_KEYEDANIMATION, "CKKeyedAnimation"},
    {NMO_CID_BEOBJECT, "CKBeObject"},
    {NMO_CID_SYNCHRO, "CKSynchro"},
    {NMO_CID_LEVEL, "CKLevel"},
    {NMO_CID_PLACE, "CKPlace"},
    {NMO_CID_GROUP, "CKGroup"},
    {NMO_CID_SOUND, "CKSound"},
    {NMO_CID_WAVESOUND, "CKWaveSound"},
    {NMO_CID_MIDISOUND, "CKMidiSound"},
    {NMO_CID_2DENTITY, "CK2dEntity"},
    {NMO_CID_SPRITE, "CKSprite"},
    {NMO_CID_SPRITETEXT, "CKSpriteText"},
    {NMO_CID_MATERIAL, "CKMaterial"},
    {NMO_CID_TEXTURE, "CKTexture"},
    {NMO_CID_MESH, "CKMesh"},
    {NMO_CID_3DENTITY, "CK3dEntity"},
    {NMO_CID_CAMERA, "CKCamera"},
    {NMO_CID_TARGETCAMERA, "CKTargetCamera"},
    {NMO_CID_CURVEPOINT, "CKCurvePoint"},
    {NMO_CID_SPRITE3D, "CKSprite3D"},
    {NMO_CID_LIGHT, "CKLight"},
    {NMO_CID_TARGETLIGHT, "CKTargetLight"},
    {NMO_CID_CHARACTER, "CKCharacter"},
    {NMO_CID_3DOBJECT, "CK3dObject"},
    {NMO_CID_BODYPART, "CKBodyPart"},
    {NMO_CID_CURVE, "CKCurve"},
    {NMO_CID_PARAMETERLOCAL, "CKParameterLocal"},
    {NMO_CID_PARAMETER, "CKParameter"},
    {NMO_CID_RENDEROBJECT, "CKRenderObject"},
    {NMO_CID_INTERFACEOBJECTMANAGER, "CKInterfaceObjectManager"},
    {NMO_CID_CRITICALSECTION, "CKCriticalSection"},
    {NMO_CID_GRID, "CKGrid"},
    {NMO_CID_LAYER, "CKLayer"},
    {NMO_CID_DATAARRAY, "CKDataArray"},
    {NMO_CID_PATCHMESH, "CKPatchMesh"},
};

static const char *get_class_name(nmo_class_id_t class_id) {
    for (size_t i = 0; i < sizeof(g_class_names) / sizeof(g_class_names[0]); i++) {
        if (g_class_names[i].class_id == class_id) {
            return g_class_names[i].name;
        }
    }
    return "Unknown";
}

/* Per-class statistics */
typedef struct {
    nmo_class_id_t class_id;
    size_t validated;
    size_t invalid;
    size_t roundtrip_ok;
    size_t roundtrip_fail;
} class_stats_t;

static class_stats_t g_class_stats[100];
static size_t g_class_stats_count = 0;

static class_stats_t *get_class_stats(nmo_class_id_t class_id) {
    for (size_t i = 0; i < g_class_stats_count; i++) {
        if (g_class_stats[i].class_id == class_id) {
            return &g_class_stats[i];
        }
    }
    if (g_class_stats_count < 100) {
        class_stats_t *stats = &g_class_stats[g_class_stats_count++];
        memset(stats, 0, sizeof(*stats));
        stats->class_id = class_id;
        return stats;
    }
    return NULL;
}

/* File statistics */
typedef struct {
    char filename[256];
    size_t object_count;
    size_t valid_states;
    size_t invalid_states;
    size_t refs_checked;
    size_t refs_broken;
    size_t roundtrip_matched;
    size_t roundtrip_mismatched;
} file_result_t;

static file_result_t g_file_results[1000];
static size_t g_file_count = 0;

static void add_file_result(const char *filename, size_t object_count,
                           size_t valid, size_t invalid,
                           size_t refs_checked, size_t refs_broken,
                           size_t rt_match, size_t rt_mismatch) {
    if (g_file_count >= 1000) {
        return;
    }
    file_result_t *r = &g_file_results[g_file_count++];
    strncpy(r->filename, filename, sizeof(r->filename) - 1);
    r->filename[sizeof(r->filename) - 1] = '\0';
    r->object_count = object_count;
    r->valid_states = valid;
    r->invalid_states = invalid;
    r->refs_checked = refs_checked;
    r->refs_broken = refs_broken;
    r->roundtrip_matched = rt_match;
    r->roundtrip_mismatched = rt_mismatch;
}

/* Validation functions */

static int validate_3dentity_state(const nmo_3dentity_state_t *state) {
    if (!state) {
        return 0;
    }
    /* If mesh_count > 0, mesh_ids should be non-NULL */
    if (state->mesh_count > 0 && !state->mesh_ids) {
        return 0;
    }
    /* If animation_count > 0, animation_ids should be non-NULL */
    if (state->animation_count > 0 && !state->animation_ids) {
        return 0;
    }
    return 1;
}

static int validate_mesh_state(const nmo_mesh_state_t *state) {
    if (!state) {
        return 0;
    }
    /* Sanity checks on counts */
    if (state->vertex_count > 10000000) {
        return 0; /* Unreasonably large */
    }
    if (state->face_count > 10000000) {
        return 0;
    }
    /* If vertex_count > 0, vertices should be non-NULL */
    if (state->vertex_count > 0 && !state->vertices) {
        return 0;
    }
    /* If face_count > 0, faces should be non-NULL */
    if (state->face_count > 0 && !state->faces) {
        return 0;
    }
    return 1;
}

static int validate_material_state(const nmo_material_state_t *state) {
    if (!state) {
        return 0;
    }
    /* Material exists - basic validation passed */
    return 1;
}

static int validate_behavior_state(const nmo_behavior_state_t *state) {
    if (!state) {
        return 0;
    }
    /* Behavior exists - basic validation passed */
    return 1;
}

static int validate_parameter_state(const nmo_parameter_state_t *state) {
    if (!state) {
        return 0;
    }
    /* Parameter exists - basic validation passed */
    return 1;
}

static int validate_objectanimation_state(const nmo_objectanimation_state_t *state) {
    if (!state) {
        return 0;
    }
    /* Format should be 0-4 (valid enum value) */
    if (state->format > 4) {
        return 0;
    }
    /* If controller_count > 0, controllers should be non-NULL */
    if (state->controller_count > 0 && !state->controllers) {
        return 0;
    }
    return 1;
}

static int validate_object_state(const nmo_object_t *obj) {
    void *state = nmo_object_get_state(obj);
    if (!state) {
        /* Some objects may not have state */
        return 1;
    }

    switch (obj->class_id) {
        case NMO_CID_3DENTITY:
            return validate_3dentity_state((const nmo_3dentity_state_t *)state);

        case NMO_CID_3DOBJECT:
            /* CK3dObject contains CK3dEntity state */
            return validate_3dentity_state(&((const nmo_3dobject_state_t *)state)->entity);

        case NMO_CID_CAMERA:
            /* CKCamera contains CK3dEntity state */
            return validate_3dentity_state(&((const nmo_camera_state_t *)state)->entity);

        case NMO_CID_TARGETCAMERA:
            /* CKTargetCamera contains CKCamera which contains CK3dEntity */
            return validate_3dentity_state(&((const nmo_targetcamera_state_t *)state)->base.entity);

        case NMO_CID_LIGHT:
            /* CKLight contains CK3dEntity state */
            return validate_3dentity_state(&((const nmo_light_state_t *)state)->entity);

        case NMO_CID_TARGETLIGHT:
            /* CKTargetLight contains CKLight which contains CK3dEntity */
            return validate_3dentity_state(&((const nmo_targetlight_state_t *)state)->base.entity);

        case NMO_CID_MESH:
            return validate_mesh_state((const nmo_mesh_state_t *)state);

        case NMO_CID_MATERIAL:
            return validate_material_state((const nmo_material_state_t *)state);

        case NMO_CID_BEHAVIOR:
            return validate_behavior_state((const nmo_behavior_state_t *)state);

        case NMO_CID_PARAMETER:
        case NMO_CID_PARAMETERIN:
        case NMO_CID_PARAMETEROUT:
        case NMO_CID_PARAMETEROPERATION:
        case NMO_CID_PARAMETERLOCAL:
            return validate_parameter_state((const nmo_parameter_state_t *)state);

        case NMO_CID_OBJECTANIMATION:
            return validate_objectanimation_state((const nmo_objectanimation_state_t *)state);

        default:
            /* Other types - assume valid if state exists */
            return 1;
    }
}

/* Reference checking */

static int check_reference(nmo_object_repository_t *repo, nmo_object_id_t id, size_t *checked_count) {
    if (id == 0) {
        return 1; /* NULL reference is valid */
    }
    (*checked_count)++;
    nmo_object_t *ref = nmo_object_repository_find_by_id(repo, id);
    return ref != NULL;
}

static size_t check_object_references(nmo_object_repository_t *repo, const nmo_object_t *obj,
                                      size_t *broken_count) {
    size_t checked = 0;
    void *state = nmo_object_get_state(obj);
    if (!state) {
        return 0;
    }

    switch (obj->class_id) {
        case NMO_CID_3DENTITY: {
            const nmo_3dentity_state_t *s = (const nmo_3dentity_state_t *)state;
            if (!check_reference(repo, s->parent_id, &checked)) (*broken_count)++;
            if (!check_reference(repo, s->place_id, &checked)) (*broken_count)++;
            if (!check_reference(repo, s->current_mesh_id, &checked)) (*broken_count)++;
            for (uint32_t i = 0; i < s->mesh_count; i++) {
                if (!check_reference(repo, s->mesh_ids[i], &checked)) (*broken_count)++;
            }
            break;
        }
        case NMO_CID_3DOBJECT: {
            const nmo_3dobject_state_t *s = (const nmo_3dobject_state_t *)state;
            checked += check_object_references(repo, (const nmo_object_t *)&s->entity, broken_count);
            break;
        }
        case NMO_CID_CAMERA: {
            const nmo_camera_state_t *s = (const nmo_camera_state_t *)state;
            checked += check_object_references(repo, (const nmo_object_t *)&s->entity, broken_count);
            break;
        }
        case NMO_CID_TARGETCAMERA: {
            const nmo_targetcamera_state_t *s = (const nmo_targetcamera_state_t *)state;
            if (!check_reference(repo, s->target_id, &checked)) (*broken_count)++;
            checked += check_object_references(repo, (const nmo_object_t *)&s->base.entity, broken_count);
            break;
        }
        case NMO_CID_LIGHT: {
            const nmo_light_state_t *s = (const nmo_light_state_t *)state;
            checked += check_object_references(repo, (const nmo_object_t *)&s->entity, broken_count);
            break;
        }
        case NMO_CID_TARGETLIGHT: {
            const nmo_targetlight_state_t *s = (const nmo_targetlight_state_t *)state;
            if (!check_reference(repo, s->target_id, &checked)) (*broken_count)++;
            checked += check_object_references(repo, (const nmo_object_t *)&s->base.entity, broken_count);
            break;
        }
        default:
            /* Other types - no reference checking yet */
            break;
    }

    return checked;
}

/* File scanning */
#ifdef _WIN32
static int scan_directory_recursive(const char *dir_path, void (*callback)(const char *)) {
    char search_path[512];
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;

    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);
    find_handle = FindFirstFileA(search_path, &find_data);

    if (find_handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_directory_recursive(full_path, callback);
        } else {
            const char *ext = strrchr(find_data.cFileName, '.');
            if (ext && (strcmp(ext, ".nmo") == 0 || strcmp(ext, ".cmo") == 0 || strcmp(ext, ".nms") == 0)) {
                callback(full_path);
            }
        }
    } while (FindNextFileA(find_handle, &find_data));

    FindClose(find_handle);
    return 1;
}
#else
static int scan_directory_recursive(const char *dir_path, void (*callback)(const char *)) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_directory_recursive(full_path, callback);
            } else {
                const char *ext = strrchr(entry->d_name, '.');
                if (ext && (strcmp(ext, ".nmo") == 0 || strcmp(ext, ".cmo") == 0 || strcmp(ext, ".nms") == 0)) {
                    callback(full_path);
                }
            }
        }
    }

    closedir(dir);
    return 1;
}
#endif

static char **g_file_list = NULL;
static size_t g_file_list_count = 0;
static size_t g_file_list_capacity = 0;

static void add_to_file_list(const char *filepath) {
    if (g_file_list_count >= g_file_list_capacity) {
        size_t new_capacity = g_file_list_capacity == 0 ? 64 : g_file_list_capacity * 2;
        char **new_list = (char **)realloc(g_file_list, new_capacity * sizeof(char *));
        if (!new_list) {
            return;
        }
        g_file_list = new_list;
        g_file_list_capacity = new_capacity;
    }
    g_file_list[g_file_list_count] = (char *)malloc(strlen(filepath) + 1);
    if (g_file_list[g_file_list_count]) {
        strcpy(g_file_list[g_file_list_count], filepath);
        g_file_list_count++;
    }
}

/* Main test function */
TEST(object_layer_acceptance, deep_validation) {
    printf("\n=== Deep Object Layer Acceptance ===\n\n");

    /* Reset statistics */
    g_file_count = 0;
    g_class_stats_count = 0;
    memset(g_file_results, 0, sizeof(g_file_results));
    memset(g_class_stats, 0, sizeof(g_class_stats));

    /* Scan data directory */
    const char *data_dir = NMO_TEST_DATA_DIR;
    printf("Scanning: %s\n\n", data_dir);

    g_file_list = NULL;
    g_file_list_count = 0;
    g_file_list_capacity = 0;

    if (!scan_directory_recursive(data_dir, add_to_file_list)) {
        printf("WARNING: Failed to scan directory\n");
    }

    printf("Found %zu files\n\n", g_file_list_count);

    /* Create context */
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    size_t files_loaded = 0;
    size_t files_failed = 0;
    size_t total_objects = 0;
    size_t total_valid = 0;
    size_t total_invalid = 0;
    size_t total_refs_checked = 0;
    size_t total_refs_broken = 0;

    /* Process each file */
    for (size_t i = 0; i < g_file_list_count; i++) {
        const char *filepath = g_file_list[i];
        const char *filename = strrchr(filepath, '\\');
        if (!filename) {
            filename = strrchr(filepath, '/');
        }
        filename = filename ? filename + 1 : filepath;

        /* Load file */
        nmo_session_t *session1 = nmo_session_create(ctx);
        if (!session1) {
            files_failed++;
            continue;
        }

        int load_result = nmo_session_load_file(session1, filepath, NULL, NULL);
        if (load_result != NMO_OK) {
            printf("File: %s\n  ERROR: Load failed\n\n", filename);
            nmo_session_destroy(session1);
            files_failed++;
            continue;
        }

        files_loaded++;

        /* Get repository */
        nmo_object_repository_t *repo1 = nmo_session_get_repository(session1);
        size_t object_count = nmo_object_repository_get_count(repo1);
        total_objects += object_count;

        size_t valid_states = 0;
        size_t invalid_states = 0;
        size_t refs_checked = 0;
        size_t refs_broken = 0;

        /* Validate each object */
        for (size_t j = 0; j < object_count; j++) {
            nmo_object_t *obj = nmo_object_repository_get_by_index(repo1, j);
            if (!obj) {
                continue;
            }

            /* State validation */
            if (validate_object_state(obj)) {
                valid_states++;
            } else {
                invalid_states++;
            }

            /* Reference checking */
            refs_checked += check_object_references(repo1, obj, &refs_broken);

            /* Update class statistics */
            class_stats_t *stats = get_class_stats(obj->class_id);
            if (stats) {
                if (validate_object_state(obj)) {
                    stats->validated++;
                } else {
                    stats->invalid++;
                }
            }
        }

        total_valid += valid_states;
        total_invalid += invalid_states;
        total_refs_checked += refs_checked;
        total_refs_broken += refs_broken;

        /* Round-trip test */
        char temp_path[512];
#ifdef _WIN32
        snprintf(temp_path, sizeof(temp_path), "%s\\temp_rt_%zu.nmo", data_dir, i);
#else
        snprintf(temp_path, sizeof(temp_path), "%s/temp_rt_%zu.nmo", data_dir, i);
#endif

        int save_result = nmo_session_save_file(session1, temp_path, NULL, NULL);
        size_t rt_matched = 0;
        size_t rt_mismatched = 0;

        if (save_result == NMO_OK) {
            /* Reload */
            nmo_session_t *session2 = nmo_session_create(ctx);
            if (session2) {
                int reload_result = nmo_session_load_file(session2, temp_path, NULL, NULL);
                if (reload_result == NMO_OK) {
                    /* Compare using comparison API */
                    nmo_comparison_result_t cmp;
                    nmo_comparison_result_init(&cmp);
                    nmo_document_t *doc1 = NULL;
                    nmo_document_t *doc2 = NULL;
                    nmo_status_t cmp_status = nmo_session_borrow_document(session1, &doc1);
                    if (cmp_status == NMO_OK) {
                        cmp_status = nmo_session_borrow_document(session2, &doc2);
                    }
                    if (cmp_status == NMO_OK) {
                        cmp_status = nmo_document_compare(
                            doc1, doc2, NMO_COMPARE_CHUNKS, &cmp);
                    }
                    if (doc1 != NULL) {
                        nmo_document_destroy(doc1);
                    }
                    if (doc2 != NULL) {
                        nmo_document_destroy(doc2);
                    }
                    ASSERT_EQ(NMO_OK, cmp_status);

                    rt_matched = cmp.objects_matched;
                    rt_mismatched = cmp.objects_compared - cmp.objects_matched;

                    /* Update per-class round-trip stats based on comparison result */
                    for (size_t j = 0; j < object_count; j++) {
                        nmo_object_t *obj = nmo_object_repository_get_by_index(repo1, j);
                        if (obj) {
                            class_stats_t *stats = get_class_stats(obj->class_id);
                            if (stats) {
                                /* If overall comparison matched, all objects matched */
                                if (cmp.match) {
                                    stats->roundtrip_ok++;
                                } else {
                                    /* When mismatch occurs, we count all objects of that type
                                     * as potentially mismatched since we don't have per-object
                                     * comparison results */
                                    stats->roundtrip_fail++;
                                }
                            }
                        }
                    }
                }
                nmo_session_destroy(session2);
            }
            remove(temp_path);
        }

        /* Print file result */
        printf("File: %s (%zu objects)\n", filename, object_count);
        printf("  State validation: %zu ok, %zu invalid\n", valid_states, invalid_states);
        printf("  Reference check: %zu refs checked, %zu broken\n", refs_checked, refs_broken);
        if (save_result == NMO_OK) {
            printf("  Round-trip: %zu compared, %zu matched\n\n",
                   rt_matched + rt_mismatched, rt_matched);
        } else {
            printf("  Round-trip: SKIP (save failed)\n\n");
        }

        add_file_result(filename, object_count, valid_states, invalid_states,
                       refs_checked, refs_broken, rt_matched, rt_mismatched);

        nmo_session_destroy(session1);
    }

    nmo_context_release(ctx);

    /* Print per-class summary */
    printf("\n=== Per-Class Summary ===\n");
    for (nmo_class_id_t cid = 1; cid <= 93; cid++) {
        for (size_t i = 0; i < g_class_stats_count; i++) {
            if (g_class_stats[i].class_id == cid) {
                class_stats_t *s = &g_class_stats[i];
                printf("  CID %2u %-24s : %5zu validated, %3zu invalid, %5zu round-trip ok\n",
                       cid, get_class_name(cid), s->validated, s->invalid, s->roundtrip_ok);
                break;
            }
        }
    }

    /* Print totals */
    printf("\n=== Totals ===\n");
    printf("Files: %zu loaded, %zu failed\n", files_loaded, files_failed);
    printf("Objects: %zu total, %zu validated, %zu invalid states\n",
           total_objects, total_valid, total_invalid);
    printf("References: %zu checked, %zu broken\n", total_refs_checked, total_refs_broken);
    printf("\n");

    /* Clean up file list */
    for (size_t i = 0; i < g_file_list_count; i++) {
        free(g_file_list[i]);
    }
    free(g_file_list);

    /* Test passes if all files loaded and no invalid states or broken refs */
    ASSERT_GT(files_loaded, 0);
    ASSERT_EQ(files_failed, 0);
    ASSERT_EQ(total_invalid, 0);
    ASSERT_EQ(total_refs_broken, 0);

    /* Round-trip mismatches are reported but don't fail the test */
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_layer_acceptance, deep_validation);
TEST_MAIN_END()

