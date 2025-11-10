/**
 * @file test_object.c
 * @brief Unit tests for NMO objects
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(object, create_object) {
    nmo_object_desc desc = {
        .type = NMO_OBJECT_TYPE_DEFAULT,
        .id = 1,
        .name = "TestObject",
    };

    nmo_object *obj = nmo_object_create(NULL, &desc);
    ASSERT_NOT_NULL(obj);
    nmo_object_destroy(obj);
}

TEST(object, get_id) {
    nmo_object_desc desc = {
        .type = NMO_OBJECT_TYPE_DEFAULT,
        .id = 42,
        .name = "TestObject",
    };

    nmo_object *obj = nmo_object_create(NULL, &desc);
    ASSERT_NOT_NULL(obj);

    uint32_t id = nmo_object_get_id(obj);
    ASSERT_EQ(id, 42);

    nmo_object_destroy(obj);
}

int main(void) {
    return 0;
}
