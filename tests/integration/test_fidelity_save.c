/**
 * @file test_fidelity_save.c
 * @brief An edited object keeps the data its schema does not model
 *
 * A chunk can hold more than the schema writes: trailing dwords in a section
 * (the engine ignores them on load) or whole sections of a newer version. When
 * only the state of such an object is edited, saving must write the new state
 * and carry that extra data along instead of dropping it.
 */

#include "../test_framework.h"

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_residue.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "session/nmo_runtime_kernel.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_system.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH_FILE "fidelity_save.tmp"
#define EXTRA_DWORD 0xC0FFEE42u

static nmo_session_t *load_session(nmo_context_t *ctx, const char *path)
{
    nmo_session_t *session = nmo_session_create(ctx);
    if (session != NULL && nmo_session_load_file(session, path, NULL, NULL) != NMO_OK) {
        nmo_session_destroy(session);
        return NULL;
    }
    return session;
}

/* Position of the identifier that starts the last section of the chain. */
static size_t last_section_start(const nmo_chunk_t *chunk)
{
    const uint32_t *d = (const uint32_t *)chunk->data.data;
    size_t pos = 0;
    while (pos + 1u < chunk->data.count && d[pos + 1u] != 0u && d[pos + 1u] > pos &&
           d[pos + 1u] < chunk->data.count) {
        pos = d[pos + 1u];
    }
    return pos;
}

/* Makes the last section of the chunk one dword longer. */
static int append_extra_dword(nmo_chunk_t *chunk, size_t *out_position)
{
    const size_t start = last_section_start(chunk);
    const size_t old_count = chunk->data.count;
    if (nmo_arena_array_resize(&chunk->data, old_count + 1u) != NMO_OK) return 0;
    uint32_t *d = (uint32_t *)chunk->data.data;
    d[old_count] = EXTRA_DWORD;
    (void)start;
    *out_position = old_count;
    return 1;
}

TEST(fidelity_save, edited_material_keeps_trailing_dwords)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    char path[1024];
    snprintf(path, sizeof(path), "%s/Ballance/base.cmo", NMO_TEST_DATA_DIR);
    nmo_session_t *session = load_session(ctx, path);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    /* The first material with a name no other object of its class shares. */
    nmo_object_t *material = NULL;
    size_t count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < count && material == NULL; i++) {
        nmo_object_t *candidate = nmo_object_repository_get_by_index(repo, i);
        if (candidate == NULL || candidate->class_id != NMO_CID_MATERIAL ||
            candidate->chunk == NULL || candidate->name == NULL ||
            nmo_object_get_state(candidate) == NULL) {
            continue;
        }
        int unique = 1;
        for (size_t j = 0; j < count && unique; j++) {
            const nmo_object_t *other = nmo_object_repository_get_by_index(repo, j);
            if (other != candidate && other != NULL && other->class_id == NMO_CID_MATERIAL &&
                other->name != NULL && strcmp(other->name, candidate->name) == 0) {
                unique = 0;
            }
        }
        if (unique) material = candidate;
    }
    ASSERT_NOT_NULL(material);
    char name[256];
    snprintf(name, sizeof(name), "%s", material->name);

    /* Give the chunk a dword the schema does not know and capture it again,
     * as if the file had come with it. */
    size_t extra_position = 0;
    ASSERT_TRUE(append_extra_dword(material->chunk, &extra_position));
    const size_t mutated_count = material->chunk->data.count;
    nmo_object_system_fidelity_stats_t stats = {0};
    ASSERT_EQ(NMO_OK, nmo_object_system_capture_fidelity(
                          repo, nmo_context_get_type_runtime(ctx), NULL, &stats));
    ASSERT_NOT_NULL(material->fidelity_canonical);

    /* Edit the material. */
    nmo_material_state_t *state = (nmo_material_state_t *)nmo_object_get_state(material);
    const float new_power = state->specular_power + 7.25f;
    state->specular_power = new_power;

    nmo_save_options_t options = nmo_save_options_default();
    ASSERT_EQ(NMO_OK, nmo_session_save_file(session, SCRATCH_FILE, &options, NULL));

    nmo_session_t *reloaded = load_session(ctx, SCRATCH_FILE);
    ASSERT_NOT_NULL(reloaded);
    nmo_object_repository_t *after = nmo_session_get_repository(reloaded);
    const nmo_object_t *saved = NULL;
    for (size_t i = 0; i < nmo_object_repository_get_count(after) && saved == NULL; i++) {
        const nmo_object_t *candidate = nmo_object_repository_get_by_index(after, i);
        if (candidate != NULL && candidate->class_id == NMO_CID_MATERIAL &&
            candidate->name != NULL && strcmp(candidate->name, name) == 0) {
            saved = candidate;
        }
    }
    ASSERT_NOT_NULL(saved);
    ASSERT_NOT_NULL(saved->chunk);

    /* The new value was written ... */
    const nmo_material_state_t *saved_state =
        (const nmo_material_state_t *)nmo_object_get_state(saved);
    ASSERT_NOT_NULL(saved_state);
    ASSERT_TRUE(saved_state->specular_power == new_power);

    /* ... and so was the dword nobody understands. */
    ASSERT_EQ(mutated_count, saved->chunk->data.count);
    const uint32_t *words = (const uint32_t *)saved->chunk->data.data;
    int found = 0;
    for (size_t i = 0; i < saved->chunk->data.count; i++) {
        if (words[i] == EXTRA_DWORD) found = 1;
    }
    ASSERT_TRUE(found);

    remove(SCRATCH_FILE);
    nmo_session_destroy(reloaded);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    (void)extra_position;
}

/* Names of the objects the level scene lists, in order. */
static size_t level_scene_names(nmo_object_repository_t *repo, char names[][64], size_t capacity,
                                nmo_object_id_t *out_members, size_t *out_member_count)
{
    size_t found = 0;
    for (size_t i = 0; i < nmo_object_repository_get_count(repo); i++) {
        const nmo_object_t *level = nmo_object_repository_get_by_index(repo, i);
        if (level == NULL || level->class_id != NMO_CID_LEVEL) continue;
        const nmo_level_state_t *state = (const nmo_level_state_t *)nmo_object_get_state(level);
        if (state == NULL || state->level_scene_chunk == NULL) return 0;
        if (state->level_scene_chunk == NULL) return 0;
        const uint32_t *data = (const uint32_t *)state->level_scene_chunk->data.data;
        for (uint32_t k = 0; k < state->level_scene_id_count && found < capacity; k++) {
            const nmo_object_id_t id = data[state->level_scene_id_positions[k]];
            const nmo_object_t *member = nmo_object_repository_find_by_id(repo, id);
            snprintf(names[found], 64, "%s", member && member->name ? member->name : "?");
            if (out_members) out_members[found] = id;
            found++;
        }
        if (out_member_count) *out_member_count = found;
        break;
    }
    return found;
}

TEST(fidelity_save, level_scene_follows_the_objects_when_indices_change)
{
    TEST_REQUIRE_FIXTURE("Demo/Tunnel.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    char path[1024];
    snprintf(path, sizeof(path), "%s/Demo/Tunnel.cmo", NMO_TEST_DATA_DIR);
    nmo_session_t *session = load_session(ctx, path);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    static char before[512][64];
    static char after[512][64];
    nmo_object_id_t members[512];
    size_t member_count = 0;
    const size_t before_count = level_scene_names(repo, before, 512, members, &member_count);
    ASSERT_GE(before_count, 10u);

    /* Delete an early object that the level scene does not list. */
    nmo_object_id_t victim = 0;
    for (size_t i = 2; i < 200 && victim == 0; i++) {
        const nmo_object_t *candidate = nmo_object_repository_get_by_index(repo, i);
        if (candidate == NULL || candidate->class_id == NMO_CID_LEVEL) continue;
        int listed = 0;
        for (size_t m = 0; m < member_count; m++) {
            if (members[m] == candidate->id) listed = 1;
        }
        if (!listed) victim = candidate->id;
    }
    ASSERT_NE(0u, victim);
    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_DELETE;
    request.flags = NMO_RUNTIME_REQUEST_SAFE_DETACH;
    request.payload.destroy.ids = &victim;
    request.payload.destroy.count = 1;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    ASSERT_EQ(NMO_OK, nmo_session_execute(session, &request, &report));

    nmo_save_options_t options = nmo_save_options_default();
    ASSERT_EQ(NMO_OK, nmo_session_save_file(session, SCRATCH_FILE, &options, NULL));
    nmo_session_t *reloaded = load_session(ctx, SCRATCH_FILE);
    ASSERT_NOT_NULL(reloaded);
    const size_t after_count =
        level_scene_names(nmo_session_get_repository(reloaded), after, 512, NULL, NULL);
    if (before_count != after_count) {
        for (size_t i = 0; i < before_count; i++) printf("  b[%zu]=%s a=%s\n", i, before[i], i < after_count ? after[i] : "-");
    }
    ASSERT_EQ(before_count, after_count);
    for (size_t i = 0; i < before_count; i++) {
        ASSERT_STR_EQ(before[i], after[i]);
    }

    remove(SCRATCH_FILE);
    nmo_session_destroy(reloaded);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* A save that does not use the original chunks replaces them with its own; the
 * next save must not take those for the ones the object was loaded with. */
TEST(fidelity_save, full_save_after_a_strip_save_is_unchanged)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    char path[1024];
    snprintf(path, sizeof(path), "%s/Ballance/base.cmo", NMO_TEST_DATA_DIR);
    nmo_session_t *plain = load_session(ctx, path);
    nmo_session_t *subset = load_session(ctx, path);
    ASSERT_NOT_NULL(plain);
    ASSERT_NOT_NULL(subset);

    /* A save that turns the fidelity mechanism off still replaces the chunks
     * (here with the same flag the tools use to strip included files). */
    nmo_save_options_t partial = nmo_save_options_default();
    partial.flags |= NMO_SAVE_STRIP_INCLUDED_FILES;
    ASSERT_EQ(NMO_OK, nmo_session_save_file(subset, SCRATCH_FILE, &partial, NULL));

    nmo_save_options_t full = nmo_save_options_default();
    ASSERT_EQ(NMO_OK, nmo_session_save_file(plain, "fidelity_plain.tmp", &full, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_save_file(subset, SCRATCH_FILE, &full, NULL));

    nmo_session_t *a = load_session(ctx, "fidelity_plain.tmp");
    nmo_session_t *b = load_session(ctx, SCRATCH_FILE);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    nmo_object_repository_t *ra = nmo_session_get_repository(a);
    nmo_object_repository_t *rb = nmo_session_get_repository(b);
    ASSERT_EQ(nmo_object_repository_get_count(ra), nmo_object_repository_get_count(rb));
    size_t different = 0;
    for (size_t i = 0; i < nmo_object_repository_get_count(ra); i++) {
        const nmo_chunk_t *x = nmo_object_repository_get_by_index(ra, i)->chunk;
        const nmo_chunk_t *y = nmo_object_repository_get_by_index(rb, i)->chunk;
        if (x == NULL || y == NULL) continue;
        /* Padding Virtools leaves behind strings may be lost on the way. */
        if (!nmo_chunk_equivalent(x, y)) {
            different++;
            const uint32_t *px = x->data.data, *py = y->data.data;
            for (size_t k = 0, shown = 0; k < x->data.count && k < y->data.count && shown < 6; k++)
                if (px[k] != py[k]) { printf("    pos %zu %08x %08x\n", k, px[k], py[k]); shown++; }
            printf("  object %zu class %u differs (%zu vs %zu dwords)\n", i, (unsigned)nmo_object_repository_get_by_index(ra, i)->class_id, x->data.count, y->data.count);
        }
    }
    ASSERT_EQ(0u, different);

    remove(SCRATCH_FILE);
    remove("fidelity_plain.tmp");
    nmo_session_destroy(a);
    nmo_session_destroy(b);
    nmo_session_destroy(plain);
    nmo_session_destroy(subset);
    nmo_context_release(ctx);
}

/* The schema refuses to write geometry the engine loads without complaint. The
 * chunk the object was loaded with still is writable. */
TEST(fidelity_save, state_the_schema_refuses_keeps_its_loaded_chunk)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    char path[1024];
    snprintf(path, sizeof(path), "%s/Ballance/base.cmo", NMO_TEST_DATA_DIR);
    nmo_session_t *session = load_session(ctx, path);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_t *mesh = NULL;
    for (size_t i = 0; i < nmo_object_repository_get_count(repo) && mesh == NULL; i++) {
        nmo_object_t *candidate = nmo_object_repository_get_by_index(repo, i);
        if (candidate != NULL && candidate->class_id == NMO_CID_MESH && candidate->chunk != NULL) {
            const nmo_mesh_state_t *state = (const nmo_mesh_state_t *)nmo_object_get_state(candidate);
            if (state != NULL && state->face_count > 0 && state->face_vertex_indices != NULL) {
                mesh = candidate;
            }
        }
    }
    ASSERT_NOT_NULL(mesh);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)nmo_object_get_state(mesh);
    state->face_vertex_indices[0] = 0xFFFFu;   /* beyond the vertices */

    /* The file as the engine would load it, captured again with that state. */
    nmo_object_system_fidelity_stats_t stats = {0};
    ASSERT_EQ(NMO_OK, nmo_object_system_capture_fidelity(
                          repo, nmo_context_get_type_runtime(ctx), NULL, &stats));
    ASSERT_TRUE(mesh->fidelity_unserializable);

    nmo_save_options_t options = nmo_save_options_default();
    ASSERT_EQ(NMO_OK, nmo_session_save_file(session, SCRATCH_FILE, &options, NULL));
    nmo_session_t *reloaded = load_session(ctx, SCRATCH_FILE);
    ASSERT_NOT_NULL(reloaded);
    remove(SCRATCH_FILE);
    nmo_session_destroy(reloaded);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(fidelity_save, state_the_schema_refuses_keeps_its_loaded_chunk);
    REGISTER_TEST(fidelity_save, full_save_after_a_strip_save_is_unchanged);
    REGISTER_TEST(fidelity_save, level_scene_follows_the_objects_when_indices_change);
    REGISTER_TEST(fidelity_save, edited_material_keeps_trailing_dwords);
TEST_MAIN_END()
