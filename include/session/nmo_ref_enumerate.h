/**
 * @file nmo_ref_enumerate.h
 * @brief Reference enumeration using type system metadata
 *
 * Phase 4.1: Enumerates object references using the type registry and
 * reflection metadata. This avoids per-class hard-coded enumerators and
 * allows plugin-defined types to participate automatically.
 *
 * ARCHITECTURE NOTE:
 * This module uses nmo_ref_kind_t from nmo_ref_graph.h (Session layer).
 * The Type layer provides generic enumeration via nmo_type_ref_visitor_fn
 * with opaque uint32_t ref_kind. This layer provides concrete semantics.
 */

#ifndef NMO_REF_ENUMERATE_H
#define NMO_REF_ENUMERATE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "session/nmo_ref_graph.h"  /* For nmo_ref_kind_t (Session layer) */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/**
 * @brief Reference visitor callback (Session layer)
 *
 * Called for each reference found by an enumerator.
 * Uses nmo_ref_kind_t from this layer for semantic meaning.
 *
 * @param user_data User-provided context
 * @param target_id Referenced object ID (non-zero)
 * @param kind Reference kind (Session layer enum)
 * @param field_name Field name containing the reference
 * @param index Array index (0 for non-array fields)
 * @return true to continue enumeration, false to stop
 */
typedef bool (*nmo_ref_visitor_fn)(
    void *user_data,
    uint32_t target_id,
    nmo_ref_kind_t kind,
    const char *field_name,
    uint32_t index
);

/* ============================================================================
 * Enumeration API
 * ============================================================================ */

/**
 * @brief Enumerate all references from an object using the type registry
 *
 * Uses the object's class ID to resolve its type descriptor, then enumerates
 * reference fields using reflection metadata. If the type provides a custom
 * enumerate_refs vtable entry, it is used instead of the default field walk.
 *
 * @param types Type registry
 * @param obj Object to enumerate
 * @param visitor Visitor callback
 * @param user_data User context
 * @return NMO_OK on success
 * @note Enumeration does not allocate; visitor must not free object state.
 */
NMO_API nmo_status_t nmo_ref_enumerate_object(
    const nmo_type_registry_t *types,
    nmo_object_t *obj,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REF_ENUMERATE_H */
