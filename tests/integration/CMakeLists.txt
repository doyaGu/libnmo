# Integration test macro
macro(add_integration_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Integration tests
# Phase 2: File I/O round-trip test (NEW - verifies complete load/save pipeline)
add_integration_test(test_file_roundtrip)

# Phase 2.1: MMAP load strategy integration test
add_integration_test(test_mmap_load)

# Existing tests
add_integration_test(test_data_roundtrip)
add_integration_test(test_file_io)
add_integration_test(test_chunk_special_cases)

# Phase 3A: Reference file compatibility test (ALL real Virtools files)
add_integration_test(test_all_reference_files)

# Phase 5+: Real file roundtrip with schema-based serialization
add_integration_test(test_real_file_roundtrip)

# Phase 4: CKBehavior historical regression fixtures (Nop1/Nop2)
add_integration_test(test_behavior_roundtrip_regression)

# Phase 3: Declarative registration system integration test (OLD SCHEMA - DISABLED)
# This test uses the old schema system which has been removed
# add_integration_test(test_macro_integration)

# Phase 5.6: Schema v2 plugin and metadata integration tests
add_integration_test(test_cascade_deletion)
add_integration_test(test_specialized_metadata)
add_integration_test(test_type_invalidation)

# Phase 6.7: Complete Type System Integration Test
add_integration_test(test_type_system_complete)

# Phase 3: Object type coverage verification
add_integration_test(test_object_type_coverage)

# Runtime clean-break integration tests
add_integration_test(test_runtime_load_pipeline)
add_integration_test(test_runtime_copy_behavior_graph)
add_integration_test(test_runtime_delete_scene_group_consistency)
add_integration_test(test_extension_abi_v3)
add_integration_test(test_reference_alignment_matrix)
add_integration_test(test_manager_lane_runtime_events)

# CLI tests
if(TARGET nmo_cli)
    add_executable(test_cli_behavior_graph test_cli_behavior_graph.c)
    target_link_libraries(test_cli_behavior_graph PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_behavior_graph nmo_cli)
    target_compile_definitions(test_cli_behavior_graph PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_behavior_graph COMMAND test_cli_behavior_graph)

    add_executable(test_cli_commands test_cli_commands.c)
    target_link_libraries(test_cli_commands PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_commands nmo_cli)
    target_compile_definitions(test_cli_commands PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_commands COMMAND test_cli_commands)

    add_executable(test_cli_diff_objects test_cli_diff_objects.c)
    target_link_libraries(test_cli_diff_objects PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_diff_objects nmo_cli)
    target_compile_definitions(test_cli_diff_objects PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_diff_objects COMMAND test_cli_diff_objects)
endif()

# Temporarily disabled - test_phase6_integration uses deprecated API
# add_integration_test(test_phase6_integration)
