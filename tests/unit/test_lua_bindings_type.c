#include "test_framework.h"

#include "core/nmo_guid.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_runtime.h"
#include "type/nmo_type_guids.h"

#include <stdio.h>

static void format_guid_literal(nmo_guid_t guid, char *buffer, size_t buffer_size)
{
    ASSERT_TRUE(buffer != NULL);
    ASSERT_TRUE(buffer_size >= 32);
    ASSERT_TRUE(nmo_guid_format(guid, buffer, buffer_size) > 0);
}

static void assert_lua_ok(nmo_lua_runtime_t *runtime, const char *script)
{
    nmo_status_t status = nmo_lua_runtime_execute_string(runtime, script);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        if (message != NULL) {
            fprintf(stderr, "Lua failure: %s\n", message);
        }
    }
    ASSERT_EQ(NMO_OK, status);
}

TEST(lua_bindings_type, type_module_exposes_full_view_family)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    char float_guid[32];
    char script[2048];
    ASSERT_NOT_NULL(runtime);

    format_guid_literal(CKPGUID_FLOAT, float_guid, sizeof(float_guid));
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(
        script,
        sizeof(script),
        "local session = require('nmo.session')\n"
        "local object = require('nmo.object')\n"
        "local type_mod = require('nmo.type')\n"
        "local ctx = session.create_context()\n"
        "local s = session.load_file(ctx, '%s')\n"
        "local cam = session.find_object_by_name(s, 'InGameCam')\n"
        "local by_guid = type_mod.view_from_guid(ctx, '%s')\n"
        "assert(by_guid.guid == '%s')\n"
        "local by_object = type_mod.view_from_object(cam)\n"
        "local by_class = type_mod.view_from_class_id(ctx, object.class_id(cam))\n"
        "local by_type_id = type_mod.view_from_type_id(ctx, by_object.type_id)\n"
        "assert(by_class.class_id == by_object.class_id)\n"
        "assert(by_type_id.type_id == by_object.type_id)\n"
        "assert(by_type_id.name == by_object.name)\n"
        "assert(type_mod.class_name(ctx, by_object.class_id) == by_object.name)\n"
        "assert(type_mod.class_id(ctx, by_object.name) == by_object.class_id)\n"
        "assert(type_mod.guid_from_name(ctx, by_guid.name) == '%s')\n",
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        float_guid,
        float_guid,
        float_guid);
    assert_lua_ok(runtime, script);

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_type, type_module_roundtrips_values_and_handles_errors)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    char float_guid[32];
    char script[1024];
    ASSERT_NOT_NULL(runtime);

    format_guid_literal(CKPGUID_FLOAT, float_guid, sizeof(float_guid));
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(
        script,
        sizeof(script),
        "local session = require('nmo.session')\n"
        "local type_mod = require('nmo.type')\n"
        "local ctx = session.create_context()\n"
        "assert(type_mod.value_roundtrip(ctx, '%s', '1.5') == '1.5')\n"
        "assert(type_mod.view_from_type_id(ctx, 999999) == nil)\n"
        "local ok, err = pcall(function()\n"
        "  type_mod.view_from_guid(ctx, 'not-a-guid')\n"
        "end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'GUID', 1, true) ~= nil)\n",
        float_guid);
    assert_lua_ok(runtime, script);

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_type, type_module_exposes_full_view_family);
    REGISTER_TEST(lua_bindings_type, type_module_roundtrips_values_and_handles_errors);
TEST_MAIN_END()
