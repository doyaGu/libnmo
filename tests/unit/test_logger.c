/**
 * @file test_logger.c
 * @brief Unit tests for logging
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(logger, create_stderr_logger) {
    nmo_logger *logger = nmo_logger_stderr();
    ASSERT_NOT_NULL(logger);
}

TEST(logger, create_null_logger) {
    nmo_logger *logger = nmo_logger_null();
    ASSERT_NOT_NULL(logger);
}

TEST(logger, log_message) {
    nmo_logger *logger = nmo_logger_stderr();
    ASSERT_NOT_NULL(logger);
    nmo_logger_log(logger, NMO_LOG_INFO, "Test message");
}

int main(void) {
    return 0;
}
