/**
 * @file test_io_checksum.c
 * @brief Tests for checksum IO wrapper
 */

#include "test_framework.h"
#include "io/nmo_io_checksum.h"
#include "io/nmo_io_memory.h"
#include <string.h>

/* Test: Create checksum IO with Adler-32 */
TEST(io_checksum, create_adler32_wrapper) {
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(mem_io);

    nmo_checksummed_io_desc_t desc = {
        .algorithm = NMO_CHECKSUM_ADLER32,
        .initial_value = 0
    };

    nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(checksum_io);

    nmo_io_close(checksum_io);
}

/* Test: Create checksum IO with CRC-32 */
TEST(io_checksum, create_crc32_wrapper) {
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(mem_io);

    nmo_checksummed_io_desc_t desc = {
        .algorithm = NMO_CHECKSUM_CRC32,
        .initial_value = 0
    };

    nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(checksum_io);

    nmo_io_close(checksum_io);
}

/* Test: Compute checksum on write */
TEST(io_checksum, checksum_on_write) {
    const char *data = "Hello, checksum!";
    const size_t data_size = strlen(data);

    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(mem_io);

    nmo_checksummed_io_desc_t desc = {
        .algorithm = NMO_CHECKSUM_ADLER32,
        .initial_value = 0
    };

    nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(checksum_io);

    /* Write data */
    int result = nmo_io_write(checksum_io, data, data_size);
    ASSERT_EQ(result, NMO_OK);

    /* Get checksum */
    uint32_t checksum = nmo_checksummed_io_get_checksum(checksum_io);
    ASSERT_GT(checksum, 0); /* Checksum should be non-zero for non-empty data */

    nmo_io_close(checksum_io);
}

/* Test: Compute checksum on read */
TEST(io_checksum, checksum_on_read) {
    const char *data = "Read checksum test";
    const size_t data_size = strlen(data);

    nmo_io_interface_t *mem_io = nmo_memory_io_open_read(data, data_size);
    ASSERT_NOT_NULL(mem_io);

    nmo_checksummed_io_desc_t desc = {
        .algorithm = NMO_CHECKSUM_ADLER32,
        .initial_value = 0
    };

    nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(checksum_io);

    /* Read data */
    char buffer[64] = {0};
    size_t bytes_read = 0;
    int result = nmo_io_read(checksum_io, buffer, data_size, &bytes_read);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(bytes_read, data_size);

    /* Get checksum */
    uint32_t checksum = nmo_checksummed_io_get_checksum(checksum_io);
    ASSERT_GT(checksum, 0); /* Checksum should be non-zero for non-empty data */

    nmo_io_close(checksum_io);
}

/* Test: Checksum accumulation across multiple writes */
TEST(io_checksum, checksum_accumulation) {
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(mem_io);

    nmo_checksummed_io_desc_t desc = {
        .algorithm = NMO_CHECKSUM_ADLER32,
        .initial_value = 0
    };

    nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(checksum_io);

    /* Write data in chunks */
    const char *chunk1 = "Hello, ";
    const char *chunk2 = "World!";
    
    nmo_io_write(checksum_io, chunk1, strlen(chunk1));
    uint32_t checksum1 = nmo_checksummed_io_get_checksum(checksum_io);
    ASSERT_GT(checksum1, 0);

    nmo_io_write(checksum_io, chunk2, strlen(chunk2));
    uint32_t checksum2 = nmo_checksummed_io_get_checksum(checksum_io);
    ASSERT_GT(checksum2, checksum1); /* Checksum should have changed */

    nmo_io_close(checksum_io);
}

/* Test: Adler-32 vs CRC-32 produce different values */
TEST(io_checksum, different_algorithms) {
    const char *data = "Test data for algorithm comparison";
    const size_t data_size = strlen(data);

    /* Test Adler-32 */
    uint32_t adler_checksum = 0;
    {
        nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
        nmo_checksummed_io_desc_t desc = {
            .algorithm = NMO_CHECKSUM_ADLER32,
            .initial_value = 0
        };
        nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
        
        nmo_io_write(checksum_io, data, data_size);
        adler_checksum = nmo_checksummed_io_get_checksum(checksum_io);
        
        nmo_io_close(checksum_io);
    }

    /* Test CRC-32 */
    uint32_t crc_checksum = 0;
    {
        nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
        nmo_checksummed_io_desc_t desc = {
            .algorithm = NMO_CHECKSUM_CRC32,
            .initial_value = 0
        };
        nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
        
        nmo_io_write(checksum_io, data, data_size);
        crc_checksum = nmo_checksummed_io_get_checksum(checksum_io);
        
        nmo_io_close(checksum_io);
    }

    /* The two algorithms should produce different checksums */
    ASSERT_NE(adler_checksum, crc_checksum);
}

/* Test: Same data produces same checksum */
TEST(io_checksum, deterministic) {
    const char *data = "Deterministic test data";
    const size_t data_size = strlen(data);

    /* Compute checksum first time */
    uint32_t checksum1 = 0;
    {
        nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
        nmo_checksummed_io_desc_t desc = {
            .algorithm = NMO_CHECKSUM_ADLER32,
            .initial_value = 0
        };
        nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
        
        nmo_io_write(checksum_io, data, data_size);
        checksum1 = nmo_checksummed_io_get_checksum(checksum_io);
        
        nmo_io_close(checksum_io);
    }

    /* Compute checksum second time */
    uint32_t checksum2 = 0;
    {
        nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
        nmo_checksummed_io_desc_t desc = {
            .algorithm = NMO_CHECKSUM_ADLER32,
            .initial_value = 0
        };
        nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
        
        nmo_io_write(checksum_io, data, data_size);
        checksum2 = nmo_checksummed_io_get_checksum(checksum_io);
        
        nmo_io_close(checksum_io);
    }

    /* Should get same checksum */
    ASSERT_EQ(checksum1, checksum2);
}

/* Test: Empty data checksum */
TEST(io_checksum, empty_data) {
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(mem_io);

    nmo_checksummed_io_desc_t desc = {
        .algorithm = NMO_CHECKSUM_ADLER32,
        .initial_value = 0
    };

    nmo_io_interface_t *checksum_io = nmo_checksummed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(checksum_io);

    /* Close without writing anything */
    uint32_t checksum = nmo_checksummed_io_get_checksum(checksum_io);
    /* Adler-32 of empty data is 1 */
    ASSERT_EQ(checksum, 1);

    nmo_io_close(checksum_io);
}

/* Test: Invalid parameters */
TEST(io_checksum, invalid_parameters) {
    /* NULL inner IO */
    nmo_checksummed_io_desc_t desc = {
        .algorithm = NMO_CHECKSUM_ADLER32,
        .initial_value = 0
    };
    
    nmo_io_interface_t *result = nmo_checksummed_io_wrap(NULL, &desc);
    ASSERT_NULL(result);

    /* NULL descriptor */
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    
    result = nmo_checksummed_io_wrap(mem_io, NULL);
    ASSERT_NULL(result);

    nmo_io_close(mem_io);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(io_checksum, create_adler32_wrapper);
    REGISTER_TEST(io_checksum, create_crc32_wrapper);
    REGISTER_TEST(io_checksum, checksum_on_write);
    REGISTER_TEST(io_checksum, checksum_on_read);
    REGISTER_TEST(io_checksum, checksum_accumulation);
    REGISTER_TEST(io_checksum, different_algorithms);
    REGISTER_TEST(io_checksum, deterministic);
    REGISTER_TEST(io_checksum, empty_data);
    REGISTER_TEST(io_checksum, invalid_parameters);
TEST_MAIN_END()
