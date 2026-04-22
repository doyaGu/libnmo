#include "test_framework.h"
#include "core/nmo_error.h"
#include "lua/nmo_lua_module.h"
#include "lua/nmo_lua_runtime.h"

static int open_test_module(lua_State *state)
{
    lua_createtable(state, 0, 1);
    lua_pushinteger(state, 42);
    lua_setfield(state, -2, "answer");
    return 1;
}

TEST(lua_runtime, creates_executes_and_destroys_runtime) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(runtime,
                                             "local value = 40 + 2\n"
                                             "assert(value == 42)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_runtime, registers_preload_module_for_require) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    nmo_lua_module_t module = {
        .name = "nmo_test",
        .open_fn = open_test_module
    };

    ASSERT_EQ(NMO_OK, nmo_lua_runtime_register_module(runtime, &module));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(runtime,
                                             "local mod = require('nmo_test')\n"
                                             "assert(mod.answer == 42)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_runtime, captures_traceback_on_runtime_error) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_lua_runtime_execute_string(runtime,
                                             "local function inner()\n"
                                             "  error('boom from lua')\n"
                                             "end\n"
                                             "local function outer()\n"
                                             "  inner()\n"
                                             "end\n"
                                             "outer()\n"));

    const char *message = nmo_last_error_message();
    ASSERT_NOT_NULL(message);
    ASSERT_TRUE(strstr(message, "boom from lua") != NULL);
    ASSERT_TRUE(strstr(message, "stack traceback") != NULL);

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_runtime, creates_executes_and_destroys_runtime);
    REGISTER_TEST(lua_runtime, registers_preload_module_for_require);
    REGISTER_TEST(lua_runtime, captures_traceback_on_runtime_error);
TEST_MAIN_END()
