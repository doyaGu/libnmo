#ifndef NMO_LUA_H
#define NMO_LUA_H

/**
 * @file nmo_lua.h
 * @brief Umbrella header of the Lua component (library nmo_lua).
 *
 * The Lua runtime, its bindings and the Lua-backed part of the behavior
 * execution API live outside the core library. Link nmo_lua in addition to
 * nmo (CMake: nmo::lua, pkg-config: libnmo-lua). These headers include Lua's
 * own lua.h, which is installed beside them in include/lua/.
 */

#include "nmo.h"
#include "nmo_edit.h"

#include "lua/nmo_lua_module.h"
#include "lua/nmo_lua_runtime.h"
#include "lua/nmo_lua_behavior.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_handles.h"
#include "lua/nmo_lua_script.h"

#endif /* NMO_LUA_H */
