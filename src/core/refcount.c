/**
 * @file refcount.c
 * @brief Lightweight reference counter utilities.
 */

#include "core/nmo_refcount.h"

void nmo_refcount_init(nmo_refcount_t *refcount) {
    if (!refcount) {
        return;
    }
    refcount->count = 1u;
}

uint32_t nmo_refcount_get(const nmo_refcount_t *refcount) {
    return refcount ? refcount->count : 0u;
}

uint32_t nmo_refcount_retain(nmo_refcount_t *refcount) {
    if (!refcount) {
        return 0u;
    }
    if (refcount->count == 0u) {
        return 0u;
    }
    refcount->count += 1u;
    return refcount->count;
}

uint32_t nmo_refcount_release(nmo_refcount_t *refcount) {
    if (!refcount || refcount->count == 0u) {
        return 0u;
    }
    refcount->count -= 1u;
    return refcount->count;
}

uint32_t nmo_refcount_release_with(nmo_refcount_t *refcount,
                                   nmo_refcount_dispose_fn disposer,
                                   void *user_data) {
    uint32_t count = nmo_refcount_release(refcount);
    if (count == 0u && disposer) {
        disposer(user_data);
    }
    return count;
}
