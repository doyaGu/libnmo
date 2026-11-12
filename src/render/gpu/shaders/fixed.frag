#version 450
/*
 * Direct3D 7's texture stage and the fragment operations before blending
 * for nmo_render's SDL_GPU backend: one texture (2D or cube) combined with
 * the vertex color, the specular added, fog, and the alpha test. SDL_GPU's
 * resource sets: sampled textures in set 2, uniform buffers in set 3.
 */

layout(location = 0) in vec4 v_tex;
#ifdef FLAT
layout(location = 1) flat in vec4 v_color;
layout(location = 2) flat in vec3 v_specular;
#else
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_specular;
#endif
layout(location = 3) in float v_fog;

layout(set = 2, binding = 0) uniform sampler2D texture_2d;
layout(set = 2, binding = 1) uniform samplerCube texture_cube;

layout(std140, set = 3, binding = 0) uniform Stage {
    vec4 fog_color;
    vec4 border_color;
    vec4 alpha;    /* the alpha test's reference, 0..1 */
    ivec4 texture; /* kind (0 none, 1 2D, 2 cube), texenv, VXTEXTURE_ADDRESSMODE */
    ivec4 flags;   /* color sum, fog, alpha test, VXCMP of the alpha test */
} u;

layout(location = 0) out vec4 out_color;

bool compare(int func, float a, float b)
{
    if (func == 1) return false;
    if (func == 2) return a < b;
    if (func == 3) return a == b;
    if (func == 4) return a <= b;
    if (func == 5) return a > b;
    if (func == 6) return a != b;
    if (func == 7) return a >= b;
    return true;
}

void main()
{
    vec4 c = clamp(v_color, 0.0, 1.0);
    int kind = u.texture.x;
    if (kind != 0) {
        vec4 t;
        if (kind == 2) {
            t = texture(texture_cube, v_tex.xyz);
        } else {
            float q = v_tex.w != 0.0 ? v_tex.w : 1.0;
            vec2 uv = v_tex.xy / q;
            int address = u.texture.z;
            if (address == 5) { /* MIRRORONCE: mirrored about 0, then clamped */
                uv = abs(uv);
            }
            t = texture(texture_2d, uv);
            if (address == 4 && (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)) {
                t = u.border_color;
            }
        }
        int texenv = u.texture.y;
        if (texenv == 1) {        /* DECAL */
            c.rgb = c.rgb * (1.0 - t.a) + t.rgb * t.a;
        } else if (texenv == 2) { /* REPLACE */
            c = t;
        } else if (texenv == 3) { /* ADD */
            c.rgb = min(c.rgb + t.rgb, vec3(1.0));
            c.a *= t.a;
        } else {                  /* MODULATE */
            c *= t;
        }
    }
    if (u.flags.x != 0) {
        c.rgb = min(c.rgb + clamp(v_specular, 0.0, 1.0), vec3(1.0));
    }
    if (u.flags.y != 0) {
        c.rgb = mix(u.fog_color.rgb, c.rgb, v_fog);
    }
    if (u.flags.z != 0 &&
        !compare(u.flags.w, floor(c.a * 255.0 + 0.5), floor(u.alpha.x * 255.0 + 0.5))) {
        discard;
    }
    out_color = c;
}
