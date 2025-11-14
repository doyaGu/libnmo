/**
 * @file nmo_stb_adapter.c
 * @brief stb_image / stb_image_write integration with libnmo arenas.
 */

#include "format/nmo_stb_adapter.h"

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define NMO_STB_THREAD_LOCAL __declspec(thread)
#else
#define NMO_STB_THREAD_LOCAL __thread
#endif

#define NMO_STB_FLAG_SYSTEM 0x1u

typedef struct nmo_stb_allocation_header {
    size_t size;
    uint32_t flags;
} nmo_stb_allocation_header_t;

static NMO_STB_THREAD_LOCAL nmo_arena_t *g_stb_arena = NULL;

static void *nmo_stb_malloc(size_t size);
static void *nmo_stb_realloc(void *ptr, size_t new_size);
static void nmo_stb_free(void *ptr);

void nmo_stb_set_arena(nmo_arena_t *arena) {
    g_stb_arena = arena;
}

nmo_arena_t *nmo_stb_get_arena(void) {
    return g_stb_arena;
}

static void *nmo_stb_alloc_block(size_t size) {
    size_t total = sizeof(nmo_stb_allocation_header_t) + size;
    size_t alignment = sizeof(void *);
    nmo_stb_allocation_header_t *header;

    if (g_stb_arena) {
        header = nmo_arena_alloc(g_stb_arena, total, alignment);
        if (!header) {
            return NULL;
        }
        header->flags = 0;
    } else {
        header = (nmo_stb_allocation_header_t *)malloc(total);
        if (!header) {
            return NULL;
        }
        header->flags = NMO_STB_FLAG_SYSTEM;
    }

    header->size = size;
    return header + 1;
}

static void *nmo_stb_malloc(size_t size) {
    return nmo_stb_alloc_block(size ? size : 1);
}

static void *nmo_stb_realloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return nmo_stb_malloc(new_size);
    }

    if (new_size == 0) {
        nmo_stb_free(ptr);
        return NULL;
    }

    nmo_stb_allocation_header_t *header = ((nmo_stb_allocation_header_t *)ptr) - 1;
    size_t old_size = header->size;
    int use_system = (header->flags & NMO_STB_FLAG_SYSTEM) != 0;

    if (use_system && !g_stb_arena) {
        size_t total = sizeof(nmo_stb_allocation_header_t) + new_size;
        nmo_stb_allocation_header_t *new_header =
            (nmo_stb_allocation_header_t *)realloc(header, total);
        if (!new_header) {
            return NULL;
        }
        new_header->size = new_size;
        return new_header + 1;
    }

    void *new_ptr = nmo_stb_malloc(new_size);
    if (!new_ptr) {
        return NULL;
    }

    size_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    if (use_system) {
        free(header);
    }

    return new_ptr;
}

static void nmo_stb_free(void *ptr) {
    if (!ptr) {
        return;
    }

    nmo_stb_allocation_header_t *header = ((nmo_stb_allocation_header_t *)ptr) - 1;
    if (header->flags & NMO_STB_FLAG_SYSTEM) {
        free(header);
    }
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz) nmo_stb_malloc(sz)
#define STBI_REALLOC(p,sz) nmo_stb_realloc(p,sz)
#define STBI_FREE(p) nmo_stb_free(p)
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_MALLOC(sz) nmo_stb_malloc(sz)
#define STBIW_REALLOC(p,sz) nmo_stb_realloc(p,sz)
#define STBIW_FREE(p) nmo_stb_free(p)
#include "stb/stb_image_write.h"

uint8_t *nmo_stbi_load_from_memory(nmo_arena_t *arena,
                                   const uint8_t *buffer,
                                   int len,
                                   int *out_width,
                                   int *out_height,
                                   int *out_channels,
                                   int desired_channels) {
    if (!buffer || len <= 0 || !arena) {
        return NULL;
    }

    nmo_arena_t *previous = nmo_stb_get_arena();
    nmo_stb_set_arena(arena);

    uint8_t *pixels = stbi_load_from_memory(buffer,
                                            len,
                                            out_width,
                                            out_height,
                                            out_channels,
                                            desired_channels);

    nmo_stb_set_arena(previous);
    return pixels;
}

typedef struct nmo_stb_write_context {
    nmo_arena_t *arena;
    uint8_t *buffer;
    size_t size;
    size_t capacity;
    int failed;
} nmo_stb_write_context_t;

static int nmo_stb_write_grow(nmo_stb_write_context_t *ctx, size_t needed) {
    if (ctx->failed) {
        return 0;
    }

    if (!ctx->arena) {
        ctx->failed = 1;
        return 0;
    }

    size_t new_capacity = ctx->capacity == 0 ? 64 * 1024 : ctx->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    uint8_t *new_buffer = nmo_arena_alloc(ctx->arena, new_capacity, 1);
    if (!new_buffer) {
        ctx->failed = 1;
        return 0;
    }

    if (ctx->buffer && ctx->size > 0) {
        memcpy(new_buffer, ctx->buffer, ctx->size);
    }

    ctx->buffer = new_buffer;
    ctx->capacity = new_capacity;
    return 1;
}

static void nmo_stb_write_to_context(void *context, void *data, int data_size) {
    nmo_stb_write_context_t *ctx = (nmo_stb_write_context_t *)context;
    if (ctx->failed) {
        return;
    }

    size_t new_size = ctx->size + (size_t)data_size;
    if (new_size > ctx->capacity) {
        if (!nmo_stb_write_grow(ctx, new_size)) {
            return;
        }
    }

    memcpy(ctx->buffer + ctx->size, data, (size_t)data_size);
    ctx->size = new_size;
}

uint8_t *nmo_stbi_write_to_memory(nmo_arena_t *arena,
                                  nmo_bitmap_format_t format,
                                  int width,
                                  int height,
                                  int channels,
                                  const uint8_t *data,
                                  int quality,
                                  size_t *out_size) {
    if (out_size) {
        *out_size = 0;
    }

    if (!arena || !data || !out_size) {
        return NULL;
    }

    if (width <= 0 || height <= 0 || channels <= 0 || channels > 4) {
        return NULL;
    }

    if (format == NMO_BITMAP_FORMAT_RAW) {
        size_t size = (size_t)width * (size_t)height * (size_t)channels;
        uint8_t *raw = nmo_arena_alloc(arena, size, 1);
        if (!raw) {
            return NULL;
        }
        memcpy(raw, data, size);
        *out_size = size;
        return raw;
    }

    nmo_stb_write_context_t ctx = { arena, NULL, 0, 0, 0 };

    nmo_arena_t *previous = nmo_stb_get_arena();
    nmo_stb_set_arena(arena);

    int write_result = 0;
    switch (format) {
        case NMO_BITMAP_FORMAT_PNG:
            write_result = stbi_write_png_to_func(nmo_stb_write_to_context,
                                                  &ctx,
                                                  width,
                                                  height,
                                                  channels,
                                                  data,
                                                  width * channels);
            break;
        case NMO_BITMAP_FORMAT_BMP:
            write_result = stbi_write_bmp_to_func(nmo_stb_write_to_context,
                                                  &ctx,
                                                  width,
                                                  height,
                                                  channels,
                                                  data);
            break;
        case NMO_BITMAP_FORMAT_TGA:
            write_result = stbi_write_tga_to_func(nmo_stb_write_to_context,
                                                  &ctx,
                                                  width,
                                                  height,
                                                  channels,
                                                  data);
            break;
        case NMO_BITMAP_FORMAT_JPG: {
            int jpg_quality = (quality > 0 && quality <= 100) ? quality : 90;
            write_result = stbi_write_jpg_to_func(nmo_stb_write_to_context,
                                                  &ctx,
                                                  width,
                                                  height,
                                                  channels,
                                                  data,
                                                  jpg_quality);
            break;
        }
        default:
            write_result = 0;
            break;
    }

    nmo_stb_set_arena(previous);

    if (!write_result || ctx.failed) {
        return NULL;
    }

    *out_size = ctx.size;
    return ctx.buffer;
}
