/**
 * @file nmo_txn.h
 * @brief Transactional IO operations
 */

#ifndef NMO_TXN_H
#define NMO_TXN_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transaction context
 */
typedef struct nmo_txn nmo_txn_t;

/**
 * Transaction state
 */
typedef enum {
    NMO_TXN_IDLE = 0,
    NMO_TXN_ACTIVE,
    NMO_TXN_COMMITTED,
    NMO_TXN_ROLLED_BACK,
} nmo_txn_state_t;

/**
 * Create a transaction
 * @param io_context Base IO context
 * @return Transaction context or NULL on error
 */
NMO_API nmo_txn_t* nmo_txn_create(void* io_context);

/**
 * Destroy transaction
 * @param txn Transaction context
 */
NMO_API void nmo_txn_destroy(nmo_txn_t* txn);

/**
 * Begin transaction
 * @param txn Transaction context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_txn_begin(nmo_txn_t* txn);

/**
 * Commit transaction
 * @param txn Transaction context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_txn_commit(nmo_txn_t* txn);

/**
 * Rollback transaction
 * @param txn Transaction context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_txn_rollback(nmo_txn_t* txn);

/**
 * Get transaction state
 * @param txn Transaction context
 * @return Current transaction state
 */
NMO_API nmo_txn_state_t nmo_txn_get_state(const nmo_txn_t* txn);

/**
 * Read from transaction
 * @param txn Transaction context
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
NMO_API size_t nmo_txn_read(nmo_txn_t* txn, void* buffer, size_t size);

/**
 * Write in transaction
 * @param txn Transaction context
 * @param buffer Data to write
 * @param size Number of bytes to write
 * @return Number of bytes written
 */
NMO_API size_t nmo_txn_write(nmo_txn_t* txn, const void* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TXN_H */
