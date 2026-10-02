# Example programs
add_executable(simple_load simple_load.c)
target_link_libraries(simple_load PRIVATE nmo)

add_executable(simple_save simple_save.c)
target_link_libraries(simple_save PRIVATE nmo)

add_executable(file_converter file_converter.c)
target_link_libraries(file_converter PRIVATE nmo)

add_executable(custom_manager custom_manager.c)
target_link_libraries(custom_manager PRIVATE nmo)

add_executable(object_inspector object_inspector.c)
target_link_libraries(object_inspector PRIVATE nmo)

add_executable(object_type_usage object_type_usage.c)
target_link_libraries(object_type_usage PRIVATE nmo)

add_executable(type_system_example type_system_example.c)
target_link_libraries(type_system_example PRIVATE nmo)

# Run the examples that need no sample file: save a file, convert it, load the
# result, and the registration examples.
if(NMO_BUILD_TESTS)
    set(example_saved "${CMAKE_CURRENT_BINARY_DIR}/example_saved.nmo")
    set(example_converted "${CMAKE_CURRENT_BINARY_DIR}/example_converted.nmo")
    add_test(NAME example_simple_save COMMAND simple_save "${example_saved}")
    add_test(NAME example_file_converter
             COMMAND file_converter "${example_saved}" "${example_converted}" --compress --validate)
    add_test(NAME example_simple_load COMMAND simple_load "${example_converted}")
    add_test(NAME example_object_inspector COMMAND object_inspector "${example_converted}")
    set_tests_properties(example_simple_save PROPERTIES FIXTURES_SETUP example_saved_file)
    set_tests_properties(example_file_converter PROPERTIES
        FIXTURES_REQUIRED example_saved_file FIXTURES_SETUP example_converted_file)
    set_tests_properties(example_simple_load example_object_inspector PROPERTIES
        FIXTURES_REQUIRED example_converted_file)
    add_test(NAME example_custom_manager COMMAND custom_manager)
    add_test(NAME example_object_type_usage COMMAND object_type_usage)
    add_test(NAME example_type_system COMMAND type_system_example)
endif()
