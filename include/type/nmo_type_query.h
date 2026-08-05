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

/*
 * Scalar class/name relationship lookups are Tier 1. Borrowed descriptor and
 * state-pointer views remain advanced; ordinary consumers should prefer
 * nmo_type_view_*() when they need stable metadata snapshots instead of
 * registry-owned descriptor pointers.
 */
#define NMO_TYPE_QUERY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_MIXED_TIER
#define NMO_TYPE_QUERY_SCALAR_LOOKUP_API_TIER NMO_API_TIER_STABLE_CONSUMER
#define NMO_TYPE_QUERY_DESCRIPTOR_VIEW_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_TYPE_QUERY_STATE_VIEW_API_TIER NMO_API_TIER_ADVANCED_C

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
 * Ordinary consumers should prefer nmo_type_view_from_guid() for a stable
 * metadata snapshot.
 * @ownership borrowed
 */
NMO_API const nmo_type_descriptor_t *nmo_type_query_find_by_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Lookup type descriptor by class ID.
 * Ordinary consumers should prefer nmo_type_view_from_class_id() for a stable
 * metadata snapshot.
 * @ownership borrowed
 */
NMO_API const nmo_type_descriptor_t *nmo_type_query_find_by_class_id(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Lookup the effective type descriptor for an object.
 *
 * An explicit object type GUID takes precedence. If it is absent or is not
 * registered, the object's class ID is resolved through the inherited class
 * lookup used by built-in schemas.
 * @ownership borrowed
 */
NMO_API const nmo_type_descriptor_t *nmo_type_query_find_for_object(
    const nmo_type_registry_t *registry,
    const nmo_object_t *obj);

/**
 * @brief Check whether an object's effective type derives from a base class.
 */
NMO_API bool nmo_type_query_object_is_derived_from_class(
    const nmo_type_registry_t *registry,
    const nmo_object_t *obj,
    nmo_class_id_t base_class_id);

/**
 * @brief Check whether an object derives from a base type GUID.
 */
NMO_API bool nmo_type_query_object_is_derived_from_guid(
    const nmo_type_registry_t *registry,
    const nmo_object_t *obj,
    nmo_guid_t base_guid);

/**
 * @brief Get ancestor state pointer for object as viewed as @p base_guid type.
 * This is an advanced state-layout API. Ordinary consumers should prefer
 * nmo_type_view_from_object() plus explicit object/value accessors where
 * possible instead of retaining raw ancestor-state pointers.
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
