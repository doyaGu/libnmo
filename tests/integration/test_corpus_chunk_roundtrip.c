/**
 * @file test_corpus_chunk_roundtrip.c
 * @brief Every object chunk of the reference corpus survives save and reload
 *
 * The document-level round-trip tests compare two loads of the same writer,
 * so they cannot tell a writer that mirrors a wrong reader from a correct
 * one. This test compares bytes instead: it loads a file, saves it with
 * schema serialization required (no raw chunk reuse), reloads the result and
 * checks that each object's chunk payload, identifier lists and sub-chunks
 * are identical to the chunk that came out of the original file.
 *
 * A failure means a schema drops, reorders or rewrites data that the file
 * contained. It says nothing about whether fields are interpreted correctly;
 * that needs a reference such as the Virtools engine binaries.
 */

#include "chunk_roundtrip_check.h"

#include "format/nmo_interface_chunk.h"
#include "object/builtin/nmo_behavior_schemas.h"

TEST(corpus_chunk_roundtrip, every_object_chunk_survives_save_and_reload)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_chunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = ctx;
    stats.require_schema = 1;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file_roundtrip, &stats);
    remove(SCRATCH_FILE);
    nmo_context_release(ctx);

    printf("  Chunk round-trip corpus: files=%zu objects=%zu load_errors=%zu "
           "save_errors=%zu reload_errors=%zu count_mismatches=%zu "
           "missing=%zu chunk_mismatches=%zu padding_dwords=%zu known_gaps=%zu\n",
           stats.files, stats.objects, stats.load_errors, stats.save_errors,
           stats.reload_errors, stats.count_mismatches, stats.missing_objects,
           stats.chunk_mismatches, stats.padding_fixes, stats.known_gap_objects);

    ASSERT_EQ(0, walk_status);
    ASSERT_GE(stats.files, 1u);
    ASSERT_GE(stats.objects, 1u);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.save_errors);
    ASSERT_EQ(0u, stats.reload_errors);
    ASSERT_EQ(0u, stats.count_mismatches);
    ASSERT_EQ(0u, stats.missing_objects);
    ASSERT_EQ(0u, stats.chunk_mismatches);
}

/* A default save keeps the chunk of an object nobody touched, so not even the
 * padding Virtools leaves behind strings and buffers may change. */
TEST(corpus_chunk_roundtrip, default_save_keeps_untouched_objects_byte_exact)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_chunk_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = ctx;
    stats.require_schema = 0;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file_roundtrip, &stats);
    remove(SCRATCH_FILE);
    nmo_context_release(ctx);

    printf("  Default-save corpus: files=%zu objects=%zu chunk_mismatches=%zu "
           "padding_dwords=%zu\n", stats.files, stats.objects, stats.chunk_mismatches,
           stats.padding_fixes);

    ASSERT_EQ(0, walk_status);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.save_errors);
    ASSERT_EQ(0u, stats.reload_errors);
    ASSERT_EQ(0u, stats.count_mismatches);
    ASSERT_EQ(0u, stats.missing_objects);
    ASSERT_EQ(0u, stats.chunk_mismatches);
    ASSERT_EQ(0u, stats.padding_fixes);
}

/* Deleting an object moves the ones after it; the others keep their data. */
TEST(corpus_chunk_roundtrip, deleting_an_object_keeps_the_others_intact)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    char path[1024];
    snprintf(path, sizeof(path), "%s/Ballance/base.cmo", NMO_TEST_DATA_DIR);
    /* Parameters that pointed at the deleted array were detached from it, and the level
     * scene lists the objects of the file by index. */
    const nmo_class_id_t may_change[] = {NMO_CID_LEVEL, NMO_CID_PARAMETEROUT,
                                         NMO_CID_PARAMETERIN, NMO_CID_PARAMETERLOCAL};
    check_deletion_keeps_the_others(ctx, path, NMO_CID_DATAARRAY, 100u, may_change,
                                    sizeof(may_change) / sizeof(may_change[0]));
    nmo_context_release(ctx);
}

/* The interface chunk of a behavior, parsed and written again, is the chunk
 * it came from (the snapshot bitmap is kept as the file held it). */
typedef struct interface_stats {
    nmo_context_t *ctx;
    size_t files;
    size_t parsed;
    size_t different;
} interface_stats_t;

static void check_interfaces(const char *path, void *user)
{
    interface_stats_t *stats = (interface_stats_t *)user;
    stats->files++;
    nmo_session_t *session = load_session(stats->ctx, path);
    if (session == NULL) return;
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_behavior_interface_parse_stats_t parse_stats;
    (void)nmo_behavior_parse_all_interfaces_ex(repo, NULL, &parse_stats);
    for (size_t i = 0; i < nmo_object_repository_get_count(repo); i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL || object->class_id != NMO_CID_BEHAVIOR) continue;
        nmo_behavior_state_t *state = (nmo_behavior_state_t *)nmo_object_get_state(object);
        if (state == NULL || state->interface_chunk == NULL || state->interface_data == NULL) continue;
        stats->parsed++;
        nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 16);
        nmo_chunk_t *written = arena != NULL ? nmo_chunk_create(arena) : NULL;
        if (written == NULL || nmo_chunk_start_write(written) != NMO_OK ||
            nmo_interface_chunk_write(written, state->interface_data, NULL) != NMO_OK) {
            stats->different++;
        } else {
            nmo_chunk_close(written);
            if (!nmo_chunk_equivalent_to_tracked(state->interface_chunk, written)) {
                if (stats->different++ < MAX_REPORTED_MISMATCHES) {
                    printf("  %s: behavior %u interface differs\n", path, (unsigned)object->id);
                }
            }
        }
        nmo_arena_destroy(arena);
    }
    nmo_session_destroy(session);
}

TEST(corpus_chunk_roundtrip, parsed_interfaces_write_back_to_their_chunk)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    interface_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = ctx;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_interfaces, &stats);
    nmo_context_release(ctx);
    printf("  Interface corpus: files=%zu parsed=%zu different=%zu\n",
           stats.files, stats.parsed, stats.different);
    ASSERT_EQ(0, walk_status);
    ASSERT_GE(stats.parsed, 1u);
    ASSERT_EQ(0u, stats.different);
}

TEST_MAIN_BEGIN()
    /* A pass over the whole corpus; slow under a sanitizer. */
    REGISTER_TEST_WITH_TIMEOUT(corpus_chunk_roundtrip, every_object_chunk_survives_save_and_reload, 300.0);
    REGISTER_TEST_WITH_TIMEOUT(corpus_chunk_roundtrip, default_save_keeps_untouched_objects_byte_exact, 300.0);
    REGISTER_TEST_WITH_TIMEOUT(corpus_chunk_roundtrip, deleting_an_object_keeps_the_others_intact, 300.0);
    REGISTER_TEST_WITH_TIMEOUT(corpus_chunk_roundtrip, parsed_interfaces_write_back_to_their_chunk, 300.0);
TEST_MAIN_END()
