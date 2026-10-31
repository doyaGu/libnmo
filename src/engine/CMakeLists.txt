# Engine component: the behavior engine, which runs the behavior graphs of a
# document frame by frame, and the built-in building block functions. It
# builds on the core library (nmo); the core does not depend on it. Consumers
# link nmo::engine (target nmo_engine).

# core/: the CK2 runtime the graphs run in and its managers. blocks/: the
# building block ports, a file for each DLL (Logics.dll by category), with the
# managers a DLL brings along.
set(NMO_ENGINE_SOURCES
    core/array.c
    core/attributes.c
    core/block.c
    core/build.c
    core/copy.c
    core/curve.c
    core/engine.c
    core/host.c
    core/input.c
    core/media.c
    core/message.c
    core/module.c
    core/operations.c
    core/param.c
    core/param_text.c
    core/render.c
    core/run.c
    core/scene.c
    core/schedule.c
    core/transform.c
    blocks/3dtransfo.c
    blocks/addons.c
    blocks/cameras.c
    blocks/collisions.c
    blocks/controllers.c
    blocks/effects.c
    blocks/interface.c
    blocks/lights.c
    blocks/materials.c
    blocks/narratives.c
    blocks/physics.c
    blocks/sounds.c
    blocks/visuals.c
    blocks/world_environments.c
    blocks/logics/array.c
    blocks/logics/attribute.c
    blocks/logics/calculator.c
    blocks/logics/groups.c
    blocks/logics/interpolator.c
    blocks/logics/loops.c
    blocks/logics/message.c
    blocks/logics/streaming.c
    blocks/logics/strings.c
    blocks/logics/test.c
    blocks/tt/database.c
    blocks/tt/gravity.c
    blocks/tt/interface_manager.c
    blocks/tt/particles.c
    blocks/tt/toolbox.c
)
list(TRANSFORM NMO_ENGINE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

if(NMO_BUILD_SHARED)
    # A shared build is a single library: the component compiles into nmo, and
    # nmo_engine is an interface target so consumers link it the same way.
    target_sources(nmo PRIVATE ${NMO_ENGINE_SOURCES})
    add_library(nmo_engine INTERFACE)
    target_link_libraries(nmo_engine INTERFACE nmo)
    set(NMO_ENGINE_PC_LIBS "")
else()
    add_library(nmo_engine STATIC ${NMO_ENGINE_SOURCES})
    target_link_libraries(nmo_engine PUBLIC nmo)
    set(NMO_ENGINE_PC_LIBS "-lnmo_engine")
endif()
add_library(nmo::engine ALIAS nmo_engine)
set_target_properties(nmo_engine PROPERTIES EXPORT_NAME engine)

install(TARGETS nmo_engine EXPORT libnmoTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# pkg-config: libnmo-engine requires libnmo.
configure_file("${PROJECT_SOURCE_DIR}/cmake/libnmo-engine.pc.in"
    "${PROJECT_BINARY_DIR}/libnmo-engine.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/libnmo-engine.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
