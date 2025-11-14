/**
 * @file test_image_utils.c
 * @brief Unit tests for image helper utilities.
 */

#include "nmo.h"
#include "test_framework.h"

TEST(image_utils, calculate_mask_shifts_rgb565) {
    nmo_mask_shifts_t shifts;
    nmo_image_calculate_mask_shifts(0xF800u, 0x07E0u, 0x001Fu, 0u, &shifts);

    ASSERT_EQ(11u, shifts.red_shift_lsb);
    ASSERT_EQ(3u, shifts.red_shift_msb);
    ASSERT_EQ(5u, shifts.green_shift_lsb);
    ASSERT_EQ(2u, shifts.green_shift_msb);
    ASSERT_EQ(0u, shifts.blue_shift_lsb);
    ASSERT_EQ(3u, shifts.blue_shift_msb);
    ASSERT_EQ(0u, shifts.alpha_shift_lsb);
    ASSERT_EQ(0u, shifts.alpha_shift_msb);
}

TEST(image_utils, extract_channel_rgb565) {
    nmo_mask_shifts_t shifts;
    nmo_image_calculate_mask_shifts(0xF800u, 0x07E0u, 0x001Fu, 0u, &shifts);

    uint16_t pure_red = 0xF800u;
    uint8_t red = nmo_image_extract_channel(pure_red, 0xF800u, &shifts, 0);

    ASSERT_EQ(0xF8u, red);
}

TEST(image_utils, init_argb32) {
    nmo_image_desc_t desc;
    nmo_image_desc_init_argb32(&desc, 100, 100);

    ASSERT_EQ(100, desc.width);
    ASSERT_EQ(100, desc.height);
    ASSERT_EQ(32, desc.bits_per_pixel);
    ASSERT_EQ(400, desc.bytes_per_line);
    ASSERT_EQ(NMO_PIXEL_FORMAT_32_ARGB8888, desc.format);
    ASSERT_EQ(0xFF000000u, desc.alpha_mask);
    ASSERT_EQ(0x00FF0000u, desc.red_mask);
    ASSERT_EQ(0x0000FF00u, desc.green_mask);
    ASSERT_EQ(0x000000FFu, desc.blue_mask);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(image_utils, calculate_mask_shifts_rgb565);
    REGISTER_TEST(image_utils, extract_channel_rgb565);
    REGISTER_TEST(image_utils, init_argb32);
TEST_MAIN_END()
