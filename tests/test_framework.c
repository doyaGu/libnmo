/**
 * @file test_framework.c
 * @brief Modern test framework implementation for libnmo with enhanced features
 * 
 * This implementation provides:
 * - Portable test registration using static initialization
 * - Setup/teardown mechanisms for test fixtures
 * - Enhanced assertion types with detailed error messages
 * - Test categorization and filtering
 * - Timeout handling with platform-specific implementations
 * - Test isolation mechanisms
 * - Comprehensive error reporting
 */

#include "test_framework.h"

/* Global variables */
test_suite *g_test_suite = NULL;
test_entry_t *g_test_registry = NULL;
test_config_t g_test_config = {
    .verbose = 0,
    .stop_on_failure = 0,
    .filter_categories = TEST_CATEGORY_ALL,
    .filter_suite = NULL,
    .filter_test = NULL,
    .default_timeout = 30.0
};

/* Internal state tracking */
static int g_test_count = 0;
static int g_pass_count = 0;
static int g_fail_count = 0;
static int g_skip_count = 0;
static double g_total_time = 0.0;

/**
 * Duplicate an error message so assertion buffers remain valid.
 *
 * The assertion macros build messages on the stack, so we must make a copy
 * that outlives the macro invocation and free it during cleanup.
 */
static char *test_duplicate_message(const char *message) {
    if (message == NULL) {
        return NULL;
    }

    size_t len = strlen(message);
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, message, len + 1);
    return copy;
}

/**
 * Get current time in milliseconds (platform-specific implementation)
 */
double test_get_time_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / frequency.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
#endif
}

/**
 * Format error message with variable arguments
 */
void test_format_error(char *buffer, size_t buffer_size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
}

/**
 * Check if a test should run based on current configuration
 */
int test_should_run_test(const char *suite, const char *name, test_category_t category) {
    /* Check category filter */
    if (!(g_test_config.filter_categories & category)) {
        return 0;
    }
    
    /* Check suite filter */
    if (g_test_config.filter_suite && strcmp(suite, g_test_config.filter_suite) != 0) {
        return 0;
    }
    
    /* Check specific test filter */
    if (g_test_config.filter_test && strcmp(name, g_test_config.filter_test) != 0) {
        return 0;
    }
    
    return 1;
}

/**
 * Initialize the test framework
 */
void test_framework_init(void) {
    if (g_test_suite == NULL) {
        g_test_suite = (test_suite *)malloc(sizeof(test_suite));
        if (!g_test_suite) {
            fprintf(stderr, "Failed to allocate memory for test suite\n");
            exit(1);
        }
        
        g_test_suite->capacity = 256;
        g_test_suite->count = 0;
        g_test_suite->results = (test_result *)malloc(
            g_test_suite->capacity * sizeof(test_result));
        
        if (!g_test_suite->results) {
            fprintf(stderr, "Failed to allocate memory for test results\n");
            free(g_test_suite);
            g_test_suite = NULL;
            exit(1);
        }
    }
    
    /* Reset counters */
    g_test_count = 0;
    g_pass_count = 0;
    g_fail_count = 0;
    g_skip_count = 0;
    g_total_time = 0.0;
}

/**
 * Clean up the test framework
 */
void test_framework_cleanup(void) {
    if (g_test_suite != NULL) {
        if (g_test_suite->results != NULL) {
            for (int i = 0; i < g_test_suite->count; ++i) {
                if (g_test_suite->results[i].failure_message != NULL) {
                    free((void *)g_test_suite->results[i].failure_message);
                }
            }
            free(g_test_suite->results);
        }
        free(g_test_suite);
        g_test_suite = NULL;
    }
    
    /* Clean up test registry */
    test_entry_t *entry = g_test_registry;
    while (entry != NULL) {
        test_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }
    g_test_registry = NULL;
}

/**
 * Configure the test framework
 */
void test_framework_configure(test_config_t config) {
    g_test_config = config;
}

/**
 * Register a test with basic features (backward compatibility)
 */
void test_register(const char *suite, const char *name, test_func_t func) {
    test_register_with_features(suite, name, func, NULL, NULL, 
                               TEST_CATEGORY_UNIT, g_test_config.default_timeout);
}

/**
 * Register a test with enhanced features
 */
void test_register_with_features(const char *suite, const char *name, 
                                 test_func_t func, setup_func_t setup, 
                                 teardown_func_t teardown, 
                                 test_category_t category, 
                                 double timeout_seconds) {
    test_entry_t *entry = (test_entry_t *)malloc(sizeof(test_entry_t));
    if (!entry) {
        fprintf(stderr, "Failed to allocate memory for test entry\n");
        return;
    }
    
    entry->suite_name = suite;
    entry->test_name = name;
    entry->func = func;
    entry->setup = setup;
    entry->teardown = teardown;
    entry->category = category;
    entry->timeout_seconds = timeout_seconds;
    entry->enabled = 1;
    entry->next = g_test_registry;
    g_test_registry = entry;
}

/**
 * Add a test result (backward compatibility)
 */
void test_add_result(const char *suite, const char *name, int passed,
                     const char *message, const char *file, int line) {
    test_add_result_with_time(suite, name, passed, message, file, line, 0.0);
}

/**
 * Add a test result with execution time
 */
void test_add_result_with_time(const char *suite, const char *name, int passed,
                               const char *message, const char *file, int line,
                               double execution_time_ms) {
    if (g_test_suite == NULL) {
        test_framework_init();
    }

    if (g_test_suite->count >= g_test_suite->capacity) {
        g_test_suite->capacity *= 2;
        test_result *new_results = (test_result *)realloc(
            g_test_suite->results, g_test_suite->capacity * sizeof(test_result));
        
        if (!new_results) {
            fprintf(stderr, "Failed to reallocate memory for test results\n");
            return;
        }
        
        g_test_suite->results = new_results;
    }

    test_result *result = &g_test_suite->results[g_test_suite->count++];
    result->suite_name = suite;
    result->test_name = name;
    result->passed = passed;
    result->failed = !passed;
    result->failure_message = test_duplicate_message(message);
    if (message != NULL && result->failure_message == NULL) {
        result->failure_message = "Failed to record failure message (out of memory)";
    }
    result->failure_file = file;
    result->failure_line = line;
    result->execution_time_ms = execution_time_ms;

    /* Determine category from registry if possible */
    result->category = "unit";
    test_entry_t *entry = g_test_registry;
    while (entry != NULL) {
        if (strcmp(entry->suite_name, suite) == 0 && 
            strcmp(entry->test_name, name) == 0) {
            switch (entry->category) {
                case TEST_CATEGORY_UNIT: result->category = "unit"; break;
                case TEST_CATEGORY_INTEGRATION: result->category = "integration"; break;
                case TEST_CATEGORY_PERFORMANCE: result->category = "performance"; break;
                case TEST_CATEGORY_STRESS: result->category = "stress"; break;
                case TEST_CATEGORY_REGRESSION: result->category = "regression"; break;
                default: result->category = "unknown"; break;
            }
            break;
        }
        entry = entry->next;
    }

    if (passed) {
        g_pass_count++;
    } else {
        g_fail_count++;
    }
    
    g_total_time += execution_time_ms;
}

/**
 * Run a single test with timeout and fixture support
 */
static int run_single_test(test_entry_t *entry) {
    double start_time = test_get_time_ms();
    int test_passed = 1;
    
    if (g_test_config.verbose) {
        printf("[RUN ] %s::%s (%s, timeout: %.1fs)\n", 
               entry->suite_name, entry->test_name, 
               (entry->category == TEST_CATEGORY_UNIT) ? "unit" :
               (entry->category == TEST_CATEGORY_INTEGRATION) ? "integration" :
               (entry->category == TEST_CATEGORY_PERFORMANCE) ? "performance" :
               (entry->category == TEST_CATEGORY_STRESS) ? "stress" :
               (entry->category == TEST_CATEGORY_REGRESSION) ? "regression" : "unknown",
               entry->timeout_seconds);
    } else {
        printf("[RUN ] %s::%s\n", entry->suite_name, entry->test_name);
    }
    
    /* Reset counters for this test */
    int before_count = g_test_suite->count;
    
    /* Run setup function if provided */
    if (entry->setup) {
        if (g_test_config.verbose) {
            printf("      Running setup...\n");
        }
        entry->setup();
    }
    
    /* Execute test with timeout handling */
    /* Note: Simple timeout implementation - in a real scenario, you might want
     * to use threads or signals for more robust timeout handling */
    double test_start = test_get_time_ms();
    
    /* Test isolation - begin */
    TEST_ISOLATE_BEGIN();
    
    /* Execute the test function */
    entry->func();
    
    /* Test isolation - end */
    TEST_ISOLATE_END();
    
    double test_end = test_get_time_ms();
    double test_duration = test_end - test_start;
    
    /* Check if test timed out */
    if (test_duration > entry->timeout_seconds * 1000.0) {
        char timeout_msg[512];
        test_format_error(timeout_msg, sizeof(timeout_msg),
                         "Test timed out after %.1fs (limit: %.1fs)",
                         test_duration / 1000.0, entry->timeout_seconds);
        test_add_result_with_time(entry->suite_name, entry->test_name, 0,
                                 timeout_msg, __FILE__, __LINE__, test_duration);
        test_passed = 0;
    }
    
    /* Run teardown function if provided */
    if (entry->teardown) {
        if (g_test_config.verbose) {
            printf("      Running teardown...\n");
        }
        entry->teardown();
    }
    
    /* Check if test passed (no failures recorded) */
    if (test_passed && g_test_suite->count == before_count) {
        /* No assertion failed, test passed */
        double end_time = test_get_time_ms();
        double execution_time = end_time - start_time;
        test_add_result_with_time(entry->suite_name, entry->test_name, 1, 
                                 NULL, NULL, 0, execution_time);
        printf("[PASS] %s::%s (%.2fms)\n", entry->suite_name, 
               entry->test_name, execution_time);
    } else if (test_passed && g_test_suite->count > before_count) {
        /* Assertion failed - a result was added */
        test_result *result = &g_test_suite->results[g_test_suite->count - 1];
        printf("[FAIL] %s::%s\n", entry->suite_name, entry->test_name);
        if (result->failure_message) {
            printf("       %s\n", result->failure_message);
        }
        if (result->failure_file) {
            printf("       at %s:%d\n", result->failure_file, result->failure_line);
        }
        printf("       (%.2fms)\n", result->execution_time_ms);
        test_passed = 0;  /* Mark test as failed */
    }
    
    return test_passed;
}

/**
 * Run all registered tests
 */
int test_framework_run(void) {
    if (g_test_registry == NULL) {
        printf("No tests registered\n");
        return 0;
    }
    
    test_framework_init();
    
    /* Manually register all tests since static initialization approach doesn't work */
    /* This is a workaround for the C compiler limitation with function calls in static initializers */
    
    /* Count tests that should run */
    int total_tests = 0;
    int enabled_tests = 0;
    test_entry_t *entry = g_test_registry;
    while (entry != NULL) {
        total_tests++;
        if (entry->enabled && test_should_run_test(entry->suite_name, 
                                                   entry->test_name, 
                                                   entry->category)) {
            enabled_tests++;
        }
        entry = entry->next;
    }
    
    printf("\n========================================\n");
    printf("Running %d test(s) out of %d registered\n", enabled_tests, total_tests);
    if (g_test_config.filter_suite) {
        printf("Suite filter: %s\n", g_test_config.filter_suite);
    }
    if (g_test_config.filter_test) {
        printf("Test filter: %s\n", g_test_config.filter_test);
    }
    printf("========================================\n\n");
    
    /* Run all enabled and filtered tests */
    entry = g_test_registry;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    
    while (entry != NULL) {
        if (!entry->enabled) {
            skipped++;
            if (g_test_config.verbose) {
                printf("[SKIP] %s::%s (disabled)\n", entry->suite_name, entry->test_name);
            }
        } else if (!test_should_run_test(entry->suite_name, 
                                         entry->test_name, 
                                         entry->category)) {
            skipped++;
            if (g_test_config.verbose) {
                printf("[SKIP] %s::%s (filtered)\n", entry->suite_name, entry->test_name);
            }
        } else {
            g_test_count++;
            int test_passed = run_single_test(entry);
            
            if (test_passed) {
                passed++;
            } else {
                failed++;
                if (g_test_config.stop_on_failure) {
                    printf("\nStopping test execution due to failure\n");
                    break;
                }
            }
        }
        
        entry = entry->next;
        printf("\n");
    }

    /* Print summary */
    printf("========================================\n");
    printf("Test Results Summary:\n");
    printf("  Total: %d\n", g_test_count);
    printf("  Passed: %d\n", passed);
    printf("  Failed: %d\n", failed);
    printf("  Skipped: %d\n", skipped);
    printf("  Total time: %.2fms\n", g_total_time);
    if (g_test_count > 0) {
        printf("  Average time: %.2fms\n", g_total_time / g_test_count);
    }
    printf("========================================\n\n");

    test_framework_cleanup();
    return failed == 0 ? 0 : 1;
}
