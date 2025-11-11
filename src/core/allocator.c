#include "core/nmo_allocator.h"
#include <stdlib.h>
#include <string.h>

// Default allocator using malloc/free
static void* default_alloc(void* user_data, size_t size, size_t alignment) {
    (void)user_data;

    if (size == 0) {
        return NULL;
    }

    // For alignments <= malloc's natural alignment, just use malloc
    if (alignment <= sizeof(void*) * 2) {
        return malloc(size);
    }

    // For larger alignments, use aligned_alloc or posix_memalign
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    return aligned_alloc(alignment, size);
#else
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
#endif
}

static void default_free(void* user_data, void* ptr) {
    (void)user_data;

    if (ptr == NULL) {
        return;
    }

#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

nmo_allocator nmo_allocator_default(void) {
    nmo_allocator allocator = {
        .alloc = default_alloc,
        .free = default_free,
        .user_data = NULL
    };
    return allocator;
}

nmo_allocator nmo_allocator_custom(nmo_alloc_fn alloc, nmo_free_fn free, void* user_data) {
    nmo_allocator allocator = {
        .alloc = alloc,
        .free = free,
        .user_data = user_data
    };
    return allocator;
}

void* nmo_alloc(nmo_allocator* allocator, size_t size, size_t alignment) {
    if (allocator == NULL || allocator->alloc == NULL) {
        return NULL;
    }
    return allocator->alloc(allocator->user_data, size, alignment);
}

void nmo_free(nmo_allocator* allocator, void* ptr) {
    if (allocator == NULL || allocator->free == NULL || ptr == NULL) {
        return;
    }
    allocator->free(allocator->user_data, ptr);
}
