# CMake wrapper around the official Lua source distribution
# (https://www.lua.org/ftp/). The upstream tarball ships only a Makefile, so
# this directory provides the build description. The Lua sources themselves are
# either fetched by deps/CMakeLists.txt via FetchContent or supplied locally.
#
# Inputs (set by the including project before add_subdirectory):
#   NMO_LUA_SOURCE_ROOT  directory containing the upstream src/ tree
#
# Targets and variables exposed to the parent project:
#   liblua_objects            OBJECT library: interpreter core + standard libs
#   lua::lua                  STATIC (or SHARED) library built from those objects
#   LUA_RESOLVED_SOURCE_ROOT  cache variable; headers live in ${...}/src
cmake_minimum_required(VERSION 3.15)
project(lua C)

option(LUA_BUILD_SHARED   "Build Lua as a shared library" OFF)
option(LUA_BUILD_STATIC   "Build Lua as a static library" ON)
option(LUA_BUILD_TESTS    "Build Lua tests (not shipped in the tarball)" OFF)
option(LUA_BUILD_READLINE "Link the standalone interpreter against readline" OFF)
option(LUA_BUILD_LIBEDIT  "Link the standalone interpreter against libedit" OFF)
option(LUA_BUILD_BINARIES "Build the lua and luac executables" OFF)

if(NOT DEFINED NMO_LUA_SOURCE_ROOT)
    set(NMO_LUA_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(NOT EXISTS "${NMO_LUA_SOURCE_ROOT}/src/lua.h")
    message(FATAL_ERROR
        "Lua sources not found: expected ${NMO_LUA_SOURCE_ROOT}/src/lua.h")
endif()

set(LUA_RESOLVED_SOURCE_ROOT "${NMO_LUA_SOURCE_ROOT}" CACHE INTERNAL
    "Root of the Lua source tree used by libnmo (headers live in src/)")

set(LUA_SRC_DIR "${NMO_LUA_SOURCE_ROOT}/src")

set(LUA_CORE_SOURCES
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)

set(LUA_LIB_SOURCES
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/lutf8lib.c
)

add_library(liblua_objects OBJECT ${LUA_CORE_SOURCES} ${LUA_LIB_SOURCES})
target_include_directories(liblua_objects PUBLIC ${LUA_SRC_DIR})
set_target_properties(liblua_objects PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ${LUA_BUILD_SHARED}
)

# Platform configuration mirrors the upstream Makefile targets.
if(APPLE)
    target_compile_definitions(liblua_objects PUBLIC LUA_USE_MACOSX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(liblua_objects PUBLIC LUA_USE_LINUX)
elseif(UNIX)
    target_compile_definitions(liblua_objects PUBLIC LUA_USE_POSIX LUA_USE_DLOPEN)
endif()
if(MSVC)
    target_compile_definitions(liblua_objects PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(LUA_BUILD_SHARED)
    add_library(lua SHARED $<TARGET_OBJECTS:liblua_objects>)
    if(WIN32)
        target_compile_definitions(liblua_objects PUBLIC LUA_BUILD_AS_DLL)
    endif()
else()
    add_library(lua STATIC $<TARGET_OBJECTS:liblua_objects>)
endif()
target_include_directories(lua PUBLIC ${LUA_SRC_DIR})
target_link_libraries(lua PUBLIC liblua_objects)
if(UNIX AND NOT APPLE)
    target_link_libraries(lua PUBLIC m ${CMAKE_DL_LIBS})
elseif(APPLE)
    target_link_libraries(lua PUBLIC m)
endif()
add_library(lua::lua ALIAS lua)

if(LUA_BUILD_BINARIES)
    add_executable(lua_interpreter ${LUA_SRC_DIR}/lua.c)
    set_target_properties(lua_interpreter PROPERTIES OUTPUT_NAME lua)
    target_link_libraries(lua_interpreter PRIVATE lua::lua)
    if(LUA_BUILD_READLINE)
        target_compile_definitions(lua_interpreter PRIVATE LUA_USE_READLINE)
        target_link_libraries(lua_interpreter PRIVATE readline)
    elseif(LUA_BUILD_LIBEDIT)
        target_compile_definitions(lua_interpreter PRIVATE LUA_USE_READLINE)
        target_link_libraries(lua_interpreter PRIVATE edit)
    endif()

    add_executable(luac ${LUA_SRC_DIR}/luac.c)
    target_link_libraries(luac PRIVATE lua::lua)
endif()
