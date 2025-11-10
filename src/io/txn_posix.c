/**
 * @file txn_posix.c
 * @brief Transactional IO operations implementation (POSIX)
 */

#include "io/nmo_txn.h"

/**
 * Create a transaction
 */
nmo_txn_t* nmo_txn_create(void* io_context) {
    (void)io_context;
    return NULL;
}

/**
 * Destroy transaction
 */
void nmo_txn_destroy(nmo_txn_t* txn) {
    (void)txn;
}

/**
 * Begin transaction
 */
nmo_result_t nmo_txn_begin(nmo_txn_t* txn) {
    (void)txn;
    return nmo_result_ok();
}

/**
 * Commit transaction
 */
nmo_result_t nmo_txn_commit(nmo_txn_t* txn) {
    (void)txn;
    return nmo_result_ok();
}

/**
 * Rollback transaction
 */
nmo_result_t nmo_txn_rollback(nmo_txn_t* txn) {
    (void)txn;
    return nmo_result_ok();
}

/**
 * Get transaction state
 */
nmo_txn_state_t nmo_txn_get_state(const nmo_txn_t* txn) {
    (void)txn;
    return NMO_TXN_IDLE;
}

/**
 * Read from transaction
 */
size_t nmo_txn_read(nmo_txn_t* txn, void* buffer, size_t size) {
    (void)txn;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Write in transaction
 */
size_t nmo_txn_write(nmo_txn_t* txn, const void* buffer, size_t size) {
    (void)txn;
    (void)buffer;
    (void)size;
    return 0;
}
