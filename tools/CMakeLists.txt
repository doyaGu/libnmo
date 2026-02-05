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
    nmo_cli_hex.c

    # Session management (shared)
    nmo_tool_session.c
    nmo_tool_common.c

    # REPL support
    nmo_repl_session.c
    nmo_repl_repl.c
    nmo_repl_util.c
    nmo_repl_commands.c

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

    # Semantic summaries (Phase 3)
    summaries/nmo_object_summary.c
)
set_target_properties(nmo_cli PROPERTIES OUTPUT_NAME nmo)
target_link_libraries(nmo_cli PRIVATE nmo yyjson)

# Include commands subdirectory
target_include_directories(nmo_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# TODO: Implement these tools
# add_executable(nmo-validate nmo-validate.c)
# target_link_libraries(nmo-validate PRIVATE nmo)

# add_executable(nmo-convert nmo-convert.c)
# target_link_libraries(nmo-convert PRIVATE nmo)

# add_executable(nmo-diff nmo-diff.c)
# target_link_libraries(nmo-diff PRIVATE nmo)

# Installation
install(TARGETS nmo_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
