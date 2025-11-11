# CLI tools
add_executable(nmo-inspect nmo-inspect.c)
target_link_libraries(nmo-inspect PRIVATE nmo)

add_executable(nmo-validate nmo-validate.c)
target_link_libraries(nmo-validate PRIVATE nmo)

add_executable(nmo-convert nmo-convert.c)
target_link_libraries(nmo-convert PRIVATE nmo)

add_executable(nmo-diff nmo-diff.c)
target_link_libraries(nmo-diff PRIVATE nmo)

# Installation
install(TARGETS nmo-inspect nmo-validate nmo-convert nmo-diff
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
