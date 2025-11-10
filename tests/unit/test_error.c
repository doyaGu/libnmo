/**
 * @file test_error.c
 * @brief Unit tests for error handling
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(error, error_code_ok) {
    ASSERT_EQ(NMO_OK, 0);
}

TEST(error, error_create) {
    nmo_error *err = nmo_error_create(NMO_ERROR_INVALID_ARGUMENT, "Test error");
    ASSERT_NOT_NULL(err);
    nmo_error_destroy(err);
}

TEST(error, error_message) {
    nmo_error *err = nmo_error_create(NMO_ERROR_INVALID_ARGUMENT, "Test error");
    ASSERT_NOT_NULL(err);
    ASSERT_NOT_NULL(nmo_error_message(err));
    nmo_error_destroy(err);
}

int main(void) {
    return 0;
}
