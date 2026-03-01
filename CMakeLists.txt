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

# Sanitizers are great for catching memory issues, but aren't available on all
# toolchains (notably some MinGW distributions on Windows). Keep them opt-in on
# Windows and on by default on non-Windows GCC/Clang Debug builds.
set(_nmo_enable_sanitizers_default OFF)
if(NOT MSVC AND NOT WIN32)
    set(_nmo_enable_sanitizers_default ON)
endif()
option(NMO_ENABLE_SANITIZERS "Enable ASan/UBSan in Debug builds when supported" ${_nmo_enable_sanitizers_default})

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
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)

        if(NMO_ENABLE_SANITIZERS)
            include(CheckCSourceCompiles)

            set(CMAKE_REQUIRED_FLAGS "-fsanitize=address -fsanitize=undefined")
            set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address -fsanitize=undefined")
            check_c_source_compiles("int main(void) { return 0; }" NMO_SANITIZERS_AVAILABLE)
            unset(CMAKE_REQUIRED_FLAGS)
            unset(CMAKE_REQUIRED_LINK_OPTIONS)

            if(NMO_SANITIZERS_AVAILABLE)
                add_compile_options(-fsanitize=address -fsanitize=undefined)
                add_link_options(-fsanitize=address -fsanitize=undefined)
            else()
                message(WARNING "NMO_ENABLE_SANITIZERS is ON, but ASan/UBSan are not available; disabling.")
            endif()
        endif()
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

    # Silence warnings from third-party miniz sources
    if(TARGET miniz)
        if(MSVC)
            target_compile_options(miniz PRIVATE /w)
        else()
            target_compile_options(miniz PRIVATE -w)
        endif()
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
include_directories(${PROJECT_SOURCE_DIR}/include)
if(ZLIB_INCLUDE_DIRS)
    include_directories(SYSTEM ${ZLIB_INCLUDE_DIRS})
endif()
# yyjson include dirs propagate automatically via target_link_libraries PUBLIC yyjson

# Core library sources
set(NMO_CORE_SOURCES
    src/core/allocator.c
    src/core/arena.c
    src/core/color.c
    src/core/error.c
    src/core/logger.c
    src/core/version.c
    src/core/guid.c
    src/core/array.c
    src/core/arena_array.c
    src/core/bit_array.c
    src/core/string.c
    src/core/hash.c
    src/core/hash_table.c
    src/core/hash_set.c
    src/core/indexed_map.c
    src/core/list.c
    src/core/pool.c
    src/core/ownership.c
    src/core/refcount.c
    src/core/shared_library.c
)

# IO layer sources
set(NMO_IO_SOURCES
    src/io/io.c
    src/io/io_file.c
    src/io/io_memory.c
    src/io/io_compressed.c
    src/io/io_checksum.c
    src/io/io_mmap.c
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
    src/format/manager_registry.c
    src/format/nmo_id_remap.c
    src/format/nmo_image.c
    src/format/nmo_image_codec.c
    src/format/nmo_stb_adapter.c
)

# Object layer sources
set(NMO_OBJECT_SOURCES
    src/object/object_type_common.c
    src/object/object_enums.c
    src/object/object_structs.c
    src/object/object_types.c
    src/object/object_repository.c
    src/object/object_index.c
    src/object/object_system.c
    src/object/shadow_storage.c
    src/object/deserialize_context.c
    src/object/builtin/object_vtables.c
    src/object/builtin/ckobject_schemas.c
    src/object/builtin/ckbeobject_schemas.c
    src/object/builtin/cksceneobject_schemas.c
    src/object/builtin/ckrenderobject_schemas.c
    src/object/builtin/ckparameter_schemas.c
    src/object/builtin/ckparameterin_schemas.c
    src/object/builtin/ckparameterout_schemas.c
    src/object/builtin/ckparameterlocal_schemas.c
    src/object/builtin/ckparameteroperation_schemas.c
    src/object/builtin/ckgroup_schemas.c
    src/object/builtin/cklevel_schemas.c
    src/object/builtin/ckscene_schemas.c
    src/object/builtin/ckbehavior_schemas.c
    src/object/builtin/ckbehaviorio_schemas.c
    src/object/builtin/ckbehaviorlink_schemas.c
    src/object/builtin/ckinterfaceobjectmanager_schemas.c
    src/object/builtin/ckdataarray_schemas.c
    src/object/builtin/ckmessagemanager_schemas.c
    src/object/builtin/ckattributemanager_schemas.c
    src/object/builtin/cksynchro_schemas.c
    src/object/builtin/cksound_schemas.c
    src/object/builtin/ckgrid_schemas.c
    src/object/builtin/cklayer_schemas.c
    src/object/builtin/ckplace_schemas.c
    src/object/builtin/ck3dentity_schemas.c
    src/object/builtin/ckcamera_schemas.c
    src/object/builtin/cktargetcamera_schemas.c
    src/object/builtin/ck3dobject_schemas.c
    src/object/builtin/ck2dentity_schemas.c
    src/object/builtin/cksprite_schemas.c
    src/object/builtin/cksprite3d_schemas.c
    src/object/builtin/ckspritetext_schemas.c
    src/object/builtin/cktexture_schemas.c
    src/object/builtin/ckmaterial_schemas.c
    src/object/builtin/cklight_schemas.c
    src/object/builtin/cktargetlight_schemas.c
    src/object/builtin/ckmesh_schemas.c
    src/object/builtin/ckpatchmesh_schemas.c
    src/object/builtin/ckcurve_schemas.c
    src/object/builtin/ckcharacter_schemas.c
    src/object/builtin/ckanimation_schemas.c
    src/object/builtin/ckkinematicchain_schemas.c
    src/object/builtin/ckrendercontext_schemas.c
)

# Type System sources
set(NMO_TYPE_SOURCES
    src/type/type_registry.c
    src/type/type_runtime.c
    src/type/plugin_support.c
    src/type/operation_registry.c
    src/type/type_parser.c
    src/type/type_registry_enum.c
    src/type/type_registry_struct.c
    src/type/type_string_converters.c
    src/type/builtin_types.c
    src/type/builtin_operations.c
    src/type/builtin_arithmetic.c
    src/type/builtin_logic.c
    src/type/builtin_bitwise.c
    src/type/builtin_trigonometry.c
    src/type/builtin_vector.c
    src/type/nmo_reflection.c
)

# DSL System sources
set(NMO_DSL_SOURCES
    src/dsl/nmo_dsl.c
    src/dsl/nmo_dsl_lex.c
    src/dsl/nmo_dsl_ast.c
    src/dsl/nmo_dsl_parse.c
    src/dsl/nmo_dsl_eval.c
    src/dsl/nmo_dsl_seq.c
)

# Extension layer sources
set(NMO_EXTENSION_SOURCES
    src/extension/extension_registry.c
    src/extension/extension_loader.c
    src/extension/extension_host.c
    src/extension/extension_diagnostics.c
)

# Session layer sources
set(NMO_SESSION_SOURCES
    src/session/object_system.c
    src/session/builder.c
    src/session/load_session.c
    src/session/id_remap.c
    src/session/id_sanitizer.c
    src/session/reference_resolver.c
    src/session/nmo_ref_graph.c
    src/session/nmo_ref_enumerate.c
)

# App layer sources
set(NMO_APP_SOURCES
    src/app/context.c
    src/app/session.c
    src/app/parser.c
    src/app/saver.c
    src/app/save_buffer.c
    src/app/finish_loading.c
    src/app/comparison.c
    src/app/stats.c
    src/app/inspector.c
    src/app/hexdump.c
)

# Combine all sources
set(NMO_SOURCES
    ${NMO_CORE_SOURCES}
    ${NMO_IO_SOURCES}
    ${NMO_FORMAT_SOURCES}
    ${NMO_OBJECT_SOURCES}
    ${NMO_TYPE_SOURCES}
    ${NMO_DSL_SOURCES}
    ${NMO_EXTENSION_SOURCES}
    ${NMO_SESSION_SOURCES}
    ${NMO_APP_SOURCES}
)

# Create library
if(NMO_BUILD_SHARED)
    add_library(nmo SHARED ${NMO_SOURCES})
else()
    add_library(nmo STATIC ${NMO_SOURCES})
endif()

target_compile_definitions(nmo PUBLIC $<$<CONFIG:Debug>:NMO_ENABLE_DEBUG_ASSERTS>)

# Link dependencies
target_link_libraries(nmo PUBLIC ${ZLIB_LIBRARIES})
if(YYJSON_FOUND)
    target_link_libraries(nmo PUBLIC yyjson)
endif()

target_include_directories(nmo PRIVATE ${PROJECT_SOURCE_DIR}/deps)

# Enable threads support
find_package(Threads REQUIRED)
target_link_libraries(nmo PUBLIC Threads::Threads)

# Link against math library on Unix-like platforms for pow()/friends
if(UNIX)
    target_link_libraries(nmo PUBLIC m)
endif()

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
    FILES_MATCHING PATTERN "*.h" PATTERN "schema" EXCLUDE
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
