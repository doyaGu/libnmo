/**
 * @file nmo_path.h
 * @brief Path manipulation helpers.
 */

#ifndef NMO_PATH_H
#define NMO_PATH_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the last path component (after '/' or '\\').
 *
 * @param path Input path, may be NULL.
 * @return Pointer into @p path (or empty string literal for NULL input).
 * @ownership borrowed
 */
NMO_API const char *nmo_path_basename(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PATH_H */
