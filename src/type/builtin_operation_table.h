#ifndef NMO_BUILTIN_OPERATION_TABLE_H
#define NMO_BUILTIN_OPERATION_TABLE_H

#include "type/nmo_operation_system.h"

#include <stddef.h>

/*
 * Row constructors for the builtin operation tables. Each row registers one
 * operation for a single scalar type. Row arguments:
 *   OP        operation family GUID (NMO_OP_GUID_*)
 *   NAME      operation name
 *   DESC      human-readable description
 *   TYPE      GUID of the operand type (both operands for binary rows)
 *   PRIO      priority for ambiguous matches (higher wins)
 *   FN        operation function
 */

/* (TYPE, TYPE) -> TYPE */
#define NMO_OP_ROW_BINARY(OP, NAME, DESC, TYPE, PRIO, FN) \
    { \
        .operation_guid = OP, \
        .p1_type_guid = TYPE, \
        .p2_type_guid = TYPE, \
        .result_type_guid = TYPE, \
        .function = FN, \
        .flags = NMO_OP_BINARY, \
        .priority = PRIO, \
        .name = NAME, \
        .description = DESC, \
    }

/* (TYPE, TYPE) -> BOOL */
#define NMO_OP_ROW_PREDICATE(OP, NAME, DESC, TYPE, PRIO, FN) \
    { \
        .operation_guid = OP, \
        .p1_type_guid = TYPE, \
        .p2_type_guid = TYPE, \
        .result_type_guid = CKPGUID_BOOL, \
        .function = FN, \
        .flags = NMO_OP_BINARY, \
        .priority = PRIO, \
        .name = NAME, \
        .description = DESC, \
    }

/* (TYPE) -> TYPE */
#define NMO_OP_ROW_UNARY(OP, NAME, DESC, TYPE, PRIO, FN) \
    { \
        .operation_guid = OP, \
        .p1_type_guid = TYPE, \
        .result_type_guid = TYPE, \
        .function = FN, \
        .flags = NMO_OP_UNARY, \
        .priority = PRIO, \
        .name = NAME, \
        .description = DESC, \
    }

/* Register every row in order; stops at the first failure. */
static inline nmo_status_t nmo_register_operation_table(
    nmo_operation_registry_t *operation_registry,
    const nmo_operation_desc_t *operations,
    size_t count,
    const nmo_type_registry_t *type_registry)
{
    for (size_t i = 0; i < count; ++i) {
        nmo_status_t status =
            nmo_operation_registry_register(operation_registry, &operations[i], type_registry);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}

#endif /* NMO_BUILTIN_OPERATION_TABLE_H */
