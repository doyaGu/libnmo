#include "../test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_image.h"

#include <string.h>

static void prepare_argb_image(nmo_image_desc_t *desc,
                               nmo_arena_t *arena,
                               int width,
                               int height,
                               uint32_t color) {
    nmo_image_desc_init_argb32(desc, width, height);
    size_t image_size = nmo_image_calc_size(desc);
    desc->image_data = nmo_arena_alloc(arena, image_size, 16);
    ASSERT_NOT_NULL(desc->image_data);
    uint32_t *pixels = (uint32_t *)desc->image_data;
    for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
        pixels[i] = color;
    }
}

static uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t red = (uint16_t)(r >> 3) << 11;
    uint16_t green = (uint16_t)(g >> 2) << 5;
    uint16_t blue = (uint16_t)(b >> 3);
    return (uint16_t)(red | green | blue);
}

TEST(chunk_raw_bitmap, write_read_argb32_roundtrip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    prepare_argb_image(&desc, arena, 32, 32, 0xFF3366CCu);

    nmo_chunk_start_write(chunk);
    nmo_result_t result = nmo_chunk_write_raw_bitmap(chunk, &desc);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t out_desc;
    uint8_t *out_pixels = NULL;
    result = nmo_chunk_read_raw_bitmap(chunk, &out_desc, &out_pixels);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(out_pixels);
    ASSERT_EQ(out_desc.width, 32);
    ASSERT_EQ(out_desc.height, 32);
    ASSERT_EQ(out_desc.bits_per_pixel, 32);

    size_t expected_size = nmo_image_calc_size(&desc);
    ASSERT_EQ(expected_size, nmo_image_calc_size(&out_desc));
    ASSERT_MEM_EQ(out_pixels, desc.image_data, expected_size);

    nmo_arena_destroy(arena);
}

TEST(chunk_raw_bitmap, write_read_gradient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 256 * 1024);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    nmo_image_desc_init_argb32(&desc, 256, 1);
    size_t img_size = nmo_image_calc_size(&desc);
    desc.image_data = nmo_arena_alloc(arena, img_size, 16);
    ASSERT_NOT_NULL(desc.image_data);
    uint32_t *pixels = (uint32_t *)desc.image_data;
    for (int x = 0; x < 256; ++x) {
        uint8_t gray = (uint8_t)x;
        pixels[x] = 0xFF000000u | ((uint32_t)gray << 16) |
                    ((uint32_t)gray << 8) | gray;
    }

    nmo_chunk_start_write(chunk);
    nmo_result_t result = nmo_chunk_write_raw_bitmap(chunk, &desc);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t read_desc;
    uint8_t *read_pixels = NULL;
    result = nmo_chunk_read_raw_bitmap(chunk, &read_desc, &read_pixels);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(read_pixels);
    ASSERT_EQ(read_desc.width, 256);
    ASSERT_EQ(read_desc.height, 1);

    uint32_t *decoded = (uint32_t *)read_pixels;
    for (int x = 0; x < 256; ++x) {
        uint8_t gray = (uint8_t)x;
        uint32_t expected = 0xFF000000u | ((uint32_t)gray << 16) |
                            ((uint32_t)gray << 8) | gray;
        ASSERT_EQ(decoded[x], expected);
    }

    nmo_arena_destroy(arena);
}

TEST(chunk_raw_bitmap, write_read_rgb565_conversion) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 256 * 1024);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.format = NMO_PIXEL_FORMAT_16_RGB565;
    desc.width = 4;
    desc.height = 2;
    desc.bits_per_pixel = 16;
    desc.bytes_per_line = nmo_image_calc_bytes_per_line(desc.width, desc.bits_per_pixel);
    desc.red_mask = 0xF800u;
    desc.green_mask = 0x07E0u;
    desc.blue_mask = 0x001Fu;
    desc.alpha_mask = 0;

    size_t payload_size = (size_t)desc.bytes_per_line * (size_t)desc.height;
    desc.image_data = nmo_arena_alloc(arena, payload_size, 4);
    ASSERT_NOT_NULL(desc.image_data);

    uint16_t pattern[8] = {
        pack_rgb565(255, 0, 0),
        pack_rgb565(0, 255, 0),
        pack_rgb565(0, 0, 255),
        pack_rgb565(255, 255, 255),
        pack_rgb565(64, 64, 64),
        pack_rgb565(128, 16, 240),
        pack_rgb565(12, 200, 32),
        pack_rgb565(0, 0, 0)
    };

    for (int y = 0; y < desc.height; ++y) {
        uint16_t *row = (uint16_t *)(desc.image_data + (size_t)y * (size_t)desc.bytes_per_line);
        for (int x = 0; x < desc.width; ++x) {
            row[x] = pattern[y * desc.width + x];
        }
    }

    nmo_chunk_start_write(chunk);
    nmo_result_t result = nmo_chunk_write_raw_bitmap(chunk, &desc);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t read_desc;
    uint8_t *decoded_pixels = NULL;
    result = nmo_chunk_read_raw_bitmap(chunk, &read_desc, &decoded_pixels);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(decoded_pixels);
    ASSERT_EQ(read_desc.width, desc.width);
    ASSERT_EQ(read_desc.height, desc.height);

    nmo_mask_shifts_t shifts;
    nmo_image_calculate_mask_shifts(desc.red_mask,
                                    desc.green_mask,
                                    desc.blue_mask,
                                    desc.alpha_mask,
                                    &shifts);

    uint32_t *decoded = (uint32_t *)decoded_pixels;
    for (int y = 0; y < desc.height; ++y) {
        for (int x = 0; x < desc.width; ++x) {
            size_t idx = (size_t)y * (size_t)desc.width + (size_t)x;
            uint16_t packed = pattern[idx];
            uint8_t r = nmo_image_extract_channel(packed, desc.red_mask, &shifts, 0);
            uint8_t g = nmo_image_extract_channel(packed, desc.green_mask, &shifts, 1);
            uint8_t b = nmo_image_extract_channel(packed, desc.blue_mask, &shifts, 2);
            uint32_t expected = 0xFF000000u | ((uint32_t)r << 16) |
                                ((uint32_t)g << 8) | b;
            ASSERT_EQ(decoded[idx], expected);
        }
    }

    nmo_arena_destroy(arena);
}

TEST(chunk_raw_bitmap, empty_descriptor_writes_zero) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    memset(&desc, 0, sizeof(desc));

    nmo_chunk_start_write(chunk);
    nmo_result_t result = nmo_chunk_write_raw_bitmap(chunk, &desc);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t read_desc;
    uint8_t *pixels = NULL;
    result = nmo_chunk_read_raw_bitmap(chunk, &read_desc, &pixels);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NULL(pixels);
    ASSERT_EQ(read_desc.width, 0);
    ASSERT_EQ(read_desc.height, 0);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_raw_bitmap, write_read_argb32_roundtrip);
    REGISTER_TEST(chunk_raw_bitmap, write_read_gradient);
    REGISTER_TEST(chunk_raw_bitmap, write_read_rgb565_conversion);
    REGISTER_TEST(chunk_raw_bitmap, empty_descriptor_writes_zero);
TEST_MAIN_END()
