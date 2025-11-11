/**
 * @file test_header1.c
 * @brief Unit tests for NMO Header1 format
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(header1, create) {
    nmo_header1_desc desc = {
        .master_version = 1,
        .file_version = 1,
    };

    nmo_header1 *header = nmo_header1_create(NULL, &desc);
    ASSERT_NOT_NULL(header);
    nmo_header1_destroy(header);
}

TEST(header1, get_versions) {
    nmo_header1_desc desc = {
        .master_version = 2,
        .file_version = 3,
    };

    nmo_header1 *header = nmo_header1_create(NULL, &desc);
    ASSERT_NOT_NULL(header);

    uint32_t master = nmo_header1_get_master_version(header);
    uint32_t file = nmo_header1_get_file_version(header);
    ASSERT_EQ(master, 2);
    ASSERT_EQ(file, 3);

    nmo_header1_destroy(header);
}

int main(void) {
    return 0;
}
