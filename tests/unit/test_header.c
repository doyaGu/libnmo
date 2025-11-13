/**
 * @file test_header.c
 * @brief Unit tests for NMO file header
 */

#include "../test_framework.h"
#include "format/nmo_header.h"
#include "io/nmo_io_memory.h"
#include <string.h>

TEST(header, create_and_destroy) {
    nmo_header_t *header = nmo_header_create();
    ASSERT_NOT_NULL(header);
    nmo_header_destroy(header);
}

TEST(header, get_size) {
    nmo_header_t *header = nmo_header_create();
    ASSERT_NOT_NULL(header);

    uint32_t size = nmo_header_get_size(header);
    ASSERT_GT(size, 0);

    nmo_header_destroy(header);
}

TEST(header, write_and_read) {
    nmo_header_t *header = nmo_header_create();
    ASSERT_NOT_NULL(header);

    /* Write header to memory */
    nmo_io_interface_t *write_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(write_io);

    nmo_result_t result = nmo_header_write(header, write_io);
    ASSERT_EQ(result.code, NMO_OK);

    /* Get written data - DON'T close write_io yet! */
    size_t written_size = 0;
    const void *data = nmo_memory_io_get_data(write_io, &written_size);
    ASSERT_NOT_NULL(data);
    ASSERT_GT(written_size, 0);

    /* Read header back */
    nmo_header_t *read_header = nmo_header_create();
    ASSERT_NOT_NULL(read_header);

    nmo_io_interface_t *read_io = nmo_memory_io_open_read(data, written_size);
    ASSERT_NOT_NULL(read_io);

    result = nmo_header_parse(read_header, read_io);
    ASSERT_EQ(result.code, NMO_OK);

    /* Clean up */
    nmo_io_close(read_io);
    nmo_io_close(write_io);  /* Close write_io AFTER we're done using the data */
    nmo_header_destroy(read_header);
    nmo_header_destroy(header);
}

TEST(header, validate) {
    nmo_header_t *header = nmo_header_create();
    ASSERT_NOT_NULL(header);

    nmo_result_t result = nmo_header_validate(header);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_header_destroy(header);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(header, create_and_destroy);
    REGISTER_TEST(header, get_size);
    REGISTER_TEST(header, write_and_read);
    REGISTER_TEST(header, validate);
TEST_MAIN_END()
