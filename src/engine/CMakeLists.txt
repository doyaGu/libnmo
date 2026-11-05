# Engine component: the behavior engine, which runs the behavior graphs of a
# document frame by frame. It builds on the core library (nmo); the core does
# not depend on it. Consumers link nmo::engine (target nmo_engine).

# core/: the CK2 runtime the graphs run in, its managers and the manager hooks
# the block packs (src/bb) add theirs through. The engine registers no blocks.
set(NMO_ENGINE_SOURCES
    core/affine.c
    core/array.c
    core/attributes.c
    core/block.c
    core/billboard.c
    core/build.c
    core/copy.c
    core/curve.c
    core/curve_properties.c
    core/curve_points.c
    core/curvepoint_properties.c
    core/engine.c
    core/host.c
    core/input.c
    core/manager.c
    core/animation.c
    core/animation_clock.c
    core/animation_binding.c
    core/animation_blend.c
    core/animation_merge.c
    core/character.c
    core/kinematic.c
    core/keyed_animation.c
    core/morph.c
    core/media.c
    core/movie.c
    core/message.c
    core/module.c
    core/operations.c
    core/param.c
    core/param_text.c
    core/portal.c
    core/place_fit.c
    core/projection.c
    core/render.c
    core/run.c
    core/scene.c
    core/schedule.c
    core/transform.c
    core/transition.c
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
