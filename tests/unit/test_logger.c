/**
 * @file test_logger.c
 * @brief Unit tests for logging
 */

#include "../test_framework.h"
#include "core/nmo_logger.h"

TEST(logger, create_stderr_logger) {
    nmo_logger_t logger = nmo_logger_stderr();
    ASSERT_NOT_NULL(logger.log);
}

TEST(logger, create_null_logger) {
    nmo_logger_t logger = nmo_logger_null();
    ASSERT_NOT_NULL(logger.log);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(logger, create_stderr_logger);
    REGISTER_TEST(logger, create_null_logger);
TEST_MAIN_END()
