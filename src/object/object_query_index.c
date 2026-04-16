/**
 * @file object_query_index.c
 * @brief Session-retained object query index implementation.
 */

#include "object_query_internal.h"

#include "core/nmo_hash.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"

#include <stdlib.h>
#include <string.h>

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

static nmo_status_t query_index_init_entry_arrays(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t status = nmo_array_init(
        &index->class_entries, sizeof(query_id_entry_t), 0, &index->allocator);
    if (status != NMO_OK) {
        return status;
    }
    status = nmo_array_init(
        &index->derived_entries, sizeof(query_id_entry_t), 0, &index->allocator);
    if (status != NMO_OK) {
        nmo_array_dispose(&index->class_entries);
        return status;
    }
    status = nmo_array_init(
        &index->name_entries, sizeof(query_name_entry_t), 0, &index->allocator);
    if (status != NMO_OK) {
        nmo_array_dispose(&index->derived_entries);
        nmo_array_dispose(&index->class_entries);
        return status;
    }
    status = nmo_array_init(
        &index->folded_name_entries, sizeof(query_name_entry_t), 0, &index->allocator);
    if (status != NMO_OK) {
        nmo_array_dispose(&index->name_entries);
        nmo_array_dispose(&index->derived_entries);
        nmo_array_dispose(&index->class_entries);
        return status;
    }
    status = nmo_array_init(
        &index->trigram_entries, sizeof(query_id_entry_t), 0, &index->allocator);
    if (status != NMO_OK) {
        nmo_array_dispose(&index->folded_name_entries);
        nmo_array_dispose(&index->name_entries);
        nmo_array_dispose(&index->derived_entries);
        nmo_array_dispose(&index->class_entries);
        return status;
    }
    status = nmo_array_init(
        &index->text_candidate_entries, sizeof(query_id_entry_t), 0, &index->allocator);
    if (status != NMO_OK) {
        nmo_array_dispose(&index->trigram_entries);
        nmo_array_dispose(&index->folded_name_entries);
        nmo_array_dispose(&index->name_entries);
        nmo_array_dispose(&index->derived_entries);
        nmo_array_dispose(&index->class_entries);
        return status;
    }
    return NMO_OK;
}

static void query_index_clear_entry_arrays(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return;
    }
    nmo_array_clear(&index->class_entries);
    nmo_array_clear(&index->derived_entries);
    nmo_array_clear(&index->name_entries);
    nmo_array_clear(&index->folded_name_entries);
    nmo_array_clear(&index->trigram_entries);
    nmo_array_clear(&index->text_candidate_entries);
}

static void query_index_dispose_entry_arrays(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return;
    }
    nmo_array_dispose(&index->text_candidate_entries);
    nmo_array_dispose(&index->trigram_entries);
    nmo_array_dispose(&index->folded_name_entries);
    nmo_array_dispose(&index->name_entries);
    nmo_array_dispose(&index->derived_entries);
    nmo_array_dispose(&index->class_entries);
}

static void query_index_dispose_name_slab(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return;
    }
    query_index_free(index, index->folded_name_slab);
    index->folded_name_slab = NULL;
    index->folded_name_slab_size = 0;
    index->folded_name_slab_capacity = 0;
}

static void query_index_clear_storage(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return;
    }

    query_index_free(index, index->metas);
    query_index_clear_entry_arrays(index);
    query_index_free(index, index->visit_marks);
    if (index->id_to_meta != NULL) {
        nmo_hash_table_destroy(index->id_to_meta);
    }

    index->metas = NULL;
    index->meta_count = 0;
    index->meta_capacity = 0;
    index->folded_name_slab_size = 0;
    index->id_to_meta = NULL;
    index->visit_marks = NULL;
    index->visit_generation = 0;
    index->text_built = false;
    index->text_dirty = true;
}

static void query_index_repository_mutated(
    nmo_object_repository_t *repository,
    uint32_t flags,
    void *user_data)
{
    nmo_object_query_index_t *index = (nmo_object_query_index_t *)user_data;
    if (index == NULL || index->repository != repository) {
        return;
    }

    uint32_t query_flags = 0;
    if ((flags & NMO_OBJECT_REPOSITORY_MUTATION_MEMBERSHIP) != 0u) {
        query_flags |= NMO_OBJECT_QUERY_INDEX_MEMBERSHIP;
    }
    if ((flags & NMO_OBJECT_REPOSITORY_MUTATION_NAMES) != 0u) {
        query_flags |= NMO_OBJECT_QUERY_INDEX_NAMES;
    }
    if (query_flags != 0u) {
        nmo_object_query_index_invalidate(index, query_flags);
    }
}

static nmo_status_t query_index_reserve_name_slab(
    nmo_object_query_index_t *index,
    size_t required)
{
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (required <= index->folded_name_slab_capacity) {
        index->folded_name_slab_size = 0;
        return NMO_OK;
    }

    char *new_slab = (char *)query_index_alloc(index, required, _Alignof(char));
    if (new_slab == NULL) {
        return NMO_ERR_NOMEM;
    }
    query_index_free(index, index->folded_name_slab);
    index->folded_name_slab = new_slab;
    index->folded_name_slab_capacity = required;
    index->folded_name_slab_size = 0;
    return NMO_OK;
}

static nmo_status_t query_index_prepare_name_slab(nmo_object_query_index_t *index)
{
    if (index == NULL || index->repository == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t total = nmo_object_repository_get_count(index->repository);
    size_t required = 0;
    for (size_t i = 0; i < total; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(index->repository, i);
        if (object == NULL) {
            continue;
        }
        const char *name = nmo_object_get_name(object);
        size_t len = strlen(name != NULL ? name : "");
        if (required > SIZE_MAX - len - 1) {
            return NMO_ERR_NOMEM;
        }
        required += len + 1;
    }
    return query_index_reserve_name_slab(index, required);
}

static nmo_status_t query_append_id_entry(
    nmo_array_t *entries,
    uint32_t key,
    size_t meta_index)
{
    query_id_entry_t entry = { key, meta_index };
    return nmo_array_append(entries, &entry);
}

static nmo_status_t query_append_name_entry(
    nmo_array_t *entries,
    const char *key,
    size_t meta_index)
{
    query_name_entry_t entry = { key, meta_index };
    return nmo_array_append(entries, &entry);
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

static void query_sort_id_entries(nmo_array_t *entries)
{
    size_t count = nmo_array_size(entries);
    if (count > 1) {
        qsort(nmo_array_data(entries), count, sizeof(query_id_entry_t), compare_id_entry);
    }
}

static void query_sort_name_entries(nmo_array_t *entries)
{
    size_t count = nmo_array_size(entries);
    if (count > 1) {
        qsort(nmo_array_data(entries), count, sizeof(query_name_entry_t), compare_name_entry);
    }
}

static char *query_fold_name_to_slab(nmo_object_query_index_t *index, const char *name)
{
    const char *src = name != NULL ? name : "";
    size_t len = strlen(src);
    if (index == NULL ||
        index->folded_name_slab == NULL ||
        index->folded_name_slab_size > SIZE_MAX - len - 1 ||
        index->folded_name_slab_size + len + 1 > index->folded_name_slab_capacity) {
        return NULL;
    }
    char *copy = index->folded_name_slab + index->folded_name_slab_size;
    for (size_t i = 0; i < len; i++) {
        copy[i] = query_fold_ascii(src[i]);
    }
    copy[len] = '\0';
    index->folded_name_slab_size += len + 1;
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
            &index->derived_entries,
            current,
            meta_index);
        if (status != NMO_OK) {
            return status;
        }
        if (index->registry == NULL) {
            break;
        }
        nmo_class_id_t parent = (nmo_class_id_t)nmo_type_registry_get_class_parent(
            index->registry, current);
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
    if (query_index_init_entry_arrays(index) != NMO_OK) {
        nmo_free(&alloc, index);
        return NULL;
    }
    return index;
}

void nmo_object_query_index_destroy(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return;
    }
    nmo_allocator_t alloc = index->allocator;
    nmo_object_query_index_detach_repository_observer(index);
    query_index_clear_storage(index);
    query_index_dispose_name_slab(index);
    query_index_dispose_entry_arrays(index);
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

    nmo_status_t status = query_index_prepare_name_slab(index);
    if (status != NMO_OK) {
        query_index_clear_storage(index);
        return status;
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
        meta->folded_name = query_fold_name_to_slab(index, nmo_object_get_name(object));
        if (meta->folded_name == NULL) {
            query_index_clear_storage(index);
            return NMO_ERR_NOMEM;
        }

        status = nmo_hash_table_insert(index->id_to_meta, &meta->object_id, &meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }

        status = query_append_id_entry(
            &index->class_entries, meta->class_id, meta_index);
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
            &index->name_entries, name != NULL ? name : "", meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }

        status = query_append_name_entry(
            &index->folded_name_entries, meta->folded_name, meta_index);
        if (status != NMO_OK) {
            query_index_clear_storage(index);
            return status;
        }
    }

    query_sort_id_entries(&index->class_entries);
    query_sort_id_entries(&index->derived_entries);
    query_sort_name_entries(&index->name_entries);
    query_sort_name_entries(&index->folded_name_entries);

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

void nmo_object_query_index_trim(nmo_object_query_index_t *index, uint32_t flags)
{
    if (index == NULL) {
        return;
    }
    if (flags == 0 ||
        (flags & NMO_OBJECT_QUERY_INDEX_ALL) != 0u ||
        (flags & NMO_OBJECT_QUERY_INDEX_TEXT) != 0u) {
        nmo_array_dispose(&index->trigram_entries);
        nmo_array_dispose(&index->text_candidate_entries);
        index->text_built = false;
        index->text_dirty = true;
    }
}

nmo_status_t nmo_object_query_index_attach_repository_observer(
    nmo_object_query_index_t *index)
{
    if (index == NULL || index->repository == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (index->observer_attached) {
        return NMO_OK;
    }
    int add_result = nmo_object_repository_add_mutation_observer(
        index->repository,
        query_index_repository_mutated,
        index);
    if (add_result != NMO_OK) {
        return add_result;
    }
    index->observer_attached = true;
    return NMO_OK;
}

void nmo_object_query_index_detach_repository_observer(
    nmo_object_query_index_t *index)
{
    if (index == NULL || index->repository == NULL || !index->observer_attached) {
        return;
    }
    nmo_object_repository_remove_mutation_observer(
        index->repository,
        query_index_repository_mutated,
        index);
    index->observer_attached = false;
}

static nmo_status_t query_index_ensure_eager(nmo_object_query_index_t *index)
{
    if (index == NULL || !index->eager_dirty) {
        return NMO_OK;
    }
    return nmo_object_query_index_rebuild(index);
}

static nmo_status_t query_index_estimate_trigram_count(
    const nmo_object_query_index_t *index,
    size_t *out_count)
{
    if (index == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = 0;
    for (size_t i = 0; i < index->meta_count; i++) {
        const char *name = index->metas[i].folded_name != NULL ? index->metas[i].folded_name : "";
        size_t len = strlen(name);
        if (len < 3) {
            continue;
        }
        size_t trigrams = len - 2;
        if (count > SIZE_MAX - trigrams) {
            return NMO_ERR_NOMEM;
        }
        count += trigrams;
    }

    *out_count = count;
    return NMO_OK;
}

static nmo_status_t query_index_build_text(nmo_object_query_index_t *index)
{
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (index->text_built && !index->text_dirty) {
        return NMO_OK;
    }

    nmo_array_clear(&index->trigram_entries);
    size_t trigram_count = 0;
    nmo_status_t status = query_index_estimate_trigram_count(index, &trigram_count);
    if (status != NMO_OK) {
        return status;
    }
    status = nmo_array_reserve(&index->trigram_entries, trigram_count);
    if (status != NMO_OK) {
        return status;
    }

    for (size_t i = 0; i < index->meta_count; i++) {
        const char *name = index->metas[i].folded_name != NULL ? index->metas[i].folded_name : "";
        size_t len = strlen(name);
        if (len < 3) {
            continue;
        }
        for (size_t pos = 0; pos + 2 < len; pos++) {
            uint32_t tri = query_make_trigram(name + pos);
            status = query_append_id_entry(
                &index->trigram_entries, tri, i);
            if (status != NMO_OK) {
                return status;
            }
        }
    }

    query_sort_id_entries(&index->trigram_entries);
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

static uint32_t query_index_reserve_mark_generations(
    nmo_object_query_index_t *index,
    uint32_t count)
{
    if (index == NULL || index->visit_marks == NULL || count == 0) {
        return 0;
    }
    if (UINT32_MAX - index->visit_generation <= count) {
        memset(index->visit_marks, 0, index->meta_count * sizeof(*index->visit_marks));
        index->visit_generation = 1;
        return index->visit_generation;
    }
    index->visit_generation++;
    return index->visit_generation;
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
    enum { QUERY_MAX_LITERAL_TRIGRAMS = 512 };
    typedef struct query_trigram_range {
        uint32_t key;
        size_t start;
        size_t count;
    } query_trigram_range_t;

    *out_candidate = query_all_candidate();
    if (index == NULL || literal == NULL || strlen(literal) < 3) {
        return NMO_OK;
    }
    nmo_status_t status = query_index_build_text(index);
    if (status != NMO_OK) {
        return status;
    }

    size_t len = strlen(literal);
    const query_id_entry_t *trigram_entries =
        (const query_id_entry_t *)nmo_array_data(&index->trigram_entries);
    size_t trigram_count = nmo_array_size(&index->trigram_entries);
    query_trigram_range_t ranges[QUERY_MAX_LITERAL_TRIGRAMS];
    size_t range_count = 0;
    size_t best_range_index = 0;
    for (size_t i = 0; i + 2 < len; i++) {
        uint32_t tri = query_make_trigram(literal + i);
        bool duplicate = false;
        for (size_t j = 0; j < range_count; j++) {
            if (ranges[j].key == tri) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (range_count == QUERY_MAX_LITERAL_TRIGRAMS) {
            break;
        }

        size_t start = 0;
        size_t count = 0;
        if (!query_id_range(trigram_entries, trigram_count, tri, &start, &count)) {
            *out_candidate = query_none_candidate();
            return NMO_OK;
        }
        ranges[range_count] = (query_trigram_range_t){
            .key = tri,
            .start = start,
            .count = count
        };
        if (range_count == 0 || count < ranges[best_range_index].count) {
            best_range_index = range_count;
        }
        range_count++;
    }

    if (range_count == 1) {
        query_trigram_range_t *best = &ranges[best_range_index];
        *out_candidate = query_id_entries_candidate_with_duplicates(
            trigram_entries + best->start, best->count);
        return NMO_OK;
    }

    if (range_count > 1) {
        query_trigram_range_t *best = &ranges[best_range_index];
        nmo_array_clear(&index->text_candidate_entries);
        nmo_status_t reserve_status =
            nmo_array_reserve(&index->text_candidate_entries, best->count);
        if (reserve_status != NMO_OK) {
            return reserve_status;
        }

        uint32_t base_generation = query_index_reserve_mark_generations(
            index, (uint32_t)range_count + 2u);
        if (base_generation == 0) {
            *out_candidate = query_id_entries_candidate_with_duplicates(
                trigram_entries + best->start, best->count);
            return NMO_OK;
        }

        const query_id_entry_t *best_entries = trigram_entries + best->start;
        for (size_t i = 0; i < best->count; i++) {
            if (best_entries[i].meta_index < index->meta_count) {
                index->visit_marks[best_entries[i].meta_index] = base_generation;
            }
        }

        uint32_t current_generation = base_generation;
        for (size_t j = 0; j < range_count; j++) {
            if (j == best_range_index) {
                continue;
            }
            const query_trigram_range_t *range = &ranges[j];
            uint32_t next_generation = current_generation + 1u;
            const query_id_entry_t *range_entries = trigram_entries + range->start;
            for (size_t i = 0; i < range->count; i++) {
                size_t meta_index = range_entries[i].meta_index;
                if (meta_index < index->meta_count &&
                    index->visit_marks[meta_index] == current_generation) {
                    index->visit_marks[meta_index] = next_generation;
                }
            }
            current_generation = next_generation;
        }

        uint32_t output_generation = current_generation + 1u;
        for (size_t i = 0; i < best->count; i++) {
            size_t meta_index = best_entries[i].meta_index;
            if (meta_index >= index->meta_count ||
                index->visit_marks[meta_index] != current_generation) {
                continue;
            }
            index->visit_marks[meta_index] = output_generation;
            query_id_entry_t entry = { 0u, meta_index };
            nmo_status_t append_status =
                nmo_array_append(&index->text_candidate_entries, &entry);
            if (append_status != NMO_OK) {
                return append_status;
            }
        }

        *out_candidate = query_id_entries_candidate(
            (const query_id_entry_t *)nmo_array_data(&index->text_candidate_entries),
            nmo_array_size(&index->text_candidate_entries));
        index->visit_generation = output_generation;
    }
    return NMO_OK;
}

nmo_status_t query_plan_candidate(
    nmo_object_query_index_t *index,
    const nmo_type_registry_t *registry,
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

    if (query->class_id != 0 &&
        (!query->include_derived_classes ||
         (index->registry != NULL && index->registry == registry))) {
        const nmo_array_t *entry_array =
            query->include_derived_classes ? &index->derived_entries : &index->class_entries;
        const query_id_entry_t *entries =
            (const query_id_entry_t *)nmo_array_data(entry_array);
        size_t count = nmo_array_size(entry_array);
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
        const nmo_array_t *entry_array = &index->name_entries;
        const query_name_entry_t *entries =
            (const query_name_entry_t *)nmo_array_data(entry_array);
        size_t count = nmo_array_size(entry_array);
        if (query->name_case_insensitive) {
            if (strlen(needle) >= sizeof(folded)) {
                *out_candidate = candidate;
                return NMO_OK;
            }
            query_fold_pattern(needle, true, folded, sizeof(folded));
            needle = folded;
            entry_array = &index->folded_name_entries;
            entries = (const query_name_entry_t *)nmo_array_data(entry_array);
            count = nmo_array_size(entry_array);
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

