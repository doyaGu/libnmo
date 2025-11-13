#include "core/nmo_allocator.h"
#include <stdlib.h>
#include <string.h>

// Default allocator using malloc/free
static void *default_alloc(void *user_data, size_t size, size_t alignment) {
    (void) user_data;

    if (size == 0) {
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
    return aligned_alloc(alignment, size);
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
    return allocator->alloc(allocator->user_data, size, alignment);
}

void nmo_free(nmo_allocator_t *allocator, void *ptr) {
    if (allocator == NULL || allocator->free == NULL || ptr == NULL) {
        return;
    }
    allocator->free(allocator->user_data, ptr);
}
