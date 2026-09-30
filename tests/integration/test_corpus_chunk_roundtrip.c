/**
 * @file test_corpus_chunk_roundtrip.c
 * @brief Every object chunk of the reference corpus survives save and reload
 *
 * The document-level round-trip tests compare two loads of the same writer,
 * so they cannot tell a writer that mirrors a wrong reader from a correct
 * one. This test compares bytes instead: it loads a file, saves it with
 * schema serialization required (no raw chunk reuse), reloads the result and
 * checks that each object's chunk payload, identifier lists and sub-chunks
 * are identical to the chunk that came out of the original file.
 *
 * A failure means a schema drops, reorders or rewrites data that the file
 * contained. It says nothing about whether fields are interpreted correctly;
 * that needs a reference such as the Virtools engine binaries.
 */

#include "../test_framework.h"

#include "format/nmo_chunk.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_serializer.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mismatches beyond this many are counted but not printed. */
#define MAX_REPORTED_MISMATCHES 20

#define SCRATCH_FILE "corpus_chunk_roundtrip.tmp"

typedef struct corpus_chunk_stats {
    nmo_context_t *ctx;
    size_t files;
    size_t objects;
    size_t load_errors;
    size_t save_errors;
    size_t reload_errors;
    size_t count_mismatches;
    size_t missing_objects;
    size_t chunk_mismatches;
    size_t padding_fixes; /* padding dwords that were normalized */
    size_t known_gap_objects; /* objects left out, see known_gaps */
} corpus_chunk_stats_t;

/* Strings and byte buffers are stored padded to whole dwords, and Virtools
 * leaves whatever was in memory in the padding (0xFEEDFA00 behind a string's
 * NUL, stray bytes behind an embedded image). Reading drops those bytes and
 * writing pads with zeros, so a dword whose upper bytes were cleared is the
 * same content. */
static int same_padding(uint32_t original, uint32_t saved)
{
    for (unsigned kept_bytes = 0; kept_bytes < 4; kept_bytes++) {
        uint32_t kept = kept_bytes == 0 ? 0u : (0xFFFFFFFFu >> (32 - 8 * kept_bytes));
        if ((original & kept) == saved) {
            return 1;
        }
    }
    return 0;
}

static int payloads_equal(const nmo_arena_array_t *a, const nmo_arena_array_t *b,
                          size_t *padding_fixes)
{
    if (a->count != b->count) {
        return 0;
    }
    const uint32_t *words_a = (const uint32_t *)a->data;
    const uint32_t *words_b = (const uint32_t *)b->data;
    for (size_t i = 0; i < a->count; i++) {
        if (words_a[i] == words_b[i]) {
            continue;
        }
        if (!same_padding(words_a[i], words_b[i])) {
            return 0;
        }
        (*padding_fixes)++;
    }
    return 1;
}

static int arrays_equal(const nmo_arena_array_t *a, const nmo_arena_array_t *b,
                        size_t element_size)
{
    if (a->count != b->count) {
        return 0;
    }
    return a->count == 0 || memcmp(a->data, b->data, a->count * element_size) == 0;
}

/* Returns the name of the first part of the chunks that differs, or NULL.
 * *nested is set when the difference lies in a sub-chunk. */
static const char *chunk_first_difference(const nmo_chunk_t *a, const nmo_chunk_t *b,
                                          int *nested, size_t *padding_fixes)
{
    if (a->data_version != b->data_version || a->chunk_version != b->chunk_version) {
        return "version";
    }
    if (!payloads_equal(&a->data, &b->data, padding_fixes)) {
        return "payload";
    }
    if (!arrays_equal(&a->ids, &b->ids, sizeof(uint32_t))) {
        return "object id list";
    }
    if (!arrays_equal(&a->chunk_refs, &b->chunk_refs, sizeof(uint32_t))) {
        return "sub-chunk offsets";
    }
    if (!arrays_equal(&a->managers, &b->managers, sizeof(uint32_t))) {
        return "manager list";
    }
    if (a->chunks.count != b->chunks.count) {
        return "sub-chunk count";
    }

    nmo_chunk_t *const *subs_a = (nmo_chunk_t *const *)a->chunks.data;
    nmo_chunk_t *const *subs_b = (nmo_chunk_t *const *)b->chunks.data;
    for (size_t i = 0; i < a->chunks.count; i++) {
        const char *difference = chunk_first_difference(subs_a[i], subs_b[i], nested, padding_fixes);
        if (difference != NULL) {
            *nested = 1;
            return difference;
        }
    }
    return NULL;
}

/* Identifier sections that files of other engine versions carry and that no
 * schema models, so the section is not written back. The engine at hand
 * never writes them (CKPatchMesh::Save writes 0x8000000 only, CKSprite::Save
 * writes 0x20000, 0x10000 and 0x20000000 besides the bitmap data), so there
 * is nothing to check the layout against. Objects holding one are counted and
 * left out of the comparison. */
typedef struct known_gap {
    uint32_t class_id;
    uint32_t identifier;
} known_gap_t;

static const known_gap_t known_gaps[] = {
    { NMO_CID_PATCHMESH, 0x00008000u },
    { NMO_CID_SPRITE, 0x40000000u },
};

/* Sections of a chunk form a chain: [identifier][offset of the next one]. */
static int chunk_has_identifier(const nmo_chunk_t *chunk, uint32_t identifier)
{
    const uint32_t *words = (const uint32_t *)chunk->data.data;
    size_t count = chunk->data.count;
    size_t pos = 0;
    while (pos + 1 < count) {
        if (words[pos] == identifier) {
            return 1;
        }
        size_t next = words[pos + 1];
        if (next <= pos) {
            break;
        }
        pos = next;
    }
    return 0;
}

static int has_known_gap(const nmo_object_t *object)
{
    for (size_t i = 0; i < sizeof(known_gaps) / sizeof(known_gaps[0]); i++) {
        if (object->class_id == known_gaps[i].class_id &&
            chunk_has_identifier(object->chunk, known_gaps[i].identifier)) {
            return 1;
        }
    }
    return 0;
}

static void compare_repositories(
    nmo_object_repository_t *before,
    nmo_object_repository_t *after,
    const char *path,
    corpus_chunk_stats_t *stats)
{
    size_t count = nmo_object_repository_get_count(before);
    if (count != nmo_object_repository_get_count(after)) {
        stats->count_mismatches++;
        printf("  %s: %zu objects before save, %zu after reload\n", path, count,
               nmo_object_repository_get_count(after));
    }

    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *original = nmo_object_repository_get_by_index(before, i);
        if (original == NULL || original->chunk == NULL) {
            continue;
        }
        stats->objects++;
        if (has_known_gap(original)) {
            stats->known_gap_objects++;
            continue;
        }

        const nmo_object_t *saved =
            nmo_object_repository_find_by_file_id(after, original->file_id);
        if (saved == NULL || saved->chunk == NULL) {
            if (stats->missing_objects++ < MAX_REPORTED_MISMATCHES) {
                printf("  %s: object file id %u (class 0x%08X) missing after reload\n",
                       path, (unsigned)original->file_id, (unsigned)original->class_id);
            }
            continue;
        }

        int nested = 0;
        const char *difference =
            chunk_first_difference(original->chunk, saved->chunk, &nested, &stats->padding_fixes);
        if (difference != NULL && stats->chunk_mismatches++ < MAX_REPORTED_MISMATCHES) {
            printf("  %s: object file id %u (class 0x%08X): %s%s differs\n", path,
                   (unsigned)original->file_id, (unsigned)original->class_id,
                   nested ? "sub-chunk " : "", difference);
        }
    }
}

static nmo_session_t *load_session(nmo_context_t *ctx, const char *path)
{
    nmo_session_t *session = nmo_session_create(ctx);
    if (session != NULL && nmo_session_load_file(session, path, NULL, NULL) != NMO_OK) {
        nmo_session_destroy(session);
        return NULL;
    }
    return session;
}

static void check_file_roundtrip(const char *path, void *user)
{
    corpus_chunk_stats_t *stats = (corpus_chunk_stats_t *)user;
    stats->files++;

    /* Saving rewrites parts of the chunks of the session it saves, so the
     * reference is a second load of the original file that is never saved. */
    nmo_session_t *pristine = load_session(stats->ctx, path);
    nmo_session_t *saved = load_session(stats->ctx, path);
    nmo_session_t *reloaded = NULL;
    if (pristine == NULL || saved == NULL) {
        stats->load_errors++;
        printf("  %s: load failed\n", path);
        goto done;
    }

    nmo_save_options_t save_options = nmo_save_options_default();
    save_options.flags |= NMO_SAVE_REQUIRE_SCHEMA;
    if (nmo_session_save_file(saved, SCRATCH_FILE, &save_options, NULL) != NMO_OK) {
        stats->save_errors++;
        printf("  %s: save with required schema serialization failed\n", path);
        goto done;
    }

    reloaded = load_session(stats->ctx, SCRATCH_FILE);
    if (reloaded == NULL) {
        stats->reload_errors++;
        printf("  %s: reload of the saved file failed\n", path);
        goto done;
    }

    compare_repositories(nmo_session_get_repository(pristine),
                         nmo_session_get_repository(reloaded), path, stats);

done:
    nmo_session_destroy(reloaded);
    nmo_session_destroy(saved);
    nmo_session_destroy(pristine);
}

TEST(corpus_chunk_roundtrip, every_object_chunk_survives_save_and_reload)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_chunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = ctx;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file_roundtrip, &stats);
    remove(SCRATCH_FILE);
    nmo_context_release(ctx);

    printf("  Chunk round-trip corpus: files=%zu objects=%zu load_errors=%zu "
           "save_errors=%zu reload_errors=%zu count_mismatches=%zu "
           "missing=%zu chunk_mismatches=%zu padding_dwords=%zu known_gaps=%zu\n",
           stats.files, stats.objects, stats.load_errors, stats.save_errors,
           stats.reload_errors, stats.count_mismatches, stats.missing_objects,
           stats.chunk_mismatches, stats.padding_fixes, stats.known_gap_objects);

    ASSERT_EQ(0, walk_status);
    ASSERT_GE(stats.files, 1u);
    ASSERT_GE(stats.objects, 1u);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.save_errors);
    ASSERT_EQ(0u, stats.reload_errors);
    ASSERT_EQ(0u, stats.count_mismatches);
    ASSERT_EQ(0u, stats.missing_objects);
    ASSERT_EQ(0u, stats.chunk_mismatches);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(corpus_chunk_roundtrip, every_object_chunk_survives_save_and_reload);
TEST_MAIN_END()
