# Unit test macro
macro(add_unit_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    target_compile_definitions(${name} PRIVATE NMO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Core layer tests
add_unit_test(test_allocator)
add_unit_test(test_arena)
add_unit_test(test_error)
add_unit_test(test_logger)
add_unit_test(test_guid)
add_unit_test(test_array)
add_unit_test(test_arena_array)
add_unit_test(test_bit_array)
add_unit_test(test_string)
add_unit_test(test_hash_table)
add_unit_test(test_hash_set)
add_unit_test(test_indexed_map)
add_unit_test(test_list)
add_unit_test(test_hex)
add_unit_test(test_parse)

# IO layer tests
# Note: Old IO API tests have been removed (test_io_file, test_io_memory, 
# test_io_compressed, test_io_checksum, test_txn) - will be rewritten with new API
add_unit_test(test_io_memory)
add_unit_test(test_io_file)
add_unit_test(test_io_compressed)
add_unit_test(test_io_checksum)
add_unit_test(test_txn)
add_unit_test(test_txn_windows)
add_unit_test(test_txn_utf8)

# Format layer tests
add_unit_test(test_header)
add_unit_test(test_header1)
add_unit_test(test_object)
# Legacy hook-based manager API removed in runtime clean-break.
# add_unit_test(test_manager)
add_unit_test(test_manager_ints)
add_unit_test(test_object_ids)
add_unit_test(test_arrays)  # Array support tests
add_unit_test(test_buffer_variants)  # Buffer operation variants tests
add_unit_test(test_chunk)
add_unit_test(test_chunk_parser)
add_unit_test(test_chunk_writer)
add_unit_test(test_chunk_writer_context)  # Phase 1.3: Version context stack
add_unit_test(test_chunk_writer_reserve)  # Phase 2.2: Reserve-and-patch API
add_unit_test(test_chunk_writer_intlist_audit)  # Phase 2.3: IntList auditor
add_unit_test(test_chunk_serialize)
add_unit_test(test_data_section_plan)
# Add new chunk API test
add_unit_test(test_chunk_api)
add_unit_test(test_chunk_helpers)
add_unit_test(test_chunk_id_remap)
add_unit_test(test_chunk_serialization)
add_unit_test(test_identifier_search)  # Phase 3: Identifier search tests
add_unit_test(test_identifiers)  # Identifier system tests
add_unit_test(test_subchunks)  # Sub-chunk support tests
add_unit_test(test_chunk_truncation)  # Phase 3: Truncated chunk safety tests
add_unit_test(test_chunk_16bit)  # Phase 6: 16-bit special format operations
add_unit_test(test_chunk_raw_bitmap)
add_unit_test(test_chunk_encoded_bitmap)
add_unit_test(test_chunk_legacy_bitmap)
add_unit_test(test_interface_chunk)
add_unit_test(test_interface_edit)
add_unit_test(test_interface_view)
add_unit_test(test_obj_parser)
add_unit_test(test_image_utils)
add_unit_test(test_image_decode)
add_unit_test(test_stb_adapter)

# Legacy schema layer tests (REMOVED)
# The legacy schema_v1 test sources have been deleted as part of the migration.
# New Phase 6 / type system suites replace their coverage.

# Schema v2 layer tests (Phase 5.5)
add_test(NAME test_type_registry
    COMMAND ${CMAKE_COMMAND} -E env
    $<TARGET_FILE:test_type_registry_exe>
)
add_executable(test_type_registry_exe type/test_type_registry.c)
target_link_libraries(test_type_registry_exe PRIVATE nmo test_framework)
target_include_directories(test_type_registry_exe PRIVATE 
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/include
)

# Phase 6.1: Operation system tests
add_unit_test(test_operation_registry)
add_unit_test(test_builtin_operations)

# Phase 6.2: Dynamic type registration tests
add_unit_test(test_type_parser)
add_unit_test(test_string_registration)
add_unit_test(test_enum_flags)
add_unit_test(test_struct)
add_unit_test(test_dynamic_types_integration)
add_unit_test(test_advanced_struct)
add_unit_test(test_union_reflection)

# Phase 6.3: Type compatibility and conversion tests
add_unit_test(test_type_compatibility)

# Phase 6.4: String conversion and serialization tests
add_unit_test(test_type_string)
add_unit_test(test_type_view)

# Phase 6.6: Custom manager support tests
add_unit_test(test_custom_managers)

# Phase 6.7: Object types system tests
add_unit_test(test_object_types)
add_unit_test(test_object_types_conformance)
add_unit_test(test_object_serialization)
add_unit_test(test_objanim_controllers)

# Extension layer tests
# Extension registry tests still target ABI v1/v2 callback layout.
# add_unit_test(test_extension_registry)
add_unit_test(test_extension_diagnostics)

# Session layer tests
add_unit_test(test_id_sanitizer)  # Phase 1.1: ID sanitization pipeline
add_unit_test(test_shadow_storage)  # Phase 1.2: Shadow blob preservation
add_unit_test(test_repository)
add_unit_test(test_object_repository)
add_unit_test(test_object_query)
add_unit_test(test_ref_query)
add_unit_test(test_builder)
add_unit_test(test_object_index)  # Phase 5: Object indexing tests
add_unit_test(test_chunk_advanced)  # Phase 6: Advanced chunk features tests
add_unit_test(test_manager_registry)
add_unit_test(test_nmo_baseline)
# Set working directory to project root so data/ can be found
set_tests_properties(test_nmo_baseline PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
# Reference-resolver is internal in runtime clean-break.
# add_unit_test(test_reference_resolver)
# add_unit_test(test_session_reference_resolver)
# add_unit_test(test_save_pipeline)  # Uses old schema system - DISABLED
add_unit_test(test_deserializer_id_remap)
add_unit_test(test_session)
add_unit_test(test_workspace_edit)

# App layer tests (Phase 7)
# Public API / umbrella header smoke test
add_unit_test(test_public_api_smoke)
add_unit_test(test_lua_runtime)
add_unit_test(test_lua_bindings)
add_unit_test(test_lua_bindings_document)
add_unit_test(test_lua_bindings_session_runtime)
add_unit_test(test_lua_bindings_object)
add_unit_test(test_lua_bindings_type)
add_unit_test(test_lua_bindings_behavior)
add_unit_test(test_lua_bindings_plan)
add_unit_test(test_lua_bindings_format)
add_unit_test(test_app_util)
target_link_libraries(test_app_util PRIVATE yyjson)
add_unit_test(test_chunk_inspect_api)
add_unit_test(test_document_stats_api)
add_unit_test(test_behavior_query)
add_unit_test(test_object_edit_api)
add_unit_test(test_object_summary_api)
target_link_libraries(test_object_summary_api PRIVATE yyjson)
add_unit_test(test_object_import_api)
add_unit_test(test_report_result)
add_unit_test(test_report_view)
target_link_libraries(test_report_view PRIVATE yyjson)

# Batch rename helpers (wildcard capture + template substitution)
# Compiles tool common sources directly; needs CLI support files for linkage.
add_executable(test_batch_rename test_batch_rename.c
    ../../tools/nmo_tool_common.c
    ../../tools/nmo_cli_common.c
    ../../tools/nmo_cli_output.c
    ../../tools/nmo_cli_json.c
)
target_link_libraries(test_batch_rename PRIVATE nmo test_framework yyjson)
target_include_directories(test_batch_rename PRIVATE ../../tools)
add_test(NAME test_batch_rename COMMAND test_batch_rename)

# Parameter value decoding
add_unit_test(test_param_value)

# Script walker
add_unit_test(test_script_walker)

# Script edit graph
add_unit_test(test_script_edit_graph)
set_tests_properties(test_script_edit_graph PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_unit_test(test_edit_plan)
add_unit_test(test_script_edit_transaction)
add_unit_test(test_script_edit_interface)
add_unit_test(test_behavior_execute)
set_tests_properties(test_behavior_execute PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# BB prototype registry (JSON loader now in nmo core)
add_unit_test(test_bb_registry)
set_tests_properties(test_bb_registry PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Virtools data loading (param types + operations from JSON)
add_unit_test(test_virtools_types)
set_tests_properties(test_virtools_types PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_unit_test(test_virtools_data_files)
target_link_libraries(test_virtools_data_files PRIVATE yyjson)
set_tests_properties(test_virtools_data_files PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Behavior ownership index
add_unit_test(test_behavior_index)
set_tests_properties(test_behavior_index PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Stable behavior view facade
add_unit_test(test_behavior_view)

# Recursive behavior graph
add_unit_test(test_behavior_graph_recursive)
set_tests_properties(test_behavior_graph_recursive PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Behavior rewrite planning
add_unit_test(test_behavior_rewrite)
set_tests_properties(test_behavior_rewrite PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Typed parameter chain tracing
add_unit_test(test_param_chain)
set_tests_properties(test_param_chain PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Context and session integration
add_unit_test(test_context_session)
add_unit_test(test_context_threadsafe)  # Thread-safety tests

# Phase 2.1: Dual-Track IO API tests
# Partial load profiles
add_unit_test(test_load_options)
set_tests_properties(test_load_options PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Runtime clean-break tests
add_unit_test(test_runtime_kernel)
add_unit_test(test_runtime_copy_delete)
add_unit_test(test_bulk_destroy)
add_unit_test(test_delete_rollback)
add_unit_test(test_safe_detach_validation)
add_unit_test(test_session_ref_graph_cache)
add_unit_test(test_strict_load)
add_unit_test(test_count_field_metadata)
add_unit_test(test_strip_preview)
add_unit_test(test_manager_event_bus)
add_unit_test(test_type_runtime_hook_coverage)
add_unit_test(test_no_legacy_runtime_api_exports)

# Phase 2.1: Memory-mapped IO tests
add_unit_test(test_io_mmap)

# Phase 2.4: DOM Comparison API tests
add_unit_test(test_comparison)
add_unit_test(test_object_diff)

# Orphan reachability (mark-sweep via ref graph)
add_unit_test(test_orphan_reachability)
set_tests_properties(test_orphan_reachability PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
