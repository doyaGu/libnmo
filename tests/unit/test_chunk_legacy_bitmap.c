#include "../test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_image.h"

static void init_desc_argb32(nmo_image_desc_t *desc,
                             nmo_arena_t *arena,
                             int width,
                             int height) {
    nmo_image_desc_init_argb32(desc, width, height);
    size_t size = nmo_image_calc_size(desc);
    desc->image_data = nmo_arena_alloc(arena, size, 16);
    ASSERT_NOT_NULL(desc->image_data);
}

static void fill_checker(uint8_t *buffer, int width, int height) {
    uint32_t *pixels = (uint32_t *)buffer;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            uint8_t shade = ((x / 4) % 2 == (y / 4) % 2) ? 240u : 10u;
            uint8_t r = shade;
            uint8_t g = (uint8_t)(255u - shade);
            uint8_t b = (uint8_t)((x * 13 + y * 7) & 0xFF);
            pixels[idx] = (0xFFu << 24) |
                          ((uint32_t)r << 16) |
                          ((uint32_t)g << 8) |
                          (uint32_t)b;
        }
    }
}

static void fill_alpha_pattern(uint8_t *buffer, int width, int height) {
    uint32_t *pixels = (uint32_t *)buffer;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            uint8_t r = (uint8_t)(x * 17);
            uint8_t g = (uint8_t)(y * 23);
            uint8_t b = (uint8_t)((x + y) * 9);
            uint8_t a = (uint8_t)((x * y) & 0xFFu);
            pixels[idx] = ((uint32_t)a << 24) |
                          ((uint32_t)r << 16) |
                          ((uint32_t)g << 8) |
                          (uint32_t)b;
        }
    }
}

TEST(chunk_legacy_bitmap, png_roundtrip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 512 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    init_desc_argb32(&desc, arena, 16, 16);
    fill_checker(desc.image_data, desc.width, desc.height);

    nmo_bitmap_properties_t props = {
        .format = NMO_BITMAP_FORMAT_PNG,
        .quality = 0,
        .compression_level = 0,
        .save_alpha = true,
        .extension = "png",
    };

    nmo_chunk_start_write(chunk);
    nmo_result_t result = nmo_chunk_write_bitmap_legacy(chunk, &desc, &props);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t decoded;
    uint8_t *decoded_pixels = NULL;
    result = nmo_chunk_read_bitmap_legacy(chunk, &decoded, &decoded_pixels);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(decoded_pixels);
    ASSERT_EQ(decoded.width, desc.width);
    ASSERT_EQ(decoded.height, desc.height);
    ASSERT_MEM_EQ(decoded_pixels, desc.image_data, nmo_image_calc_size(&desc));

    nmo_arena_destroy(arena);
}

TEST(chunk_legacy_bitmap, bmp_forces_opaque_alpha) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 512 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    init_desc_argb32(&desc, arena, 8, 8);
    fill_alpha_pattern(desc.image_data, desc.width, desc.height);

    nmo_bitmap_properties_t props = {
        .format = NMO_BITMAP_FORMAT_BMP,
        .quality = 0,
        .compression_level = 0,
        .save_alpha = false,
        .extension = "bmp",
    };

    nmo_chunk_start_write(chunk);
    nmo_result_t result = nmo_chunk_write_bitmap_legacy(chunk, &desc, &props);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t decoded;
    uint8_t *decoded_pixels = NULL;
    result = nmo_chunk_read_bitmap_legacy(chunk, &decoded, &decoded_pixels);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(decoded_pixels);
    ASSERT_EQ(decoded.width, desc.width);
    ASSERT_EQ(decoded.height, desc.height);

    size_t total_pixels = (size_t)desc.width * (size_t)desc.height;
    uint32_t *original = (uint32_t *)desc.image_data;
    uint32_t *roundtrip = (uint32_t *)decoded_pixels;
    for (size_t i = 0; i < total_pixels; ++i) {
        uint32_t orig_rgb = original[i] & 0x00FFFFFFu;
        uint32_t read_rgb = roundtrip[i] & 0x00FFFFFFu;
        ASSERT_EQ(orig_rgb, read_rgb);
        ASSERT_EQ(roundtrip[i] >> 24, 0xFFu);
    }

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_legacy_bitmap, png_roundtrip);
    REGISTER_TEST(chunk_legacy_bitmap, bmp_forces_opaque_alpha);
TEST_MAIN_END()
