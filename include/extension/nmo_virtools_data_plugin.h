/**
 * @file nmo_virtools_data_plugin.h
 * @brief Built-in Virtools data extension plugin
 *
 * Provides access to the static plugin descriptor for the built-in
 * Virtools data extension.  Registered by context.c during creation.
 */

#ifndef NMO_VIRTOOLS_DATA_PLUGIN_H
#define NMO_VIRTOOLS_DATA_PLUGIN_H

#include "extension/nmo_extension_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the Virtools data plugin descriptor
 *
 * @return Static plugin descriptor (process lifetime)
 * @ownership static
 */
const nmo_extension_plugin_t *nmo_virtools_data_plugin_get(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VIRTOOLS_DATA_PLUGIN_H */
