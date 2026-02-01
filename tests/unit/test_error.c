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
 * Test error creation
 */
TEST(error, create) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_error_t* err = nmo_error_create(arena, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Test error", __FILE__, __LINE__);
    ASSERT_NOT_NULL(err);
    ASSERT_EQ(err->code, NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(err->severity, NMO_SEVERITY_ERROR);
    ASSERT_STR_EQ(err->message, "Test error");
    
    nmo_arena_destroy(arena);
}

/**
 * Test error message
 */
TEST(error, message) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_error_t* err = nmo_error_create(arena, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Test error message", __FILE__, __LINE__);
    ASSERT_NOT_NULL(err);
    
    const char* msg = nmo_error_string(NMO_ERR_INVALID_ARGUMENT);
    ASSERT_NOT_NULL(msg);
    ASSERT_TRUE(strlen(msg) > 0);
    
    nmo_arena_destroy(arena);
}

/**
 * Test result creation
 */
TEST(error, result_create) {
    nmo_result_t result = nmo_result_ok();
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NULL(result.error);
    
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_error_t* err = nmo_error_create(arena, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Test error", __FILE__, __LINE__);
    nmo_result_t error_result = nmo_result_error(err);
    ASSERT_EQ(error_result.code, NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(error_result.error, err);
    
    nmo_arena_destroy(arena);
}

TEST(error, free_non_arena_chain) {
    nmo_error_t *cause = nmo_error_createf_at(NULL,
                                              NMO_ERR_INVALID_ARGUMENT,
                                              NMO_SEVERITY_ERROR,
                                              __FILE__,
                                              __LINE__,
                                              "Cause %d", 1);
    ASSERT_NOT_NULL(cause);

    nmo_error_t *err = nmo_error_createf_at(NULL,
                                            NMO_ERR_INVALID_STATE,
                                            NMO_SEVERITY_ERROR,
                                            __FILE__,
                                            __LINE__,
                                            "Top error");
    ASSERT_NOT_NULL(err);
    nmo_error_add_cause(err, cause);

    nmo_error_free(err);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(error, code_ok);
    REGISTER_TEST(error, create);
    REGISTER_TEST(error, message);
    REGISTER_TEST(error, result_create);
    REGISTER_TEST(error, free_non_arena_chain);
TEST_MAIN_END()
