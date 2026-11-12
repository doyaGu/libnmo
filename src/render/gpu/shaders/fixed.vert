#version 450
/*
 * Direct3D 7's transform stage for nmo_render's SDL_GPU backend: the
 * vertices come lit; this places them, generates their texture coordinates
 * and their fog factor. Matrices are VxMatrix's rows uploaded as they are,
 * so `M * v` is CK's row vector times M. SDL_GPU's resource sets: uniform
 * buffers in set 1.
 */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) in vec3 in_specular;

layout(std140, set = 1, binding = 0) uniform Transform {
    mat4 model_view;
    mat4 projection;
    mat4 normal_matrix;  /* the model view's inverse: normals go by its transpose */
    mat4 texture_matrix;
    vec4 fog;            /* mode (0 none, 1 exp, 2 exp2, 3 linear), start, end, density */
    ivec4 mode;          /* texgen: 0 none, 1 reflect, 2 normal, 3 eye position */
} u;

layout(location = 0) out vec4 v_tex;
#ifdef FLAT
layout(location = 1) flat out vec4 v_color;
layout(location = 2) flat out vec3 v_specular;
#else
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_specular;
#endif
layout(location = 3) out float v_fog;

void main()
{
    vec4 eye = u.model_view * vec4(in_position, 1.0);
    gl_Position = u.projection * eye;
    vec4 coords = vec4(in_uv, 0.0, 1.0);
    int texgen = u.mode.x;
    if (texgen == 3) {
        coords = vec4(eye.xyz, 1.0);
    } else if (texgen == 1 || texgen == 2) {
        vec3 n = (vec4(in_normal, 0.0) * u.normal_matrix).xyz;
        float n_length = length(n);
        n = n_length > 0.0 ? n / n_length : vec3(0.0, 0.0, 1.0);
        if (texgen == 2) {
            coords = vec4(n, 1.0);
        } else {
            float e_length = length(eye.xyz);
            vec3 e = e_length > 0.0 ? eye.xyz / e_length : eye.xyz;
            coords = vec4(e - 2.0 * n * dot(n, e), 1.0);
        }
    }
    v_tex = u.texture_matrix * coords;
    v_color = in_color;
    v_specular = in_specular;
    float d = abs(eye.z), f = 1.0;
    if (u.fog.x == 1.0) {
        f = exp(-u.fog.w * d);
    } else if (u.fog.x == 2.0) {
        float e = u.fog.w * d;
        f = exp(-e * e);
    } else if (u.fog.x == 3.0 && u.fog.z != u.fog.y) {
        f = (u.fog.z - d) / (u.fog.z - u.fog.y);
    }
    v_fog = clamp(f, 0.0, 1.0);
}
