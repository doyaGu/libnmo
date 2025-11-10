/**
 * @file test_chunk_serialize.c
 * @brief Unit tests for chunk serialization
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(chunk_serialize, serialize_chunk) {
    nmo_chunk_desc desc = {
        .type = NMO_CHUNK_TYPE_DATA,
        .flags = NMO_CHUNK_FLAG_DEFAULT,
        .size = 100,
    };

    nmo_chunk *chunk = nmo_chunk_create(NULL, &desc);
    ASSERT_NOT_NULL(chunk);

    uint8_t buffer[256];
    size_t written = 0;
    nmo_error *err = nmo_chunk_serialize(chunk, buffer, sizeof(buffer), &written);

    if (err != NULL) {
        nmo_error_destroy(err);
    }

    nmo_chunk_destroy(chunk);
}

TEST(chunk_serialize, deserialize_chunk) {
    uint8_t buffer[256] = {0};
    size_t read = 0;

    nmo_chunk *chunk = nmo_chunk_deserialize(NULL, buffer, sizeof(buffer), &read);
    if (chunk != NULL) {
        nmo_chunk_destroy(chunk);
    }
}

int main(void) {
    return 0;
}
