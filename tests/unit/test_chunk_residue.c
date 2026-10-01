/**
 * @file test_chunk_residue.c
 * @brief Carrying what a schema does not model into the chunk written for an object
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_residue.h"
#include "format/nmo_id_remap.h"

#include <string.h>

#define SECTION_A 0x1000u
#define SECTION_B 0x2000u
#define SECTION_X 0x3000u

static nmo_chunk_t *begin(nmo_arena_t *arena, uint32_t data_version)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) return NULL;
    chunk->class_id = 31;
    chunk->data_version = data_version;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    return nmo_chunk_start_write(chunk) == NMO_OK ? chunk : NULL;
}

static const uint32_t *words(const nmo_chunk_t *chunk)
{
    return (const uint32_t *)chunk->data.data;
}

TEST(chunk_residue, equivalent_ignores_id_values_and_padding)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *a = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(a, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(a, 0x11223344u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(a, 5));
    nmo_chunk_close(a);

    nmo_chunk_t *b = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(b, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(b, 0x11223344u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(b, 77));
    nmo_chunk_close(b);
    ASSERT_TRUE(nmo_chunk_equivalent(a, b));
    ASSERT_NE(nmo_chunk_digest(a), nmo_chunk_digest(b));

    nmo_chunk_t *c = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(c, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(c, 0x11223345u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(c, 5));
    nmo_chunk_close(c);
    ASSERT_FALSE(nmo_chunk_equivalent(a, c));

    /* The writer zeroes the upper bytes that Virtools leaves behind a string. */
    nmo_chunk_t *garbage = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(garbage, SECTION_B));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(garbage, 0xFEEDFA41u));
    nmo_chunk_close(garbage);
    nmo_chunk_t *clean = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(clean, SECTION_B));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(clean, 0x41u));
    nmo_chunk_close(clean);
    ASSERT_TRUE(nmo_chunk_equivalent(garbage, clean));

    nmo_arena_destroy(arena);
}

TEST(chunk_residue, merge_keeps_unknown_sections_and_tails)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* The file: section A with a dword more than the schema models, section B
     * with an object id, and a section X the schema does not know. */
    nmo_chunk_t *original = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(original, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 0xAAAA0001u));   /* tail */
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(original, SECTION_B));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(original, 4));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(original, SECTION_X));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 0xBBBB0002u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(original, 5));
    nmo_chunk_close(original);

    /* What the schema wrote for the state as loaded. */
    nmo_chunk_t *canonical = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(canonical, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(canonical, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(canonical, SECTION_B));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(canonical, 4));
    nmo_chunk_close(canonical);
    ASSERT_FALSE(nmo_chunk_equivalent(original, canonical));

    /* What the schema writes after an edit. */
    nmo_chunk_t *target = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(target, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 99));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(target, SECTION_B));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(target, 8));
    nmo_chunk_close(target);

    nmo_id_remap_t *map = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(map);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(map, 5, 9));

    nmo_chunk_residue_stats_t stats;
    ASSERT_EQ(NMO_OK, nmo_chunk_merge_residue(target, original, canonical, map, arena, &stats));
    ASSERT_EQ(1u, stats.tails_kept);
    ASSERT_EQ(1u, stats.sections_kept);
    ASSERT_EQ(0u, stats.skipped);

    const uint32_t *d = words(target);
    /* A: id, next, 99, tail | B: id, next, 8 | X: id, next, 0xBBBB0002, 9 */
    ASSERT_EQ(11u, target->data.count);
    ASSERT_EQ(SECTION_A, d[0]);
    ASSERT_EQ(4u, d[1]);
    ASSERT_EQ(99u, d[2]);
    ASSERT_EQ(0xAAAA0001u, d[3]);
    ASSERT_EQ(SECTION_B, d[4]);
    ASSERT_EQ(7u, d[5]);
    ASSERT_EQ(8u, d[6]);
    ASSERT_EQ(SECTION_X, d[7]);
    ASSERT_EQ(0u, d[8]);
    ASSERT_EQ(0xBBBB0002u, d[9]);
    ASSERT_EQ(9u, d[10]);
    ASSERT_EQ(2u, target->ids.count);
    ASSERT_EQ(6u, ((const uint32_t *)target->ids.data)[0]);
    ASSERT_EQ(10u, ((const uint32_t *)target->ids.data)[1]);
    nmo_arena_destroy(arena);
}

TEST(chunk_residue, translate_uses_the_layout_when_the_chunk_tracks_no_ids)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* A chunk read from a file: the ids are plain dwords and nothing records
     * where they are. */
    nmo_chunk_t *loaded = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(loaded, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(loaded, 5));   /* a count, not an id */
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(loaded, 5));   /* an id */
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(loaded, 0xFFFFFFFFu));
    nmo_chunk_close(loaded);
    ASSERT_EQ(0u, loaded->ids.count);

    /* The same state serialized again, which does track them. */
    nmo_chunk_t *layout = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(layout, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(layout, 5));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(layout, 40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(layout, 0));
    nmo_chunk_close(layout);

    nmo_id_remap_t *map = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(map);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(map, 5, 3));

    bool unresolved = false;
    ASSERT_EQ(NMO_OK, nmo_chunk_translate_ids_with_layout(loaded, layout, map, 8, &unresolved));
    ASSERT_FALSE(unresolved);
    ASSERT_EQ(5u, words(loaded)[2]);
    ASSERT_EQ(3u, words(loaded)[3]);
    ASSERT_EQ(0xFFFFFFFFu, words(loaded)[4]);

    /* An id below the limit that has no image names an object that is gone. */
    nmo_id_remap_t *gone = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(gone);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(gone, 1, 1));
    unresolved = false;
    ASSERT_EQ(NMO_OK, nmo_chunk_translate_ids_with_layout(loaded, layout, gone, 8, &unresolved));
    ASSERT_TRUE(unresolved);
    nmo_arena_destroy(arena);
}

TEST(chunk_residue, merge_restores_values_the_schema_normalized_but_the_edit_left_alone)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* The file holds 7 for an enum the schema clamps to 1, and an object id. */
    nmo_chunk_t *original = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(original, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 7));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 4));   /* an id of the file */
    nmo_chunk_close(original);

    nmo_chunk_t *canonical = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(canonical, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(canonical, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(canonical, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(canonical, 4));
    nmo_chunk_close(canonical);
    ASSERT_FALSE(nmo_chunk_equivalent(original, canonical));

    /* Edit 1 changes the first dword only: the enum keeps the file's 7. */
    nmo_chunk_t *first = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(first, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(first, 99));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(first, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(first, 12));  /* the id is written differently */
    nmo_chunk_close(first);
    nmo_chunk_residue_stats_t stats;
    ASSERT_EQ(NMO_OK, nmo_chunk_merge_residue(first, original, canonical, NULL, arena, &stats));
    ASSERT_EQ(1u, stats.values_restored);
    ASSERT_EQ(99u, words(first)[2]);
    ASSERT_EQ(7u, words(first)[3]);
    ASSERT_EQ(12u, words(first)[4]);

    /* Edit 2 sets the enum itself: the edit wins. */
    nmo_chunk_t *second = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(second, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(second, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(second, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(second, 12));
    nmo_chunk_close(second);
    ASSERT_EQ(NMO_OK, nmo_chunk_merge_residue(second, original, canonical, NULL, arena, &stats));
    ASSERT_EQ(0u, stats.values_restored);
    ASSERT_EQ(2u, words(second)[3]);
    nmo_arena_destroy(arena);
}

TEST(chunk_residue, merge_moves_sub_chunk_references_with_their_sections)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *original = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(original, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 0xAAAA0001u));   /* tail */
    nmo_chunk_close(original);

    nmo_chunk_t *canonical = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(canonical, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(canonical, 10));
    nmo_chunk_close(canonical);

    /* The edited chunk has a sub-chunk in a later section. */
    nmo_chunk_t *sub = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(sub, 0x5151u));
    nmo_chunk_close(sub);
    nmo_chunk_t *target = begin(arena, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(target, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 99));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(target, SECTION_B));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(target, sub));
    nmo_chunk_close(target);
    ASSERT_EQ(1u, target->chunk_refs.count);
    const uint32_t before = ((const uint32_t *)target->chunk_refs.data)[0];
    const uint32_t header_dword = words(target)[before];

    nmo_chunk_residue_stats_t stats;
    ASSERT_EQ(NMO_OK, nmo_chunk_merge_residue(target, original, canonical, NULL, arena, &stats));
    ASSERT_EQ(1u, stats.tails_kept);
    ASSERT_EQ(0u, stats.skipped);
    ASSERT_EQ(1u, target->chunk_refs.count);
    const uint32_t after = ((const uint32_t *)target->chunk_refs.data)[0];
    ASSERT_EQ(before + 1u, after);
    /* The reference still names the header of the sub-chunk. */
    ASSERT_EQ(header_dword, words(target)[after]);
    ASSERT_EQ(0xAAAA0001u, words(target)[3]);
    nmo_arena_destroy(arena);
}

TEST(chunk_residue, merge_does_nothing_across_data_versions)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *original = begin(arena, 4);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(original, SECTION_X));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(original, 1));
    nmo_chunk_close(original);
    nmo_chunk_t *canonical = begin(arena, 9);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(canonical, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(canonical, 1));
    nmo_chunk_close(canonical);
    nmo_chunk_t *target = begin(arena, 9);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(target, SECTION_A));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 2));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_OK, nmo_chunk_merge_residue(target, original, canonical, NULL, arena, NULL));
    ASSERT_EQ(3u, target->data.count);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_residue, equivalent_ignores_id_values_and_padding);
    REGISTER_TEST(chunk_residue, merge_keeps_unknown_sections_and_tails);
    REGISTER_TEST(chunk_residue, translate_uses_the_layout_when_the_chunk_tracks_no_ids);
    REGISTER_TEST(chunk_residue, merge_restores_values_the_schema_normalized_but_the_edit_left_alone);
    REGISTER_TEST(chunk_residue, merge_moves_sub_chunk_references_with_their_sections);
    REGISTER_TEST(chunk_residue, merge_does_nothing_across_data_versions);
TEST_MAIN_END()
