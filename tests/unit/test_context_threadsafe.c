/**
 * @file test_context_threadsafe.c
 * @brief Thread-safety tests for context reference counting
 */

#include "../test_framework.h"
#include "app/nmo_context.h"
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    typedef HANDLE thread_t;
    typedef unsigned (__stdcall *thread_func_t)(void*);
    #define thread_create(handle, func, arg) \
        (*(handle) = (HANDLE)_beginthreadex(NULL, 0, (thread_func_t)(func), (arg), 0, NULL))
    #define thread_join(handle) (WaitForSingleObject((handle), INFINITE), CloseHandle((handle)))
#else
    #include <pthread.h>
    typedef pthread_t thread_t;
    typedef void* (*thread_func_t)(void*);
    #define thread_create(handle, func, arg) pthread_create((handle), NULL, (func), (arg))
    #define thread_join(handle) pthread_join((handle), NULL)
#endif

typedef struct {
    nmo_context_t *ctx;
    int iterations;
} thread_data_t;

#ifdef _WIN32
unsigned __stdcall
#else
void*
#endif
retain_release_thread(void *arg) {
    thread_data_t *data = (thread_data_t*)arg;
    
    for (int i = 0; i < data->iterations; i++) {
        nmo_context_retain(data->ctx);
        /* Simulate some work */
        for (volatile int j = 0; j < 100; j++);
        nmo_context_release(data->ctx);
    }
    
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

TEST(context_threadsafe, multiple_threads_retain_release) {
    const int num_threads = 4;
    const int iterations = 1000;
    
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    thread_t threads[4];
    thread_data_t thread_data[4];
    
    /* Create threads */
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].ctx = ctx;
        thread_data[i].iterations = iterations;
        thread_create(&threads[i], retain_release_thread, &thread_data[i]);
    }
    
    /* Wait for all threads to complete */
    for (int i = 0; i < num_threads; i++) {
        thread_join(threads[i]);
    }
    
    /* Context should still be valid (refcount = 1) */
    nmo_context_release(ctx);
    /* If we get here without crashing, the test passed */
}

TEST(context_threadsafe, stress_test) {
    const int num_threads = 8;
    const int iterations = 10000;
    
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    thread_t threads[8];
    thread_data_t thread_data[8];
    
    /* Create threads */
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].ctx = ctx;
        thread_data[i].iterations = iterations;
        thread_create(&threads[i], retain_release_thread, &thread_data[i]);
    }
    
    /* Wait for all threads to complete */
    for (int i = 0; i < num_threads; i++) {
        thread_join(threads[i]);
    }
    
    /* Context should still be valid (refcount = 1) */
    nmo_context_release(ctx);
    /* If we get here without crashing, the test passed */
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(context_threadsafe, multiple_threads_retain_release);
    REGISTER_TEST(context_threadsafe, stress_test);
TEST_MAIN_END()
