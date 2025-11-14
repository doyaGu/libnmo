# Integration test macro
macro(add_integration_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Integration tests
# Phase 2: File I/O round-trip test (NEW - verifies complete load/save pipeline)
add_integration_test(test_file_roundtrip)

# Existing tests
add_integration_test(test_data_roundtrip)
add_integration_test(test_file_io)
add_integration_test(test_chunk_special_cases)

# Phase 5: FinishLoading and object index tests
add_integration_test(test_finish_loading)

# Temporarily disabled tests (old API)
# add_integration_test(test_roundtrip)
# add_integration_test(test_round_trip)
# add_integration_test(test_real_file_roundtrip)
# add_integration_test(test_load_pipeline)
# add_integration_test(test_save_pipeline)
