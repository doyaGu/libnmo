# Performance test macro
macro(add_performance_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "performance")
endmacro()

add_performance_test(test_performance)
add_performance_test(test_index_queries)
