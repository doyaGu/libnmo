#ifndef NMO_MUTABLE_REFS_INTERNAL_H
#define NMO_MUTABLE_REFS_INTERNAL_H

#include "object/nmo_ref.h"
#include "type/nmo_type_system.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum nmo_mutable_ref_operation {
    NMO_MUTABLE_REF_VALIDATE = 0,
    NMO_MUTABLE_REF_REMAP,
    NMO_MUTABLE_REF_REMOVE,
} nmo_mutable_ref_operation_t;

typedef bool (*nmo_mutable_ref_resolver_fn)(
    const void *context,
    const nmo_ref_t *ref,
    nmo_class_id_t expected_class_id,
    nmo_object_id_t *out_replacement_id);

typedef struct nmo_mutable_ref_request {
    nmo_mutable_ref_operation_t operation;
    nmo_mutable_ref_resolver_fn resolve;
    const void *context;
} nmo_mutable_ref_request_t;

/**
 * Apply a private mutable-reference Adapter across a complete type hierarchy.
 *
 * Non-validation operations first validate every matching Adapter lane, then
 * mutate without allocation.  REMAP changes only Adapter-owned nested refs;
 * ordinary reflected fields remain owned by the reflection implementation.
 * REMOVE detaches matches according to each lane's storage policy: compacting
 * sequence lanes or clearing references inside fixed records.
 */
nmo_status_t nmo_mutable_refs_apply(
    const nmo_type_descriptor_t *derived_type,
    void *root_instance,
    const nmo_mutable_ref_request_t *request,
    size_t *out_change_count);

/** Return whether a reflected field is owned by REMOVE for this type. */
bool nmo_mutable_refs_claims_remove_field(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *field);

#endif /* NMO_MUTABLE_REFS_INTERNAL_H */
