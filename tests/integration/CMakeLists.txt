# Integration test macro
macro(add_integration_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Integration tests
add_integration_test(test_roundtrip)
add_integration_test(test_round_trip)
add_integration_test(test_load_pipeline)
add_integration_test(test_save_pipeline)
