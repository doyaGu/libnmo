# CLI tools

if(NOT YYJSON_FOUND)
    message(FATAL_ERROR "nmo requires yyjson. Ensure deps/yyjson is synced or provide a system installation.")
endif()

# CLI - group/action architecture
add_executable(nmo_cli
    # Entry point
    nmo_cli_main.c
    nmo_command_registry.c
    nmo_cli_dispatch.c
    nmo_cli_common.c
    nmo_cli_output.c
    nmo_cli_json.c
    nmo_cli_record.c
    nmo_edit_report_json.c
    nmo_cli_sort.c
    nmo_cli_write.c

    # Session management
    nmo_tool_owner.c
    nmo_tool_session.c
    nmo_tool_common.c

    # Command framework
    nmo_cmd_ctx.c
    nmo_opt.c
    nmo_cmd_core.c

    # REPL support
    nmo_repl_session.c
    nmo_repl_repl.c
    nmo_repl_util.c
    nmo_repl_commands.c
    nmo_repl_input.c

    # Command groups
    commands/nmo_cmd_file.c
    commands/nmo_cmd_chunk.c
    commands/nmo_cmd_object.c
    commands/nmo_cmd_object_refs.c
    commands/nmo_cmd_object_write.c
    commands/nmo_cmd_debug.c
    commands/nmo_cmd_repl.c
    commands/nmo_cmd_type.c
    commands/nmo_cmd_validate.c
    commands/nmo_cmd_resource.c
    commands/nmo_cmd_behavior.c
    commands/nmo_cmd_behavior_show.c
    commands/nmo_cmd_behavior_graph.c
    commands/nmo_cmd_behavior_rewrite.c
    commands/nmo_cmd_behavior_search.c
    commands/nmo_cmd_behavior_interface.c
    commands/nmo_cmd_behavior_link.c
    commands/nmo_cmd_patch.c
    commands/nmo_cmd_parameter.c
    commands/nmo_cmd_script.c
    commands/nmo_cmd_convert.c
    commands/nmo_cmd_diff.c
    commands/nmo_cmd_extension.c
    commands/nmo_cmd_texture.c
    commands/nmo_cmd_data.c
    commands/nmo_cmd_scene.c
    commands/nmo_cmd_entity.c
    commands/nmo_cmd_material.c
    commands/nmo_cmd_mesh.c
    commands/nmo_cmd_animation.c
    commands/nmo_cmd_completion.c
)
set_target_properties(nmo_cli PROPERTIES OUTPUT_NAME nmo)
target_link_libraries(nmo_cli PRIVATE nmo yyjson)

if(MINGW AND NMO_MINGW_STATIC_RUNTIME)
    target_link_options(nmo_cli PRIVATE -static)
endif()

# Include commands subdirectory
target_include_directories(nmo_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# isocline (REPL line editing). Resolution order:
#   1. deps/isocline/CMakeLists.txt   locally checked-out copy
#   2. FetchContent                    pinned upstream archive (default)
option(NMO_USE_ISOCLINE "Use isocline for REPL line editing" ON)
set(NMO_ISOCLINE_COMMIT "8d6dc1ef95b1b46711e66eb23d39d4467a0fcdac")
set(NMO_ISOCLINE_URL "https://github.com/daanx/isocline/archive/${NMO_ISOCLINE_COMMIT}.tar.gz")
set(NMO_ISOCLINE_SHA256 "0a2149aa99bdffc0c430aa4ecfcdc378bd5f98565eef10991475db52b33f607b")
if(NMO_USE_ISOCLINE)
    set(IC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/deps/isocline/CMakeLists.txt")
        add_subdirectory(${CMAKE_SOURCE_DIR}/deps/isocline ${CMAKE_BINARY_DIR}/deps/isocline)
        message(STATUS "isocline enabled for REPL line editing (deps/isocline)")
    else()
        include(FetchContent)
        set(_nmo_isocline_fetch_args
            URL "${NMO_ISOCLINE_URL}"
            URL_HASH "SHA256=${NMO_ISOCLINE_SHA256}"
        )
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
            list(APPEND _nmo_isocline_fetch_args DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        endif()
        FetchContent_Declare(isocline_upstream ${_nmo_isocline_fetch_args})
        FetchContent_MakeAvailable(isocline_upstream)
        message(STATUS "isocline enabled for REPL line editing (fetched ${NMO_ISOCLINE_COMMIT})")
    endif()
    if(NOT TARGET isocline)
        message(FATAL_ERROR "isocline did not provide the expected 'isocline' target")
    endif()
    # Suppress warnings in third-party isocline sources
    if(MSVC)
        target_compile_options(isocline PRIVATE /w)
    else()
        target_compile_options(isocline PRIVATE -w)
    endif()
    target_link_libraries(nmo_cli PRIVATE isocline)
    target_compile_definitions(nmo_cli PRIVATE NMO_HAVE_ISOCLINE)
else()
    message(STATUS "isocline disabled, REPL will use basic fgets input")
endif()

# Installation
install(TARGETS nmo_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/completions/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/completions
)
