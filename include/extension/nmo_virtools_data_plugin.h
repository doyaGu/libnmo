/**
 * @file nmo_virtools_data_plugin.h
 * @brief Built-in Virtools data extension plugin
 *
 * Loads Virtools parameter types, operation signatures, and BB prototypes
 * from JSON files. Registered as a static extension during context creation.
 * Reads data_dir from extension_registry user_data.
 */

#ifndef NMO_VIRTOOLS_DATA_PLUGIN_H
#define NMO_VIRTOOLS_DATA_PLUGIN_H

#include "extension/nmo_extension_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the Virtools data plugin descriptor
 * @return Static plugin descriptor (process lifetime)
 */
const nmo_extension_plugin_t *nmo_virtools_data_plugin_get(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VIRTOOLS_DATA_PLUGIN_H */
