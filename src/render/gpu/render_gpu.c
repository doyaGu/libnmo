/**
 * @file render_gpu.c
 * @brief The SDL_GPU backend: render_backend.h's fixed function state drawn
 *        with one shader (shaders/fixed.vert, fixed.frag) and pipelines
 *        made for each set of fixed states.
 *
 * A GPU device records commands and uploads data in passes of their own, so
 * the backend records a frame's operations as they come (each draw's state
 * and vertices copied) and replays them when the screen ends: one copy pass
 * with every vertex and texture image, then the render passes in order, a
 * clear drawn as a quad, the screen kept for the next frame's copies and
 * shown in the window. The screen is a texture of its own, kept from frame
 * to frame, so a frame that does not clear draws over the one before.
 * Textures and the screen are drawn into upright: SDL_GPU's coordinates
 * start at the top left, as the front end's do.
 */

#include "render/nmo_render_gpu.h"
#include "render_gpu.h"
#include "../render_internal.h"
#include "../render_math.h"
#include "shaders/fixed_shaders.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPU_MAX_LEVELS 16
#define GPU_COLOR_FORMAT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM

/* ============================================================================
 * Recording
 * ============================================================================ */

/* A vertex as the shader takes it */
typedef struct gpu_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
    float specular[3];
} gpu_vertex_t;

typedef struct gpu_texture {
    render_texture_kind_t kind;
    SDL_GPUTexture *texture; /* NULL until it is made */
    int32_t width, height, levels;
    bool target;             /* made to be drawn into */
    bool fresh;              /* drawn into never: cleared at first */
    /* The images texture_image gave, uploaded when texture_levels says how many there are */
    struct gpu_image {
        int32_t width, height;
        uint8_t *rgba;
    } images[6][GPU_MAX_LEVELS];
} gpu_texture_t;

typedef enum gpu_op_kind {
    GPU_OP_BEGIN,  /* render pass into the screen, or a texture's face */
    GPU_OP_END,
    GPU_OP_CLEAR,
    GPU_OP_DRAW,
    GPU_OP_UPLOAD, /* a texture region, in the order it came */
} gpu_op_kind_t;

typedef struct gpu_op {
    gpu_op_kind_t kind;
    gpu_texture_t *texture; /* BEGIN: NULL for the screen; UPLOAD: the texture */
    int32_t face, level;
    int32_t rect[4];        /* UPLOAD: x, y, width, height */
    size_t state;           /* CLEAR, DRAW: index of its state */
    bool clear_color, clear_depth, clear_stencil;
    float clear_value[4];
    SDL_GPUPrimitiveType primitive;
    uint32_t first_vertex, vertex_count; /* DRAW: in the frame's vertices */
    uint32_t first_index, index_count;   /* DRAW: in the frame's indices; 0 for none */
    size_t data;            /* UPLOAD: offset of its bytes in the frame's upload data */
} gpu_op_t;

/* An upload done before the frame's passes: the image of a texture's level */
typedef struct gpu_upload {
    gpu_texture_t *texture;
    int32_t face, level, width, height;
    size_t data;
} gpu_upload_t;

#define GPU_VEC(type) struct { type *items; size_t count, capacity; }

#define GPU_PUSH(vec, value)                                                                      \
    (gpu_grow((void **)&(vec).items, &(vec).capacity, (vec).count + 1, sizeof(*(vec).items))      \
         ? ((vec).items[(vec).count++] = (value), true)                                          \
         : false)

static bool gpu_grow(void **items, size_t *capacity, size_t needed, size_t size)
{
    if (needed <= *capacity) {
        return true;
    }
    size_t grown = *capacity != 0 ? *capacity * 2 : 64;
    while (grown < needed) {
        grown *= 2;
    }
    void *more = realloc(*items, grown * size);
    if (more == NULL) {
        return false;
    }
    *items = more;
    *capacity = grown;
    return true;
}

typedef struct gpu_pipeline_key {
    uint8_t primitive, fill, cull, front_ccw, flat;
    uint8_t blend, source_blend, dest_blend, color_write;
    uint8_t depth_test, depth_func, depth_write;
    uint8_t stencil, stencil_func, stencil_fail, stencil_zfail, stencil_pass;
    uint8_t stencil_compare_mask, stencil_write_mask;
} gpu_pipeline_key_t;

typedef struct gpu_pipeline {
    gpu_pipeline_key_t key;
    SDL_GPUGraphicsPipeline *pipeline;
} gpu_pipeline_t;

typedef struct gpu_sampler_key {
    uint8_t min_filter, mag_filter, mip, address, anisotropic, cube;
} gpu_sampler_key_t;

typedef struct gpu_sampler {
    gpu_sampler_key_t key;
    SDL_GPUSampler *sampler;
} gpu_sampler_t;

/* A depth and stencil texture for drawing into textures of a size */
typedef struct gpu_depth {
    int32_t width, height;
    SDL_GPUTexture *texture;
    bool fresh;
} gpu_depth_t;

typedef struct gpu {
    render_backend_t base;
    SDL_GPUDevice *device;
    SDL_Window *window;          /* claimed for the device; NULL when offscreen */
    bool own_device;
    SDL_GPUShaderFormat format;  /* the shader format the device takes */
    SDL_GPUTextureFormat depth_format;
    SDL_GPUShader *vertex[2], *fragment[2]; /* smooth, flat */
    SDL_GPUTexture *white_2d, *white_cube;  /* bound where no texture is */

    /* The screen and what it keeps for the next frame's copies */
    SDL_GPUTexture *screen, *screen_depth, *last;
    int32_t screen_w, screen_h, last_w, last_h;
    bool screen_fresh, last_valid;

    /* The frame being recorded */
    GPU_VEC(gpu_op_t) ops;
    GPU_VEC(render_state_t) states;
    GPU_VEC(gpu_vertex_t) vertices;
    GPU_VEC(uint16_t) indices;
    GPU_VEC(uint8_t) data;
    GPU_VEC(gpu_upload_t) uploads;
    GPU_VEC(gpu_texture_t *) releases; /* textures destroyed while the frame was recorded */
    bool in_frame;
    bool screen_open; /* the screen begun and not ended: a target's end goes back to it */

    GPU_VEC(gpu_pipeline_t) pipelines;
    GPU_VEC(gpu_sampler_t) samplers;
    GPU_VEC(gpu_depth_t) depths;
    SDL_GPUBuffer *vertex_buffer, *index_buffer;
    uint32_t vertex_buffer_size, index_buffer_size;
    SDL_GPUTransferBuffer *transfer;
    uint32_t transfer_size;
} gpu_t;

/* ============================================================================
 * Enums
 * ============================================================================ */

static SDL_GPUCompareOp gpu_compare(int32_t func)
{
    switch (func) {
    case 1: return SDL_GPU_COMPAREOP_NEVER;
    case 2: return SDL_GPU_COMPAREOP_LESS;
    case 3: return SDL_GPU_COMPAREOP_EQUAL;
    case 4: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case 5: return SDL_GPU_COMPAREOP_GREATER;
    case 6: return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case 7: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    default: return SDL_GPU_COMPAREOP_ALWAYS;
    }
}

static SDL_GPUStencilOp gpu_stencil_op(int32_t op)
{
    switch (op) {
    case 2: return SDL_GPU_STENCILOP_ZERO;
    case 3: return SDL_GPU_STENCILOP_REPLACE;
    case 4: return SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
    case 5: return SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP;
    case 6: return SDL_GPU_STENCILOP_INVERT;
    case 7: return SDL_GPU_STENCILOP_INCREMENT_AND_WRAP;
    case 8: return SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
    default: return SDL_GPU_STENCILOP_KEEP;
    }
}

static SDL_GPUBlendFactor gpu_blend(int32_t factor)
{
    switch (factor) {
    case 1: return SDL_GPU_BLENDFACTOR_ZERO;
    case 3: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case 4: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case 6: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case 8: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case 10: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case 11: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
    default: return SDL_GPU_BLENDFACTOR_ONE;
    }
}

/* ============================================================================
 * Device objects
 * ============================================================================ */

static SDL_GPUShader *gpu_shader(gpu_t *gpu, bool fragment, bool flat)
{
    SDL_GPUShaderCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.stage = fragment ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
    info.num_samplers = fragment ? 2 : 0;
    info.num_uniform_buffers = 1;
    if (gpu->format == SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = fragment ? (flat ? fixed_flat_fragment_msl : fixed_smooth_fragment_msl)
                             : (flat ? fixed_flat_vertex_msl : fixed_smooth_vertex_msl);
        info.code_size = fragment ? (flat ? sizeof(fixed_flat_fragment_msl) : sizeof(fixed_smooth_fragment_msl))
                                  : (flat ? sizeof(fixed_flat_vertex_msl) : sizeof(fixed_smooth_vertex_msl));
    } else {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.entrypoint = "main";
        info.code = fragment ? (flat ? fixed_flat_fragment_spirv : fixed_smooth_fragment_spirv)
                             : (flat ? fixed_flat_vertex_spirv : fixed_smooth_vertex_spirv);
        info.code_size = fragment ? (flat ? sizeof(fixed_flat_fragment_spirv) : sizeof(fixed_smooth_fragment_spirv))
                                  : (flat ? sizeof(fixed_flat_vertex_spirv) : sizeof(fixed_smooth_vertex_spirv));
    }
    return SDL_CreateGPUShader(gpu->device, &info);
}

static SDL_GPUTexture *gpu_make_texture(gpu_t *gpu, render_texture_kind_t kind, int32_t width,
                                        int32_t height, int32_t levels, SDL_GPUTextureUsageFlags usage,
                                        SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = kind == RENDER_TEXTURE_CUBE ? SDL_GPU_TEXTURETYPE_CUBE : SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = (Uint32)width;
    info.height = (Uint32)height;
    info.layer_count_or_depth = kind == RENDER_TEXTURE_CUBE ? 6u : 1u;
    info.num_levels = (Uint32)(levels > 0 ? levels : 1);
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(gpu->device, &info);
}

static SDL_GPUGraphicsPipeline *gpu_pipeline(gpu_t *gpu, const gpu_pipeline_key_t *key)
{
    for (size_t i = 0; i < gpu->pipelines.count; i++) {
        if (memcmp(&gpu->pipelines.items[i].key, key, sizeof(*key)) == 0) {
            return gpu->pipelines.items[i].pipeline;
        }
    }
    SDL_GPUVertexBufferDescription buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.slot = 0;
    buffer.pitch = sizeof(gpu_vertex_t);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[5] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(gpu_vertex_t, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(gpu_vertex_t, normal)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(gpu_vertex_t, uv)},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (Uint32)offsetof(gpu_vertex_t, color)},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(gpu_vertex_t, specular)},
    };
    SDL_GPUColorTargetDescription color;
    memset(&color, 0, sizeof(color));
    color.format = GPU_COLOR_FORMAT;
    color.blend_state.enable_blend = key->blend != 0;
    color.blend_state.src_color_blendfactor = gpu_blend(key->source_blend);
    color.blend_state.dst_color_blendfactor = gpu_blend(key->dest_blend);
    color.blend_state.src_alpha_blendfactor = gpu_blend(key->source_blend);
    color.blend_state.dst_alpha_blendfactor = gpu_blend(key->dest_blend);
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.enable_color_write_mask = true;
    color.blend_state.color_write_mask =
        key->color_write ? (SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B |
                            SDL_GPU_COLORCOMPONENT_A)
                         : 0;
    SDL_GPUGraphicsPipelineCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.vertex_shader = gpu->vertex[key->flat];
    info.fragment_shader = gpu->fragment[key->flat];
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 5;
    info.primitive_type = (SDL_GPUPrimitiveType)key->primitive;
    info.rasterizer_state.fill_mode = key->fill ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = key->cull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face =
        key->front_ccw ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE : SDL_GPU_FRONTFACE_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUDepthStencilState *ds = &info.depth_stencil_state;
    ds->enable_depth_test = key->depth_test != 0;
    ds->enable_depth_write = key->depth_test != 0 && key->depth_write != 0;
    ds->compare_op = gpu_compare(key->depth_func);
    ds->enable_stencil_test = key->stencil != 0;
    SDL_GPUStencilOpState stencil;
    stencil.compare_op = gpu_compare(key->stencil_func);
    stencil.fail_op = gpu_stencil_op(key->stencil_fail);
    stencil.depth_fail_op = gpu_stencil_op(key->stencil_zfail);
    stencil.pass_op = gpu_stencil_op(key->stencil_pass);
    ds->front_stencil_state = stencil;
    ds->back_stencil_state = stencil;
    ds->compare_mask = key->stencil_compare_mask;
    ds->write_mask = key->stencil_write_mask;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = gpu->depth_format;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
    if (pipeline != NULL) {
        gpu_pipeline_t entry = {*key, pipeline};
        if (!GPU_PUSH(gpu->pipelines, entry)) {
            SDL_ReleaseGPUGraphicsPipeline(gpu->device, pipeline);
            return NULL;
        }
    } else {
        fprintf(stderr, "Error: SDL_CreateGPUGraphicsPipeline failed (%s)\n", SDL_GetError());
    }
    return pipeline;
}

static SDL_GPUSampler *gpu_sampler(gpu_t *gpu, const render_sampler_t *sampler, bool cube)
{
    gpu_sampler_key_t key;
    memset(&key, 0, sizeof(key));
    key.min_filter = (uint8_t)sampler->min_filter;
    key.mag_filter = (uint8_t)sampler->mag_filter;
    key.mip = (uint8_t)sampler->mip;
    key.address = (uint8_t)(cube ? 3 : sampler->address);
    key.anisotropic = sampler->anisotropic && sampler->mip != RENDER_MIP_NONE;
    key.cube = cube;
    for (size_t i = 0; i < gpu->samplers.count; i++) {
        if (memcmp(&gpu->samplers.items[i].key, &key, sizeof(key)) == 0) {
            return gpu->samplers.items[i].sampler;
        }
    }
    SDL_GPUSamplerCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.min_filter = key.min_filter == RENDER_FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mag_filter = key.mag_filter == RENDER_FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = key.mip == RENDER_MIP_LINEAR ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                                    : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    /* Border and mirror once are the shader's: the sampler clamps for them */
    SDL_GPUSamplerAddressMode address = key.address == 2 ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT
                                        : key.address >= 3 && key.address <= 5
                                            ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                            : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_u = info.address_mode_v = info.address_mode_w = address;
    info.min_lod = 0.0f;
    info.max_lod = key.mip == RENDER_MIP_NONE ? 0.0f : 1000.0f;
    info.enable_anisotropy = key.anisotropic != 0;
    info.max_anisotropy = 8.0f;
    SDL_GPUSampler *made = SDL_CreateGPUSampler(gpu->device, &info);
    if (made != NULL) {
        gpu_sampler_t entry = {key, made};
        if (!GPU_PUSH(gpu->samplers, entry)) {
            SDL_ReleaseGPUSampler(gpu->device, made);
            return NULL;
        }
    }
    return made;
}

/* The depth and stencil texture for targets of a size; cleared the first time it is drawn with */
static gpu_depth_t *gpu_target_depth(gpu_t *gpu, int32_t width, int32_t height)
{
    for (size_t i = 0; i < gpu->depths.count; i++) {
        if (gpu->depths.items[i].width == width && gpu->depths.items[i].height == height) {
            return &gpu->depths.items[i];
        }
    }
    gpu_depth_t depth = {width, height,
                         gpu_make_texture(gpu, RENDER_TEXTURE_2D, width, height, 1,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, gpu->depth_format),
                         true};
    if (depth.texture == NULL || !GPU_PUSH(gpu->depths, depth)) {
        if (depth.texture != NULL) {
            SDL_ReleaseGPUTexture(gpu->device, depth.texture);
        }
        return NULL;
    }
    return &gpu->depths.items[gpu->depths.count - 1];
}

/* ============================================================================
 * Textures
 * ============================================================================ */

static void gpu_free_images(gpu_texture_t *texture)
{
    for (int face = 0; face < 6; face++) {
        for (int level = 0; level < GPU_MAX_LEVELS; level++) {
            free(texture->images[face][level].rgba);
            texture->images[face][level].rgba = NULL;
        }
    }
}

static uintptr_t gpu_texture_create(render_backend_t *backend)
{
    (void)backend;
    return (uintptr_t)calloc(1, sizeof(gpu_texture_t));
}

static void gpu_texture_free(gpu_t *gpu, gpu_texture_t *texture)
{
    if (texture->texture != NULL) {
        SDL_ReleaseGPUTexture(gpu->device, texture->texture);
    }
    gpu_free_images(texture);
    free(texture);
}

static void gpu_texture_destroy(render_backend_t *backend, uintptr_t handle)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_texture_t *texture = (gpu_texture_t *)handle;
    if (texture == NULL) {
        return;
    }
    if (gpu->in_frame && GPU_PUSH(gpu->releases, texture)) {
        return; /* the recorded frame may still name it */
    }
    gpu_texture_free(gpu, texture);
}

static void gpu_texture_image(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                              int32_t face, int32_t level, int32_t width, int32_t height,
                              const uint8_t *rgba)
{
    (void)backend;
    gpu_texture_t *texture = (gpu_texture_t *)handle;
    if (texture == NULL || face < 0 || face >= 6 || level < 0 || level >= GPU_MAX_LEVELS || width <= 0 ||
        height <= 0 || rgba == NULL) {
        return;
    }
    struct gpu_image *image = &texture->images[face][level];
    size_t size = (size_t)width * (size_t)height * 4u;
    uint8_t *copy = (uint8_t *)realloc(image->rgba, size);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, rgba, size);
    image->rgba = copy;
    image->width = width;
    image->height = height;
    (void)kind;
}

/* The images given since the texture was last made, uploaded with the frame: made again when its
   shape changes */
static void gpu_texture_levels(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                               int32_t last)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_texture_t *texture = (gpu_texture_t *)handle;
    if (texture == NULL || texture->images[0][0].rgba == NULL) {
        return;
    }
    int32_t faces = kind == RENDER_TEXTURE_CUBE ? 6 : 1;
    int32_t levels = 1;
    while (levels <= last && levels < GPU_MAX_LEVELS && texture->images[0][levels].rgba != NULL) {
        levels++;
    }
    int32_t width = texture->images[0][0].width, height = texture->images[0][0].height;
    if (texture->texture == NULL || texture->kind != kind || texture->width != width ||
        texture->height != height || texture->levels != levels || texture->target) {
        if (texture->texture != NULL) {
            SDL_ReleaseGPUTexture(gpu->device, texture->texture);
        }
        texture->texture = gpu_make_texture(gpu, kind, width, height, levels, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                            GPU_COLOR_FORMAT);
        texture->kind = kind;
        texture->width = width;
        texture->height = height;
        texture->levels = levels;
        texture->target = false;
    }
    if (texture->texture == NULL) {
        gpu_free_images(texture);
        return;
    }
    for (int32_t face = 0; face < faces; face++) {
        for (int32_t level = 0; level < levels; level++) {
            struct gpu_image *image = &texture->images[face][level];
            if (image->rgba == NULL) {
                continue;
            }
            size_t size = (size_t)image->width * (size_t)image->height * 4u;
            gpu_upload_t upload = {texture, face, level, image->width, image->height, gpu->data.count};
            if (gpu_grow((void **)&gpu->data.items, &gpu->data.capacity, gpu->data.count + size, 1) &&
                GPU_PUSH(gpu->uploads, upload)) {
                memcpy(gpu->data.items + gpu->data.count, image->rgba, size);
                gpu->data.count += size;
            }
        }
    }
    gpu_free_images(texture);
    gpu->in_frame = true;
}

static void gpu_texture_sub_image(render_backend_t *backend, uintptr_t handle, int32_t x, int32_t y,
                                  int32_t width, int32_t height, const uint8_t *rgba)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_texture_t *texture = (gpu_texture_t *)handle;
    if (texture == NULL || texture->texture == NULL || width <= 0 || height <= 0 || x < 0 || y < 0 ||
        x + width > texture->width || y + height > texture->height) {
        return;
    }
    size_t size = (size_t)width * (size_t)height * 4u;
    gpu_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = GPU_OP_UPLOAD;
    op.texture = texture;
    op.rect[0] = x, op.rect[1] = y, op.rect[2] = width, op.rect[3] = height;
    op.data = gpu->data.count;
    if (gpu_grow((void **)&gpu->data.items, &gpu->data.capacity, gpu->data.count + size, 1) &&
        GPU_PUSH(gpu->ops, op)) {
        memcpy(gpu->data.items + gpu->data.count, rgba, size);
        gpu->data.count += size;
        gpu->in_frame = true;
    }
}

static void gpu_texture_target(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                               int32_t width, int32_t height, bool keep_faces)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_texture_t *texture = (gpu_texture_t *)handle;
    if (texture == NULL || width <= 0 || height <= 0) {
        return;
    }
    if (keep_faces && texture->target && texture->texture != NULL && texture->kind == kind &&
        texture->width == width && texture->height == height) {
        return;
    }
    if (texture->texture != NULL) {
        SDL_ReleaseGPUTexture(gpu->device, texture->texture);
    }
    texture->texture = gpu_make_texture(gpu, kind, width, height, 1,
                                        SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                                        GPU_COLOR_FORMAT);
    texture->kind = kind;
    texture->width = width;
    texture->height = height;
    texture->levels = 1;
    texture->target = true;
    texture->fresh = true;
    gpu_free_images(texture);
}

/* ============================================================================
 * Recording a frame
 * ============================================================================ */

static void gpu_record(gpu_t *gpu, const gpu_op_t *op)
{
    if (GPU_PUSH(gpu->ops, *op)) {
        gpu->in_frame = true;
    }
}

static bool gpu_begin_screen(render_backend_t *backend, int32_t width, int32_t height, bool preserve)
{
    gpu_t *gpu = (gpu_t *)backend;
    if (gpu->screen == NULL || gpu->screen_w != width || gpu->screen_h != height) {
        if (gpu->screen != NULL) {
            SDL_ReleaseGPUTexture(gpu->device, gpu->screen);
            SDL_ReleaseGPUTexture(gpu->device, gpu->screen_depth);
        }
        gpu->screen = gpu_make_texture(gpu, RENDER_TEXTURE_2D, width, height, 1,
                                       SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                       GPU_COLOR_FORMAT);
        gpu->screen_depth = gpu_make_texture(gpu, RENDER_TEXTURE_2D, width, height, 1,
                                             SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, gpu->depth_format);
        gpu->screen_w = width;
        gpu->screen_h = height;
        gpu->screen_fresh = true;
    }
    gpu_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = GPU_OP_BEGIN;
    gpu_record(gpu, &op);
    gpu->screen_open = true;
    return preserve && gpu->screen != NULL;
}

static void gpu_replay(gpu_t *gpu);

static void gpu_end_screen(render_backend_t *backend)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = GPU_OP_END;
    gpu_record(gpu, &op);
    gpu->screen_open = false;
    gpu_replay(gpu);
}

static bool gpu_begin_target(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                             int32_t face, int32_t width, int32_t height)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_texture_t *texture = (gpu_texture_t *)handle;
    if (texture == NULL || texture->texture == NULL || !texture->target || texture->kind != kind ||
        texture->width != width || texture->height != height || face < 0 || face >= 6) {
        return false;
    }
    gpu_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = GPU_OP_BEGIN;
    op.texture = texture;
    op.face = face;
    gpu_record(gpu, &op);
    return true;
}

static void gpu_end_target(render_backend_t *backend)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = GPU_OP_END;
    gpu_record(gpu, &op);
    if (gpu->screen_open) { /* back to the screen it was drawn in the middle of */
        op.kind = GPU_OP_BEGIN;
        gpu_record(gpu, &op);
    }
}

static bool gpu_push_state(gpu_t *gpu, const render_state_t *state, size_t *out)
{
    *out = gpu->states.count;
    return GPU_PUSH(gpu->states, *state);
}

static void gpu_clear(render_backend_t *backend, const render_state_t *state, bool color, bool depth,
                      bool stencil, const float value[4])
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = GPU_OP_CLEAR;
    op.clear_color = color && state->color_write;
    op.clear_depth = depth && state->depth_write;
    op.clear_stencil = stencil && (state->stencil_write_mask & 0xFFu) != 0;
    memcpy(op.clear_value, value, sizeof(op.clear_value));
    if (!op.clear_color && !op.clear_depth && !op.clear_stencil) {
        return;
    }
    /* A quad over the whole target at the far plane, in clip space, in the clear color */
    size_t first = gpu->vertices.count, first_index = gpu->indices.count;
    if (!gpu_grow((void **)&gpu->vertices.items, &gpu->vertices.capacity, first + 4u, sizeof(gpu_vertex_t)) ||
        !gpu_grow((void **)&gpu->indices.items, &gpu->indices.capacity, first_index + 6u, sizeof(uint16_t))) {
        return;
    }
    static const float corners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    for (int i = 0; i < 4; i++) {
        gpu_vertex_t *v = &gpu->vertices.items[first + (size_t)i];
        memset(v, 0, sizeof(*v));
        v->position[0] = corners[i][0];
        v->position[1] = corners[i][1];
        v->position[2] = 1.0f;
        memcpy(v->color, value, sizeof(v->color));
    }
    static const uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
    memcpy(&gpu->indices.items[first_index], quad, sizeof(quad));
    gpu->vertices.count = first + 4u;
    gpu->indices.count = first_index + 6u;
    op.first_vertex = (uint32_t)first;
    op.vertex_count = 4;
    op.first_index = (uint32_t)first_index;
    op.index_count = 6;
    if (gpu_push_state(gpu, state, &op.state)) {
        gpu_record(gpu, &op);
    }
}

static const float *gpu_stream(const void *base, size_t stride, int32_t index)
{
    return (const float *)((const uint8_t *)base + (size_t)index * stride);
}

/* The draw's vertices in the frame's, with the state's color and specular where it has none */
static bool gpu_vertices(gpu_t *gpu, const render_state_t *s, const render_geometry_t *g,
                         uint32_t *out_first)
{
    size_t first = gpu->vertices.count;
    if (!gpu_grow((void **)&gpu->vertices.items, &gpu->vertices.capacity, first + (size_t)g->vertex_count,
                  sizeof(gpu_vertex_t))) {
        return false;
    }
    for (int32_t i = 0; i < g->vertex_count; i++) {
        gpu_vertex_t *v = &gpu->vertices.items[first + (size_t)i];
        memset(v, 0, sizeof(*v));
        memcpy(v->position, gpu_stream(g->positions, g->position_stride, i), sizeof(v->position));
        if (g->normals != NULL) {
            memcpy(v->normal, gpu_stream(g->normals, g->normal_stride, i), sizeof(v->normal));
        }
        if (g->uvs != NULL) {
            memcpy(v->uv, gpu_stream(g->uvs, g->uv_stride, i), sizeof(v->uv));
        }
        if (g->float_colors != NULL) {
            memcpy(v->color, &g->float_colors[(size_t)i * 4u], sizeof(v->color));
        } else if (g->colors != NULL) {
            for (int c = 0; c < 4; c++) {
                v->color[c] = (float)g->colors[(size_t)i * 4u + (size_t)c] / 255.0f;
            }
        } else {
            memcpy(v->color, s->color, sizeof(v->color));
        }
        const uint8_t *spec = g->speculars != NULL ? &g->speculars[(size_t)i * g->specular_stride] : s->secondary;
        for (int c = 0; c < 3; c++) {
            v->specular[c] = (float)spec[c] / 255.0f;
        }
    }
    gpu->vertices.count = first + (size_t)g->vertex_count;
    *out_first = (uint32_t)first;
    return true;
}

static void gpu_draw(render_backend_t *backend, const render_state_t *state, const render_geometry_t *g)
{
    gpu_t *gpu = (gpu_t *)backend;
    if (g->positions == NULL || g->vertex_count <= 0) {
        return;
    }
    gpu_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = GPU_OP_DRAW;
    op.primitive = g->primitive == RENDER_PRIMITIVE_LINES ? SDL_GPU_PRIMITIVETYPE_LINELIST
                                                          : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    if (!gpu_vertices(gpu, state, g, &op.first_vertex)) {
        return;
    }
    op.vertex_count = (uint32_t)g->vertex_count;
    /* Indices: the geometry's, or a fan's as a list */
    int32_t count = g->indices != NULL ? g->index_count : g->vertex_count;
    if (g->primitive == RENDER_PRIMITIVE_TRIANGLE_FAN) {
        if (count < 3) {
            return;
        }
        size_t first = gpu->indices.count, needed = (size_t)(count - 2) * 3u;
        if (!gpu_grow((void **)&gpu->indices.items, &gpu->indices.capacity, first + needed, sizeof(uint16_t))) {
            return;
        }
        for (int32_t i = 1; i + 1 < count; i++) {
            uint16_t *tri = &gpu->indices.items[first + (size_t)(i - 1) * 3u];
            tri[0] = g->indices != NULL ? g->indices[0] : 0;
            tri[1] = g->indices != NULL ? g->indices[i] : (uint16_t)i;
            tri[2] = g->indices != NULL ? g->indices[i + 1] : (uint16_t)(i + 1);
        }
        gpu->indices.count = first + needed;
        op.first_index = (uint32_t)first;
        op.index_count = (uint32_t)needed;
    } else if (g->indices != NULL) {
        size_t first = gpu->indices.count;
        if (!gpu_grow((void **)&gpu->indices.items, &gpu->indices.capacity, first + (size_t)count,
                      sizeof(uint16_t))) {
            return;
        }
        memcpy(&gpu->indices.items[first], g->indices, (size_t)count * sizeof(uint16_t));
        gpu->indices.count = first + (size_t)count;
        op.first_index = (uint32_t)first;
        op.index_count = (uint32_t)count;
    }
    if (gpu_push_state(gpu, state, &op.state)) {
        gpu_record(gpu, &op);
    }
}

/* ============================================================================
 * Replaying a frame
 * ============================================================================ */

/* The uniforms of fixed.vert and fixed.frag (std140) */
typedef struct gpu_vertex_uniforms {
    float model_view[16], projection[16], normal_matrix[16], texture_matrix[16];
    float fog[4];
    int32_t mode[4];
} gpu_vertex_uniforms_t;

typedef struct gpu_fragment_uniforms {
    float fog_color[4], border_color[4], alpha[4];
    int32_t texture[4], flags[4];
} gpu_fragment_uniforms_t;

typedef struct gpu_replay_pass {
    SDL_GPURenderPass *pass;
    const gpu_op_t *begin; /* what it draws into */
    int32_t width, height;
} gpu_replay_pass_t;

static bool gpu_begin_pass(gpu_t *gpu, SDL_GPUCommandBuffer *cmd, const gpu_op_t *begin,
                           gpu_replay_pass_t *out)
{
    SDL_GPUColorTargetInfo color;
    SDL_GPUDepthStencilTargetInfo depth;
    memset(&color, 0, sizeof(color));
    memset(&depth, 0, sizeof(depth));
    color.load_op = SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    depth.load_op = depth.stencil_load_op = SDL_GPU_LOADOP_LOAD;
    depth.store_op = depth.stencil_store_op = SDL_GPU_STOREOP_STORE;
    depth.clear_depth = 1.0f;
    if (begin->texture == NULL) {
        if (gpu->screen == NULL || gpu->screen_depth == NULL) {
            return false;
        }
        color.texture = gpu->screen;
        depth.texture = gpu->screen_depth;
        if (gpu->screen_fresh) { /* black, far and 0, as a new window's buffers */
            color.load_op = depth.load_op = depth.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
            color.clear_color.a = 1.0f;
            gpu->screen_fresh = false;
        }
        out->width = gpu->screen_w;
        out->height = gpu->screen_h;
    } else {
        gpu_texture_t *texture = begin->texture;
        gpu_depth_t *target_depth = gpu_target_depth(gpu, texture->width, texture->height);
        if (texture->texture == NULL || target_depth == NULL) {
            return false;
        }
        color.texture = texture->texture;
        color.layer_or_depth_plane = (Uint32)begin->face;
        depth.texture = target_depth->texture;
        if (texture->fresh) {
            color.load_op = SDL_GPU_LOADOP_CLEAR;
            texture->fresh = false;
        }
        if (target_depth->fresh) {
            depth.load_op = depth.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
            target_depth->fresh = false;
        }
        out->width = texture->width;
        out->height = texture->height;
    }
    out->pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
    out->begin = begin;
    if (out->pass != NULL) {
        SDL_BindGPUVertexBuffers(out->pass, 0, &(SDL_GPUBufferBinding){gpu->vertex_buffer, 0}, 1);
        if (gpu->index_buffer != NULL) {
            SDL_BindGPUIndexBuffer(out->pass, &(SDL_GPUBufferBinding){gpu->index_buffer, 0},
                                   SDL_GPU_INDEXELEMENTSIZE_16BIT);
        }
    }
    return out->pass != NULL;
}

/* The pass's scissor: the state's, inside the target; false when nothing is left */
static bool gpu_scissor(const render_state_t *s, const gpu_replay_pass_t *pass, SDL_Rect *out)
{
    int32_t x0 = 0, y0 = 0, x1 = pass->width, y1 = pass->height;
    if (s->scissor) {
        x0 = s->scissor_rect[0] > x0 ? s->scissor_rect[0] : x0;
        y0 = s->scissor_rect[1] > y0 ? s->scissor_rect[1] : y0;
        x1 = s->scissor_rect[0] + s->scissor_rect[2] < x1 ? s->scissor_rect[0] + s->scissor_rect[2] : x1;
        y1 = s->scissor_rect[1] + s->scissor_rect[3] < y1 ? s->scissor_rect[1] + s->scissor_rect[3] : y1;
    }
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    return out->w > 0 && out->h > 0;
}

static void gpu_bind_textures(gpu_t *gpu, SDL_GPURenderPass *pass, const render_state_t *s,
                              gpu_fragment_uniforms_t *fu)
{
    const gpu_texture_t *texture = s->texture_kind != RENDER_TEXTURE_NONE ? (const gpu_texture_t *)s->texture
                                                                          : NULL;
    bool cube = texture != NULL && texture->kind == RENDER_TEXTURE_CUBE;
    SDL_GPUTextureSamplerBinding bindings[2];
    SDL_GPUSampler *sampler_2d = gpu_sampler(gpu, &s->sampler, false);
    SDL_GPUSampler *sampler_cube = gpu_sampler(gpu, &s->sampler, true);
    bindings[0].texture = texture != NULL && !cube && texture->texture != NULL ? texture->texture : gpu->white_2d;
    bindings[0].sampler = sampler_2d;
    bindings[1].texture = texture != NULL && cube && texture->texture != NULL ? texture->texture : gpu->white_cube;
    bindings[1].sampler = sampler_cube;
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
    fu->texture[0] = texture == NULL || texture->texture == NULL ? 0 : cube ? 2 : 1;
    fu->texture[1] = (int32_t)s->texenv;
    fu->texture[2] = s->sampler.address;
}

static void gpu_replay_draw(gpu_t *gpu, SDL_GPUCommandBuffer *cmd, const gpu_replay_pass_t *pass,
                            const gpu_op_t *op)
{
    const render_state_t *s = &gpu->states.items[op->state];
    bool clear = op->kind == GPU_OP_CLEAR;
    SDL_Rect scissor;
    if (!gpu_scissor(s, pass, &scissor)) {
        return;
    }
    gpu_pipeline_key_t key;
    memset(&key, 0, sizeof(key));
    if (clear) {
        /* A quad over the whole target at the far plane, cut by the scissor */
        key.primitive = (uint8_t)SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        key.color_write = op->clear_color;
        key.depth_test = op->clear_depth;
        key.depth_func = 8;
        key.depth_write = op->clear_depth;
        key.stencil = op->clear_stencil;
        key.stencil_func = 8;
        key.stencil_fail = key.stencil_zfail = 3;
        key.stencil_pass = 3;
        key.stencil_compare_mask = 0xFF;
        key.stencil_write_mask = op->clear_stencil ? (uint8_t)s->stencil_write_mask : 0;
    } else {
        key.primitive = (uint8_t)op->primitive;
        key.fill = s->fill != RENDER_FILL_SOLID;
        key.cull = s->cull;
        key.front_ccw = s->front_ccw;
        key.flat = s->flat;
        key.blend = s->blend;
        if (s->blend) {
            key.source_blend = (uint8_t)s->source_blend;
            key.dest_blend = (uint8_t)s->dest_blend;
        }
        key.color_write = s->color_write;
        key.depth_test = s->depth_test;
        if (s->depth_test) {
            key.depth_func = (uint8_t)s->depth_func;
            key.depth_write = s->depth_write;
        }
        key.stencil = s->stencil;
        if (s->stencil) {
            key.stencil_func = (uint8_t)s->stencil_func;
            key.stencil_fail = (uint8_t)s->stencil_fail;
            key.stencil_zfail = (uint8_t)s->stencil_zfail;
            key.stencil_pass = (uint8_t)s->stencil_pass;
            key.stencil_compare_mask = (uint8_t)s->stencil_mask;
            key.stencil_write_mask = (uint8_t)s->stencil_write_mask;
        }
    }
    SDL_GPUGraphicsPipeline *pipeline = gpu_pipeline(gpu, &key);
    if (pipeline == NULL) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass->pass, pipeline);
    SDL_GPUViewport viewport;
    if (clear) {
        viewport = (SDL_GPUViewport){0.0f, 0.0f, (float)pass->width, (float)pass->height, 0.0f, 1.0f};
    } else {
        viewport = (SDL_GPUViewport){(float)s->viewport[0], (float)s->viewport[1], (float)s->viewport[2],
                                     (float)s->viewport[3], 0.0f, 1.0f};
    }
    SDL_SetGPUViewport(pass->pass, &viewport);
    SDL_SetGPUScissor(pass->pass, &scissor);
    SDL_SetGPUStencilReference(pass->pass, (Uint8)(clear ? s->clear_stencil : s->stencil_ref));

    gpu_vertex_uniforms_t vu;
    gpu_fragment_uniforms_t fu;
    memset(&vu, 0, sizeof(vu));
    memset(&fu, 0, sizeof(fu));
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (clear) {
        memcpy(vu.model_view, identity, sizeof(identity));
        memcpy(vu.projection, identity, sizeof(identity));
        memcpy(vu.normal_matrix, identity, sizeof(identity));
        memcpy(vu.texture_matrix, identity, sizeof(identity));
        SDL_BindGPUFragmentSamplers(
            pass->pass, 0,
            (SDL_GPUTextureSamplerBinding[2]){{gpu->white_2d, gpu_sampler(gpu, &s->sampler, false)},
                                             {gpu->white_cube, gpu_sampler(gpu, &s->sampler, true)}},
            2);
    } else {
        memcpy(vu.model_view, s->model_view, sizeof(vu.model_view));
        render_clip_projection(s, vu.projection);
        if (!render_affine_inverse(vu.normal_matrix, s->model_view)) {
            memcpy(vu.normal_matrix, identity, sizeof(identity));
        }
        memcpy(vu.texture_matrix, s->texture_matrix, sizeof(vu.texture_matrix));
        vu.mode[0] = (int32_t)s->texgen;
        if (s->fog) {
            vu.fog[0] = (float)(s->fog_mode >= 1 && s->fog_mode <= 3 ? s->fog_mode : 3);
            vu.fog[1] = s->fog_start;
            vu.fog[2] = s->fog_end;
            vu.fog[3] = s->fog_density;
        }
        render_color(s->fog_color, fu.fog_color);
        render_color(s->sampler.border_color, fu.border_color);
        fu.alpha[0] = s->alpha_ref;
        fu.flags[0] = s->color_sum;
        fu.flags[1] = s->fog;
        fu.flags[2] = s->alpha_test;
        fu.flags[3] = s->alpha_func;
        gpu_bind_textures(gpu, pass->pass, s, &fu);
    }
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));
    if (op->index_count > 0) {
        SDL_DrawGPUIndexedPrimitives(pass->pass, op->index_count, 1, op->first_index,
                                     (Sint32)op->first_vertex, 0);
    } else {
        SDL_DrawGPUPrimitives(pass->pass, op->vertex_count, 1, op->first_vertex, 0);
    }
}

/* The frame's transfer buffer, `size` bytes; false when it cannot be had */
static bool gpu_transfer(gpu_t *gpu, uint32_t size)
{
    if (gpu->transfer != NULL && gpu->transfer_size >= size) {
        return true;
    }
    if (gpu->transfer != NULL) {
        SDL_ReleaseGPUTransferBuffer(gpu->device, gpu->transfer);
    }
    uint32_t grown = gpu->transfer_size > 0 ? gpu->transfer_size : 1u << 20;
    while (grown < size) {
        grown *= 2;
    }
    SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, grown, 0};
    gpu->transfer = SDL_CreateGPUTransferBuffer(gpu->device, &info);
    gpu->transfer_size = gpu->transfer != NULL ? grown : 0;
    return gpu->transfer != NULL;
}

static bool gpu_buffer(gpu_t *gpu, SDL_GPUBuffer **buffer, uint32_t *have, uint32_t size,
                       SDL_GPUBufferUsageFlags usage)
{
    if (*buffer != NULL && *have >= size) {
        return true;
    }
    if (*buffer != NULL) {
        SDL_ReleaseGPUBuffer(gpu->device, *buffer);
    }
    uint32_t grown = *have > 0 ? *have : 1u << 16;
    while (grown < size) {
        grown *= 2;
    }
    SDL_GPUBufferCreateInfo info = {usage, grown, 0};
    *buffer = SDL_CreateGPUBuffer(gpu->device, &info);
    *have = *buffer != NULL ? grown : 0;
    return *buffer != NULL;
}

static void gpu_upload_texture(SDL_GPUCopyPass *copy, SDL_GPUTransferBuffer *transfer, size_t offset,
                               SDL_GPUTexture *texture, int32_t face, int32_t level, int32_t x, int32_t y,
                               int32_t width, int32_t height)
{
    SDL_GPUTextureTransferInfo source = {transfer, (Uint32)offset, (Uint32)width, (Uint32)height};
    SDL_GPUTextureRegion destination;
    memset(&destination, 0, sizeof(destination));
    destination.texture = texture;
    destination.mip_level = (Uint32)level;
    destination.layer = (Uint32)face;
    destination.x = (Uint32)x;
    destination.y = (Uint32)y;
    destination.w = (Uint32)width;
    destination.h = (Uint32)height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
}

static void gpu_forget_frame(gpu_t *gpu)
{
    gpu->ops.count = gpu->states.count = gpu->vertices.count = gpu->indices.count = 0;
    gpu->data.count = gpu->uploads.count = 0;
    gpu->in_frame = false;
    for (size_t i = 0; i < gpu->releases.count; i++) {
        gpu_texture_free(gpu, gpu->releases.items[i]);
    }
    gpu->releases.count = 0;
}

static void gpu_replay(gpu_t *gpu)
{
    size_t vertex_bytes = gpu->vertices.count * sizeof(gpu_vertex_t);
    size_t index_bytes = ((gpu->indices.count * sizeof(uint16_t)) + 3u) & ~(size_t)3u;
    size_t total = vertex_bytes + index_bytes + gpu->data.count;
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu->device);
    if (cmd == NULL || total > 0xFFFFFFFFu ||
        !gpu_buffer(gpu, &gpu->vertex_buffer, &gpu->vertex_buffer_size,
                    (uint32_t)(vertex_bytes > 0 ? vertex_bytes : sizeof(gpu_vertex_t)), SDL_GPU_BUFFERUSAGE_VERTEX) ||
        !gpu_buffer(gpu, &gpu->index_buffer, &gpu->index_buffer_size, (uint32_t)(index_bytes > 0 ? index_bytes : 4u),
                    SDL_GPU_BUFFERUSAGE_INDEX) ||
        !gpu_transfer(gpu, (uint32_t)(total > 0 ? total : 4u))) {
        if (cmd != NULL) {
            SDL_CancelGPUCommandBuffer(cmd);
        }
        gpu_forget_frame(gpu);
        return;
    }
    /* Everything the frame uploads, in one transfer buffer: vertices, indices, then images */
    uint8_t *mapped = (uint8_t *)SDL_MapGPUTransferBuffer(gpu->device, gpu->transfer, true);
    if (mapped == NULL) {
        SDL_CancelGPUCommandBuffer(cmd);
        gpu_forget_frame(gpu);
        return;
    }
    if (vertex_bytes > 0) {
        memcpy(mapped, gpu->vertices.items, vertex_bytes);
    }
    if (gpu->indices.count > 0) {
        memcpy(mapped + vertex_bytes, gpu->indices.items, gpu->indices.count * sizeof(uint16_t));
    }
    if (gpu->data.count > 0) {
        memcpy(mapped + vertex_bytes + index_bytes, gpu->data.items, gpu->data.count);
    }
    SDL_UnmapGPUTransferBuffer(gpu->device, gpu->transfer);
    size_t data_base = vertex_bytes + index_bytes;

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (vertex_bytes > 0) {
        SDL_UploadToGPUBuffer(copy, &(SDL_GPUTransferBufferLocation){gpu->transfer, 0},
                              &(SDL_GPUBufferRegion){gpu->vertex_buffer, 0, (Uint32)vertex_bytes}, true);
    }
    if (gpu->indices.count > 0) {
        SDL_UploadToGPUBuffer(copy, &(SDL_GPUTransferBufferLocation){gpu->transfer, (Uint32)vertex_bytes},
                              &(SDL_GPUBufferRegion){gpu->index_buffer, 0, (Uint32)index_bytes}, true);
    }
    for (size_t i = 0; i < gpu->uploads.count; i++) {
        const gpu_upload_t *u = &gpu->uploads.items[i];
        if (u->texture->texture != NULL) {
            gpu_upload_texture(copy, gpu->transfer, data_base + u->data, u->texture->texture, u->face,
                               u->level, 0, 0, u->width, u->height);
        }
    }
    SDL_EndGPUCopyPass(copy);

    gpu_replay_pass_t pass;
    memset(&pass, 0, sizeof(pass));
    for (size_t i = 0; i < gpu->ops.count; i++) {
        const gpu_op_t *op = &gpu->ops.items[i];
        switch (op->kind) {
        case GPU_OP_BEGIN:
            if (pass.pass != NULL) {
                SDL_EndGPURenderPass(pass.pass);
            }
            memset(&pass, 0, sizeof(pass));
            gpu_begin_pass(gpu, cmd, op, &pass);
            break;
        case GPU_OP_END:
            if (pass.pass != NULL) {
                SDL_EndGPURenderPass(pass.pass);
            }
            memset(&pass, 0, sizeof(pass));
            break;
        case GPU_OP_CLEAR:
        case GPU_OP_DRAW:
            if (pass.pass != NULL) {
                gpu_replay_draw(gpu, cmd, &pass, op);
            }
            break;
        case GPU_OP_UPLOAD: {
            const gpu_op_t *open = pass.pass != NULL ? pass.begin : NULL;
            if (pass.pass != NULL) {
                SDL_EndGPURenderPass(pass.pass);
                pass.pass = NULL;
            }
            if (op->texture->texture != NULL) {
                copy = SDL_BeginGPUCopyPass(cmd);
                gpu_upload_texture(copy, gpu->transfer, data_base + op->data, op->texture->texture, op->face,
                                   op->level, op->rect[0], op->rect[1], op->rect[2], op->rect[3]);
                SDL_EndGPUCopyPass(copy);
            }
            if (open != NULL) {
                gpu_begin_pass(gpu, cmd, open, &pass);
            }
            break;
        }
        }
    }
    if (pass.pass != NULL) {
        SDL_EndGPURenderPass(pass.pass);
    }

    if (gpu->screen != NULL) {
        /* The screen kept for the next frame's copies of the frame before */
        if (gpu->last == NULL || gpu->last_w != gpu->screen_w || gpu->last_h != gpu->screen_h) {
            if (gpu->last != NULL) {
                SDL_ReleaseGPUTexture(gpu->device, gpu->last);
            }
            gpu->last = gpu_make_texture(gpu, RENDER_TEXTURE_2D, gpu->screen_w, gpu->screen_h, 1,
                                         SDL_GPU_TEXTUREUSAGE_SAMPLER, GPU_COLOR_FORMAT);
            gpu->last_w = gpu->screen_w;
            gpu->last_h = gpu->screen_h;
            gpu->last_valid = false;
        }
        if (gpu->last != NULL) {
            copy = SDL_BeginGPUCopyPass(cmd);
            SDL_CopyGPUTextureToTexture(copy, &(SDL_GPUTextureLocation){gpu->screen, 0, 0, 0, 0, 0},
                                        &(SDL_GPUTextureLocation){gpu->last, 0, 0, 0, 0, 0},
                                        (Uint32)gpu->screen_w, (Uint32)gpu->screen_h, 1, false);
            SDL_EndGPUCopyPass(copy);
            gpu->last_valid = true;
        }
        if (gpu->window != NULL) {
            SDL_GPUTexture *swapchain = NULL;
            Uint32 sw = 0, sh = 0;
            if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, gpu->window, &swapchain, &sw, &sh) &&
                swapchain != NULL) {
                SDL_GPUBlitInfo blit;
                memset(&blit, 0, sizeof(blit));
                blit.source.texture = gpu->screen;
                blit.source.w = (Uint32)gpu->screen_w;
                blit.source.h = (Uint32)gpu->screen_h;
                blit.destination.texture = swapchain;
                blit.destination.w = sw;
                blit.destination.h = sh;
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_LINEAR;
                SDL_BlitGPUTexture(cmd, &blit);
            }
        }
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    gpu_forget_frame(gpu);
}

/* ============================================================================
 * Reading
 * ============================================================================ */

/* `rect` of a texture, read back once the device has drawn it: RGBA rows top first */
static bool gpu_read(gpu_t *gpu, SDL_GPUTexture *texture, const int32_t rect[4], uint8_t *rgba)
{
    if (texture == NULL || rect[2] <= 0 || rect[3] <= 0) {
        return false;
    }
    uint32_t size = (uint32_t)rect[2] * (uint32_t)rect[3] * 4u;
    SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, size, 0};
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu->device, &info);
    SDL_GPUCommandBuffer *cmd = transfer != NULL ? SDL_AcquireGPUCommandBuffer(gpu->device) : NULL;
    if (cmd == NULL) {
        if (transfer != NULL) {
            SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
        }
        return false;
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion source;
    memset(&source, 0, sizeof(source));
    source.texture = texture;
    source.x = (Uint32)rect[0];
    source.y = (Uint32)rect[1];
    source.w = (Uint32)rect[2];
    source.h = (Uint32)rect[3];
    source.d = 1;
    SDL_GPUTextureTransferInfo destination = {transfer, 0, (Uint32)rect[2], (Uint32)rect[3]};
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    bool read = false;
    if (fence != NULL && SDL_WaitForGPUFences(gpu->device, true, &fence, 1)) {
        const uint8_t *mapped = (const uint8_t *)SDL_MapGPUTransferBuffer(gpu->device, transfer, false);
        if (mapped != NULL) {
            memcpy(rgba, mapped, size);
            SDL_UnmapGPUTransferBuffer(gpu->device, transfer);
            read = true;
        }
    }
    if (fence != NULL) {
        SDL_ReleaseGPUFence(gpu->device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
    return read;
}

static bool gpu_last_frame_ready(render_backend_t *backend, int32_t width, int32_t height)
{
    const gpu_t *gpu = (const gpu_t *)backend;
    return gpu->last_valid && gpu->last != NULL && gpu->last_w == width && gpu->last_h == height;
}

static bool gpu_read_last_frame(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    gpu_t *gpu = (gpu_t *)backend;
    if (rect[0] < 0 || rect[1] < 0 || rect[0] + rect[2] > gpu->last_w || rect[1] + rect[3] > gpu->last_h) {
        return false;
    }
    return gpu_read(gpu, gpu->last, rect, rgba);
}

static nmo_status_t gpu_read_pixels(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    gpu_t *gpu = (gpu_t *)backend;
    if (rect[0] < 0 || rect[1] < 0 || rect[0] + rect[2] > gpu->screen_w || rect[1] + rect[3] > gpu->screen_h) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return gpu_read(gpu, gpu->screen, rect, rgba) ? NMO_OK : NMO_ERR_INVALID_STATE;
}

/* ============================================================================
 * The backend
 * ============================================================================ */

static void gpu_destroy(render_backend_t *backend)
{
    gpu_t *gpu = (gpu_t *)backend;
    gpu_forget_frame(gpu);
    SDL_WaitForGPUIdle(gpu->device);
    for (size_t i = 0; i < gpu->pipelines.count; i++) {
        SDL_ReleaseGPUGraphicsPipeline(gpu->device, gpu->pipelines.items[i].pipeline);
    }
    for (size_t i = 0; i < gpu->samplers.count; i++) {
        SDL_ReleaseGPUSampler(gpu->device, gpu->samplers.items[i].sampler);
    }
    for (size_t i = 0; i < gpu->depths.count; i++) {
        SDL_ReleaseGPUTexture(gpu->device, gpu->depths.items[i].texture);
    }
    SDL_GPUTexture *textures[5] = {gpu->screen, gpu->screen_depth, gpu->last, gpu->white_2d, gpu->white_cube};
    for (int i = 0; i < 5; i++) {
        if (textures[i] != NULL) {
            SDL_ReleaseGPUTexture(gpu->device, textures[i]);
        }
    }
    for (int i = 0; i < 2; i++) {
        if (gpu->vertex[i] != NULL) {
            SDL_ReleaseGPUShader(gpu->device, gpu->vertex[i]);
        }
        if (gpu->fragment[i] != NULL) {
            SDL_ReleaseGPUShader(gpu->device, gpu->fragment[i]);
        }
    }
    if (gpu->vertex_buffer != NULL) {
        SDL_ReleaseGPUBuffer(gpu->device, gpu->vertex_buffer);
    }
    if (gpu->index_buffer != NULL) {
        SDL_ReleaseGPUBuffer(gpu->device, gpu->index_buffer);
    }
    if (gpu->transfer != NULL) {
        SDL_ReleaseGPUTransferBuffer(gpu->device, gpu->transfer);
    }
    if (gpu->window != NULL) {
        SDL_ReleaseWindowFromGPUDevice(gpu->device, gpu->window);
    }
    if (gpu->own_device) {
        SDL_DestroyGPUDevice(gpu->device);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    free(gpu->ops.items);
    free(gpu->states.items);
    free(gpu->vertices.items);
    free(gpu->indices.items);
    free(gpu->data.items);
    free(gpu->uploads.items);
    free(gpu->releases.items);
    free(gpu->pipelines.items);
    free(gpu->samplers.items);
    free(gpu->depths.items);
    free(gpu);
}

static const render_backend_ops_t gpu_ops = {
    gpu_destroy,
    gpu_texture_create,
    gpu_texture_destroy,
    gpu_texture_image,
    gpu_texture_levels,
    gpu_texture_sub_image,
    gpu_texture_target,
    gpu_begin_screen,
    gpu_end_screen,
    gpu_begin_target,
    gpu_end_target,
    gpu_clear,
    gpu_draw,
    gpu_last_frame_ready,
    gpu_read_last_frame,
    gpu_read_pixels,
};

/* A white texel in each kind of texture, bound where a draw has none */
static bool gpu_make_white(gpu_t *gpu)
{
    gpu->white_2d = gpu_make_texture(gpu, RENDER_TEXTURE_2D, 1, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                     GPU_COLOR_FORMAT);
    gpu->white_cube = gpu_make_texture(gpu, RENDER_TEXTURE_CUBE, 1, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                       GPU_COLOR_FORMAT);
    if (gpu->white_2d == NULL || gpu->white_cube == NULL) {
        return false;
    }
    SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, 7u * 4u, 0};
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu->device, &info);
    SDL_GPUCommandBuffer *cmd = transfer != NULL ? SDL_AcquireGPUCommandBuffer(gpu->device) : NULL;
    uint8_t *mapped = cmd != NULL ? (uint8_t *)SDL_MapGPUTransferBuffer(gpu->device, transfer, false) : NULL;
    if (mapped == NULL) {
        if (cmd != NULL) {
            SDL_CancelGPUCommandBuffer(cmd);
        }
        if (transfer != NULL) {
            SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
        }
        return false;
    }
    memset(mapped, 0xFF, 7u * 4u);
    SDL_UnmapGPUTransferBuffer(gpu->device, transfer);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    gpu_upload_texture(copy, transfer, 0, gpu->white_2d, 0, 0, 0, 0, 1, 1);
    for (int32_t face = 0; face < 6; face++) {
        gpu_upload_texture(copy, transfer, (size_t)(face + 1) * 4u, gpu->white_cube, face, 0, 0, 0, 1, 1);
    }
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
    return true;
}

uint32_t nmo_render_gpu_shader_formats(void)
{
    return SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
}

/* The backend on `device`, the window claimed; NULL when it cannot be set up (an own device goes) */
static gpu_t *gpu_make(SDL_GPUDevice *device, SDL_Window *window, bool own_device)
{
    gpu_t *gpu = (gpu_t *)calloc(1, sizeof(*gpu));
    if (gpu == NULL) {
        if (own_device) {
            SDL_DestroyGPUDevice(device);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        return NULL;
    }
    gpu->base.ops = &gpu_ops;
    gpu->device = device;
    gpu->own_device = own_device;
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    gpu->format = (formats & SDL_GPU_SHADERFORMAT_MSL) ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;
    gpu->depth_format = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                                     SDL_GPU_TEXTURETYPE_2D,
                                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                            ? SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
                            : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    bool ok = (formats & (SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV)) != 0;
    for (int flat = 0; ok && flat < 2; flat++) {
        gpu->vertex[flat] = gpu_shader(gpu, false, flat != 0);
        gpu->fragment[flat] = gpu_shader(gpu, true, flat != 0);
        ok = gpu->vertex[flat] != NULL && gpu->fragment[flat] != NULL;
    }
    ok = ok && gpu_make_white(gpu);
    if (ok && window != NULL) {
        ok = SDL_ClaimWindowForGPUDevice(device, window);
        gpu->window = ok ? window : NULL;
    }
    if (!ok) {
        fprintf(stderr, "Error: Could not set up the GPU renderer (%s)\n", SDL_GetError());
        gpu_destroy(&gpu->base);
        return NULL;
    }
    return gpu;
}

static nmo_status_t gpu_create(nmo_engine_t *engine, SDL_GPUDevice *device, SDL_Window *window,
                               bool own_device, nmo_render_t **out_render)
{
    gpu_t *gpu = gpu_make(device, window, own_device);
    if (gpu == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    return render_create(engine, &gpu->base, out_render);
}

nmo_status_t nmo_render_create_gpu(nmo_engine_t *engine, SDL_GPUDevice *device, SDL_Window *window,
                                   nmo_render_t **out_render)
{
    if (engine == NULL || device == NULL || out_render == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_render = NULL;
    return gpu_create(engine, device, window, false, out_render);
}

/* A device of the backend's own (SDL makes GPU devices with its video subsystem up) */
static SDL_GPUDevice *gpu_own_device(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error: No video subsystem (%s)\n", SDL_GetError());
        return NULL;
    }
    SDL_GPUDevice *device = SDL_CreateGPUDevice(nmo_render_gpu_shader_formats(), false, NULL);
    if (device == NULL) {
        fprintf(stderr, "Error: No GPU device (%s)\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return device;
}

nmo_status_t nmo_render_create_gpu_offscreen(nmo_engine_t *engine, nmo_render_t **out_render)
{
    if (engine == NULL || out_render == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_render = NULL;
    SDL_GPUDevice *device = gpu_own_device();
    if (device == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    return gpu_create(engine, device, NULL, true, out_render);
}

render_backend_t *render_gpu_backend_create_offscreen(void)
{
    SDL_GPUDevice *device = gpu_own_device();
    gpu_t *gpu = device != NULL ? gpu_make(device, NULL, true) : NULL;
    return gpu != NULL ? &gpu->base : NULL;
}
