/**
 * @file comparison.c
 * @brief DOM comparison implementation for round-trip testing (Phase 2.4)
 */

#include "app/nmo_comparison.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_id_remap.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdio.h>

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
        strncpy(entry->context, context, NMO_DIFF_CONTEXT_MAX - 1);
        entry->context[NMO_DIFF_CONTEXT_MAX - 1] = '\0';
    } else {
        entry->context[0] = '\0';
    }
    
    result->diff_count++;
}

/* ============================================================================
 * File Info Comparison
 * ============================================================================ */

int nmo_session_compare_file_info(const nmo_session_t *session1,
                                  const nmo_session_t *session2,
                                  nmo_comparison_result_t *result) {
    if (session1 == NULL || session2 == NULL || result == NULL) {
        return 0;
    }
    
    nmo_file_info_t info1 = nmo_session_get_file_info(session1);
    nmo_file_info_t info2 = nmo_session_get_file_info(session2);
    
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

    for (size_t i = 0; i < count2; i++) {
        nmo_object_t *obj2 = objects2[i];
        if (obj2 == NULL) continue;

        nmo_object_id_t key2 = obj2->file_id;
        if (key2 == 0) continue;

        for (size_t j = 0; j < count1; j++) {
            nmo_object_t *obj1 = objects1[j];
            if (obj1 == NULL) continue;

            nmo_object_id_t key1 = obj1->file_id;
            if (key1 == 0) continue;

            if (key1 == key2 && obj1->class_id == obj2->class_id) {
                (void)nmo_id_remap_add(remap, obj2->id, obj1->id);
                break;
            }
        }
    }

    return remap;
}

static int compare_chunks_normalized(const nmo_chunk_t *chunk1,
                                     const nmo_chunk_t *chunk2,
                                     const nmo_id_remap_t *remap,
                                     nmo_arena_t *arena,
                                     nmo_object_id_t object_id,
                                     nmo_comparison_result_t *result) {
    if (chunk1 == NULL && chunk2 == NULL) {
        return 1;
    }
    if (chunk1 == NULL || chunk2 == NULL) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "chunk: %s vs %s",
                 chunk1 ? "present" : "NULL",
                 chunk2 ? "present" : "NULL");
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
        snprintf(ctx, sizeof(ctx), "chunk data size: %zu vs %zu DWORDs", size1, size2);
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
            snprintf(ctx, sizeof(ctx), "chunk data differs (%zu DWORDs)", size1);
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
    
    /* Compare names */
    if (flags & NMO_COMPARE_NAMES) {
        const char *name1 = obj1->name ? obj1->name : "";
        const char *name2 = obj2->name ? obj2->name : "";
        
        if (strcmp(name1, name2) != 0) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx), "name: '%s' vs '%s'", name1, name2);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_NAME, obj1->id, ctx);
            match = 0;
        }
    }
    
    /* Compare class IDs */
    if (flags & NMO_COMPARE_CLASS_IDS) {
        if (obj1->class_id != obj2->class_id) {
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx), "class_id: 0x%08X vs 0x%08X",
                     obj1->class_id, obj2->class_id);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_CLASS_ID, obj1->id, ctx);
            match = 0;
        }
    }
    
    /* Compare chunks */
    if (flags & NMO_COMPARE_CHUNKS) {
        if (!compare_chunks_normalized(obj1->chunk, obj2->chunk, remap, arena, obj1->id, result)) {
            match = 0;
        }
    }
    
    return match;
}

int nmo_session_compare_objects(const nmo_session_t *session1,
                                const nmo_session_t *session2,
                                nmo_compare_flags_t flags,
                                nmo_comparison_result_t *result) {
    if (session1 == NULL || session2 == NULL || result == NULL) {
        return 0;
    }
    
    nmo_object_repository_t *repo1 = nmo_session_get_repository(session1);
    nmo_object_repository_t *repo2 = nmo_session_get_repository(session2);
    
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

    /* Compare objects in order (or by ID if NMO_COMPARE_IGNORE_ORDER) */
    int all_match = 1;

    if (flags & NMO_COMPARE_IGNORE_ORDER) {
        if (count2 > 0 && arena == NULL) {
            return 0;
        }
        uint8_t *used = NULL;
        if (count2 > 0) {
            used = (uint8_t *)nmo_arena_alloc(arena, count2 * sizeof(uint8_t), 1);
            if (used == NULL) {
                if (arena) nmo_arena_destroy(arena);
                return 0;
            }
            memset(used, 0, count2 * sizeof(uint8_t));
        }

        for (size_t i = 0; i < count1; i++) {
            nmo_object_t *obj1 = objects1[i];
            if (obj1 == NULL) continue;

            size_t match_index = (size_t)-1;
            for (size_t j = 0; j < count2; j++) {
                if (used != NULL && used[j]) continue;
                nmo_object_t *obj2 = objects2[j];
                if (obj2 == NULL) continue;
                if (obj2->id == obj1->id) {
                    match_index = j;
                    break;
                }
            }

            if (match_index == (size_t)-1) {
                char ctx[NMO_DIFF_CONTEXT_MAX];
                snprintf(ctx, sizeof(ctx), "object missing in session2: id=%u", obj1->id);
                nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_MISSING, obj1->id, ctx);
                all_match = 0;
                continue;
            }

            if (used != NULL) {
                used[match_index] = 1;
            }

            if (compare_objects(obj1, objects2[match_index], flags, remap, arena, result)) {
                result->objects_matched++;
            } else {
                all_match = 0;
            }
        }

        for (size_t j = 0; j < count2; j++) {
            if (used != NULL && used[j]) continue;
            nmo_object_t *obj2 = objects2[j];
            if (obj2 == NULL) continue;
            char ctx[NMO_DIFF_CONTEXT_MAX];
            snprintf(ctx, sizeof(ctx), "object missing in session1: id=%u", obj2->id);
            nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_MISSING, obj2->id, ctx);
            all_match = 0;
        }

        if (arena) nmo_arena_destroy(arena);
        return all_match;
    }

    size_t min_count = count1 < count2 ? count1 : count2;
    for (size_t i = 0; i < min_count; i++) {
        nmo_object_t *obj1 = objects1[i];
        nmo_object_t *obj2 = objects2[i];

        if (compare_objects(obj1, obj2, flags, remap, arena, result)) {
            result->objects_matched++;
        } else {
            all_match = 0;
        }
    }

    /* Report extra objects in session1 */
    for (size_t i = min_count; i < count1; i++) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "extra object in session1: id=%u",
                 objects1[i] ? objects1[i]->id : 0);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_MISSING,
                                objects1[i] ? objects1[i]->id : 0, ctx);
        all_match = 0;
    }

    /* Report extra objects in session2 */
    for (size_t i = min_count; i < count2; i++) {
        char ctx[NMO_DIFF_CONTEXT_MAX];
        snprintf(ctx, sizeof(ctx), "extra object in session2: id=%u",
                 objects2[i] ? objects2[i]->id : 0);
        nmo_comparison_add_diff(result, NMO_DIFF_OBJECT_MISSING,
                                objects2[i] ? objects2[i]->id : 0, ctx);
        all_match = 0;
    }

    if (arena) nmo_arena_destroy(arena);
    return all_match;
}

/* ============================================================================
 * Full Session Comparison
 * ============================================================================ */

int nmo_session_compare(const nmo_session_t *session1,
                        const nmo_session_t *session2,
                        nmo_compare_flags_t flags,
                        nmo_comparison_result_t *result) {
    if (session1 == NULL || session2 == NULL || result == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Use STRICT flags if no specific flags provided */
    if (flags == NMO_COMPARE_DEFAULT) {
        flags = NMO_COMPARE_FILE_INFO | NMO_COMPARE_STRUCTURE | 
                NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS;
    }
    
    /* Compare file info */
    if (flags & NMO_COMPARE_FILE_INFO) {
        nmo_session_compare_file_info(session1, session2, result);
    }
    
    /* Compare objects */
    if (flags & (NMO_COMPARE_STRUCTURE | NMO_COMPARE_NAMES | 
                 NMO_COMPARE_CLASS_IDS | NMO_COMPARE_CHUNKS)) {
        nmo_session_compare_objects(session1, session2, flags, result);
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
                case NMO_DIFF_OBJECT_ID:        type_str = "OBJECT_ID"; break;
                case NMO_DIFF_OBJECT_NAME:      type_str = "OBJECT_NAME"; break;
                case NMO_DIFF_OBJECT_CLASS_ID:  type_str = "OBJECT_CLASS_ID"; break;
                case NMO_DIFF_OBJECT_CHUNK_SIZE: type_str = "CHUNK_SIZE"; break;
                case NMO_DIFF_OBJECT_CHUNK_DATA: type_str = "CHUNK_DATA"; break;
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
