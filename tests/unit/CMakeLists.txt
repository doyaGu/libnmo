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
add_unit_test(test_hash_table)
add_unit_test(test_indexed_map)

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
add_unit_test(test_chunk_serialize)
# Add new chunk API test
add_unit_test(test_chunk_api)
add_unit_test(test_chunk_id_remap)
add_unit_test(test_chunk_serialization)
add_unit_test(test_identifier_search)  # Phase 3: Identifier search tests
add_unit_test(test_identifiers)  # Identifier system tests
add_unit_test(test_subchunks)  # Sub-chunk support tests
add_unit_test(test_chunk_16bit)  # Phase 6: 16-bit special format operations

# Schema layer tests
add_unit_test(test_schema)
add_unit_test(test_schema_registry)
add_unit_test(test_validator)  # Phase 1: Schema validator tests
add_unit_test(test_migrator)   # Phase 1: Schema migrator tests

# Session layer tests
add_unit_test(test_repository)
add_unit_test(test_object_repository)
add_unit_test(test_object_index)  # Phase 5: Object indexing tests
add_unit_test(test_chunk_advanced)  # Phase 6: Advanced chunk features tests
add_unit_test(test_context)
add_unit_test(test_context_session)
add_unit_test(test_context_threadsafe)  # Thread-safety tests
add_unit_test(test_manager_registry)
add_unit_test(test_nmo_baseline)
add_unit_test(test_reference_resolver)
add_unit_test(test_save_pipeline)
add_unit_test(test_load_session_id_remap)
add_unit_test(test_session)

# App layer tests (Phase 7)
# TODO: Re-enable when tests are updated to use correct API
# add_unit_test(test_stats)
# add_unit_test(test_inspector)

