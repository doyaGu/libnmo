# Fuzz tests
macro(add_fuzz_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endmacro()

add_fuzz_test(test_truncation_fuzz)
