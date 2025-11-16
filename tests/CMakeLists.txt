# Test framework
add_library(test_framework STATIC test_framework.c)
target_include_directories(test_framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_framework PUBLIC NMO_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

# Unit tests
add_subdirectory(unit)

# Integration tests
add_subdirectory(integration)

# Performance tests
add_subdirectory(performance)

# Stress tests
add_subdirectory(stress)

# Fuzz tests (optional)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/CMakeLists.txt)
    add_subdirectory(fuzz)
endif()
