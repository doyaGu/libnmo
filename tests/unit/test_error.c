/**
 * @file test_error.c
 * @brief Unit tests for error handling
 */

#include "../test_framework.h"
#include "nmo.h"

/**
 * Test error code OK
 */
TEST(error, code_ok) {
    ASSERT_EQ(NMO_OK, 0);
}

/**
 * Test nmo_status_t return codes
 */
TEST(error, status_codes) {
    nmo_last_error_clear();
    nmo_status_t result = NMO_OK;
    ASSERT_EQ(result, NMO_OK);
    ASSERT_TRUE(result == NMO_OK);
    ASSERT_FALSE(result != NMO_OK);
}

/**
 * Test TLS last-error API
 */
TEST(error, last_error_api) {
    /* Clear any previous errors */
    nmo_last_error_clear();
    ASSERT_EQ(nmo_last_error_code(), NMO_OK);
    
    /* Set an error using the TLS API */
    nmo_last_error_setf(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, __FILE__, __LINE__, "Test error %d", 42);
    ASSERT_EQ(nmo_last_error_code(), NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(nmo_last_error_severity(), NMO_SEVERITY_ERROR);
    
    char msg[256];
    nmo_last_error_message_copy(msg, sizeof(msg));
    ASSERT_TRUE(strstr(msg, "Test error 42") != NULL);
    
    /* Clear and verify */
    nmo_last_error_clear();
    ASSERT_EQ(nmo_last_error_code(), NMO_OK);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(error, code_ok);
    REGISTER_TEST(error, status_codes);
    REGISTER_TEST(error, last_error_api);
TEST_MAIN_END()
