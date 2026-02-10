#include "core/nmo_ownership.h"

#include <stdlib.h>
#include <string.h>

#ifdef NMO_ENABLE_DEBUG_ASSERTS
static int g_nmo_ownership_checks_initialized = 0;
static bool g_nmo_ownership_checks_enabled = true;

static bool nmo_ownership_parse_env_flag(const char *value, bool default_value) {
    if (value == NULL || value[0] == '\0') {
        return default_value;
    }

    if (strcmp(value, "0") == 0 ||
        strcmp(value, "false") == 0 ||
        strcmp(value, "FALSE") == 0 ||
        strcmp(value, "off") == 0 ||
        strcmp(value, "OFF") == 0 ||
        strcmp(value, "no") == 0 ||
        strcmp(value, "NO") == 0) {
        return false;
    }

    return true;
}

static void nmo_ownership_initialize_from_env_if_needed(void) {
    if (g_nmo_ownership_checks_initialized) {
        return;
    }

    const char *env = getenv("NMO_OWNERSHIP_CHECKS");
    g_nmo_ownership_checks_enabled = nmo_ownership_parse_env_flag(env, true);
    g_nmo_ownership_checks_initialized = 1;
}
#endif

bool nmo_ownership_checks_enabled(void) {
#ifdef NMO_ENABLE_DEBUG_ASSERTS
    nmo_ownership_initialize_from_env_if_needed();
    return g_nmo_ownership_checks_enabled;
#else
    return false;
#endif
}

void nmo_ownership_set_checks_enabled(bool enabled) {
#ifdef NMO_ENABLE_DEBUG_ASSERTS
    g_nmo_ownership_checks_enabled = enabled;
    g_nmo_ownership_checks_initialized = 1;
#else
    (void)enabled;
#endif
}

void nmo_ownership_reload_checks_from_env(void) {
#ifdef NMO_ENABLE_DEBUG_ASSERTS
    g_nmo_ownership_checks_initialized = 0;
    nmo_ownership_initialize_from_env_if_needed();
#endif
}
