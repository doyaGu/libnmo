/**
 * @file main.c
 * @brief Links an installed libnmo and exercises the core, Lua and project components
 */

#include "nmo.h"
#include "nmo_lua.h"
#include "nmo_project.h"

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

    nmo_project_plan_t *plan = NULL;
    if (nmo_project_plan_create(&plan) != NMO_OK || plan == NULL) {
        fprintf(stderr, "nmo_project_plan_create failed\n");
        return 1;
    }
    nmo_project_plan_destroy(plan);

    printf("libnmo %s\n", nmo_version());
    return 0;
}
