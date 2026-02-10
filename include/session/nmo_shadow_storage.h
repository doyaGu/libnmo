/**
 * @file nmo_shadow_storage.h
 * @brief Session-level shadow blob preservation for round-trip fidelity.
 *
 * Implements the Shadow Blob mechanism described in IMPROVEMENT_PLAN.md Phase 1.2:
 * - Preserves Included Files blob verbatim from nmo_file_chunk_t::included_files
 * - Captures Chunk "raw tails" - unparsed bytes remaining after schema deserialization
 *
 * This allows libnmo to round-trip Virtools files without data loss, even for
 * unknown/unparsed portions of the format.
 */

#ifndef NMO_SHADOW_STORAGE_H
#define NMO_SHADOW_STORAGE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
/* OWNERSHIP:
 * - owner: caller
 * - allocator: arena (storage + included files), heap (chunk tails)
 * - lifetime: until nmo_shadow_storage_destroy()
 * - free: nmo_shadow_storage_destroy()/reset
 * - note: included-files overwrite uses arena mark/rewind to reclaim prior capture
 * - thread: caller-synchronized
 */
typedef struct nmo_shadow_storage nmo_shadow_storage_t;

/**
 * @brief Opaque buffer structure for shadow data.
 */
typedef struct nmo_shadow_buffer {
    void *data;
    size_t size;
} nmo_shadow_buffer_t;

/**
 * @brief Create a shadow storage instance.
 *
 * Included files data uses the provided arena; chunk tail data uses
 * heap allocations that are released on reset/destroy.
 *
 * @param arena Arena for allocations (required, not NULL)
 * @return New shadow storage, or NULL on error
 */
NMO_API nmo_shadow_storage_t *nmo_shadow_storage_create(nmo_arena_t *arena);

/**
 * @brief Destroy shadow storage state (hash tables), but not the arena.
 *
 * @param storage Storage to destroy (may be NULL)
 */
NMO_API void nmo_shadow_storage_destroy(nmo_shadow_storage_t *storage);

/**
 * @brief Clear all captured shadow data.
 *
 * @param storage Storage to reset
 */
NMO_API void nmo_shadow_storage_reset(nmo_shadow_storage_t *storage);

/**
 * @brief Capture the Included Files blob from the file header.
 *
 * The data is copied into arena-allocated memory. Calling this again
 * rewinds the previous included-files capture scope and overwrites it.
 * OWNERSHIP:
 * - storage owns the copied data
 * - valid until reset/destroy or next capture
 *
 * @param storage Shadow storage instance
 * @param data    Pointer to included files data
 * @param size    Size of data in bytes
 * @return NMO_OK on success, error code otherwise
 */
NMO_API int nmo_shadow_capture_included_files(nmo_shadow_storage_t *storage,
                                               const void *data, size_t size);

/**
 * @brief Capture a chunk's raw tail (unparsed trailing bytes).
 *
 * This preserves bytes that were not consumed during deserialization,
 * keyed by the chunk's object ID. Calling again for the same chunk_id
 * overwrites the previous capture.
 * OWNERSHIP:
 * - storage owns the copied data
 * - valid until reset/destroy or next capture for the same chunk_id
 *
 * @param storage   Shadow storage instance
 * @param chunk_id  Object ID (runtime ID) of the chunk owner
 * @param tail      Pointer to unparsed tail data
 * @param tail_size Size of tail in bytes
 * @return NMO_OK on success, error code otherwise
 */
NMO_API int nmo_shadow_capture_chunk_tail(nmo_shadow_storage_t *storage,
                                           uint32_t chunk_id,
                                           const void *tail, size_t tail_size);

/**
 * @brief Retrieve the captured Included Files blob.
 *
 * @param storage  Shadow storage instance
 * @param out_size Output: size of returned data (may be NULL)
 * @return Pointer to data, or NULL if not captured
 * @note Returned pointer is storage-owned; do not free.
 */
NMO_API const void *nmo_shadow_get_included_files(const nmo_shadow_storage_t *storage,
                                                   size_t *out_size);

/**
 * @brief Retrieve a chunk's raw tail by object ID.
 *
 * @param storage   Shadow storage instance
 * @param chunk_id  Object ID (runtime ID) to look up
 * @param out_size  Output: size of returned data (may be NULL)
 * @return Pointer to tail data, or NULL if not found
 * @note Returned pointer is storage-owned; do not free.
 */
NMO_API const void *nmo_shadow_get_chunk_tail(const nmo_shadow_storage_t *storage,
                                               uint32_t chunk_id,
                                               size_t *out_size);

/**
 * @brief Check if any included files data has been captured.
 *
 * @param storage Shadow storage instance
 * @return true if included files blob exists, false otherwise
 */
NMO_API bool nmo_shadow_has_included_files(const nmo_shadow_storage_t *storage);

/**
 * @brief Get the number of captured chunk tails.
 *
 * @param storage Shadow storage instance
 * @return Number of chunk tails stored
 */
NMO_API size_t nmo_shadow_chunk_tail_count(const nmo_shadow_storage_t *storage);

/**
 * @brief Iterate over all captured chunk tails.
 *
 * Callback receives chunk_id, data pointer, and size for each tail.
 * Iteration continues while callback returns true.
 *
 * @param storage  Shadow storage instance
 * @param callback Function to call for each tail
 * @param user     User data passed to callback
 */
typedef bool (*nmo_shadow_tail_callback_t)(uint32_t chunk_id,
                                           const void *data,
                                           size_t size,
                                           void *user);

NMO_API void nmo_shadow_iterate_chunk_tails(const nmo_shadow_storage_t *storage,
                                             nmo_shadow_tail_callback_t callback,
                                             void *user);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SHADOW_STORAGE_H */
