# Project authoring component: project plans, scene/asset/script authoring, the
# plan executor, validator and manifest reader. It builds on the core library
# (nmo); the core does not depend on it. Consumers link nmo::project (target
# nmo_project).

set(NMO_PROJECT_SOURCES
    project_executor.c
    project_plan.c
    scene_authoring.c
    asset_authoring.c
    script_authoring.c
    project_manifest_json.c
    project_validator.c
)
list(TRANSFORM NMO_PROJECT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

if(NMO_BUILD_SHARED)
    # A shared build is a single library: the component compiles into nmo, and
    # nmo_project is an interface target so consumers link it the same way.
    target_sources(nmo PRIVATE ${NMO_PROJECT_SOURCES})
    add_library(nmo_project INTERFACE)
    target_link_libraries(nmo_project INTERFACE nmo)
    set(NMO_PROJECT_PC_LIBS "")
else()
    add_library(nmo_project STATIC ${NMO_PROJECT_SOURCES})
    target_link_libraries(nmo_project PUBLIC nmo)
    target_include_directories(nmo_project
        PRIVATE
            ${PROJECT_SOURCE_DIR}/deps
            ${NMO_YYJSON_DIR}/src
    )
    set(NMO_PROJECT_PC_LIBS "-lnmo_project")
endif()
add_library(nmo::project ALIAS nmo_project)
set_target_properties(nmo_project PROPERTIES EXPORT_NAME project)

install(TARGETS nmo_project EXPORT libnmoTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# pkg-config: libnmo-project requires libnmo.
configure_file("${PROJECT_SOURCE_DIR}/cmake/libnmo-project.pc.in"
    "${PROJECT_BINARY_DIR}/libnmo-project.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/libnmo-project.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
