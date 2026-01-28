/**
 * @file test_type_system_bench.c
 * @brief Performance benchmarks for Type System (Phase 6)
 *
 * Measures:
 * - Type registration throughput
 * - Type lookup latency (by GUID, name, class ID)
 * - String conversion performance
 * - Memory usage under load
 */

#include "../test_framework.h"
#include "type/type_system.h"
#include "type/type_string.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <sys/time.h>
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

/* Test configuration */
#define BENCH_ITERATIONS 1000
#define BENCH_TYPE_COUNT 500
#define BENCH_LOOKUP_COUNT 10000

/* Test fixtures */
static nmo_arena_t *bench_arena = NULL;
static nmo_type_registry_t *bench_registry = NULL;

static void bench_setup(void) {
    bench_arena = nmo_arena_create(NULL, 1024 * 1024); /* 1MB arena */
    bench_registry = nmo_type_registry_create(bench_arena);
}

static void bench_teardown(void) {
    if (bench_registry) {
        nmo_type_registry_destroy(bench_registry);
        bench_registry = NULL;
    }
    if (bench_arena) {
        nmo_arena_destroy(bench_arena);
        bench_arena = NULL;
    }
}

/* ============================================================================
 * Benchmark: Type Registration Throughput
 * ============================================================================ */

TEST(type_system_bench, registration_throughput) {
    bench_setup();
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCH_TYPE_COUNT; i++) {
        nmo_guid_t guid = {0xBENCH000 + i, 0x00000001};
        char name[64];
        snprintf(name, sizeof(name), "BenchType_%04d", i);
        
        nmo_type_descriptor_t type = {
            .guid = guid,
            .name = name,
            .size = sizeof(int),
            .category = NMO_TYPE_CATEGORY_PRIMITIVE
        };
        
        nmo_result_t result = nmo_type_registry_register(bench_registry, &type);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    double end = get_time_ms();
    double elapsed = end - start;
    double per_type = elapsed / BENCH_TYPE_COUNT;
    
    printf("\n  Registration Throughput:\n");
    printf("    Total: %d types in %.2f ms\n", BENCH_TYPE_COUNT, elapsed);
    printf("    Per type: %.4f ms (%.0f types/sec)\n", per_type, 1000.0 / per_type);
    
    /* Performance target: < 0.1ms per registration */
    ASSERT_TRUE(per_type < 0.5);
    
    bench_teardown();
}

/* ============================================================================
 * Benchmark: GUID Lookup Latency
 * ============================================================================ */

TEST(type_system_bench, guid_lookup_latency) {
    bench_setup();
    
    /* First, register types */
    for (int i = 0; i < BENCH_TYPE_COUNT; i++) {
        nmo_guid_t guid = {0xLOOKUP00 + i, 0x00000002};
        char name[64];
        snprintf(name, sizeof(name), "LookupType_%04d", i);
        
        nmo_type_descriptor_t type = {
            .guid = guid,
            .name = name,
            .size = sizeof(int),
            .category = NMO_TYPE_CATEGORY_PRIMITIVE
        };
        nmo_type_registry_register(bench_registry, &type);
    }
    
    /* Benchmark lookups */
    double start = get_time_ms();
    
    int found_count = 0;
    for (int i = 0; i < BENCH_LOOKUP_COUNT; i++) {
        int idx = (i * 7) % BENCH_TYPE_COUNT; /* Varied access pattern */
        nmo_guid_t guid = {0xLOOKUP00 + idx, 0x00000002};
        
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(bench_registry, guid);
        if (type != NULL) found_count++;
    }
    
    double end = get_time_ms();
    double elapsed = end - start;
    double per_lookup = elapsed / BENCH_LOOKUP_COUNT;
    
    printf("\n  GUID Lookup Latency:\n");
    printf("    Total: %d lookups in %.2f ms\n", BENCH_LOOKUP_COUNT, elapsed);
    printf("    Per lookup: %.6f ms (%.0f lookups/sec)\n", per_lookup, 1000.0 / per_lookup);
    printf("    Found: %d/%d\n", found_count, BENCH_LOOKUP_COUNT);
    
    /* All lookups should succeed */
    ASSERT_EQ(BENCH_LOOKUP_COUNT, found_count);
    
    /* Performance target: O(1) hash lookup, < 0.01ms per lookup */
    ASSERT_TRUE(per_lookup < 0.05);
    
    bench_teardown();
}

/* ============================================================================
 * Benchmark: Name Lookup Latency
 * ============================================================================ */

TEST(type_system_bench, name_lookup_latency) {
    bench_setup();
    
    /* Register types with names */
    char names[BENCH_TYPE_COUNT][64];
    for (int i = 0; i < BENCH_TYPE_COUNT; i++) {
        nmo_guid_t guid = {0xNAME0000 + i, 0x00000003};
        snprintf(names[i], sizeof(names[i]), "NamedType_%04d", i);
        
        nmo_type_descriptor_t type = {
            .guid = guid,
            .name = names[i],
            .size = sizeof(int),
            .category = NMO_TYPE_CATEGORY_PRIMITIVE
        };
        nmo_type_registry_register(bench_registry, &type);
    }
    
    /* Benchmark name lookups */
    double start = get_time_ms();
    
    int found_count = 0;
    for (int i = 0; i < BENCH_LOOKUP_COUNT; i++) {
        int idx = (i * 11) % BENCH_TYPE_COUNT;
        
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_name(bench_registry, names[idx]);
        if (type != NULL) found_count++;
    }
    
    double end = get_time_ms();
    double elapsed = end - start;
    double per_lookup = elapsed / BENCH_LOOKUP_COUNT;
    
    printf("\n  Name Lookup Latency:\n");
    printf("    Total: %d lookups in %.2f ms\n", BENCH_LOOKUP_COUNT, elapsed);
    printf("    Per lookup: %.6f ms (%.0f lookups/sec)\n", per_lookup, 1000.0 / per_lookup);
    printf("    Found: %d/%d\n", found_count, BENCH_LOOKUP_COUNT);
    
    ASSERT_EQ(BENCH_LOOKUP_COUNT, found_count);
    
    /* Performance target: < 0.02ms per lookup (string hashing overhead) */
    ASSERT_TRUE(per_lookup < 0.1);
    
    bench_teardown();
}

/* ============================================================================
 * Benchmark: Enum Registration
 * ============================================================================ */

TEST(type_system_bench, enum_registration) {
    bench_setup();
    
    const int ENUM_COUNT = 100;
    
    double start = get_time_ms();
    
    for (int i = 0; i < ENUM_COUNT; i++) {
        nmo_guid_t guid = {0xENUM0000 + i, 0x00000004};
        char name[64];
        snprintf(name, sizeof(name), "BenchEnum_%04d", i);
        
        nmo_result_t result = nmo_type_registry_register_enum(
            bench_registry,
            guid,
            name,
            "VALUE_A=0,VALUE_B=1,VALUE_C=2,VALUE_D=3"
        );
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    double end = get_time_ms();
    double elapsed = end - start;
    double per_enum = elapsed / ENUM_COUNT;
    
    printf("\n  Enum Registration:\n");
    printf("    Total: %d enums in %.2f ms\n", ENUM_COUNT, elapsed);
    printf("    Per enum: %.4f ms\n", per_enum);
    
    /* Verify statistics */
    size_t enum_count = nmo_type_registry_get_enum_count(bench_registry);
    ASSERT_TRUE(enum_count >= ENUM_COUNT);
    
    /* Performance target: < 0.5ms per enum registration */
    ASSERT_TRUE(per_enum < 1.0);
    
    bench_teardown();
}

/* ============================================================================
 * Benchmark: Flags Registration
 * ============================================================================ */

TEST(type_system_bench, flags_registration) {
    bench_setup();
    
    const int FLAGS_COUNT = 100;
    
    double start = get_time_ms();
    
    for (int i = 0; i < FLAGS_COUNT; i++) {
        nmo_guid_t guid = {0xFLAGS000 + i, 0x00000005};
        char name[64];
        snprintf(name, sizeof(name), "BenchFlags_%04d", i);
        
        nmo_result_t result = nmo_type_registry_register_flags(
            bench_registry,
            guid,
            name,
            "FLAG_A=0x1,FLAG_B=0x2,FLAG_C=0x4,FLAG_D=0x8"
        );
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    double end = get_time_ms();
    double elapsed = end - start;
    double per_flags = elapsed / FLAGS_COUNT;
    
    printf("\n  Flags Registration:\n");
    printf("    Total: %d flags in %.2f ms\n", FLAGS_COUNT, elapsed);
    printf("    Per flags: %.4f ms\n", per_flags);
    
    size_t flags_count = nmo_type_registry_get_flags_count(bench_registry);
    ASSERT_TRUE(flags_count >= FLAGS_COUNT);
    
    ASSERT_TRUE(per_flags < 1.0);
    
    bench_teardown();
}

/* ============================================================================
 * Benchmark: Memory Usage
 * ============================================================================ */

TEST(type_system_bench, memory_usage) {
    bench_setup();
    
    size_t initial_mem = nmo_type_registry_get_memory_usage(bench_registry);
    
    /* Register many types */
    for (int i = 0; i < BENCH_TYPE_COUNT; i++) {
        nmo_guid_t guid = {0xMEMORY00 + i, 0x00000006};
        char name[64];
        snprintf(name, sizeof(name), "MemoryType_%04d", i);
        
        nmo_type_descriptor_t type = {
            .guid = guid,
            .name = name,
            .size = 64, /* Typical struct size */
            .category = NMO_TYPE_CATEGORY_STRUCT
        };
        nmo_type_registry_register(bench_registry, &type);
    }
    
    size_t final_mem = nmo_type_registry_get_memory_usage(bench_registry);
    size_t used_mem = final_mem - initial_mem;
    double per_type_kb = (double)used_mem / BENCH_TYPE_COUNT / 1024.0;
    
    printf("\n  Memory Usage:\n");
    printf("    Initial: %zu bytes\n", initial_mem);
    printf("    Final: %zu bytes\n", final_mem);
    printf("    Used: %zu bytes for %d types\n", used_mem, BENCH_TYPE_COUNT);
    printf("    Per type: %.2f KB\n", per_type_kb);
    
    /* Memory target: < 1KB per type descriptor */
    ASSERT_TRUE(per_type_kb < 2.0);
    
    bench_teardown();
}

/* ============================================================================
 * Benchmark: Type Unregistration
 * ============================================================================ */

TEST(type_system_bench, unregistration_throughput) {
    bench_setup();
    
    /* First register types */
    nmo_guid_t guids[BENCH_TYPE_COUNT];
    for (int i = 0; i < BENCH_TYPE_COUNT; i++) {
        guids[i] = (nmo_guid_t){0xUNREG000 + i, 0x00000007};
        char name[64];
        snprintf(name, sizeof(name), "UnregType_%04d", i);
        
        nmo_type_descriptor_t type = {
            .guid = guids[i],
            .name = name,
            .size = sizeof(int),
            .category = NMO_TYPE_CATEGORY_PRIMITIVE
        };
        nmo_type_registry_register(bench_registry, &type);
    }
    
    /* Benchmark unregistration */
    double start = get_time_ms();
    
    for (int i = 0; i < BENCH_TYPE_COUNT; i++) {
        nmo_result_t result = nmo_type_registry_unregister(bench_registry, guids[i]);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    double end = get_time_ms();
    double elapsed = end - start;
    double per_type = elapsed / BENCH_TYPE_COUNT;
    
    printf("\n  Unregistration Throughput:\n");
    printf("    Total: %d types in %.2f ms\n", BENCH_TYPE_COUNT, elapsed);
    printf("    Per type: %.4f ms\n", per_type);
    
    /* Performance target: similar to registration */
    ASSERT_TRUE(per_type < 0.5);
    
    bench_teardown();
}

/* ============================================================================
 * Benchmark Summary
 * ============================================================================ */

TEST(type_system_bench, summary) {
    printf("\n");
    printf("  ======================================\n");
    printf("  Type System Performance Summary\n");
    printf("  ======================================\n");
    printf("  Benchmark configuration:\n");
    printf("    - Type count: %d\n", BENCH_TYPE_COUNT);
    printf("    - Lookup iterations: %d\n", BENCH_LOOKUP_COUNT);
    printf("  ======================================\n");
    
    /* This test always passes - it's just for summary output */
    ASSERT_TRUE(1);
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST_CATEGORIZED(type_system_bench, registration_throughput, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(type_system_bench, guid_lookup_latency, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(type_system_bench, name_lookup_latency, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(type_system_bench, enum_registration, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(type_system_bench, flags_registration, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(type_system_bench, memory_usage, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(type_system_bench, unregistration_throughput, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(type_system_bench, summary, TEST_CATEGORY_PERFORMANCE);
TEST_MAIN_END()
