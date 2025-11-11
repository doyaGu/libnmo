/**
 * @file test_header.c
 * @brief Unit tests for NMO file header
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(header, create_header) {
    nmo_header_desc desc = {
        .major_version = 1,
        .minor_version = 0,
        .flags = NMO_HEADER_FLAG_DEFAULT,
    };

    nmo_header *header = nmo_header_create(NULL, &desc);
    ASSERT_NOT_NULL(header);
    nmo_header_destroy(header);
}

TEST(header, get_version) {
    nmo_header_desc desc = {
        .major_version = 1,
        .minor_version = 0,
        .flags = NMO_HEADER_FLAG_DEFAULT,
    };

    nmo_header *header = nmo_header_create(NULL, &desc);
    ASSERT_NOT_NULL(header);

    uint32_t major = nmo_header_get_major_version(header);
    uint32_t minor = nmo_header_get_minor_version(header);
    ASSERT_EQ(major, 1);
    ASSERT_EQ(minor, 0);

    nmo_header_destroy(header);
}

int main(void) {
    return 0;
}
