/**
 * @file test_framework.c
 * @brief Simple test framework implementation for libnmo
 */

#include "test_framework.h"

test_suite *g_test_suite = NULL;
int g_test_count = 0;
int g_pass_count = 0;
int g_fail_count = 0;

void test_framework_init(void) {
    if (g_test_suite == NULL) {
        g_test_suite = (test_suite *)malloc(sizeof(test_suite));
        g_test_suite->capacity = 256;
        g_test_suite->count = 0;
        g_test_suite->results =
            (test_result *)malloc(g_test_suite->capacity * sizeof(test_result));
    }
}

void test_framework_cleanup(void) {
    if (g_test_suite != NULL) {
        if (g_test_suite->results != NULL) {
            free(g_test_suite->results);
        }
        free(g_test_suite);
        g_test_suite = NULL;
    }
}

void test_add_result(const char *suite, const char *name, int passed,
                     const char *message, const char *file, int line) {
    if (g_test_suite == NULL) {
        test_framework_init();
    }

    if (g_test_suite->count >= g_test_suite->capacity) {
        g_test_suite->capacity *= 2;
        g_test_suite->results = (test_result *)realloc(
            g_test_suite->results, g_test_suite->capacity * sizeof(test_result));
    }

    test_result *result = &g_test_suite->results[g_test_suite->count++];
    result->suite_name = suite;
    result->test_name = name;
    result->passed = passed;
    result->failure_message = message;
    result->failure_file = file;
    result->failure_line = line;

    if (passed) {
        g_pass_count++;
    } else {
        g_fail_count++;
    }
}

int test_framework_run(void) {
    if (g_test_suite == NULL || g_test_suite->count == 0) {
        printf("No tests registered\n");
        return 0;
    }

    printf("\n========================================\n");
    printf("Test Results\n");
    printf("========================================\n");

    for (int i = 0; i < g_test_suite->count; i++) {
        test_result *result = &g_test_suite->results[i];
        if (result->passed) {
            printf("[PASS] %s::%s\n", result->suite_name, result->test_name);
        } else {
            printf("[FAIL] %s::%s\n", result->suite_name, result->test_name);
            if (result->failure_message) {
                printf("  Message: %s\n", result->failure_message);
            }
            if (result->failure_file) {
                printf("  Location: %s:%d\n", result->failure_file,
                       result->failure_line);
            }
        }
    }

    printf("========================================\n");
    printf("Total: %d, Passed: %d, Failed: %d\n", g_pass_count + g_fail_count,
           g_pass_count, g_fail_count);
    printf("========================================\n\n");

    return g_fail_count == 0 ? 0 : 1;
}
