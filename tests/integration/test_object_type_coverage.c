/**
 * @file test_object_type_coverage.c
 * @brief Verify that the test data corpus covers all concrete object types.
 *
 * Loads every .nmo and .cmo file from data/ and tracks which CK class IDs
 * appear. Reports coverage and fails if too many concrete types are missing.
 */

#include "../test_framework.h"
#include "session/nmo_context.h"
#include "document/nmo_document_load.h"
#include "session/nmo_session.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* Track which CIDs we've seen. Max CID in nmo_class_ids.h is 53. */
#define MAX_CID 64
static int g_cid_seen[MAX_CID];

static void scan_session_objects(nmo_session_t *session) {
    nmo_object_t **objects = NULL;
    size_t count = 0;
    if (nmo_session_get_objects(session, &objects, &count) != NMO_OK) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
        if (cid < MAX_CID) {
            g_cid_seen[cid] = 1;
        }
    }
}

static int load_and_scan(const char *path) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) return 0;

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return 0;
    }

    int result = nmo_load_file(session, path, NULL);
    if (result == NMO_OK) {
        scan_session_objects(session);
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return (result == NMO_OK) ? 1 : 0;
}

#ifdef _WIN32
static size_t collect_and_scan_ext(const char *dir, const char *ext) {
    size_t loaded = 0;

    /* Scan files matching *ext in this directory */
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*%s", dir, ext);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, fd.cFileName);
            if (load_and_scan(full_path)) loaded++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* Recurse into subdirectories */
    char dir_pattern[512];
    snprintf(dir_pattern, sizeof(dir_pattern), "%s\\*", dir);

    h = FindFirstFileA(dir_pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

            char subdir[512];
            snprintf(subdir, sizeof(subdir), "%s/%s", dir, fd.cFileName);
            loaded += collect_and_scan_ext(subdir, ext);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    return loaded;
}
#else
static size_t collect_and_scan_ext(const char *dir, const char *ext) {
    DIR *d = opendir(dir);
    if (!d) return 0;

    size_t elen = strlen(ext);
    size_t loaded = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            loaded += collect_and_scan_ext(full_path, ext);
            continue;
        }

        size_t nlen = strlen(entry->d_name);
        if (nlen < elen || strcmp(entry->d_name + nlen - elen, ext) != 0) continue;
        if (load_and_scan(full_path)) loaded++;
    }
    closedir(d);
    return loaded;
}
#endif

typedef struct {
    int cid;
    const char *name;
    int abstract; /* 1 = abstract base, never serialized directly */
} cid_entry_t;

static const cid_entry_t CORE_TYPES[] = {
    /* Abstract bases (never appear as concrete type in files) */
    { NMO_CID_OBJECT,                 "CKObject",                1 },
    { NMO_CID_BEOBJECT,               "CKBeObject",              1 },
    { NMO_CID_SCENEOBJECT,            "CKSceneObject",           1 },
    { NMO_CID_RENDEROBJECT,           "CKRenderObject",          1 },

    /* Concrete types that should appear in test corpus */
    { NMO_CID_GROUP,                   "CKGroup",                0 },
    { NMO_CID_LEVEL,                   "CKLevel",                0 },
    { NMO_CID_SCENE,                   "CKScene",                0 },
    { NMO_CID_BEHAVIOR,               "CKBehavior",              0 },
    { NMO_CID_BEHAVIORIO,             "CKBehaviorIO",            0 },
    { NMO_CID_BEHAVIORLINK,           "CKBehaviorLink",          0 },
    { NMO_CID_PARAMETER,              "CKParameter",             0 },
    { NMO_CID_PARAMETERIN,            "CKParameterIn",           0 },
    { NMO_CID_PARAMETEROUT,           "CKParameterOut",          0 },
    { NMO_CID_PARAMETERLOCAL,         "CKParameterLocal",        0 },
    { NMO_CID_PARAMETEROPERATION,     "CKParameterOperation",    0 },
    { NMO_CID_3DENTITY,               "CK3dEntity",              0 },
    { NMO_CID_3DOBJECT,               "CK3dObject",              0 },
    { NMO_CID_CAMERA,                 "CKCamera",                0 },
    { NMO_CID_TARGETCAMERA,           "CKTargetCamera",          0 },
    { NMO_CID_LIGHT,                  "CKLight",                 0 },
    { NMO_CID_TARGETLIGHT,            "CKTargetLight",           0 },
    { NMO_CID_MATERIAL,               "CKMaterial",              0 },
    { NMO_CID_TEXTURE,                "CKTexture",               0 },
    { NMO_CID_MESH,                   "CKMesh",                  0 },
    { NMO_CID_2DENTITY,               "CK2dEntity",              0 },
    { NMO_CID_SPRITE,                 "CKSprite",                0 },
    { NMO_CID_SOUND,                  "CKSound",                 0 },
    { NMO_CID_WAVESOUND,              "CKWaveSound",             0 },
    { NMO_CID_DATAARRAY,              "CKDataArray",             0 },
};
#define CORE_TYPE_COUNT (sizeof(CORE_TYPES) / sizeof(CORE_TYPES[0]))

TEST(coverage, all_core_types_present) {
    memset(g_cid_seen, 0, sizeof(g_cid_seen));

    const char *data_dir = NMO_TEST_DATA_DIR;
    size_t loaded_nmo = collect_and_scan_ext(data_dir, ".nmo");
    size_t loaded_cmo = collect_and_scan_ext(data_dir, ".cmo");
    printf("  Loaded %zu .nmo + %zu .cmo files from %s\n",
           loaded_nmo, loaded_cmo, data_dir);

    int concrete_total = 0;
    int concrete_found = 0;
    int abstract_found = 0;

    printf("  Object type coverage report:\n");
    for (size_t i = 0; i < CORE_TYPE_COUNT; i++) {
        int cid = CORE_TYPES[i].cid;
        const char *name = CORE_TYPES[i].name;
        int is_abstract = CORE_TYPES[i].abstract;

        if (is_abstract) {
            printf("    [%c] CID %2d %-24s (abstract)\n",
                   g_cid_seen[cid] ? 'x' : '-', cid, name);
            if (g_cid_seen[cid]) abstract_found++;
        } else {
            concrete_total++;
            if (g_cid_seen[cid]) {
                printf("    [x] CID %2d %-24s\n", cid, name);
                concrete_found++;
            } else {
                printf("    [ ] CID %2d %-24s  ** MISSING **\n", cid, name);
            }
        }
    }

    int missing = concrete_total - concrete_found;
    printf("  Concrete type coverage: %d/%d\n", concrete_found, concrete_total);
    if (missing > 0) {
        printf("  WARNING: %d concrete type(s) not found in test corpus\n", missing);
    }

    /* Require >= 75% concrete type coverage */
    ASSERT_TRUE(concrete_found >= concrete_total * 3 / 4);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(coverage, all_core_types_present);
TEST_MAIN_END()
