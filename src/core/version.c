#include "nmo.h"

#define NMO_STR_HELPER(x) #x
#define NMO_STR(x) NMO_STR_HELPER(x)

static const char NMO_VERSION_STRING[] =
    NMO_STR(NMO_VERSION_MAJOR) "." NMO_STR(NMO_VERSION_MINOR) "." NMO_STR(NMO_VERSION_PATCH);

const char *nmo_version(void) {
    return NMO_VERSION_STRING;
}

uint32_t nmo_version_int(void) {
    return ((uint32_t)NMO_VERSION_MAJOR << 16)
        | ((uint32_t)NMO_VERSION_MINOR << 8)
        | (uint32_t)NMO_VERSION_PATCH;
}
