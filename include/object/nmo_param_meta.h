/**
 * @file nmo_param_meta.h
 * @brief Parameter metadata for Virtools file format
 *
 * This file provides the parameter metadata system used in the old schema system.
 * It defines structures for parameter types, operations, and metadata.
 */

#ifndef NMO_PARAM_META_H
#define NMO_PARAM_META_H

#include "nmo_types.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_param_meta nmo_param_meta_t;
typedef struct nmo_param_meta_registry nmo_param_meta_registry_t;

/**
 * @brief Parameter operation type
 */
typedef enum nmo_param_operation {
    NMO_PARAMOP_NONE = 0,
    NMO_PARAMOP_SIMPLE,
    NMO_PARAMOP_STRUCT,
    NMO_PARAMOP_ARRAY,
    NMO_PARAMOP_CUSTOM
} nmo_param_operation_t;

/**
 * @brief Parameter metadata
 */
struct nmo_param_meta {
    nmo_guid_t guid;                    /**< Parameter type GUID */
    const char *name;                   /**< Parameter type name */
    nmo_param_operation_t operation;    /**< Operation type */
    uint32_t flags;                     /**< Parameter flags */
    size_t size;                        /**< Size in bytes */
    const char *ui_name;                /**< Optional UI display name */
    const char *description;            /**< Optional description */
};

/**
 * @brief Get parameter metadata registry
 * @return Registry instance
 */
nmo_param_meta_registry_t *nmo_get_param_meta_registry(void);

/**
 * @brief Find parameter metadata by GUID
 * @param registry Registry instance
 * @param guid Parameter GUID
 * @return Parameter metadata or NULL if not found
 */
const nmo_param_meta_t *nmo_param_meta_find_by_guid(
    nmo_param_meta_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Find parameter metadata by name
 * @param registry Registry instance
 * @param name Parameter name
 * @return Parameter metadata or NULL if not found
 */
const nmo_param_meta_t *nmo_param_meta_find_by_name(
    nmo_param_meta_registry_t *registry,
    const char *name);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARAM_META_H */
