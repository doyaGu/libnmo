# CLI tools

if(NOT YYJSON_FOUND)
    message(FATAL_ERROR "nmo requires yyjson. Ensure deps/yyjson is synced or provide a system installation.")
endif()

# CLI - group/action architecture
add_executable(nmo_cli
    # Entry point
    nmo_cli_main.c
    nmo_cli_dispatch.c
    nmo_cli_common.c
    nmo_cli_output.c
    nmo_cli_json.c

    # Session management
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
    commands/nmo_cmd_debug.c
    commands/nmo_cmd_repl.c
    commands/nmo_cmd_type.c
    commands/nmo_cmd_validate.c
    commands/nmo_cmd_resource.c
    commands/nmo_cmd_behavior.c
    commands/nmo_cmd_parameter.c
    commands/nmo_cmd_convert.c
    commands/nmo_cmd_diff.c
    commands/nmo_cmd_query.c
    commands/nmo_cmd_extension.c
    commands/nmo_cmd_texture.c
)
set_target_properties(nmo_cli PROPERTIES OUTPUT_NAME nmo)
target_link_libraries(nmo_cli PRIVATE nmo nmo_json yyjson)

# Include commands subdirectory
target_include_directories(nmo_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

option(NMO_USE_ISOCLINE "Use isocline for REPL line editing" ON)
if(NMO_USE_ISOCLINE AND EXISTS "${CMAKE_SOURCE_DIR}/deps/isocline/CMakeLists.txt")
    add_subdirectory(${CMAKE_SOURCE_DIR}/deps/isocline ${CMAKE_BINARY_DIR}/deps/isocline)
    # Suppress warnings in vendored isocline (third-party code)
    if(TARGET isocline)
        target_compile_options(isocline PRIVATE -w)
    endif()
    target_link_libraries(nmo_cli PRIVATE isocline)
    target_compile_definitions(nmo_cli PRIVATE NMO_HAVE_ISOCLINE)
    message(STATUS "isocline enabled for REPL line editing")
else()
    message(STATUS "isocline not found or disabled, REPL will use basic fgets input")
endif()

# Installation
install(TARGETS nmo_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
