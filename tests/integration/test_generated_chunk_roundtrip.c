/**
 * @file test_generated_chunk_roundtrip.c
 * @brief The save fidelity checks of the corpus tests, on files the library generates
 *
 * test_corpus_chunk_roundtrip and test_fidelity_save need the Virtools sample files,
 * which are not in the repository, so a clean checkout (and CI) skips them. These tests
 * build their own files and run the same comparisons, so the guarantees that a save
 * keeps what it did not change are checked everywhere:
 *
 *  - all_classes: one object of every class the library can write;
 *  - scene: a project-authored level with references between objects and a script;
 *  - foreign: all_classes with data the schemas do not model, as a newer tool or the
 *    engine itself would leave it (a trailing dword in the last section, or a whole
 *    section nobody knows).
 */

#include "chunk_roundtrip_check.h"

#include "document/nmo_document_load.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/nmo_object_system.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
#include "document/nmo_document.h"
#include "document/nmo_document_save.h"
#include "runtime/nmo_workspace.h"
#include "../../src/runtime/runtime_internal.h"

#define ALL_CLASSES_FILE "generated_all_classes.cmo"
#define SCENE_FILE "generated_scene.cmo"
#define FOREIGN_TRAILING_FILE "generated_foreign_trailing.cmo"
#define FOREIGN_SECTION_FILE "generated_foreign_section.cmo"

#define EXTRA_DWORD 0xC0FFEE42u
#define UNKNOWN_SECTION_ID 0x7E57C0DEu
#define UNKNOWN_SECTION_PAYLOAD 0x0DDBA11u

/* Ids 7, 14, 17 and 44 are not classes. */
#define FIRST_CLASS_ID 1
#define LAST_CLASS_ID 53

typedef enum extra_flavor {
    EXTRA_TRAILING_DWORD,
    EXTRA_UNKNOWN_SECTION
} extra_flavor_t;

static nmo_context_t *make_context(void)
{
    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    return nmo_context_create(&desc);
}

static int is_class_id(int class_id)
{
    return class_id != 7 && class_id != 14 && class_id != 17 && class_id != 44;
}

/* One object of every class, with a name that tells the class. */
static int generate_all_classes(nmo_context_t *ctx, const char *path, size_t *out_created)
{
    nmo_document_t *document = nmo_document_create(ctx);
    if (document == NULL) return NMO_ERR_NOMEM;
    nmo_workspace_t *workspace = NULL;
    int status = nmo_workspace_create(ctx, document, &workspace);
    size_t created = 0;
    for (int class_id = FIRST_CLASS_ID; status == NMO_OK && class_id <= LAST_CLASS_ID; class_id++) {
        if (!is_class_id(class_id)) continue;
        char name[32];
        snprintf(name, sizeof(name), "obj_%d", class_id);
        nmo_object_id_t id = 0;
        status = nmo_session_create_object(nmo_workspace_internal_session(workspace),
                                           (nmo_class_id_t)class_id, name,
                                           (nmo_guid_t){0, 0}, &id, NULL);
        if (status == NMO_OK) created++;
    }
    if (status == NMO_OK) status = nmo_document_save_file(document, path, NULL);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    if (out_created != NULL) *out_created = created;
    return status;
}

/* A level with a camera, a light, a parent and a child entity and a script on the child. */
static int generate_scene(const char *path)
{
    nmo_project_plan_t *plan = NULL;
    int status = nmo_project_plan_create(&plan);
    uint32_t scene = 0, camera = 0, light = 0, parent = 0, child = 0, script = 0;
    if (status == NMO_OK) status = nmo_project_plan_set_document_name(plan, "GeneratedChunks");
    if (status == NMO_OK) status = nmo_project_plan_add_scene(plan, "Scene_Main", &scene);
    nmo_project_object_spec_t spec = {.scene_handle = scene,
                                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE};
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_CAMERA; spec.name = "Camera_Main";
        status = nmo_project_plan_add_object(plan, &spec, &camera);
    }
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_LIGHT; spec.name = "Light_Key";
        status = nmo_project_plan_add_object(plan, &spec, &light);
    }
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_3DENTITY; spec.name = "Parent";
        status = nmo_project_plan_add_object(plan, &spec, &parent);
    }
    if (status == NMO_OK) {
        spec.class_id = NMO_CID_3DENTITY; spec.name = "Child";
        status = nmo_project_plan_add_object(plan, &spec, &child);
    }
    if (status == NMO_OK) status = nmo_project_plan_set_scene_active_camera(plan, scene, camera);
    if (status == NMO_OK) status = nmo_project_plan_set_object_parent(plan, child, parent);
    if (status == NMO_OK) status = nmo_project_plan_set_object_position(plan, child, 1.0f, 2.0f, 3.0f);
    if (status == NMO_OK) status = nmo_project_plan_add_object_script(plan, child, "ChildScript", &script);
    if (status == NMO_OK) {
        status = nmo_project_plan_script_add_on_start_debug_output(plan, script, "generated chunks");
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

/* Position of the identifier that starts the last section, or SIZE_MAX when the data
 * is not a clean chain of sections. */
static size_t last_section_start(const nmo_chunk_t *chunk)
{
    const uint32_t *d = (const uint32_t *)chunk->data.data;
    const size_t count = chunk->data.count;
    if (count < 2u || d[0] == 0u) return (size_t)-1;
    size_t pos = 0;
    while (d[pos + 1u] != 0u) {
        const size_t next = d[pos + 1u];
        if (next <= pos + 1u || next + 1u >= count || d[next] == 0u) return (size_t)-1;
        pos = next;
    }
    return pos;
}

static int add_unmodeled_data(nmo_chunk_t *chunk, extra_flavor_t flavor)
{
    const size_t start = last_section_start(chunk);
    if (start == (size_t)-1) return 0;
    const size_t old_count = chunk->data.count;
    const size_t added = flavor == EXTRA_TRAILING_DWORD ? 1u : 3u;
    if (nmo_arena_array_resize(&chunk->data, old_count + added) != NMO_OK) return 0;
    uint32_t *d = (uint32_t *)chunk->data.data;
    if (flavor == EXTRA_TRAILING_DWORD) {
        d[old_count] = EXTRA_DWORD;
    } else {
        d[start + 1u] = (uint32_t)old_count;   /* the old last section now links to the new one */
        d[old_count] = UNKNOWN_SECTION_ID;
        d[old_count + 1u] = 0u;
        d[old_count + 2u] = UNKNOWN_SECTION_PAYLOAD;
    }
    return 1;
}

/* all_classes with unmodeled data in every chunk that takes it. The file comes out of an
 * ordinary default save of objects nobody edited, so it carries the data exactly as the
 * chunks held it. */
static int generate_foreign(nmo_context_t *ctx, const char *source, const char *path,
                            extra_flavor_t flavor, size_t *out_with_residue)
{
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) return NMO_ERR_NOMEM;
    int status = nmo_session_load_file(session, source, NULL, NULL);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    size_t modified = 0;
    for (size_t i = 0; status == NMO_OK && i < nmo_object_repository_get_count(repo); i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object != NULL && object->chunk != NULL && add_unmodeled_data(object->chunk, flavor)) {
            /* The writer emits the original bytes while they are set. */
            object->chunk->raw_data = NULL;
            object->chunk->raw_size = 0;
            modified++;
        }
    }
    nmo_object_system_fidelity_stats_t stats = {0};
    if (status == NMO_OK) {
        status = nmo_object_system_capture_fidelity(
            repo, nmo_context_get_type_runtime(ctx), NULL, &stats);
    }
    if (status == NMO_OK) {
        nmo_save_options_t options = nmo_save_options_default();
        status = nmo_session_save_file(session, path, &options, NULL);
    }
    nmo_session_destroy(session);
    if (out_with_residue != NULL) *out_with_residue = stats.with_residue;
    if (status == NMO_OK && modified == 0) status = NMO_ERR_VALIDATION_FAILED;
    if (status != NMO_OK) {
        char detail[512];
        nmo_last_error_message_copy(detail, sizeof(detail));
        printf("  generate_foreign(%s) failed (%d, %zu chunks modified): %s\n", path, status,
               modified, detail);
    }
    return status;
}

static int chunk_contains_word(const nmo_chunk_t *chunk, uint32_t word)
{
    const uint32_t *words = (const uint32_t *)chunk->data.data;
    for (size_t i = 0; i < chunk->data.count; i++) {
        if (words[i] == word) return 1;
    }
    return 0;
}

/* Objects of `path` whose chunk holds `word`. */
static size_t count_chunks_with_word(nmo_context_t *ctx, const char *path, uint32_t word)
{
    nmo_session_t *session = load_session(ctx, path);
    if (session == NULL) return 0;
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    size_t found = 0;
    for (size_t i = 0; i < nmo_object_repository_get_count(repo); i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        found += object != NULL && object->chunk != NULL && chunk_contains_word(object->chunk, word);
    }
    nmo_session_destroy(session);
    return found;
}

static void generate_files(nmo_context_t *ctx, size_t *out_classes, size_t *out_residue)
{
    size_t created = 0;
    ASSERT_EQ(NMO_OK, generate_all_classes(ctx, ALL_CLASSES_FILE, &created));
    ASSERT_GE(created, 45u);
    ASSERT_EQ(NMO_OK, generate_scene(SCENE_FILE));
    size_t residue_trailing = 0, residue_section = 0;
    ASSERT_EQ(NMO_OK, generate_foreign(ctx, ALL_CLASSES_FILE, FOREIGN_TRAILING_FILE,
                                       EXTRA_TRAILING_DWORD, &residue_trailing));
    ASSERT_EQ(NMO_OK, generate_foreign(ctx, ALL_CLASSES_FILE, FOREIGN_SECTION_FILE,
                                       EXTRA_UNKNOWN_SECTION, &residue_section));
    /* The files must hold the data on disk: the checks below compare against it. */
    ASSERT_GE(count_chunks_with_word(ctx, FOREIGN_TRAILING_FILE, EXTRA_DWORD), 30u);
    ASSERT_GE(count_chunks_with_word(ctx, FOREIGN_SECTION_FILE, UNKNOWN_SECTION_PAYLOAD), 30u);
    if (out_classes != NULL) *out_classes = created;
    if (out_residue != NULL) *out_residue = residue_trailing < residue_section ? residue_trailing
                                                                               : residue_section;
}

static void remove_generated_files(void)
{
    /* NMO_KEEP_GENERATED=1 leaves the files for inspection with the nmo tool. */
    if (getenv("NMO_KEEP_GENERATED") != NULL) return;
    remove(ALL_CLASSES_FILE);
    remove(SCENE_FILE);
    remove(FOREIGN_TRAILING_FILE);
    remove(FOREIGN_SECTION_FILE);
    remove(SCRATCH_FILE);
}

static const char *const generated_files[] = {
    ALL_CLASSES_FILE, SCENE_FILE, FOREIGN_TRAILING_FILE, FOREIGN_SECTION_FILE};

static size_t count_distinct_classes(nmo_context_t *ctx, const char *path)
{
    nmo_session_t *session = load_session(ctx, path);
    if (session == NULL) return 0;
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    unsigned char seen[256] = {0};
    size_t distinct = 0;
    for (size_t i = 0; i < nmo_object_repository_get_count(repo); i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object != NULL && object->class_id < sizeof(seen) && !seen[object->class_id]) {
            seen[object->class_id] = 1;
            distinct++;
        }
    }
    nmo_session_destroy(session);
    return distinct;
}

TEST(generated_chunk_roundtrip, the_files_cover_the_classes_and_carry_unmodeled_data)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    size_t classes = 0, residue = 0;
    generate_files(ctx, &classes, &residue);

    const size_t distinct = count_distinct_classes(ctx, ALL_CLASSES_FILE);
    printf("  Generated: %zu classes created, %zu distinct after reload, %zu objects with unmodeled data\n",
           classes, distinct, residue);
    ASSERT_EQ(classes, distinct);
    /* Nearly every class takes the extra data; a drop means the generator stopped working. */
    ASSERT_GE(residue, 30u);
    ASSERT_GE(count_distinct_classes(ctx, SCENE_FILE), 6u);
    remove_generated_files();
    nmo_context_release(ctx);
}

static void run_roundtrip(nmo_context_t *ctx, const char *path, int require_schema,
                          corpus_chunk_stats_t *stats)
{
    stats->ctx = ctx;
    stats->require_schema = require_schema;
    check_file_roundtrip(path, stats);
}

TEST(generated_chunk_roundtrip, every_object_chunk_survives_save_and_reload)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    generate_files(ctx, NULL, NULL);

    /* Required schema serialization drops what the schemas do not model, so the foreign
     * files are not part of this pass. */
    corpus_chunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    run_roundtrip(ctx, ALL_CLASSES_FILE, 1, &stats);
    run_roundtrip(ctx, SCENE_FILE, 1, &stats);
    remove_generated_files();
    nmo_context_release(ctx);

    printf("  Generated round-trip: files=%zu objects=%zu chunk_mismatches=%zu padding_dwords=%zu\n",
           stats.files, stats.objects, stats.chunk_mismatches, stats.padding_fixes);
    ASSERT_EQ(2u, stats.files);
    ASSERT_GE(stats.objects, 50u);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.save_errors);
    ASSERT_EQ(0u, stats.reload_errors);
    ASSERT_EQ(0u, stats.count_mismatches);
    ASSERT_EQ(0u, stats.missing_objects);
    ASSERT_EQ(0u, stats.chunk_mismatches);
}

TEST(generated_chunk_roundtrip, default_save_keeps_untouched_objects_byte_exact)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    generate_files(ctx, NULL, NULL);

    corpus_chunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    for (size_t i = 0; i < sizeof(generated_files) / sizeof(generated_files[0]); i++) {
        run_roundtrip(ctx, generated_files[i], 0, &stats);
    }
    remove_generated_files();
    nmo_context_release(ctx);

    printf("  Generated default save: files=%zu objects=%zu chunk_mismatches=%zu padding_dwords=%zu\n",
           stats.files, stats.objects, stats.chunk_mismatches, stats.padding_fixes);
    ASSERT_EQ(4u, stats.files);
    ASSERT_GE(stats.objects, 150u);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.save_errors);
    ASSERT_EQ(0u, stats.reload_errors);
    ASSERT_EQ(0u, stats.count_mismatches);
    ASSERT_EQ(0u, stats.missing_objects);
    ASSERT_EQ(0u, stats.chunk_mismatches);
    ASSERT_EQ(0u, stats.padding_fixes);
}

TEST(generated_chunk_roundtrip, deleting_an_object_keeps_the_others_intact)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    generate_files(ctx, NULL, NULL);

    /* The scene lists the deleted entity and its child had it as parent. */
    const nmo_class_id_t scene_changes[] = {NMO_CID_SCENE, NMO_CID_3DENTITY};
    check_deletion_keeps_the_others(ctx, SCENE_FILE, NMO_CID_3DENTITY, 6u, scene_changes,
                                    sizeof(scene_changes) / sizeof(scene_changes[0]));
    /* Nothing refers to the objects of all_classes, so nothing may change. */
    check_deletion_keeps_the_others(ctx, FOREIGN_TRAILING_FILE, NMO_CID_GROUP, 40u, NULL, 0u);
    check_deletion_keeps_the_others(ctx, FOREIGN_SECTION_FILE, NMO_CID_GROUP, 40u, NULL, 0u);
    remove_generated_files();
    nmo_context_release(ctx);
}

/* The generated counterpart of test_fidelity_save's material test: an edited object is
 * written from its state, keeps the data the schema does not model, and no other object
 * changes. */
static void check_edit_keeps_unmodeled_data(nmo_context_t *ctx, const char *path,
                                            uint32_t marker)
{
    nmo_session_t *pristine = load_session(ctx, path);
    nmo_session_t *edited = load_session(ctx, path);
    ASSERT_NOT_NULL(pristine);
    ASSERT_NOT_NULL(edited);

    nmo_object_repository_t *repo = nmo_session_get_repository(edited);
    nmo_object_t *material = NULL;
    for (size_t i = 0; i < nmo_object_repository_get_count(repo) && material == NULL; i++) {
        nmo_object_t *candidate = nmo_object_repository_get_by_index(repo, i);
        if (candidate != NULL && candidate->class_id == NMO_CID_MATERIAL) material = candidate;
    }
    ASSERT_NOT_NULL(material);
    ASSERT_NOT_NULL(material->chunk);
    ASSERT_TRUE(chunk_contains_word(material->chunk, marker));
    ASSERT_NOT_NULL(material->fidelity_canonical);
    const uint32_t file_id = material->file_id;
    const size_t chunk_words = material->chunk->data.count;

    nmo_material_state_t *state = (nmo_material_state_t *)nmo_object_get_state(material);
    ASSERT_NOT_NULL(state);
    const float new_power = state->specular_power + 7.25f;
    state->specular_power = new_power;

    nmo_save_options_t options = nmo_save_options_default();
    ASSERT_EQ(NMO_OK, nmo_session_save_file(edited, SCRATCH_FILE, &options, NULL));
    nmo_session_t *reloaded = load_session(ctx, SCRATCH_FILE);
    ASSERT_NOT_NULL(reloaded);

    const nmo_object_t *saved =
        nmo_object_repository_find_by_file_id(nmo_session_get_repository(reloaded), file_id);
    ASSERT_NOT_NULL(saved);
    ASSERT_NOT_NULL(saved->chunk);
    const nmo_material_state_t *saved_state = (const nmo_material_state_t *)nmo_object_get_state(saved);
    ASSERT_NOT_NULL(saved_state);
    ASSERT_TRUE(saved_state->specular_power == new_power);
    ASSERT_TRUE(chunk_contains_word(saved->chunk, marker));
    ASSERT_EQ(chunk_words, saved->chunk->data.count);

    /* Everything else is what it was: the edited material is the one chunk that differs. */
    corpus_chunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    compare_repositories(nmo_session_get_repository(pristine),
                         nmo_session_get_repository(reloaded), path, &stats);
    ASSERT_EQ(1u, stats.chunk_mismatches);
    ASSERT_EQ(0u, stats.missing_objects);
    ASSERT_EQ(0u, stats.count_mismatches);

    remove(SCRATCH_FILE);
    nmo_session_destroy(reloaded);
    nmo_session_destroy(edited);
    nmo_session_destroy(pristine);
}

TEST(generated_chunk_roundtrip, edited_object_keeps_a_trailing_dword_it_does_not_model)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    generate_files(ctx, NULL, NULL);
    check_edit_keeps_unmodeled_data(ctx, FOREIGN_TRAILING_FILE, EXTRA_DWORD);
    remove_generated_files();
    nmo_context_release(ctx);
}

TEST(generated_chunk_roundtrip, edited_object_keeps_a_section_it_does_not_know)
{
    nmo_context_t *ctx = make_context();
    ASSERT_NOT_NULL(ctx);
    generate_files(ctx, NULL, NULL);
    check_edit_keeps_unmodeled_data(ctx, FOREIGN_SECTION_FILE, UNKNOWN_SECTION_PAYLOAD);
    remove_generated_files();
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(generated_chunk_roundtrip, the_files_cover_the_classes_and_carry_unmodeled_data);
    REGISTER_TEST(generated_chunk_roundtrip, every_object_chunk_survives_save_and_reload);
    REGISTER_TEST(generated_chunk_roundtrip, default_save_keeps_untouched_objects_byte_exact);
    REGISTER_TEST(generated_chunk_roundtrip, deleting_an_object_keeps_the_others_intact);
    REGISTER_TEST(generated_chunk_roundtrip, edited_object_keeps_a_trailing_dword_it_does_not_model);
    REGISTER_TEST(generated_chunk_roundtrip, edited_object_keeps_a_section_it_does_not_know);
TEST_MAIN_END()
