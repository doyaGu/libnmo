/**
 * @file render_math.h
 * @brief Matrices and colors as Virtools has them: row vectors, VxMatrix's
 *        rows, ARGB colors as VxColor packs them.
 */

#ifndef NMO_RENDER_MATH_H
#define NMO_RENDER_MATH_H

#include "engine/nmo_engine_render.h"

#include <stdbool.h>
#include <stdint.h>

/* An ARGB color as r, g, b, a from 0 to 1 */
void render_color(uint32_t argb, float out[4]);
/* Row-vector matrices: a then b */
void render_multiply(float out[16], const float a[16], const float b[16]);
/* The inverse of a rigid (scaled) world matrix: Vx3DInverseMatrix */
void render_inverse(float out[16], const float m[16]);
/* The inverse of an affine row-vector matrix (rotation, scale and translation); false when singular */
bool render_affine_inverse(float out[16], const float m[16]);
/*
 * VxMatrix::Perspective (or Orthographic) with the aspect of the frame's
 * viewport (RCKRenderContext::UpdateProjection): Direct3D's, depth 0..w.
 */
void render_projection(const nmo_engine_frame_t *frame, bool orthographic, float fov,
                       float near_plane, float far_plane, float out[16]);

#endif /* NMO_RENDER_MATH_H */
