/**
 * @file test_header.c
 * @brief Unit tests for NMO file header
 */

#include "../test_framework.h"
#include "format/nmo_header.h"
#include "io/nmo_io_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nmo_status_t header_read_failure(
    void *handle,
    void *buffer,
    size_t size,
    size_t *bytes_read) {
    (void)handle;
    (void)buffer;
    (void)size;
    if (bytes_read != NULL) {
        *bytes_read = 0;
    }
    return NMO_ERR_CANT_READ_FILE;
}

static nmo_status_t header_write_failure(
    void *handle,
    const void *buffer,
    size_t size) {
    (void)handle;
    (void)buffer;
    (void)size;
    return NMO_ERR_CANCELLED;
}

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

    nmo_status_t result = nmo_header_write(header, write_io);
    ASSERT_EQ(result, NMO_OK);

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
    ASSERT_EQ(result, NMO_OK);

    /* Clean up */
    nmo_io_close(read_io);
    nmo_io_close(write_io);  /* Close write_io AFTER we're done using the data */
    nmo_header_destroy(read_header);
    nmo_header_destroy(header);
}

TEST(header, validate) {
    nmo_header_t *header = nmo_header_create();
    ASSERT_NOT_NULL(header);

    nmo_status_t result = nmo_header_validate(header);
    ASSERT_EQ(result, NMO_OK);

    nmo_header_destroy(header);
}

TEST(header, compute_crc_matches_ck2_reference_fixture) {
    FILE *file = fopen(NMO_TEST_DATA_FILE("Nop.cmo"), "rb");
    ASSERT_NOT_NULL(file);

    ASSERT_EQ(0, fseek(file, 0, SEEK_END));
    long file_size = ftell(file);
    ASSERT_GT(file_size, 64);
    ASSERT_EQ(0, fseek(file, 0, SEEK_SET));

    uint8_t *bytes = (uint8_t *)malloc((size_t)file_size);
    ASSERT_NOT_NULL(bytes);
    ASSERT_EQ((int)file_size, (int)fread(bytes, 1, (size_t)file_size, file));
    fclose(file);

    nmo_file_header_t header;
    nmo_io_interface_t *io = nmo_memory_io_open_read(bytes, (size_t)file_size);
    ASSERT_NOT_NULL(io);
    ASSERT_EQ(NMO_OK, nmo_file_header_parse(io, &header));
    nmo_io_close(io);

    ASSERT_EQ(0xCDB6A00Au, header.crc);
    ASSERT((uint32_t)file_size >= 64u + header.hdr1_pack_size + header.data_pack_size);

    uint32_t computed = nmo_file_header_compute_crc(
        &header,
        bytes + 64,
        header.hdr1_pack_size,
        bytes + 64 + header.hdr1_pack_size,
        header.data_pack_size);

    ASSERT_EQ(header.crc, computed);
    free(bytes);
}

TEST(header, parse_failure_preserves_output) {
    static const uint8_t truncated[] = {
        'N', 'e', 'm', 'o', ' ', 'F', 'i', '\0',
        0x12, 0x34,
    };
    nmo_io_interface_t *io =
        nmo_memory_io_open_read(truncated, sizeof(truncated));
    ASSERT_NOT_NULL(io);

    nmo_file_header_t header;
    nmo_file_header_t expected;
    memset(&header, 0xA5, sizeof(header));
    expected = header;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_file_header_parse(io, &header));
    ASSERT_MEM_EQ(&expected, &header, sizeof(header));

    nmo_io_close(io);
}

TEST(header, propagates_backend_errors) {
    nmo_io_interface_t read_io = {
        .read = header_read_failure,
    };
    nmo_file_header_t parsed;
    memset(&parsed, 0xA5, sizeof(parsed));
    nmo_file_header_t expected = parsed;
    ASSERT_EQ(NMO_ERR_CANT_READ_FILE,
              nmo_file_header_parse(&read_io, &parsed));
    ASSERT_MEM_EQ(&expected, &parsed, sizeof(parsed));

    nmo_file_header_t header = {0};
    memcpy(header.signature, "Nemo Fi\0", 8);
    header.file_version = 8;
    nmo_io_interface_t write_io = {
        .write = header_write_failure,
    };
    ASSERT_EQ(NMO_ERR_CANCELLED,
              nmo_file_header_serialize(&header, &write_io));
}

TEST(header, rejects_invalid_header_before_writing) {
    nmo_file_header_t header = {0};
    header.file_version = 8;

    nmo_io_interface_t *io = nmo_memory_io_open_write(64);
    ASSERT_NOT_NULL(io);
    ASSERT_EQ(NMO_ERR_INVALID_SIGNATURE,
              nmo_file_header_serialize(&header, io));

    size_t written_size = SIZE_MAX;
    (void)nmo_memory_io_get_data(io, &written_size);
    ASSERT_EQ(0u, written_size);

    memcpy(header.signature, "Nemo Fi\0", 8);
    header.file_version2 = 1;
    ASSERT_EQ(NMO_ERR_UNSUPPORTED_VERSION,
              nmo_file_header_serialize(&header, io));
    (void)nmo_memory_io_get_data(io, &written_size);
    ASSERT_EQ(0u, written_size);
    nmo_io_close(io);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(header, create_and_destroy);
    REGISTER_TEST(header, get_size);
    REGISTER_TEST(header, write_and_read);
    REGISTER_TEST(header, validate);
    REGISTER_TEST(header, compute_crc_matches_ck2_reference_fixture);
    REGISTER_TEST(header, parse_failure_preserves_output);
    REGISTER_TEST(header, propagates_backend_errors);
    REGISTER_TEST(header, rejects_invalid_header_before_writing);
TEST_MAIN_END()
