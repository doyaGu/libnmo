/**
 * @file test_stb_adapter.c
 * @brief Tests for stb adapter helpers.
 */

#include "nmo.h"
#include "format/nmo_stb_adapter.h"
#include "test_framework.h"

#include <string.h>

static void write_u32_le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void write_u16_le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

TEST(stb_adapter, load_embedded_bitmap) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NOT_NULL(arena);

    uint8_t bmp[70] = {0};
    bmp[0] = 'B';
    bmp[1] = 'M';
    write_u32_le(&bmp[2], sizeof(bmp));
    write_u32_le(&bmp[10], 54);      /* Pixel data offset */
    write_u32_le(&bmp[14], 40);      /* BITMAPINFOHEADER size */
    write_u32_le(&bmp[18], 2);       /* width */
    write_u32_le(&bmp[22], 2);       /* height */
    write_u16_le(&bmp[26], 1);       /* planes */
    write_u16_le(&bmp[28], 24);      /* bpp */
    write_u32_le(&bmp[34], 16);      /* image size */
    write_u32_le(&bmp[38], 2835);    /* x ppm */
    write_u32_le(&bmp[42], 2835);    /* y ppm */

    uint8_t *pixel = bmp + 54;
    /* Bottom row: blue, green */
    pixel[0] = 0xFF; pixel[1] = 0x00; pixel[2] = 0x00;  /* blue */
    pixel[3] = 0x00; pixel[4] = 0xFF; pixel[5] = 0x00;  /* green */
    pixel[6] = 0x00; pixel[7] = 0x00;                   /* padding */
    /* Top row: red, white */
    pixel[8]  = 0x00; pixel[9]  = 0x00; pixel[10] = 0xFF;       /* red */
    pixel[11] = 0xFF; pixel[12] = 0xFF; pixel[13] = 0xFF;       /* white */
    pixel[14] = 0x00; pixel[15] = 0x00;                         /* padding */

    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t *pixels = nmo_stbi_load_from_memory(
        arena,
        bmp,
        (int)sizeof(bmp),
        &width,
        &height,
        &channels,
        4);

    ASSERT_NOT_NULL(pixels);
    ASSERT_EQ(2, width);
    ASSERT_EQ(2, height);
    ASSERT_EQ(3, channels);

    static const uint8_t expected_colors[4][4] = {
        {255, 0, 0, 255},
        {255, 255, 255, 255},
        {0, 0, 255, 255},
        {0, 255, 0, 255}
    };

    bool color_found[4] = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        const uint8_t *pixel_rgba = pixels + (i * 4);
        for (int j = 0; j < 4; ++j) {
            if (!color_found[j] && memcmp(pixel_rgba, expected_colors[j], 4) == 0) {
                color_found[j] = true;
                break;
            }
        }
    }

    for (int j = 0; j < 4; ++j) {
        ASSERT_TRUE(color_found[j]);
    }

    nmo_arena_destroy(arena);
}

TEST(stb_adapter, write_png_signature) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 256 * 1024);
    ASSERT_NOT_NULL(arena);

    static const uint8_t pixels[] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255
    };

    size_t png_size = 0;
    uint8_t *png_data = nmo_stbi_write_to_memory(
        arena,
        NMO_BITMAP_FORMAT_PNG,
        2,
        2,
        4,
        pixels,
        0,
        &png_size);

    ASSERT_NOT_NULL(png_data);
    ASSERT_GT(png_size, 0u);
    ASSERT_EQ(0x89, png_data[0]);
    ASSERT_EQ(0x50, png_data[1]);
    ASSERT_EQ(0x4E, png_data[2]);
    ASSERT_EQ(0x47, png_data[3]);

    nmo_arena_destroy(arena);
}

TEST(stb_adapter, roundtrip_png) {
    nmo_arena_t *arena_encode = nmo_arena_create(NULL, 512 * 1024);
    nmo_arena_t *arena_decode = nmo_arena_create(NULL, 512 * 1024);
    ASSERT_NOT_NULL(arena_encode);
    ASSERT_NOT_NULL(arena_decode);

    static const uint8_t pixels[] = {
        128, 64, 32, 255,
        255, 255, 255, 255,
        0, 0, 0, 255,
        100, 150, 200, 128
    };

    size_t png_size = 0;
    uint8_t *png_data = nmo_stbi_write_to_memory(
        arena_encode,
        NMO_BITMAP_FORMAT_PNG,
        2,
        2,
        4,
        pixels,
        0,
        &png_size);

    ASSERT_NOT_NULL(png_data);
    ASSERT_GT(png_size, 0u);

    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t *decoded = nmo_stbi_load_from_memory(
        arena_decode,
        png_data,
        (int)png_size,
        &width,
        &height,
        &channels,
        4);

    ASSERT_NOT_NULL(decoded);
    ASSERT_EQ(2, width);
    ASSERT_EQ(2, height);
    ASSERT_GT(channels, 0);
    ASSERT_EQ(0, memcmp(pixels, decoded, sizeof(pixels)));

    nmo_arena_destroy(arena_encode);
    nmo_arena_destroy(arena_decode);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(stb_adapter, load_embedded_bitmap);
    REGISTER_TEST(stb_adapter, write_png_signature);
    REGISTER_TEST(stb_adapter, roundtrip_png);
TEST_MAIN_END()
