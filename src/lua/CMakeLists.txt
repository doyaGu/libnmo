# Lua component: the embedded Lua runtime, the nmo bindings and the Lua-backed
# part of the behavior execution API. It builds on the core library (nmo); the
# core does not depend on it. Consumers link nmo::lua (target nmo_lua).

set(NMO_LUA_SOURCES
    lua_behavior_execution.c
    lua_bindings_core.c
    lua_bindings_context.c
    lua_bindings_behavior.c
    lua_bindings_plan.c
    lua_bindings_document.c
    lua_bindings_format.c
    lua_bindings_object.c
    lua_bindings_runtime.c
    lua_bindings_session.c
    lua_bindings_type.c
    lua_bindings_workspace.c
    lua_edit_report.c
    lua_fold_map_parser.c
    lua_handles.c
    lua_module.c
    lua_pushers_generated.c
    lua_runtime.c
)
list(TRANSFORM NMO_LUA_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

if(NMO_BUILD_SHARED)
    # A shared build is a single library: the component compiles into nmo, and
    # nmo_lua is an interface target so consumers link it the same way.
    target_sources(nmo PRIVATE ${NMO_LUA_SOURCES})
    target_link_libraries(nmo PRIVATE lua::lua)
    target_include_directories(nmo PUBLIC $<BUILD_INTERFACE:${LUA_RESOLVED_SOURCE_ROOT}/src>)
    add_library(nmo_lua INTERFACE)
    target_link_libraries(nmo_lua INTERFACE nmo)
    set(NMO_LUA_PC_LIBS "")
else()
    add_library(nmo_lua STATIC ${NMO_LUA_SOURCES} $<TARGET_OBJECTS:liblua_objects>)
    target_link_libraries(nmo_lua PUBLIC nmo)
    target_include_directories(nmo_lua PUBLIC $<BUILD_INTERFACE:${LUA_RESOLVED_SOURCE_ROOT}/src>)
    set(NMO_LUA_PC_LIBS "-lnmo_lua")
endif()
add_library(nmo::lua ALIAS nmo_lua)
set_target_properties(nmo_lua PROPERTIES EXPORT_NAME lua)

install(TARGETS nmo_lua EXPORT libnmoTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# The Lua headers of include/lua/ include "lua.h". Installing Lua's public
# headers beside them lets that same-directory lookup succeed without an extra
# include path for consumers.
install(FILES
    "${LUA_RESOLVED_SOURCE_ROOT}/src/lua.h"
    "${LUA_RESOLVED_SOURCE_ROOT}/src/luaconf.h"
    "${LUA_RESOLVED_SOURCE_ROOT}/src/lualib.h"
    "${LUA_RESOLVED_SOURCE_ROOT}/src/lauxlib.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lua
)

# pkg-config: libnmo-lua requires libnmo.
configure_file("${PROJECT_SOURCE_DIR}/cmake/libnmo-lua.pc.in"
    "${PROJECT_BINARY_DIR}/libnmo-lua.pc" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/libnmo-lua.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Lua ships its license only as the notice at the end of lua.h.
file(READ "${LUA_RESOLVED_SOURCE_ROOT}/src/lua.h" _nmo_lua_header)
string(FIND "${_nmo_lua_header}" "* Copyright (C)" _nmo_lua_begin)
string(FIND "${_nmo_lua_header}" "SOFTWARE.\n" _nmo_lua_end REVERSE)
if(_nmo_lua_begin EQUAL -1 OR _nmo_lua_end LESS _nmo_lua_begin)
    message(FATAL_ERROR "Could not find the license notice in lua.h")
endif()
math(EXPR _nmo_lua_length "${_nmo_lua_end} + 9 - ${_nmo_lua_begin}")
string(SUBSTRING "${_nmo_lua_header}" ${_nmo_lua_begin} ${_nmo_lua_length} _nmo_lua_license)
string(REGEX REPLACE "(^|\n)\\* ?" "\\1" _nmo_lua_license "${_nmo_lua_license}")
file(WRITE "${PROJECT_BINARY_DIR}/lua-LICENSE" "${_nmo_lua_license}\n")
install(FILES "${PROJECT_BINARY_DIR}/lua-LICENSE" DESTINATION ${NMO_THIRD_PARTY_DOCDIR})
