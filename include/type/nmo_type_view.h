#ifndef NMO_TYPE_VIEW_H
#define NMO_TYPE_VIEW_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t nmo_type_id_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/*
 * Stable read-only metadata view for binding-facing callers. This avoids
 * exposing registry-owned descriptor pointers as the primary consumer contract.
 */
#define NMO_TYPE_VIEW_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_TYPE_VIEW_METADATA_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef struct nmo_type_view {
    nmo_guid_t guid;
    nmo_guid_t base_guid;
    nmo_type_id_t type_id;
    nmo_class_id_t class_id;
    uint16_t category;
    uint16_t flags;
    uint32_t size;
    uint32_t alignment;
    size_t field_count;
    const char *name;
    const char *description;
    bool has_reflection;
    bool ui_visible;
} nmo_type_view_t;

/**
 * @brief Populate a stable type metadata snapshot by GUID.
 *
 * The returned strings are borrowed from the registry. No descriptor pointer is
 * exposed to the caller.
 */
NMO_API nmo_status_t nmo_type_view_from_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid,
    nmo_type_view_t *out_view);

/**
 * @brief Populate a stable type metadata snapshot by class ID.
 */
NMO_API nmo_status_t nmo_type_view_from_class_id(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_type_view_t *out_view);

/**
 * @brief Populate a stable type metadata snapshot by type ID.
 */
NMO_API nmo_status_t nmo_type_view_from_type_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_type_view_t *out_view);

/**
 * @brief Populate a stable type metadata snapshot for one object.
 *
 * Prefers the object's explicit type GUID when present, then falls back to the
 * inherited class-id lookup used for untyped objects.
 */
NMO_API nmo_status_t nmo_type_view_from_object(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object,
    nmo_type_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_VIEW_H */
