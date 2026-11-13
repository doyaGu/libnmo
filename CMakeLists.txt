cmake_minimum_required(VERSION 3.15)

project(libnmo
    VERSION 0.3.0
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
if(NOT MSVC AND NOT WIN32 AND NOT EMSCRIPTEN)
    set(_nmo_enable_sanitizers_default ON)
endif()
option(NMO_ENABLE_SANITIZERS "Enable ASan/UBSan in Debug builds when supported" ${_nmo_enable_sanitizers_default})

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# WebAssembly (Emscripten): every program grows its memory as it needs
# (Ballance's files take a few hundred megabytes) and gets a stack as large as
# a desktop main thread's instead of Emscripten's 64 KiB.
if(EMSCRIPTEN)
    add_link_options(-sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=8MB)
endif()

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Werror)
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

# The optional components are static libraries that depend on the core, so a
# link line names libnmo.a more than once to satisfy the order. Apple's linker
# warns about that; the warning carries no information here.
if(APPLE)
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_LINK_OPTIONS -Wl,-no_warn_duplicate_libraries)
    check_c_source_compiles("int main(void) { return 0; }" NMO_LINKER_HAS_NO_WARN_DUPLICATE_LIBRARIES)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(NMO_LINKER_HAS_NO_WARN_DUPLICATE_LIBRARIES)
        add_link_options(-Wl,-no_warn_duplicate_libraries)
    endif()
endif()

# libFuzzer builds of the harnesses in tests/fuzz. Everything is compiled with
# coverage instrumentation, ASan and UBSan, and undefined behavior aborts so the
# fuzzer counts it as a crash. Needs a Clang that ships libFuzzer (Apple's does
# not; Homebrew's llvm does).
option(NMO_BUILD_FUZZERS "Build the libFuzzer harnesses in tests/fuzz (Clang with libFuzzer)" OFF)
if(NMO_BUILD_FUZZERS)
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer)
    check_c_source_compiles(
        "#include <stddef.h>
        #include <stdint.h>
        int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { (void)data; (void)size; return 0; }"
        NMO_LIBFUZZER_AVAILABLE)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(NOT NMO_LIBFUZZER_AVAILABLE)
        message(FATAL_ERROR "NMO_BUILD_FUZZERS is ON, but ${CMAKE_C_COMPILER} cannot link with -fsanitize=fuzzer")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# Find bundled third-party dependencies through one shared entrypoint.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/deps" "${CMAKE_CURRENT_BINARY_DIR}/deps" EXCLUDE_FROM_ALL)

set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS "${NMO_MINIZ_DIR}" "${NMO_MINIZ_EXPORT_DIR}")

set(YYJSON_FOUND TRUE)
set(YYJSON_INCLUDE_DIRS "${NMO_YYJSON_DIR}/src")
set(YYJSON_LIBRARIES yyjson)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
if(ZLIB_INCLUDE_DIRS)
    include_directories(SYSTEM ${ZLIB_INCLUDE_DIRS})
endif()
# yyjson include dirs are provided to targets that include yyjson directly (e.g. nmo_cli).

# Platform layer: what the library takes from the operating system
if(WIN32)
    set(NMO_PLATFORM_SOURCES src/platform/platform_windows.c)
else()
    set(NMO_PLATFORM_SOURCES src/platform/platform_posix.c)
endif()

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
    src/io/io_mmap.c
    src/io/txn.c
)

# Format layer sources
set(NMO_FORMAT_SOURCES
    src/format/perf_stats.c
    src/format/header.c
    src/format/header1.c
    src/format/nmo_data.c
    src/format/object.c
    src/format/manager.c
    src/format/chunk.c
    src/format/chunk_primitives.c
    src/format/chunk_arrays.c
    src/format/chunk_subchunks.c
    src/format/chunk_compression.c
    src/format/chunk_remap.c
    src/format/chunk_residue.c
    src/format/chunk_bitmap.c
    src/format/chunk_pool.c
    src/format/manager_registry.c
    src/format/nmo_id_remap.c
    src/format/nmo_image.c
    src/format/nmo_image_codec.c
    src/format/nmo_plane_codec.c
    src/format/nmo_stb_adapter.c
    src/format/interface_chunk.c
    src/format/interface_copy.c
    src/format/interface_edit.c
    src/format/obj_parser.c
)

# Object layer sources
set(NMO_OBJECT_SOURCES
    src/object/context.c
    src/object/object_type_common.c
    src/object/object_enums.c
    src/object/object_structs.c
    src/object/object_types.c
    src/object/object_repository.c
    src/object/ref.c
    src/object/object_query_index.c
    src/object/object_index.c
    src/object/object_system.c
    src/object/mutable_refs.c
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
    src/type/object_ancestor_state.c
    src/type/interface_chunk_types.c
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
    src/type/builtin_math.c
    src/type/nmo_reflection.c
    src/type/type_view.c
    src/type/type_query.c
)

# Extension layer sources
set(NMO_EXTENSION_SOURCES
    src/extension/bb_registry.c
    src/extension/extension_registry.c
    src/extension/extension_loader.c
    src/extension/extension_host.c
    src/extension/virtools_data_plugin.c
    src/extension/virtools_loader.c
)

# Runtime/session layer sources
set(NMO_RUNTIME_SOURCES
    src/runtime/object_hierarchy.c
    src/runtime/object_query.c
    src/runtime/object_import.c
    src/runtime/object_refs.c
    src/runtime/object_summary.c
    src/runtime/document_workspace.c
    src/runtime/session_util.c
    src/runtime/workspace_edit_journal.c
    src/runtime/workspace_edit.c
    src/runtime/workspace_edit_animation.c
    src/runtime/workspace_edit_asset.c
    src/runtime/workspace_edit_behavior.c
    src/runtime/workspace_edit_common.c
    src/runtime/workspace_edit_entity.c
    src/runtime/workspace_edit_manager.c
    src/runtime/workspace_edit_object.c
    src/runtime/workspace_edit_param.c
    src/runtime/workspace_edit_scene.c
    src/runtime/workspace_edit_sound.c
    src/session/object_system.c
    src/session/serializer.c
    src/session/session_load.c
    src/session/session.c
    src/session/deserializer.c
    src/session/id_mapping.c
    src/session/save_id_remap.c
    src/session/reference_resolver.c
    src/session/runtime_kernel.c
    src/session/runtime_ref_remap.c
    src/session/runtime_delete.c
)

# Document layer sources
set(NMO_DOCUMENT_SOURCES
    src/document/object_diff.c
    src/document/document_file_state.c
    src/document/document_export.c
    src/document/document_load.c
    src/document/document_objects.c
    src/document/document_save.c
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
    src/export/export_gltf.c
    src/export/export_hexdump.c
    src/export/export_json_util.c
)

# Behavior layer sources
set(NMO_BEHAVIOR_SOURCES
    src/behavior/behavior_acceleration.c
    src/behavior/interface_view.c
    src/behavior/behavior_boundary.c
    src/behavior/behavior_graph.c
    src/behavior/behavior_index.c
    src/behavior/behavior_op_block.c
    src/behavior/behavior_query.c
    src/behavior/behavior_view.c
    src/behavior/script_edit_graph.c
    src/behavior/script_index.c
    src/behavior/script_model.c
    src/behavior/script_trace_view.c
    src/behavior/script_walker.c
    src/behavior/param_value.c
)

# Combine all sources (yyjson compiled directly into nmo)
set(NMO_SOURCES
    ${NMO_PLATFORM_SOURCES}
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
    ${NMO_EXTENSION_SOURCES}
    ${NMO_YYJSON_DIR}/src/yyjson.c
)

# Create the core library. The Lua runtime (src/lua, target nmo_lua) and the
# project authoring layer (src/project, target nmo_project) are separate
# components declared below; the core does not depend on either of them.
if(NMO_BUILD_SHARED)
    add_library(nmo SHARED ${NMO_SOURCES})
    # A DLL exports what a shared object does on other systems: every
    # function that is not static, NMO_API or not.
    set_target_properties(nmo PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    add_library(nmo STATIC ${NMO_SOURCES})
endif()

target_compile_definitions(nmo PUBLIC $<$<CONFIG:Debug>:NMO_ENABLE_DEBUG_ASSERTS>)

# Link dependencies: miniz is compiled into the library itself.
target_sources(nmo PRIVATE $<TARGET_OBJECTS:nmo_miniz_objects>)

target_include_directories(nmo
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
    SOVERSION 3
    PUBLIC_HEADER "include/nmo.h"
)

# Installation rules
include(GNUInstallDirs)

install(TARGETS nmo EXPORT libnmoTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "app/*" EXCLUDE
    PATTERN "schema" EXCLUDE
    PATTERN "session/nmo_context.h" EXCLUDE
    PATTERN "session/nmo_object_system.h" EXCLUDE
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

# Optional components built on the core library. Each one installs its own
# library, and in a shared build compiles into the shared nmo library. The Lua
# and project components build on the edit component.
set(NMO_THIRD_PARTY_DOCDIR "${CMAKE_INSTALL_DOCDIR}/third-party")
include(cmake/NmoEmbedText.cmake)
include(cmake/NmoUtf8CodePage.cmake)
add_subdirectory(src/engine)
add_subdirectory(src/bb)
set(NMO_IVP_SOURCE_DIR "" CACHE PATH "libivp source tree: builds the libivp physics provider (nmo_physics_ivp)")
if(NMO_IVP_SOURCE_DIR)
    add_subdirectory(src/physics_ivp)
endif()
option(NMO_RENDER "Build the renderer of the engine's frames and its software backend (nmo_render, nmo_render_soft; no graphics API, no window)" ON)
option(NMO_RENDER_GL "Build the renderer's OpenGL backend (nmo_render_gl); implies NMO_RENDER" OFF)
option(NMO_RENDER_GPU "Build the renderer's SDL_GPU backend (nmo_render_gpu: Metal or Vulkan; needs SDL3); implies NMO_RENDER" OFF)
option(NMO_VIEWER "Build the SDL3 view of the engine (nmo_view, nmo behavior run --window); implies NMO_RENDER_GL and NMO_RENDER_GPU" OFF)
if(NMO_RENDER OR NMO_RENDER_GL OR NMO_RENDER_GPU OR NMO_VIEWER)
    add_subdirectory(src/render)
endif()
if(NMO_VIEWER)
    add_subdirectory(src/view)
endif()
add_subdirectory(src/edit)
add_subdirectory(src/lua)
add_subdirectory(src/project)

# CMake package: find_package(libnmo) provides the nmo::nmo target.
include(CMakePackageConfigHelpers)
set(NMO_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/libnmo")
install(EXPORT libnmoTargets
    NAMESPACE nmo::
    DESTINATION ${NMO_CMAKE_INSTALL_DIR}
)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/libnmoConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/libnmoConfig.cmake"
    INSTALL_DESTINATION ${NMO_CMAKE_INSTALL_DIR}
)
# Minor releases may break the API before 1.0 (see docs/api-tiers.md).
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/libnmoConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion
)
install(FILES
    "${PROJECT_BINARY_DIR}/libnmoConfig.cmake"
    "${PROJECT_BINARY_DIR}/libnmoConfigVersion.cmake"
    DESTINATION ${NMO_CMAKE_INSTALL_DIR}
)

# pkg-config: the prefix is derived from the .pc location, so an unpacked
# release archive works wherever it is extracted.
set(_nmo_pc_system_libs ${CMAKE_THREAD_LIBS_INIT})
if(UNIX)
    list(APPEND _nmo_pc_system_libs -lm)
endif()
string(REPLACE ";" " " _nmo_pc_system_libs "${_nmo_pc_system_libs}")
if(NMO_BUILD_SHARED)
    set(NMO_PC_LIBS "")
    set(NMO_PC_LIBS_PRIVATE "${_nmo_pc_system_libs}")
else()
    set(NMO_PC_LIBS "${_nmo_pc_system_libs}")
    set(NMO_PC_LIBS_PRIVATE "")
endif()
configure_file("${PROJECT_SOURCE_DIR}/cmake/libnmo.pc.in" "${PROJECT_BINARY_DIR}/libnmo.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/libnmo.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

set(NMO_RUNTIME_DATA_FILES
    data/virtools_building_blocks.json
    data/virtools_building_blocks_ext.json
    data/virtools_building_block_layouts.json
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

# Licenses of libnmo and of every third-party project compiled into it.
install(FILES LICENSE README.md CHANGELOG.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
foreach(_nmo_dep IN ITEMS miniz yyjson stb)
    install(FILES "${PROJECT_SOURCE_DIR}/deps/${_nmo_dep}/LICENSE"
        DESTINATION ${NMO_THIRD_PARTY_DOCDIR}
        RENAME ${_nmo_dep}-LICENSE
    )
endforeach()
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
# API reference: `cmake --build <build> --target nmo_docs` writes <build>/docs/html
# from the public headers and the format and API documents, when Doxygen is found.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "libnmo")
    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
    set(DOXYGEN_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/docs")
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC NO)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_FILE_PATTERNS "*.h" "*.md")
    set(DOXYGEN_STRIP_FROM_PATH "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}")
    set(DOXYGEN_STRIP_FROM_INC_PATH "${PROJECT_SOURCE_DIR}/include")
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")
    set(DOXYGEN_MARKDOWN_SUPPORT YES)
    set(DOXYGEN_ENABLE_PREPROCESSING YES)
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
    set(DOXYGEN_PREDEFINED "NMO_API=")
    # Headers mark who owns a returned pointer with @ownership
    set(DOXYGEN_ALIASES "ownership=@par Ownership:^^")
    # Heading anchors as GitHub makes them, which the documents link to
    set(DOXYGEN_MARKDOWN_ID_STYLE GITHUB)
    set(DOXYGEN_EXCLUDE "${PROJECT_SOURCE_DIR}/docs/Virtools")
    set(DOXYGEN_INCLUDED_BY_GRAPH NO)
    set(DOXYGEN_DOT_GRAPH_MAX_NODES 100)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
    set(DOXYGEN_WARN_IF_INCOMPLETE_DOC NO)
    set(DOXYGEN_QUIET YES)
    doxygen_add_docs(nmo_docs
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/README.md"
        "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
        "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md"
        "${PROJECT_SOURCE_DIR}/docs"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Generating the libnmo API reference")
endif()

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
message(STATUS "  API reference: ${DOXYGEN_FOUND} (target nmo_docs)")
message(STATUS "  MinGW static runtime: ${NMO_MINGW_STATIC_RUNTIME}")
message(STATUS "  ZLIB found:    ${ZLIB_FOUND}")
message(STATUS "  yyjson found:  ${YYJSON_FOUND}")
message(STATUS "")
