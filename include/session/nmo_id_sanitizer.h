/**
 * @file nmo_id_sanitizer.h
 * @brief Session-level ID sanitization and mapping helpers.
 *
 * Implements the ID "clean-up" pipeline described in IMPROVEMENT_PLAN.md:
 * - Strips the 0x800000 mask used for reference-only objects
 * - Tracks negative external references
 * - Maintains File Index (0-based) <-> Runtime ID (1-based) mappings
 */

#ifndef NMO_ID_SANITIZER_H
#define NMO_ID_SANITIZER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct nmo_id_sanitizer nmo_id_sanitizer_t;

/** Reference-only mask present on some raw IDs. */
#define NMO_ID_REF_MASK 0x800000u

/**
 * @brief Create an ID sanitizer instance.
 *
 * All allocations use the provided arena; the caller retains ownership
 * of the arena lifetime.
 */
NMO_API nmo_id_sanitizer_t *nmo_id_sanitizer_create(nmo_arena_t *arena);

/**
 * @brief Destroy sanitizer state (hash tables), but not the arena.
 */
NMO_API void nmo_id_sanitizer_destroy(nmo_id_sanitizer_t *sanitizer);

/**
 * @brief Clear all recorded mappings.
 */
NMO_API void nmo_id_sanitizer_reset(nmo_id_sanitizer_t *sanitizer);

/**
 * @brief Strip the reference-only mask from a raw ID.
 *
 * Example: 0x80000042 -> 0x42.
 */
NMO_API uint32_t nmo_id_sanitize(uint32_t raw_id);

/**
 * @brief Register a File Index -> Runtime ID mapping.
 */
NMO_API int nmo_id_sanitizer_register(nmo_id_sanitizer_t *sanitizer,
                                      uint32_t file_index,
                                      uint32_t runtime_id);

/**
 * @brief Register a negative external reference ID.
 *
 * Returns the positive runtime ID derived from the negative input.
 * On error, returns NMO_OBJECT_ID_INVALID.
 */
NMO_API int32_t nmo_id_register_external(nmo_id_sanitizer_t *sanitizer,
                                         int32_t negative_id);

/**
 * @brief Bulk reset-and-register convenience for remapped IDs.
 *
 * Clears existing tables and registers a set of file_index -> runtime_id pairs.
 */
NMO_API int nmo_id_sanitizer_reseed(nmo_id_sanitizer_t *sanitizer,
                                    const uint32_t *file_indices,
                                    const uint32_t *runtime_ids,
                                    size_t count);

/**
 * @brief Lookup runtime ID from a file index.
 *
 * Returns NMO_OBJECT_ID_INVALID if not found.
 */
NMO_API uint32_t nmo_id_file_to_runtime(const nmo_id_sanitizer_t *sanitizer,
                                        uint32_t file_index);

/**
 * @brief Lookup file index from a runtime ID.
 *
 * Returns NMO_OBJECT_ID_INVALID if not found.
 */
NMO_API uint32_t nmo_id_runtime_to_file(const nmo_id_sanitizer_t *sanitizer,
                                        uint32_t runtime_id);

/**
 * @brief Lookup the original negative ID recorded for an external reference.
 *
 * Returns 0 if none exists for the runtime ID.
 */
NMO_API int32_t nmo_id_original_external(const nmo_id_sanitizer_t *sanitizer,
                                         uint32_t runtime_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ID_SANITIZER_H */
