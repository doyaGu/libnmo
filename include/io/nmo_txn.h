/**
 * @file nmo_txn.h
 * @brief Transactional IO operations for atomic file writes
 *
 * Provides ACID-like guarantees for file writes:
 * - Atomicity: File appears all-at-once or not at all
 * - Durability: Configurable sync guarantees
 * - Isolation: Writes to temporary file until commit
 *
 * Usage:
 * @code
 * nmo_txn_desc desc = {
 *     .path = "/path/to/file.nmo",
 *     .durability = NMO_TXN_FSYNC,
 *     .staging_dir = NULL  // Use system temp dir
 * };
 *
 * nmo_txn_handle* txn = nmo_txn_open(&desc);
 * if (!txn) return ERROR;
 *
 * nmo_result_t result = nmo_txn_write(txn, data, size);
 * if (result.code != NMO_OK) {
 *     nmo_txn_rollback(txn);
 *     nmo_txn_close(txn);
 *     return result;
 * }
 *
 * result = nmo_txn_commit(txn);  // Atomic rename
 * nmo_txn_close(txn);
 * @endcode
 */

#ifndef NMO_TXN_H
#define NMO_TXN_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque transaction handle
 *
 * Represents an in-progress atomic file write operation.
 * Must be closed with nmo_txn_close() to free resources.
 */
typedef struct nmo_txn_handle nmo_txn_handle_t;

/**
 * @brief Durability mode for transaction commits
 *
 * Controls how aggressively data is synced to disk on commit.
 */
typedef enum nmo_txn_durability {
    NMO_TXN_NONE = 0,  /**< No explicit sync (fastest, least durable) */
    NMO_TXN_FDATASYNC, /**< Sync data only (skip metadata when possible) */
    NMO_TXN_FSYNC,     /**< Full sync of data and metadata (safest) */
} nmo_txn_durability_t;

/**
 * @brief Transaction descriptor
 *
 * Configuration for opening a new transactional write.
 */
typedef struct nmo_txn_desc {
    const char *path;                /**< Final file path (required) */
    nmo_txn_durability_t durability; /**< Durability mode (default: NMO_TXN_NONE) */
    const char *staging_dir;         /**< Staging directory (NULL = use temp dir) */
} nmo_txn_desc_t;

/**
 * @brief Open a new transaction for atomic file write
 *
 * Creates a temporary file in the staging directory (or system temp dir if NULL).
 * The temporary file uses naming: .{basename}.{pid}.{random}.tmp
 *
 * @param desc Transaction descriptor (must not be NULL)
 * @return Transaction handle or NULL on error
 *
 * @note The returned handle must be freed with nmo_txn_close()
 * @note If the final path exists, it will be replaced atomically on commit
 */
NMO_API nmo_txn_handle_t *nmo_txn_open(const nmo_txn_desc_t *desc);

/**
 * @brief Write data to transaction
 *
 * Writes data to the temporary file. Data is buffered for performance
 * and flushed on commit.
 *
 * @param txn Transaction handle (must not be NULL)
 * @param data Data to write (must not be NULL)
 * @param size Number of bytes to write
 * @return NMO_OK on success, error code otherwise
 *
 * @note Multiple writes are supported and append to the temp file
 * @note Writes after commit/rollback will return NMO_ERR_INVALID_STATE
 */
NMO_API nmo_result_t nmo_txn_write(nmo_txn_handle_t *txn, const void *data, size_t size);

/**
 * @brief Commit transaction atomically
 *
 * Flushes all buffered data, syncs to disk (if requested), and
 * atomically renames the temporary file to the final path.
 *
 * @param txn Transaction handle (must not be NULL)
 * @return NMO_OK on success, error code otherwise
 *
 * @note After commit, the transaction is in committed state
 * @note Subsequent writes will fail with NMO_ERR_INVALID_STATE
 * @note The temporary file is removed after successful commit
 * @note On failure, temporary file is preserved for debugging
 */
NMO_API nmo_result_t nmo_txn_commit(nmo_txn_handle_t *txn);

/**
 * @brief Rollback transaction and discard changes
 *
 * Closes the temporary file and deletes it.
 *
 * @param txn Transaction handle (must not be NULL)
 * @return NMO_OK on success, error code otherwise
 *
 * @note After rollback, the transaction is in rolled-back state
 * @note Subsequent writes will fail with NMO_ERR_INVALID_STATE
 * @note The temporary file is always removed
 */
NMO_API nmo_result_t nmo_txn_rollback(nmo_txn_handle_t *txn);

/**
 * @brief Close transaction and free resources
 *
 * Closes the temporary file (if still open) and frees the transaction handle.
 * If neither commit nor rollback was called, performs an implicit rollback.
 *
 * @param txn Transaction handle (NULL is safe)
 *
 * @note Always call this to free transaction resources
 * @note After close, the handle is invalid and must not be used
 */
NMO_API void nmo_txn_close(nmo_txn_handle_t *txn);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TXN_H */
