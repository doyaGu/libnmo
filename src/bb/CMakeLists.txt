# Block packs: the building block ports, each pack the blocks of a set of
# DLLs with the managers they bring along, a file for each DLL (Logics.dll by
# category). They build on the engine component (nmo_engine) through
# bb_sdk.h; the engine does not depend on them. A host registers the packs it
# wants on an engine (nmo_bb_<pack>_register).
#
#   nmo_bb_virtools    the stock building block DLLs (bb/nmo_bb_virtools.h)
#   nmo_bb_physics     physics_RT and the physics manager (bb/nmo_bb_physics.h)
#   nmo_bb_terratools  the TT_* DLLs and the Terratools managers (bb/nmo_bb_terratools.h)

set(NMO_BB_VIRTOOLS_SOURCES
    virtools/virtools.c
    virtools/3dtransfo.c
    virtools/addons.c
    virtools/cameras.c
    virtools/characters.c
    virtools/character_goto.c
    virtools/character_curve.c
    virtools/enhanced_character_curve.c
    virtools/animation_sync.c
    virtools/unlimited_controller.c
    virtools/collisions.c
    virtools/controllers.c
    virtools/effects.c
    virtools/interface.c
    virtools/lights.c
    virtools/materials.c
    virtools/narratives.c
    virtools/sounds.c
    virtools/visuals.c
    virtools/world_environments.c
    virtools/logics/array.c
    virtools/logics/attribute.c
    virtools/logics/calculator.c
    virtools/logics/groups.c
    virtools/logics/interpolator.c
    virtools/logics/loops.c
    virtools/logics/message.c
    virtools/logics/streaming.c
    virtools/logics/strings.c
    virtools/logics/test.c
)
set(NMO_BB_PHYSICS_SOURCES
    physics/physics.c
)
set(NMO_BB_TERRATOOLS_SOURCES
    terratools/terratools.c
    terratools/database.c
    terratools/gravity.c
    terratools/interface_manager.c
    terratools/particles.c
    terratools/toolbox.c
)

set(NMO_BB_PC_LIBS "")
foreach(_pack virtools physics terratools)
    string(TOUPPER "${_pack}" _PACK)
    set(_sources ${NMO_BB_${_PACK}_SOURCES})
    list(TRANSFORM _sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    if(NMO_BUILD_SHARED)
        # A shared build is a single library: the pack compiles into nmo, and
        # nmo_bb_<pack> is an interface target so consumers link it the same way.
        target_sources(nmo PRIVATE ${_sources})
        add_library(nmo_bb_${_pack} INTERFACE)
        target_link_libraries(nmo_bb_${_pack} INTERFACE nmo_engine)
    else()
        add_library(nmo_bb_${_pack} STATIC ${_sources})
        target_link_libraries(nmo_bb_${_pack} PUBLIC nmo_engine)
        string(APPEND NMO_BB_PC_LIBS " -lnmo_bb_${_pack}")
    endif()
    add_library(nmo::bb_${_pack} ALIAS nmo_bb_${_pack})
    set_target_properties(nmo_bb_${_pack} PROPERTIES EXPORT_NAME bb_${_pack})
    install(TARGETS nmo_bb_${_pack} EXPORT libnmoTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endforeach()
string(STRIP "${NMO_BB_PC_LIBS}" NMO_BB_PC_LIBS)

# pkg-config: libnmo-bb (the three packs) requires libnmo-engine.
configure_file("${PROJECT_SOURCE_DIR}/cmake/libnmo-bb.pc.in"
    "${PROJECT_BINARY_DIR}/libnmo-bb.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/libnmo-bb.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
