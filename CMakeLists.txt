cmake_minimum_required(VERSION 3.15)

project(libnmo
    VERSION 0.2.0
    DESCRIPTION "C library for reading and writing Virtools file formats (.nmo/.cmo/.vmo)"
    LANGUAGES C
)

set(NMO_PUBLIC_VERSION_HEADER "${PROJECT_SOURCE_DIR}/include/nmo_types.h")
foreach(_nmo_version_part MAJOR MINOR PATCH)
    file(STRINGS "${NMO_PUBLIC_VERSION_HEADER}" _nmo_version_line
        REGEX "^#define NMO_VERSION_${_nmo_version_part} [0-9]+$")
    if(NOT _nmo_version_line)
        message(FATAL_ERROR
            "Failed to parse NMO_VERSION_${_nmo_version_part} from ${NMO_PUBLIC_VERSION_HEADER}")
    endif()
    string(REGEX REPLACE "^#define NMO_VERSION_${_nmo_version_part} ([0-9]+)$" "\\1"
        NMO_PUBLIC_VERSION_${_nmo_version_part} "${_nmo_version_line}")
endforeach()

set(NMO_PUBLIC_VERSION
    "${NMO_PUBLIC_VERSION_MAJOR}.${NMO_PUBLIC_VERSION_MINOR}.${NMO_PUBLIC_VERSION_PATCH}")
if(NOT NMO_PUBLIC_VERSION STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "Public version macros in ${NMO_PUBLIC_VERSION_HEADER} (${NMO_PUBLIC_VERSION}) do not match "
        "project() VERSION (${PROJECT_VERSION}).")
endif()

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
option(NMO_MINGW_STATIC_RUNTIME "Link MinGW CLI executables with the static runtime" OFF)

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
            set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=address -fsanitize=undefined)
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

# Find bundled third-party dependencies through one shared entrypoint.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/deps" "${CMAKE_CURRENT_BINARY_DIR}/deps" EXCLUDE_FROM_ALL)

set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS "${NMO_MINIZ_DIR}")
set(ZLIB_LIBRARIES miniz)

set(YYJSON_FOUND TRUE)
set(YYJSON_INCLUDE_DIRS "${NMO_YYJSON_DIR}/src")
set(YYJSON_LIBRARIES yyjson)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
if(ZLIB_INCLUDE_DIRS)
    include_directories(SYSTEM ${ZLIB_INCLUDE_DIRS})
endif()
# yyjson include dirs are provided to targets that include yyjson directly (e.g. nmo_cli).

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
    src/core/indexed_map.c
    src/core/hex.c
    src/core/parse.c
    src/core/utils.c
    src/core/path.c
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
    src/format/interface_chunk.c
    src/format/interface_chunk_types.c
    src/format/interface_edit.c
    src/format/interface_view.c
    src/format/obj_parser.c
)

# Object layer sources
set(NMO_OBJECT_SOURCES
    src/object/object_type_common.c
    src/object/object_ancestor_state.c
    src/object/object_enums.c
    src/object/object_structs.c
    src/object/object_types.c
    src/object/object_repository.c
    src/object/object_refs.c
    src/object/ref.c
    src/object/object_query.c
    src/object/object_query_index.c
    src/object/object_index.c
    src/object/object_hierarchy.c
    src/object/object_diff.c
    src/object/object_summary.c
    src/object/object_import.c
    src/object/object_system.c
    src/object/shadow_storage.c
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
    src/object/nmo_ref_graph.c
    src/object/nmo_ref_enumerate.c
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
    src/type/type_registry_struct_values.c
    src/type/type_string.c
    src/type/builtin_type_values.c
    src/type/builtin_types.c
    src/type/builtin_operations.c
    src/type/builtin_arithmetic.c
    src/type/builtin_logic.c
    src/type/builtin_bitwise.c
    src/type/builtin_trigonometry.c
    src/type/builtin_vector.c
    src/type/nmo_reflection.c
    src/type/type_view.c
    src/type/type_query.c
)

# Extension layer sources
set(NMO_EXTENSION_SOURCES
    src/extension/extension_registry.c
    src/extension/extension_loader.c
    src/extension/extension_host.c
    src/extension/extension_diagnostics.c
    src/extension/virtools_data_plugin.c
    src/extension/virtools_loader.c
)

# Runtime/session layer sources
set(NMO_RUNTIME_SOURCES
    src/runtime/context.c
    src/runtime/document_workspace.c
    src/runtime/session_util.c
    src/runtime/workspace_edit.c
    src/session/object_system.c
    src/session/deserializer.c
    src/session/id_mapping.c
    src/session/save_id_remap.c
    src/session/id_sanitizer.c
    src/session/reference_resolver.c
    src/session/runtime_kernel.c
    src/session/runtime_ref_remap.c
    src/session/runtime_delete.c
)

# Document layer sources
set(NMO_DOCUMENT_SOURCES
    src/document/document_file_state.c
    src/document/document_load.c
    src/document/document_save.c
    src/document/document_perf_stats.c
    src/document/save_buffer.c
    src/document/document_compare.c
    src/document/document_compare_result.c
    src/document/document_stats.c
)

# Chunk layer sources
set(NMO_CHUNK_SOURCES
    src/chunk/chunk_index.c
    src/chunk/chunk_inspect.c
)

# Export layer sources
set(NMO_EXPORT_SOURCES
    src/export/export_dot.c
    src/export/export_hexdump.c
    src/export/export_json_util.c
)

# Project authoring layer sources
set(NMO_PROJECT_SOURCES
    src/project/project_executor.c
    src/project/project_plan.c
    src/project/scene_authoring.c
    src/project/asset_authoring.c
    src/project/script_authoring.c
    src/project/project_manifest_json.c
    src/project/project_validator.c
)

# Behavior layer sources
set(NMO_BEHAVIOR_SOURCES
    src/behavior/bb_registry.c
    src/behavior/behavior_boundary.c
    src/behavior/behavior_execute.c
    src/behavior/behavior_graph.c
    src/behavior/behavior_index.c
    src/behavior/behavior_query.c
    src/behavior/behavior_rewrite.c
    src/behavior/behavior_view.c
    src/behavior/edit_plan.c
    src/behavior/edit_plan_json.c
    src/behavior/probe_analyzer.c
    src/behavior/semantic_validator.c
    src/behavior/script_edit.c
    src/behavior/script_edit_graph.c
    src/behavior/script_trace_view.c
    src/behavior/script_walker.c
    src/behavior/param_value.c
)

# Lua platform sources
set(NMO_LUA_SOURCES
    src/lua/lua_bindings_core.c
    src/lua/lua_bindings_context.c
    src/lua/lua_bindings_behavior.c
    src/lua/lua_bindings_plan.c
    src/lua/lua_bindings_document.c
    src/lua/lua_bindings_format.c
    src/lua/lua_bindings_object.c
    src/lua/lua_bindings_runtime.c
    src/lua/lua_bindings_session.c
    src/lua/lua_bindings_type.c
    src/lua/lua_bindings_workspace.c
    src/lua/lua_edit_report.c
    src/lua/lua_fold_map_parser.c
    src/lua/lua_handles.c
    src/lua/lua_module.c
    src/lua/lua_runtime.c
)

# Combine all sources (yyjson compiled directly into nmo)
set(NMO_SOURCES
    ${NMO_CORE_SOURCES}
    ${NMO_IO_SOURCES}
    ${NMO_FORMAT_SOURCES}
    ${NMO_TYPE_SOURCES}
    ${NMO_OBJECT_SOURCES}
    ${NMO_RUNTIME_SOURCES}
    ${NMO_DOCUMENT_SOURCES}
    ${NMO_CHUNK_SOURCES}
    ${NMO_EXPORT_SOURCES}
    ${NMO_BEHAVIOR_SOURCES}
    ${NMO_LUA_SOURCES}
    ${NMO_EXTENSION_SOURCES}
    ${NMO_PROJECT_SOURCES}
    ${NMO_YYJSON_DIR}/src/yyjson.c
)

# Create library
if(NMO_BUILD_SHARED)
    add_library(nmo SHARED ${NMO_SOURCES})
else()
    add_library(nmo STATIC ${NMO_SOURCES})
    if(TARGET liblua_objects)
        target_sources(nmo PRIVATE $<TARGET_OBJECTS:liblua_objects>)
    endif()
endif()

target_compile_definitions(nmo PUBLIC $<$<CONFIG:Debug>:NMO_ENABLE_DEBUG_ASSERTS>)

# Link dependencies
target_link_libraries(nmo PUBLIC ${ZLIB_LIBRARIES})
if(NMO_BUILD_SHARED)
    target_link_libraries(nmo PRIVATE lua::lua)
endif()

target_include_directories(nmo
    PUBLIC
        $<BUILD_INTERFACE:${LUA_RESOLVED_SOURCE_ROOT}/src>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/deps
        ${NMO_YYJSON_DIR}/src
)

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
    SOVERSION 2
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
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "app/*" EXCLUDE
    PATTERN "schema" EXCLUDE
    PATTERN "session/nmo_context.h" EXCLUDE
    PATTERN "session/nmo_object_system.h" EXCLUDE
    PATTERN "session/nmo_id_sanitizer.h" EXCLUDE
    PATTERN "session/nmo_runtime_delete.h" EXCLUDE
    PATTERN "session/nmo_runtime_ref_remap.h" EXCLUDE
    PATTERN "session/nmo_save_id_remap.h" EXCLUDE
    PATTERN "session/nmo_session_bridge.h" EXCLUDE
    PATTERN "session/nmo_session_util.h" EXCLUDE
    PATTERN "session/nmo_session_internal.h" EXCLUDE
    PATTERN "session/nmo_reference_resolver.h" EXCLUDE
    PATTERN "session/nmo_ref_graph.h" EXCLUDE
    PATTERN "session/nmo_ref_enumerate.h" EXCLUDE
)

set(NMO_RUNTIME_DATA_FILES
    data/virtools_building_blocks.json
    data/virtools_building_blocks_ext.json
    data/virtools_operation_types.json
    data/virtools_parameter_types.json
    data/virtools_plugins.json
)

# Keep the build-tree CLI relocatable in the same way as an installed CLI.
# nmo resolves runtime metadata relative to its executable, so stage the data
# directory beside the build outputs instead of relying on the caller's CWD.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/data")
foreach(_nmo_data_file IN LISTS NMO_RUNTIME_DATA_FILES)
    get_filename_component(_nmo_data_name "${_nmo_data_file}" NAME)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${_nmo_data_file}"
        "${CMAKE_CURRENT_BINARY_DIR}/data/${_nmo_data_name}"
        COPYONLY
    )
endforeach()

install(FILES
    ${NMO_RUNTIME_DATA_FILES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/libnmo/data
)

# Build tools (before tests so CLI tests can depend on nmo_cli target)
if(NMO_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Build tests
if(NMO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
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
message(STATUS "  MinGW static runtime: ${NMO_MINGW_STATIC_RUNTIME}")
message(STATUS "  ZLIB found:    ${ZLIB_FOUND}")
message(STATUS "  yyjson found:  ${YYJSON_FOUND}")
message(STATUS "")
