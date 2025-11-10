/**
 * @file test_chunk.c
 * @brief Unit tests for chunk handling
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(chunk, create_chunk) {
    nmo_chunk_desc desc = {
        .type = NMO_CHUNK_TYPE_DATA,
        .flags = NMO_CHUNK_FLAG_DEFAULT,
        .size = 256,
    };

    nmo_chunk *chunk = nmo_chunk_create(NULL, &desc);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_destroy(chunk);
}

TEST(chunk, get_type) {
    nmo_chunk_desc desc = {
        .type = NMO_CHUNK_TYPE_DATA,
        .flags = NMO_CHUNK_FLAG_DEFAULT,
        .size = 256,
    };

    nmo_chunk *chunk = nmo_chunk_create(NULL, &desc);
    ASSERT_NOT_NULL(chunk);

    uint32_t type = nmo_chunk_get_type(chunk);
    ASSERT_EQ(type, NMO_CHUNK_TYPE_DATA);

    nmo_chunk_destroy(chunk);
}

int main(void) {
    return 0;
}
