/**
 * @file type_runtime.c
 * @brief Aggregated type runtime helper implementation
 */

#include "type/nmo_type_runtime.h"

#include "type/nmo_type_system.h"
#include "type/nmo_operation_system.h"

nmo_status_t nmo_type_runtime_finalize(nmo_type_runtime_t *rt) {
    if (rt == NULL || rt->types == NULL || rt->ops == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "type runtime and registries are required");
    }

    NMO_RETURN_IF_ERROR(nmo_type_registry_finalize(rt->types));
    NMO_RETURN_IF_ERROR(nmo_operation_registry_finalize(rt->ops, rt->types));

    rt->types_finalized_version = rt->types->registry_version;
    rt->ops_finalized_version = rt->ops->registry_version;
    NMO_RETURN_OK();
}
