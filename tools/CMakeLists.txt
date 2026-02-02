# CLI tools

if(NOT YYJSON_FOUND)
    message(FATAL_ERROR "nmo requires yyjson. Ensure deps/yyjson is synced or provide a system installation.")
endif()

# Phase 7 tools
add_executable(nmo_cli
    nmo_tool_main.c
    nmo_tool_session.c
    nmo_inspect_main.c
    nmo_inspect_cli.c
    nmo_inspect_collect.c
    nmo_inspect_util.c
    nmo_inspect_render.c
    nmo_inspect_render_text.c
    nmo_inspect_render_json.c
    nmo_debug_main.c
    nmo_debug_session.c
    nmo_debug_repl.c
    nmo_debug_util.c
    nmo_debug_commands.c
    nmo_tool_common.c
)
set_target_properties(nmo_cli PROPERTIES OUTPUT_NAME nmo)
target_link_libraries(nmo_cli PRIVATE nmo yyjson)

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

