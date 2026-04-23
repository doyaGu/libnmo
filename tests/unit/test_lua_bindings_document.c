#include "test_framework.h"

#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_runtime.h"

#include <stdio.h>

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

TEST(lua_bindings_document, context_and_document_modules_expose_canonical_file_workflow)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "assert(doc ~= nil)\n"
        "local stats = document.stats(doc)\n"
        "assert(type(stats) == 'table')\n"
        "assert(stats.objects.total_count == 18)\n"
        "assert(type(stats.references.total_references) == 'number')\n"
        "document.save_file(doc, 'test_lua_document_save_out.nmo')\n");

    (void)remove("test_lua_document_save_out.nmo");
    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_document, document_module_exposes_compare_and_no_app_module)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local ctx = context.create()\n"
        "local left = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "local right = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "local cmp = document.compare(left, right, document.compare_flags.default)\n"
        "assert(cmp.match == true)\n"
        "assert(cmp.diff_count == 0)\n"
        "local ok, err = pcall(function() require('nmo.app') end)\n"
        "assert(ok == false)\n"
        "assert(type(err) == 'string')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_document,
                  context_and_document_modules_expose_canonical_file_workflow);
    REGISTER_TEST(lua_bindings_document,
                  document_module_exposes_compare_and_no_app_module);
TEST_MAIN_END()
