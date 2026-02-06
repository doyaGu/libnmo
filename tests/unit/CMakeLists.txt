# Unit test macro
macro(add_unit_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
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
add_unit_test(test_manager)
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
# Add new chunk API test
add_unit_test(test_chunk_api)
add_unit_test(test_chunk_helpers)
add_unit_test(test_chunk_id_remap)
add_unit_test(test_chunk_serialization)
add_unit_test(test_identifier_search)  # Phase 3: Identifier search tests
add_unit_test(test_identifiers)  # Identifier system tests
add_unit_test(test_subchunks)  # Sub-chunk support tests
add_unit_test(test_chunk_16bit)  # Phase 6: 16-bit special format operations
add_unit_test(test_chunk_raw_bitmap)
add_unit_test(test_chunk_encoded_bitmap)
add_unit_test(test_chunk_legacy_bitmap)
add_unit_test(test_image_utils)
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

# Phase 6.x: DSL expression engine tests (Phase A parity)
add_unit_test(test_dsl_expr)

# Phase B: DSL script mode tests
add_unit_test(test_dsl_script)

# Phase C: DSL schema mode tests
add_unit_test(test_dsl_schema)

# Phase D/E: DSL type builtins and operations tests
add_unit_test(test_dsl_operations)

# Phase 6.6: Custom manager support tests
add_unit_test(test_custom_managers)

# Phase 6.7: Object types system tests
add_unit_test(test_object_types)
add_unit_test(test_object_types_conformance)

# Extension layer tests
add_unit_test(test_extension_registry)

# Session layer tests
add_unit_test(test_id_sanitizer)  # Phase 1.1: ID sanitization pipeline
add_unit_test(test_shadow_storage)  # Phase 1.2: Shadow blob preservation
add_unit_test(test_repository)
add_unit_test(test_object_repository)
add_unit_test(test_object_index)  # Phase 5: Object indexing tests
add_unit_test(test_chunk_advanced)  # Phase 6: Advanced chunk features tests
add_unit_test(test_manager_registry)
add_unit_test(test_nmo_baseline)
# Set working directory to project root so data/ can be found
set_tests_properties(test_nmo_baseline PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_unit_test(test_reference_resolver)
add_unit_test(test_session_reference_resolver)
# add_unit_test(test_save_pipeline)  # Uses old schema system - DISABLED
add_unit_test(test_load_session_id_remap)
add_unit_test(test_session)

# App layer tests (Phase 7)
# Public API / umbrella header smoke test
add_unit_test(test_public_api_smoke)

# Context and session integration
add_unit_test(test_context_session)
add_unit_test(test_context_threadsafe)  # Thread-safety tests

# Phase 2.1: Dual-Track IO API tests
add_unit_test(test_load_options)

# Phase 2.1: Memory-mapped IO tests
add_unit_test(test_io_mmap)

# Phase 2.4: DOM Comparison API tests
add_unit_test(test_comparison)
