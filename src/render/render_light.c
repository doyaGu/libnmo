/**
 * @file render_light.c
 * @brief Direct3D 7's vertex lighting, which GL's fixed function cannot do.
 */

#include "render_light.h"
#include "render_math.h"

#include <math.h>
#include <string.h>

render_lighting_t render_lighting(const nmo_engine_frame_t *frame, const nmo_engine_material_t *m)
{
    render_lighting_t l;
    l.frame = frame;
    l.material = m;
    l.specular = m->power > 0.05f;
    l.eye[0] = frame->view[12], l.eye[1] = frame->view[13], l.eye[2] = frame->view[14];
    float ambient[4];
    render_color(frame->ambient_light, ambient);
    l.ambient[0] = ambient[0], l.ambient[1] = ambient[1], l.ambient[2] = ambient[2];
    return l;
}

static uint8_t light_unit_byte(float v)
{
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    return (uint8_t)(v * 255.0f + 0.5f);
}

void render_light_vertex(const render_lighting_t *l, const float p[3], const float n[3],
                         uint8_t diffuse_out[4], uint8_t specular_out[4])
{
    const nmo_engine_material_t *m = l->material;
    float d[3], s[3] = {0.0f, 0.0f, 0.0f};
    for (int c = 0; c < 3; c++) {
        d[c] = m->emissive[c] + m->ambient[c] * l->ambient[c];
    }
    float to_eye[3] = {l->eye[0] - p[0], l->eye[1] - p[1], l->eye[2] - p[2]};
    float eye_len = sqrtf(to_eye[0] * to_eye[0] + to_eye[1] * to_eye[1] + to_eye[2] * to_eye[2]);
    if (eye_len > 0.0f) {
        to_eye[0] /= eye_len, to_eye[1] /= eye_len, to_eye[2] /= eye_len;
    }
    for (int32_t i = 0; i < l->frame->light_count && i < 8; i++) {
        const nmo_engine_light_t *light = &l->frame->lights[i];
        float L[3], scale = 1.0f;
        if (light->type == 3) {
            L[0] = -light->direction[0], L[1] = -light->direction[1], L[2] = -light->direction[2];
            float len = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            if (len <= 0.0f) {
                continue;
            }
            L[0] /= len, L[1] /= len, L[2] /= len;
        } else {
            L[0] = light->position[0] - p[0], L[1] = light->position[1] - p[1], L[2] = light->position[2] - p[2];
            float dist = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            if (dist > light->range) {
                continue;
            }
            if (dist > 0.0f) {
                L[0] /= dist, L[1] /= dist, L[2] /= dist;
            }
            float att = light->attenuation[0] + light->attenuation[1] * dist + light->attenuation[2] * dist * dist;
            scale = att > 0.0f ? 1.0f / att : 1.0f;
            if (light->type == 2) {
                float axis[3] = {light->direction[0], light->direction[1], light->direction[2]};
                float alen = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
                float rho = alen > 0.0f ? -(L[0] * axis[0] + L[1] * axis[1] + L[2] * axis[2]) / alen : 1.0f;
                float cos_inner = cosf(light->inner_cone * 0.5f), cos_outer = cosf(light->outer_cone * 0.5f);
                if (rho <= cos_outer) {
                    continue;
                }
                if (rho <= cos_inner) {
                    float t = cos_inner - cos_outer > 0.0f ? (rho - cos_outer) / (cos_inner - cos_outer) : 1.0f;
                    scale *= light->falloff == 1.0f ? t : powf(t, light->falloff);
                }
            }
        }
        float ndotl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
        if (ndotl <= 0.0f) {
            continue;
        }
        for (int c = 0; c < 3; c++) {
            d[c] += m->diffuse[c] * light->color[c] * ndotl * scale;
        }
        if (l->specular && light->specular) {
            float h[3] = {L[0] + to_eye[0], L[1] + to_eye[1], L[2] + to_eye[2]};
            float hlen = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            float ndoth = hlen > 0.0f ? (n[0] * h[0] + n[1] * h[1] + n[2] * h[2]) / hlen : 0.0f;
            if (ndoth > 0.0f) {
                float power = powf(ndoth, m->power) * scale;
                for (int c = 0; c < 3; c++) {
                    s[c] += m->specular[c] * light->color[c] * power;
                }
            }
        }
    }
    for (int c = 0; c < 3; c++) {
        diffuse_out[c] = light_unit_byte(d[c]);
        specular_out[c] = light_unit_byte(s[c]);
    }
    diffuse_out[3] = light_unit_byte(m->diffuse[3]);
    specular_out[3] = 255;
}

bool render_light_mesh(const render_lighting_t *l, const nmo_vertex_t *vertices, int32_t count,
                       const float world[16], uint8_t *colors, uint8_t *speculars)
{
    float inverse[16];
    if (!render_affine_inverse(inverse, world)) {
        memset(inverse, 0, sizeof(inverse));
        inverse[0] = inverse[5] = inverse[10] = inverse[15] = 1.0f;
    }
    bool any = false;
    for (int32_t v = 0; v < count; v++) {
        const nmo_vector_t *q = &vertices[v].position, *k = &vertices[v].normal;
        float p[3], n[3];
        for (int c = 0; c < 3; c++) {
            p[c] = q->x * world[c] + q->y * world[4 + c] + q->z * world[8 + c] + world[12 + c];
            /* row vectors: n (A^-1)^T */
            n[c] = k->x * inverse[c * 4 + 0] + k->y * inverse[c * 4 + 1] + k->z * inverse[c * 4 + 2];
        }
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 0.0f) {
            n[0] /= len, n[1] /= len, n[2] /= len;
        }
        render_light_vertex(l, p, n, &colors[v * 4], &speculars[v * 4]);
        any = any || speculars[v * 4] != 0 || speculars[v * 4 + 1] != 0 || speculars[v * 4 + 2] != 0;
    }
    return any;
}
