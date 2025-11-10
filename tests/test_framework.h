/**
 * @file test_framework.h
 * @brief Simple test framework for libnmo
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *suite_name;
    const char *test_name;
    int passed;
    int failed;
    const char *failure_message;
    const char *failure_file;
    int failure_line;
} test_result;

typedef struct {
    test_result *results;
    int count;
    int capacity;
} test_suite;

extern test_suite *g_test_suite;

void test_framework_init(void);
void test_framework_cleanup(void);
int test_framework_run(void);
void test_add_result(const char *suite, const char *name, int passed,
                     const char *message, const char *file, int line);

#define TEST(suite, name)                                                      \
    static void test_##suite##_##name(void);                                  \
    static void __attribute__((constructor)) register_##suite##_##name(void) { \
        extern void test_framework_init(void);                                \
        test_framework_init();                                                 \
    }                                                                          \
    static void test_##suite##_##name(void)

#define ASSERT_EQ(a, b)                                                        \
    do {                                                                       \
        if (!((a) == (b))) {                                                  \
            test_add_result(__func__, __FUNCTION__, 0,                        \
                            "Assertion failed: " #a " == " #b, __FILE__,      \
                            __LINE__);                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NE(a, b)                                                        \
    do {                                                                       \
        if (!((a) != (b))) {                                                  \
            test_add_result(__func__, __FUNCTION__, 0,                        \
                            "Assertion failed: " #a " != " #b, __FILE__,      \
                            __LINE__);                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                        \
            test_add_result(__func__, __FUNCTION__, 0,                        \
                            "Assertion failed: " #expr, __FILE__, __LINE__);  \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(expr)                                                     \
    do {                                                                       \
        if ((expr)) {                                                         \
            test_add_result(__func__, __FUNCTION__, 0,                        \
                            "Assertion failed: !" #expr, __FILE__, __LINE__); \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr)                                                   \
    do {                                                                       \
        if ((ptr) == NULL) {                                                  \
            test_add_result(__func__, __FUNCTION__, 0,                        \
                            "Assertion failed: " #ptr " is not NULL",         \
                            __FILE__, __LINE__);                              \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NULL(ptr)                                                       \
    do {                                                                       \
        if ((ptr) != NULL) {                                                  \
            test_add_result(__func__, __FUNCTION__, 0,                        \
                            "Assertion failed: " #ptr " is NULL", __FILE__,   \
                            __LINE__);                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif // TEST_FRAMEWORK_H
