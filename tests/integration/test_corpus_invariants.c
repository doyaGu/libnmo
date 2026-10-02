/**
 * @file test_corpus_invariants.c
 * @brief Decoded values of the reference corpus satisfy what the engine guarantees
 *
 * test_corpus_chunk_roundtrip proves that no bytes are lost; it cannot tell a
 * field that was read correctly from one that was read into the wrong member.
 * These checks decode the corpus and assert relationships that the Virtools
 * engine maintains, so a swapped or misread field shows up as a violation.
 */

#include "../test_framework.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_session.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REPORTED_VIOLATIONS 20

typedef struct corpus_invariants {
    nmo_context_t *ctx;
    size_t files;
    size_t load_errors;

    size_t skins;
    size_t skin_vertices;
    size_t skin_weight_violations;
    size_t skin_index_violations;

    size_t characters;
    size_t characters_without_root;
    size_t characters_without_effective_root;

    size_t reported;
} corpus_invariants_t;

static int is_3dentity_family(nmo_class_id_t class_id)
{
    switch (class_id) {
    case NMO_CID_3DENTITY:
    case NMO_CID_CAMERA:
    case NMO_CID_TARGETCAMERA:
    case NMO_CID_SPRITE3D:
    case NMO_CID_LIGHT:
    case NMO_CID_TARGETLIGHT:
    case NMO_CID_CHARACTER:
    case NMO_CID_3DOBJECT:
    case NMO_CID_BODYPART:
    case NMO_CID_CURVE:
        return 1;
    default:
        return 0;
    }
}

/* The weights of a skinned vertex add up to one and each bone index names one
 * of the skin's bones. A reader that swaps the two buffers fails both. */
static void check_skin(corpus_invariants_t *stats, const char *path,
                       const nmo_object_t *object,
                       const nmo_3dentity_skin_t *skin)
{
    stats->skins++;
    for (uint32_t v = 0; v < skin->vertex_count; v++) {
        const nmo_3dentity_skin_vertex_t *vertex = &skin->vertices[v];
        stats->skin_vertices++;

        double weight_sum = 0.0;
        int indices_valid = 1;
        for (uint32_t k = 0; k < vertex->bone_count; k++) {
            weight_sum += (double)vertex->bone_weights[k];
            if (vertex->bone_indices[k] >= skin->bone_count) {
                indices_valid = 0;
            }
        }

        if (vertex->bone_count > 0 && fabs(weight_sum - 1.0) > 0.01) {
            if (stats->reported++ < MAX_REPORTED_VIOLATIONS) {
                printf("  %s: object %u vertex %u: bone weights sum to %g\n", path,
                       (unsigned)object->file_id, (unsigned)v, weight_sum);
            }
            stats->skin_weight_violations++;
        }
        if (!indices_valid) {
            if (stats->reported++ < MAX_REPORTED_VIOLATIONS) {
                printf("  %s: object %u vertex %u: bone index outside %u bones\n", path,
                       (unsigned)object->file_id, (unsigned)v,
                       (unsigned)skin->bone_count);
            }
            stats->skin_index_violations++;
        }
    }
}

static void check_file(const char *path, void *user)
{
    corpus_invariants_t *stats = (corpus_invariants_t *)user;
    stats->files++;

    nmo_session_t *session = nmo_session_create(stats->ctx);
    if (session == NULL || nmo_session_load_file(session, path, NULL, NULL) != NMO_OK) {
        stats->load_errors++;
        printf("  %s: load failed\n", path);
        nmo_session_destroy(session);
        return;
    }

    nmo_object_repository_t *repository = nmo_session_get_repository(session);
    size_t count = nmo_object_repository_get_count(repository);
    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(repository, i);
        if (object != NULL && object->class_id == NMO_CID_CHARACTER) {
            /* A character without a stored root takes its first child as the
               root; the one such character of the corpus has no children (its
               body parts name it as their character but have no parent). */
            const nmo_character_state_t *character =
                (const nmo_character_state_t *)nmo_object_get_state(object);
            stats->characters++;
            if (character != NULL &&
                nmo_ref_runtime_id(&character->root_body_part) == NMO_OBJECT_ID_NONE) {
                stats->characters_without_root++;
                if (nmo_character_effective_root_body_part(repository, object) ==
                    NMO_OBJECT_ID_NONE) {
                    stats->characters_without_effective_root++;
                }
            }
        }
        if (object == NULL || !is_3dentity_family(object->class_id)) {
            continue;
        }
        const nmo_3dentity_state_t *entity =
            (const nmo_3dentity_state_t *)nmo_object_get_state(object);
        if (entity != NULL && entity->skin != NULL) {
            check_skin(stats, path, object, entity->skin);
        }
    }

    nmo_session_destroy(session);
}

TEST(corpus_invariants, decoded_values_satisfy_engine_guarantees)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_invariants_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.ctx = ctx;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file, &stats);
    nmo_context_release(ctx);

    printf("  Corpus invariants: files=%zu load_errors=%zu skins=%zu skin_vertices=%zu "
           "skin_weight_violations=%zu skin_index_violations=%zu\n",
           stats.files, stats.load_errors, stats.skins, stats.skin_vertices,
           stats.skin_weight_violations, stats.skin_index_violations);

    ASSERT_EQ(0, walk_status);
    ASSERT_GE(stats.files, 1u);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.skin_weight_violations);
    ASSERT_EQ(0u, stats.skin_index_violations);
    printf("  Characters: %zu, without stored root: %zu, without effective root: %zu\n",
           stats.characters, stats.characters_without_root,
           stats.characters_without_effective_root);
    ASSERT_GE(stats.characters_without_root, 1u);
    ASSERT_EQ(stats.characters_without_root, stats.characters_without_effective_root);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(corpus_invariants, decoded_values_satisfy_engine_guarantees);
TEST_MAIN_END()
