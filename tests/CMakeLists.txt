# Test framework
add_library(test_framework STATIC test_framework.c)
target_include_directories(test_framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_framework PUBLIC
    NMO_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    NMO_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")

add_test(
    NAME test_schema_io_result_audit
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/schema_io_result_audit.cmake
)

add_test(
    NAME test_source_encoding_audit
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/source_encoding_audit.cmake
)

add_test(
    NAME test_layering_audit
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/layering_audit.cmake
)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    if(TARGET nmo_cli)
        add_test(
            NAME test_ballance_tools
            COMMAND ${CMAKE_COMMAND} -E env
                    "NMO_TEST_CLI=$<TARGET_FILE:nmo_cli>"
                    "PYTHONDONTWRITEBYTECODE=1"
                    ${Python3_EXECUTABLE} -m unittest discover
                    -s "${PROJECT_SOURCE_DIR}/apps/ballance/tests" -v
        )
    endif()
    add_test(
        NAME test_layering_calls_audit
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/layering_calls_audit.py
                ${PROJECT_SOURCE_DIR}
    )
    add_test(
        NAME test_completions_manifest
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/scripts/gen_completions.py
                --check
    )
    set_tests_properties(test_completions_manifest PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME test_object_enums_generated
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/scripts/gen_object_enums.py
                --input include/object/nmo_object_enum_defs.h
                --overrides tools/scripts/enum_overrides.json
                --emit-register
                --output src/object/object_enums.generated.h
                --check
    )
    set_tests_properties(test_object_enums_generated PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME test_lua_pushers_generated
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/scripts/gen_lua_pushers.py
                --check
    )
    add_test(
        NAME test_lua_edit_bindings_generated
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/scripts/gen_lua_edit_bindings.py
                --check
    )
    # The Python binding loads the shared library (bindings/python)
    if(NMO_BUILD_SHARED)
        add_test(
            NAME test_python_binding
            COMMAND ${CMAKE_COMMAND} -E env
                    "NMO_LIBRARY=$<TARGET_FILE:nmo>"
                    "NMO_DATA_DIR=${PROJECT_SOURCE_DIR}/data"
                    "PYTHONPATH=${PROJECT_SOURCE_DIR}/bindings/python"
                    "PYTHONDONTWRITEBYTECODE=1"
                    ${Python3_EXECUTABLE} -m unittest discover
                    -s "${PROJECT_SOURCE_DIR}/bindings/python/tests" -v
        )
    endif()
endif()

# Unit tests
add_subdirectory(unit)

# Integration tests
add_subdirectory(integration)

# Round-trip tests
add_subdirectory(round_trip)

# Performance tests
add_subdirectory(performance)

# Stress tests
add_subdirectory(stress)

# A test binary run by hand changes to the directory CTest runs it in, so it
# writes its temporary files where a CTest run does and never into the
# caller's working directory (the source tree, say). Reading another
# directory's test properties needs CMake 3.28; older ones keep the caller's.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    foreach(test_dir unit integration round_trip performance stress)
        get_property(dir_tests DIRECTORY ${test_dir} PROPERTY TESTS)
        get_property(dir_binary DIRECTORY ${test_dir} PROPERTY BINARY_DIR)
        foreach(test_name IN LISTS dir_tests)
            if(TARGET ${test_name})
                get_property(test_work TEST ${test_name} DIRECTORY ${test_dir}
                             PROPERTY WORKING_DIRECTORY)
                if(NOT test_work)
                    set(test_work "${dir_binary}")
                endif()
                target_compile_definitions(${test_name} PRIVATE
                    NMO_TEST_WORKING_DIR="${test_work}")
            endif()
        endforeach()
    endforeach()
endif()

# Batch oracle tool (not a CTest target — run manually)
add_executable(batch_interface_oracle batch_interface_oracle.c)
target_link_libraries(batch_interface_oracle PRIVATE nmo)
target_include_directories(batch_interface_oracle PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(
    NAME test_interface_chunk_batch_oracle
    COMMAND batch_interface_oracle "${PROJECT_SOURCE_DIR}/data/BBSamples"
)
set_tests_properties(test_interface_chunk_batch_oracle PROPERTIES SKIP_RETURN_CODE 77)

# Fuzz tests (optional)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/CMakeLists.txt)
    add_subdirectory(fuzz)
endif()
