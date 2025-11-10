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
option(NMO_BUILD_TESTS "Build tests" ON)
option(NMO_BUILD_TOOLS "Build CLI tools" ON)
option(NMO_BUILD_EXAMPLES "Build examples" ON)
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
    add_compile_options(-Wall -Wextra -Werror -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -fsanitize=address -fsanitize=undefined)
        add_link_options(-fsanitize=address -fsanitize=undefined)
    endif()
endif()

# Find dependencies
find_package(ZLIB REQUIRED)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${ZLIB_INCLUDE_DIRS}
)

# Core library sources
set(NMO_CORE_SOURCES
    src/core/allocator.c
    src/core/arena.c
    src/core/error.c
    src/core/logger.c
    src/core/guid.c
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
    src/format/object.c
    src/format/manager.c
    src/format/chunk.c
    src/format/chunk_parser.c
    src/format/chunk_writer.c
    src/format/chunk_serialize.c
    src/format/manager_registry.c
)

# Schema layer sources
set(NMO_SCHEMA_SOURCES
    src/schema/schema.c
    src/schema/schema_registry.c
    src/schema/validator.c
    src/schema/migrator.c
    src/builtin_schemas/nmo_builtin_schemas.c
)

# Session layer sources
set(NMO_SESSION_SOURCES
    src/session/object_repository.c
    src/session/load_session.c
    src/session/id_remap.c
    src/session/parser.c
    src/session/builder.c
)

# App layer sources
set(NMO_APP_SOURCES
    src/app/context.c
    src/app/session.c
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
message(STATUS "")
