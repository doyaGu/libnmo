# Fuzz tests
macro(add_fuzz_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

add_fuzz_test(test_truncation_fuzz)
