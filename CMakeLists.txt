cmake_minimum_required(VERSION 3.15)

project(libnmo
    VERSION 1.0.0
    DESCRIPTION "C library for reading and writing Virtools file formats (.nmo/.cmo/.vmo)"
    LANGUAGES C
)

# Set C standard to C17
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build options
# Re-enable tests to verify identifier implementation
option(NMO_BUILD_TESTS "Build unit tests" ON)
option(NMO_BUILD_TOOLS "Build CLI tools" ON)
option(NMO_BUILD_EXAMPLES "Build examples" OFF)
option(NMO_ENABLE_SIMD "Enable SIMD optimizations" OFF)
option(NMO_BUILD_SHARED "Build shared library" OFF)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2)
    endif()
else()
    # Temporarily disable -Werror to see all compilation errors
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
        # Temporarily disable sanitizers to simplify initial build
        # add_compile_options(-fsanitize=address -fsanitize=undefined)
        # add_link_options(-fsanitize=address -fsanitize=undefined)
    endif()
endif()

# Find dependencies
# Use miniz
set(MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/miniz")
if(EXISTS "${MINIZ_DIR}/miniz.h")
    message(STATUS "Using miniz from parent project: ${MINIZ_DIR}")
    set(ZLIB_FOUND TRUE)
    set(ZLIB_INCLUDE_DIRS "${MINIZ_DIR}")
    set(ZLIB_LIBRARIES miniz)

    # Add miniz as a subdirectory if not already added
    if(NOT TARGET miniz)
        add_subdirectory("${MINIZ_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/miniz" EXCLUDE_FROM_ALL)
    endif()
else()
    # Fall back to system ZLIB if miniz not available
    find_package(ZLIB REQUIRED)
endif()

# Use yyjson for JSON export
set(YYJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/yyjson")
if(EXISTS "${YYJSON_DIR}/src/yyjson.h")
    message(STATUS "Using yyjson from: ${YYJSON_DIR}")
    # Add yyjson as a subdirectory if not already added
    if(NOT TARGET yyjson)
        set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_MISC OFF CACHE BOOL "" FORCE)
        add_subdirectory("${YYJSON_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/yyjson" EXCLUDE_FROM_ALL)
    endif()
    set(YYJSON_FOUND TRUE)
    set(YYJSON_INCLUDE_DIRS "${YYJSON_DIR}/src")
    set(YYJSON_LIBRARIES yyjson)
else()
    message(WARNING "yyjson not found, JSON export features will be limited")
    set(YYJSON_FOUND FALSE)
endif()

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${ZLIB_INCLUDE_DIRS}
    ${YYJSON_INCLUDE_DIRS}
)

# Core library sources
set(NMO_CORE_SOURCES
    src/core/allocator.c
    src/core/arena.c
    src/core/error.c
    src/core/logger.c
    src/core/guid.c
    src/core/array.c
    src/core/bit_array.c
    src/core/string.c
    src/core/hash.c
    src/core/hash_table.c
    src/core/indexed_map.c
    src/core/shared_library.c
)

# IO layer sources
set(NMO_IO_SOURCES
    src/io/io.c
    src/io/io_file.c
    src/io/io_memory.c
    src/io/io_compressed.c
    src/io/io_checksum.c
)

# Platform-specific IO sources
if(WIN32)
    list(APPEND NMO_IO_SOURCES src/io/txn_windows.c)
else()
    list(APPEND NMO_IO_SOURCES src/io/txn_posix.c)
endif()

# Format layer sources
set(NMO_FORMAT_SOURCES
    src/format/header.c
    src/format/header1.c
    src/format/nmo_data.c
    src/format/object.c
    src/format/manager.c
    src/format/chunk.c
    src/format/chunk_parser.c
    src/format/chunk_writer.c
    src/format/chunk_lifecycle.c
    src/format/chunk_navigation.c
    src/format/chunk_primitives.c
    src/format/chunk_arrays.c
    src/format/chunk_sequences.c
    src/format/chunk_identifiers.c
    src/format/chunk_managers.c
    src/format/chunk_subchunks.c
    src/format/chunk_math.c
    src/format/chunk_compression.c
    src/format/chunk_crc.c
    src/format/chunk_remap.c
    src/format/chunk_bitmap.c
    src/format/chunk_pool.c
    src/format/stream_io.c
    src/format/manager_registry.c
    src/format/nmo_id_remap.c
    src/format/nmo_image.c
    src/format/nmo_image_codec.c
    src/format/nmo_stb_adapter.c
)

# Schema layer sources
set(NMO_SCHEMA_SOURCES
    src/schema/schema.c
    src/schema/schema_registry.c
    src/schema/schema_builder.c
    src/schema/validator.c
    src/schema/migrator.c
    src/schema/builtin/builtin_types.c
    src/schema/builtin/ckobject_hierarchy.c
)

# Session layer sources
set(NMO_SESSION_SOURCES
    src/session/object_repository.c
    src/session/object_index.c
    src/session/load_session.c
    src/session/id_remap.c
    src/session/reference_resolver.c
)

# App layer sources
set(NMO_APP_SOURCES
    src/app/context.c
    src/app/session.c
    src/app/parser.c
    src/app/finish_loading.c
    src/app/stats.c
    src/app/inspector.c
    src/app/nmo_plugin_manager.c
)

# Combine all sources
set(NMO_SOURCES
    ${NMO_CORE_SOURCES}
    ${NMO_IO_SOURCES}
    ${NMO_FORMAT_SOURCES}
    ${NMO_SCHEMA_SOURCES}
    ${NMO_SESSION_SOURCES}
    ${NMO_APP_SOURCES}
)

# Create library
if(NMO_BUILD_SHARED)
    add_library(nmo SHARED ${NMO_SOURCES})
else()
    add_library(nmo STATIC ${NMO_SOURCES})
endif()

# Link dependencies
target_link_libraries(nmo PUBLIC ${ZLIB_LIBRARIES})
if(YYJSON_FOUND)
    target_link_libraries(nmo PUBLIC ${YYJSON_LIBRARIES})
endif()

target_include_directories(nmo PRIVATE ${PROJECT_SOURCE_DIR}/deps)

# Enable threads support
find_package(Threads REQUIRED)
target_link_libraries(nmo PUBLIC Threads::Threads)

# Set library properties
set_target_properties(nmo PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "include/nmo.h"
)

# Installation rules
include(GNUInstallDirs)

install(TARGETS nmo
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Build tests
if(NMO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Build tools
if(NMO_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Build examples
if(NMO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "libnmo configuration summary:")
message(STATUS "  Version:       ${PROJECT_VERSION}")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler:    ${CMAKE_C_COMPILER}")
message(STATUS "  C standard:    C${CMAKE_C_STANDARD}")
message(STATUS "  Build tests:   ${NMO_BUILD_TESTS}")
message(STATUS "  Build tools:   ${NMO_BUILD_TOOLS}")
message(STATUS "  Build examples: ${NMO_BUILD_EXAMPLES}")
message(STATUS "  Enable SIMD:   ${NMO_ENABLE_SIMD}")
message(STATUS "  Build shared:  ${NMO_BUILD_SHARED}")
message(STATUS "  ZLIB found:    ${ZLIB_FOUND}")
message(STATUS "  yyjson found:  ${YYJSON_FOUND}")
message(STATUS "")
