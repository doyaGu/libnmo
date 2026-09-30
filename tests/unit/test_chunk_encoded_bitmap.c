#include "../test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_image.h"

#include <string.h>

static void fill_checker_argb(uint8_t *buffer, int width, int height) {
    uint32_t *pixels = (uint32_t *)buffer;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            uint8_t shade = ((x / 4) % 2 == (y / 4) % 2) ? 200u : 20u;
            uint8_t r = shade;
            uint8_t g = (uint8_t)(shade / 2u);
            uint8_t b = (uint8_t)(255u - shade);
            pixels[idx] = (0xFFu << 24) |
                          ((uint32_t)r << 16) |
                          ((uint32_t)g << 8) |
                          (uint32_t)b;
        }
    }
}

static void fill_alpha_gradient(uint8_t *buffer, int width, int height) {
    uint32_t *pixels = (uint32_t *)buffer;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            uint8_t r = (uint8_t)(x * 16);
            uint8_t g = (uint8_t)(y * 16);
            uint8_t b = 0x80u;
            uint8_t a = (uint8_t)((x + y) * 8);
            pixels[idx] = ((uint32_t)a << 24) |
                          ((uint32_t)r << 16) |
                          ((uint32_t)g << 8) |
                          (uint32_t)b;
        }
    }
}

static void init_desc_argb32(nmo_image_desc_t *desc,
                             nmo_arena_t *arena,
                             int width,
                             int height) {
    nmo_image_desc_init_argb32(desc, width, height);
    size_t size = nmo_image_calc_size(desc);
    desc->image_data = nmo_arena_alloc(arena, size, 16);
    ASSERT_NOT_NULL(desc->image_data);
}

TEST(chunk_encoded_bitmap, png_roundtrip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 512 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    init_desc_argb32(&desc, arena, 16, 16);
    fill_checker_argb(desc.image_data, desc.width, desc.height);

    nmo_bitmap_properties_t props = {
        .format = NMO_BITMAP_FORMAT_PNG,
        .quality = 0,
        .compression_level = 0,
        .save_alpha = true,
        .extension = "png",
    };

    nmo_chunk_start_write(chunk);
    nmo_status_t result = nmo_chunk_write_encoded_bitmap(chunk, &desc, &props);
    ASSERT_EQ(result, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t decoded;
    uint8_t *decoded_pixels = NULL;
    result = nmo_chunk_read_encoded_bitmap(chunk, &decoded, &decoded_pixels);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(decoded_pixels);
    ASSERT_EQ(decoded.width, desc.width);
    ASSERT_EQ(decoded.height, desc.height);
    ASSERT_MEM_EQ(decoded_pixels, desc.image_data, nmo_image_calc_size(&desc));

    nmo_arena_destroy(arena);
}

TEST(chunk_encoded_bitmap, jpeg_with_alpha_plane) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 512 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_image_desc_t desc;
    init_desc_argb32(&desc, arena, 8, 8);
    fill_alpha_gradient(desc.image_data, desc.width, desc.height);

    nmo_bitmap_properties_t props = {
        .format = NMO_BITMAP_FORMAT_JPG,
        .quality = 90,
        .compression_level = 0,
        .save_alpha = false,
        .extension = "jpg",
    };

    nmo_chunk_start_write(chunk);
    nmo_status_t result = nmo_chunk_write_encoded_bitmap(chunk, &desc, &props);
    ASSERT_EQ(result, NMO_OK);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_image_desc_t decoded;
    uint8_t *decoded_pixels = NULL;
    result = nmo_chunk_read_encoded_bitmap(chunk, &decoded, &decoded_pixels);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(decoded_pixels);
    ASSERT_EQ(decoded.width, desc.width);
    ASSERT_EQ(decoded.height, desc.height);

    size_t total_pixels = (size_t)desc.width * (size_t)desc.height;
    for (size_t i = 0; i < total_pixels; ++i) {
        ASSERT_EQ(decoded_pixels[i * 4u + 3u], desc.image_data[i * 4u + 3u]);
    }

    nmo_arena_destroy(arena);
}

TEST(chunk_encoded_bitmap, truncated_payload_keeps_position) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 256 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_int(chunk, 1); /* NMO_BITMAP_STORE_ENCODED wire value */
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk,
                                   (uint32_t)'p' |
                                   ((uint32_t)'n' << 8u) |
                                   ((uint32_t)'g' << 16u));
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_guid(chunk, (nmo_guid_t){0x02D45C7Bu, 0x4AAC16ECu});
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_dword(chunk, 8u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0xAABBCCDDu);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_image_desc_t decoded;
    uint8_t *decoded_pixels = NULL;
    result = nmo_chunk_read_encoded_bitmap(chunk, &decoded, &decoded_pixels);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_NULL(decoded_pixels);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_encoded_bitmap, layout_is_the_engine_reader_bitmap) {
    /* WriteReaderBitmap: kind, extension, GUID, size-prefixed image and, for a
     * codec without alpha, the number of distinct alpha values with that value
     * or the alpha plane. */
    nmo_arena_t *arena = nmo_arena_create(NULL, 512 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_image_desc_t desc;
    init_desc_argb32(&desc, arena, 8, 8);
    fill_alpha_gradient(desc.image_data, desc.width, desc.height);

    const nmo_bitmap_properties_t png = {.format = NMO_BITMAP_FORMAT_PNG, .save_alpha = true, .extension = "png"};
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_encoded_bitmap(chunk, &desc, &png));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    int32_t kind = 0;
    uint32_t extension = 0;
    nmo_guid_t guid;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(chunk, &kind));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &extension));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_guid(chunk, &guid));
    ASSERT_EQ(1, kind);
    ASSERT_EQ(0x00676E70u, extension);
    ASSERT_EQ(0x02D45C7Bu, guid.d1);
    ASSERT_EQ(0x4AAC16ECu, guid.d2);

    const nmo_bitmap_properties_t jpg = {.format = NMO_BITMAP_FORMAT_JPG, .quality = 90, .extension = "jpg"};
    nmo_chunk_t *with_alpha = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(with_alpha);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(with_alpha));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_encoded_bitmap(with_alpha, &desc, &jpg));
    nmo_chunk_close(with_alpha);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(with_alpha));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(with_alpha, &kind));
    ASSERT_EQ(2, kind);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(with_alpha, &extension));
    ASSERT_EQ(0x0067706Au, extension);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_guid(with_alpha, &guid));
    ASSERT_EQ(0x4AE51AC4u, guid.d1);
    void *encoded = NULL;
    size_t encoded_size = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_buffer(with_alpha, &encoded, &encoded_size));
    ASSERT_TRUE(encoded_size > 0);
    int32_t distinct = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(with_alpha, &distinct));
    ASSERT_TRUE(distinct > 1);
    void *plane = NULL;
    size_t plane_size = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_buffer(with_alpha, &plane, &plane_size));
    ASSERT_EQ(64u, plane_size);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_encoded_bitmap, png_roundtrip);
    REGISTER_TEST(chunk_encoded_bitmap, jpeg_with_alpha_plane);
    REGISTER_TEST(chunk_encoded_bitmap, truncated_payload_keeps_position);
    REGISTER_TEST(chunk_encoded_bitmap, layout_is_the_engine_reader_bitmap);
TEST_MAIN_END()
