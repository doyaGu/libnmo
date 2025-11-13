# CLI tools
# Temporarily use simple test tool instead of complex tools
add_executable(test-tool test-tool.c)
target_link_libraries(test-tool PRIVATE nmo)

# TODO: Fix original tools for proper API usage
# add_executable(nmo-inspect nmo-inspect.c)
# target_link_libraries(nmo-inspect PRIVATE nmo)

# add_executable(nmo-validate nmo-validate.c)
# target_link_libraries(nmo-validate PRIVATE nmo)

# add_executable(nmo-convert nmo-convert.c)
# target_link_libraries(nmo-convert PRIVATE nmo)

# add_executable(nmo-diff nmo-diff.c)
# target_link_libraries(nmo-diff PRIVATE nmo)

# add_executable(nmo-debug nmo-debug.c)
# target_link_libraries(nmo-debug PRIVATE nmo)

# Installation
install(TARGETS test-tool
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
