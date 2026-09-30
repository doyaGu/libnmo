# Edit component: edit plans, script edits, behavior rewrites (replace and
# fold), the semantic validator, the probe analyzer and behavior execution. It
# builds on the core library (nmo); the core does not depend on it. The Lua and
# project components build on it. Consumers link nmo::edit (target nmo_edit).

set(NMO_EDIT_SOURCES
    behavior_execute.c
    behavior_rewrite.c
    behavior_rewrite_apply.c
    behavior_rewrite_replace.c
    edit_op_kind.c
    edit_plan.c
    edit_plan_executor.c
    edit_plan_json.c
    edit_plan_op.c
    edit_plan_report.c
    edit_plan_state.c
    probe_analyzer.c
    semantic_validator.c
    script_edit.c
    script_edit_interface.c
    script_edit_link.c
    script_edit_node.c
    script_edit_operation.c
    script_edit_parameter.c
)
list(TRANSFORM NMO_EDIT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

if(NMO_BUILD_SHARED)
    # A shared build is a single library: the component compiles into nmo, and
    # nmo_edit is an interface target so consumers link it the same way.
    target_sources(nmo PRIVATE ${NMO_EDIT_SOURCES})
    add_library(nmo_edit INTERFACE)
    target_link_libraries(nmo_edit INTERFACE nmo)
    set(NMO_EDIT_PC_LIBS "")
else()
    add_library(nmo_edit STATIC ${NMO_EDIT_SOURCES})
    target_link_libraries(nmo_edit PUBLIC nmo)
    target_include_directories(nmo_edit
        PRIVATE
            ${PROJECT_SOURCE_DIR}/deps
            ${NMO_YYJSON_DIR}/src
    )
    set(NMO_EDIT_PC_LIBS "-lnmo_edit")
endif()
add_library(nmo::edit ALIAS nmo_edit)
set_target_properties(nmo_edit PROPERTIES EXPORT_NAME edit)

install(TARGETS nmo_edit EXPORT libnmoTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# pkg-config: libnmo-edit requires libnmo.
configure_file("${PROJECT_SOURCE_DIR}/cmake/libnmo-edit.pc.in"
    "${PROJECT_BINARY_DIR}/libnmo-edit.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/libnmo-edit.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
