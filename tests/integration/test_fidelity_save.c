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
#include "object/builtin/nmo_material_schemas.h"
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

TEST_MAIN_BEGIN()
    REGISTER_TEST(fidelity_save, edited_material_keeps_trailing_dwords);
TEST_MAIN_END()
