#include "lua/nmo_lua_behavior.h"
#include "lua/nmo_lua_bindings.h"

/* The address of this object is the attachment key; its value is never read. */
static const char lua_runtime_attachment_key = 0;

static void lua_runtime_attachment_dispose(void *data)
{
    nmo_lua_runtime_destroy((nmo_lua_runtime_t *)data);
}

NMO_API nmo_lua_runtime_t *nmo_behavior_execution_lua_runtime(
    nmo_behavior_execution_t *execution)
{
    if (execution == NULL) {
        return NULL;
    }

    nmo_lua_runtime_t *runtime = (nmo_lua_runtime_t *)
        nmo_behavior_execution_get_attachment(execution, &lua_runtime_attachment_key);
    if (runtime != NULL) {
        return runtime;
    }

    runtime = nmo_lua_runtime_create();
    if (runtime == NULL) {
        return NULL;
    }
    if (nmo_lua_register_platform_bindings(runtime) != NMO_OK ||
        nmo_behavior_execution_set_attachment(
            execution, &lua_runtime_attachment_key, runtime,
            lua_runtime_attachment_dispose) != NMO_OK) {
        nmo_lua_runtime_destroy(runtime);
        return NULL;
    }
    return runtime;
}
