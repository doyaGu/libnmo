/**
 * @file test_io_compressed.c
 * @brief Tests for compressed IO wrapper
 */

#include "test_framework.h"
#include "io/nmo_io_compressed.h"
#include "io/nmo_io_memory.h"
#include <string.h>

/* Test: Create compressed IO for deflate (write) */
TEST(io_compressed, create_deflate_wrapper) {
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(mem_io);

    nmo_compressed_io_desc_t desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_DEFLATE,
        .level = 6
    };

    nmo_io_interface_t *compressed_io = nmo_compressed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(compressed_io);

    nmo_io_close(compressed_io);
}

/* Test: Create compressed IO for inflate (read) */
TEST(io_compressed, create_inflate_wrapper) {
    const size_t buffer_size = 1024;
    uint8_t buffer[1024] = {0};
    nmo_io_interface_t *mem_io = nmo_memory_io_open_read(buffer, buffer_size);
    ASSERT_NOT_NULL(mem_io);

    nmo_compressed_io_desc_t desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_INFLATE,
        .level = 0 /* ignored for inflate */
    };

    nmo_io_interface_t *compressed_io = nmo_compressed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(compressed_io);

    nmo_io_close(compressed_io);
}

/* Test: Compress and decompress small data */
TEST(io_compressed, compress_and_decompress_small) {
    const char *original = "Hello, compressed world!";
    const size_t original_size = strlen(original);
    
    /* Step 1: Compress data to memory buffer */
    nmo_io_interface_t *compress_io_mem = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(compress_io_mem);

    nmo_compressed_io_desc_t compress_desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_DEFLATE,
        .level = 6
    };

    nmo_io_interface_t *compress_io = nmo_compressed_io_wrap(compress_io_mem, &compress_desc);
    ASSERT_NOT_NULL(compress_io);

    int result = nmo_io_write(compress_io, original, original_size);
    ASSERT_EQ(result, NMO_OK);
    
    /* Flush compression stream without closing inner memory IO */
    result = nmo_io_flush(compress_io);
    ASSERT_EQ(result, NMO_OK);
    
    /* Now we can safely get compressed data from memory IO */
    size_t compressed_size = 0;
    const void *compressed_data = nmo_memory_io_get_data(compress_io_mem, &compressed_size);
    ASSERT_NOT_NULL(compressed_data);
    ASSERT_GT(compressed_size, 0);
    
    /* Copy to buffer for decompression */
    uint8_t compressed_buffer[1024];
    ASSERT_LE(compressed_size, sizeof(compressed_buffer));
    memcpy(compressed_buffer, compressed_data, compressed_size);
    
    nmo_io_close(compress_io); /* Close wrapper and inner IO */

    /* Step 2: Decompress data from compressed buffer */
    nmo_io_interface_t *decompress_io_mem = nmo_memory_io_open_read(compressed_buffer, compressed_size);
    ASSERT_NOT_NULL(decompress_io_mem);

    nmo_compressed_io_desc_t decompress_desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_INFLATE,
        .level = 0
    };

    nmo_io_interface_t *decompress_io = nmo_compressed_io_wrap(decompress_io_mem, &decompress_desc);
    ASSERT_NOT_NULL(decompress_io);

    char decompressed[256] = {0};
    size_t bytes_read = 0;
    result = nmo_io_read(decompress_io, decompressed, original_size, &bytes_read);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(bytes_read, original_size);
    ASSERT_STR_EQ(decompressed, original);

    nmo_io_close(decompress_io);
}

/* Test: Compress large data (multiple writes) */
TEST(io_compressed, compress_large_data) {
    const size_t chunk_size = 256;
    const size_t num_chunks = 10;
    
    /* Create test data - repeating pattern */
    char chunk[256];
    memset(chunk, 'A', chunk_size);

    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(16384);
    ASSERT_NOT_NULL(mem_io);

    nmo_compressed_io_desc_t desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_DEFLATE,
        .level = 9 /* Best compression for repetitive data */
    };

    nmo_io_interface_t *compressed_io = nmo_compressed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(compressed_io);

    /* Write multiple chunks */
    for (size_t i = 0; i < num_chunks; i++) {
        int result = nmo_io_write(compressed_io, chunk, chunk_size);
        ASSERT_EQ(result, NMO_OK);
    }

    ASSERT_EQ(NMO_OK, nmo_io_flush(compressed_io));

    /* Verify compression ratio before closing inner IO */
    size_t compressed_size = 0;
    const void *compressed_data = nmo_memory_io_get_data(mem_io, &compressed_size);
    ASSERT_NOT_NULL(compressed_data);
    
    size_t original_size = chunk_size * num_chunks;
    ASSERT_GT(compressed_size, 0);
    ASSERT_LT(compressed_size, original_size); /* Should be compressed */
    
    /* With best compression and repetitive data, expect high ratio */
    ASSERT_LT(compressed_size, original_size / 10); /* At least 10x compression */

    nmo_io_close(compressed_io);
}

/* Test: Different compression levels */
TEST(io_compressed, compression_levels) {
    const char *data = "This is test data for compression level testing. ";
    const size_t data_size = strlen(data);

    size_t size_level1 = 0;
    size_t size_level9 = 0;

    /* Test level 1 (fastest) */
    {
        nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
        
        nmo_compressed_io_desc_t desc = {
            .codec = NMO_CODEC_ZLIB,
            .mode = NMO_COMPRESS_MODE_DEFLATE,
            .level = 1
        };

        nmo_io_interface_t *compressed_io = nmo_compressed_io_wrap(mem_io, &desc);
        ASSERT_NOT_NULL(compressed_io);

        int result = nmo_io_write(compressed_io, data, data_size);
        ASSERT_EQ(result, NMO_OK);
        ASSERT_EQ(NMO_OK, nmo_io_flush(compressed_io));

        const void *compressed_data = nmo_memory_io_get_data(mem_io, &size_level1);
        ASSERT_NOT_NULL(compressed_data);
        ASSERT_GT(size_level1, 0);

        nmo_io_close(compressed_io);
    }

    /* Test level 9 (best compression) */
    {
        nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
        
        nmo_compressed_io_desc_t desc = {
            .codec = NMO_CODEC_ZLIB,
            .mode = NMO_COMPRESS_MODE_DEFLATE,
            .level = 9
        };

        nmo_io_interface_t *compressed_io = nmo_compressed_io_wrap(mem_io, &desc);
        ASSERT_NOT_NULL(compressed_io);

        int result = nmo_io_write(compressed_io, data, data_size);
        ASSERT_EQ(result, NMO_OK);
        ASSERT_EQ(NMO_OK, nmo_io_flush(compressed_io));

        const void *compressed_data = nmo_memory_io_get_data(mem_io, &size_level9);
        ASSERT_NOT_NULL(compressed_data);
        ASSERT_GT(size_level9, 0);

        nmo_io_close(compressed_io);
    }

    /* Level 9 should produce smaller or equal size compared to level 1 */
    ASSERT_LE(size_level9, size_level1);
}

/* Test: Empty data compression */
TEST(io_compressed, compress_empty_data) {
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    ASSERT_NOT_NULL(mem_io);

    nmo_compressed_io_desc_t desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_DEFLATE,
        .level = 6
    };

    nmo_io_interface_t *compressed_io = nmo_compressed_io_wrap(mem_io, &desc);
    ASSERT_NOT_NULL(compressed_io);

    /* Close without writing anything */
    ASSERT_EQ(NMO_OK, nmo_io_flush(compressed_io));

    /* Should still have header/footer data */
    size_t compressed_size = 0;
    const void *compressed_data = nmo_memory_io_get_data(mem_io, &compressed_size);
    ASSERT_NOT_NULL(compressed_data);
    ASSERT_GT(compressed_size, 0); /* zlib header is always present */

    nmo_io_close(compressed_io);
}

/* Test: Invalid parameters */
TEST(io_compressed, invalid_parameters) {
    /* NULL inner IO */
    nmo_compressed_io_desc_t desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_DEFLATE,
        .level = 6
    };
    
    nmo_io_interface_t *result = nmo_compressed_io_wrap(NULL, &desc);
    ASSERT_NULL(result);

    /* NULL descriptor */
    nmo_io_interface_t *mem_io = nmo_memory_io_open_write(1024);
    
    result = nmo_compressed_io_wrap(mem_io, NULL);
    ASSERT_NULL(result);

    nmo_io_close(mem_io);
}

/* Test: Read compressed data in one go (incremental read doesn't make sense for small compressed data) */
TEST(io_compressed, read_after_compression) {
    const char *original = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const size_t original_size = strlen(original);
    
    /* Compress */
    nmo_io_interface_t *compress_io_mem = nmo_memory_io_open_write(1024);
    
    nmo_compressed_io_desc_t compress_desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_DEFLATE,
        .level = 6
    };

    nmo_io_interface_t *compress_io = nmo_compressed_io_wrap(compress_io_mem, &compress_desc);
    nmo_io_write(compress_io, original, original_size);
    
    /* Flush to finalize compression without closing inner IO */
    nmo_io_flush(compress_io);

    /* Get compressed data while inner IO still valid */
    size_t compressed_size = 0;
    const void *compressed_data = nmo_memory_io_get_data(compress_io_mem, &compressed_size);
    
    /* Copy to buffer before closing */
    uint8_t compressed_buffer[1024];
    memcpy(compressed_buffer, compressed_data, compressed_size);
    
    nmo_io_close(compress_io); /* Now close everything */
    
    /* Decompress - read all at once */
    nmo_io_interface_t *decompress_io_mem = nmo_memory_io_open_read(compressed_buffer, compressed_size);
    
    nmo_compressed_io_desc_t decompress_desc = {
        .codec = NMO_CODEC_ZLIB,
        .mode = NMO_COMPRESS_MODE_INFLATE,
        .level = 0
    };

    nmo_io_interface_t *decompress_io = nmo_compressed_io_wrap(decompress_io_mem, &decompress_desc);

    /* Read all data in one go */
    char result[64] = {0};
    size_t bytes_read = 0;
    int read_result = nmo_io_read(decompress_io, result, original_size, &bytes_read);
    
    ASSERT_EQ(read_result, NMO_OK);
    ASSERT_EQ(bytes_read, original_size);
    ASSERT_STR_EQ(result, original);

    nmo_io_close(decompress_io);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(io_compressed, create_deflate_wrapper);
    REGISTER_TEST(io_compressed, create_inflate_wrapper);
    REGISTER_TEST(io_compressed, compress_and_decompress_small);
    REGISTER_TEST(io_compressed, compress_large_data);
    REGISTER_TEST(io_compressed, compression_levels);
    REGISTER_TEST(io_compressed, compress_empty_data);
    REGISTER_TEST(io_compressed, invalid_parameters);
    REGISTER_TEST(io_compressed, read_after_compression);
TEST_MAIN_END()
