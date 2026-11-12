/**
 * @file nmo_render_gpu.h
 * @brief The SDL_GPU backend of nmo_render: Direct3D 7's fixed function
 *        state drawn with shaders on Metal or Vulkan.
 */

#ifndef NMO_RENDER_GPU_H
#define NMO_RENDER_GPU_H

#include "render/nmo_render.h"

#define NMO_RENDER_GPU_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_RENDER_GPU_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_Window SDL_Window;

/** The shader formats the backend brings (SDL_GPUShaderFormat bits), for SDL_CreateGPUDevice. */
NMO_API uint32_t nmo_render_gpu_shader_formats(void);

/**
 * A renderer drawing with `device`. Each frame is drawn offscreen and then
 * shown in `window`, which the renderer claims for the device until it is
 * destroyed; with no window it stays offscreen (nmo_render_screenshot reads
 * it). The device must outlive the renderer.
 */
NMO_API nmo_status_t nmo_render_create_gpu(nmo_engine_t *engine, SDL_GPUDevice *device,
                                           SDL_Window *window, nmo_render_t **out_render);

/** A renderer with a GPU device of its own and no window: frames drawn offscreen. */
NMO_API nmo_status_t nmo_render_create_gpu_offscreen(nmo_engine_t *engine, nmo_render_t **out_render);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RENDER_GPU_H */
