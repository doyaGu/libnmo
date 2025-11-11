/**
 * @file test_guid.c
 * @brief Unit tests for GUID handling
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(guid, create_guid) {
    nmo_guid guid = nmo_guid_create();
    ASSERT_TRUE(nmo_guid_is_valid(&guid));
}

TEST(guid, guid_string_conversion) {
    nmo_guid guid = nmo_guid_create();
    char buffer[37];
    nmo_guid_to_string(&guid, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(buffer);
}

TEST(guid, parse_guid_string) {
    const char *guid_str = "123e4567-e89b-12d3-a456-426614174000";
    nmo_guid guid;
    nmo_error *err = nmo_guid_parse_string(guid_str, &guid);
    ASSERT_NOT_NULL(&guid);
    if (err != NULL) {
        nmo_error_destroy(err);
    }
}

int main(void) {
    return 0;
}
