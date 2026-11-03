# Optional physics provider on libivp (the C17 port of Ipion's IVP): the
# simulation behind the engine's physics blocks. Built when NMO_IVP_SOURCE_DIR
# names a libivp source tree; libivp itself stays outside libnmo.

# libivp builds with its own flags, not the core's warnings-as-errors
get_directory_property(_nmo_ivp_options COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
set(IVP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(IVP_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory("${NMO_IVP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/libivp" EXCLUDE_FROM_ALL)

# Report the selected libivp policy, including values migrated from old caches.
if(DEFINED IVP_STABLE_INTERNAL_STATE)
    message(STATUS "  libivp stable internal state: ${IVP_STABLE_INTERNAL_STATE}")
endif()

add_library(nmo_physics_ivp STATIC "${CMAKE_CURRENT_SOURCE_DIR}/physics_ivp.c")
target_compile_options(nmo_physics_ivp PRIVATE ${_nmo_ivp_options})
target_link_libraries(nmo_physics_ivp PUBLIC nmo_bb_physics PRIVATE ivp)
target_compile_definitions(nmo_physics_ivp PUBLIC NMO_HAS_PHYSICS_IVP=1)
add_library(nmo::physics_ivp ALIAS nmo_physics_ivp)
