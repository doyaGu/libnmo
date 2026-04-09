/**
 * @file test_image_decode.c
 * @brief Unit tests for nmo_image_decode_interleaved_to_rgba32.
 */

#include "nmo.h"
#include "test_framework.h"

#include <string.h>

/* Helper: build a 1x1 image descriptor with given pixel data (LE). */
static void make_1x1_desc(nmo_image_desc_t *desc,
                           nmo_pixel_format_t fmt,
                           int bpp,
                           uint32_t rmask, uint32_t gmask,
                           uint32_t bmask, uint32_t amask,
                           uint8_t *pixel_buf) {
    memset(desc, 0, sizeof(*desc));
    desc->format = fmt;
    desc->width = 1;
    desc->height = 1;
    desc->bits_per_pixel = bpp;
    desc->bytes_per_line = 0; /* let decoder compute */
    desc->red_mask = rmask;
    desc->green_mask = gmask;
    desc->blue_mask = bmask;
    desc->alpha_mask = amask;
    desc->image_data = pixel_buf;
}

TEST(image_decode, rgb565_decode) {
    /* 0xF800 in RGB565: R=11111, G=000000, B=00000 -> (255,0,0,255) */
    uint8_t pixel[2];
    pixel[0] = 0x00; /* low byte */
    pixel[1] = 0xF8; /* high byte -> 0xF800 LE */

    nmo_image_desc_t desc;
    make_1x1_desc(&desc, NMO_PIXEL_FORMAT_16_RGB565, 16,
                  0xF800u, 0x07E0u, 0x001Fu, 0u, pixel);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    nmo_status_t st = nmo_image_decode_interleaved_to_rgba32(
        &desc, arena, &rgba, &w, &h);

    ASSERT_EQ(NMO_OK, st);
    ASSERT_NOT_NULL(rgba);
    ASSERT_EQ(1, w);
    ASSERT_EQ(1, h);
    ASSERT_EQ(255, (int)rgba[0]); /* R */
    ASSERT_EQ(0,   (int)rgba[1]); /* G */
    ASSERT_EQ(0,   (int)rgba[2]); /* B */
    ASSERT_EQ(255, (int)rgba[3]); /* A (no alpha mask -> 0xFF) */

    nmo_arena_destroy(arena);
}

TEST(image_decode, argb1555_decode) {
    /* 0xFC00 in ARGB1555: A=1, R=11111, G=00000, B=00000
     * masks: A=0x8000, R=0x7C00, G=0x03E0, B=0x001F
     * -> (255,0,0,255) */
    uint8_t pixel[2];
    pixel[0] = 0x00; /* low byte */
    pixel[1] = 0xFC; /* high byte -> 0xFC00 LE */

    nmo_image_desc_t desc;
    make_1x1_desc(&desc, NMO_PIXEL_FORMAT_16_ARGB1555, 16,
                  0x7C00u, 0x03E0u, 0x001Fu, 0x8000u, pixel);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    nmo_status_t st = nmo_image_decode_interleaved_to_rgba32(
        &desc, arena, &rgba, &w, &h);

    ASSERT_EQ(NMO_OK, st);
    ASSERT_NOT_NULL(rgba);
    ASSERT_EQ(1, w);
    ASSERT_EQ(1, h);
    ASSERT_EQ(255, (int)rgba[0]); /* R */
    ASSERT_EQ(0,   (int)rgba[1]); /* G */
    ASSERT_EQ(0,   (int)rgba[2]); /* B */
    ASSERT_EQ(255, (int)rgba[3]); /* A */

    nmo_arena_destroy(arena);
}

TEST(image_decode, rejects_dxt) {
    uint8_t dummy[8] = {0};
    nmo_image_desc_t desc;
    make_1x1_desc(&desc, NMO_PIXEL_FORMAT_DXT1, 4,
                  0, 0, 0, 0, dummy);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    nmo_status_t st = nmo_image_decode_interleaved_to_rgba32(
        &desc, arena, &rgba, &w, &h);

    ASSERT_EQ(NMO_ERR_NOT_SUPPORTED, st);
    ASSERT_NULL(rgba);

    nmo_arena_destroy(arena);
}

TEST(image_decode, rejects_bump) {
    uint8_t dummy[2] = {0};
    nmo_image_desc_t desc;
    make_1x1_desc(&desc, NMO_PIXEL_FORMAT_16_V8U8, 16,
                  0, 0, 0, 0, dummy);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    nmo_status_t st = nmo_image_decode_interleaved_to_rgba32(
        &desc, arena, &rgba, &w, &h);

    ASSERT_EQ(NMO_ERR_NOT_SUPPORTED, st);
    ASSERT_NULL(rgba);

    nmo_arena_destroy(arena);
}

/* ---- DXT decode tests ---- */

TEST(image_decode, dxt1_solid_red) {
    /* DXT1 block: c0=0xF800 (red), c1=0x0000, all indices=00.
     * All 16 pixels should be (255,0,0,255). */
    uint8_t block[8];
    /* c0 = 0xF800 LE */
    block[0] = 0x00;
    block[1] = 0xF8;
    /* c1 = 0x0000 LE */
    block[2] = 0x00;
    block[3] = 0x00;
    /* 4 bytes of indices, all zero (select c0) */
    block[4] = 0x00;
    block[5] = 0x00;
    block[6] = 0x00;
    block[7] = 0x00;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    nmo_status_t st = nmo_image_decode_dxt(
        block, sizeof(block), 4, 4,
        NMO_PIXEL_FORMAT_DXT1, arena, &rgba);

    ASSERT_EQ(NMO_OK, st);
    ASSERT_NOT_NULL(rgba);

    /* Check all 16 pixels are solid red */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(255, (int)rgba[i * 4 + 0]); /* R */
        ASSERT_EQ(0,   (int)rgba[i * 4 + 1]); /* G */
        ASSERT_EQ(0,   (int)rgba[i * 4 + 2]); /* B */
        ASSERT_EQ(255, (int)rgba[i * 4 + 3]); /* A */
    }

    nmo_arena_destroy(arena);
}

TEST(image_decode, dxt_rejects_invalid_format) {
    uint8_t block[8] = {0};

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    nmo_status_t st = nmo_image_decode_dxt(
        block, sizeof(block), 4, 4,
        NMO_PIXEL_FORMAT_16_RGB565, arena, &rgba);

    ASSERT_NE(NMO_OK, st);
    nmo_arena_destroy(arena);
}

TEST(image_decode, dxt_rejects_truncated) {
    /* DXT1 4x4 needs 8 bytes, pass only 4 */
    uint8_t block[4] = {0};

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    nmo_status_t st = nmo_image_decode_dxt(
        block, sizeof(block), 4, 4,
        NMO_PIXEL_FORMAT_DXT1, arena, &rgba);

    ASSERT_NE(NMO_OK, st);
    nmo_arena_destroy(arena);
}

TEST(image_decode, dxt_rejects_zero_dims) {
    uint8_t block[8] = {0};

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *rgba = NULL;
    nmo_status_t st = nmo_image_decode_dxt(
        block, sizeof(block), 0, 4,
        NMO_PIXEL_FORMAT_DXT1, arena, &rgba);

    ASSERT_NE(NMO_OK, st);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(image_decode, rgb565_decode);
    REGISTER_TEST(image_decode, argb1555_decode);
    REGISTER_TEST(image_decode, rejects_dxt);
    REGISTER_TEST(image_decode, rejects_bump);
    REGISTER_TEST(image_decode, dxt1_solid_red);
    REGISTER_TEST(image_decode, dxt_rejects_invalid_format);
    REGISTER_TEST(image_decode, dxt_rejects_truncated);
    REGISTER_TEST(image_decode, dxt_rejects_zero_dims);
TEST_MAIN_END()
