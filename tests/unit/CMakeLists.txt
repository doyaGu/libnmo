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

# IO layer tests
add_unit_test(test_io_file)
add_unit_test(test_io_memory)
add_unit_test(test_io_compressed)
add_unit_test(test_io_checksum)
add_unit_test(test_txn)

# Format layer tests
add_unit_test(test_header)
add_unit_test(test_header1)
add_unit_test(test_object)
add_unit_test(test_manager)
add_unit_test(test_chunk)
add_unit_test(test_chunk_parser)
add_unit_test(test_chunk_writer)
add_unit_test(test_chunk_serialize)
add_unit_test(test_chunk_remap)

# Schema layer tests
add_unit_test(test_schema_registry)

# Session layer tests
add_unit_test(test_object_repository)
add_unit_test(test_id_remap)
add_unit_test(test_context)
add_unit_test(test_session)
