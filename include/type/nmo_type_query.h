/**
 * @file nmo_type_query.h
 * @brief Convenience helpers for class/type registry lookups.
 */

#ifndef NMO_TYPE_QUERY_H
#define NMO_TYPE_QUERY_H

#include "nmo_types.h"
#include "core/nmo_guid.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief Get class name from class ID using the context's type registry.
 *
 * Falls back to inherited class descriptors when direct registration is absent.
 * @ownership borrowed
 */
NMO_API const char *nmo_type_query_class_name_from_id(const nmo_type_registry_t *registry, nmo_class_id_t class_id);

/**
 * @brief Get class ID from class name.
 * @return Class ID, or 0 if not found.
 */
NMO_API nmo_class_id_t nmo_type_query_class_id_from_name(const nmo_type_registry_t *registry, const char *name);

/**
 * @brief Get parent class ID.
 * @return Parent class ID, or 0 if root/not found.
 */
NMO_API nmo_class_id_t nmo_type_query_class_get_parent(const nmo_type_registry_t *registry, nmo_class_id_t class_id);

/**
 * @brief Check whether @p class_id derives from @p base_id.
 */
NMO_API bool nmo_type_query_class_is_derived_from(const nmo_type_registry_t *registry,
                                                  nmo_class_id_t class_id,
                                                  nmo_class_id_t base_id);

/**
 * @brief Lookup type descriptor by type GUID.
 * @ownership borrowed
 */
NMO_API const nmo_type_descriptor_t *nmo_type_query_find_by_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Lookup type descriptor by class ID.
 * @ownership borrowed
 */
NMO_API const nmo_type_descriptor_t *nmo_type_query_find_by_class_id(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Check whether an object derives from a base type GUID.
 */
NMO_API bool nmo_type_query_object_is_derived_from_guid(
    const nmo_type_registry_t *registry,
    const nmo_object_t *obj,
    nmo_guid_t base_guid);

/**
 * @brief Get ancestor state pointer for object as viewed as @p base_guid type.
 * @ownership borrowed
 */
NMO_API void *nmo_type_query_object_get_ancestor_state_by_guid(
    const nmo_type_registry_t *registry,
    nmo_object_t *obj,
    nmo_guid_t base_guid);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_QUERY_H */
