#ifndef NMO_CONTAINER_LIFECYCLE_H
#define NMO_CONTAINER_LIFECYCLE_H

#include "nmo_types.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function type invoked to initialize a container element.
 *
 * The callback receives a pointer to the element storage and the user-provided
 * context. When NULL, containers zero-initialize new elements by default.
 */
typedef void (*nmo_container_init_func_t)(void *element, void *user_data);

/**
 * @brief Function type invoked when a container element is reset for overwrite.
 *
 * The callback receives a pointer to the element storage and the user-provided
 * context. When NULL, containers dispose (if configured) and then zero-initialize
 * the element storage.
 */
typedef void (*nmo_container_reset_func_t)(void *element, void *user_data);

/**
 * @brief Function type invoked when a container element is disposed.
 *
 * The callback receives a pointer to the element stored in the container
 * as well as the user-provided context pointer. Implementations can use
 * this hook to release nested memory, decrement reference counts, etc.
 */
typedef void (*nmo_container_dispose_func_t)(void *element, void *user_data);

/**
 * @brief Function type invoked when a container copies an element into storage.
 *
 * The callback receives destination and source element pointers plus the
 * user-provided context. Implementations can perform deep-copy logic, retain
 * references, etc. When NULL, the container falls back to memcpy.
 */
typedef void (*nmo_container_copy_func_t)(void *dest, const void *src, void *user_data);

/**
 * @brief Function type invoked when a container relocates an element in storage.
 *
 * Used for internal moves such as reallocation, insert shifts, and compaction.
 * When NULL, the container falls back to memmove.
 */
typedef void (*nmo_container_move_func_t)(void *dest, void *src, void *user_data);

/**
 * @brief Lifecycle hooks shared by arena-backed containers.
 */
typedef struct nmo_container_lifecycle {
    nmo_container_init_func_t init;       /**< Optional element init hook */
    nmo_container_reset_func_t reset;     /**< Optional element reset hook */
    nmo_container_copy_func_t copy;       /**< Optional element copy hook */
    nmo_container_move_func_t move;       /**< Optional element move hook */
    nmo_container_dispose_func_t dispose; /**< Optional element teardown hook */
    void *user_data;                      /**< Passed to lifecycle callbacks */
} nmo_container_lifecycle_t;

/**
 * @brief Helper macro for zero-initialized lifecycle definitions.
 */
#define NMO_CONTAINER_LIFECYCLE_INIT \
    { NULL, NULL, NULL, NULL, NULL, NULL }

/**
 * @brief Initialize a single element using lifecycle hooks or zero-init fallback.
 */
static inline void nmo_container_init_element(const nmo_container_lifecycle_t *lifecycle,
                                              void *element,
                                              size_t element_size) {
    if (element == NULL || element_size == 0) {
        return;
    }
    if (lifecycle && lifecycle->init) {
        lifecycle->init(element, lifecycle->user_data);
    } else {
        memset(element, 0, element_size);
    }
}

/**
 * @brief Reset a single element before overwrite.
 */
static inline void nmo_container_reset_element(const nmo_container_lifecycle_t *lifecycle,
                                               void *element,
                                               size_t element_size) {
    if (element == NULL || element_size == 0) {
        return;
    }
    if (lifecycle && lifecycle->reset) {
        lifecycle->reset(element, lifecycle->user_data);
        return;
    }
    if (lifecycle && lifecycle->dispose) {
        lifecycle->dispose(element, lifecycle->user_data);
    }
    memset(element, 0, element_size);
}

/**
 * @brief Copy a single element using lifecycle hooks or memcpy fallback.
 */
static inline void nmo_container_copy_element(const nmo_container_lifecycle_t *lifecycle,
                                              void *dest,
                                              const void *src,
                                              size_t element_size) {
    if (dest == NULL || src == NULL || element_size == 0) {
        return;
    }
    if (lifecycle && lifecycle->copy) {
        lifecycle->copy(dest, src, lifecycle->user_data);
    } else {
        memcpy(dest, src, element_size);
    }
}

/**
 * @brief Move a single element using lifecycle hooks or memmove fallback.
 */
static inline void nmo_container_move_element(const nmo_container_lifecycle_t *lifecycle,
                                              void *dest,
                                              void *src,
                                              size_t element_size) {
    if (dest == NULL || src == NULL || element_size == 0) {
        return;
    }
    if (lifecycle && lifecycle->move) {
        lifecycle->move(dest, src, lifecycle->user_data);
    } else {
        memmove(dest, src, element_size);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_CONTAINER_LIFECYCLE_H */

