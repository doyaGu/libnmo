/**
 * @file render_gl.c
 * @brief The OpenGL backend: the front end's Direct3D 7 state applied to
 *        OpenGL's fixed function pipeline (2.1 with EXT_framebuffer_object).
 *
 * Matrices stay in Virtools' convention (row vectors, left-handed): the
 * model-view matrix loads as it is, and the projection is Direct3D's
 * followed by a remap of depth to OpenGL's -1..1. Front faces are
 * clockwise on screen, as Direct3D culls counter-clockwise ones. A texture
 * is drawn into upside down, so that its first row is the frame's top as CK
 * has it: rectangles, projections and windings are turned over there.
 */

#define GL_SILENCE_DEPRECATION

#include "render/nmo_render_gl.h"
#include "../render_internal.h"
#include "../render_math.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <stdlib.h>
#include <string.h>

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_NORMAL_MAP
#define GL_NORMAL_MAP 0x8511
#endif
#ifndef GL_REFLECTION_MAP
#define GL_REFLECTION_MAP 0x8512
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif

typedef struct rgl {
    render_backend_t base;
    float anisotropy; /* the driver's most for mipmapped textures; 0 for none */
    int screen_w, screen_h;
    bool kept;        /* the screen draws into the kept frame */
    bool flip;        /* drawing into a texture: upside down */
    int target_h;     /* the height of what is drawn into */
    /* The framebuffer render passes draw in, and its depth and stencil */
    GLuint target_fbo, target_depth;
    int target_fbo_w, target_fbo_h;
    GLenum target_attach; /* the face attached while drawing a pass */
    /* The frame kept between frames when the render context does not clear its background
       (Global Blur): drawn there, then shown */
    GLuint kept_fbo, kept_color, kept_depth;
    int kept_w, kept_h;
    /* The frame drawn before, for Copy Buffer to Texture (CK copies the back buffer as the block runs) */
    GLuint last_fbo, last_color;
    int last_w, last_h;
    bool last_valid;
} rgl_t;

/* ============================================================================
 * Enums
 * ============================================================================ */

static GLenum rgl_address(int32_t mode)
{
    switch (mode) {
    case 2: return GL_MIRRORED_REPEAT;
    case 3: return GL_CLAMP_TO_EDGE;
    case 4: return GL_CLAMP_TO_BORDER;
#ifdef GL_MIRROR_CLAMP_TO_EDGE_EXT
    case 5: return GL_MIRROR_CLAMP_TO_EDGE_EXT; /* VXTEXTURE_ADDRESSMIRRORONCE */
#endif
    default: return GL_REPEAT;
    }
}

static GLenum rgl_blend(int32_t factor)
{
    switch (factor) {
    case 1: return GL_ZERO;
    case 2: return GL_ONE;
    case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_SRC_COLOR;
    case 5: return GL_SRC_ALPHA;
    case 6: return GL_ONE_MINUS_SRC_ALPHA;
    case 7: return GL_DST_ALPHA;
    case 8: return GL_ONE_MINUS_DST_ALPHA;
    case 9: return GL_DST_COLOR;
    case 10: return GL_ONE_MINUS_DST_COLOR;
    case 11: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

static GLenum rgl_compare(int32_t func)
{
    switch (func) {
    case 1: return GL_NEVER;
    case 2: return GL_LESS;
    case 3: return GL_EQUAL;
    case 4: return GL_LEQUAL;
    case 5: return GL_GREATER;
    case 6: return GL_NOTEQUAL;
    case 7: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

static GLenum rgl_stencil_op(int32_t op)
{
    switch (op) {
    case 2: return GL_ZERO;
    case 3: return GL_REPLACE;
    case 4: return GL_INCR;
    case 5: return GL_DECR;
    case 6: return GL_INVERT;
    case 7: return GL_INCR_WRAP;
    case 8: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

static GLint rgl_min_filter(const render_sampler_t *sampler)
{
    bool linear = sampler->min_filter == RENDER_FILTER_LINEAR;
    switch (sampler->mip) {
    case RENDER_MIP_NEAREST: return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    case RENDER_MIP_LINEAR: return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    default: return linear ? GL_LINEAR : GL_NEAREST;
    }
}

static GLint rgl_texenv(render_texenv_t texenv)
{
    switch (texenv) {
    case RENDER_TEXENV_DECAL: return GL_DECAL;
    case RENDER_TEXENV_REPLACE: return GL_REPLACE;
    case RENDER_TEXENV_ADD: return GL_ADD;
    default: return GL_MODULATE;
    }
}

static GLenum rgl_primitive(render_primitive_t primitive)
{
    switch (primitive) {
    case RENDER_PRIMITIVE_LINES: return GL_LINES;
    case RENDER_PRIMITIVE_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
    default: return GL_TRIANGLES;
    }
}

/* A rectangle from the target's top as GL's (from its bottom); turned over in a texture */
static void rgl_rect(const rgl_t *gl, const int32_t rect[4], GLint out[4])
{
    out[0] = rect[0];
    out[1] = gl->flip ? rect[1] : gl->target_h - rect[1] - rect[3];
    out[2] = rect[2];
    out[3] = rect[3];
}

/* ============================================================================
 * States
 * ============================================================================ */

static void rgl_scissor(const rgl_t *gl, const render_state_t *s)
{
    if (s->scissor) {
        GLint r[4];
        rgl_rect(gl, s->scissor_rect, r);
        glEnable(GL_SCISSOR_TEST);
        glScissor(r[0], r[1], r[2], r[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

static void rgl_projection(const rgl_t *gl, const render_state_t *s)
{
    glMatrixMode(GL_PROJECTION);
    if (s->projection_kind == RENDER_PROJECTION_2D) {
        double w = s->projection_rect[0], h = s->projection_rect[1];
        glLoadIdentity();
        glOrtho(0.0, w, gl->flip ? 0.0 : h, gl->flip ? h : 0.0, -1.0, 1.0);
    } else if (s->projection_kind == RENDER_PROJECTION_SCREEN) {
        double left = s->projection_rect[0], top = s->projection_rect[1];
        double width = s->projection_rect[2], height = s->projection_rect[3];
        glLoadIdentity();
        glOrtho(left, left + width, gl->flip ? top : top + height, gl->flip ? top + height : top, 0.0, -1.0);
    } else {
        /* Direct3D's depth 0..w to OpenGL's -w..w */
        float projection[16];
        memcpy(projection, s->projection, sizeof(projection));
        const float remap[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, -1, 1};
        render_multiply(projection, projection, remap);
        if (gl->flip) {
            projection[1] = -projection[1], projection[5] = -projection[5];
            projection[9] = -projection[9], projection[13] = -projection[13];
        }
        if (s->depth_scale != 0.0f) {
            /* GL's depth translation is the matrix's [14], as D3D's is its [3][2] */
            projection[14] *= s->depth_scale;
        }
        glLoadMatrixf(projection);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(s->model_view);
}

static void rgl_texgen(const render_state_t *s)
{
    glMatrixMode(GL_TEXTURE);
    glLoadMatrixf(s->texture_matrix);
    glMatrixMode(GL_MODELVIEW);
    GLint mode = s->texgen == RENDER_TEXGEN_REFLECT  ? GL_REFLECTION_MAP
                 : s->texgen == RENDER_TEXGEN_NORMAL ? GL_NORMAL_MAP
                 : s->texgen == RENDER_TEXGEN_EYE    ? GL_EYE_LINEAR
                                                     : 0;
    if (mode == 0) {
        glDisable(GL_TEXTURE_GEN_S);
        glDisable(GL_TEXTURE_GEN_T);
        glDisable(GL_TEXTURE_GEN_R);
        return;
    }
    if (mode == GL_EYE_LINEAR) {
        /* planes taken in eye space: the identity at the time they are given */
        const float s_plane[4] = {1, 0, 0, 0}, t_plane[4] = {0, 1, 0, 0}, r_plane[4] = {0, 0, 1, 0};
        glPushMatrix();
        glLoadIdentity();
        glTexGenfv(GL_S, GL_EYE_PLANE, s_plane);
        glTexGenfv(GL_T, GL_EYE_PLANE, t_plane);
        glTexGenfv(GL_R, GL_EYE_PLANE, r_plane);
        glPopMatrix();
    }
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, mode);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, mode);
    glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, mode);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glEnable(GL_TEXTURE_GEN_R);
}

static void rgl_texture(const rgl_t *gl, const render_state_t *s)
{
    if (s->texture_kind == RENDER_TEXTURE_NONE || s->texture == 0) {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_CUBE_MAP);
        return;
    }
    GLenum target = s->texture_kind == RENDER_TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glDisable(target == GL_TEXTURE_2D ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D);
    glEnable(target);
    glBindTexture(target, (GLuint)s->texture);
    const render_sampler_t *sampler = &s->sampler;
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, rgl_min_filter(sampler));
    if (gl->anisotropy > 1.0f) {
        glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, sampler->anisotropic ? gl->anisotropy : 1.0f);
    }
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER,
                    sampler->mag_filter == RENDER_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR);
    GLint wrap = (GLint)rgl_address(sampler->address);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    if (target == GL_TEXTURE_CUBE_MAP) {
        glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
    } else if (wrap == GL_CLAMP_TO_BORDER) { /* the material's border color */
        float border[4];
        render_color(sampler->border_color, border);
        glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, rgl_texenv(s->texenv));
}

/* All of the state a draw is made with */
static void rgl_apply(const rgl_t *gl, const render_state_t *s)
{
    GLint viewport[4];
    rgl_rect(gl, s->viewport, viewport);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    rgl_scissor(gl, s);
    if (s->antialias) {
        glEnable(GL_MULTISAMPLE);
    } else {
        glDisable(GL_MULTISAMPLE);
    }
    rgl_projection(gl, s);
    rgl_texture(gl, s);
    rgl_texgen(s);

    glDisable(GL_LIGHTING); /* the front end lit the vertices */
    glEnable(GL_NORMALIZE);
    glColor4fv(s->color);
    glSecondaryColor3ub(s->secondary[0], s->secondary[1], s->secondary[2]);
    if (s->color_sum) {
        glEnable(GL_COLOR_SUM);
    } else {
        glDisable(GL_COLOR_SUM);
    }
    glShadeModel(s->flat ? GL_FLAT : GL_SMOOTH);
    glPolygonMode(GL_FRONT_AND_BACK, s->fill == RENDER_FILL_LINE    ? GL_LINE
                                     : s->fill == RENDER_FILL_POINT ? GL_POINT
                                                                    : GL_FILL);
    if (s->cull) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    glCullFace(GL_BACK);
    glFrontFace(s->front_ccw != gl->flip ? GL_CCW : GL_CW);

    if (s->alpha_test) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(rgl_compare(s->alpha_func), s->alpha_ref);
    } else {
        glDisable(GL_ALPHA_TEST);
    }
    if (s->blend) {
        glEnable(GL_BLEND);
        glBlendFunc(rgl_blend(s->source_blend), rgl_blend(s->dest_blend));
    } else {
        glDisable(GL_BLEND);
    }
    if (s->depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthFunc(rgl_compare(s->depth_func));
    glDepthMask(s->depth_write ? GL_TRUE : GL_FALSE);
    GLboolean color = s->color_write ? GL_TRUE : GL_FALSE;
    glColorMask(color, color, color, color);
    if (s->stencil) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(rgl_compare(s->stencil_func), (GLint)s->stencil_ref, s->stencil_mask);
        glStencilOp(rgl_stencil_op(s->stencil_fail), rgl_stencil_op(s->stencil_zfail),
                    rgl_stencil_op(s->stencil_pass));
    } else {
        glDisable(GL_STENCIL_TEST);
    }
    glStencilMask(s->stencil_write_mask);
    if (s->fog) {
        float fog_color[4];
        render_color(s->fog_color, fog_color);
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, s->fog_mode == 1 ? GL_EXP : s->fog_mode == 2 ? GL_EXP2 : GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fog_color);
        glFogf(GL_FOG_START, s->fog_start);
        glFogf(GL_FOG_END, s->fog_end);
        glFogf(GL_FOG_DENSITY, s->fog_density);
    } else {
        glDisable(GL_FOG);
    }
}

/* ============================================================================
 * Drawing
 * ============================================================================ */

static void rgl_clear(render_backend_t *backend, const render_state_t *s, bool color, bool depth,
                      bool stencil, const float value[4])
{
    rgl_t *gl = (rgl_t *)backend;
    rgl_scissor(gl, s);
    glDepthMask(s->depth_write ? GL_TRUE : GL_FALSE);
    GLboolean write = s->color_write ? GL_TRUE : GL_FALSE;
    glColorMask(write, write, write, write);
    glStencilMask(s->stencil_write_mask);
    glClearColor(value[0], value[1], value[2], value[3]);
    glClearStencil((GLint)s->clear_stencil);
    GLbitfield bits = (color ? GL_COLOR_BUFFER_BIT : 0u) | (depth ? GL_DEPTH_BUFFER_BIT : 0u) |
                      (stencil ? GL_STENCIL_BUFFER_BIT : 0u);
    if (bits != 0) {
        glClear(bits);
    }
}

static void rgl_draw(render_backend_t *backend, const render_state_t *s, const render_geometry_t *g)
{
    rgl_t *gl = (rgl_t *)backend;
    if (g->vertex_count <= 0 || g->positions == NULL) {
        return;
    }
    rgl_apply(gl, s);
    GLenum mode = rgl_primitive(g->primitive);
    if (g->indices != NULL) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, (GLsizei)g->position_stride, g->positions);
        if (g->normals != NULL) {
            glEnableClientState(GL_NORMAL_ARRAY);
            glNormalPointer(GL_FLOAT, (GLsizei)g->normal_stride, g->normals);
        }
        if (g->uvs != NULL) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, (GLsizei)g->uv_stride, g->uvs);
        }
        if (g->colors != NULL) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, g->colors);
        } else if (g->float_colors != NULL) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_FLOAT, 0, g->float_colors);
        }
        if (g->speculars != NULL) {
            glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
            glSecondaryColorPointer(3, GL_UNSIGNED_BYTE, (GLsizei)g->specular_stride, g->speculars);
        }
        glDrawElements(mode, g->index_count, GL_UNSIGNED_SHORT, g->indices);
        glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        return;
    }
    glBegin(mode);
    for (int32_t i = 0; i < g->vertex_count; i++) {
        if (g->float_colors != NULL) {
            glColor4fv(&g->float_colors[i * 4]);
        } else if (g->colors != NULL) {
            const uint8_t *c = &g->colors[i * 4];
            glColor4ub(c[0], c[1], c[2], c[3]);
        }
        if (g->speculars != NULL) {
            const uint8_t *c = &g->speculars[(size_t)i * g->specular_stride];
            glSecondaryColor3ub(c[0], c[1], c[2]);
        }
        if (g->normals != NULL) {
            glNormal3fv((const float *)((const uint8_t *)g->normals + (size_t)i * g->normal_stride));
        }
        if (g->uvs != NULL) {
            const float *uv = (const float *)((const uint8_t *)g->uvs + (size_t)i * g->uv_stride);
            glTexCoord2f(uv[0], uv[1]);
        }
        const float *p = (const float *)((const uint8_t *)g->positions + (size_t)i * g->position_stride);
        glVertex3f(p[0], p[1], p[2]);
    }
    glEnd();
}

/* ============================================================================
 * Textures
 * ============================================================================ */

static uintptr_t rgl_texture_create(render_backend_t *backend)
{
    (void)backend;
    GLuint name = 0;
    glGenTextures(1, &name);
    return (uintptr_t)name;
}

static void rgl_texture_destroy(render_backend_t *backend, uintptr_t texture)
{
    (void)backend;
    GLuint name = (GLuint)texture;
    glDeleteTextures(1, &name);
}

static void rgl_texture_image(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                              int32_t face, int32_t level, int32_t width, int32_t height,
                              const uint8_t *rgba)
{
    (void)backend;
    if (kind == RENDER_TEXTURE_CUBE) {
        /* CK's face order and rows are GL's */
        glBindTexture(GL_TEXTURE_CUBE_MAP, (GLuint)texture);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, level, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else {
        glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
}

static void rgl_texture_levels(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                               int32_t last)
{
    (void)backend;
    GLenum target = kind == RENDER_TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glBindTexture(target, (GLuint)texture);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, last);
}

static void rgl_texture_sub_image(render_backend_t *backend, uintptr_t texture, int32_t x, int32_t y,
                                  int32_t width, int32_t height, const uint8_t *rgba)
{
    (void)backend;
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static void rgl_texture_target(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                               int32_t width, int32_t height, bool keep_faces)
{
    (void)backend;
    bool cube = kind == RENDER_TEXTURE_CUBE;
    GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glBindTexture(target, (GLuint)texture);
    for (int face = 0; face < (cube ? 6 : 1); face++) {
        if (cube && keep_faces) {
            GLint w = 0, h = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, 0, GL_TEXTURE_HEIGHT, &h);
            if (w == width && h == height) {
                continue;
            }
        }
        glTexImage2D(cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face : GL_TEXTURE_2D, 0, GL_RGBA,
                     width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/* ============================================================================
 * Targets
 * ============================================================================ */

/* Draw into the kept frame, made (black) at the window's size; false when it cannot be made */
static bool rgl_kept_begin(rgl_t *gl, int window_w, int window_h)
{
    bool made = false;
    if (gl->kept_fbo == 0) {
        glGenFramebuffersEXT(1, &gl->kept_fbo);
        glGenTextures(1, &gl->kept_color);
        glGenRenderbuffersEXT(1, &gl->kept_depth);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gl->kept_fbo);
    if (gl->kept_w != window_w || gl->kept_h != window_h) {
        gl->kept_w = window_w;
        gl->kept_h = window_h;
        glBindTexture(GL_TEXTURE_2D, gl->kept_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, window_w, window_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, gl->kept_depth);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT, window_w, window_h);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, gl->kept_color, 0);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, gl->kept_depth);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
                                     gl->kept_depth);
        made = true;
    }
    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        return false;
    }
    if (made) {
        glViewport(0, 0, window_w, window_h);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    return true;
}

/* The kept frame shown in the window */
static void rgl_kept_end(rgl_t *gl, int window_w, int window_h)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    glViewport(0, 0, window_w, window_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_COLOR_SUM);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_GEN_R);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_CUBE_MAP);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gl->kept_color);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/* Keep what the frame just drawn left (the window's pixels) for the next frame's buffer copies */
static void rgl_keep_last_frame(rgl_t *gl, int window_w, int window_h)
{
    if (window_w <= 0 || window_h <= 0) {
        return;
    }
    if (gl->last_color == 0) {
        glGenTextures(1, &gl->last_color);
    }
    glBindTexture(GL_TEXTURE_2D, gl->last_color);
    if (gl->last_w != window_w || gl->last_h != window_h) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, window_w, window_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gl->last_w = window_w;
        gl->last_h = window_h;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, window_w, window_h);
    gl->last_valid = true;
}

static bool rgl_begin_screen(render_backend_t *backend, int32_t width, int32_t height, bool preserve)
{
    rgl_t *gl = (rgl_t *)backend;
    gl->screen_w = width;
    gl->screen_h = height;
    gl->target_h = height;
    gl->flip = false;
    gl->kept = preserve && rgl_kept_begin(gl, width, height);
    return gl->kept;
}

static void rgl_end_screen(render_backend_t *backend)
{
    rgl_t *gl = (rgl_t *)backend;
    if (gl->kept) {
        rgl_kept_end(gl, gl->screen_w, gl->screen_h);
        gl->kept = false;
    }
    rgl_keep_last_frame(gl, gl->screen_w, gl->screen_h);
}

static bool rgl_begin_target(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                             int32_t face, int32_t width, int32_t height)
{
    rgl_t *gl = (rgl_t *)backend;
    if (gl->target_fbo == 0) {
        glGenFramebuffersEXT(1, &gl->target_fbo);
        glGenRenderbuffersEXT(1, &gl->target_depth);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gl->target_fbo);
    if (gl->target_fbo_w != width || gl->target_fbo_h != height) {
        gl->target_fbo_w = width;
        gl->target_fbo_h = height;
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, gl->target_depth);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT, width, height);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
                                     gl->target_depth);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
                                     gl->target_depth);
    }
    gl->target_attach = kind == RENDER_TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face
                                                    : GL_TEXTURE_2D;
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, gl->target_attach,
                              (GLuint)texture, 0);
    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, gl->target_attach, 0, 0);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        return false;
    }
    gl->flip = true;
    gl->target_h = height;
    return true;
}

static void rgl_end_target(render_backend_t *backend)
{
    rgl_t *gl = (rgl_t *)backend;
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, gl->target_attach, 0, 0);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    gl->flip = false;
    gl->target_h = gl->screen_h;
}

/* ============================================================================
 * Reading
 * ============================================================================ */

static bool rgl_last_frame_ready(render_backend_t *backend, int32_t width, int32_t height)
{
    const rgl_t *gl = (const rgl_t *)backend;
    return gl->last_valid && gl->last_w == width && gl->last_h == height;
}

/* GL's rows of `rect` (bottom first) into `rgba` top first */
static bool rgl_read_rect(int target_h, const int32_t rect[4], uint8_t *rgba)
{
    size_t row = (size_t)rect[2] * 4u;
    uint8_t *pixels = (uint8_t *)malloc(row * (size_t)rect[3]);
    if (pixels == NULL) {
        return false;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(rect[0], target_h - rect[1] - rect[3], rect[2], rect[3], GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);
    for (int y = 0; y < rect[3]; y++) {
        memcpy(rgba + (size_t)y * row, pixels + (size_t)(rect[3] - 1 - y) * row, row);
    }
    free(pixels);
    return true;
}

static bool rgl_read_last_frame(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    rgl_t *gl = (rgl_t *)backend;
    GLint bound = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &bound);
    if (gl->last_fbo == 0) {
        glGenFramebuffersEXT(1, &gl->last_fbo);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gl->last_fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, gl->last_color, 0);
    bool read = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT &&
                rgl_read_rect(gl->last_h, rect, rgba);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)bound);
    return read;
}

/* The back buffer, before it is swapped: the front buffer is undefined once swapped (macOS
   composites the window) */
static nmo_status_t rgl_read_pixels(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    const rgl_t *gl = (const rgl_t *)backend;
    glReadBuffer(GL_BACK);
    return rgl_read_rect(gl->screen_h, rect, rgba) ? NMO_OK : NMO_ERR_NOMEM;
}

/* ============================================================================
 * The backend
 * ============================================================================ */

static void rgl_destroy(render_backend_t *backend)
{
    rgl_t *gl = (rgl_t *)backend;
    if (gl->target_fbo != 0) {
        glDeleteFramebuffersEXT(1, &gl->target_fbo);
        glDeleteRenderbuffersEXT(1, &gl->target_depth);
    }
    if (gl->last_fbo != 0) {
        glDeleteFramebuffersEXT(1, &gl->last_fbo);
    }
    if (gl->last_color != 0) {
        glDeleteTextures(1, &gl->last_color);
    }
    if (gl->kept_fbo != 0) {
        glDeleteFramebuffersEXT(1, &gl->kept_fbo);
        glDeleteTextures(1, &gl->kept_color);
        glDeleteRenderbuffersEXT(1, &gl->kept_depth);
    }
    free(gl);
}

static const render_backend_ops_t rgl_ops = {
    rgl_destroy,
    rgl_texture_create,
    rgl_texture_destroy,
    rgl_texture_image,
    rgl_texture_levels,
    rgl_texture_sub_image,
    rgl_texture_target,
    rgl_begin_screen,
    rgl_end_screen,
    rgl_begin_target,
    rgl_end_target,
    rgl_clear,
    rgl_draw,
    rgl_last_frame_ready,
    rgl_read_last_frame,
    rgl_read_pixels,
};

nmo_status_t nmo_render_create_gl(nmo_engine_t *engine, nmo_render_t **out_render)
{
    if (engine == NULL || out_render == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_render = NULL;
    rgl_t *gl = (rgl_t *)calloc(1, sizeof(*gl));
    if (gl == NULL) {
        return NMO_ERR_NOMEM;
    }
    gl->base.ops = &rgl_ops;
    glEnable(GL_MULTISAMPLE);
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (extensions != NULL && strstr(extensions, "GL_EXT_texture_filter_anisotropic") != NULL) {
        GLfloat most = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
        gl->anisotropy = most > 8.0f ? 8.0f : most;
    }
    return render_create(engine, &gl->base, out_render);
}
