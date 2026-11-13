/**
 * @file test_run_host.c
 * @brief Tests for what a run's host gives the engine (tools/nmo_run_host.c):
 *        files found by the names a document gives in any case, registry
 *        values and keys held down by frame.
 */

#include "test_framework.h"
#include "nmo_run_host.h"
#include "../../src/platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUN_HOST_GAME NMO_TEST_SCRATCH_DIR "/run_host/Game"

/* A game folder holding base.cmo and Textures/Wood.BMP, in their own case */
static void make_game_folder(void)
{
    ASSERT_TRUE(nmo_platform_make_directory(NMO_TEST_SCRATCH_DIR) >= 0);
    ASSERT_TRUE(nmo_platform_make_directory(NMO_TEST_SCRATCH_DIR "/run_host") >= 0);
    ASSERT_TRUE(nmo_platform_make_directory(RUN_HOST_GAME) >= 0);
    ASSERT_TRUE(nmo_platform_make_directory(RUN_HOST_GAME "/Textures") >= 0);
    const char *files[] = {RUN_HOST_GAME "/base.cmo", RUN_HOST_GAME "/Textures/Wood.BMP"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        FILE *file = nmo_platform_fopen(files[i], "wb");
        ASSERT_NOT_NULL(file);
        ASSERT_EQ((size_t)1, fwrite("x", 1, 1, file));
        fclose(file);
    }
}

/* Whether `path` names a file that opens */
static bool opens(const char *path)
{
    FILE *file = path != NULL ? nmo_platform_fopen(path, "rb") : NULL;
    if (file != NULL) {
        fclose(file);
    }
    return file != NULL;
}

/* Windows finds a name in any case: so does the resolver on any file system */
TEST(run_host, files_are_found_in_any_case)
{
    make_game_folder();
    nmo_run_files_t files;
    ASSERT_TRUE(nmo_run_files_init(&files, RUN_HOST_GAME "/base.cmo", NULL, NULL, false));

    ASSERT_TRUE(opens(nmo_run_files_resolve(&files, "Textures\\Wood.BMP")));
    ASSERT_TRUE(opens(nmo_run_files_resolve(&files, "TEXTURES\\wood.bmp")));
    ASSERT_TRUE(opens(nmo_run_files_resolve(&files, "..\\textures\\WOOD.bmp")));
    ASSERT_TRUE(opens(nmo_run_files_resolve(&files, "BASE.CMO")));
    /* An absolute name is found by its last part only, in the folders */
    ASSERT_NULL(nmo_run_files_resolve(&files, "E:\\Gravity\\Textures\\wood.bmp"));
    ASSERT_NULL(nmo_run_files_resolve(&files, "Textures\\Stone.bmp"));
    ASSERT_NULL(nmo_run_files_resolve(&files, "Sounds\\Wood.BMP"));

    nmo_run_files_release(&files);
}

TEST(run_host, registry_entries_need_a_key_and_a_name)
{
    ASSERT_TRUE(nmo_run_set_registry(NULL, "Software\\Ballance\\Settings\\Language=1"));
    ASSERT_TRUE(nmo_run_set_registry(NULL, "A\\B=1;C\\D=two"));
    ASSERT_FALSE(nmo_run_set_registry(NULL, "Language=1"));
    ASSERT_FALSE(nmo_run_set_registry(NULL, "Software\\=1"));
}

TEST(run_host, keys_are_held_for_their_frames)
{
    ASSERT_EQ(-1L, nmo_run_parse_keys("Enter", NULL));
    ASSERT_EQ(-1L, nmo_run_parse_keys("Enter@9-3", NULL));
    ASSERT_EQ(-1L, nmo_run_parse_keys("NoSuchKey@3", NULL));
    nmo_run_key_t keys[3];
    ASSERT_EQ(3L, nmo_run_parse_keys("Enter@800-805,Up@3200,0x1C@900", keys));
    ASSERT_EQ(keys[0].key, keys[2].key);
    ASSERT_EQ(800u, keys[0].first);
    ASSERT_EQ(805u, keys[0].last);
    ASSERT_EQ(3200u, keys[1].first);
    ASSERT_EQ(3200u, keys[1].last);
    ASSERT_EQ(900u, keys[2].first);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(run_host, files_are_found_in_any_case);
    REGISTER_TEST(run_host, registry_entries_need_a_key_and_a_name);
    REGISTER_TEST(run_host, keys_are_held_for_their_frames);
TEST_MAIN_END()
