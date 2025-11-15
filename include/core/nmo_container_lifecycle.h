#ifndef NMO_CONTAINER_LIFECYCLE_H
#define NMO_CONTAINER_LIFECYCLE_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function type invoked when a container element is disposed.
 *
 * The callback receives a pointer to the element stored in the container
 * as well as the user-provided context pointer. Implementations can use
 * this hook to release nested memory, decrement reference counts, etc.
 */
typedef void (*nmo_container_dispose_func_t)(void *element, void *user_data);

/**
 * @brief Lifecycle hooks shared by arena-backed containers.
 */
typedef struct nmo_container_lifecycle {
    nmo_container_dispose_func_t dispose; /**< Optional element teardown hook */
    void *user_data;                      /**< Passed to dispose callback */
} nmo_container_lifecycle_t;

/**
 * @brief Helper macro for zero-initialized lifecycle definitions.
 */
#define NMO_CONTAINER_LIFECYCLE_INIT \
    { NULL, NULL }

#ifdef __cplusplus
}
#endif

#endif /* NMO_CONTAINER_LIFECYCLE_H */

