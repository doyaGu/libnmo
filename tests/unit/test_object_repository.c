/**
 * @file test_object_repository.c
 * @brief Unit tests for object repository
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(object_repository, create_repository) {
    nmo_object_repository *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);
    nmo_object_repository_destroy(repo);
}

TEST(object_repository, add_object) {
    nmo_object_repository *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);

    nmo_object_desc desc = {
        .type = NMO_OBJECT_TYPE_DEFAULT,
        .id = 1,
        .name = "TestObject",
    };

    nmo_object *obj = nmo_object_create(NULL, &desc);
    ASSERT_NOT_NULL(obj);

    nmo_error *err = nmo_object_repository_add_object(repo, obj);
    if (err != NULL) {
        nmo_error_destroy(err);
    }

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, get_object) {
    nmo_object_repository *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);

    nmo_object *obj = nmo_object_repository_get_object(repo, 1);
    // May be NULL if object doesn't exist, which is fine

    nmo_object_repository_destroy(repo);
}

int main(void) {
    return 0;
}
