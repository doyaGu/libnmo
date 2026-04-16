/**
 * @file object_query.c
 * @brief Library-level object query helpers.
 */

#include "object/nmo_object_query.h"

#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "core/nmo_hash.h"
#include "core/nmo_hash_table.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_system.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct query_collect_ctx {
    nmo_object_t **objects;
    size_t count;
} query_collect_ctx_t;

typedef struct query_meta {
    nmo_object_id_t object_id;
    nmo_class_id_t class_id;
    size_t repository_index;
    nmo_object_t *object;
    char *folded_name;
} query_meta_t;

typedef struct query_id_entry {
    uint32_t key;
    size_t meta_index;
} query_id_entry_t;

typedef struct query_name_entry {
    const char *key;
    size_t meta_index;
} query_name_entry_t;

struct nmo_object_query_index {
    nmo_object_repository_t *repository;
    const nmo_type_registry_t *registry;
    nmo_allocator_t allocator;

    query_meta_t *metas;
    size_t meta_count;
    size_t meta_capacity;

    nmo_hash_table_t *id_to_meta;
    query_id_entry_t *class_entries;
    size_t class_count;
    size_t class_capacity;
    query_id_entry_t *derived_entries;
    size_t derived_count;
    size_t derived_capacity;
    query_name_entry_t *name_entries;
    size_t name_count;
    size_t name_capacity;
    query_name_entry_t *folded_name_entries;
    size_t folded_name_count;
    size_t folded_name_capacity;

    query_id_entry_t *trigram_entries;
    size_t trigram_count;
    size_t trigram_capacity;
    bool text_built;

    uint32_t *visit_marks;
    uint32_t visit_generation;

    bool eager_dirty;
    bool text_dirty;
};

typedef enum query_candidate_kind {
    QUERY_CANDIDATE_ALL = 0,
    QUERY_CANDIDATE_NONE,
    QUERY_CANDIDATE_SINGLE,
    QUERY_CANDIDATE_ID_ENTRIES,
    QUERY_CANDIDATE_NAME_ENTRIES
} query_candidate_kind_t;

typedef struct query_candidate {
    query_candidate_kind_t kind;
    size_t count;
    size_t single_meta_index;
    const query_id_entry_t *id_entries;
    const query_name_entry_t *name_entries;
} query_candidate_t;

static char query_fold_char(char c, bool icase)
{
    return icase ? (char)tolower((unsigned char)c) : c;
}

static char query_fold_ascii(char c)
{
    return (char)tolower((unsigned char)c);
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

static void *query_index_alloc(nmo_object_query_index_t *index, size_t size, size_t align)
{
    if (index == NULL || size == 0) {
        return NULL;
    }
    return nmo_alloc(&index->allocator, size, align);
}

static void query_index_free(nmo_object_query_index_t *index, void *ptr)
{
    if (index != NULL && ptr != NULL) {
        nmo_free(&index->allocator, ptr);
    }
}

static void query_index_free_meta_names(nmo_object_query_index_t *index)
{
    if (index == NULL || index->metas == NULL) {
        return;
    }
    for (size_t i = 0; i < index->meta_count; i++) {
        query_index_free(index, index->metas[i].folded_name);
        index->metas[i].folded_name = NULL;
    }
}

static void query_index_clear_storage(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return;
    }

    query_index_free_meta_names(index);
    query_index_free(index, index->metas);
    query_index_free(index, index->class_entries);
    query_index_free(index, index->derived_entries);
    query_index_free(index, index->name_entries);
    query_index_free(index, index->folded_name_entries);
    query_index_free(index, index->trigram_entries);
    query_index_free(index, index->visit_marks);
    if (index->id_to_meta != NULL) {
        nmo_hash_table_destroy(index->id_to_meta);
    }

    index->metas = NULL;
    index->meta_count = 0;
    index->meta_capacity = 0;
    index->id_to_meta = NULL;
    index->class_entries = NULL;
    index->class_count = 0;
    index->class_capacity = 0;
    index->derived_entries = NULL;
    index->derived_count = 0;
    index->derived_capacity = 0;
    index->name_entries = NULL;
    index->name_count = 0;
    index->name_capacity = 0;
    index->folded_name_entries = NULL;
    index->folded_name_count = 0;
    index->folded_name_capacity = 0;
    index->trigram_entries = NULL;
    index->trigram_count = 0;
    index->trigram_capacity = 0;
    index->visit_marks = NULL;
    index->visit_generation = 0;
    index->text_built = false;
    index->text_dirty = true;
}

static nmo_status_t query_reserve_bytes(
    nmo_object_query_index_t *index,
    void **ptr,
    size_t *capacity,
    size_t required,
    size_t element_size,
    size_t align)
{
    if (required <= *capacity) {
        return NMO_OK;
    }
    size_t new_capacity = *capacity == 0 ? 16 : *capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2) {
            return NMO_ERR_NOMEM;
        }
        new_capacity *= 2;
    }
    if (new_capacity > SIZE_MAX / element_size) {
        return NMO_ERR_NOMEM;
    }
    void *new_ptr = query_index_alloc(index, new_capacity * element_size, align);
    if (new_ptr == NULL) {
        return NMO_ERR_NOMEM;
    }
    if (*ptr != NULL && *capacity > 0) {
        memcpy(new_ptr, *ptr, *capacity * element_size);
        query_index_free(index, *ptr);
    }
    *ptr = new_ptr;
    *capacity = new_capacity;
    return NMO_OK;
}

static nmo_status_t query_append_id_entry(
    nmo_object_query_index_t *index,
    query_id_entry_t **entries,
    size_t *count,
    size_t *capacity,
    uint32_t key,
    size_t meta_index)
{
    nmo_status_t status = query_reserve_bytes(
        index, (void **)entries, capacity, *count + 1,
        sizeof(**entries), _Alignof(query_id_entry_t));
    if (status != NMO_OK) {
        return status;
    }
    (*entries)[*count] = (query_id_entry_t){ key, meta_index };
    (*count)++;
    return NMO_OK;
}

static nmo_status_t query_append_name_entry(
    nmo_object_query_index_t *index,
    query_name_entry_t **entries,
    size_t *count,
    size_t *capacity,
    const char *key,
    size_t meta_index)
{
    nmo_status_t status = query_reserve_bytes(
        index, (void **)entries, capacity, *count + 1,
        sizeof(**entries), _Alignof(query_name_entry_t));
    if (status != NMO_OK) {
        return status;
    }
    (*entries)[*count] = (query_name_entry_t){ key, meta_index };
    (*count)++;
    return NMO_OK;
}

static int compare_id_entry(const void *a, const void *b)
{
    const query_id_entry_t *ea = (const query_id_entry_t *)a;
    const query_id_entry_t *eb = (const query_id_entry_t *)b;
    if (ea->key < eb->key) return -1;
    if (ea->key > eb->key) return 1;
    if (ea->meta_index < eb->meta_index) return -1;
    if (ea->meta_index > eb->meta_index) return 1;
    return 0;
}

static int compare_name_entry(const void *a, const void *b)
{
    const query_name_entry_t *ea = (const query_name_entry_t *)a;
    const query_name_entry_t *eb = (const query_name_entry_t *)b;
    int cmp = strcmp(ea->key != NULL ? ea->key : "", eb->key != NULL ? eb->key : "");
    if (cmp != 0) return cmp;
    if (ea->meta_index < eb->meta_index) return -1;
    if (ea->meta_index > eb->meta_index) return 1;
    return 0;
}

static char *query_fold_copy(nmo_object_query_index_t *index, const char *name)
{
    const char *src = name != NULL ? name : "";
    size_t len = strlen(src);
    char *copy = (char *)query_index_alloc(index, len + 1, _Alignof(char));
    if (copy == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        copy[i] = query_fold_ascii(src[i]);
    }
    copy[len] = '\0';
    return copy;
}

static uint32_t query_make_trigram(const char *s)
{
    return ((uint32_t)(unsigned char)s[0] << 16) |
           ((uint32_t)(unsigned char)s[1] << 8) |
           ((uint32_t)(unsigned char)s[2]);
}

static nmo_status_t query_index_add_derived_chain(
    nmo_object_query_index_t *index,
    nmo_class_id_t class_id,
    size_t meta_index)
{
    nmo_class_id_t current = class_id;
    for (size_t depth = 0; current != 0 && depth < 64; depth++) {
        nmo_status_t status = query_append_id_entry(
            index,
            &index->derived_entries,
            &index->derived_count,
            &index->derived_capacity,
            current,
            meta_index);
        if (status != NMO_OK) {
            return status;
        }
        if (index->registry == NULL) {
            break;
        }
        nmo_class_id_t parent =
            nmo_type_query_class_get_parent(index->registry, current);
        if (parent == 0 || parent == current) {
            break;
        }
        current = parent;
    }
    return NMO_OK;
}

nmo_object_query_index_t *nmo_object_query_index_create(
    nmo_object_repository_t *repository,
    const nmo_type_registry_t *registry,
    const nmo_allocator_t *allocator)
{
    if (repository == NULL) {
        return NULL;
    }
    nmo_allocator_t alloc = allocator != NULL ? *allocator : nmo_allocator_default();
    nmo_object_query_index_t *index = (nmo_object_query_index_t *)nmo_alloc(
        &alloc, sizeof(*index), _Alignof(nmo_object_query_index_t));
    if (index == NULL) {
        return NULL;
    }
    memset(index, 0, sizeof(*index));
    index->repository = repository;
    index->registry = registry;
    index->allocator = alloc;
    index->eager_dirty = true;
    index->text_dirty = true;
    return index;
}

void nmo_object_query_index_destroy(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return;
    }
    nmo_allocator_t alloc = index->allocator;
    query_index_clear_storage(index);
    nmo_free(&alloc, index);
}

nmo_status_t nmo_object_query_index_rebuild(nmo_object_query_index_t *index)
{
    if (index == NULL || index->repository == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    query_index_clear_storage(index);

    size_t total = nmo_object_repository_get_count(index->repository);
    if (total == 0) {
        index->eager_dirty = false;
        index->text_dirty = true;
        return NMO_OK;
    }

    index->metas = (query_meta_t *)query_index_alloc(
        index, total * sizeof(*index->metas), _Alignof(query_meta_t));
    index->visit_marks = (uint32_t *)query_index_alloc(
        index, total * sizeof(*index->visit_marks), _Alignof(uint32_t));
    if (index->metas == NULL || index->visit_marks == NULL) {
        query_index_clear_storage(index);
        return NMO_ERR_NOMEM;
    }
    memset(index->metas, 0, total * sizeof(*index->metas));
    memset(index->visit_marks, 0, total * sizeof(*index->visit_marks));
    index->meta_capacity = total;

    index->id_to_meta = nmo_hash_table_create(
        &index->allocator,
        sizeof(nmo_object_id_t),
        sizeof(size_t),
        total * 2,
        nmo_hash_uint32,
        NULL);
    if (index->id_to_meta == NULL) {
        query_index_clear_storage(index);
        return NMO_ERR_NOMEM;
    }

    for (size_t i = 0; i < total; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(index->repository, i);
        if (object == NULL) {
            continue;
        }

        size_t meta_index = index->meta_count++;
        query_meta_t *meta = &index->metas[meta_index];
        meta->object_id = nmo_object_get_id(object);
        meta->class_id = nmo_object_get_class_id(object);
        meta->repository_index = i;
        meta->object = object;
        meta->folded_name = query_fold_copy(index, nmo_object_get_name(object));
        if (meta->folded_name == NULL) {
            query_index_clear_storage(index);
            return NMO_ERR_NOMEM;
        }

        nmo_status_t status =
            nmo_hash_table_insert(index->id_to_meta, &meta->object_id, &meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }

        status = query_append_id_entry(
            index, &index->class_entries, &index->class_count,
            &index->class_capacity, meta->class_id, meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }

        status = query_index_add_derived_chain(index, meta->class_id, meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }

        const char *name = nmo_object_get_name(object);
        status = query_append_name_entry(
            index, &index->name_entries, &index->name_count,
            &index->name_capacity, name != NULL ? name : "", meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }

        status = query_append_name_entry(
            index, &index->folded_name_entries, &index->folded_name_count,
            &index->folded_name_capacity, meta->folded_name, meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }
    }

    qsort(index->class_entries, index->class_count, sizeof(*index->class_entries), compare_id_entry);
    qsort(index->derived_entries, index->derived_count, sizeof(*index->derived_entries), compare_id_entry);
    qsort(index->name_entries, index->name_count, sizeof(*index->name_entries), compare_name_entry);
    qsort(index->folded_name_entries, index->folded_name_count, sizeof(*index->folded_name_entries), compare_name_entry);

    index->eager_dirty = false;
    index->text_dirty = true;
    index->text_built = false;
    return NMO_OK;
}

void nmo_object_query_index_invalidate(nmo_object_query_index_t *index, uint32_t flags)
{
    if (index == NULL) {
        return;
    }
    if (flags == 0 || (flags & NMO_OBJECT_QUERY_INDEX_ALL) != 0u ||
        (flags & (NMO_OBJECT_QUERY_INDEX_MEMBERSHIP | NMO_OBJECT_QUERY_INDEX_NAMES)) != 0u) {
        index->eager_dirty = true;
        index->text_dirty = true;
        return;
    }
    if ((flags & NMO_OBJECT_QUERY_INDEX_TEXT) != 0u) {
        index->text_dirty = true;
    }
}

static nmo_status_t query_index_ensure_eager(nmo_object_query_index_t *index)
{
    if (index == NULL || !index->eager_dirty) {
        return NMO_OK;
    }
    return nmo_object_query_index_rebuild(index);
}

static nmo_status_t query_index_build_text(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (index->text_built && !index->text_dirty) {
        return NMO_OK;
    }

    query_index_free(index, index->trigram_entries);
    index->trigram_entries = NULL;
    index->trigram_count = 0;
    index->trigram_capacity = 0;

    for (size_t i = 0; i < index->meta_count; i++) {
        const char *name = index->metas[i].folded_name != NULL ? index->metas[i].folded_name : "";
        size_t len = strlen(name);
        if (len < 3) {
            continue;
        }
        for (size_t pos = 0; pos + 2 < len; pos++) {
            uint32_t tri = query_make_trigram(name + pos);
            nmo_status_t status = query_append_id_entry(
                index, &index->trigram_entries, &index->trigram_count,
                &index->trigram_capacity, tri, i);
            if (status != NMO_OK) {
                return status;
            }
        }
    }

    qsort(index->trigram_entries, index->trigram_count, sizeof(*index->trigram_entries), compare_id_entry);
    index->text_built = true;
    index->text_dirty = false;
    return NMO_OK;
}

static bool query_id_range(
    const query_id_entry_t *entries,
    size_t count,
    uint32_t key,
    size_t *out_start,
    size_t *out_count)
{
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (entries[mid].key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    size_t start = lo;
    while (lo < count && entries[lo].key == key) {
        lo++;
    }
    *out_start = start;
    *out_count = lo - start;
    return *out_count > 0;
}

static bool query_name_range(
    const query_name_entry_t *entries,
    size_t count,
    const char *key,
    size_t *out_start,
    size_t *out_count)
{
    const char *needle = key != NULL ? key : "";
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *entry_key = entries[mid].key != NULL ? entries[mid].key : "";
        if (strcmp(entry_key, needle) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    size_t start = lo;
    while (lo < count) {
        const char *entry_key = entries[lo].key != NULL ? entries[lo].key : "";
        if (strcmp(entry_key, needle) != 0) {
            break;
        }
        lo++;
    }
    *out_start = start;
    *out_count = lo - start;
    return *out_count > 0;
}

static query_candidate_t query_all_candidate(void)
{
    return (query_candidate_t){ .kind = QUERY_CANDIDATE_ALL };
}

static query_candidate_t query_none_candidate(void)
{
    return (query_candidate_t){ .kind = QUERY_CANDIDATE_NONE };
}

static query_candidate_t query_id_entries_candidate(
    const query_id_entry_t *entries,
    size_t count)
{
    return (query_candidate_t){
        .kind = count == 0 ? QUERY_CANDIDATE_NONE : QUERY_CANDIDATE_ID_ENTRIES,
        .count = count,
        .id_entries = entries
    };
}

static query_candidate_t query_name_entries_candidate(
    const query_name_entry_t *entries,
    size_t count)
{
    return (query_candidate_t){
        .kind = count == 0 ? QUERY_CANDIDATE_NONE : QUERY_CANDIDATE_NAME_ENTRIES,
        .count = count,
        .name_entries = entries
    };
}

static query_candidate_t query_single_candidate(size_t meta_index)
{
    return (query_candidate_t){
        .kind = QUERY_CANDIDATE_SINGLE,
        .count = 1,
        .single_meta_index = meta_index
    };
}

static size_t query_candidate_count(const query_candidate_t *candidate, size_t total)
{
    if (candidate == NULL) {
        return total;
    }
    switch (candidate->kind) {
    case QUERY_CANDIDATE_ALL:
        return total;
    case QUERY_CANDIDATE_NONE:
        return 0;
    case QUERY_CANDIDATE_SINGLE:
    case QUERY_CANDIDATE_ID_ENTRIES:
    case QUERY_CANDIDATE_NAME_ENTRIES:
        return candidate->count;
    default:
        return total;
    }
}

static query_candidate_t query_choose_smaller(
    query_candidate_t current,
    query_candidate_t next,
    size_t total)
{
    if (next.kind == QUERY_CANDIDATE_ALL) {
        return current;
    }
    if (current.kind == QUERY_CANDIDATE_ALL) {
        return next;
    }
    return query_candidate_count(&next, total) < query_candidate_count(&current, total)
        ? next
        : current;
}

static bool query_extract_wildcard_literal(
    const char *pattern,
    char *out,
    size_t out_size)
{
    if (pattern == NULL || out == NULL || out_size == 0) {
        return false;
    }
    size_t best_start = 0;
    size_t best_len = 0;
    size_t cur_start = 0;
    size_t cur_len = 0;
    for (size_t i = 0;; i++) {
        char c = pattern[i];
        if (c == '\0' || c == '*' || c == '?') {
            if (cur_len > best_len) {
                best_start = cur_start;
                best_len = cur_len;
            }
            cur_len = 0;
            cur_start = i + 1;
            if (c == '\0') {
                break;
            }
        } else {
            if (cur_len == 0) {
                cur_start = i;
            }
            cur_len++;
        }
    }
    if (best_len < 3) {
        out[0] = '\0';
        return false;
    }
    if (best_len >= out_size) {
        best_len = out_size - 1;
    }
    for (size_t i = 0; i < best_len; i++) {
        out[i] = query_fold_ascii(pattern[best_start + i]);
    }
    out[best_len] = '\0';
    return true;
}

static bool query_extract_regex_literal(
    const char *pattern,
    char *out,
    size_t out_size)
{
    if (pattern == NULL || out == NULL || out_size == 0) {
        return false;
    }
    char best[256];
    char cur[256];
    size_t best_len = 0;
    size_t cur_len = 0;
    const char *meta = ".^$*+?[](){}|";

    for (size_t i = 0;; i++) {
        char c = pattern[i];
        bool end = c == '\0';
        bool literal = false;
        char literal_char = c;
        if (end) {
            literal = false;
        } else if (c == '\\' && pattern[i + 1] != '\0') {
            literal = true;
            literal_char = pattern[++i];
        } else if (strchr(meta, c) == NULL) {
            literal = true;
        }

        if (literal) {
            if (cur_len + 1 < sizeof(cur)) {
                cur[cur_len++] = query_fold_ascii(literal_char);
            }
        } else {
            if (cur_len > best_len) {
                best_len = cur_len;
                memcpy(best, cur, best_len);
            }
            cur_len = 0;
            if (end) {
                break;
            }
        }
    }

    if (best_len < 3) {
        out[0] = '\0';
        return false;
    }
    if (best_len >= out_size) {
        best_len = out_size - 1;
    }
    memcpy(out, best, best_len);
    out[best_len] = '\0';
    return true;
}

static void query_fold_pattern(const char *pattern, bool icase, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    const char *src = pattern != NULL ? pattern : "";
    size_t i = 0;
    for (; i + 1 < out_size && src[i] != '\0'; i++) {
        out[i] = icase ? query_fold_ascii(src[i]) : src[i];
    }
    out[i] = '\0';
}

static nmo_status_t query_text_candidate_for_literal(
    nmo_object_query_index_t *index,
    const char *literal,
    query_candidate_t *out_candidate)
{
    *out_candidate = query_all_candidate();
    if (index == NULL || literal == NULL || strlen(literal) < 3) {
        return NMO_OK;
    }
    nmo_status_t status = query_index_build_text(index);
    if (status != NMO_OK) {
        return status;
    }

    size_t len = strlen(literal);
    bool found_any = false;
    size_t best_start = 0;
    size_t best_count = 0;
    for (size_t i = 0; i + 2 < len; i++) {
        uint32_t tri = query_make_trigram(literal + i);
        size_t start = 0;
        size_t count = 0;
        if (!query_id_range(index->trigram_entries, index->trigram_count, tri, &start, &count)) {
            *out_candidate = query_none_candidate();
            return NMO_OK;
        }
        if (!found_any || count < best_count) {
            found_any = true;
            best_start = start;
            best_count = count;
        }
    }
    if (found_any) {
        *out_candidate = query_id_entries_candidate(
            index->trigram_entries + best_start, best_count);
    }
    return NMO_OK;
}

static nmo_status_t query_plan_candidate(
    nmo_object_query_index_t *index,
    const nmo_object_query_t *query,
    query_candidate_t *out_candidate)
{
    *out_candidate = query_all_candidate();
    if (index == NULL || query == NULL) {
        return NMO_OK;
    }

    nmo_status_t status = query_index_ensure_eager(index);
    if (status != NMO_OK) {
        return status;
    }

    size_t total = index->meta_count;
    query_candidate_t candidate = query_all_candidate();

    if (query->object_id != 0) {
        size_t meta_index = 0;
        if (index->id_to_meta == NULL ||
            nmo_hash_table_get(index->id_to_meta, &query->object_id, &meta_index) != NMO_OK) {
            *out_candidate = query_none_candidate();
            return NMO_OK;
        }
        *out_candidate = query_single_candidate(meta_index);
        return NMO_OK;
    }

    if (query->class_id != 0) {
        const query_id_entry_t *entries =
            query->include_derived_classes ? index->derived_entries : index->class_entries;
        size_t count =
            query->include_derived_classes ? index->derived_count : index->class_count;
        size_t start = 0;
        size_t range_count = 0;
        query_candidate_t class_candidate = query_none_candidate();
        if (query_id_range(entries, count, query->class_id, &start, &range_count)) {
            class_candidate = query_id_entries_candidate(entries + start, range_count);
        }
        candidate = query_choose_smaller(candidate, class_candidate, total);
    }

    if (query->name_mode == NMO_OBJECT_QUERY_NAME_EXACT) {
        char folded[512];
        const char *needle = query->name != NULL ? query->name : "";
        const query_name_entry_t *entries = index->name_entries;
        size_t count = index->name_count;
        if (query->name_case_insensitive) {
            if (strlen(needle) >= sizeof(folded)) {
                *out_candidate = candidate;
                return NMO_OK;
            }
            query_fold_pattern(needle, true, folded, sizeof(folded));
            needle = folded;
            entries = index->folded_name_entries;
            count = index->folded_name_count;
        }
        size_t start = 0;
        size_t range_count = 0;
        query_candidate_t name_candidate = query_none_candidate();
        if (query_name_range(entries, count, needle, &start, &range_count)) {
            name_candidate = query_name_entries_candidate(entries + start, range_count);
        }
        candidate = query_choose_smaller(candidate, name_candidate, total);
    } else if (query->name_mode == NMO_OBJECT_QUERY_NAME_SUBSTRING) {
        char folded[512];
        query_fold_pattern(query->name, true, folded, sizeof(folded));
        query_candidate_t text_candidate = query_all_candidate();
        status = query_text_candidate_for_literal(index, folded, &text_candidate);
        if (status != NMO_OK) {
            return status;
        }
        candidate = query_choose_smaller(candidate, text_candidate, total);
    } else if (query->name_mode == NMO_OBJECT_QUERY_NAME_WILDCARD) {
        char literal[512];
        if (query_extract_wildcard_literal(query->name, literal, sizeof(literal))) {
            query_candidate_t text_candidate = query_all_candidate();
            status = query_text_candidate_for_literal(index, literal, &text_candidate);
            if (status != NMO_OK) {
                return status;
            }
            candidate = query_choose_smaller(candidate, text_candidate, total);
        }
    } else if (query->name_mode == NMO_OBJECT_QUERY_NAME_REGEX) {
        char literal[512];
        if (query_extract_regex_literal(query->name, literal, sizeof(literal))) {
            query_candidate_t text_candidate = query_all_candidate();
            status = query_text_candidate_for_literal(index, literal, &text_candidate);
            if (status != NMO_OK) {
                return status;
            }
            candidate = query_choose_smaller(candidate, text_candidate, total);
        }
    }

    *out_candidate = candidate;
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
        return query->name_mode == NMO_OBJECT_QUERY_NAME_NONE;
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

    query_index_next_generation(index);
    for (size_t i = 0; i < candidate->count; i++) {
        size_t meta_index = candidate->single_meta_index;
        if (candidate->kind == QUERY_CANDIDATE_ID_ENTRIES) {
            meta_index = candidate->id_entries[i].meta_index;
        } else if (candidate->kind == QUERY_CANDIDATE_NAME_ENTRIES) {
            meta_index = candidate->name_entries[i].meta_index;
        }
        if (meta_index >= index->meta_count ||
            !query_index_mark_meta(index, meta_index)) {
            continue;
        }
        query_meta_t *meta = &index->metas[meta_index];
        nmo_object_t *object =
            nmo_object_repository_find_by_id(ctx->repository, meta->object_id);
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
        nmo_status_t status = query_plan_candidate(ctx->index, query, &candidate);
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
