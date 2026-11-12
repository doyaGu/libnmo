/**
 * @file render_gpu.h
 * @brief The SDL_GPU backend on its own, for a test that drives
 *        render_backend.h directly.
 */

#ifndef NMO_RENDER_GPU_BACKEND_H
#define NMO_RENDER_GPU_BACKEND_H

#include "../render_backend.h"

/* A GPU backend with a device of its own and no window; NULL when there is no device. Its destroy
   op frees it. */
render_backend_t *render_gpu_backend_create_offscreen(void);

#endif /* NMO_RENDER_GPU_BACKEND_H */
