/**
 * @file test_corpus_state_copy.c
 * @brief The copy, equals and hash hooks of every class agree with real object states
 *
 * The unit tests build the states of the classes by hand, so they only cover the
 * members and the shapes their author thought of. The sample files hold the states
 * the engine wrote: strings, chunks, nested record arrays, skins, patch channels.
 * For every object of every file this test copies the state in the two ways the
 * library does (into a zeroed state, as the runtime copy does, and into a created one),
 * and checks that
 *
 *  - the copy validates, and equals and hashes like the original;
 *  - the copy serializes to the dwords the original serializes to;
 *  - the copy owns its memory: it is destroyed first and the original is destroyed
 *    afterwards, so a member the copy shared with the original shows up as a double
 *    free or a use after free under the sanitizers.
 */

#include "../test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mismatches beyond this many are counted but not printed. */
#define MAX_REPORTED_MISMATCHES 20

typedef struct copy_stats {
    nmo_context_t *ctx;
    size_t files;
    size_t objects;
    size_t checked;
    size_t no_hooks;
    size_t load_errors;
    size_t copy_errors;
    size_t validate_errors;
    size_t equality_mismatches;
    size_t hash_mismatches;
    size_t serialize_mismatches;
    size_t not_serializable;
} copy_stats_t;

static void report(copy_stats_t *stats, size_t *counter, const char *path,
                   const nmo_object_t *object, const char *what)
{
    if ((*counter)++ < MAX_REPORTED_MISMATCHES) {
        printf("  %s: object file id %u (class 0x%08X): %s\n", path,
               (unsigned)object->file_id, (unsigned)object->class_id, what);
    }
    (void)stats;
}

/* The dwords the hook writes for a state, or NMO_OK != 0 when it cannot write it. */
static int serialize_words(const nmo_type_descriptor_t *type, const void *state,
                           nmo_arena_t *arena, nmo_object_repository_t *repo,
                           const uint32_t **out_words, size_t *out_count)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) return NMO_ERR_NOMEM;
    nmo_chunk_start_write(chunk);
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_serialize_context_t context =
        nmo_serialize_context_create(arena, repo, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    int status = type->vtable->serialize(state, chunk, type, &context);
    if (status != NMO_OK) return status;
    nmo_chunk_close(chunk);
    *out_words = (const uint32_t *)chunk->data.data;
    *out_count = chunk->data.count;
    return NMO_OK;
}

static int same_words(const uint32_t *a, size_t a_count, const uint32_t *b, size_t b_count)
{
    return a_count == b_count && (a_count == 0 || memcmp(a, b, a_count * sizeof(uint32_t)) == 0);
}

static void check_copy(copy_stats_t *stats, const char *path, const nmo_object_t *object,
                       const nmo_type_descriptor_t *type, nmo_object_repository_t *repo,
                       int into_created_state)
{
    const void *state = nmo_object_get_state(object);
    const uint32_t size = nmo_object_get_state_size(object);
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    void *copy = calloc(1, size);
    if (arena == NULL || copy == NULL) {
        stats->copy_errors++;
        goto done;
    }

    if (into_created_state &&
        type->vtable->create(copy, type, NULL) != NMO_OK) {
        report(stats, &stats->copy_errors, path, object, "create failed");
        goto done;
    }
    int status = type->vtable->copy(state, copy, type, arena);
    if (status != NMO_OK) {
        report(stats, &stats->copy_errors, path, object, "copy failed");
        if (into_created_state) type->vtable->destroy(copy, type, NULL);
        goto done;
    }

    if (type->vtable->validate(copy, type, NULL) != NMO_OK) {
        report(stats, &stats->validate_errors, path, object, "the copy does not validate");
    }
    if (!type->vtable->equals(state, copy)) {
        report(stats, &stats->equality_mismatches, path, object,
               "the copy is not equal to the original");
    }
    if (type->vtable->hash(state) != type->vtable->hash(copy)) {
        report(stats, &stats->hash_mismatches, path, object,
               "the copy hashes differently from the original");
    }

    const uint32_t *original_words = NULL;
    const uint32_t *copied_words = NULL;
    size_t original_count = 0;
    size_t copied_count = 0;
    if (serialize_words(type, state, arena, repo, &original_words, &original_count) != NMO_OK) {
        stats->not_serializable++;
    } else if (serialize_words(type, copy, arena, repo, &copied_words, &copied_count) != NMO_OK ||
               !same_words(original_words, original_count, copied_words, copied_count)) {
        report(stats, &stats->serialize_mismatches, path, object,
               "the copy serializes differently from the original");
    }

    type->vtable->destroy(copy, type, NULL);
    stats->checked++;
done:
    free(copy);
    nmo_arena_destroy(arena);
}

static void check_file(const char *path, void *user)
{
    copy_stats_t *stats = (copy_stats_t *)user;
    stats->files++;
    nmo_session_t *session = nmo_session_create(stats->ctx);
    if (session == NULL || nmo_session_load_file(session, path, NULL, NULL) != NMO_OK) {
        stats->load_errors++;
        printf("  %s: load failed\n", path);
        nmo_session_destroy(session);
        return;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(stats->ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    const size_t count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL || nmo_object_get_state(object) == NULL ||
            nmo_object_get_state_size(object) == 0) {
            continue;
        }
        stats->objects++;
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_class_id_inherited(registry, object->class_id);
        if (type == NULL || type->vtable == NULL || type->vtable->copy == NULL ||
            type->vtable->create == NULL || type->vtable->destroy == NULL ||
            type->vtable->validate == NULL || type->vtable->equals == NULL ||
            type->vtable->hash == NULL || type->vtable->serialize == NULL) {
            stats->no_hooks++;
            continue;
        }
        check_copy(stats, path, object, type, repo, 0);
        check_copy(stats, path, object, type, repo, 1);
    }
    nmo_session_destroy(session);
}

TEST(corpus_state_copy, every_object_state_copies_like_its_original)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    copy_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = ctx;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file, &stats);
    nmo_context_release(ctx);

    printf("  State copy corpus: files=%zu objects=%zu checked=%zu no_hooks=%zu "
           "not_serializable=%zu load_errors=%zu copy_errors=%zu validate_errors=%zu "
           "equality_mismatches=%zu hash_mismatches=%zu serialize_mismatches=%zu\n",
           stats.files, stats.objects, stats.checked, stats.no_hooks, stats.not_serializable,
           stats.load_errors, stats.copy_errors, stats.validate_errors,
           stats.equality_mismatches, stats.hash_mismatches, stats.serialize_mismatches);

    ASSERT_EQ(0, walk_status);
    ASSERT_GE(stats.files, 1u);
    ASSERT_GE(stats.checked, 1u);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.copy_errors);
    ASSERT_EQ(0u, stats.validate_errors);
    ASSERT_EQ(0u, stats.equality_mismatches);
    ASSERT_EQ(0u, stats.hash_mismatches);
    ASSERT_EQ(0u, stats.serialize_mismatches);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(corpus_state_copy, every_object_state_copies_like_its_original);
TEST_MAIN_END()
