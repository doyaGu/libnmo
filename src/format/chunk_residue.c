// chunk_residue.c - Keep the content of a chunk that a schema does not model

#include "format/nmo_chunk_residue.h"
#include "core/nmo_utils.h"
#include "format/nmo_chunk_api.h"

#include <stdlib.h>
#include <string.h>

#define ID_SEQUENCE_MARKER 0xFFFFFFFFu

/* ---------------------------------------------------------------------------
 * Access helpers
 * --------------------------------------------------------------------------- */

static const uint32_t *chunk_words(const nmo_chunk_t *chunk)
{
    return chunk->data.count > 0 ? NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data) : NULL;
}

static const uint32_t *array_words(const nmo_arena_array_t *array)
{
    return array->count > 0 ? NMO_ARENA_ARRAY_DATA(uint32_t, array) : NULL;
}

/* ---------------------------------------------------------------------------
 * Digest
 * --------------------------------------------------------------------------- */

static uint64_t digest_update(uint64_t hash, const void *bytes, size_t size)
{
    const uint8_t *p = (const uint8_t *)bytes;
    for (size_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t digest_array(uint64_t hash, const nmo_arena_array_t *array)
{
    const uint64_t count = array->count;
    hash = digest_update(hash, &count, sizeof(count));
    return array->count > 0
        ? digest_update(hash, array->data, array->count * sizeof(uint32_t))
        : hash;
}

static uint64_t digest_chunk(uint64_t hash, const nmo_chunk_t *chunk)
{
    const uint32_t versions[2] = {chunk->data_version, chunk->chunk_version};
    hash = digest_update(hash, versions, sizeof(versions));
    hash = digest_array(hash, &chunk->data);
    hash = digest_array(hash, &chunk->ids);
    hash = digest_array(hash, &chunk->chunk_refs);
    hash = digest_array(hash, &chunk->managers);
    const uint64_t sub_count = chunk->chunks.count;
    hash = digest_update(hash, &sub_count, sizeof(sub_count));
    nmo_chunk_t *const *subs = chunk->chunks.count > 0
        ? (nmo_chunk_t *const *)chunk->chunks.data : NULL;
    for (size_t i = 0; i < chunk->chunks.count; ++i) {
        if (subs[i] != NULL) hash = digest_chunk(hash, subs[i]);
    }
    return hash;
}

uint64_t nmo_chunk_digest(const nmo_chunk_t *chunk)
{
    return chunk == NULL ? 0u : digest_chunk(14695981039346656037ull, chunk);
}

/* ---------------------------------------------------------------------------
 * Equivalence
 * --------------------------------------------------------------------------- */

/* A dword equals its copy with the upper bytes cleared. */
static bool same_with_padding(uint32_t original, uint32_t canonical)
{
    for (unsigned kept = 1; kept < 4; ++kept) {
        const uint32_t mask = 0xFFFFFFFFu >> (32 - 8 * kept);
        if ((original & mask) == canonical) return true;
    }
    return false;
}

/* Marks the dwords of a chunk that hold object ids. */
static bool mark_id_positions(const nmo_chunk_t *chunk, uint8_t *marks)
{
    const uint32_t *ids = array_words(&chunk->ids);
    const uint32_t *data = chunk_words(chunk);
    for (size_t i = 0; i < chunk->ids.count; ++i) {
        if (ids[i] != ID_SEQUENCE_MARKER) {
            if (ids[i] >= chunk->data.count) return false;
            marks[ids[i]] = 1;
            continue;
        }
        if (++i >= chunk->ids.count || ids[i] >= chunk->data.count) return false;
        const size_t header = ids[i];
        const size_t count = data[header];
        if (count > chunk->data.count - header - 1u) return false;
        for (size_t k = 0; k < count; ++k) marks[header + 1u + k] = 1;
    }
    return true;
}

static bool arrays_equal(const nmo_arena_array_t *a, const nmo_arena_array_t *b)
{
    return a->count == b->count &&
        (a->count == 0 || memcmp(a->data, b->data, a->count * sizeof(uint32_t)) == 0);
}

static bool equivalent_impl(const nmo_chunk_t *a, const nmo_chunk_t *b, bool a_untracked)
{
    if (a == NULL || b == NULL) return a == b;
    if (a->data_version != b->data_version || a->chunk_version != b->chunk_version ||
        a->data.count != b->data.count || !arrays_equal(&a->chunk_refs, &b->chunk_refs) ||
        a->managers.count != b->managers.count ||
        (!a_untracked && (a->chunks.count != b->chunks.count || a->ids.count != b->ids.count))) {
        return false;
    }

    const size_t count = a->data.count;
    uint8_t *marks = count > 0 ? (uint8_t *)calloc(count, 1) : NULL;
    if (count > 0 && marks == NULL) return false;
    bool equal = count == 0 || mark_id_positions(a_untracked ? b : a, marks);
    if (equal && !a_untracked && a->ids.count > 0) {
        /* The id positions must agree; their values are not compared. */
        equal = arrays_equal(&a->ids, &b->ids);
    }
    if (equal && count > 0) {
        const uint32_t *da = chunk_words(a);
        const uint32_t *db = chunk_words(b);
        for (size_t i = 0; i < count && equal; ++i) {
            if (marks[i] || da[i] == db[i]) continue;
            /* A null id is 0xFFFFFFFF in a chunk written for a file and 0 in
               one written without (which does not track it either). */
            if (da[i] == 0xFFFFFFFFu && db[i] == 0u) continue;
            equal = same_with_padding(da[i], db[i]);
        }
    }
    free(marks);
    if (!equal) return false;
    /* A chunk read from a file holds its sub-chunks only inside its data. */
    if (a_untracked) return true;

    nmo_chunk_t *const *sa = a->chunks.count > 0 ? (nmo_chunk_t *const *)a->chunks.data : NULL;
    nmo_chunk_t *const *sb = b->chunks.count > 0 ? (nmo_chunk_t *const *)b->chunks.data : NULL;
    for (size_t i = 0; i < a->chunks.count; ++i) {
        if (!nmo_chunk_equivalent(sa[i], sb[i])) return false;
    }
    return true;
}

bool nmo_chunk_equivalent(const nmo_chunk_t *a, const nmo_chunk_t *b)
{
    return equivalent_impl(a, b, false);
}

bool nmo_chunk_equivalent_to_tracked(const nmo_chunk_t *untracked, const nmo_chunk_t *tracked)
{
    return equivalent_impl(untracked, tracked, true);
}

/* ---------------------------------------------------------------------------
 * Sections
 * --------------------------------------------------------------------------- */

typedef struct section {
    uint32_t id;
    size_t start;   /* position of the identifier dword */
    size_t end;     /* position after the last payload dword */
} section_t;

typedef struct section_list {
    section_t *items;
    size_t count;
} section_list_t;

/* Sections form the chain [id][next] payload ...; the last next is 0. */
static bool parse_sections(const nmo_chunk_t *chunk, nmo_arena_t *arena, section_list_t *out)
{
    out->items = NULL;
    out->count = 0;
    const uint32_t *data = chunk_words(chunk);
    const size_t total = chunk->data.count;
    size_t capacity = 0;
    size_t pos = 0;
    while (pos + 1 < total) {
        const uint32_t next = data[pos + 1];
        const size_t end = (next > pos + 1 && next <= total) ? next : total;
        if (out->count == capacity) {
            const size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            section_t *grown = nmo_arena_alloc(arena, new_capacity * sizeof(section_t), _Alignof(section_t));
            if (grown == NULL) return false;
            if (out->count > 0) memcpy(grown, out->items, out->count * sizeof(section_t));
            out->items = grown;
            capacity = new_capacity;
        }
        out->items[out->count].id = data[pos];
        out->items[out->count].start = pos;
        out->items[out->count].end = end;
        out->count++;
        if (end >= total) break;
        pos = end;
    }
    return true;
}

static const section_t *find_section(const section_list_t *list, uint32_t id)
{
    for (size_t i = 0; i < list->count; ++i) {
        if (list->items[i].id == id) return &list->items[i];
    }
    return NULL;
}

static size_t payload_dwords(const section_t *s)
{
    return s->end - s->start - 2u;
}

/* ---------------------------------------------------------------------------
 * Residue fragments
 * --------------------------------------------------------------------------- */

typedef struct fragment {
    size_t source_start;     /* range of original copied */
    size_t source_end;
    uint32_t target_id;      /* section of the target the fragment extends; 0 = new section */
    bool whole_section;
} fragment_t;

static bool list_touches_range(const nmo_arena_array_t *list, size_t start, size_t end)
{
    const uint32_t *values = array_words(list);
    for (size_t i = 0; i < list->count; ++i) {
        if (values[i] >= start && values[i] < end) return true;
    }
    return false;
}

/* The fragment can be copied when nothing in it refers to a sub-chunk or a manager. */
static bool fragment_is_plain(const nmo_chunk_t *original, size_t start, size_t end)
{
    return !list_touches_range(&original->chunk_refs, start, end) &&
           !list_touches_range(&original->managers, start, end);
}

typedef struct id_entry {
    size_t pos;
    bool sequence;
} id_entry_t;

static int compare_id_entries(const void *a, const void *b)
{
    const size_t pa = ((const id_entry_t *)a)->pos;
    const size_t pb = ((const id_entry_t *)b)->pos;
    return pa < pb ? -1 : (pa > pb ? 1 : 0);
}

/* ---------------------------------------------------------------------------
 * Translating ids
 * --------------------------------------------------------------------------- */

static void translate_one(uint32_t *word, const nmo_id_remap_t *map,
                          uint32_t unmapped_limit, bool *unresolved)
{
    if (*word == ID_SEQUENCE_MARKER) return;
    nmo_object_id_t mapped = 0;
    if (nmo_id_remap_lookup_id(map, *word, &mapped) == NMO_OK) {
        *word = mapped;
    } else if (*word < unmapped_limit) {
        *unresolved = true;
    }
}

nmo_status_t nmo_chunk_translate_ids_with_layout(
    nmo_chunk_t *chunk,
    const nmo_chunk_t *layout,
    const nmo_id_remap_t *map,
    uint32_t unmapped_limit,
    bool *out_unresolved)
{
    if (chunk == NULL || layout == NULL || map == NULL || out_unresolved == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (layout->chunks.count != chunk->chunks.count) return NMO_ERR_INVALID_STATE;
    uint32_t *data = chunk->data.count > 0 ? NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data) : NULL;
    const uint32_t *ids = array_words(&layout->ids);
    for (size_t i = 0; i < layout->ids.count; ++i) {
        if (ids[i] != ID_SEQUENCE_MARKER) {
            if (ids[i] >= chunk->data.count) return NMO_ERR_INVALID_STATE;
            translate_one(&data[ids[i]], map, unmapped_limit, out_unresolved);
            continue;
        }
        if (++i >= layout->ids.count || ids[i] >= chunk->data.count) return NMO_ERR_INVALID_STATE;
        const size_t header = ids[i];
        const size_t count = data[header];
        if (count > chunk->data.count - header - 1u) return NMO_ERR_INVALID_STATE;
        for (size_t k = 0; k < count; ++k) {
            translate_one(&data[header + 1u + k], map, unmapped_limit, out_unresolved);
        }
    }
    nmo_chunk_t **subs = chunk->chunks.count > 0 ? (nmo_chunk_t **)chunk->chunks.data : NULL;
    nmo_chunk_t **layout_subs =
        layout->chunks.count > 0 ? (nmo_chunk_t **)layout->chunks.data : NULL;
    for (size_t i = 0; i < chunk->chunks.count; ++i) {
        if (subs[i] == NULL || layout_subs[i] == NULL) {
            if (subs[i] != layout_subs[i]) return NMO_ERR_INVALID_STATE;
            continue;
        }
        nmo_status_t status = nmo_chunk_translate_ids_with_layout(
            subs[i], layout_subs[i], map, unmapped_limit, out_unresolved);
        if (status != NMO_OK) return status;
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_translate_ids(
    nmo_chunk_t *chunk,
    const nmo_id_remap_t *map,
    uint32_t unmapped_limit,
    bool *out_unresolved)
{
    return nmo_chunk_translate_ids_with_layout(chunk, chunk, map, unmapped_limit, out_unresolved);
}

/* ---------------------------------------------------------------------------
 * Merge
 * --------------------------------------------------------------------------- */

static uint32_t translate_id(const nmo_id_remap_t *map, uint32_t value)
{
    nmo_object_id_t mapped = 0;
    if (map != NULL && nmo_id_remap_lookup_id(map, value, &mapped) == NMO_OK && mapped != 0) {
        return mapped;
    }
    return value;
}

/* Position of a dword of the target after its sections were rebuilt. */
static size_t map_position(const section_list_t *sections, const size_t *new_starts, size_t pos)
{
    for (size_t i = 0; i < sections->count; ++i) {
        if (pos >= sections->items[i].start && pos < sections->items[i].end) {
            return new_starts[i] + (pos - sections->items[i].start);
        }
    }
    return pos;
}

/* Copies a fragment of the original to out[dest] and records its object ids. */
static void copy_fragment(
    const fragment_t *frag,
    const nmo_chunk_t *original,
    const uint32_t *o_data,
    const nmo_id_remap_t *map,
    uint32_t *out,
    size_t dest,
    id_entry_t *entries,
    size_t *entry_count)
{
    memcpy(out + dest, o_data + frag->source_start,
           (frag->source_end - frag->source_start) * sizeof(uint32_t));
    const uint32_t *ids = array_words(&original->ids);
    for (size_t k = 0; k < original->ids.count; ++k) {
        if (ids[k] != ID_SEQUENCE_MARKER) {
            if (ids[k] >= frag->source_start && ids[k] < frag->source_end) {
                const size_t np = dest + (ids[k] - frag->source_start);
                out[np] = translate_id(map, out[np]);
                entries[(*entry_count)++] = (id_entry_t){np, false};
            }
            continue;
        }
        if (++k >= original->ids.count) break;
        const size_t header = ids[k];
        if (header < frag->source_start || header >= frag->source_end) continue;
        const size_t new_header = dest + (header - frag->source_start);
        const size_t count = out[new_header];
        if (count > frag->source_end - header - 1u) continue;
        for (size_t m = 0; m < count; ++m) {
            out[new_header + 1u + m] = translate_id(map, out[new_header + 1u + m]);
        }
        entries[(*entry_count)++] = (id_entry_t){new_header, true};
    }
}

/* ---------------------------------------------------------------------------
 * Values the edit did not touch
 * --------------------------------------------------------------------------- */

static void mark_positions(const nmo_arena_array_t *list, bool *skip, size_t size)
{
    const uint32_t *values = array_words(list);
    for (size_t i = 0; i < list->count; ++i) {
        if (values[i] == ID_SEQUENCE_MARKER) {
            ++i;  /* the position that follows is handled by the caller */
            continue;
        }
        if (values[i] < size) skip[values[i]] = true;
    }
}

/* The schema may write a value other than the one the file holds (it clamps
 * an enum, normalizes flags). When the edit did not change such a dword, that
 * is when the target still holds what the schema wrote at load, the file's
 * own value goes back in. Only sections of equal length are compared, so a
 * dword is always matched with the dword it was; a section the file made
 * longer than the schema wrote may have a different layout and is left to the
 * tail handling. Ids are left alone: they live in different id spaces. */
static size_t restore_untouched_values(
    nmo_chunk_t *target,
    const nmo_chunk_t *original,
    const nmo_chunk_t *canonical,
    const section_list_t *t_sections,
    const section_list_t *o_sections,
    const section_list_t *c_sections,
    nmo_arena_t *arena)
{
    const size_t c_total = canonical->data.count;
    bool *skip = c_total > 0 ? nmo_arena_alloc(arena, c_total, 1) : NULL;
    if (skip == NULL) return 0;
    memset(skip, 0, c_total);
    mark_positions(&canonical->ids, skip, c_total);
    mark_positions(&canonical->chunk_refs, skip, c_total);
    mark_positions(&canonical->managers, skip, c_total);
    /* Sequences: the count and every id after it. */
    const uint32_t *c_ids = array_words(&canonical->ids);
    const uint32_t *c_data = chunk_words(canonical);
    for (size_t i = 0; i + 1 < canonical->ids.count; ++i) {
        if (c_ids[i] != ID_SEQUENCE_MARKER) continue;
        const size_t header = c_ids[++i];
        if (header >= c_total) continue;
        const size_t end = header + 1u + c_data[header];
        for (size_t k = header; k < end && k < c_total; ++k) skip[k] = true;
    }

    uint32_t *t_data = target->data.count > 0 ? NMO_ARENA_ARRAY_DATA(uint32_t, &target->data) : NULL;
    const uint32_t *o_data = chunk_words(original);
    size_t restored = 0;
    for (size_t i = 0; i < c_sections->count; ++i) {
        const section_t *c = &c_sections->items[i];
        const section_t *t = find_section(t_sections, c->id);
        const section_t *o = find_section(o_sections, c->id);
        if (t == NULL || o == NULL || payload_dwords(t) != payload_dwords(c) ||
            payload_dwords(o) != payload_dwords(c)) {
            continue;
        }
        if (!fragment_is_plain(original, o->start, o->end)) continue;
        for (size_t k = 0; k < payload_dwords(c); ++k) {
            const size_t cp = c->start + 2u + k;
            if (skip[cp]) continue;
            const size_t tp = t->start + 2u + k;
            const size_t op = o->start + 2u + k;
            if (t_data[tp] == c_data[cp] && o_data[op] != c_data[cp]) {
                t_data[tp] = o_data[op];
                restored++;
            }
        }
    }
    return restored;
}

nmo_status_t nmo_chunk_merge_residue(
    nmo_chunk_t *target,
    const nmo_chunk_t *original,
    const nmo_chunk_t *canonical,
    const nmo_id_remap_t *original_to_target,
    nmo_arena_t *arena,
    nmo_chunk_residue_stats_t *out_stats)
{
    nmo_chunk_residue_stats_t stats = {0};
    if (out_stats != NULL) *out_stats = stats;
    if (target == NULL || original == NULL || canonical == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (target->data_version != original->data_version ||
        canonical->data_version != original->data_version ||
        target->chunk_version != original->chunk_version ||
        canonical->chunk_version != original->chunk_version) {
        return NMO_OK;  /* different layouts: nothing carries over */
    }

    section_list_t t_sections, o_sections, c_sections;
    if (!parse_sections(target, arena, &t_sections) ||
        !parse_sections(original, arena, &o_sections) ||
        !parse_sections(canonical, arena, &c_sections)) {
        return NMO_ERR_NOMEM;
    }

    stats.values_restored = restore_untouched_values(
        target, original, canonical, &t_sections, &o_sections, &c_sections, arena);

    /* Collect the residue. */
    fragment_t *fragments = o_sections.count > 0
        ? nmo_arena_alloc(arena, o_sections.count * sizeof(fragment_t), _Alignof(fragment_t))
        : NULL;
    size_t fragment_count = 0;
    for (size_t i = 0; i < o_sections.count; ++i) {
        const section_t *o = &o_sections.items[i];
        const section_t *c = find_section(&c_sections, o->id);
        const section_t *t = find_section(&t_sections, o->id);
        if (c == NULL) {
            if (t != NULL) continue;  /* the target wrote the section itself */
            if (!fragment_is_plain(original, o->start, o->end)) { stats.skipped++; continue; }
            fragments[fragment_count++] = (fragment_t){o->start, o->end, o->id, true};
        } else if (payload_dwords(o) > payload_dwords(c)) {
            if (t == NULL) continue;  /* the edit removed the section */
            const size_t tail_start = o->start + 2u + payload_dwords(c);
            if (!fragment_is_plain(original, tail_start, o->end)) {
                stats.skipped++;
                continue;
            }
            fragments[fragment_count++] = (fragment_t){tail_start, o->end, o->id, false};
        }
    }
    if (fragment_count == 0) {
        if (out_stats != NULL) *out_stats = stats;
        return NMO_OK;
    }

    /* Rebuild the data: each section of the target with its tail, then the new sections. */
    const uint32_t *t_data = chunk_words(target);
    const uint32_t *o_data = chunk_words(original);
    size_t total_out = target->data.count;
    for (size_t i = 0; i < fragment_count; ++i) {
        total_out += fragments[i].source_end - fragments[i].source_start;
    }
    const size_t max_sections = t_sections.count + fragment_count;
    uint32_t *out = nmo_arena_alloc(arena, total_out * sizeof(uint32_t), _Alignof(uint32_t));
    size_t *section_new_start = nmo_arena_alloc(arena, max_sections * sizeof(size_t), _Alignof(size_t));
    size_t *output_starts = nmo_arena_alloc(arena, max_sections * sizeof(size_t), _Alignof(size_t));
    const size_t id_capacity = target->ids.count + original->ids.count + 2u;
    id_entry_t *entries = nmo_arena_alloc(arena, id_capacity * sizeof(id_entry_t), _Alignof(id_entry_t));
    if (out == NULL || section_new_start == NULL || output_starts == NULL || entries == NULL) {
        return NMO_ERR_NOMEM;
    }

    size_t out_pos = 0;
    size_t entry_count = 0;
    size_t output_count = 0;

    for (size_t i = 0; i < t_sections.count; ++i) {
        const section_t *t = &t_sections.items[i];
        section_new_start[i] = out_pos;
        output_starts[output_count++] = out_pos;
        const size_t length = t->end - t->start;
        memcpy(out + out_pos, t_data + t->start, length * sizeof(uint32_t));
        out_pos += length;
        for (size_t f = 0; f < fragment_count; ++f) {
            const fragment_t *frag = &fragments[f];
            if (frag->whole_section || frag->target_id != t->id) continue;
            copy_fragment(frag, original, o_data, original_to_target, out, out_pos,
                          entries, &entry_count);
            out_pos += frag->source_end - frag->source_start;
            stats.tails_kept++;
        }
    }
    for (size_t f = 0; f < fragment_count; ++f) {
        const fragment_t *frag = &fragments[f];
        if (!frag->whole_section) continue;
        output_starts[output_count++] = out_pos;
        copy_fragment(frag, original, o_data, original_to_target, out, out_pos,
                      entries, &entry_count);
        out_pos += frag->source_end - frag->source_start;
        stats.sections_kept++;
    }

    /* Each section points at the next one; the last points at 0. */
    for (size_t k = 0; k < output_count; ++k) {
        out[output_starts[k] + 1] = k + 1 < output_count ? (uint32_t)output_starts[k + 1] : 0u;
    }

    /* Positions of the target's own id entries move with their sections. */
    const uint32_t *t_ids = array_words(&target->ids);
    for (size_t k = 0; k < target->ids.count; ++k) {
        if (t_ids[k] == ID_SEQUENCE_MARKER) {
            if (++k >= target->ids.count) break;
            entries[entry_count++] = (id_entry_t){
                map_position(&t_sections, section_new_start, t_ids[k]), true};
        } else {
            entries[entry_count++] = (id_entry_t){
                map_position(&t_sections, section_new_start, t_ids[k]), false};
        }
    }
    qsort(entries, entry_count, sizeof(id_entry_t), compare_id_entries);

    /* The positions of sub-chunks and manager ids move with their sections. */
    if (target->chunk_refs.count > 0) {
        uint32_t *refs = NMO_ARENA_ARRAY_DATA(uint32_t, &target->chunk_refs);
        for (size_t k = 0; k < target->chunk_refs.count; ++k) {
            if (refs[k] == ID_SEQUENCE_MARKER) {
                if (++k >= target->chunk_refs.count) break;
            }
            refs[k] = (uint32_t)map_position(&t_sections, section_new_start, refs[k]);
        }
    }
    if (target->managers.count > 0) {
        uint32_t *managers = NMO_ARENA_ARRAY_DATA(uint32_t, &target->managers);
        for (size_t k = 0; k < target->managers.count; ++k) {
            managers[k] = (uint32_t)map_position(&t_sections, section_new_start, managers[k]);
        }
    }

    /* Install the result. */
    nmo_status_t status = nmo_arena_array_resize(&target->data, out_pos);
    if (status != NMO_OK) return status;
    memcpy(target->data.data, out, out_pos * sizeof(uint32_t));
    status = nmo_arena_array_resize(&target->ids, 0);
    if (status != NMO_OK) return status;
    for (size_t k = 0; k < entry_count; ++k) {
        if (entries[k].sequence) {
            const uint32_t marker = ID_SEQUENCE_MARKER;
            status = nmo_arena_array_append(&target->ids, &marker);
            if (status != NMO_OK) return status;
        }
        const uint32_t position = (uint32_t)entries[k].pos;
        status = nmo_arena_array_append(&target->ids, &position);
        if (status != NMO_OK) return status;
    }
    if (target->ids.count > 0) target->chunk_options |= NMO_CHUNK_OPTION_IDS;
    nmo_chunk_close(target);

    if (out_stats != NULL) *out_stats = stats;
    NMO_RETURN_OK();
}
