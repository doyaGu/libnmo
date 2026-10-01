/**
 * @file chunk_roundtrip_check.h
 * @brief Shared by the corpus and the generated chunk round-trip tests
 *
 * Compares the object chunks of a never-saved load with the chunks that come back
 * after a save and a reload, and checks that deleting an object leaves the other
 * objects' chunks as they were. Include from one test source file only.
 */

#ifndef NMO_TEST_CHUNK_ROUNDTRIP_CHECK_H
#define NMO_TEST_CHUNK_ROUNDTRIP_CHECK_H

#include "../test_framework.h"

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_residue.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "type/nmo_type_system.h"
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

/* The references of a repository as text, one "from kind to" line per edge, with objects
 * named by class and name, so two files can be compared by what points at what even when
 * the objects sit at other places. Edges that touch `skip_id` are left out. */
static int compare_text(const void *lhs, const void *rhs)
{
    return strcmp(*(const char *const *)lhs, *(const char *const *)rhs);
}

static char *reference_edge_text(nmo_object_repository_t *repo, const nmo_ref_edge_t *edge)
{
    const nmo_object_t *from = nmo_object_repository_find_by_id(repo, edge->from);
    const nmo_object_t *to = nmo_object_repository_find_by_id(repo, edge->to);
    const char *from_name = from && from->name ? from->name : "?";
    const char *to_name = to && to->name ? to->name : "?";
    const int length = snprintf(NULL, 0, "%u:%s -%d-> %u:%s", from ? (unsigned)from->class_id : 0u,
                                from_name, (int)edge->kind, to ? (unsigned)to->class_id : 0u, to_name);
    if (length < 0) return NULL;
    char *text = (char *)malloc((size_t)length + 1u);
    if (text != NULL) {
        snprintf(text, (size_t)length + 1u, "%u:%s -%d-> %u:%s", from ? (unsigned)from->class_id : 0u,
                 from_name, (int)edge->kind, to ? (unsigned)to->class_id : 0u, to_name);
    }
    return text;
}

static size_t collect_reference_texts(nmo_context_t *ctx, nmo_object_repository_t *repo,
                                      nmo_object_id_t skip_id, char ***out_texts)
{
    *out_texts = NULL;
    nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 20);
    nmo_ref_graph_t *graph = arena != NULL
        ? nmo_ref_graph_create(repo, nmo_context_get_type_runtime(ctx)->types, arena) : NULL;
    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    size_t kept = 0;
    if (graph != NULL && nmo_ref_graph_get_edges(graph, &edges, &edge_count) == NMO_OK &&
        edge_count > 0) {
        char **texts = (char **)calloc(edge_count, sizeof(char *));
        for (size_t i = 0; texts != NULL && i < edge_count; i++) {
            if (edges[i].from == skip_id || edges[i].to == skip_id) continue;
            texts[kept] = reference_edge_text(repo, &edges[i]);
            if (texts[kept] != NULL) kept++;
        }
        qsort(texts, kept, sizeof(char *), compare_text);
        *out_texts = texts;
    }
    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    return kept;
}

static void free_reference_texts(char **texts, size_t count)
{
    for (size_t i = 0; i < count; i++) free(texts[i]);
    free(texts);
}

/* Objects that changed place when the writer filled the freed slot: a chunk written
 * before the move names them by their old file index. */
typedef struct moved_objects {
    uint32_t old_index[64];
    uint32_t new_index[64];
    size_t count;
} moved_objects_t;

static int moved_to(const moved_objects_t *moved, uint32_t old_value, uint32_t new_value)
{
    for (size_t i = 0; i < moved->count; i++) {
        if (moved->old_index[i] == old_value && moved->new_index[i] == new_value) return 1;
    }
    return 0;
}

/* The same chunk but for references to moved objects. A file chunk does not record where
 * its ids are, so every dword that differs must be the old index of a moved object next to
 * its new one. Chunks with sub-chunks are compared as they are. */
static int chunk_equivalent_after_moves(const nmo_chunk_t *before, const nmo_chunk_t *after,
                                        const moved_objects_t *moved)
{
    if (before->data.count != after->data.count || before->chunks.count != 0u ||
        after->chunks.count != 0u || moved->count == 0u) {
        return 0;
    }
    const uint32_t *a = (const uint32_t *)before->data.data;
    const uint32_t *b = (const uint32_t *)after->data.data;
    for (size_t i = 0; i < before->data.count; i++) {
        if (a[i] != b[i] && !moved_to(moved, a[i], b[i])) return 0;
    }
    return 1;
}

/* Deleting an object moves the ones after it; the others keep their data. The object
 * deleted is the first of `victim_class` in the second half of the file. */
static void check_deletion_keeps_the_others(nmo_context_t *ctx, const char *path,
                                            nmo_class_id_t victim_class, size_t min_objects,
                                            const nmo_class_id_t *may_change, size_t may_change_count)
{
    nmo_session_t *pristine = load_session(ctx, path);
    nmo_session_t *edited = load_session(ctx, path);
    ASSERT_NOT_NULL(pristine);
    ASSERT_NOT_NULL(edited);

    nmo_object_repository_t *repo = nmo_session_get_repository(edited);
    size_t count = nmo_object_repository_get_count(repo);
    ASSERT_GE(count, min_objects);

    /* An object of `victim_class` from the middle of the file, deleted the way the tools
     * delete objects (references to it are detached). */
    size_t victim_index = 0;
    nmo_object_id_t victim_id = 0;
    for (size_t pass = 0; pass < 2 && victim_index == 0; pass++) {
        for (size_t i = pass == 0 ? count / 2 : 1; i < count; i++) {
            const nmo_object_t *candidate = nmo_object_repository_get_by_index(repo, i);
            if (candidate != NULL && candidate->class_id == victim_class) {
                victim_index = i;
                victim_id = candidate->id;
                break;
            }
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

    /* A name that is unique in its class tells where an object went. Chunks name an
     * object by its place in the file, which is its index in the repository. */
    moved_objects_t moved;
    memset(&moved, 0, sizeof(moved));
    for (size_t k = 0; k + 1u < count && moved.count < 64u; k++) {
        const nmo_object_t *after_object = nmo_object_repository_get_by_index(after, new_order[k]);
        const char *after_name = after_object->name ? after_object->name : "";
        size_t same_after = 0;
        for (size_t n = 0; n + 1u < count; n++) {
            const nmo_object_t *other = nmo_object_repository_get_by_index(after, new_order[n]);
            same_after += other->class_id == after_object->class_id &&
                          strcmp(other->name ? other->name : "", after_name) == 0;
        }
        size_t same_before = 0;
        size_t before_position = 0;
        for (size_t m = 0; m < old_count; m++) {
            const nmo_object_t *other = nmo_object_repository_get_by_index(before, old_order[m]);
            if (other->class_id == after_object->class_id &&
                strcmp(other->name ? other->name : "", after_name) == 0) {
                same_before++;
                before_position = old_order[m];
            }
        }
        if (same_after == 1u && same_before == 1u && before_position != new_order[k]) {
            moved.old_index[moved.count] = (uint32_t)before_position;
            moved.new_index[moved.count] = (uint32_t)new_order[k];
            moved.count++;
        }
    }

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
                    nmo_chunk_equivalent(old_object->chunk, new_object->chunk) ||
                    chunk_equivalent_after_moves(old_object->chunk, new_object->chunk, &moved)) {
                    claimed[m] = 1;
                    matched = 1;
                }
            }
            /* Objects that pointed at the deleted one were detached from it, so their
             * classes (`may_change`) are allowed to differ. */
            int allowed = 0;
            for (size_t c = 0; c < may_change_count; c++) {
                allowed |= new_object->class_id == may_change[c];
            }
            if (!matched && !allowed) {
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

    /* What the objects point at, by name: moving an object must not change it. */
    char **refs_before = NULL;
    char **refs_after = NULL;
    const size_t refs_before_count = collect_reference_texts(ctx, before, victim_id, &refs_before);
    const size_t refs_after_count = collect_reference_texts(ctx, after, 0u, &refs_after);
    size_t ref_differences = 0;
    if (refs_before_count != refs_after_count) {
        ref_differences++;
        printf("    %zu references before, %zu after\n", refs_before_count, refs_after_count);
    }
    for (size_t i = 0; i < refs_before_count && i < refs_after_count; i++) {
        if (strcmp(refs_before[i], refs_after[i]) != 0 && ref_differences++ < 10u) {
            printf("    reference differs: '%s' vs '%s'\n", refs_before[i], refs_after[i]);
        }
    }
    free_reference_texts(refs_before, refs_before_count);
    free_reference_texts(refs_after, refs_after_count);
    printf("  References by name: %zu before the deletion, %zu after, %zu different\n",
           refs_before_count, refs_after_count, ref_differences);
    ASSERT_EQ(0u, ref_differences);
    remove(SCRATCH_FILE);
    nmo_session_destroy(reloaded);
    nmo_session_destroy(edited);
    nmo_session_destroy(pristine);
    printf("  Deleted object %zu of %zu; %zu chunks outside the level and detached parameters differ in more than ids\n",
           victim_index, count, differing);
    ASSERT_EQ(0u, differing);
}

#endif /* NMO_TEST_CHUNK_ROUNDTRIP_CHECK_H */
