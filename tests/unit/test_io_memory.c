/**
 * @file test_io_memory.c
 * @brief Unit tests for memory IO operations
 */

#include "../test_framework.h"
#include "io/nmo_io_memory.h"

TEST(io_memory, sparse_seek_write_zero_fills_gap) {
    nmo_io_interface_t *io = nmo_memory_io_open_write(0);
    ASSERT_NOT_NULL(io);

    uint8_t tail[2] = {0xABu, 0xCDu};
    int result = nmo_io_seek(io, 5, NMO_SEEK_SET);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_io_write(io, tail, sizeof(tail));
    ASSERT_EQ(result, NMO_OK);

    size_t out_size = 0;
    const uint8_t *out_data = (const uint8_t *) nmo_memory_io_get_data(io, &out_size);
    ASSERT_NOT_NULL(out_data);
    ASSERT_EQ(out_size, 7u);
    ASSERT_EQ(out_data[0], 0u);
    ASSERT_EQ(out_data[1], 0u);
    ASSERT_EQ(out_data[2], 0u);
    ASSERT_EQ(out_data[3], 0u);
    ASSERT_EQ(out_data[4], 0u);
    ASSERT_EQ(out_data[5], 0xABu);
    ASSERT_EQ(out_data[6], 0xCDu);

    nmo_io_close(io);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(io_memory, sparse_seek_write_zero_fills_gap);
TEST_MAIN_END()
