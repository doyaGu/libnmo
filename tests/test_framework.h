/**
 * @file test_framework.h
 * @brief Modern test framework for libnmo with enhanced features
 * 
 * This framework provides:
 * - Portable test registration (no compiler-specific attributes)
 * - Setup/teardown mechanisms for test fixtures
 * - Enhanced assertion types with detailed error messages
 * - Test categorization and filtering
 * - Timeout handling
 * - Test isolation mechanisms
 * 
 * The framework maintains backward compatibility with existing tests.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "../src/runtime/runtime_internal.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#ifndef NMO_TEST_DATA_DIR
#define NMO_TEST_DATA_DIR "../data"
#endif

#define NMO_TEST_DATA_FILE(name) NMO_TEST_DATA_DIR "/" name

#ifdef __cplusplus
extern "C" {
#endif

/* Test result structure */
typedef struct {
    const char *suite_name;
    const char *test_name;
    int passed;
    int failed;
    const char *failure_message;
    const char *failure_file;
    int failure_line;
    double execution_time_ms;
    const char *category;
} test_result;

/* Test suite structure */
typedef struct {
    test_result *results;
    int count;
    int capacity;
} test_suite;

/* Test function types */
typedef void (*test_func_t)(void);
typedef void (*setup_func_t)(void);
typedef void (*teardown_func_t)(void);

/* Test categories for filtering */
typedef enum {
    TEST_CATEGORY_UNIT = 1 << 0,
    TEST_CATEGORY_INTEGRATION = 1 << 1,
    TEST_CATEGORY_PERFORMANCE = 1 << 2,
    TEST_CATEGORY_STRESS = 1 << 3,
    TEST_CATEGORY_REGRESSION = 1 << 4,
    TEST_CATEGORY_ALL = (TEST_CATEGORY_UNIT | TEST_CATEGORY_INTEGRATION | TEST_CATEGORY_PERFORMANCE | TEST_CATEGORY_STRESS | TEST_CATEGORY_REGRESSION)
} test_category_t;

/* Test registration entry with enhanced features */
typedef struct test_entry {
    const char *suite_name;
    const char *test_name;
    test_func_t func;
    setup_func_t setup;
    teardown_func_t teardown;
    test_category_t category;
    double timeout_seconds;
    int enabled;
    struct test_entry *next;
} test_entry_t;

/* Test configuration structure */
typedef struct {
    int verbose;
    int stop_on_failure;
    test_category_t filter_categories;
    const char *filter_suite;
    const char *filter_test;
    double default_timeout;
} test_config_t;

/* Global variables */
extern test_suite *g_test_suite;
extern test_entry_t *g_test_registry;
extern test_config_t g_test_config;

/* Core framework functions */
void test_framework_init(void);
void test_framework_cleanup(void);
int test_framework_run(void);
void test_framework_configure(test_config_t config);

/* Test registration functions */
void test_register(const char *suite, const char *name, test_func_t func);
void test_register_with_features(const char *suite, const char *name, 
                                 test_func_t func, setup_func_t setup, 
                                 teardown_func_t teardown, 
                                 test_category_t category, 
                                 double timeout_seconds);

/* Result reporting */
void test_add_result(const char *suite, const char *name, int passed,
                     const char *message, const char *file, int line);
void test_add_result_with_time(const char *suite, const char *name, int passed,
                               const char *message, const char *file, int line,
                               double execution_time_ms);

/* Skip support: a test may declare itself skipped (for example when an
 * optional data fixture is absent). Skipped tests are neither passed nor
 * failed. If every executed test in a binary is skipped, main() returns
 * TEST_EXIT_CODE_SKIPPED so CTest can report the binary as skipped. */
#define TEST_EXIT_CODE_SKIPPED 77
void test_mark_skipped(const char *message, const char *file, int line);
int test_file_exists(const char *path);

/* Utility functions */
double test_get_time_ms(void);
int test_should_run_test(const char *suite, const char *name, test_category_t category);
void test_format_error(char *buffer, size_t buffer_size, const char *format, ...);

/* Test macros */
/* Tests must be registered explicitly in main() using REGISTER_TEST macro */
#define TEST(suite, name)                                                      \
    static void test_##suite##_##name(void)

#define TEST_WITH_SETUP(suite, name, setup_func)                               \
    static void test_##suite##_##name(void)

#define TEST_WITH_TEARDOWN(suite, name, teardown_func)                          \
    static void test_##suite##_##name(void)

#define TEST_WITH_FIXTURE(suite, name, setup_func, teardown_func)              \
    static void test_##suite##_##name(void)

#define TEST_CATEGORIZED(suite, name, category)                                \
    static void test_##suite##_##name(void)

#define TEST_WITH_TIMEOUT(suite, name, timeout)                                \
    static void test_##suite##_##name(void)

/* Test registration macros for use in main() */
#define REGISTER_TEST(suite, name) \
    test_register(#suite, #name, test_##suite##_##name)

#define REGISTER_TEST_WITH_SETUP(suite, name, setup_func) \
    test_register_with_features(#suite, #name, test_##suite##_##name, \
                               setup_func, NULL, TEST_CATEGORY_UNIT, \
                               g_test_config.default_timeout)

#define REGISTER_TEST_WITH_TEARDOWN(suite, name, teardown_func) \
    test_register_with_features(#suite, #name, test_##suite##_##name, \
                               NULL, teardown_func, TEST_CATEGORY_UNIT, \
                               g_test_config.default_timeout)

#define REGISTER_TEST_WITH_FIXTURE(suite, name, setup_func, teardown_func) \
    test_register_with_features(#suite, #name, test_##suite##_##name, \
                               setup_func, teardown_func, TEST_CATEGORY_UNIT, \
                               g_test_config.default_timeout)

#define REGISTER_TEST_CATEGORIZED(suite, name, category) \
    test_register_with_features(#suite, #name, test_##suite##_##name, \
                               NULL, NULL, category, \
                               g_test_config.default_timeout)

#define REGISTER_TEST_WITH_TIMEOUT(suite, name, timeout) \
    test_register_with_features(#suite, #name, test_##suite##_##name, \
                               NULL, NULL, TEST_CATEGORY_UNIT, timeout)

/* Standard main() template for tests */
#define TEST_MAIN_BEGIN() \
    int main(void) { \
        test_framework_init();

#define TEST_MAIN_END() \
        return test_framework_run(); \
    }

/* Skip macros. Use inside a TEST body or a setup function. */
#define TEST_SKIP(reason)                                                      \
    do {                                                                       \
        test_mark_skipped((reason), __FILE__, __LINE__);                       \
        return;                                                                 \
    } while (0)

/* Skip the current test when an absolute or cwd-relative file is missing. */
#define TEST_REQUIRE_FILE(path)                                                \
    do {                                                                       \
        const char *_req_path = (path);                                        \
        if (!test_file_exists(_req_path)) {                                    \
            char _msg[512];                                                    \
            test_format_error(_msg, sizeof(_msg),                              \
                             "Missing fixture: %s", _req_path);                 \
            test_mark_skipped(_msg, __FILE__, __LINE__);                       \
            return;                                                            \
        }                                                                      \
    } while (0)

/* Skip the current test when a file under the data directory is missing. */
#define TEST_REQUIRE_FIXTURE(name) TEST_REQUIRE_FILE(NMO_TEST_DATA_FILE(name))

/*
 * Assertion operands are evaluated exactly once. The comparison macros need
 * a type to hold each operand, so they rely on __typeof__, which GCC, Clang,
 * and MSVC 19.39+ (Visual Studio 2022 17.9) all provide in C17 mode.
 */
#if defined(__GNUC__) || defined(__clang__) || \
    (defined(_MSC_VER) && _MSC_VER >= 1939)
#define TEST_TYPEOF(expr) __typeof__(expr)
#else
#error "test_framework.h needs __typeof__ for single-evaluation assertions"
#endif
/*
 * The type both operands of a comparison convert to: arrays and functions
 * decay to pointers, integer operands get the usual arithmetic conversions,
 * and a NULL operand takes the other side's pointer type. Nothing here is
 * evaluated.
 */
#define TEST_COMMON_TYPE(a, b) TEST_TYPEOF(1 ? (a) : (b))

/* Basic assertion macros - enhanced with better error messages
 * Convention: ASSERT_* takes (expected, actual) where applicable.
 * Every operand is evaluated once, so side effects in operands are safe.
 */
#define ASSERT_EQ(a, b)                                                       \
    do {                                                                       \
        TEST_COMMON_TYPE(a, b) test_lhs_ = (a);                                \
        TEST_COMMON_TYPE(a, b) test_rhs_ = (b);                                \
        if (!(test_lhs_ == test_rhs_)) {                                      \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " == " #b "\n"          \
                             "  Expected: %lld\n  Actual: %lld",              \
                             (long long)test_lhs_, (long long)test_rhs_);     \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            char _msg[512];                                                    \
            test_format_error(_msg, sizeof(_msg),                              \
                             "Assertion failed: " #condition);                  \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__);  \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_NE(a, b)                                                       \
    do {                                                                       \
        TEST_COMMON_TYPE(a, b) test_lhs_ = (a);                                \
        TEST_COMMON_TYPE(a, b) test_rhs_ = (b);                                \
        if (!(test_lhs_ != test_rhs_)) {                                      \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " != " #b "\n"          \
                             "  Left: %lld\n  Right: %lld",                  \
                             (long long)test_lhs_, (long long)test_rhs_);     \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                        \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #expr "\n"                 \
                             "  Expression evaluated to false");              \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(expr)                                                     \
    do {                                                                       \
        if ((expr)) {                                                         \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: !" #expr "\n"                \
                             "  Expression evaluated to true");               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr)                                                   \
    do {                                                                       \
        if ((ptr) == NULL) {                                                  \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #ptr " is not NULL\n"      \
                             "  Pointer is NULL");                            \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NULL(ptr)                                                       \
    do {                                                                       \
        const void *test_ptr_ = (const void *)(ptr);                           \
        if (test_ptr_ != NULL) {                                               \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #ptr " is NULL\n"          \
                             "  Pointer is not NULL: %p",                     \
                             (void*)test_ptr_);                               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

/* Enhanced assertion macros with detailed error messages */
#include <math.h>

static inline const char *test_safe_cstr(const char *str) {
    return str ? str : "(null)";
}

#define ASSERT_FLOAT_EQ(a, b, epsilon)                                         \
    do {                                                                       \
        double test_lhs_ = (double)(a);                                        \
        double test_rhs_ = (double)(b);                                        \
        double test_eps_ = (double)(epsilon);                                  \
        double _diff = fabs(test_lhs_ - test_rhs_);                            \
        if (_diff > test_eps_) {                                               \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Float assertion failed: %s ≈ %s (±%g)\n"       \
                             "  Expected: %g\n  Actual: %g\n  Diff: %g",      \
                             #a, #b, test_eps_,                               \
                             test_lhs_, test_rhs_, _diff);                    \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(s1, s2)                                                  \
    do {                                                                       \
        const char *_s1 = (s1);                                                \
        const char *_s2 = (s2);                                                \
        if (_s1 == NULL || _s2 == NULL || strcmp(_s1, _s2) != 0) {            \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "String assertion failed: %s == %s\n"            \
                             "  Expected: \"%s\"\n  Actual: \"%s\"",         \
                             #s1, #s2,                                        \
                             test_safe_cstr(_s1), test_safe_cstr(_s2));       \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                                  \
    do {                                                                       \
        const char *_haystack = (haystack);                                   \
        const char *_needle = (needle);                                       \
        if (_haystack == NULL || strstr(_haystack, _needle) == NULL) {       \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "String contains assertion failed: '%s' should contain '%s'\n" \
                             "  Actual: %s",                                  \
                             #haystack, #needle,                              \
                             test_safe_cstr(_haystack));                      \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_LT(a, b)                                                       \
    do {                                                                       \
        TEST_COMMON_TYPE(a, b) test_lhs_ = (a);                                \
        TEST_COMMON_TYPE(a, b) test_rhs_ = (b);                                \
        if (!(test_lhs_ < test_rhs_)) {                                       \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " < " #b "\n"           \
                             "  Expected: %lld < %lld\n"                      \
                             "  But got: %lld >= %lld",                       \
                             (long long)test_lhs_, (long long)test_rhs_,      \
                             (long long)test_lhs_, (long long)test_rhs_);     \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_LE(a, b)                                                       \
    do {                                                                       \
        TEST_COMMON_TYPE(a, b) test_lhs_ = (a);                                \
        TEST_COMMON_TYPE(a, b) test_rhs_ = (b);                                \
        if (!(test_lhs_ <= test_rhs_)) {                                      \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " <= " #b "\n"           \
                             "  Expected: %lld <= %lld\n"                      \
                             "  But got: %lld > %lld",                       \
                             (long long)test_lhs_, (long long)test_rhs_,      \
                             (long long)test_lhs_, (long long)test_rhs_);     \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_GT(a, b)                                                       \
    do {                                                                       \
        TEST_COMMON_TYPE(a, b) test_lhs_ = (a);                                \
        TEST_COMMON_TYPE(a, b) test_rhs_ = (b);                                \
        if (!(test_lhs_ > test_rhs_)) {                                       \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " > " #b "\n"           \
                             "  Expected: %lld > %lld\n"                      \
                             "  But got: %lld <= %lld",                       \
                             (long long)test_lhs_, (long long)test_rhs_,      \
                             (long long)test_lhs_, (long long)test_rhs_);     \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_GE(a, b)                                                       \
    do {                                                                       \
        TEST_COMMON_TYPE(a, b) test_lhs_ = (a);                                \
        TEST_COMMON_TYPE(a, b) test_rhs_ = (b);                                \
        if (!(test_lhs_ >= test_rhs_)) {                                      \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " >= " #b "\n"           \
                             "  Expected: %lld >= %lld\n"                      \
                             "  But got: %lld < %lld",                       \
                             (long long)test_lhs_, (long long)test_rhs_,      \
                             (long long)test_lhs_, (long long)test_rhs_);     \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

/* New assertion types for enhanced testing */
#define ASSERT_MEM_EQ(ptr1, ptr2, size)                                        \
    do {                                                                       \
        const void *test_p1_ = (const void *)(ptr1);                           \
        const void *test_p2_ = (const void *)(ptr2);                           \
        size_t test_size_ = (size_t)(size);                                    \
        if (test_p1_ == NULL || test_p2_ == NULL) {                            \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: NULL pointer\n"       \
                             "  ptr1: %p\n  ptr2: %p",                        \
                             (void*)test_p1_, (void*)test_p2_);               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
        if (memcmp(test_p1_, test_p2_, test_size_) != 0) {                      \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: " #ptr1 " == " #ptr2 "\n" \
                             "  Size: %zu bytes\n  ptr1: %p\n  ptr2: %p",     \
                             test_size_, (void*)test_p1_, (void*)test_p2_);   \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_MEM_NE(ptr1, ptr2, size)                                        \
    do {                                                                       \
        const void *test_p1_ = (const void *)(ptr1);                           \
        const void *test_p2_ = (const void *)(ptr2);                           \
        size_t test_size_ = (size_t)(size);                                    \
        if (test_p1_ == NULL || test_p2_ == NULL) {                            \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: NULL pointer\n"       \
                             "  ptr1: %p\n  ptr2: %p",                        \
                             (void*)test_p1_, (void*)test_p2_);               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
        if (memcmp(test_p1_, test_p2_, test_size_) == 0) {                      \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: " #ptr1 " != " #ptr2 "\n" \
                             "  Size: %zu bytes\n  ptr1: %p\n  ptr2: %p",     \
                             test_size_, (void*)test_p1_, (void*)test_p2_);   \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_IN_RANGE(value, min, max)                                        \
    do {                                                                       \
        TEST_COMMON_TYPE(value, min) test_val_ = (value);                      \
        TEST_COMMON_TYPE(value, min) test_min_ = (min);                        \
        TEST_COMMON_TYPE(value, max) test_max_ = (max);                        \
        if (test_val_ < test_min_ || test_val_ > test_max_) {                  \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Range assertion failed: %s in [%s, %s]\n"       \
                             "  Value: %lld\n  Expected range: [%lld, %lld]", \
                             #value, #min, #max,                              \
                             (long long)test_val_, (long long)test_min_,      \
                             (long long)test_max_);                           \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_IN_RANGE_FLOAT(value, min, max, epsilon)                        \
    do {                                                                       \
        double _val = (double)(value);                                        \
        double _min = (double)(min);                                          \
        double _max = (double)(max);                                          \
        double _eps = (double)(epsilon);                                      \
        if (_val < (_min - _eps) || _val > (_max + _eps)) {                    \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Float range assertion failed: %s in [%s, %s] (±%g)\n" \
                             "  Value: %g\n  Expected range: [%g, %g]",       \
                             #value, #min, #max, _eps,                        \
                             _val, _min, _max);                               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_ARRAY_EQ(arr1, arr2, count)                                     \
    do {                                                                       \
        TEST_TYPEOF(&(arr1)[0]) test_a1_ = (arr1);                             \
        TEST_TYPEOF(&(arr2)[0]) test_a2_ = (arr2);                             \
        size_t test_count_ = (size_t)(count);                                  \
        if (test_a1_ == NULL || test_a2_ == NULL) {                            \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Array assertion failed: NULL array\n"         \
                             "  arr1: %p\n  arr2: %p",                       \
                             (void*)test_a1_, (void*)test_a2_);               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
        for (size_t _i = 0; _i < test_count_; _i++) {                          \
            if (test_a1_[_i] != test_a2_[_i]) {                                \
                char _msg[512];                                               \
                test_format_error(_msg, sizeof(_msg),                         \
                                 "Array assertion failed: " #arr1 " == " #arr2 "\n" \
                                 "  Size: %zu elements\n  First mismatch at index %zu\n" \
                                 "  Expected[%zu] = %lld\n  Actual[%zu] = %lld", \
                                 test_count_, _i, _i,                         \
                                 (long long)test_a2_[_i], _i,                 \
                                 (long long)test_a1_[_i]);                    \
                test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
                return;                                                        \
            }                                                                  \
        }                                                                      \
    } while (0)

#define ASSERT_FAIL(message)                                                   \
    do {                                                                       \
        char _msg[512];                                                       \
        test_format_error(_msg, sizeof(_msg),                                 \
                         "Expected failure: %s", message);                    \
        test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__);      \
        return;                                                                \
    } while (0)

/* Test isolation macros */
#define TEST_ISOLATE_BEGIN()                                                   \
    do {                                                                       \
        /* Save current state if needed */                                     \
    } while (0)

#define TEST_ISOLATE_END()                                                     \
    do {                                                                       \
        /* Restore state if needed */                                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* TEST_FRAMEWORK_H */
