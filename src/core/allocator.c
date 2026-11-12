#include "core/nmo_allocator.h"
#include "core/nmo_debug.h"
#include "core/nmo_utils.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>

// Default allocator: the platform's aligned allocation
static void *default_alloc(void *user_data, size_t size, size_t alignment) {
    (void) user_data;

    if (size == 0) {
        return NULL;
    }

    if (alignment == 0) {
        alignment = NMO_MAX_ALIGN;
    }

    if (!NMO_IS_POWER_OF_TWO(alignment)) {
        return NULL;
    }

    return nmo_platform_aligned_alloc(size, alignment);
}

static void default_free(void *user_data, void *ptr) {
    (void) user_data;
    nmo_platform_aligned_free(ptr);
}

nmo_allocator_t nmo_allocator_default(void) {
    nmo_allocator_t allocator = {
        .alloc = default_alloc,
        .free = default_free,
        .user_data = NULL
    };
    return allocator;
}

nmo_allocator_t nmo_allocator_custom(nmo_alloc_fn alloc, nmo_free_fn free, void *user_data) {
    nmo_allocator_t allocator = {
        .alloc = alloc,
        .free = free,
        .user_data = user_data
    };
    return allocator;
}

void *nmo_alloc(nmo_allocator_t *allocator, size_t size, size_t alignment) {
    if (allocator == NULL || allocator->alloc == NULL) {
        return NULL;
    }
    if (alignment == 0) {
        alignment = NMO_MAX_ALIGN;
    }
    if (!NMO_IS_POWER_OF_TWO(alignment)) {
        return NULL;
    }
    return allocator->alloc(allocator->user_data, size, alignment);
}

void nmo_free(nmo_allocator_t *allocator, void *ptr) {
    if (allocator == NULL || allocator->free == NULL || ptr == NULL) {
        return;
    }
    allocator->free(allocator->user_data, ptr);
}

typedef struct nmo_tracking_header {
    void *raw;
    size_t size;
} nmo_tracking_header_t;

typedef struct nmo_debug_header {
    void *raw;
    size_t size;
    const char *module;
    const char *tag;
} nmo_debug_header_t;

static void *tracking_alloc(void *user_data, size_t size, size_t alignment) {
    nmo_allocator_tracking_t *tracking = (nmo_allocator_tracking_t *)user_data;
    if (tracking == NULL || tracking->base.alloc == NULL) {
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    if (alignment == 0) {
        alignment = NMO_MAX_ALIGN;
    }

    if (!NMO_IS_POWER_OF_TWO(alignment)) {
        return NULL;
    }

    size_t header_size = sizeof(nmo_tracking_header_t);
    if (size > SIZE_MAX - header_size - (alignment - 1)) {
        return NULL;
    }
    size_t total = size + header_size + (alignment - 1);

    void *raw = tracking->base.alloc(tracking->base.user_data, total, NMO_MAX_ALIGN);
    if (raw == NULL) {
        return NULL;
    }

    uintptr_t start = (uintptr_t)raw + header_size;
    uintptr_t aligned = (start + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    nmo_tracking_header_t *header = (nmo_tracking_header_t *)(aligned - header_size);
    header->raw = raw;
    header->size = size;

    if (tracking->stats) {
        tracking->stats->total_allocations += 1;
        tracking->stats->total_bytes += size;
        tracking->stats->current_bytes += size;
        if (tracking->stats->current_bytes > tracking->stats->peak_bytes) {
            tracking->stats->peak_bytes = tracking->stats->current_bytes;
        }
    }

    return (void *)aligned;
}

static void tracking_free(void *user_data, void *ptr) {
    nmo_allocator_tracking_t *tracking = (nmo_allocator_tracking_t *)user_data;
    if (tracking == NULL || tracking->base.free == NULL || ptr == NULL) {
        return;
    }

    nmo_tracking_header_t *header = (nmo_tracking_header_t *)((uint8_t *)ptr - sizeof(nmo_tracking_header_t));
    if (tracking->stats) {
        if (tracking->stats->current_bytes >= header->size) {
            tracking->stats->current_bytes -= header->size;
        } else {
            tracking->stats->current_bytes = 0;
        }
        tracking->stats->total_frees += 1;
    }

    tracking->base.free(tracking->base.user_data, header->raw);
}

nmo_allocator_t nmo_allocator_tracking_init(nmo_allocator_tracking_t *tracking,
                                            nmo_allocator_t base,
                                            nmo_allocator_stats_t *stats) {
    if (tracking == NULL) {
        return nmo_allocator_custom(NULL, NULL, NULL);
    }

    tracking->base = base;
    tracking->stats = stats;

    return nmo_allocator_custom(tracking_alloc, tracking_free, tracking);
}

static void *debug_alloc(void *user_data, size_t size, size_t alignment) {
    nmo_allocator_debug_t *debug = (nmo_allocator_debug_t *)user_data;
    if (debug == NULL || debug->base.alloc == NULL) {
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    if (alignment == 0) {
        alignment = NMO_MAX_ALIGN;
    }

    if (!NMO_IS_POWER_OF_TWO(alignment)) {
        return NULL;
    }

    size_t header_size = sizeof(nmo_debug_header_t);
    if (size > SIZE_MAX - header_size - (alignment - 1)) {
        return NULL;
    }
    size_t total = size + header_size + (alignment - 1);

    void *raw = debug->base.alloc(debug->base.user_data, total, NMO_MAX_ALIGN);
    if (raw == NULL) {
        return NULL;
    }

    uintptr_t start = (uintptr_t)raw + header_size;
    uintptr_t aligned = (start + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    nmo_debug_header_t *header = (nmo_debug_header_t *)(aligned - header_size);
    header->raw = raw;
    header->size = size;
    header->module = debug->module;
    header->tag = debug->tag;

    return (void *)aligned;
}

static void debug_free(void *user_data, void *ptr) {
    nmo_allocator_debug_t *debug = (nmo_allocator_debug_t *)user_data;
    if (debug == NULL || debug->base.free == NULL || ptr == NULL) {
        return;
    }

    nmo_debug_header_t *header = (nmo_debug_header_t *)((uint8_t *)ptr - sizeof(nmo_debug_header_t));
    NMO_DEBUG_ASSERT(header->module == debug->module);
    NMO_DEBUG_ASSERT(header->tag == debug->tag);

    debug->base.free(debug->base.user_data, header->raw);
}

nmo_allocator_t nmo_allocator_debug_init(nmo_allocator_debug_t *debug,
                                         nmo_allocator_t base,
                                         const char *module,
                                         const char *tag) {
    if (debug == NULL) {
        return nmo_allocator_custom(NULL, NULL, NULL);
    }

    debug->base = base;
    debug->module = module;
    debug->tag = tag;

    return nmo_allocator_custom(debug_alloc, debug_free, debug);
}

void nmo_allocator_stats_reset(nmo_allocator_stats_t *stats) {
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

char *nmo_strdup(nmo_allocator_t *alloc, const char *src) {
    if (!alloc || !src) return NULL;
    const size_t len = strlen(src) + 1u;
    char *dst = (char *)nmo_alloc(alloc, len, _Alignof(char));
    if (!dst) return NULL;
    memcpy(dst, src, len);
    return dst;
}
