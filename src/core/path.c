#include "core/nmo_path.h"

#include <string.h>

const char *nmo_path_basename(const char *path) {
    if (!path || !*path) {
        return "";
    }

    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *sep = slash;
    if (!sep || (bslash && bslash > sep)) {
        sep = bslash;
    }
    return sep ? (sep + 1) : path;
}
