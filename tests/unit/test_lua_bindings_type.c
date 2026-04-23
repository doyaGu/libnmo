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

TEST(lua_bindings_type, type_module_works_with_context_and_document_scoped_objects)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    char float_guid[32];
    char script[2048];
    ASSERT_NOT_NULL(runtime);

    format_guid_literal(CKPGUID_FLOAT, float_guid, sizeof(float_guid));
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(script,
             sizeof(script),
             "local context = require('nmo.context')\n"
             "local document = require('nmo.document')\n"
             "local object = require('nmo.object')\n"
             "local type_mod = require('nmo.type')\n"
             "local ctx = context.create()\n"
             "local doc = document.load_file(ctx, '%s')\n"
             "local cam = object.find_object_by_name(doc, 'InGameCam')\n"
             "local float_view = type_mod.view_from_guid(ctx, '%s')\n"
             "assert(float_view.guid == '%s')\n"
             "local cam_view = type_mod.view_from_object(cam)\n"
             "assert(cam_view.class_id == object.class_id(cam))\n",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
             float_guid,
             float_guid);
    assert_lua_ok(runtime, script);

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_type, type_module_roundtrips_scalar_values)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    char float_guid[32];
    char int_guid[32];
    char script[1024];
    ASSERT_NOT_NULL(runtime);

    format_guid_literal(CKPGUID_FLOAT, float_guid, sizeof(float_guid));
    format_guid_literal(CKPGUID_INT, int_guid, sizeof(int_guid));
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(script,
             sizeof(script),
             "local context = require('nmo.context')\n"
             "local type_mod = require('nmo.type')\n"
             "local ctx = context.create()\n"
             "assert(type_mod.value_roundtrip(ctx, '%s', '1.5') == '1.5')\n"
             "assert(type_mod.value_roundtrip(ctx, '%s', '42') == '42')\n",
             float_guid,
             int_guid);
    assert_lua_ok(runtime, script);

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_type,
                  type_module_works_with_context_and_document_scoped_objects);
    REGISTER_TEST(lua_bindings_type,
                  type_module_roundtrips_scalar_values);
TEST_MAIN_END()
