/**
 * @file param_type_table.h
 * @brief Parameter type table for old schema system
 *
 * This file provides the parameter type lookup table used in the old schema system.
 */

#ifndef NMO_PARAM_TYPE_TABLE_H
#define NMO_PARAM_TYPE_TABLE_H

#include "object/nmo_param_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get parameter type table
 * @param count Output parameter count
 * @return Array of parameter metadata
 */
const nmo_param_meta_t *nmo_get_param_type_table(size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARAM_TYPE_TABLE_H */
