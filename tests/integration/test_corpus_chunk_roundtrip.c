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
#include "format/nmo_chunk_residue.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_serializer.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mismatches beyond this many are counted but not printed. */
#define MAX_REPORTED_MISMATCHES 20

#define SCRATCH_FILE "corpus_chunk_roundtrip.tmp"

typedef struct corpus_chunk_stats {
    nmo_context_t *ctx;
    int require_schema; /* 1: re-serialize every object; 0: default save */
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
    for (unsigned kept_bytes = 1; kept_bytes < 4; kept_bytes++) {
        uint32_t kept = 0xFFFFFFFFu >> (32 - 8 * kept_bytes);
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

/* Identifier sections that no schema models, so the section is not written
 * back and the objects holding one are counted and left out of the comparison.
 *
 * CKPatchMesh 0x8000 is written by RCKMesh::Save for a patch mesh whose render
 * mesh has been built: a face count and packed face masks, all zero in the
 * corpus. Only RCKMesh::Load reads it, and a patch mesh has no faces at that
 * point, so the engine ignores it and BuildRenderMesh recreates the faces. It
 * is derived data; libnmo skips it on load and cannot re-emit it. */
typedef struct known_gap {
    uint32_t class_id;
    uint32_t identifier;
} known_gap_t;

static const known_gap_t known_gaps[] = {
    { NMO_CID_PATCHMESH, 0x00008000u },
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
    if (stats->require_schema) {
        save_options.flags |= NMO_SAVE_REQUIRE_SCHEMA;
    }
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
    stats.require_schema = 1;
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

/* A default save keeps the chunk of an object nobody touched, so not even the
 * padding Virtools leaves behind strings and buffers may change. */
TEST(corpus_chunk_roundtrip, default_save_keeps_untouched_objects_byte_exact)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_chunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = ctx;
    stats.require_schema = 0;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file_roundtrip, &stats);
    remove(SCRATCH_FILE);
    nmo_context_release(ctx);

    printf("  Default-save corpus: files=%zu objects=%zu chunk_mismatches=%zu "
           "padding_dwords=%zu\n", stats.files, stats.objects, stats.chunk_mismatches,
           stats.padding_fixes);

    ASSERT_EQ(0, walk_status);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.save_errors);
    ASSERT_EQ(0u, stats.reload_errors);
    ASSERT_EQ(0u, stats.count_mismatches);
    ASSERT_EQ(0u, stats.missing_objects);
    ASSERT_EQ(0u, stats.chunk_mismatches);
    ASSERT_EQ(0u, stats.padding_fixes);
}

static nmo_object_repository_t *sort_repository;

static int compare_by_class_name_index(const void *lhs, const void *rhs)
{
    size_t a = *(const size_t *)lhs;
    size_t b = *(const size_t *)rhs;
    const nmo_object_t *object_a = nmo_object_repository_get_by_index(sort_repository, a);
    const nmo_object_t *object_b = nmo_object_repository_get_by_index(sort_repository, b);
    if (object_a->class_id != object_b->class_id) {
        return object_a->class_id < object_b->class_id ? -1 : 1;
    }
    int by_name = strcmp(object_a->name ? object_a->name : "", object_b->name ? object_b->name : "");
    if (by_name != 0) return by_name;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* Deleting an object moves the ones after it; the others keep their data. */
TEST(corpus_chunk_roundtrip, deleting_an_object_keeps_the_others_intact)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    char path[1024];
    snprintf(path, sizeof(path), "%s/Ballance/base.cmo", NMO_TEST_DATA_DIR);
    nmo_session_t *pristine = load_session(ctx, path);
    nmo_session_t *edited = load_session(ctx, path);
    ASSERT_NOT_NULL(pristine);
    ASSERT_NOT_NULL(edited);

    nmo_object_repository_t *repo = nmo_session_get_repository(edited);
    size_t count = nmo_object_repository_get_count(repo);
    ASSERT_GE(count, 100u);

    /* A data array from the middle of the file, deleted the way the tools
     * delete objects (references to it are detached). */
    size_t victim_index = 0;
    nmo_object_id_t victim_id = 0;
    for (size_t i = count / 2; i < count; i++) {
        const nmo_object_t *candidate = nmo_object_repository_get_by_index(repo, i);
        if (candidate != NULL && candidate->class_id == NMO_CID_DATAARRAY) {
            victim_index = i;
            victim_id = candidate->id;
            break;
        }
    }
    ASSERT_NE(0u, victim_index);
    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_DELETE;
    request.flags = NMO_RUNTIME_REQUEST_SAFE_DETACH;
    request.payload.destroy.ids = &victim_id;
    request.payload.destroy.count = 1;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    ASSERT_EQ(NMO_OK, nmo_session_execute(edited, &request, &report));
    ASSERT_EQ(count - 1u, nmo_object_repository_get_count(repo));

    nmo_save_options_t save_options = nmo_save_options_default();
    nmo_status_t save_status = nmo_session_save_file(edited, SCRATCH_FILE, &save_options, NULL);
    if (save_status != NMO_OK) {
        char detail[512];
        nmo_last_error_message_copy(detail, sizeof(detail));
        printf("  save failed (%d): %s\n", (int)save_status, detail);
    }
    ASSERT_EQ(NMO_OK, save_status);
    nmo_session_t *reloaded = load_session(ctx, SCRATCH_FILE);
    ASSERT_NOT_NULL(reloaded);

    nmo_object_repository_t *before = nmo_session_get_repository(pristine);
    nmo_object_repository_t *after = nmo_session_get_repository(reloaded);
    ASSERT_EQ(count - 1u, nmo_object_repository_get_count(after));

    /* The writer may reorder the objects: pair them up by class and name. Only the ids inside a
     * chunk may differ. */
    size_t *old_order = (size_t *)malloc(count * sizeof(size_t));
    size_t *new_order = (size_t *)malloc(count * sizeof(size_t));
    ASSERT_NOT_NULL(old_order);
    ASSERT_NOT_NULL(new_order);
    size_t old_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (i != victim_index) old_order[old_count++] = i;
    }
    for (size_t i = 0; i < count - 1u; i++) new_order[i] = i;
    sort_repository = before;
    qsort(old_order, old_count, sizeof(size_t), compare_by_class_name_index);
    sort_repository = after;
    qsort(new_order, count - 1u, sizeof(size_t), compare_by_class_name_index);

    /* Objects with the same class and name form a run; within a run each new
     * chunk must match some old chunk that is still unclaimed. */
    size_t differing = 0;
    unsigned char *claimed = (unsigned char *)calloc(count, 1);
    ASSERT_NOT_NULL(claimed);
    size_t run_start = 0;
    while (run_start < count - 1u) {
        const nmo_object_t *first = nmo_object_repository_get_by_index(after, new_order[run_start]);
        size_t run_end = run_start + 1u;
        while (run_end < count - 1u &&
               compare_by_class_name_index(&new_order[run_start], &new_order[run_end]) == 0) {
            run_end++;
        }
        (void)first;
        for (size_t k = run_start; k < run_end; k++) {
            const nmo_object_t *new_object =
                nmo_object_repository_get_by_index(after, new_order[k]);
            int matched = 0;
            for (size_t m = run_start; m < run_end && !matched; m++) {
                if (claimed[m]) continue;
                const nmo_object_t *old_object =
                    nmo_object_repository_get_by_index(before, old_order[m]);
                if (old_object->class_id != new_object->class_id) continue;
                if (old_object->chunk == NULL || new_object->chunk == NULL ||
                    nmo_chunk_equivalent(old_object->chunk, new_object->chunk)) {
                    claimed[m] = 1;
                    matched = 1;
                }
            }
            /* Parameters that pointed at the deleted array were detached from it,
             * and the level scene lists the objects of the file by index, so
             * those may have changed. */
            if (!matched && new_object->class_id != NMO_CID_LEVEL &&
                new_object->class_id != NMO_CID_PARAMETEROUT &&
                new_object->class_id != NMO_CID_PARAMETERIN &&
                new_object->class_id != NMO_CID_PARAMETERLOCAL) {
                differing++;
                printf("    differs: class %u '%s'\n", (unsigned)new_object->class_id,
                       new_object->name ? new_object->name : "");
            }
        }
        run_start = run_end;
    }
    free(claimed);
    free(old_order);
    free(new_order);
    remove(SCRATCH_FILE);
    nmo_session_destroy(reloaded);
    nmo_session_destroy(edited);
    nmo_session_destroy(pristine);
    nmo_context_release(ctx);
    printf("  Deleted object %zu of %zu; %zu chunks outside the level and detached parameters differ in more than ids\n",
           victim_index, count, differing);
    ASSERT_EQ(0u, differing);
}

TEST_MAIN_BEGIN()
    /* A pass over the whole corpus; slow under a sanitizer. */
    REGISTER_TEST_WITH_TIMEOUT(corpus_chunk_roundtrip, every_object_chunk_survives_save_and_reload, 300.0);
    REGISTER_TEST_WITH_TIMEOUT(corpus_chunk_roundtrip, default_save_keeps_untouched_objects_byte_exact, 300.0);
    REGISTER_TEST_WITH_TIMEOUT(corpus_chunk_roundtrip, deleting_an_object_keeps_the_others_intact, 300.0);
TEST_MAIN_END()
