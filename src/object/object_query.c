/**
 * @file object_query.c
 * @brief Library-level object query helpers.
 */

#include "object_query_internal.h"

#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"

#include <ctype.h>
#include <string.h>

typedef struct query_collect_ctx {
    nmo_object_t **objects;
    size_t count;
} query_collect_ctx_t;


static char query_fold_char(char c, bool icase)
{
    return icase ? (char)tolower((unsigned char)c) : c;
}

static bool query_streq(const char *a, const char *b, bool icase)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (query_fold_char(*a, icase) != query_fold_char(*b, icase)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool query_substr(const char *haystack, const char *needle, bool icase)
{
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen == 0) {
        return true;
    }
    if (nlen > hlen) {
        return false;
    }
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (query_fold_char(haystack[i + j], icase) !=
                query_fold_char(needle[j], icase)) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static bool query_wildcard_match(const char *pattern, const char *text, bool icase)
{
    if (pattern == NULL || text == NULL) {
        return false;
    }

    const char *p = pattern;
    const char *t = text;
    const char *star = NULL;
    const char *retry = NULL;

    while (*t != '\0') {
        if (*p == '?' ||
            query_fold_char(*p, icase) == query_fold_char(*t, icase)) {
            p++;
            t++;
        } else if (*p == '*') {
            star = p++;
            retry = t;
        } else if (star != NULL) {
            p = star + 1;
            t = ++retry;
        } else {
            return false;
        }
    }

    while (*p == '*') {
        p++;
    }
    return *p == '\0';
}

static size_t query_regex_atom_length(const char *pattern)
{
    if (pattern == NULL || *pattern == '\0') {
        return 0;
    }
    if (pattern[0] == '\\' && pattern[1] != '\0') {
        return 2;
    }
    if (pattern[0] == '[') {
        size_t i = 1;
        if (pattern[i] == '^') {
            i++;
        }
        if (pattern[i] == ']') {
            i++;
        }
        while (pattern[i] != '\0' && pattern[i] != ']') {
            if (pattern[i] == '\\' && pattern[i + 1] != '\0') {
                i += 2;
            } else {
                i++;
            }
        }
        if (pattern[i] == ']') {
            return i + 1;
        }
        return 0;
    }
    return 1;
}

static bool query_regex_class_match(const char *pattern, size_t len, char c, bool icase)
{
    bool negate = false;
    size_t i = 1;
    if (i < len && pattern[i] == '^') {
        negate = true;
        i++;
    }

    bool matched = false;
    char target = query_fold_char(c, icase);

    while (i < len && pattern[i] != ']') {
        if (pattern[i] == '\\' && (i + 1) < len) {
            i++;
            if (query_fold_char(pattern[i], icase) == target) {
                matched = true;
            }
            i++;
            continue;
        }

        if ((i + 2) < len && pattern[i + 1] == '-') {
            char start = query_fold_char(pattern[i], icase);
            char end = query_fold_char(pattern[i + 2], icase);
            if (start <= target && target <= end) {
                matched = true;
            }
            i += 3;
            continue;
        }

        if (query_fold_char(pattern[i], icase) == target) {
            matched = true;
        }
        i++;
    }

    return negate ? !matched : matched;
}

static bool query_regex_match_here(const char *text, const char *pattern, bool icase)
{
    if (*pattern == '\0') {
        return true;
    }

    if (pattern[0] == '$' && pattern[1] == '\0') {
        return *text == '\0';
    }

    size_t atom_len = query_regex_atom_length(pattern);
    if (atom_len == 0) {
        return false;
    }

    bool star = pattern[atom_len] == '*';
    const char *next = pattern + atom_len + (star ? 1 : 0);

    if (star) {
        const char *t = text;
        while (*t != '\0') {
            bool atom_match = false;
            if (pattern[0] == '.') {
                atom_match = true;
            } else if (pattern[0] == '[') {
                atom_match = query_regex_class_match(pattern, atom_len, *t, icase);
            } else if (pattern[0] == '\\' && atom_len >= 2) {
                atom_match = query_fold_char(pattern[1], icase) ==
                             query_fold_char(*t, icase);
            } else {
                atom_match = query_fold_char(pattern[0], icase) ==
                             query_fold_char(*t, icase);
            }

            if (!atom_match) {
                break;
            }

            if (query_regex_match_here(t + 1, next, icase)) {
                return true;
            }
            t++;
        }
        return query_regex_match_here(text, next, icase);
    }

    if (*text == '\0') {
        return false;
    }

    if (pattern[0] == '.') {
        return query_regex_match_here(text + 1, next, icase);
    }

    if (pattern[0] == '[') {
        if (!query_regex_class_match(pattern, atom_len, *text, icase)) {
            return false;
        }
        return query_regex_match_here(text + 1, next, icase);
    }

    if (pattern[0] == '\\' && atom_len >= 2) {
        if (query_fold_char(pattern[1], icase) != query_fold_char(*text, icase)) {
            return false;
        }
        return query_regex_match_here(text + 1, next, icase);
    }

    if (query_fold_char(pattern[0], icase) != query_fold_char(*text, icase)) {
        return false;
    }

    return query_regex_match_here(text + 1, next, icase);
}

static bool query_regex_match(const char *text, const char *pattern, bool icase)
{
    if (pattern == NULL) {
        return false;
    }

    if (pattern[0] == '^') {
        return query_regex_match_here(text != NULL ? text : "", pattern + 1, icase);
    }

    const char *t = text != NULL ? text : "";
    do {
        if (query_regex_match_here(t, pattern, icase)) {
            return true;
        }
    } while (*t++ != '\0');

    return false;
}

static nmo_status_t query_match_name(
    const nmo_object_t *object,
    const nmo_object_query_t *query,
    bool *out_matches)
{
    const char *object_name = nmo_object_get_name(object);
    const char *name = object_name != NULL ? object_name : "";
    const char *pattern = query->name != NULL ? query->name : "";
    bool matched = false;

    switch (query->name_mode) {
    case NMO_OBJECT_QUERY_NAME_NONE:
        matched = true;
        break;
    case NMO_OBJECT_QUERY_NAME_EXACT:
        matched = query_streq(name, pattern, query->name_case_insensitive);
        break;
    case NMO_OBJECT_QUERY_NAME_SUBSTRING:
        matched = query_substr(name, pattern, query->name_case_insensitive);
        break;
    case NMO_OBJECT_QUERY_NAME_WILDCARD:
        matched = query_wildcard_match(pattern, name, query->name_case_insensitive);
        break;
    case NMO_OBJECT_QUERY_NAME_REGEX:
        matched = query_regex_match(name, pattern, query->name_case_insensitive);
        break;
    default:
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_matches = matched;
    return NMO_OK;
}

static bool query_collect_visitor(size_t object_index, nmo_object_t *object, void *user_data)
{
    (void)object_index;
    query_collect_ctx_t *ctx = (query_collect_ctx_t *)user_data;
    ctx->objects[ctx->count++] = object;
    return true;
}

nmo_status_t nmo_object_query_matches(
    const nmo_object_t *object,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    bool *out_matches)
{
    if (object == NULL || out_matches == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_matches = false;
    if (query == NULL) {
        *out_matches = true;
        return NMO_OK;
    }

    if (query->object_id != 0 &&
        nmo_object_get_id(object) != query->object_id) {
        return NMO_OK;
    }

    if (query->class_id != 0) {
        nmo_class_id_t object_class = nmo_object_get_class_id(object);
        if (query->include_derived_classes) {
            if (registry == NULL) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            if (!nmo_type_registry_is_class_derived_from(
                    registry, object_class, query->class_id)) {
                return NMO_OK;
            }
        } else if (object_class != query->class_id) {
            return NMO_OK;
        }
    }

    if (query->name_mode != NMO_OBJECT_QUERY_NAME_NONE) {
        bool name_matches = false;
        nmo_status_t status = query_match_name(object, query, &name_matches);
        if (status != NMO_OK || !name_matches) {
            return status;
        }
    }

    if (query->predicate != NULL &&
        !query->predicate(object, query->predicate_user_data)) {
        return NMO_OK;
    }

    *out_matches = true;
    return NMO_OK;
}

static nmo_status_t query_visit_object(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    nmo_object_t *object,
    size_t object_index,
    bool assume_match,
    nmo_object_query_visitor_fn visitor,
    void *user_data,
    nmo_object_query_result_t *result,
    bool *out_continue)
{
    *out_continue = true;
    if (object == NULL) {
        return NMO_OK;
    }

    if (!assume_match) {
        bool matches = false;
        nmo_status_t status =
            nmo_object_query_matches(object, query, ctx->registry, &matches);
        if (status != NMO_OK || !matches) {
            return status;
        }
    }

    result->matched++;
    if (visitor != NULL) {
        result->visited++;
        if (!visitor(object_index, object, user_data)) {
            result->stopped_early = true;
            *out_continue = false;
        }
    }
    return NMO_OK;
}

static void query_index_next_generation(nmo_object_query_index_t *index)
{
    if (index == NULL || index->visit_marks == NULL) {
        return;
    }
    index->visit_generation++;
    if (index->visit_generation == 0) {
        memset(index->visit_marks, 0, index->meta_count * sizeof(*index->visit_marks));
        index->visit_generation = 1;
    }
}

static bool query_index_mark_meta(nmo_object_query_index_t *index, size_t meta_index)
{
    if (index == NULL || index->visit_marks == NULL || meta_index >= index->meta_count) {
        return true;
    }
    if (index->visit_marks[meta_index] == index->visit_generation) {
        return false;
    }
    index->visit_marks[meta_index] = index->visit_generation;
    return true;
}

static bool query_candidate_covers_query(const nmo_object_query_t *query)
{
    if (query == NULL || query->predicate != NULL) {
        return false;
    }
    if (query->object_id != 0) {
        return query->class_id == 0 &&
               query->name_mode == NMO_OBJECT_QUERY_NAME_NONE;
    }
    if (query->class_id != 0) {
        return !query->include_derived_classes &&
               query->name_mode == NMO_OBJECT_QUERY_NAME_NONE;
    }
    return query->name_mode == NMO_OBJECT_QUERY_NAME_EXACT;
}

static bool query_candidate_covers_indexed_filters(const nmo_object_query_t *query)
{
    if (query == NULL) {
        return false;
    }
    if (query->object_id != 0) {
        return query->class_id == 0 &&
               query->name_mode == NMO_OBJECT_QUERY_NAME_NONE;
    }
    if (query->class_id != 0) {
        return !query->include_derived_classes &&
               query->name_mode == NMO_OBJECT_QUERY_NAME_NONE;
    }
    return query->name_mode == NMO_OBJECT_QUERY_NAME_EXACT;
}

static nmo_status_t query_iterate_candidate(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    const query_candidate_t *candidate,
    nmo_object_query_visitor_fn visitor,
    void *user_data,
    nmo_object_query_result_t *result)
{
    nmo_object_query_index_t *index = ctx->index;
    size_t total = nmo_object_repository_get_count(ctx->repository);
    result->total = total;
    bool assume_match = query_candidate_covers_query(query) &&
                        candidate->kind != QUERY_CANDIDATE_ALL &&
                        candidate->kind != QUERY_CANDIDATE_NONE;

    if (assume_match && visitor == NULL) {
        result->matched = candidate->count;
        return NMO_OK;
    }

    if (candidate->kind == QUERY_CANDIDATE_ALL || index == NULL) {
        for (size_t i = 0; i < total; i++) {
            nmo_object_t *object = nmo_object_repository_get_by_index(ctx->repository, i);
            bool keep_going = true;
            nmo_status_t status = query_visit_object(
                ctx, query, object, i, false, visitor, user_data, result, &keep_going);
            if (status != NMO_OK || !keep_going) {
                return status;
            }
        }
        return NMO_OK;
    }

    if (candidate->kind == QUERY_CANDIDATE_NONE) {
        return NMO_OK;
    }

    if (candidate->may_contain_duplicates) {
        query_index_next_generation(index);
    }
    for (size_t i = 0; i < candidate->count; i++) {
        size_t meta_index = candidate->single_meta_index;
        if (candidate->kind == QUERY_CANDIDATE_ID_ENTRIES) {
            meta_index = candidate->id_entries[i].meta_index;
        } else if (candidate->kind == QUERY_CANDIDATE_NAME_ENTRIES) {
            meta_index = candidate->name_entries[i].meta_index;
        }
        if (meta_index >= index->meta_count) {
            continue;
        }
        if (candidate->may_contain_duplicates &&
            !query_index_mark_meta(index, meta_index)) {
            continue;
        }
        query_meta_t *meta = &index->metas[meta_index];
        nmo_object_t *object = meta->object;
        bool keep_going = true;
        nmo_status_t status = query_visit_object(
            ctx, query, object, meta->repository_index, assume_match,
            visitor, user_data, result, &keep_going);
        if (status != NMO_OK || !keep_going) {
            return status;
        }
    }

    return NMO_OK;
}

static nmo_status_t query_collect_candidate(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    const query_candidate_t *candidate,
    nmo_object_t **objects,
    size_t object_capacity,
    size_t *out_count,
    nmo_object_query_result_t *result)
{
    nmo_object_query_index_t *index = ctx->index;
    size_t total = nmo_object_repository_get_count(ctx->repository);
    result->total = total;
    *out_count = 0;

    if (candidate->kind == QUERY_CANDIDATE_NONE) {
        return NMO_OK;
    }
    if (candidate->kind == QUERY_CANDIDATE_ALL || index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    bool assume_indexed_filters = query_candidate_covers_indexed_filters(query);
    if (candidate->may_contain_duplicates) {
        query_index_next_generation(index);
    }

    for (size_t i = 0; i < candidate->count; i++) {
        size_t meta_index = candidate->single_meta_index;
        if (candidate->kind == QUERY_CANDIDATE_ID_ENTRIES) {
            meta_index = candidate->id_entries[i].meta_index;
        } else if (candidate->kind == QUERY_CANDIDATE_NAME_ENTRIES) {
            meta_index = candidate->name_entries[i].meta_index;
        }
        if (meta_index >= index->meta_count) {
            continue;
        }
        if (candidate->may_contain_duplicates &&
            !query_index_mark_meta(index, meta_index)) {
            continue;
        }

        query_meta_t *meta = &index->metas[meta_index];
        nmo_object_t *object = meta->object;
        if (object == NULL) {
            continue;
        }
        if (!assume_indexed_filters) {
            bool matches = false;
            nmo_status_t status =
                nmo_object_query_matches(object, query, ctx->registry, &matches);
            if (status != NMO_OK) {
                return status;
            }
            if (!matches) {
                continue;
            }
        } else if (query != NULL && query->predicate != NULL &&
                   !query->predicate(object, query->predicate_user_data)) {
            continue;
        }
        if (*out_count >= object_capacity) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        objects[(*out_count)++] = object;
        result->matched++;
        result->visited++;
    }

    return NMO_OK;
}

nmo_status_t nmo_object_query_iterate(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    nmo_object_query_visitor_fn visitor,
    void *user_data,
    nmo_object_query_result_t *out_result)
{
    if (ctx == NULL || ctx->repository == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_query_result_t result = {0};
    query_candidate_t candidate = query_all_candidate();
    if (ctx->index != NULL) {
        nmo_status_t status = query_plan_candidate(
            ctx->index, ctx->registry, query, &candidate);
        if (status != NMO_OK) {
            if (out_result != NULL) {
                *out_result = result;
            }
            return status;
        }
    }

    nmo_status_t status = query_iterate_candidate(
        ctx, query, &candidate, visitor, user_data, &result);

    if (out_result != NULL) {
        *out_result = result;
    }
    return status;
}

nmo_status_t nmo_object_query_collect(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    nmo_arena_t *arena,
    nmo_object_t ***out_objects,
    size_t *out_count,
    nmo_object_query_result_t *out_result)
{
    if (ctx == NULL || ctx->repository == NULL || arena == NULL ||
        out_objects == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_objects = NULL;
    *out_count = 0;

    query_candidate_t candidate = query_all_candidate();
    if (ctx->index != NULL) {
        nmo_status_t plan_status = query_plan_candidate(
            ctx->index, ctx->registry, query, &candidate);
        if (plan_status != NMO_OK) {
            if (out_result != NULL) {
                *out_result = (nmo_object_query_result_t){0};
            }
            return plan_status;
        }

        if (candidate.kind != QUERY_CANDIDATE_ALL) {
            size_t total = nmo_object_repository_get_count(ctx->repository);
            size_t upper_bound = query_candidate_upper_bound(&candidate, total);
            nmo_object_query_result_t collect_result = {0};
            if (upper_bound == 0) {
                nmo_status_t status = query_iterate_candidate(
                    ctx, query, &candidate, NULL, NULL, &collect_result);
                if (out_result != NULL) {
                    *out_result = collect_result;
                }
                return status;
            }

            nmo_object_t **objects = (nmo_object_t **)nmo_arena_alloc(
                arena,
                upper_bound * sizeof(*objects),
                _Alignof(nmo_object_t *));
            if (objects == NULL) {
                return NMO_ERR_NOMEM;
            }

            size_t collect_count = 0;
            nmo_status_t status = query_collect_candidate(
                ctx, query, &candidate, objects, upper_bound,
                &collect_count, &collect_result);
            if (status != NMO_OK) {
                return status;
            }

            *out_objects = collect_count > 0 ? objects : NULL;
            *out_count = collect_count;
            if (out_result != NULL) {
                *out_result = collect_result;
            }
            return NMO_OK;
        }
    }

    nmo_object_query_result_t count_result = {0};
    nmo_status_t status =
        nmo_object_query_iterate(ctx, query, NULL, NULL, &count_result);
    if (status != NMO_OK) {
        if (out_result != NULL) {
            *out_result = count_result;
        }
        return status;
    }

    if (count_result.matched == 0) {
        if (out_result != NULL) {
            *out_result = count_result;
        }
        return NMO_OK;
    }

    nmo_object_t **objects = (nmo_object_t **)nmo_arena_alloc(
        arena,
        count_result.matched * sizeof(*objects),
        _Alignof(nmo_object_t *));
    if (objects == NULL) {
        return NMO_ERR_NOMEM;
    }

    query_collect_ctx_t collect = {
        .objects = objects,
        .count = 0
    };
    nmo_object_query_result_t collect_result = {0};
    status = nmo_object_query_iterate(
        ctx, query, query_collect_visitor, &collect, &collect_result);
    if (status != NMO_OK) {
        return status;
    }

    *out_objects = objects;
    *out_count = collect.count;
    if (out_result != NULL) {
        *out_result = collect_result;
    }
    return NMO_OK;
}
