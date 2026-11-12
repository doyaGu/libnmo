/**
 * @file render_math.c
 * @brief Matrices and colors as Virtools has them.
 */

#include "render_math.h"

#include <math.h>
#include <string.h>

void render_color(uint32_t argb, float out[4])
{
    out[0] = (float)((argb >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((argb >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(argb & 0xFF) / 255.0f;
    out[3] = (float)(argb >> 24) / 255.0f;
}

void render_multiply(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] +
                           a[i * 4 + 2] * b[2 * 4 + j] + a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
    memcpy(out, r, sizeof(r));
}

void render_inverse(float out[16], const float m[16])
{
    float r[16] = {0};
    for (int i = 0; i < 3; i++) {
        float len2 = m[i * 4 + 0] * m[i * 4 + 0] + m[i * 4 + 1] * m[i * 4 + 1] +
                     m[i * 4 + 2] * m[i * 4 + 2];
        float s = len2 > 0.0f ? 1.0f / len2 : 0.0f;
        for (int j = 0; j < 3; j++) {
            r[j * 4 + i] = m[i * 4 + j] * s;
        }
    }
    for (int j = 0; j < 3; j++) {
        r[12 + j] = -(m[12] * r[0 * 4 + j] + m[13] * r[1 * 4 + j] + m[14] * r[2 * 4 + j]);
    }
    r[15] = 1.0f;
    memcpy(out, r, sizeof(r));
}

bool render_affine_inverse(float out[16], const float m[16])
{
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f) {
        return false;
    }
    float inv = 1.0f / det;
    float r[9] = {(e * i - f * h) * inv, (c * h - b * i) * inv, (b * f - c * e) * inv,
                  (f * g - d * i) * inv, (a * i - c * g) * inv, (c * d - a * f) * inv,
                  (d * h - e * g) * inv, (b * g - a * h) * inv, (a * e - b * d) * inv};
    memset(out, 0, 16 * sizeof(float));
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            out[row * 4 + col] = r[row * 3 + col];
        }
    }
    for (int col = 0; col < 3; col++) {
        out[12 + col] = -(m[12] * r[col] + m[13] * r[3 + col] + m[14] * r[6 + col]);
    }
    out[15] = 1.0f;
    return true;
}

void render_clip_projection(const render_state_t *s, float out[16])
{
    memset(out, 0, 16 * sizeof(float));
    if (s->projection_kind == RENDER_PROJECTION_2D) {
        /* x right, y down in render context pixels; z -1..1 to 1..0 */
        out[0] = 2.0f / s->projection_rect[0];
        out[5] = -2.0f / s->projection_rect[1];
        out[10] = -0.5f;
        out[12] = -1.0f;
        out[13] = 1.0f;
        out[14] = 0.5f;
        out[15] = 1.0f;
    } else if (s->projection_kind == RENDER_PROJECTION_SCREEN) {
        float left = s->projection_rect[0], top = s->projection_rect[1];
        float width = s->projection_rect[2], height = s->projection_rect[3];
        out[0] = 2.0f / width;
        out[5] = -2.0f / height;
        out[10] = 1.0f;
        out[12] = -2.0f * left / width - 1.0f;
        out[13] = 2.0f * top / height + 1.0f;
        out[15] = 1.0f;
    } else {
        memcpy(out, s->projection, 16 * sizeof(float));
        if (s->depth_scale != 0.0f) {
            out[14] *= s->depth_scale;
        }
    }
}

void render_projection(const nmo_engine_frame_t *frame, bool orthographic, float fov,
                       float near_plane, float far_plane, float out[16])
{
    float aspect = frame->viewport[2] > 0 && frame->viewport[3] > 0
                       ? (float)frame->viewport[2] / (float)frame->viewport[3]
                       : (float)frame->width / (float)frame->height;
    memset(out, 0, 16 * sizeof(float));
    float depth = far_plane - near_plane;
    if (depth <= 0.0f) {
        depth = 1.0f;
    }
    if (!orthographic) {
        double half = (double)fov * 0.5;
        float scale = (float)(cos(half) / sin(half));
        out[0] = scale;
        out[5] = scale * aspect;
        out[10] = far_plane / depth;
        out[14] = -near_plane * far_plane / depth;
        out[11] = 1.0f;
    } else {
        out[0] = frame->zoom;
        out[5] = frame->zoom * aspect;
        out[10] = 1.0f / depth;
        out[14] = -near_plane / depth;
        out[15] = 1.0f;
    }
}
