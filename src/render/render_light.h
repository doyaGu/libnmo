/**
 * @file render_light.h
 * @brief Direct3D 7's vertex lighting, done on the CPU.
 *
 * A point or spot light reaches no farther than its range; a spot is full
 * inside half its inner cone, nothing outside half its outer one, and
 * ((rho - cos phi/2) / (cos theta/2 - cos phi/2))^falloff between;
 * attenuation is 1 / (a0 + a1 d + a2 d^2). The color is the emissive, the
 * ambient times the global ambient and each light's diffuse times N.L,
 * clamped, the material's diffuse alpha; the specular (added after
 * texturing) each light's specular times (N.H)^power with the local
 * viewer's half vector, where N.L is positive. The world space here is the
 * scene's, the eye the camera's position.
 */

#ifndef NMO_RENDER_LIGHT_H
#define NMO_RENDER_LIGHT_H

#include "engine/nmo_engine_render.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct render_lighting {
    const nmo_engine_frame_t *frame;
    const nmo_engine_material_t *material;
    bool specular;   /* VXRENDERSTATE_SPECULARENABLE: the material's power above 0.05 */
    float eye[3];
    float ambient[3];
} render_lighting_t;

/* The frame's lights on a material */
render_lighting_t render_lighting(const nmo_engine_frame_t *frame, const nmo_engine_material_t *m);
/* One vertex at world position `p` with world normal `n` (unit): its color and specular, RGBA bytes */
void render_light_vertex(const render_lighting_t *l, const float p[3], const float n[3],
                         uint8_t diffuse_out[4], uint8_t specular_out[4]);
/*
 * A mesh's vertices lit by the material, in the world `world` puts them in
 * (normals by its inverse transpose, made unit); colors and speculars RGBA
 * bytes a vertex; whether any specular is not black.
 */
bool render_light_mesh(const render_lighting_t *l, const nmo_vertex_t *vertices, int32_t count,
                       const float world[16], uint8_t *colors, uint8_t *speculars);

#endif /* NMO_RENDER_LIGHT_H */
