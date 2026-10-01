/**
 * @file nmo_chunk_residue.h
 * @brief Keeping what a schema does not model when an object is saved again
 *
 * A schema reads the fields the engine reads; a chunk written by another
 * program, an older engine or a newer one can hold more: whole sections the
 * schema does not know, or dwords after the fields of a known section. Saving
 * the object from its state alone would drop them.
 *
 * The state of an object that was loaded is serialized once after the load
 * (the canonical chunk). What the original chunk holds beyond the canonical
 * one is the residue, and it is carried into every chunk written for the
 * object afterwards.
 */

#ifndef NMO_CHUNK_RESIDUE_H
#define NMO_CHUNK_RESIDUE_H

#include "nmo_types.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_id_remap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 64-bit digest of everything a chunk holds (data, id lists, sub-chunks). */
NMO_API uint64_t nmo_chunk_digest(const nmo_chunk_t *chunk);

/**
 * @brief Whether two chunks hold the same content.
 *
 * The values at object id positions are not compared (the chunks can be in
 * different id spaces), and a dword whose upper bytes were cleared by the
 * writer counts as equal, as the uninitialised padding of Virtools' strings
 * and buffers does.
 */
NMO_API bool nmo_chunk_equivalent(const nmo_chunk_t *a, const nmo_chunk_t *b);

/**
 * @brief Like nmo_chunk_equivalent, for a chunk that tracks no ids.
 *
 * The id positions of tracked are taken to be those of untracked too; the
 * values at those positions are not compared.
 */
NMO_API bool nmo_chunk_equivalent_to_tracked(
    const nmo_chunk_t *untracked, const nmo_chunk_t *tracked);

/**
 * @brief Translate the object ids of a chunk and its sub-chunks in place.
 *
 * An id found in map is replaced by its image. An id that is not in map but is
 * below unmapped_limit names an object of the source file that has no image
 * (it is gone); *out_unresolved is then set and the id is left alone. Larger
 * ids, and the null id 0xFFFFFFFF, are left alone.
 */
NMO_API nmo_status_t nmo_chunk_translate_ids(
    nmo_chunk_t *chunk,
    const nmo_id_remap_t *map,
    uint32_t unmapped_limit,
    bool *out_unresolved);

/**
 * @brief Like nmo_chunk_translate_ids, for a chunk that does not track its ids.
 *
 * A chunk read from a file does not know where its ids are (the ids of a file
 * chunk are converted while it is read). layout is a chunk with the same
 * structure whose id positions are known, for instance the same object's state
 * serialized again; chunk and layout must have the same sub-chunk structure.
 */
NMO_API nmo_status_t nmo_chunk_translate_ids_with_layout(
    nmo_chunk_t *chunk,
    const nmo_chunk_t *layout,
    const nmo_id_remap_t *map,
    uint32_t unmapped_limit,
    bool *out_unresolved);

/** @brief What nmo_chunk_merge_residue kept. */
typedef struct nmo_chunk_residue_stats {
    size_t sections_kept;   /**< sections the canonical chunk does not have */
    size_t tails_kept;      /**< trailing dwords of sections both have */
    size_t skipped;         /**< residue that could not be carried over */
    size_t values_restored; /**< dwords the edit left alone that get the file's value back */
    size_t ids_unknown;     /**< carried pieces that may hold object ids that moved (original tracks none) */
} nmo_chunk_residue_stats_t;

/**
 * @brief Add to target what original holds beyond canonical.
 *
 * target, original and canonical must have the same chunk and data versions;
 * otherwise the layouts differ and nothing is merged. Object ids of the kept
 * dwords are translated through original_to_target when that is not NULL.
 * Residue that contains sub-chunks or manager references is skipped.
 *
 * @param target               Chunk written for the object now; modified
 * @param original             Chunk the object was loaded from
 * @param canonical            Serialization of the state as loaded
 * @param original_to_target   Maps the ids of original to the ids of target
 * @param arena                Scratch for the merge and the new arrays
 * @param out_stats            Optional
 */
NMO_API nmo_status_t nmo_chunk_merge_residue(
    nmo_chunk_t *target,
    const nmo_chunk_t *original,
    const nmo_chunk_t *canonical,
    const nmo_id_remap_t *original_to_target,
    nmo_arena_t *arena,
    nmo_chunk_residue_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_RESIDUE_H */
