/**
 * @file comparison.c
 * @brief DOM comparison implementation for round-trip testing (Phase 2.4)
 */

/* Diff context strings intentionally truncate when composing multiple
   NMO_DIFF_CONTEXT_MAX buffers into a single NMO_DIFF_CONTEXT_MAX output.
   Truncation is safe -- snprintf always NUL-terminates. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include "document/nmo_document_compare.h"
#include "../runtime/runtime_internal.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_shadow_storage.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_id_remap.h"
#include "format/nmo_data.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int object_is_reference_only(const nmo_object_t *obj) {
    if (obj == NULL) {
        return 0;
    }
    return ((obj->flags | obj->save_flags) & NMO_OBJECT_REFERENCE_FLAG) != 0;
}

static nmo_file_info_t document_file_info(const nmo_document_t *document)
{
    const nmo_file_state_t *file_state = nmo_document_internal_file_state(document);
    nmo_file_info_t info = {0};

    if (file_state != NULL) {
        info = file_state->info;
    }
    return info;
}

static void format_object_context(const nmo_object_t *obj, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    if (obj == NULL) {
        buffer[0] = '\0';
        return;
    }
    const char *name = obj->name ? obj->name : "";
    snprintf(buffer, buffer_size,
             "class=0x%08X file_id=%u index=%u name='%s'",
             obj->class_id,
             obj->file_id,
             obj->file_index,
             name);
}

static void format_chunk_context(const nmo_chunk_t *chunk, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    if (chunk == NULL) {
        snprintf(buffer, buffer_size, "chunk=NULL");
        return;
    }
    snprintf(buffer, buffer_size,
             "chunk_class=0x%08X dv=%u cv=%u opts=0x%X dwords=%zu",
             chunk->class_id,
             chunk->data_version,
             chunk->chunk_version,
             chunk->chunk_options,
             chunk->data.count);
}

static nmo_object_id_t remap_or_identity(const nmo_id_remap_t *remap, nmo_object_id_t id) {
    if (remap == NULL || id == 0) {
        return id;
    }

    nmo_object_id_t mapped = id;
    if (nmo_id_remap_lookup_id(remap, id, &mapped) == NMO_OK) {
        return mapped;
    }
    return id;
}

static int object_match_score(const nmo_object_t *obj1, const nmo_object_t *obj2) {
    if (obj1 == NULL || obj2 == NULL) {
        return 0;
    }

    if (obj1->file_id != 0 && obj2->file_id != 0 && obj1->file_id == obj2->file_id) {
        int score = (obj1->class_id == obj2->class_id) ? 6 : 5;
        if (obj1->file_index != 0 && obj2->file_index != 0 && obj1->file_index == obj2->file_index) {
            score++;
        }
        return score;
    }

    if (obj1->id != 0 && obj2->id != 0 && obj1->id == obj2->id) {
        return (obj1->class_id == obj2->class_id) ? 4 : 3;
    }

    const char *name1 = obj1->name ? obj1->name : "";
    const char *name2 = obj2->name ? obj2->name : "";
    if (obj1->class_id == obj2->class_id && name1[0] != '\0' && strcmp(name1, name2) == 0) {
        return 2;
    }

    return 0;
}

static int find_best_object_match(const nmo_object_t *obj1,
                                  nmo_object_t **objects2,
                                  size_t count2,
                                  const uint8_t *used2) {
    int best_index = -1;
    int best_score = 0;
    int best_ambiguous = 0;

    for (size_t j = 0; j < count2; j++) {
        if (used2 != NULL && used2[j]) {
            continue;
        }
        nmo_object_t *obj2 = objects2[j];
        if (obj2 == NULL) {
            continue;
        }

        int score = object_match_score(obj1, obj2);
        if (score == 0) {
            continue;
        }

        if (score > best_score) {
            best_score = score;
            best_index = (int)j;
            best_ambiguous = 0;
        } else if (score == best_score) {
            best_ambiguous = 1;
        }
    }

    if (best_index < 0) {
        return -1;
    }
    if (best_ambiguous) {
        return -2;
    }
    return best_index;
}

/* ============================================================================
 * Result Initialization
 * ============================================================================ */

void nmo_comparison_result_init(nmo_comparison_result_t *result) {
    if (result == NULL) return;
    
    memset(result, 0, sizeof(nmo_comparison_result_t));
    result->match = 1;  /* Assume match until proven otherwise */
}

/* ============================================================================
 * Difference Tracking
 * ============================================================================ */

void nmo_comparison_add_diff(nmo_comparison_result_t *result,
                             nmo_diff_type_t type,
                             uint32_t object_id,
                             const char *context) {
    if (result == NULL) return;
    
    result->match = 0;  /* Any difference means no match */
    
    if (result->diff_count >= NMO_MAX_DIFFS) {
        result->diff_overflow = 1;
        return;
    }
    
    nmo_diff_entry_t *entry = &result->diffs[result->diff_count];
    entry->type = type;
    entry->object_id = object_id;
    
    if (context != NULL) {
        snprintf(entry->context, NMO_DIFF_CONTEXT_MAX, "%s", context);
    } else {
        entry->context[0] = '\0';
    }
    
    result->diff_count++;
}

/* ============================================================================
 * File Info Comparison
 * ============================================================================ */

int nmo_document_compare_file_info(const nmo_document_t *document1,
                                   const nmo_document_t *document2,
                                   nmo_comparison_result_t *result) {
    if (document1 == NULL || document2 == NULL || result == NULL) {
        return 0;
    }

    nmo_file_info_t info1 = document_file_info(document1);
    nmo_file_info_t info2 = document_file_info(document2);
    
    int match = 1;
    
    if (info1.file_version != info2.file_version) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "file_version: %u vs %u",
                 info1.file_version, info2.file_version);
        nmo_comparison_add_diff(result, NMO_DIFF_FILE_VERSION, 0, ctx);
        match = 0;
    }
    
    if (info1.ck_version != info2.ck_version) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "ck_version: 0x%08X vs 0x%08X",
                 info1.ck_version, info2.ck_version);
        nmo_comparison_add_diff(result, NMO_DIFF_CK_VERSION, 0, ctx);
        match = 0;
    }
    
    if (info1.object_count != info2.object_count) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "object_count: %u vs %u",
                 info1.object_count, info2.object_count);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_COUNT, 0, ctx);
        if (result->diff_count > 0) {
            result->diffs[result->diff_count - 1].data.count.expected = info1.object_count;
            result->diffs[result->diff_count - 1].data.count.actual = info2.object_count;
        }
        match = 0;
    }
    
    if (info1.manager_count != info2.manager_count) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "manager_count: %u vs %u",
                 info1.manager_count, info2.manager_count);
        nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_COUNT, 0, ctx);
        if (result->diff_count > 0) {
            result->diffs[result->diff_count - 1].data.count.expected = info1.manager_count;
            result->diffs[result->diff_count - 1].data.count.actual = info2.manager_count;
        }
        match = 0;
    }
    
    return match;
}

/* ============================================================================
 * Object Comparison
 * ============================================================================ */

/**
 * @brief Compare two objects for equality
 */
static nmo_id_remap_t *build_id_remap_by_file_id(nmo_object_t **objects1,
                                                 size_t count1,
                                                 nmo_object_t **objects2,
                                                 size_t count2,
                                                 nmo_arena_t *arena) {
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    if (remap == NULL) {
        return NULL;
    }

    uint8_t *used1 = NULL;
    if (count1 > 0) {
        used1 = (uint8_t *)calloc(count1, sizeof(uint8_t));
        if (used1 == NULL) {
            return remap;
        }
    }

    for (size_t i = 0; i < count2; i++) {
        nmo_object_t *obj2 = objects2[i];
        if (obj2 == NULL || obj2->id == 0) {
            continue;
        }

        int best_index = find_best_object_match(obj2, objects1, count1, used1);
        if (best_index < 0) {
            continue;
        }

        nmo_object_t *obj1 = objects1[(size_t)best_index];
        if (obj1 == NULL || obj1->id == 0) {
            continue;
        }

        (void)nmo_id_remap_add(remap, obj2->id, obj1->id);
        if (used1 != NULL) {
            used1[(size_t)best_index] = 1;
        }
    }

    free(used1);

    return remap;
}

static int compare_chunks_normalized(const nmo_chunk_t *chunk1,
                                     const nmo_chunk_t *chunk2,
                                     const nmo_id_remap_t *remap,
                                     nmo_arena_t *arena,
                                     const nmo_object_t *obj1,
                                     const nmo_object_t *obj2,
                                     nmo_object_id_t object_id,
                                     nmo_comparison_result_t *result) {
    if (chunk1 == NULL && chunk2 == NULL) {
        return 1;
    }
    if (chunk1 == NULL || chunk2 == NULL) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        char obj_ctx[NMO_DIFF_CONTEXT_MAX];
        format_object_context(obj1 ? obj1 : obj2, obj_ctx, sizeof(obj_ctx));
        snprintf(ctx, sizeof(ctx), "chunk: %s vs %s (%s)",
                 chunk1 ? "present" : "NULL",
                 chunk2 ? "present" : "NULL",
                 obj_ctx);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_CHUNK_SIZE, object_id, ctx);
        return 0;
    }

    const nmo_chunk_t *compare_chunk2 = chunk2;
    nmo_chunk_t *clone2 = NULL;
    if (remap != NULL && arena != NULL) {
        clone2 = nmo_chunk_clone(chunk2, arena);
        if (clone2 != NULL) {
            (void)nmo_chunk_remap_object_ids(clone2, remap);
            compare_chunk2 = clone2;
        }
    }

    size_t size1 = chunk1->data.count;
    size_t size2 = compare_chunk2->data.count;
    if (size1 != size2) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        char obj_ctx[NMO_DIFF_CONTEXT_MAX];
        char chunk1_ctx[NMO_DIFF_CONTEXT_MAX];
        char chunk2_ctx[NMO_DIFF_CONTEXT_MAX];
        format_object_context(obj1, obj_ctx, sizeof(obj_ctx));
        format_chunk_context(chunk1, chunk1_ctx, sizeof(chunk1_ctx));
        format_chunk_context(compare_chunk2, chunk2_ctx, sizeof(chunk2_ctx));
        snprintf(ctx, sizeof(ctx),
                 "chunk size: %zu vs %zu DWORDs (%s; %s vs %s)",
                 size1, size2, obj_ctx, chunk1_ctx, chunk2_ctx);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_CHUNK_SIZE, object_id, ctx);
        if (result->diff_count > 0) {
            result->diffs[result->diff_count - 1].data.size.expected_size = size1 * sizeof(uint32_t);
            result->diffs[result->diff_count - 1].data.size.actual_size = size2 * sizeof(uint32_t);
        }
        return 0;
    }

    if (size1 > 0) {
        const uint32_t *data1 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk1->data);
        const uint32_t *data2 = NMO_ARENA_ARRAY_DATA(uint32_t, &compare_chunk2->data);
        if (memcmp(data1, data2, size1 * sizeof(uint32_t)) != 0) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            char obj_ctx[NMO_DIFF_CONTEXT_MAX];
            char chunk1_ctx[NMO_DIFF_CONTEXT_MAX];
            char chunk2_ctx[NMO_DIFF_CONTEXT_MAX];
            format_object_context(obj1, obj_ctx, sizeof(obj_ctx));
            format_chunk_context(chunk1, chunk1_ctx, sizeof(chunk1_ctx));
            format_chunk_context(compare_chunk2, chunk2_ctx, sizeof(chunk2_ctx));
            snprintf(ctx, sizeof(ctx), "chunk data differs (%zu DWORDs) (%s; %s vs %s)",
                     size1, obj_ctx, chunk1_ctx, chunk2_ctx);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_CHUNK_DATA, object_id, ctx);
            return 0;
        }
    }

    return 1;
}

static int compare_objects(const nmo_object_t *obj1,
                           const nmo_object_t *obj2,
                           nmo_compare_flags_t flags,
                           const nmo_id_remap_t *remap,
                           nmo_arena_t *arena,
                           nmo_comparison_result_t *result) {
    if (obj1 == NULL || obj2 == NULL) {
        return 0;
    }
    
    int match = 1;

    /* Compare IDs */
    if (flags & NMO_COMPARE_IDS) {
        if (obj1->id != obj2->id) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx), "id: %u vs %u", obj1->id, obj2->id);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_ID, obj1->id, ctx);
            match = 0;
        }
    }
    
    const int ref1 = object_is_reference_only(obj1);
    const int ref2 = object_is_reference_only(obj2);

    if (ref1 != ref2) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        char obj_ctx[NMO_DIFF_CONTEXT_MAX];
        format_object_context(obj1 ? obj1 : obj2, obj_ctx, sizeof(obj_ctx));
        snprintf(ctx, sizeof(ctx), "reference_only: %d vs %d (%s)", ref1, ref2, obj_ctx);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_REFERENCE_FLAG, obj1 ? obj1->id : 0, ctx);
        match = 0;
    }

    /* Compare names */
    if (flags & NMO_COMPARE_NAMES) {
        const char *name1 = obj1->name ? obj1->name : "";
        const char *name2 = obj2->name ? obj2->name : "";
        
        if (strcmp(name1, name2) != 0) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            char obj_ctx[NMO_DIFF_CONTEXT_MAX];
            format_object_context(obj1, obj_ctx, sizeof(obj_ctx));
            snprintf(ctx, sizeof(ctx), "name: '%s' vs '%s' (%s)", name1, name2, obj_ctx);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_NAME, obj1->id, ctx);
            match = 0;
        }
    }
    
    /* Compare class IDs */
    if (flags & NMO_COMPARE_CLASS_IDS) {
        if (obj1->class_id != obj2->class_id) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            char obj_ctx[NMO_DIFF_CONTEXT_MAX];
            format_object_context(obj1, obj_ctx, sizeof(obj_ctx));
            snprintf(ctx, sizeof(ctx), "class_id: 0x%08X vs 0x%08X (%s)",
                     obj1->class_id, obj2->class_id, obj_ctx);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_CLASS_ID, obj1->id, ctx);
            match = 0;
        }
    }
    
    /* Compare chunks */
    if (flags & NMO_COMPARE_CHUNKS) {
        if (!(ref1 || ref2)) {
            if (!compare_chunks_normalized(obj1->chunk, obj2->chunk, remap, arena, obj1, obj2, obj1->id, result)) {
                match = 0;
            }
        }
    }
    
    return match;
}

int nmo_document_compare_objects(const nmo_document_t *document1,
                                 const nmo_document_t *document2,
                                 nmo_compare_flags_t flags,
                                 nmo_comparison_result_t *result) {
    if (document1 == NULL || document2 == NULL || result == NULL) {
        return 0;
    }

    nmo_object_repository_t *repo1 = nmo_document_get_repository(document1);
    nmo_object_repository_t *repo2 = nmo_document_get_repository(document2);
    
    if (repo1 == NULL || repo2 == NULL) {
        return 0;
    }
    
    size_t count1 = 0, count2 = 0;
    nmo_object_t **objects1 = nmo_object_repository_get_all(repo1, &count1);
    nmo_object_t **objects2 = nmo_object_repository_get_all(repo2, &count2);
    
    result->objects_compared = (uint32_t)(count1 > count2 ? count1 : count2);
    
    /* Compare counts */
    if (count1 != count2) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "repository object count: %zu vs %zu",
                 count1, count2);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_COUNT, 0, ctx);
    }
    
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_id_remap_t *remap = NULL;
    if (arena != NULL) {
        remap = build_id_remap_by_file_id(objects1, count1, objects2, count2, arena);
    }

    /* Compare objects by semantic identity first, then optionally enforce order. */
    int all_match = 1;

    uint8_t *used2 = NULL;
    if (count2 > 0) {
        used2 = (uint8_t *)calloc(count2, sizeof(uint8_t));
        if (used2 == NULL) {
            if (arena) {
                nmo_arena_destroy(arena);
            }
            return 0;
        }
    }

    for (size_t i = 0; i < count1; i++) {
        nmo_object_t *obj1 = objects1[i];
        if (obj1 == NULL) {
            continue;
        }

        int match_index = find_best_object_match(obj1, objects2, count2, used2);
        if (match_index == -2) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            char obj_ctx[NMO_DIFF_CONTEXT_MAX];
            format_object_context(obj1, obj_ctx, sizeof(obj_ctx));
            snprintf(ctx, sizeof(ctx), "ambiguous object match in session2 (%s)", obj_ctx);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_MISSING, obj1->id, ctx);
            all_match = 0;
            continue;
        }

        if (match_index < 0) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            char obj_ctx[NMO_DIFF_CONTEXT_MAX];
            format_object_context(obj1, obj_ctx, sizeof(obj_ctx));
            snprintf(ctx, sizeof(ctx), "object missing in session2: id=%u (%s)", obj1->id, obj_ctx);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_MISSING, obj1->id, ctx);
            all_match = 0;
            continue;
        }

        size_t matched_index = (size_t)match_index;
        used2[matched_index] = 1;

        if ((flags & NMO_COMPARE_IGNORE_ORDER) == 0 && matched_index != i) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx), "object order mismatch: session1[%zu] -> session2[%zu]", i, matched_index);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_ORDER, obj1->id, ctx);
            all_match = 0;
        }

        if (compare_objects(obj1, objects2[matched_index], flags, remap, arena, result)) {
            result->objects_matched++;
        } else {
            all_match = 0;
        }
    }

    for (size_t j = 0; j < count2; j++) {
        if (used2 != NULL && used2[j]) {
            continue;
        }

        nmo_object_t *obj2 = objects2[j];
        if (obj2 == NULL) {
            continue;
        }

        char ctx[NMO_DIFF_CONTEXT_MAX];
        char obj_ctx[NMO_DIFF_CONTEXT_MAX];
        format_object_context(obj2, obj_ctx, sizeof(obj_ctx));
        snprintf(ctx, sizeof(ctx), "object missing in session1: id=%u (%s)", obj2->id, obj_ctx);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_MISSING, obj2->id, ctx);
        all_match = 0;
    }

    free(used2);

    if (arena) nmo_arena_destroy(arena);
    return all_match;
}

static int compare_manager_chunks(const nmo_manager_data_t *mgr1,
                                  const nmo_manager_data_t *mgr2,
                                  const nmo_id_remap_t *remap2to1,
                                  nmo_arena_t *arena,
                                  nmo_comparison_result_t *result) {
    if (mgr1 == NULL || mgr2 == NULL || result == NULL) {
        return 0;
    }

    const nmo_chunk_t *chunk1 = mgr1->chunk;
    const nmo_chunk_t *chunk2 = mgr2->chunk;

    if (chunk1 == NULL && chunk2 == NULL) {
        return 1;
    }

    char guid_ctx[NMO_DIFF_CONTEXT_MAX];
    nmo_guid_format(mgr1->guid, guid_ctx, sizeof(guid_ctx));

    if (chunk1 == NULL || chunk2 == NULL) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx),
                 "manager chunk presence mismatch guid=%s (%s vs %s)",
                 guid_ctx,
                 chunk1 ? "present" : "NULL",
                 chunk2 ? "present" : "NULL");
        nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_CHUNK_SIZE, 0, ctx);
        return 0;
    }

    const nmo_chunk_t *compare_chunk2 = chunk2;
    nmo_chunk_t *remapped_chunk2 = NULL;
    if (remap2to1 != NULL && arena != NULL) {
        remapped_chunk2 = nmo_chunk_clone(chunk2, arena);
        if (remapped_chunk2 != NULL) {
            (void)nmo_chunk_remap_object_ids(remapped_chunk2, remap2to1);
            compare_chunk2 = remapped_chunk2;
        }
    }

    if (chunk1->data.count != compare_chunk2->data.count) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx),
                 "manager chunk size guid=%s: %zu vs %zu DWORDs",
                 guid_ctx,
                 chunk1->data.count,
                 compare_chunk2->data.count);
        nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_CHUNK_SIZE, 0, ctx);
        if (result->diff_count > 0) {
            result->diffs[result->diff_count - 1].data.size.expected_size =
                chunk1->data.count * sizeof(uint32_t);
            result->diffs[result->diff_count - 1].data.size.actual_size =
                compare_chunk2->data.count * sizeof(uint32_t);
        }
        return 0;
    }

    if (chunk1->data.count > 0) {
        const uint32_t *data1 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk1->data);
        const uint32_t *data2 = NMO_ARENA_ARRAY_DATA(uint32_t, &compare_chunk2->data);
        if (memcmp(data1, data2, chunk1->data.count * sizeof(uint32_t)) != 0) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx),
                     "manager chunk data mismatch guid=%s (%zu DWORDs)",
                     guid_ctx,
                     chunk1->data.count);
            nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_CHUNK_DATA, 0, ctx);
            return 0;
        }
    }

    return 1;
}

static int nmo_document_compare_managers(const nmo_document_t *document1,
                                         const nmo_document_t *document2,
                                         nmo_compare_flags_t flags,
                                         nmo_comparison_result_t *result) {
    (void)flags;
    if (document1 == NULL || document2 == NULL || result == NULL) {
        return 0;
    }

    uint32_t count1 = 0;
    uint32_t count2 = 0;
    const nmo_file_state_t *mfs1 = nmo_document_internal_file_state(document1);
    const nmo_file_state_t *mfs2 = nmo_document_internal_file_state(document2);
    nmo_manager_data_t *managers1 = mfs1 ? mfs1->manager_data : NULL;
    nmo_manager_data_t *managers2 = mfs2 ? mfs2->manager_data : NULL;
    count1 = mfs1 ? mfs1->manager_data_count : 0;
    count2 = mfs2 ? mfs2->manager_data_count : 0;

    result->managers_compared = (count1 > count2) ? count1 : count2;

    if (count1 != count2) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "manager data count: %u vs %u", count1, count2);
        nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_COUNT, 0, ctx);
        if (result->diff_count > 0) {
            result->diffs[result->diff_count - 1].data.count.expected = count1;
            result->diffs[result->diff_count - 1].data.count.actual = count2;
        }
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_id_remap_t *remap2to1 = NULL;
    if (arena != NULL) {
        nmo_object_repository_t *repo1 = nmo_document_get_repository(document1);
        nmo_object_repository_t *repo2 = nmo_document_get_repository(document2);
        if (repo1 != NULL && repo2 != NULL) {
            size_t object_count1 = 0;
            size_t object_count2 = 0;
            nmo_object_t **objects1 = nmo_object_repository_get_all(repo1, &object_count1);
            nmo_object_t **objects2 = nmo_object_repository_get_all(repo2, &object_count2);
            remap2to1 = build_id_remap_by_file_id(objects1, object_count1, objects2, object_count2, arena);
        }
    }

    int all_match = 1;
    uint8_t *used2 = NULL;
    if (count2 > 0) {
        used2 = (uint8_t *)calloc(count2, sizeof(uint8_t));
        if (used2 == NULL) {
            if (arena != NULL) {
                nmo_arena_destroy(arena);
            }
            return 0;
        }
    }

    for (uint32_t i = 0; i < count1; i++) {
        const nmo_manager_data_t *mgr1 = (managers1 != NULL) ? &managers1[i] : NULL;
        if (mgr1 == NULL) {
            continue;
        }

        int match_index = -1;
        int ambiguous = 0;
        for (uint32_t j = 0; j < count2; j++) {
            if (used2 != NULL && used2[j]) {
                continue;
            }
            const nmo_manager_data_t *mgr2 = (managers2 != NULL) ? &managers2[j] : NULL;
            if (mgr2 == NULL) {
                continue;
            }
            if (!nmo_guid_equals(mgr1->guid, mgr2->guid)) {
                continue;
            }
            if (match_index < 0) {
                match_index = (int)j;
            } else {
                ambiguous = 1;
                break;
            }
        }

        char guid_ctx[NMO_DIFF_CONTEXT_MAX];
        nmo_guid_format(mgr1->guid, guid_ctx, sizeof(guid_ctx));

        if (ambiguous) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx), "ambiguous manager match in session2 guid=%s", guid_ctx);
            nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_GUID, 0, ctx);
            all_match = 0;
            continue;
        }

        if (match_index < 0) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx), "manager missing in session2 guid=%s", guid_ctx);
            nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_MISSING, 0, ctx);
            all_match = 0;
            continue;
        }

        if (used2 != NULL) {
            used2[(size_t)match_index] = 1;
        }

        if (compare_manager_chunks(mgr1, &managers2[match_index], remap2to1, arena, result)) {
            result->managers_matched++;
        } else {
            all_match = 0;
        }
    }

    for (uint32_t j = 0; j < count2; j++) {
        if (used2 != NULL && used2[j]) {
            continue;
        }
        const nmo_manager_data_t *mgr2 = (managers2 != NULL) ? &managers2[j] : NULL;
        if (mgr2 == NULL) {
            continue;
        }

        char guid_ctx[NMO_DIFF_CONTEXT_MAX];
        char ctx[NMO_DIFF_CONTEXT_MAX];
        nmo_guid_format(mgr2->guid, guid_ctx, sizeof(guid_ctx));
        snprintf(ctx, sizeof(ctx), "manager missing in session1 guid=%s", guid_ctx);
        nmo_comparison_add_diff(result, NMO_DIFF_MANAGER_MISSING, 0, ctx);
        all_match = 0;
    }

    free(used2);

    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    return all_match;
}

typedef struct nmo_shadow_compare_iter_ctx {
    const nmo_shadow_storage_t *other;
    const nmo_id_remap_t *key_remap;
    nmo_comparison_result_t *result;
    int all_match;
    const char *missing_label;
} nmo_shadow_compare_iter_ctx_t;

static bool nmo_shadow_compare_iter(uint32_t chunk_id,
                                    const void *data,
                                    size_t size,
                                    void *user) {
    nmo_shadow_compare_iter_ctx_t *ctx = (nmo_shadow_compare_iter_ctx_t *)user;
    if (ctx == NULL || ctx->other == NULL || ctx->result == NULL) {
        return true;
    }

    nmo_object_id_t mapped_id = remap_or_identity(ctx->key_remap, chunk_id);
    size_t other_size = 0;
    const void *other_data = nmo_shadow_get_chunk_tail(ctx->other, mapped_id, &other_size);

    if (other_data == NULL) {
        char diff_ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(diff_ctx, sizeof(diff_ctx),
                 "shadow chunk tail missing (%s): id=%u mapped=%u size=%zu",
                 ctx->missing_label ? ctx->missing_label : "unknown",
                 chunk_id,
                 mapped_id,
                 size);
        nmo_comparison_add_diff(ctx->result, NMO_DIFF_SHADOW_DATA, chunk_id, diff_ctx);
        ctx->all_match = 0;
        return true;
    }

    if (size != other_size) {
        char diff_ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(diff_ctx, sizeof(diff_ctx),
                 "shadow chunk tail size mismatch: id=%u mapped=%u %zu vs %zu",
                 chunk_id,
                 mapped_id,
                 size,
                 other_size);
        nmo_comparison_add_diff(ctx->result, NMO_DIFF_SHADOW_DATA, chunk_id, diff_ctx);
        if (ctx->result->diff_count > 0) {
            ctx->result->diffs[ctx->result->diff_count - 1].data.size.expected_size = size;
            ctx->result->diffs[ctx->result->diff_count - 1].data.size.actual_size = other_size;
        }
        ctx->all_match = 0;
        return true;
    }

    if (size > 0 && data != NULL && memcmp(data, other_data, size) != 0) {
        char diff_ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(diff_ctx, sizeof(diff_ctx),
                 "shadow chunk tail payload mismatch: id=%u mapped=%u size=%zu",
                 chunk_id,
                 mapped_id,
                 size);
        nmo_comparison_add_diff(ctx->result, NMO_DIFF_SHADOW_DATA, chunk_id, diff_ctx);
        ctx->all_match = 0;
    }

    return true;
}

static int nmo_document_compare_shadow(const nmo_document_t *document1,
                                       const nmo_document_t *document2,
                                       nmo_comparison_result_t *result) {
    if (document1 == NULL || document2 == NULL || result == NULL) {
        return 0;
    }

    const nmo_shadow_storage_t *shadow1 = nmo_document_internal_get_shadow_storage(document1);
    const nmo_shadow_storage_t *shadow2 = nmo_document_internal_get_shadow_storage(document2);

    if (shadow1 == NULL && shadow2 == NULL) {
        return 1;
    }

    if (shadow1 == NULL || shadow2 == NULL) {
        nmo_comparison_add_diff(result, NMO_DIFF_SHADOW_DATA, 0,
                                "shadow storage missing in one session");
        return 0;
    }

    int all_match = 1;

    size_t included_size1 = 0;
    size_t included_size2 = 0;
    const void *included1 = nmo_shadow_get_included_files(shadow1, &included_size1);
    const void *included2 = nmo_shadow_get_included_files(shadow2, &included_size2);

    if ((included1 == NULL) != (included2 == NULL)) {
        nmo_comparison_add_diff(result, NMO_DIFF_SHADOW_DATA, 0,
                                "included-files shadow presence mismatch");
        all_match = 0;
    } else if (included1 != NULL) {
        if (included_size1 != included_size2) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx),
                     "included-files shadow size mismatch: %zu vs %zu",
                     included_size1,
                     included_size2);
            nmo_comparison_add_diff(result, NMO_DIFF_SHADOW_DATA, 0, ctx);
            if (result->diff_count > 0) {
                result->diffs[result->diff_count - 1].data.size.expected_size = included_size1;
                result->diffs[result->diff_count - 1].data.size.actual_size = included_size2;
            }
            all_match = 0;
        } else if (included_size1 > 0 && memcmp(included1, included2, included_size1) != 0) {
            nmo_comparison_add_diff(result, NMO_DIFF_SHADOW_DATA, 0,
                                    "included-files shadow payload mismatch");
            all_match = 0;
        }
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    const nmo_id_remap_t *remap1to2 = NULL;
    const nmo_id_remap_t *remap2to1 = NULL;
    if (arena != NULL) {
        nmo_object_repository_t *repo1 = nmo_document_get_repository(document1);
        nmo_object_repository_t *repo2 = nmo_document_get_repository(document2);
        if (repo1 != NULL && repo2 != NULL) {
            size_t object_count1 = 0;
            size_t object_count2 = 0;
            nmo_object_t **objects1 = nmo_object_repository_get_all(repo1, &object_count1);
            nmo_object_t **objects2 = nmo_object_repository_get_all(repo2, &object_count2);
            remap2to1 = build_id_remap_by_file_id(objects1, object_count1, objects2, object_count2, arena);
            remap1to2 = build_id_remap_by_file_id(objects2, object_count2, objects1, object_count1, arena);
        }
    }

    size_t tails1 = nmo_shadow_chunk_tail_count(shadow1);
    size_t tails2 = nmo_shadow_chunk_tail_count(shadow2);
    if (tails1 != tails2) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "shadow chunk tail count mismatch: %zu vs %zu", tails1, tails2);
        nmo_comparison_add_diff(result, NMO_DIFF_SHADOW_DATA, 0, ctx);
        all_match = 0;
    }

    nmo_shadow_compare_iter_ctx_t ctx12 = {
        .other = shadow2,
        .key_remap = remap1to2,
        .result = result,
        .all_match = 1,
        .missing_label = "session2",
    };
    nmo_shadow_iterate_chunk_tails(shadow1, nmo_shadow_compare_iter, &ctx12);
    if (!ctx12.all_match) {
        all_match = 0;
    }

    nmo_shadow_compare_iter_ctx_t ctx21 = {
        .other = shadow1,
        .key_remap = remap2to1,
        .result = result,
        .all_match = 1,
        .missing_label = "session1",
    };
    nmo_shadow_iterate_chunk_tails(shadow2, nmo_shadow_compare_iter, &ctx21);
    if (!ctx21.all_match) {
        all_match = 0;
    }

    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }

    return all_match;
}

/* ============================================================================
 * Full Session Comparison
 * ============================================================================ */

nmo_status_t nmo_document_compare(const nmo_document_t *document1,
                                  const nmo_document_t *document2,
                                  nmo_compare_flags_t flags,
                                  nmo_comparison_result_t *result) {
    if (document1 == NULL || document2 == NULL || result == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Use STRICT flags if no specific flags provided */
    if (flags == NMO_COMPARE_DEFAULT) {
        flags = NMO_COMPARE_FILE_INFO | NMO_COMPARE_STRUCTURE | 
                NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS;
    }
    
    /* Compare file info */
    if (flags & NMO_COMPARE_FILE_INFO) {
        nmo_document_compare_file_info(document1, document2, result);
    }
    
    /* Compare objects */
    if (flags & (NMO_COMPARE_STRUCTURE | NMO_COMPARE_IDS | NMO_COMPARE_NAMES |
                 NMO_COMPARE_CLASS_IDS | NMO_COMPARE_CHUNKS)) {
        nmo_document_compare_objects(document1, document2, flags, result);
    }

    /* Compare manager chunks */
    if (flags & NMO_COMPARE_MANAGERS) {
        nmo_document_compare_managers(document1, document2, flags, result);
    }

    /* Compare shadow preservation data */
    if (flags & NMO_COMPARE_SHADOW) {
        nmo_document_compare_shadow(document1, document2, result);
    }
    
    /* Generate report if verbose */
    if (flags & NMO_COMPARE_VERBOSE) {
        nmo_comparison_result_format_report(result);
    }
    
    return NMO_OK;
}

/* ============================================================================
 * Report Generation
 * ============================================================================ */

void nmo_comparison_result_format_report(nmo_comparison_result_t *result) {
    if (result == NULL) return;
    
    char *p = result->report;
    size_t remaining = sizeof(result->report);
    int written;
    
    /* Header */
    if (result->match) {
        written = snprintf(p, remaining, "=== Comparison Result: MATCH ===\n\n");
    } else {
        written = snprintf(p, remaining, "=== Comparison Result: MISMATCH ===\n\n");
    }
    if (written > 0 && (size_t)written < remaining) {
        p += written;
        remaining -= written;
    }
    
    /* Statistics */
    written = snprintf(p, remaining,
                       "Objects: %u compared, %u matched\n"
                       "Managers: %u compared, %u matched\n"
                       "Differences: %d%s\n\n",
                       result->objects_compared, result->objects_matched,
                       result->managers_compared, result->managers_matched,
                       result->diff_count,
                       result->diff_overflow ? " (truncated)" : "");
    if (written > 0 && (size_t)written < remaining) {
        p += written;
        remaining -= written;
    }
    
    /* Difference details */
    if (result->diff_count > 0) {
        written = snprintf(p, remaining, "Differences:\n");
        if (written > 0 && (size_t)written < remaining) {
            p += written;
            remaining -= written;
        }
        
        for (int i = 0; i < result->diff_count && remaining > 0; i++) {
            const nmo_diff_entry_t *diff = &result->diffs[i];
            
            const char *type_str = "UNKNOWN";
            switch (diff->type) {
                case NMO_DIFF_OBJECT_COUNT:     type_str = "OBJECT_COUNT"; break;
                case NMO_DIFF_MANAGER_COUNT:    type_str = "MANAGER_COUNT"; break;
                case NMO_DIFF_OBJECT_MISSING:   type_str = "OBJECT_MISSING"; break;
                case NMO_DIFF_OBJECT_ORDER:     type_str = "OBJECT_ORDER"; break;
                case NMO_DIFF_OBJECT_ID:        type_str = "OBJECT_ID"; break;
                case NMO_DIFF_OBJECT_NAME:      type_str = "OBJECT_NAME"; break;
                case NMO_DIFF_OBJECT_CLASS_ID:  type_str = "OBJECT_CLASS_ID"; break;
                case NMO_DIFF_OBJECT_REFERENCE_FLAG: type_str = "OBJECT_REFERENCE_FLAG"; break;
                case NMO_DIFF_OBJECT_CHUNK_SIZE: type_str = "CHUNK_SIZE"; break;
                case NMO_DIFF_OBJECT_CHUNK_DATA: type_str = "CHUNK_DATA"; break;
                case NMO_DIFF_MANAGER_MISSING:  type_str = "MANAGER_MISSING"; break;
                case NMO_DIFF_MANAGER_GUID:     type_str = "MANAGER_GUID"; break;
                case NMO_DIFF_MANAGER_CHUNK_SIZE: type_str = "MANAGER_CHUNK_SIZE"; break;
                case NMO_DIFF_MANAGER_CHUNK_DATA: type_str = "MANAGER_CHUNK_DATA"; break;
                case NMO_DIFF_FILE_VERSION:     type_str = "FILE_VERSION"; break;
                case NMO_DIFF_CK_VERSION:       type_str = "CK_VERSION"; break;
                case NMO_DIFF_SHADOW_DATA:      type_str = "SHADOW_DATA"; break;
                default: break;
            }
            
            if (diff->object_id != 0) {
                written = snprintf(p, remaining, "  [%d] %s (obj=%u): %s\n",
                                   i + 1, type_str, diff->object_id, diff->context);
            } else {
                written = snprintf(p, remaining, "  [%d] %s: %s\n",
                                   i + 1, type_str, diff->context);
            }
            
            if (written > 0 && (size_t)written < remaining) {
                p += written;
                remaining -= written;
            }
        }
    }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
