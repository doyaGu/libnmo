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

/* Basic assertion macros - enhanced with better error messages */
#define ASSERT_EQ(a, b)                                                        \
    do {                                                                       \
        if (!((a) == (b))) {                                                  \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " == " #b "\n"          \
                             "  Expected: %lld\n  Actual: %lld",              \
                             (long long)(b), (long long)(a));               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NE(a, b)                                                        \
    do {                                                                       \
        if (!((a) != (b))) {                                                  \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " != " #b "\n"          \
                             "  Both values: %lld",                            \
                             (long long)(a));                                 \
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
        if ((ptr) != NULL) {                                                  \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #ptr " is NULL\n"          \
                             "  Pointer is not NULL: %p",                     \
                             (void*)(ptr));                                   \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

/* Enhanced assertion macros with detailed error messages */
#include <math.h>

#define ASSERT_FLOAT_EQ(a, b, epsilon)                                         \
    do {                                                                       \
        double _diff = fabs((double)(a) - (double)(b));                       \
        if (_diff > (double)(epsilon)) {                                      \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Float assertion failed: %s ≈ %s (±%g)\n"       \
                             "  Expected: %g\n  Actual: %g\n  Diff: %g",      \
                             #a, #b, (double)(epsilon),                      \
                             (double)(b), (double)(a), _diff);               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(s1, s2)                                                  \
    do {                                                                       \
        if ((s1) == NULL || (s2) == NULL || strcmp((s1), (s2)) != 0) {       \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "String assertion failed: %s == %s\n"            \
                             "  Expected: \"%s\"\n  Actual: \"%s\"",         \
                             #s1, #s2,                                        \
                             (s2) ? (s2) : "(null)", (s1) ? (s1) : "(null)"); \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                                  \
    do {                                                                       \
        if ((haystack) == NULL || strstr((haystack), (needle)) == NULL) {     \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "String contains assertion failed: '%s' should contain '%s'\n" \
                             "  Actual: %s",                                  \
                             #haystack, #needle,                              \
                             (haystack) ? (haystack) : "(null)");             \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_LT(a, b)                                                        \
    do {                                                                       \
        if (!((a) < (b))) {                                                   \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " < " #b "\n"           \
                             "  Expected: %lld < %lld\n"                      \
                             "  But got: %lld >= %lld",                       \
                             (long long)(a), (long long)(b),                  \
                             (long long)(a), (long long)(b));                \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_LE(a, b)                                                        \
    do {                                                                       \
        if (!((a) <= (b))) {                                                  \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " <= " #b "\n"          \
                             "  Expected: %lld <= %lld\n"                     \
                             "  But got: %lld > %lld",                        \
                             (long long)(a), (long long)(b),                  \
                             (long long)(a), (long long)(b));                \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_GT(a, b)                                                        \
    do {                                                                       \
        if (!((a) > (b))) {                                                   \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " > " #b "\n"           \
                             "  Expected: %lld > %lld\n"                      \
                             "  But got: %lld <= %lld",                       \
                             (long long)(a), (long long)(b),                  \
                             (long long)(a), (long long)(b));                \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_GE(a, b)                                                        \
    do {                                                                       \
        if (!((a) >= (b))) {                                                  \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Assertion failed: " #a " >= " #b "\n"          \
                             "  Expected: %lld >= %lld\n"                     \
                             "  But got: %lld < %lld",                        \
                             (long long)(a), (long long)(b),                  \
                             (long long)(a), (long long)(b));                \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

/* New assertion types for enhanced testing */
#define ASSERT_MEM_EQ(ptr1, ptr2, size)                                        \
    do {                                                                       \
        if ((ptr1) == NULL || (ptr2) == NULL) {                              \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: NULL pointer\n"       \
                             "  ptr1: %p\n  ptr2: %p",                        \
                             (void*)(ptr1), (void*)(ptr2));                  \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
        if (memcmp((ptr1), (ptr2), (size)) != 0) {                            \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: " #ptr1 " == " #ptr2 "\n" \
                             "  Size: %zu bytes\n  ptr1: %p\n  ptr2: %p",     \
                             (size_t)(size), (void*)(ptr1), (void*)(ptr2));  \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_MEM_NE(ptr1, ptr2, size)                                        \
    do {                                                                       \
        if ((ptr1) == NULL || (ptr2) == NULL) {                              \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: NULL pointer\n"       \
                             "  ptr1: %p\n  ptr2: %p",                        \
                             (void*)(ptr1), (void*)(ptr2));                  \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
        if (memcmp((ptr1), (ptr2), (size)) == 0) {                            \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Memory assertion failed: " #ptr1 " != " #ptr2 "\n" \
                             "  Size: %zu bytes\n  ptr1: %p\n  ptr2: %p",     \
                             (size_t)(size), (void*)(ptr1), (void*)(ptr2));  \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_IN_RANGE(value, min, max)                                        \
    do {                                                                       \
        if ((value) < (min) || (value) > (max)) {                            \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Range assertion failed: %s in [%s, %s]\n"       \
                             "  Value: %lld\n  Expected range: [%lld, %lld]", \
                             #value, #min, #max,                              \
                             (long long)(value), (long long)(min), (long long)(max)); \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_IN_RANGE_FLOAT(value, min, max, epsilon)                        \
    do {                                                                       \
        double _val = (double)(value);                                        \
        double _min = (double)(min);                                          \
        double _max = (double)(max);                                          \
        if (_val < (_min - (double)(epsilon)) || _val > (_max + (double)(epsilon))) { \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Float range assertion failed: %s in [%s, %s] (±%g)\n" \
                             "  Value: %g\n  Expected range: [%g, %g]",       \
                             #value, #min, #max, (double)(epsilon),           \
                             _val, _min, _max);                               \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_ARRAY_EQ(arr1, arr2, count)                                     \
    do {                                                                       \
        if ((arr1) == NULL || (arr2) == NULL) {                              \
            char _msg[512];                                                   \
            test_format_error(_msg, sizeof(_msg),                             \
                             "Array assertion failed: NULL array\n"         \
                             "  arr1: %p\n  arr2: %p",                       \
                             (void*)(arr1), (void*)(arr2));                  \
            test_add_result(__func__, __func__, 0, _msg, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
        for (size_t _i = 0; _i < (size_t)(count); _i++) {                    \
            if ((arr1)[_i] != (arr2)[_i]) {                                  \
                char _msg[512];                                               \
                test_format_error(_msg, sizeof(_msg),                         \
                                 "Array assertion failed: " #arr1 " == " #arr2 "\n" \
                                 "  Size: %zu elements\n  First mismatch at index %zu\n" \
                                 "  Expected[%zu] = %lld\n  Actual[%zu] = %lld", \
                                 (size_t)(count), _i, _i,                    \
                                 (long long)(arr2)[_i], _i, (long long)(arr1)[_i]); \
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
