/**
 * @file test_old_chunk_and_data_layouts.c
 * @brief The older sub-chunk layout and the data section of file versions below 8
 *
 * CKStateChunk::ReadSubChunk reads [size][class][data size][skipped][data] for
 * chunks older than version 4, and for a newer chunk when the dword after the
 * class, plus four, is the size. CKFile::ReadFileData starts the data section
 * of every file version below 8 with SaveIDMax and ObjectCount.
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "core/nmo_utils.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"

#include <string.h>

/* ------------------------------------------------------------------------
 * Sub-chunks
 * ------------------------------------------------------------------------ */

/* A chunk holding these dwords, at this chunk version, open for reading. */
static nmo_chunk_t *chunk_of_words(nmo_arena_t *arena, const uint32_t *words,
                                   size_t count, uint16_t chunk_version)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL || nmo_chunk_start_write(chunk) != NMO_OK) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        if (nmo_chunk_write_dword(chunk, words[i]) != NMO_OK) {
            return NULL;
        }
    }
    nmo_chunk_close(chunk);
    chunk->chunk_version = chunk_version;
    if (nmo_chunk_start_read(chunk) != NMO_OK) {
        return NULL;
    }
    return chunk;
}

static const uint32_t SENTINEL = 0xCAFEBABEu;

TEST(old_sub_chunk, chunk_older_than_four_reads_the_old_layout)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* The size counts itself: 4 header dwords and the data. The first sub-chunk's
     * data is an object id the way chunks older than version 4 store it. */
    const uint32_t words[] = {
        8u, 0x12u, 4u, 0xDEADBEEFu, 1u, 0xAAAAAAAAu, 0xBBBBBBBBu, 77u,
        4u, 0x34u, 0u, 0x99999999u,
        SENTINEL,
    };
    nmo_chunk_t *chunk = chunk_of_words(arena, words, sizeof(words) / sizeof(words[0]), 3);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_t *sub = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(chunk, &sub));
    ASSERT_NOT_NULL(sub);
    ASSERT_EQ(0x12u, sub->class_id);
    ASSERT_EQ(0x12u, sub->chunk_class_id);
    /* The engine makes the sub-chunk with both versions 0 and only its data. */
    ASSERT_EQ(0u, sub->data_version);
    ASSERT_EQ(0u, sub->chunk_version);
    ASSERT_EQ(4u, sub->data.count);
    ASSERT_EQ(0u, sub->ids.count);
    ASSERT_EQ(0u, sub->chunk_refs.count);
    ASSERT_EQ(0u, sub->managers.count);
    ASSERT_EQ(0u, sub->chunk_options & (NMO_CHUNK_OPTION_FILE | NMO_CHUNK_OPTION_IDS |
                                        NMO_CHUNK_OPTION_CHN | NMO_CHUNK_OPTION_MAN));
    ASSERT_NULL(sub->file_context);
    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->data);
    ASSERT_EQ(1u, data[0]);
    ASSERT_EQ(77u, data[3]);

    /* Its version 0 makes its own reads use the old object id encoding. */
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(sub));
    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(sub, &id));
    ASSERT_EQ(77u, id);

    /* A sub-chunk without data is still a sub-chunk, and the next one starts
     * right after the first one's last dword. */
    nmo_chunk_t *empty = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(chunk, &empty));
    ASSERT_NOT_NULL(empty);
    ASSERT_EQ(0x34u, empty->class_id);
    ASSERT_EQ(0u, empty->chunk_version);
    ASSERT_EQ(0u, empty->data.count);

    uint32_t next = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &next));
    ASSERT_EQ(SENTINEL, next);
    nmo_arena_destroy(arena);
}

TEST(old_sub_chunk, sequence_of_old_sub_chunks)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    const uint32_t words[] = {
        2u,
        5u, 0x21u, 1u, 0u, 0x1111u,
        6u, 0x22u, 2u, 0u, 0x2222u, 0x3333u,
        SENTINEL,
    };
    nmo_chunk_t *chunk = chunk_of_words(arena, words, sizeof(words) / sizeof(words[0]), 2);
    ASSERT_NOT_NULL(chunk);

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read_sub_chunk_sequence(chunk, &count));
    ASSERT_EQ(2u, count);

    nmo_chunk_t *first = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(chunk, &first));
    ASSERT_NOT_NULL(first);
    ASSERT_EQ(0x21u, first->class_id);
    ASSERT_EQ(1u, first->data.count);
    ASSERT_EQ(0x1111u, NMO_ARENA_ARRAY_DATA(uint32_t, &first->data)[0]);

    nmo_chunk_t *second = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(chunk, &second));
    ASSERT_NOT_NULL(second);
    ASSERT_EQ(0x22u, second->class_id);
    ASSERT_EQ(2u, second->data.count);
    ASSERT_EQ(0x3333u, NMO_ARENA_ARRAY_DATA(uint32_t, &second->data)[1]);

    uint32_t next = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &next));
    ASSERT_EQ(SENTINEL, next);
    nmo_arena_destroy(arena);
}

TEST(old_sub_chunk, null_and_truncated_old_sub_chunks)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* A zero size is no sub-chunk. The second claims 50 data dwords but only
     * five follow its header. */
    const uint32_t words[] = {
        0u,
        5u, 0x21u, 50u, 0u, 1u, 2u, 3u, 4u, 5u,
    };
    nmo_chunk_t *chunk = chunk_of_words(arena, words, sizeof(words) / sizeof(words[0]), 3);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_t *sub = chunk;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(chunk, &sub));
    ASSERT_NULL(sub);

    const size_t start = nmo_chunk_get_position(chunk);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_chunk_read_sub_chunk(chunk, &sub));
    ASSERT_NULL(sub);
    ASSERT_EQ(start, nmo_chunk_get_position(chunk));
    nmo_arena_destroy(arena);
}

TEST(old_sub_chunk, newer_chunk_detects_the_old_layout)
{
    /* In a chunk of version 4 or later the dword after the class is the
     * versions of a current sub-chunk, or the data size of an old one, whose
     * size is that plus four. Chunk versions 4 to 7 all look for it. */
    for (uint16_t chunk_version = 4; chunk_version <= 7; chunk_version++) {
        nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
        ASSERT_NOT_NULL(arena);

        nmo_chunk_t *source = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(source);
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(source));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(source, 0xA1u));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(source, 0xA2u));
        nmo_chunk_close(source);
        source->class_id = 0x37u;
        source->chunk_class_id = 0x37u;
        source->data_version = 5;

        nmo_chunk_t *parent = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(parent);
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parent));
        const uint32_t old_sub[] = {6u, 0x12u, 2u, 0xDEADBEEFu, 0x1111u, 0x2222u};
        for (size_t i = 0; i < sizeof(old_sub) / sizeof(old_sub[0]); i++) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(parent, old_sub[i]));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(parent, source));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(parent, SENTINEL));
        nmo_chunk_close(parent);
        parent->chunk_version = chunk_version;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_read(parent));

        nmo_chunk_t *old = NULL;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(parent, &old));
        ASSERT_NOT_NULL(old);
        ASSERT_EQ(0x12u, old->class_id);
        ASSERT_EQ(0u, old->data_version);
        ASSERT_EQ(0u, old->chunk_version);
        ASSERT_EQ(2u, old->data.count);
        ASSERT_EQ(0x1111u, NMO_ARENA_ARRAY_DATA(uint32_t, &old->data)[0]);
        ASSERT_EQ(0x2222u, NMO_ARENA_ARRAY_DATA(uint32_t, &old->data)[1]);

        /* The current layout right after it reads as before. */
        nmo_chunk_t *current = NULL;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(parent, &current));
        ASSERT_NOT_NULL(current);
        ASSERT_EQ(0x37u, current->class_id);
        ASSERT_EQ(5u, current->data_version);
        ASSERT_EQ(NMO_CHUNK_VERSION4, current->chunk_version);
        ASSERT_EQ(2u, current->data.count);
        ASSERT_EQ(0xA1u, NMO_ARENA_ARRAY_DATA(uint32_t, &current->data)[0]);
        ASSERT_EQ(0xA2u, NMO_ARENA_ARRAY_DATA(uint32_t, &current->data)[1]);

        uint32_t next = 0;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(parent, &next));
        ASSERT_EQ(SENTINEL, next);
        nmo_arena_destroy(arena);
    }
}

TEST(old_sub_chunk, old_sub_chunk_is_written_back_as_the_engine_would)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* The engine checks `size` dwords after the size dword, one more than the
     * old layout has, so an old sub-chunk needs a dword behind it. */
    const uint32_t words[] = {6u, 0x12u, 2u, 0xDEADBEEFu, 0x1111u, 0x2222u, SENTINEL};
    nmo_chunk_t *old_parent = chunk_of_words(arena, words, sizeof(words) / sizeof(words[0]), 3);
    ASSERT_NOT_NULL(old_parent);
    nmo_chunk_t *sub = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(old_parent, &sub));
    ASSERT_NOT_NULL(sub);

    /* CKStateChunk::WriteSubChunk: size, class, versions (both 0), data size,
     * file flag (the sub-chunk has no file), id, chunk and manager counts, data. */
    nmo_chunk_t *parent = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parent));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(parent, sub));
    nmo_chunk_close(parent);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(parent));

    const uint32_t expected[] = {
        9u, 0x12u, 0u, 2u, 0u, 0u, 0u, 0u, 0x1111u, 0x2222u,
    };
    ASSERT_EQ(sizeof(expected) / sizeof(expected[0]), parent->data.count);
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        uint32_t value = 0;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(parent, &value));
        ASSERT_EQ(expected[i], value);
    }
    nmo_arena_destroy(arena);
}

/* ------------------------------------------------------------------------
 * Data section
 * ------------------------------------------------------------------------ */

static nmo_chunk_t *two_dword_chunk(nmo_arena_t *arena, uint32_t first, uint32_t second)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL || nmo_chunk_start_write(chunk) != NMO_OK ||
        nmo_chunk_write_dword(chunk, first) != NMO_OK ||
        nmo_chunk_write_dword(chunk, second) != NMO_OK) {
        return NULL;
    }
    nmo_chunk_close(chunk);
    /* The data section parser marks its chunks as file chunks, as they are in a file. */
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    return chunk;
}

/* Whether a parsed chunk holds exactly these two dwords. */
static int chunk_holds(nmo_chunk_t *chunk, uint32_t first, uint32_t second)
{
    uint32_t a = 0;
    uint32_t b = 0;
    return chunk != NULL && nmo_chunk_start_read(chunk) == NMO_OK &&
           nmo_chunk_read_dword(chunk, &a) == NMO_OK &&
           nmo_chunk_read_dword(chunk, &b) == NMO_OK && a == first && b == second;
}

/* A section with one manager and two objects, the second without a chunk. */
static int make_section(nmo_arena_t *arena, nmo_data_section_t *section,
                        nmo_manager_data_t *managers, nmo_object_data_t *objects)
{
    memset(section, 0, sizeof(*section));
    memset(managers, 0, sizeof(*managers));
    memset(objects, 0, 2 * sizeof(*objects));

    managers[0].guid.d1 = 0x11223344u;
    managers[0].guid.d2 = 0x55667788u;
    managers[0].chunk = two_dword_chunk(arena, 0xA1u, 0xA2u);
    objects[0].object_id = 0x41u;
    objects[0].chunk = two_dword_chunk(arena, 0xB1u, 0xB2u);
    objects[1].object_id = 0x42u;

    section->manager_count = 1u;
    section->managers = managers;
    section->object_count = 2u;
    section->objects = objects;
    section->save_id_max = 0x1234u;
    return managers[0].chunk != NULL && objects[0].chunk != NULL;
}

TEST(data_section_layouts, version_7_starts_with_save_id_max_and_object_count)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);
    nmo_data_section_t section;
    nmo_manager_data_t managers[1];
    nmo_object_data_t objects[2];
    ASSERT_TRUE(make_section(arena, &section, managers, objects));

    /* The same section at version 8 is what version 7 holds after its two dwords. */
    const size_t size8 = nmo_data_section_calculate_size(&section, 8, arena);
    const size_t size7 = nmo_data_section_calculate_size(&section, 7, arena);
    ASSERT_GT(size8, 0u);
    ASSERT_EQ(size8 + 8u, size7);

    uint8_t *bytes8 = nmo_arena_alloc(arena, size8, 16);
    uint8_t *bytes7 = nmo_arena_alloc(arena, size7, 16);
    ASSERT_NOT_NULL(bytes8);
    ASSERT_NOT_NULL(bytes7);
    size_t written8 = 0;
    size_t written7 = 0;
    ASSERT_EQ(NMO_OK, nmo_data_section_serialize(&section, 8, bytes8, size8, &written8, arena));
    ASSERT_EQ(NMO_OK, nmo_data_section_serialize(&section, 7, bytes7, size7, &written7, arena));
    ASSERT_EQ(size8, written8);
    ASSERT_EQ(size7, written7);

    ASSERT_EQ(0x1234u, nmo_read_u32_le(bytes7));
    ASSERT_EQ(2u, nmo_read_u32_le(bytes7 + 4));
    ASSERT_EQ(0x11223344u, nmo_read_u32_le(bytes7 + 8));
    ASSERT_MEM_EQ(bytes8, bytes7 + 8, size8);
    /* Version 8 has no leading counts: the header holds them. */
    ASSERT_EQ(0x11223344u, nmo_read_u32_le(bytes8));

    /* The plan agrees with the direct size. */
    nmo_data_section_plan_t plan = {0};
    ASSERT_EQ(NMO_OK, nmo_data_section_plan_build(&section, 7, arena, &plan));
    ASSERT_EQ(size7, plan.total_size);

    nmo_arena_destroy(arena);
}

TEST(data_section_layouts, version_7_round_trip)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);
    nmo_data_section_t section;
    nmo_manager_data_t managers[1];
    nmo_object_data_t objects[2];
    ASSERT_TRUE(make_section(arena, &section, managers, objects));

    const size_t size = nmo_data_section_calculate_size(&section, 7, arena);
    uint8_t *bytes = nmo_arena_alloc(arena, size, 16);
    ASSERT_NOT_NULL(bytes);
    size_t written = 0;
    ASSERT_EQ(NMO_OK, nmo_data_section_serialize(&section, 7, bytes, size, &written, arena));

    /* The header gives the manager and object counts; the section its own. */
    nmo_data_section_t parsed = {0};
    parsed.manager_count = 1u;
    parsed.object_count = 2u;
    ASSERT_EQ(NMO_OK, nmo_data_section_parse(bytes, written, 7, &parsed, NULL, arena));
    ASSERT_EQ(0x1234u, parsed.save_id_max);
    ASSERT_EQ(1u, parsed.manager_count);
    ASSERT_EQ(2u, parsed.object_count);
    ASSERT_EQ(0x11223344u, parsed.managers[0].guid.d1);
    ASSERT_EQ(0x55667788u, parsed.managers[0].guid.d2);
    ASSERT_TRUE(chunk_holds(parsed.managers[0].chunk, 0xA1u, 0xA2u));
    ASSERT_TRUE(chunk_holds(parsed.objects[0].chunk, 0xB1u, 0xB2u));
    ASSERT_NULL(parsed.objects[1].chunk);
    ASSERT_EQ(0u, parsed.objects[1].data_size);

    uint8_t *again = nmo_arena_alloc(arena, size, 16);
    ASSERT_NOT_NULL(again);
    size_t rewritten = 0;
    ASSERT_EQ(NMO_OK, nmo_data_section_serialize(&parsed, 7, again, size, &rewritten, arena));
    ASSERT_EQ(written, rewritten);
    ASSERT_MEM_EQ(bytes, again, written);

    nmo_arena_destroy(arena);
}

TEST(data_section_layouts, version_7_uses_the_section_object_count_up_to_the_header_s)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);
    nmo_data_section_t section;
    nmo_manager_data_t managers[1];
    nmo_object_data_t objects[2];
    ASSERT_TRUE(make_section(arena, &section, managers, objects));

    const size_t size = nmo_data_section_calculate_size(&section, 7, arena);
    uint8_t *bytes = nmo_arena_alloc(arena, size, 16);
    ASSERT_NOT_NULL(bytes);
    size_t written = 0;
    ASSERT_EQ(NMO_OK, nmo_data_section_serialize(&section, 7, bytes, size, &written, arena));

    /* A header with more objects than the section holds: the engine reads the
     * section's two and leaves the third without a chunk. */
    nmo_data_section_t parsed = {0};
    parsed.manager_count = 1u;
    parsed.object_count = 3u;
    ASSERT_EQ(NMO_OK, nmo_data_section_parse(bytes, written, 7, &parsed, NULL, arena));
    ASSERT_EQ(2u, parsed.object_count);
    ASSERT_TRUE(chunk_holds(parsed.objects[0].chunk, 0xB1u, 0xB2u));

    /* A section with more objects than the header's table has is corrupt. */
    memset(&parsed, 0, sizeof(parsed));
    parsed.manager_count = 1u;
    parsed.object_count = 1u;
    ASSERT_EQ(NMO_ERR_CORRUPT, nmo_data_section_parse(bytes, written, 7, &parsed, NULL, arena));
    ASSERT_NULL(parsed.objects);

    /* A count the rest of the section cannot hold is refused before allocating. */
    uint8_t forged[12];
    memset(forged, 0, sizeof(forged));
    nmo_write_u32_le(forged + 4, 0x40000000u);
    memset(&parsed, 0, sizeof(parsed));
    parsed.object_count = 0x7FFFFFFFu;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_data_section_parse(forged, sizeof(forged), 7, &parsed, NULL, arena));

    /* Fewer than the two leading dwords. */
    memset(&parsed, 0, sizeof(parsed));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_data_section_parse(forged, 7, 7, &parsed, NULL, arena));

    nmo_arena_destroy(arena);
}

TEST(data_section_layouts, version_6_and_5_take_the_section_object_count)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);

    nmo_manager_data_t manager = {0};
    manager.guid.d1 = 0x11111111u;
    manager.guid.d2 = 0x22222222u;
    nmo_object_data_t object = {0};
    object.object_id = 0x55u;
    nmo_data_section_t section = {0};
    section.manager_count = 1u;
    section.managers = &manager;
    section.object_count = 1u;
    section.objects = &object;
    section.save_id_max = 0x66u;

    /* leading counts 8, manager entry 12 (6 and later), object entry 8 (id and size) */
    const struct { uint32_t version; size_t expected; } cases[] = {{5u, 16u}, {6u, 28u}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t bytes[64];
        size_t written = 0;
        ASSERT_EQ(NMO_OK, nmo_data_section_serialize(
            &section, cases[i].version, bytes, sizeof(bytes), &written, arena));
        ASSERT_EQ(cases[i].expected, written);
        ASSERT_EQ(cases[i].expected,
                  nmo_data_section_calculate_size(&section, cases[i].version, arena));
        ASSERT_EQ(0x66u, nmo_read_u32_le(bytes));
        ASSERT_EQ(1u, nmo_read_u32_le(bytes + 4));

        /* Without an object table in the header the section's count rules. */
        nmo_data_section_t parsed = {0};
        parsed.manager_count = 1u;
        ASSERT_EQ(NMO_OK, nmo_data_section_parse(
            bytes, written, cases[i].version, &parsed, NULL, arena));
        ASSERT_EQ(0x66u, parsed.save_id_max);
        ASSERT_EQ(1u, parsed.object_count);
        ASSERT_EQ(0x55u, parsed.objects[0].object_id);
        ASSERT_EQ(cases[i].version >= 6u ? 1u : 0u, parsed.manager_count);
    }
    nmo_arena_destroy(arena);
}

TEST(data_section_layouts, version_8_has_no_leading_counts)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);
    nmo_data_section_t section;
    nmo_manager_data_t managers[1];
    nmo_object_data_t objects[2];
    ASSERT_TRUE(make_section(arena, &section, managers, objects));

    const size_t size = nmo_data_section_calculate_size(&section, 8, arena);
    uint8_t *bytes = nmo_arena_alloc(arena, size, 16);
    ASSERT_NOT_NULL(bytes);
    size_t written = 0;
    ASSERT_EQ(NMO_OK, nmo_data_section_serialize(&section, 8, bytes, size, &written, arena));
    ASSERT_EQ(size, written);
    /* The manager entry comes first and the object entries are [size][chunk]. */
    ASSERT_EQ(0x11223344u, nmo_read_u32_le(bytes));
    ASSERT_EQ(0x55667788u, nmo_read_u32_le(bytes + 4));

    /* The header holds the counts, so parse keeps what the caller gives it. */
    nmo_data_section_t parsed = {0};
    parsed.manager_count = 1u;
    parsed.object_count = 2u;
    parsed.save_id_max = 0x77u;
    ASSERT_EQ(NMO_OK, nmo_data_section_parse(bytes, written, 8, &parsed, NULL, arena));
    ASSERT_EQ(0x77u, parsed.save_id_max);
    ASSERT_EQ(1u, parsed.manager_count);
    ASSERT_EQ(2u, parsed.object_count);
    ASSERT_TRUE(chunk_holds(parsed.managers[0].chunk, 0xA1u, 0xA2u));
    ASSERT_TRUE(chunk_holds(parsed.objects[0].chunk, 0xB1u, 0xB2u));
    ASSERT_NULL(parsed.objects[1].chunk);

    /* The header's count rules here: one that is larger than the section's
     * data runs out of it, as before. */
    memset(&parsed, 0, sizeof(parsed));
    parsed.manager_count = 1u;
    parsed.object_count = 5u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_data_section_parse(bytes, written, 8, &parsed, NULL, arena));
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(old_sub_chunk, chunk_older_than_four_reads_the_old_layout);
    REGISTER_TEST(old_sub_chunk, sequence_of_old_sub_chunks);
    REGISTER_TEST(old_sub_chunk, null_and_truncated_old_sub_chunks);
    REGISTER_TEST(old_sub_chunk, newer_chunk_detects_the_old_layout);
    REGISTER_TEST(old_sub_chunk, old_sub_chunk_is_written_back_as_the_engine_would);
    REGISTER_TEST(data_section_layouts, version_7_starts_with_save_id_max_and_object_count);
    REGISTER_TEST(data_section_layouts, version_7_round_trip);
    REGISTER_TEST(data_section_layouts, version_7_uses_the_section_object_count_up_to_the_header_s);
    REGISTER_TEST(data_section_layouts, version_6_and_5_take_the_section_object_count);
    REGISTER_TEST(data_section_layouts, version_8_has_no_leading_counts);
TEST_MAIN_END()
