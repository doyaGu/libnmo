# Performance test macro
macro(add_performance_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "performance")
endmacro()

add_performance_test(test_performance)
add_performance_test(test_array_perf)
add_performance_test(test_index_queries)
add_performance_test(test_object_query_perf)
add_performance_test(test_load_save_mmap_baseline)
add_performance_test(test_load_save_phase_stats)

# Temporarily disabled - needs API update for type_system.h v2
# add_performance_test(test_type_system_bench)
