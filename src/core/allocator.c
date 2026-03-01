#include "core/nmo_allocator.h"
#include "core/nmo_debug.h"
#include "core/nmo_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>

// Default allocator using malloc/free
static void *default_alloc(void *user_data, size_t size, size_t alignment) {
    (void) user_data;

    if (size == 0) {
        return NULL;
    }

    if (alignment == 0) {
        alignment = alignof(max_align_t);
    }

    if (!NMO_IS_POWER_OF_TWO(alignment)) {
        return NULL;
    }

    // On Windows, always use _aligned_malloc to ensure _aligned_free works correctly
    // On other platforms, use malloc for small alignments
#if defined(_WIN32)
    // Ensure alignment is at least sizeof(void*)
    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    return _aligned_malloc(size, alignment);
#else
    // For alignments <= malloc's natural alignment, just use malloc
    if (alignment <= sizeof(void *) * 2) {
        return malloc(size);
    }

    // For larger alignments, use aligned_alloc or posix_memalign
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    size_t aligned_size = nmo_align(size, alignment);
    if (aligned_size < size) {
        return NULL;
    }
    return aligned_alloc(alignment, aligned_size);
#else
    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
#endif
#endif
}

static void default_free(void *user_data, void *ptr) {
    (void) user_data;

    if (ptr == NULL) {
        return;
    }

#if defined(_WIN32)
    // On Windows, all allocations use _aligned_malloc, so use _aligned_free
    _aligned_free(ptr);
#else
    // On other platforms, we may have used malloc, aligned_alloc, or posix_memalign
    // All of these can be freed with free()
    free(ptr);
#endif
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
        alignment = alignof(max_align_t);
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
        alignment = alignof(max_align_t);
    }

    if (!NMO_IS_POWER_OF_TWO(alignment)) {
        return NULL;
    }

    size_t header_size = sizeof(nmo_tracking_header_t);
    if (size > SIZE_MAX - header_size - (alignment - 1)) {
        return NULL;
    }
    size_t total = size + header_size + (alignment - 1);

    void *raw = tracking->base.alloc(tracking->base.user_data, total, alignof(max_align_t));
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
        alignment = alignof(max_align_t);
    }

    if (!NMO_IS_POWER_OF_TWO(alignment)) {
        return NULL;
    }

    size_t header_size = sizeof(nmo_debug_header_t);
    if (size > SIZE_MAX - header_size - (alignment - 1)) {
        return NULL;
    }
    size_t total = size + header_size + (alignment - 1);

    void *raw = debug->base.alloc(debug->base.user_data, total, alignof(max_align_t));
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
