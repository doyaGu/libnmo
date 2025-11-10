/**
 * @file test_txn.c
 * @brief Unit tests for transactional IO
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(txn, begin_commit) {
    uint8_t buffer[256] = {0};
    nmo_io_writer *writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(writer);

    nmo_txn *txn = nmo_txn_begin(writer);
    ASSERT_NOT_NULL(txn);

    nmo_txn_commit(txn);
    nmo_io_writer_release(writer);
}

TEST(txn, begin_rollback) {
    uint8_t buffer[256] = {0};
    nmo_io_writer *writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(writer);

    nmo_txn *txn = nmo_txn_begin(writer);
    ASSERT_NOT_NULL(txn);

    nmo_txn_rollback(txn);
    nmo_io_writer_release(writer);
}

int main(void) {
    return 0;
}
