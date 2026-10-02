/**
 * @file test_section_schemas.c
 * @brief What a section-based schema reads and writes, byte for byte
 *
 * CKBehaviorIO, CKBehaviorLink and CKKinematicChain keep their data in a few
 * identifier sections of a chunk. These tests pin the wire format of those
 * sections: which identifier and how many dwords a section holds, what a
 * reader makes of a section that is too short or too long, which sections
 * are tried when (the legacy layouts of a link only when the new one is
 * absent), what a save to a chunk without a file writes under which save
 * flags, and that a failed read leaves the state alone.
 */

#include "../test_framework.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_deserialize_context.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_types.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_statesave_ids.h"
#include <stdio.h>
#include <string.h>

#define UNRELATED_IDENTIFIER 0x11223344u

typedef struct fixture {
    nmo_arena_t *arena;
    nmo_id_remap_t *file_to_runtime;
    nmo_id_remap_t *runtime_to_file;
    nmo_chunk_file_context_t read_context;
    nmo_chunk_file_context_t write_context;
} fixture_t;

static int fixture_init(fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    f->arena = nmo_arena_create(NULL, 16384);
    if (f->arena == NULL) return 0;
    f->file_to_runtime = nmo_id_remap_create(f->arena);
    f->runtime_to_file = nmo_id_remap_create(f->arena);
    if (f->file_to_runtime == NULL || f->runtime_to_file == NULL) return 0;
    f->read_context.file_to_runtime = f->file_to_runtime;
    f->write_context.runtime_to_file = f->runtime_to_file;
    return 1;
}

/* A chunk of a file: identifier, then dwords, optionally another identifier. */
static nmo_chunk_t *section_chunk(fixture_t *f, nmo_class_id_t class_id,
                                  uint32_t identifier, const uint32_t *words,
                                  size_t word_count, uint32_t next_identifier)
{
    nmo_chunk_t *chunk = nmo_chunk_create(f->arena);
    if (chunk == NULL) return NULL;
    chunk->class_id = class_id;
    chunk->data_version = 8;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    if (nmo_chunk_start_write(chunk) != NMO_OK) return NULL;
    if (identifier != 0 && nmo_chunk_write_identifier(chunk, identifier) != NMO_OK) return NULL;
    for (size_t i = 0; i < word_count; ++i) {
        if (nmo_chunk_write_dword(chunk, words[i]) != NMO_OK) return NULL;
    }
    if (next_identifier != 0 &&
        nmo_chunk_write_identifier(chunk, next_identifier) != NMO_OK) {
        return NULL;
    }
    nmo_chunk_close(chunk);
    nmo_chunk_set_file_context(chunk, &f->read_context);
    return chunk;
}

static nmo_chunk_t *empty_chunk(fixture_t *f, nmo_class_id_t class_id)
{
    return section_chunk(f, class_id, 0, NULL, 0, 0);
}

static nmo_deserialize_context_t deserialize_context(fixture_t *f)
{
    return nmo_deserialize_context_create(f->arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
}

/* The dwords a hook writes, in a file chunk (save_flags ignored) or not. */
static nmo_chunk_t *written(fixture_t *f, nmo_class_id_t class_id, int file,
                            uint32_t save_flags,
                            nmo_status_t (*serialize)(const void *, nmo_chunk_t *,
                                                      const nmo_type_descriptor_t *, void *),
                            const void *state, nmo_status_t *out_status)
{
    nmo_chunk_t *chunk = nmo_chunk_create(f->arena);
    chunk->class_id = class_id;
    chunk->data_version = 8;
    if (file) {
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        nmo_chunk_set_file_context(chunk, &f->write_context);
    }
    nmo_serialize_context_t context = file
        ? nmo_serialize_context_create(f->arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0)
        : nmo_serialize_context_create_nonfile(f->arena, NULL, save_flags);
    nmo_chunk_start_write(chunk);
    *out_status = serialize(state, chunk, NULL, &context);
    nmo_chunk_close(chunk);
    return chunk;
}

static int data_is(const nmo_chunk_t *chunk, const uint32_t *words, size_t count)
{
    const int same = chunk->data.count == count &&
        (count == 0 || memcmp(chunk->data.data, words, count * sizeof(uint32_t)) == 0);
    if (!same) {
        const uint32_t *actual = (const uint32_t *)chunk->data.data;
        printf("  chunk holds %zu dwords:", (size_t)chunk->data.count);
        for (size_t i = 0; i < chunk->data.count; ++i) printf(" %08X", actual[i]);
        printf("\n  expected %zu dwords:", count);
        for (size_t i = 0; i < count; ++i) printf(" %08X", words[i]);
        printf("\n");
    }
    return same;
}

/* The dwords of the last section as the chunk API writes them: identifier, the
 * offset of the next section (0: none), then the payload. */
#define EXPECT_SECTION(chunk, identifier, ...)                                          \
    do {                                                                               \
        const uint32_t payload__[] = {__VA_ARGS__};                                    \
        uint32_t expected__[2 + sizeof(payload__) / sizeof(payload__[0])];             \
        expected__[0] = (identifier);                                                  \
        expected__[1] = 0u; /* the last section has no successor */                   \
        memcpy(&expected__[2], payload__, sizeof(payload__));                          \
        ASSERT_TRUE(data_is((chunk), expected__,                                       \
                            sizeof(expected__) / sizeof(expected__[0])));              \
    } while (0)

/* ---------------------------------------------------------------- CKBehaviorIO */

TEST(section_schemas, behaviorio_reads_its_flags_section)
{
    fixture_t f;
    ASSERT_TRUE(fixture_init(&f));
    nmo_deserialize_context_t context = deserialize_context(&f);
    nmo_behaviorio_state_t state;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_vtable.create(&state, NULL, NULL));
    ASSERT_TRUE(state.has_flags);

    /* No section: the flags are absent, not zero. */
    ASSERT_EQ(NMO_OK, nmo_behaviorio_deserialize(
        &state, empty_chunk(&f, NMO_CID_BEHAVIORIO), NULL, &context));
    ASSERT_FALSE(state.has_flags);
    ASSERT_EQ(0u, state.old_flags);

    const uint32_t flags[] = {0x2Au};
    ASSERT_EQ(NMO_OK, nmo_behaviorio_deserialize(
        &state, section_chunk(&f, NMO_CID_BEHAVIORIO, CK_STATESAVE_BEHAV_IOFLAGS, flags, 1, 0),
        NULL, &context));
    ASSERT_TRUE(state.has_flags);
    ASSERT_EQ(0x2Au, state.old_flags);

    /* A section is exactly one dword. */
    state.old_flags = 0x77u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behaviorio_deserialize(
        &state, section_chunk(&f, NMO_CID_BEHAVIORIO, CK_STATESAVE_BEHAV_IOFLAGS, NULL, 0, 0),
        NULL, &context));
    const uint32_t two[] = {1u, 2u};
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behaviorio_deserialize(
        &state, section_chunk(&f, NMO_CID_BEHAVIORIO, CK_STATESAVE_BEHAV_IOFLAGS, two, 2, 0),
        NULL, &context));
    ASSERT_EQ(0x77u, state.old_flags);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behaviorio_deserialize(
        &state,
        section_chunk(&f, NMO_CID_BEHAVIORIO, CK_STATESAVE_BEHAV_IOFLAGS, NULL, 0,
                      UNRELATED_IDENTIFIER),
        NULL, &context));
    ASSERT_EQ(0x77u, state.old_flags);

    /* A section of the same value after unrelated data is found anywhere. */
    const uint32_t other_first[] = {5u};
    nmo_chunk_t *chunk = nmo_chunk_create(f.arena);
    chunk->class_id = NMO_CID_BEHAVIORIO;
    chunk->data_version = 8;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, UNRELATED_IDENTIFIER);
    nmo_chunk_write_dword(chunk, other_first[0]);
    nmo_chunk_write_identifier(chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    nmo_chunk_write_dword(chunk, 0x31u);
    nmo_chunk_close(chunk);
    nmo_chunk_set_file_context(chunk, &f.read_context);
    ASSERT_EQ(NMO_OK, nmo_behaviorio_deserialize(&state, chunk, NULL, &context));
    ASSERT_EQ(0x31u, state.old_flags);

    nmo_behaviorio_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(f.arena);
}

TEST(section_schemas, behaviorio_writes_its_flags_section)
{
    fixture_t f;
    ASSERT_TRUE(fixture_init(&f));
    nmo_behaviorio_state_t state;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_vtable.create(&state, NULL, NULL));
    state.old_flags = 0x2Au;
    nmo_status_t status;

    nmo_chunk_t *chunk = written(&f, NMO_CID_BEHAVIORIO, 1, 0, nmo_behaviorio_serialize,
                                 &state, &status);
    ASSERT_EQ(NMO_OK, status);
    EXPECT_SECTION(chunk, CK_STATESAVE_BEHAV_IOFLAGS, 0x2Au);

    /* Without a file only the save flags that name the class's data write it. */
    chunk = written(&f, NMO_CID_BEHAVIORIO, 0, 0, nmo_behaviorio_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, chunk->data.count);
    chunk = written(&f, NMO_CID_BEHAVIORIO, 0, CK_STATESAVE_BEHAVIOONLY,
                    nmo_behaviorio_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    EXPECT_SECTION(chunk, CK_STATESAVE_BEHAV_IOFLAGS, 0x2Au);
    chunk = written(&f, NMO_CID_BEHAVIORIO, 0, 0x1u, nmo_behaviorio_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, chunk->data.count);

    /* A state whose flags were absent in the file writes no section. */
    state.has_flags = false;
    state.old_flags = 0;
    chunk = written(&f, NMO_CID_BEHAVIORIO, 1, 0, nmo_behaviorio_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, chunk->data.count);

    /* Flags without their section cannot be written. */
    state.old_flags = 3u;
    chunk = written(&f, NMO_CID_BEHAVIORIO, 1, 0, nmo_behaviorio_serialize, &state, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);
    ASSERT_EQ(0u, chunk->data.count);

    nmo_behaviorio_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(f.arena);
}

/* ------------------------------------------------------------- CKBehaviorLink */

static void link_expect_defaults(const nmo_behaviorlink_state_t *state)
{
    ASSERT_EQ(1, state->activation_delay);
    ASSERT_EQ(1, state->initial_activation_delay);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, state->in_io.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, state->out_io.raw_id);
}

TEST(section_schemas, behaviorlink_reads_the_new_and_the_legacy_layouts)
{
    fixture_t f;
    ASSERT_TRUE(fixture_init(&f));
    nmo_deserialize_context_t context = deserialize_context(&f);
    nmo_behaviorlink_state_t state;

    /* Nothing: the link has no format, and keeps the engine's delays. */
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&state, NULL, NULL));
    ASSERT_TRUE(state.has_format);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
        &state, empty_chunk(&f, NMO_CID_BEHAVIORLINK), NULL, &context));
    ASSERT_FALSE(state.has_format);
    ASSERT_FALSE(state.use_new_format);
    link_expect_defaults(&state);

    /* New layout: both delays in one dword, then the two ends. */
    const uint32_t new_layout[] = {0xFFFE0003u, 41u, 42u};
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
        &state,
        section_chunk(&f, NMO_CID_BEHAVIORLINK, CK_STATESAVE_BEHAV_LINK_NEWDATA, new_layout, 3, 0),
        NULL, &context));
    ASSERT_TRUE(state.has_format);
    ASSERT_TRUE(state.use_new_format);
    ASSERT_FALSE(state.has_legacy_curdelay || state.has_legacy_ios || state.has_legacy_delay);
    ASSERT_EQ(3, state.activation_delay);
    ASSERT_EQ(-2, state.initial_activation_delay);
    ASSERT_EQ(41u, state.in_io.raw_id);
    ASSERT_EQ(42u, state.out_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.in_io.state);

    /* Legacy layout: three sections, each present on its own. */
    const uint32_t cur[] = {9u};
    const uint32_t ios[] = {51u, 52u};
    const uint32_t del[] = {(uint32_t)-4};
    nmo_chunk_t *legacy = nmo_chunk_create(f.arena);
    legacy->class_id = NMO_CID_BEHAVIORLINK;
    legacy->data_version = 8;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_start_write(legacy);
    nmo_chunk_write_identifier(legacy, CK_STATESAVE_BEHAV_LINK_CURDELAY);
    nmo_chunk_write_dword(legacy, cur[0]);
    nmo_chunk_write_identifier(legacy, CK_STATESAVE_BEHAV_LINK_IOS);
    nmo_chunk_write_dword(legacy, ios[0]);
    nmo_chunk_write_dword(legacy, ios[1]);
    nmo_chunk_write_identifier(legacy, CK_STATESAVE_BEHAV_LINK_DELAY);
    nmo_chunk_write_dword(legacy, del[0]);
    nmo_chunk_close(legacy);
    nmo_chunk_set_file_context(legacy, &f.read_context);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(&state, legacy, NULL, &context));
    ASSERT_TRUE(state.has_format);
    ASSERT_FALSE(state.use_new_format);
    ASSERT_TRUE(state.has_legacy_curdelay && state.has_legacy_ios && state.has_legacy_delay);
    ASSERT_EQ(9, state.activation_delay);
    ASSERT_EQ(-4, state.initial_activation_delay);
    ASSERT_EQ(51u, state.in_io.raw_id);
    ASSERT_EQ(52u, state.out_io.raw_id);

    /* A legacy section alone leaves the rest at the defaults. */
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
        &state,
        section_chunk(&f, NMO_CID_BEHAVIORLINK, CK_STATESAVE_BEHAV_LINK_DELAY, del, 1, 0),
        NULL, &context));
    ASSERT_TRUE(state.has_format);
    ASSERT_TRUE(state.has_legacy_delay);
    ASSERT_FALSE(state.has_legacy_curdelay || state.has_legacy_ios);
    ASSERT_EQ(1, state.activation_delay);
    ASSERT_EQ(-4, state.initial_activation_delay);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, state.in_io.raw_id);

    /* The new layout wins: a legacy section next to it is not read. */
    nmo_chunk_t *both = nmo_chunk_create(f.arena);
    both->class_id = NMO_CID_BEHAVIORLINK;
    both->data_version = 8;
    both->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_start_write(both);
    nmo_chunk_write_identifier(both, CK_STATESAVE_BEHAV_LINK_NEWDATA);
    for (int i = 0; i < 3; ++i) nmo_chunk_write_dword(both, new_layout[i]);
    nmo_chunk_write_identifier(both, CK_STATESAVE_BEHAV_LINK_CURDELAY);
    nmo_chunk_write_dword(both, 77u);
    nmo_chunk_close(both);
    nmo_chunk_set_file_context(both, &f.read_context);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(&state, both, NULL, &context));
    ASSERT_TRUE(state.use_new_format);
    ASSERT_FALSE(state.has_legacy_curdelay);
    ASSERT_EQ(3, state.activation_delay);

    nmo_behaviorlink_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(f.arena);
}

TEST(section_schemas, behaviorlink_sections_have_exact_sizes_and_fail_atomically)
{
    fixture_t f;
    ASSERT_TRUE(fixture_init(&f));
    nmo_deserialize_context_t context = deserialize_context(&f);
    nmo_behaviorlink_state_t state;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&state, NULL, NULL));
    state.activation_delay = 11;
    state.initial_activation_delay = 12;
    state.in_io = nmo_ref_from_raw(901);
    state.out_io = nmo_ref_from_raw(902);
    state.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;

    static const struct {
        uint32_t identifier;
        size_t words;
    } layouts[] = {
        {CK_STATESAVE_BEHAV_LINK_NEWDATA, 3},
        {CK_STATESAVE_BEHAV_LINK_CURDELAY, 1},
        {CK_STATESAVE_BEHAV_LINK_IOS, 2},
        {CK_STATESAVE_BEHAV_LINK_DELAY, 1},
    };
    const uint32_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behaviorlink_deserialize(
            &state,
            section_chunk(&f, NMO_CID_BEHAVIORLINK, layouts[i].identifier, payload,
                          layouts[i].words - 1, 0),
            NULL, &context));
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behaviorlink_deserialize(
            &state,
            section_chunk(&f, NMO_CID_BEHAVIORLINK, layouts[i].identifier, payload,
                          layouts[i].words + 1, 0),
            NULL, &context));
        ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
            &state,
            section_chunk(&f, NMO_CID_BEHAVIORLINK, layouts[i].identifier, payload,
                          layouts[i].words, 0),
            NULL, &context));
        /* Back to a known state for the next layout. */
        state.activation_delay = 11;
        state.initial_activation_delay = 12;
        state.in_io = nmo_ref_from_raw(901);
        state.out_io = nmo_ref_from_raw(902);
        state.has_format = true;
        state.use_new_format = true;
        state.has_legacy_curdelay = state.has_legacy_ios = state.has_legacy_delay = false;
        state.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
    }

    /* A failed legacy read publishes nothing, not even the sections before it. */
    nmo_chunk_t *partial = nmo_chunk_create(f.arena);
    partial->class_id = NMO_CID_BEHAVIORLINK;
    partial->data_version = 8;
    partial->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_start_write(partial);
    nmo_chunk_write_identifier(partial, CK_STATESAVE_BEHAV_LINK_CURDELAY);
    nmo_chunk_write_dword(partial, 6u);
    nmo_chunk_write_identifier(partial, CK_STATESAVE_BEHAV_LINK_IOS);
    nmo_chunk_write_dword(partial, 7u);
    nmo_chunk_close(partial);
    nmo_chunk_set_file_context(partial, &f.read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_behaviorlink_deserialize(&state, partial, NULL, &context));
    ASSERT_EQ(11, state.activation_delay);
    ASSERT_EQ(12, state.initial_activation_delay);
    ASSERT_EQ(901u, state.in_io.raw_id);
    ASSERT_EQ(902u, state.out_io.raw_id);
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, state.base.visibility_flags);

    nmo_behaviorlink_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(f.arena);
}

TEST(section_schemas, behaviorlink_writes_the_layout_it_was_read_in)
{
    fixture_t f;
    ASSERT_TRUE(fixture_init(&f));
    nmo_behaviorlink_state_t state;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&state, NULL, NULL));
    nmo_status_t status;

    /* New layout: the delays share a dword, activation in the low half. */
    state.activation_delay = 3;
    state.initial_activation_delay = -2;
    state.in_io = nmo_ref_from_raw(41);
    state.out_io = nmo_ref_from_raw(42);
    nmo_chunk_t *chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize,
                                 &state, &status);
    ASSERT_EQ(NMO_OK, status);
    EXPECT_SECTION(chunk, CK_STATESAVE_BEHAV_LINK_NEWDATA, 0xFFFE0003u, 41u, 42u);

    /* The same bytes without a file, under the save flags of the class. */
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 0, 0, nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, chunk->data.count);
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 0, CK_STATESAVE_BEHAV_LINKONLY,
                    nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(5u, chunk->data.count);

    /* Legacy layout: only the sections the file held, in a fixed order. */
    state.use_new_format = false;
    state.has_legacy_curdelay = true;
    state.has_legacy_delay = true;
    state.has_legacy_ios = false;
    state.in_io = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state.out_io = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state.activation_delay = 9;
    state.initial_activation_delay = -4;
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    {
        const uint32_t expected[] = {
            CK_STATESAVE_BEHAV_LINK_CURDELAY, 3u, 9u,
            CK_STATESAVE_BEHAV_LINK_DELAY, 0u, (uint32_t)-4,
        };
        ASSERT_TRUE(data_is(chunk, expected, sizeof(expected) / sizeof(expected[0])));
    }
    state.has_legacy_ios = true;
    state.in_io = nmo_ref_from_raw(51);
    state.out_io = nmo_ref_from_raw(52);
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    {
        const uint32_t expected[] = {
            CK_STATESAVE_BEHAV_LINK_CURDELAY, 3u, 9u,
            CK_STATESAVE_BEHAV_LINK_IOS, 7u, 51u, 52u,
            CK_STATESAVE_BEHAV_LINK_DELAY, 0u, (uint32_t)-4,
        };
        ASSERT_TRUE(data_is(chunk, expected, sizeof(expected) / sizeof(expected[0])));
    }

    /* A link without a format section writes nothing. */
    state.has_format = false;
    state.use_new_format = false;
    state.has_legacy_curdelay = state.has_legacy_ios = state.has_legacy_delay = false;
    state.activation_delay = 1;
    state.initial_activation_delay = 1;
    state.in_io = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state.out_io = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, chunk->data.count);

    nmo_behaviorlink_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(f.arena);
}

TEST(section_schemas, behaviorlink_refuses_states_it_cannot_write_losslessly)
{
    fixture_t f;
    ASSERT_TRUE(fixture_init(&f));
    nmo_behaviorlink_state_t state;
    nmo_status_t status;

    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&state, NULL, NULL));
    state.has_legacy_ios = true;  /* the new layout next to a legacy one */
    nmo_chunk_t *chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize,
                                 &state, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);
    ASSERT_EQ(0u, chunk->data.count);

    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&state, NULL, NULL));
    state.use_new_format = false;  /* a legacy format with no section */
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);

    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&state, NULL, NULL));
    state.use_new_format = false;
    state.has_legacy_delay = true;
    state.activation_delay = 5;  /* a delay no legacy section holds */
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);

    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&state, NULL, NULL));
    state.has_format = false;  /* data with no format section */
    state.use_new_format = false;
    state.in_io = nmo_ref_from_raw(61);
    chunk = written(&f, NMO_CID_BEHAVIORLINK, 1, 0, nmo_behaviorlink_serialize, &state, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);

    nmo_behaviorlink_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(f.arena);
}

/* ----------------------------------------------------------- CKKinematicChain */

TEST(section_schemas, kinematicchain_reads_and_writes_its_chain_section)
{
    fixture_t f;
    ASSERT_TRUE(fixture_init(&f));
    nmo_deserialize_context_t context = deserialize_context(&f);
    nmo_kinematicchain_state_t state;
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.create(&state, NULL, NULL));
    state.has_chain_data = 1;
    state.reserved_ref = nmo_ref_from_raw(7);
    state.start_effector = nmo_ref_from_raw(8);
    state.end_effector = nmo_ref_from_raw(9);

    /* No section: the chain data is absent and the references are cleared. */
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_deserialize(
        &state, empty_chunk(&f, NMO_CID_KINEMATICCHAIN), NULL, &context));
    ASSERT_EQ(0, state.has_chain_data);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, state.reserved_ref.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, state.start_effector.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, state.end_effector.raw_id);

    const uint32_t chain[] = {31u, 32u, 33u};
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_deserialize(
        &state,
        section_chunk(&f, NMO_CID_KINEMATICCHAIN, CK_STATESAVE_KINEMATICCHAINALL, chain, 3, 0),
        NULL, &context));
    ASSERT_EQ(1, state.has_chain_data);
    ASSERT_EQ(31u, state.reserved_ref.raw_id);
    ASSERT_EQ(32u, state.start_effector.raw_id);
    ASSERT_EQ(33u, state.end_effector.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, state.end_effector.state);

    /* Three dwords are the minimum; a longer section is read as far as it is known. */
    const uint32_t longer[] = {41u, 42u, 43u, 44u};
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_deserialize(
        &state,
        section_chunk(&f, NMO_CID_KINEMATICCHAIN, CK_STATESAVE_KINEMATICCHAINALL, longer, 4, 0),
        NULL, &context));
    ASSERT_EQ(41u, state.reserved_ref.raw_id);
    ASSERT_EQ(43u, state.end_effector.raw_id);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_kinematicchain_deserialize(
        &state,
        section_chunk(&f, NMO_CID_KINEMATICCHAIN, CK_STATESAVE_KINEMATICCHAINALL, chain, 2, 0),
        NULL, &context));
    ASSERT_EQ(41u, state.reserved_ref.raw_id);
    ASSERT_EQ(43u, state.end_effector.raw_id);

    nmo_status_t status;
    state.reserved_ref = nmo_ref_from_raw(31);
    state.start_effector = nmo_ref_from_raw(32);
    state.end_effector = nmo_ref_from_raw(33);
    nmo_chunk_t *chunk = written(&f, NMO_CID_KINEMATICCHAIN, 1, 0, nmo_kinematicchain_serialize,
                                 &state, &status);
    ASSERT_EQ(NMO_OK, status);
    EXPECT_SECTION(chunk, CK_STATESAVE_KINEMATICCHAINALL, 31u, 32u, 33u);

    chunk = written(&f, NMO_CID_KINEMATICCHAIN, 0, 0, nmo_kinematicchain_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, chunk->data.count);
    chunk = written(&f, NMO_CID_KINEMATICCHAIN, 0, CK_STATESAVE_KINEMATICCHAINALL,
                    nmo_kinematicchain_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    EXPECT_SECTION(chunk, CK_STATESAVE_KINEMATICCHAINALL, 31u, 32u, 33u);

    state.has_chain_data = 0;
    chunk = written(&f, NMO_CID_KINEMATICCHAIN, 1, 0, nmo_kinematicchain_serialize, &state, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, chunk->data.count);

    nmo_kinematicchain_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(f.arena);
}

/* ---- The class a reference must have ---- */

typedef struct class_world {
    nmo_type_registry_t *types;
    nmo_object_repository_t *repository;
    nmo_type_runtime_t runtime;
    nmo_chunk_file_context_t file_context;
} class_world_t;

/* Files ids 11 and 12 name objects of the wrong class, 21 and 22 a behavior IO
 * and a body part. */
static int class_world_init(fixture_t *f, class_world_t *w)
{
    memset(w, 0, sizeof(*w));
    w->types = nmo_type_registry_create(f->arena);
    if (w->types == NULL || nmo_register_builtin_types(w->types) != NMO_OK ||
        nmo_register_object_types(w->types) != NMO_OK) {
        return 0;
    }
    w->repository = nmo_object_repository_create(NULL);
    if (w->repository == NULL) return 0;
    /* Not static: the GUIDs are compound literals. */
    const struct {
        nmo_object_id_t id;
        nmo_class_id_t class_id;
        nmo_guid_t guid;
    } objects[] = {
        {1101u, NMO_CID_SCENEOBJECT, CKPGUID_SCENEOBJECT},
        {1102u, NMO_CID_SCENEOBJECT, CKPGUID_SCENEOBJECT},
        {1201u, NMO_CID_BEHAVIORIO, CKPGUID_BEHAVIORIO},
        {1202u, NMO_CID_BODYPART, CKPGUID_BODYPART},
    };
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i) {
        nmo_object_t *object = nmo_object_create(NULL, objects[i].id, objects[i].class_id);
        if (object == NULL || nmo_object_set_type_guid(object, objects[i].guid) != NMO_OK ||
            nmo_object_repository_add(w->repository, &object) != NMO_OK) {
            return 0;
        }
    }
    static const nmo_object_id_t file_ids[] = {11u, 12u, 21u, 22u};
    static const nmo_object_id_t runtime_ids[] = {1101u, 1102u, 1201u, 1202u};
    for (size_t i = 0; i < 4; ++i) {
        if (nmo_id_remap_add(f->file_to_runtime, file_ids[i], runtime_ids[i]) != NMO_OK) return 0;
    }
    w->runtime.types = w->types;
    w->file_context.file_to_runtime = f->file_to_runtime;
    w->file_context.ref_tokens = nmo_object_repository_ref_tokens(w->repository);
    return 1;
}

TEST(section_schemas, references_are_checked_against_the_class_they_must_have)
{
    fixture_t f;
    class_world_t w;
    ASSERT_TRUE(fixture_init(&f));
    ASSERT_TRUE(class_world_init(&f, &w));
    nmo_deserialize_context_t context = nmo_deserialize_context_create(
        f.arena, w.repository, &w.runtime, NMO_DESER_FLAG_FILE_MODE);

    /* A link's ends must be behavior IOs, in the new and in the legacy layout. */
    nmo_behaviorlink_state_t link;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&link, NULL, NULL));
    const uint32_t new_layout[] = {0x00020001u, 11u, 21u};
    nmo_chunk_t *chunk = section_chunk(&f, NMO_CID_BEHAVIORLINK, CK_STATESAVE_BEHAV_LINK_NEWDATA,
                                       new_layout, 3, 0);
    nmo_chunk_set_file_context(chunk, &w.file_context);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(&link, chunk, NULL, &context));
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH, link.in_io.state);
    ASSERT_EQ(11u, link.in_io.raw_id);
    ASSERT_EQ(1101u, link.in_io.id);
    ASSERT_EQ(NMO_REF_RESOLVED, link.out_io.state);
    ASSERT_EQ(1201u, link.out_io.id);

    const uint32_t ios[] = {21u, 12u};
    chunk = section_chunk(&f, NMO_CID_BEHAVIORLINK, CK_STATESAVE_BEHAV_LINK_IOS, ios, 2, 0);
    nmo_chunk_set_file_context(chunk, &w.file_context);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(&link, chunk, NULL, &context));
    ASSERT_EQ(NMO_REF_RESOLVED, link.in_io.state);
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH, link.out_io.state);

    /* A chain's effectors must be body parts; its reserved reference is not checked. */
    nmo_kinematicchain_state_t chain;
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.create(&chain, NULL, NULL));
    const uint32_t chain_words[] = {11u, 22u, 12u};
    chunk = section_chunk(&f, NMO_CID_KINEMATICCHAIN, CK_STATESAVE_KINEMATICCHAINALL,
                          chain_words, 3, 0);
    nmo_chunk_set_file_context(chunk, &w.file_context);
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_deserialize(&chain, chunk, NULL, &context));
    ASSERT_EQ(NMO_REF_RESOLVED, chain.reserved_ref.state);
    ASSERT_EQ(NMO_REF_RESOLVED, chain.start_effector.state);
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH, chain.end_effector.state);

    nmo_behaviorlink_vtable.destroy(&link, NULL, NULL);
    nmo_kinematicchain_vtable.destroy(&chain, NULL, NULL);
    nmo_object_repository_destroy(w.repository);
    nmo_type_registry_destroy(w.types);
    nmo_arena_destroy(f.arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(section_schemas, behaviorio_reads_its_flags_section);
    REGISTER_TEST(section_schemas, behaviorio_writes_its_flags_section);
    REGISTER_TEST(section_schemas, behaviorlink_reads_the_new_and_the_legacy_layouts);
    REGISTER_TEST(section_schemas, behaviorlink_sections_have_exact_sizes_and_fail_atomically);
    REGISTER_TEST(section_schemas, behaviorlink_writes_the_layout_it_was_read_in);
    REGISTER_TEST(section_schemas, behaviorlink_refuses_states_it_cannot_write_losslessly);
    REGISTER_TEST(section_schemas, kinematicchain_reads_and_writes_its_chain_section);
    REGISTER_TEST(section_schemas, references_are_checked_against_the_class_they_must_have);
TEST_MAIN_END()
