/**
 * @file main.c
 * @brief Links an installed libnmo and exercises the context and Lua runtime
 */

#include "nmo.h"
#include "lua/nmo_lua_runtime.h"

#include <stdio.h>

int main(void) {
    const uint32_t header_version = ((uint32_t)NMO_VERSION_MAJOR << 16)
        | ((uint32_t)NMO_VERSION_MINOR << 8)
        | (uint32_t)NMO_VERSION_PATCH;
    if (nmo_version_int() != header_version) {
        fprintf(stderr, "headers do not match library %s\n", nmo_version());
        return 1;
    }

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (ctx == NULL) {
        fprintf(stderr, "nmo_context_create failed\n");
        return 1;
    }
    nmo_context_release(ctx);

    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    if (runtime == NULL) {
        fprintf(stderr, "nmo_lua_runtime_create failed\n");
        return 1;
    }
    nmo_lua_runtime_destroy(runtime);

    printf("libnmo %s\n", nmo_version());
    return 0;
}
