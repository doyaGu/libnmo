/**
 * @file test_manager.c
 * @brief Unit tests for object managers
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(manager, create_manager) {
    nmo_manager_desc desc = {
        .type = NMO_MANAGER_TYPE_DEFAULT,
        .guid = {0},
    };

    nmo_manager *mgr = nmo_manager_create(NULL, &desc);
    ASSERT_NOT_NULL(mgr);
    nmo_manager_destroy(mgr);
}

TEST(manager, get_guid) {
    nmo_guid guid = nmo_guid_create();
    nmo_manager_desc desc = {
        .type = NMO_MANAGER_TYPE_DEFAULT,
        .guid = guid,
    };

    nmo_manager *mgr = nmo_manager_create(NULL, &desc);
    ASSERT_NOT_NULL(mgr);

    nmo_guid retrieved = nmo_manager_get_guid(mgr);
    ASSERT_TRUE(nmo_guid_is_valid(&retrieved));

    nmo_manager_destroy(mgr);
}

int main(void) {
    return 0;
}
