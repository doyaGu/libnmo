#include "lua/nmo_lua_runtime.h"

#include <limits.h>
#include <stdlib.h>

#include "lua/nmo_lua_module.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

struct nmo_lua_runtime {
    lua_State *state;
};

static int nmo_lua_runtime_traceback(lua_State *state)
{
    const char *message = lua_tostring(state, 1);
    if (message == NULL) {
        if (!lua_isnoneornil(state, 1) && luaL_callmeta(state, 1, "__tostring")) {
            message = lua_tostring(state, -1);
        } else {
            lua_pushliteral(state, "(error object is not a string)");
            message = lua_tostring(state, -1);
        }
    }

    luaL_traceback(state, state, message, 1);
    return 1;
}

nmo_lua_runtime_t *nmo_lua_runtime_create(void)
{
    lua_State *state = luaL_newstate();
    if (state == NULL) {
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Failed to allocate Lua state");
        return NULL;
    }

    nmo_lua_runtime_t *runtime =
        (nmo_lua_runtime_t *)calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        lua_close(state);
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                           "Failed to allocate Lua runtime wrapper");
        return NULL;
    }

    luaL_openlibs(state);
    runtime->state = state;
    nmo_last_error_clear();
    return runtime;
}

void nmo_lua_runtime_destroy(nmo_lua_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    if (runtime->state != NULL) {
        lua_close(runtime->state);
    }

    free(runtime);
}

nmo_status_t nmo_lua_runtime_execute_string(nmo_lua_runtime_t *runtime,
                                            const char *chunk)
{
    if (runtime == NULL || runtime->state == NULL || chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua runtime and chunk must be non-null");
    }

    lua_settop(runtime->state, 0);
    lua_pushcfunction(runtime->state, nmo_lua_runtime_traceback);
    int traceback_index = lua_gettop(runtime->state);

    if (luaL_loadstring(runtime->state, chunk) != LUA_OK) {
        const char *message = lua_tostring(runtime->state, -1);
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                           "Lua load failed: %s",
                           (message != NULL) ? message : "unknown load error");
        lua_settop(runtime->state, 0);
        return NMO_ERR_VALIDATION_FAILED;
    }

    if (lua_pcall(runtime->state, 0, LUA_MULTRET, traceback_index) != LUA_OK) {
        const char *message = lua_tostring(runtime->state, -1);
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                           "Lua execution failed: %s",
                           (message != NULL) ? message : "unknown execution error");
        lua_settop(runtime->state, 0);
        return NMO_ERR_VALIDATION_FAILED;
    }

    lua_settop(runtime->state, 0);
    NMO_RETURN_OK();
}

/*
 * Run a script as the standalone interpreter does: `arg` holds `script_name`
 * at 0 and `args` from 1, and the chunk gets `args` as its `...`. `chunk_name`
 * names the chunk in messages; the file is read from `path`, or the text from
 * `text` when `path` is NULL.
 */
static nmo_status_t nmo_lua_runtime_execute_script(nmo_lua_runtime_t *runtime,
                                                   const char *script_name,
                                                   const char *path,
                                                   const char *text,
                                                   size_t text_size,
                                                   const char *const *args,
                                                   size_t arg_count)
{
    if (runtime == NULL || runtime->state == NULL || script_name == NULL ||
        (path == NULL && text == NULL) || (args == NULL && arg_count > 0) ||
        arg_count > (size_t)INT_MAX - 1) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua runtime and script must be non-null");
    }

    lua_State *state = runtime->state;
    lua_settop(state, 0);
    lua_pushcfunction(state, nmo_lua_runtime_traceback);
    int traceback_index = lua_gettop(state);

    int load_status = LUA_OK;
    if (path != NULL) {
        load_status = luaL_loadfilex(state, path, "t");
    } else {
        lua_pushfstring(state, "=%s", script_name);
        load_status = luaL_loadbufferx(state, text, text_size, lua_tostring(state, -1), "t");
        lua_remove(state, -2);
    }
    if (load_status != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        nmo_status_t status = load_status == LUA_ERRFILE ? NMO_ERR_CANT_OPEN_FILE
                                                         : NMO_ERR_VALIDATION_FAILED;
        NMO_SET_LAST_ERROR(status, NMO_SEVERITY_ERROR, "Lua load failed: %s",
                           (message != NULL) ? message : "unknown load error");
        lua_settop(state, 0);
        return status;
    }

    lua_createtable(state, (int)arg_count, 1);
    lua_pushstring(state, script_name);
    lua_rawseti(state, -2, 0);
    for (size_t i = 0; i < arg_count; i++) {
        lua_pushstring(state, args[i]);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setglobal(state, "arg");
    if (!lua_checkstack(state, (int)arg_count + 1)) {
        lua_settop(state, 0);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Too many arguments for the Lua script");
    }
    for (size_t i = 0; i < arg_count; i++) {
        lua_pushstring(state, args[i]);
    }

    if (lua_pcall(state, (int)arg_count, 0, traceback_index) != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                           "Lua execution failed: %s",
                           (message != NULL) ? message : "unknown execution error");
        lua_settop(state, 0);
        return NMO_ERR_VALIDATION_FAILED;
    }

    lua_settop(state, 0);
    NMO_RETURN_OK();
}

nmo_status_t nmo_lua_runtime_execute_file(nmo_lua_runtime_t *runtime,
                                          const char *path,
                                          const char *const *args,
                                          size_t arg_count)
{
    return nmo_lua_runtime_execute_script(runtime, path, path, NULL, 0, args, arg_count);
}

nmo_status_t nmo_lua_runtime_execute_buffer(nmo_lua_runtime_t *runtime,
                                            const char *name,
                                            const char *text,
                                            size_t text_size,
                                            const char *const *args,
                                            size_t arg_count)
{
    if (text == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua script text must be non-null");
    }
    return nmo_lua_runtime_execute_script(runtime, name, NULL, text, text_size, args,
                                          arg_count);
}

nmo_status_t nmo_lua_runtime_register_module(nmo_lua_runtime_t *runtime,
                                             const nmo_lua_module_t *module)
{
    if (runtime == NULL || runtime->state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua runtime must be non-null");
    }

    return nmo_lua_module_register(runtime->state, module);
}

lua_State *nmo_lua_runtime_state(nmo_lua_runtime_t *runtime)
{
    return runtime != NULL ? runtime->state : NULL;
}
