/**
 * @file nmo_plane_codec.c
 * @brief Decoder for the 8-bit plane codec of CKStateChunk raw bitmaps
 *
 * A raw bitmap whose compression field is 1 stores each colour plane in
 * CCompressionTools' own DCT codec (CCompressionTools::jpegDecode in CK2.dll),
 * not in JPEG. The stream is a quality byte, the width and height as 32-bit
 * integers, then for every 8x8 block the 64 quantised coefficients in zigzag
 * order, read bit by bit from the least significant bit of each byte on.
 */

#include "format/nmo_image.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

/* Largest plane accepted; the engine has no limit but a plane is a texture. */
#define NMO_PLANE_MAX_DIMENSION 65536
#define NMO_PLANE_MAX_PIXELS ((size_t)1 << 28)

static const int nmo_plane_quantum[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99,
};

/* Row and column of the n-th coefficient of a block. */
static const uint8_t nmo_plane_zigzag[64][2] = {
    {0, 0}, {0, 1}, {1, 0}, {2, 0}, {1, 1}, {0, 2}, {0, 3}, {1, 2},
    {2, 1}, {3, 0}, {4, 0}, {3, 1}, {2, 2}, {1, 3}, {0, 4}, {0, 5},
    {1, 4}, {2, 3}, {3, 2}, {4, 1}, {5, 0}, {6, 0}, {5, 1}, {4, 2},
    {3, 3}, {2, 4}, {1, 5}, {0, 6}, {0, 7}, {1, 6}, {2, 5}, {3, 4},
    {4, 3}, {5, 2}, {6, 1}, {7, 0}, {7, 1}, {6, 2}, {5, 3}, {4, 4},
    {3, 5}, {2, 6}, {1, 7}, {2, 7}, {3, 6}, {4, 5}, {5, 4}, {6, 3},
    {7, 2}, {7, 3}, {6, 4}, {5, 5}, {4, 6}, {3, 7}, {4, 7}, {5, 6},
    {6, 5}, {7, 4}, {7, 5}, {6, 6}, {5, 7}, {6, 7}, {7, 6}, {7, 7},
};

typedef struct nmo_plane_bits {
    const uint8_t *data;
    size_t bit_count;
    size_t position;
    bool overrun;
} nmo_plane_bits_t;

/* Reads count bits, the first one read becoming the most significant. */
static uint32_t nmo_plane_read_bits(nmo_plane_bits_t *bits, unsigned count)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (bits->position >= bits->bit_count) {
            bits->overrun = true;
            return 0;
        }
        uint32_t bit = (bits->data[bits->position >> 3] >> (bits->position & 7u)) & 1u;
        value |= bit << (count - 1u - i);
        bits->position++;
    }
    return value;
}

/* CCompressionTools::jpegInputCode; *run is the count of zeros still owed. */
static int nmo_plane_input_code(nmo_plane_bits_t *bits, int *run)
{
    if (*run > 0) {
        (*run)--;
        return 0;
    }
    uint32_t kind = nmo_plane_read_bits(bits, 2);
    if (kind == 0) {
        *run = (int)nmo_plane_read_bits(bits, 4);
        return 0;
    }
    unsigned length = kind == 1
        ? nmo_plane_read_bits(bits, 1) + 1u
        : nmo_plane_read_bits(bits, 2) + 4u * kind - 5u;
    int value = (int)nmo_plane_read_bits(bits, length);
    if (((1 << (length - 1u)) & value) == 0) {
        return value - (1 << length) + 1;
    }
    return value;
}

nmo_status_t nmo_image_decode_dct_plane(
    const uint8_t *data,
    size_t size,
    nmo_arena_t *arena,
    int *out_width,
    int *out_height,
    uint8_t **out_plane)
{
    if (data == NULL || arena == NULL || out_width == NULL ||
        out_height == NULL || out_plane == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (size < 9u) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    const uint8_t quality = data[0];
    int32_t width = 0;
    int32_t height = 0;
    memcpy(&width, data + 1, sizeof(width));
    memcpy(&height, data + 5, sizeof(height));
    if (width <= 0 || height <= 0 || width > NMO_PLANE_MAX_DIMENSION ||
        height > NMO_PLANE_MAX_DIMENSION) {
        return NMO_ERR_INVALID_FORMAT;
    }

    const size_t block_rows = ((size_t)height + 7u) / 8u;
    /* The engine lets the blocks of the last row and column run past the plane
       in a buffer of width * height bytes; keep the same layout in a buffer
       with room for them and return the first width * height bytes. */
    const size_t scratch_size = block_rows * 8u * (size_t)width + 8u;
    if (scratch_size > NMO_PLANE_MAX_PIXELS) {
        return NMO_ERR_INVALID_FORMAT;
    }
    uint8_t *plane = (uint8_t *)nmo_arena_alloc(arena, scratch_size, 1);
    if (plane == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(plane, 0, scratch_size);

    /* jpegInitialize: the quantisation table scaled by the quality, and the
       cosine basis. */
    int quantum[64];
    const double scale = (double)(50 - (int)quality) * 0.02;
    for (int i = 0; i < 64; ++i) {
        const int base = nmo_plane_quantum[i];
        quantum[i] = base + (int)(int64_t)((double)base * scale);
        if (quantum[i] == 0) quantum[i] = 1;
    }
    const double pi = atan(1.0) * 4.0;
    double basis[8][8];
    for (int j = 0; j < 8; ++j) {
        basis[0][j] = 1.0 / sqrt(8.0);
    }
    for (int i = 1; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            basis[i][j] = cos((double)(2 * j + 1) * (double)i * pi * 0.0625) * sqrt(0.25);
        }
    }

    nmo_plane_bits_t bits = {
        .data = data,
        .bit_count = size * 8u,
        .position = 72u,
    };
    int run = 0;

    for (size_t block_row = 0; block_row < block_rows; ++block_row) {
        for (int x = 0; x < width; x += 8) {
            int coefficients[64];
            for (int n = 0; n < 64; ++n) {
                const int row = nmo_plane_zigzag[n][0];
                const int col = nmo_plane_zigzag[n][1];
                coefficients[row * 8 + col] =
                    quantum[row * 8 + col] * nmo_plane_input_code(&bits, &run);
            }
            if (bits.overrun) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }

            /* jpegInverseDCT */
            double rows[8][8];
            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 8; ++j) {
                    double sum = 0.0;
                    for (int m = 0; m < 8; ++m) {
                        sum += (double)coefficients[i * 8 + m] * basis[m][j];
                    }
                    rows[i][j] = sum;
                }
            }
            uint8_t *target = plane + block_row * 8u * (size_t)width + (size_t)x;
            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    double sum = 0.0;
                    for (int m = 0; m < 8; ++m) {
                        sum += basis[m][r] * rows[m][c];
                    }
                    sum += 128.0;
                    uint8_t value;
                    if (sum < 0.0) value = 0;
                    else if (sum > 255.0) value = 255;
                    else value = (uint8_t)(int64_t)(sum + 0.5);
                    target[(size_t)r * (size_t)width + (size_t)c] = value;
                }
            }
        }
    }

    *out_width = width;
    *out_height = height;
    *out_plane = plane;
    return NMO_OK;
}
