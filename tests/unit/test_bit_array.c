/**
 * @file test_bit_array.c
 * @brief Unit tests for nmo_bit_array (XBitArray equivalent).
 */

#include "nmo.h"
#include "test_framework.h"

#include <string.h>

TEST(bit_array, basic_set_and_test) {
    nmo_bit_array_t bits;
    ASSERT_EQ(NMO_OK, nmo_bit_array_init(&bits, 0, NULL).code);
    ASSERT_EQ(0u, nmo_bit_array_capacity(&bits));

    ASSERT_EQ(NMO_OK, nmo_bit_array_set(&bits, 5).code);
    ASSERT_TRUE(nmo_bit_array_test(&bits, 5));
    ASSERT_FALSE(nmo_bit_array_test(&bits, 4));
    ASSERT_TRUE(nmo_bit_array_capacity(&bits) >= 6);

    ASSERT_EQ(NMO_OK, nmo_bit_array_clear(&bits, 5).code);
    ASSERT_FALSE(nmo_bit_array_test(&bits, 5));

    nmo_bit_array_dispose(&bits);
}

TEST(bit_array, toggle_and_fill) {
    nmo_bit_array_t bits;
    ASSERT_EQ(NMO_OK, nmo_bit_array_init(&bits, 64, NULL).code);
    ASSERT_EQ(64u, nmo_bit_array_capacity(&bits));

    ASSERT_EQ(NMO_OK, nmo_bit_array_toggle(&bits, 10).code);
    ASSERT_TRUE(nmo_bit_array_test(&bits, 10));
    ASSERT_EQ(NMO_OK, nmo_bit_array_toggle(&bits, 10).code);
    ASSERT_FALSE(nmo_bit_array_test(&bits, 10));

    nmo_bit_array_fill(&bits, 1);
    ASSERT_EQ(64u, nmo_bit_array_count_set(&bits));
    nmo_bit_array_clear_all(&bits);
    ASSERT_EQ(0u, nmo_bit_array_count_set(&bits));

    nmo_bit_array_dispose(&bits);
}

TEST(bit_array, find_ordinals) {
    nmo_bit_array_t bits;
    ASSERT_EQ(NMO_OK, nmo_bit_array_init(&bits, 0, NULL).code);

    ASSERT_EQ(NMO_OK, nmo_bit_array_set(&bits, 2).code);
    ASSERT_EQ(NMO_OK, nmo_bit_array_set(&bits, 5).code);
    ASSERT_EQ(NMO_OK, nmo_bit_array_set(&bits, 9).code);

    ASSERT_EQ(3u, nmo_bit_array_count_set(&bits));
    ASSERT_EQ(2u, nmo_bit_array_find_nth_set(&bits, 0));
    ASSERT_EQ(5u, nmo_bit_array_find_nth_set(&bits, 1));
    ASSERT_EQ(9u, nmo_bit_array_find_nth_set(&bits, 2));
    ASSERT_EQ(SIZE_MAX, nmo_bit_array_find_nth_set(&bits, 3));

    size_t first_unset = nmo_bit_array_find_nth_unset(&bits, 0);
    ASSERT_NE(SIZE_MAX, first_unset);
    ASSERT_FALSE(nmo_bit_array_test(&bits, first_unset));

    size_t far_unset = nmo_bit_array_find_nth_unset(&bits, 100);
    ASSERT_FALSE(nmo_bit_array_test(&bits, far_unset));
    ASSERT_TRUE(nmo_bit_array_capacity(&bits) > far_unset);

    nmo_bit_array_dispose(&bits);
}

TEST(bit_array, bitwise_ops) {
    nmo_bit_array_t lhs;
    nmo_bit_array_t rhs;
    ASSERT_EQ(NMO_OK, nmo_bit_array_init(&lhs, 32, NULL).code);
    ASSERT_EQ(NMO_OK, nmo_bit_array_init(&rhs, 32, NULL).code);

    nmo_bit_array_set(&lhs, 1);
    nmo_bit_array_set(&lhs, 3);
    nmo_bit_array_set(&rhs, 3);
    nmo_bit_array_set(&rhs, 4);

    ASSERT_EQ(NMO_OK, nmo_bit_array_and(&lhs, &rhs).code);
    ASSERT_FALSE(nmo_bit_array_test(&lhs, 1));
    ASSERT_TRUE(nmo_bit_array_test(&lhs, 3));
    ASSERT_FALSE(nmo_bit_array_test(&lhs, 4));

    ASSERT_EQ(NMO_OK, nmo_bit_array_or(&lhs, &rhs).code);
    ASSERT_TRUE(nmo_bit_array_test(&lhs, 4));

    ASSERT_EQ(NMO_OK, nmo_bit_array_xor(&lhs, &rhs).code);
    ASSERT_FALSE(nmo_bit_array_test(&lhs, 3));
    ASSERT_TRUE(nmo_bit_array_test(&lhs, 4));

    nmo_bit_array_not(&lhs);
    ASSERT_FALSE(nmo_bit_array_test(&lhs, 4)); /* was 1, now inverted */

    nmo_bit_array_dispose(&lhs);
    nmo_bit_array_dispose(&rhs);
}

TEST(bit_array, to_string) {
    nmo_bit_array_t bits;
    ASSERT_EQ(NMO_OK, nmo_bit_array_init(&bits, 8, NULL).code);

    nmo_bit_array_set(&bits, 0);
    nmo_bit_array_set(&bits, 3);
    nmo_bit_array_set(&bits, 7);

    char buffer[32];
    ASSERT_NOT_NULL(nmo_bit_array_to_string(&bits, buffer, sizeof(buffer)));
    ASSERT_EQ(strlen(buffer), nmo_bit_array_capacity(&bits));
    ASSERT_EQ('1', buffer[0]);
    ASSERT_EQ('1', buffer[3]);
    ASSERT_EQ('1', buffer[7]);

    nmo_bit_array_dispose(&bits);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(bit_array, basic_set_and_test);
    REGISTER_TEST(bit_array, toggle_and_fill);
    REGISTER_TEST(bit_array, find_ordinals);
    REGISTER_TEST(bit_array, bitwise_ops);
    REGISTER_TEST(bit_array, to_string);
TEST_MAIN_END()
