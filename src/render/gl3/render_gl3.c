/**
 * @file render_gl3.c
 * @brief The GL3 backend: render_backend.h's fixed function state drawn
 *        with the shader shared with the SDL_GPU backend (fixed.vert,
 *        fixed.frag, compiled to GLSL 3.30 and GLSL ES 3.00 by
 *        tools/scripts/gen_gpu_shaders.py into shaders/fixed_glsl.h), on
 *        OpenGL 3.3 core, OpenGL ES 3.0 or WebGL 2.
 *
 * No system GL header is included: every function, from glViewport up, is
 * named from the host's loader, so the same file builds against whichever
 * of the three the current context is. The dialect (and so which compiled
 * shader strings to use) is picked once from GL_VERSION: a string starting
 * "OpenGL ES" (which is also how Emscripten's WebGL emulation reports
 * itself) takes the ES 3.00 shaders; anything else takes the 3.30 core
 * ones.
 *
 * Matrices stay in Virtools' convention (row vectors). render_clip_projection
 * gives a Direct3D/Vulkan-style projection (depth 0..w); every draw remaps
 * its z to GL's -1..1 on the CPU before it is uploaded, the same multiply
 * render_gl.c's fixed-function path does with glLoadMatrixf. Unlike that
 * fixed-function backend, the screen and a render-to-texture target are
 * both backed by a framebuffer object here (the screen's is persistent
 * across frames, blitted to the context's default framebuffer at
 * end_screen so the view's SDL_GL_SwapWindow shows it, and to a second kept
 * texture for read_last_frame); a render target is still drawn upside down
 * relative to the screen (`flip`), exactly as render_gl.c's is, so that a
 * texture made from it samples right-side up afterwards.
 */

#include "render/nmo_render_gl3.h"
#include "render_gl3.h"
#include "../render_internal.h"
#include "../render_math.h"
#include "shaders/fixed_glsl.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Minimal GL types and constants (no system header): values are the same
 * across desktop GL, GL ES and WebGL unless noted.
 * ============================================================================ */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef long GLsizeiptr;
typedef long GLintptr;
typedef void GLvoid;
#if defined(_WIN32)
#define NMO_GLAPI __stdcall
#else
#define NMO_GLAPI
#endif

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_FAN 0x0006

#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308

#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_CW 0x0900
#define GL_CCW 0x0901

#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05

#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403

#define GL_RGBA 0x1908
#define GL_DEPTH_STENCIL 0x84F9
#define GL_COLOR 0x1800 /* glClearBufferfv's buffer enum, distinct from GL_COLOR_BUFFER_BIT */
#define GL_DEPTH24_STENCIL8 0x88F0

#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_INVERT 0x150A
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508

#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400

#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_DYNAMIC_DRAW 0x88E8

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_VERSION 0x1F02

#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_POINT 0x1B00

/* ============================================================================
 * Functions, all loaded: nothing here is assumed directly linkable.
 * ============================================================================ */

typedef void(NMO_GLAPI *gl3_viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void(NMO_GLAPI *gl3_scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void(NMO_GLAPI *gl3_enable_fn)(GLenum);
typedef void(NMO_GLAPI *gl3_blend_func_fn)(GLenum, GLenum);
typedef void(NMO_GLAPI *gl3_depth_func_fn)(GLenum);
typedef void(NMO_GLAPI *gl3_depth_mask_fn)(GLboolean);
typedef void(NMO_GLAPI *gl3_color_mask_fn)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void(NMO_GLAPI *gl3_stencil_func_fn)(GLenum, GLint, GLuint);
typedef void(NMO_GLAPI *gl3_stencil_op_fn)(GLenum, GLenum, GLenum);
typedef void(NMO_GLAPI *gl3_stencil_mask_fn)(GLuint);
typedef void(NMO_GLAPI *gl3_cull_face_fn)(GLenum);
typedef void(NMO_GLAPI *gl3_front_face_fn)(GLenum);
typedef void(NMO_GLAPI *gl3_clear_bufferfv_fn)(GLenum, GLint, const GLfloat *);
typedef void(NMO_GLAPI *gl3_clear_bufferfi_fn)(GLenum, GLint, GLfloat, GLint);
typedef void(NMO_GLAPI *gl3_pixel_storei_fn)(GLenum, GLint);
typedef void(NMO_GLAPI *gl3_read_pixels_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void(NMO_GLAPI *gl3_read_buffer_fn)(GLenum);
typedef const GLchar *(NMO_GLAPI *gl3_get_string_fn)(GLenum);
typedef void(NMO_GLAPI *gl3_polygon_mode_fn)(GLenum, GLenum); /* desktop only; NULL on ES/WebGL */
typedef void(NMO_GLAPI *gl3_draw_arrays_fn)(GLenum, GLint, GLsizei);
typedef void(NMO_GLAPI *gl3_draw_elements_fn)(GLenum, GLsizei, GLenum, const void *);
typedef void(NMO_GLAPI *gl3_active_texture_fn)(GLenum);

typedef void(NMO_GLAPI *gl3_gen_textures_fn)(GLsizei, GLuint *);
typedef void(NMO_GLAPI *gl3_delete_textures_fn)(GLsizei, const GLuint *);
typedef void(NMO_GLAPI *gl3_bind_texture_fn)(GLenum, GLuint);
typedef void(NMO_GLAPI *gl3_tex_image_2d_fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                                             GLenum, const void *);
typedef void(NMO_GLAPI *gl3_tex_sub_image_2d_fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                                                 GLenum, const void *);
typedef void(NMO_GLAPI *gl3_tex_parameteri_fn)(GLenum, GLenum, GLint);

typedef void(NMO_GLAPI *gl3_gen_fn)(GLsizei, GLuint *);
typedef void(NMO_GLAPI *gl3_delete_fn)(GLsizei, const GLuint *);
typedef void(NMO_GLAPI *gl3_bind_fn)(GLenum, GLuint);
typedef GLenum(NMO_GLAPI *gl3_check_framebuffer_fn)(GLenum);
typedef void(NMO_GLAPI *gl3_framebuffer_texture_2d_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(NMO_GLAPI *gl3_framebuffer_renderbuffer_fn)(GLenum, GLenum, GLenum, GLuint);
typedef void(NMO_GLAPI *gl3_renderbuffer_storage_fn)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(NMO_GLAPI *gl3_blit_framebuffer_fn)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                                 GLbitfield, GLenum);

typedef void(NMO_GLAPI *gl3_gen_vertex_arrays_fn)(GLsizei, GLuint *);
typedef void(NMO_GLAPI *gl3_delete_vertex_arrays_fn)(GLsizei, const GLuint *);
typedef void(NMO_GLAPI *gl3_bind_vertex_array_fn)(GLuint);
typedef void(NMO_GLAPI *gl3_enable_vertex_attrib_fn)(GLuint);
typedef void(NMO_GLAPI *gl3_vertex_attrib_pointer_fn)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                                      const void *);

typedef void(NMO_GLAPI *gl3_gen_buffers_fn)(GLsizei, GLuint *);
typedef void(NMO_GLAPI *gl3_delete_buffers_fn)(GLsizei, const GLuint *);
typedef void(NMO_GLAPI *gl3_bind_buffer_fn)(GLenum, GLuint);
typedef void(NMO_GLAPI *gl3_buffer_data_fn)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void(NMO_GLAPI *gl3_bind_buffer_base_fn)(GLenum, GLuint, GLuint);

typedef GLuint(NMO_GLAPI *gl3_create_shader_fn)(GLenum);
typedef void(NMO_GLAPI *gl3_shader_source_fn)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void(NMO_GLAPI *gl3_compile_shader_fn)(GLuint);
typedef void(NMO_GLAPI *gl3_get_shaderiv_fn)(GLuint, GLenum, GLint *);
typedef void(NMO_GLAPI *gl3_get_shader_log_fn)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(NMO_GLAPI *gl3_delete_shader_fn)(GLuint);
typedef GLuint(NMO_GLAPI *gl3_create_program_fn)(void);
typedef void(NMO_GLAPI *gl3_attach_shader_fn)(GLuint, GLuint);
typedef void(NMO_GLAPI *gl3_link_program_fn)(GLuint);
typedef void(NMO_GLAPI *gl3_get_programiv_fn)(GLuint, GLenum, GLint *);
typedef void(NMO_GLAPI *gl3_get_program_log_fn)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(NMO_GLAPI *gl3_delete_program_fn)(GLuint);
typedef void(NMO_GLAPI *gl3_use_program_fn)(GLuint);
typedef GLuint(NMO_GLAPI *gl3_get_uniform_block_index_fn)(GLuint, const GLchar *);
typedef void(NMO_GLAPI *gl3_uniform_block_binding_fn)(GLuint, GLuint, GLuint);
typedef GLint(NMO_GLAPI *gl3_get_uniform_location_fn)(GLuint, const GLchar *);
typedef void(NMO_GLAPI *gl3_uniform1i_fn)(GLint, GLint);

typedef struct gl3_functions {
    gl3_viewport_fn viewport;
    gl3_scissor_fn scissor;
    gl3_enable_fn enable, disable;
    gl3_blend_func_fn blend_func;
    gl3_depth_func_fn depth_func;
    gl3_depth_mask_fn depth_mask;
    gl3_color_mask_fn color_mask;
    gl3_stencil_func_fn stencil_func;
    gl3_stencil_op_fn stencil_op;
    gl3_stencil_mask_fn stencil_mask;
    gl3_cull_face_fn cull_face;
    gl3_front_face_fn front_face;
    gl3_clear_bufferfv_fn clear_bufferfv;
    gl3_clear_bufferfi_fn clear_bufferfi;
    gl3_pixel_storei_fn pixel_storei;
    gl3_read_pixels_fn read_pixels;
    gl3_read_buffer_fn read_buffer;
    gl3_get_string_fn get_string;
    gl3_polygon_mode_fn polygon_mode; /* may be NULL */
    gl3_draw_arrays_fn draw_arrays;
    gl3_draw_elements_fn draw_elements;
    gl3_active_texture_fn active_texture;

    gl3_gen_textures_fn gen_textures;
    gl3_delete_textures_fn delete_textures;
    gl3_bind_texture_fn bind_texture;
    gl3_tex_image_2d_fn tex_image_2d;
    gl3_tex_sub_image_2d_fn tex_sub_image_2d;
    gl3_tex_parameteri_fn tex_parameteri;

    gl3_gen_fn gen_framebuffers, gen_renderbuffers;
    gl3_delete_fn delete_framebuffers, delete_renderbuffers;
    gl3_bind_fn bind_framebuffer, bind_renderbuffer;
    gl3_check_framebuffer_fn check_framebuffer_status;
    gl3_framebuffer_texture_2d_fn framebuffer_texture_2d;
    gl3_framebuffer_renderbuffer_fn framebuffer_renderbuffer;
    gl3_renderbuffer_storage_fn renderbuffer_storage;
    gl3_blit_framebuffer_fn blit_framebuffer;

    gl3_gen_vertex_arrays_fn gen_vertex_arrays;
    gl3_delete_vertex_arrays_fn delete_vertex_arrays;
    gl3_bind_vertex_array_fn bind_vertex_array;
    gl3_enable_vertex_attrib_fn enable_vertex_attrib_array;
    gl3_vertex_attrib_pointer_fn vertex_attrib_pointer;

    gl3_gen_buffers_fn gen_buffers;
    gl3_delete_buffers_fn delete_buffers;
    gl3_bind_buffer_fn bind_buffer;
    gl3_buffer_data_fn buffer_data;
    gl3_bind_buffer_base_fn bind_buffer_base;

    gl3_create_shader_fn create_shader;
    gl3_shader_source_fn shader_source;
    gl3_compile_shader_fn compile_shader;
    gl3_get_shaderiv_fn get_shaderiv;
    gl3_get_shader_log_fn get_shader_info_log;
    gl3_delete_shader_fn delete_shader;
    gl3_create_program_fn create_program;
    gl3_attach_shader_fn attach_shader;
    gl3_link_program_fn link_program;
    gl3_get_programiv_fn get_programiv;
    gl3_get_program_log_fn get_program_info_log;
    gl3_delete_program_fn delete_program;
    gl3_use_program_fn use_program;
    gl3_get_uniform_block_index_fn get_uniform_block_index;
    gl3_uniform_block_binding_fn uniform_block_binding;
    gl3_get_uniform_location_fn get_uniform_location;
    gl3_uniform1i_fn uniform1i;
} gl3_functions_t;

/* ============================================================================
 * State
 * ============================================================================ */

/* A vertex as the shader takes it: the same layout fixed.vert's attributes expect */
typedef struct gl3_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
    float specular[3];
} gl3_vertex_t;

/* The uniforms of fixed.vert and fixed.frag (std140), matching the GPU backend's */
typedef struct gl3_vertex_uniforms {
    float model_view[16], projection[16], normal_matrix[16], texture_matrix[16];
    float fog[4];
    int32_t mode[4];
} gl3_vertex_uniforms_t;

typedef struct gl3_fragment_uniforms {
    float fog_color[4], border_color[4], alpha[4];
    int32_t texture[4], flags[4];
} gl3_fragment_uniforms_t;

typedef struct gl3_fbo {
    GLuint fbo, color, depth; /* depth is a combined depth+stencil renderbuffer */
    int32_t width, height;
    bool cube; /* color is a cube map (6 faces), not a 2D texture */
} gl3_fbo_t;

typedef struct gl3 {
    render_backend_t base;
    gl3_functions_t fn;
    bool es; /* OpenGL ES or WebGL (the fixed_es_*_glsl shaders); else desktop core (fixed_core_*_glsl) */

    GLuint vertex_shader[2], fragment_shader[2]; /* smooth, flat */
    GLuint program[2];
    GLuint vao, vertex_buffer, index_buffer, transform_ubo, stage_ubo;
    uint32_t vertex_buffer_size, index_buffer_size;

    gl3_fbo_t screen; /* persistent across frames: blitted to the default framebuffer at end_screen */
    gl3_fbo_t last;   /* a copy of the screen at the end of the frame before, for read_last_frame */
    gl3_fbo_t target; /* reused for every render-to-texture pass */
    bool flip;        /* drawing into target: upside down, so a texture made from it samples upright */
    int32_t target_h; /* the height of what is drawn into now */
    bool screen_open; /* the screen begun and not ended: a target's end goes back to it */

    /* An offscreen backend's own window and context (the test's; nmo_render_create_gl3 takes neither) */
    SDL_Window *own_window;
    SDL_GLContext own_context;
} gl3_t;

/* ============================================================================
 * Enums (the same mapping render_gl.c and render_gpu.c use)
 * ============================================================================ */

static GLenum gl3_address(int32_t mode)
{
    switch (mode) {
    case 2: return GL_MIRRORED_REPEAT;
    default: return GL_REPEAT;
    /* 3 clamp, 4 border and 5 mirror-once all sample with GL_CLAMP_TO_EDGE: the shader paints
       the border color or mirrors the coordinate (fixed.frag), as render_gpu's sampler does too */
    case 3:
    case 4:
    case 5:
        return GL_CLAMP_TO_EDGE;
    }
}

static GLenum gl3_blend(int32_t factor)
{
    switch (factor) {
    case 1: return GL_ZERO;
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

static GLenum gl3_compare(int32_t func)
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

static GLenum gl3_stencil_op(int32_t op)
{
    switch (op) {
    case 2: return GL_ZERO;
    case 3: return GL_REPLACE;
    case 4: return GL_INCR_WRAP; /* VXSTENCILOP's INCR/DECR already wrap (D3D's do) */
    case 5: return GL_DECR_WRAP;
    case 6: return GL_INVERT;
    case 7: return GL_INCR_WRAP;
    case 8: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

static GLint gl3_min_filter(const render_sampler_t *sampler)
{
    bool linear = sampler->min_filter == RENDER_FILTER_LINEAR;
    switch (sampler->mip) {
    case RENDER_MIP_NEAREST: return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    case RENDER_MIP_LINEAR: return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    default: return linear ? GL_LINEAR : GL_NEAREST;
    }
}

static GLenum gl3_primitive(render_primitive_t primitive)
{
    switch (primitive) {
    case RENDER_PRIMITIVE_LINES: return GL_LINES;
    case RENDER_PRIMITIVE_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
    default: return GL_TRIANGLES;
    }
}

/* ============================================================================
 * Loading
 * ============================================================================ */

#define GL3_LOAD(field, name)                                                                      \
    do {                                                                                            \
        gl->fn.field = (gl3_##field##_fn)load(name);                                                \
        ok = ok && gl->fn.field != NULL;                                                            \
    } while (0)

static bool gl3_load_functions(gl3_t *gl, nmo_render_gl3_loader_t load)
{
    bool ok = true;
    gl->fn.viewport = (gl3_viewport_fn)load("glViewport");
    gl->fn.scissor = (gl3_scissor_fn)load("glScissor");
    gl->fn.enable = (gl3_enable_fn)load("glEnable");
    gl->fn.disable = (gl3_enable_fn)load("glDisable");
    gl->fn.blend_func = (gl3_blend_func_fn)load("glBlendFunc");
    gl->fn.depth_func = (gl3_depth_func_fn)load("glDepthFunc");
    gl->fn.depth_mask = (gl3_depth_mask_fn)load("glDepthMask");
    gl->fn.color_mask = (gl3_color_mask_fn)load("glColorMask");
    gl->fn.stencil_func = (gl3_stencil_func_fn)load("glStencilFunc");
    gl->fn.stencil_op = (gl3_stencil_op_fn)load("glStencilOp");
    gl->fn.stencil_mask = (gl3_stencil_mask_fn)load("glStencilMask");
    gl->fn.cull_face = (gl3_cull_face_fn)load("glCullFace");
    gl->fn.front_face = (gl3_front_face_fn)load("glFrontFace");
    gl->fn.clear_bufferfv = (gl3_clear_bufferfv_fn)load("glClearBufferfv");
    gl->fn.clear_bufferfi = (gl3_clear_bufferfi_fn)load("glClearBufferfi");
    gl->fn.pixel_storei = (gl3_pixel_storei_fn)load("glPixelStorei");
    gl->fn.read_pixels = (gl3_read_pixels_fn)load("glReadPixels");
    gl->fn.read_buffer = (gl3_read_buffer_fn)load("glReadBuffer");
    gl->fn.get_string = (gl3_get_string_fn)load("glGetString");
    gl->fn.polygon_mode = (gl3_polygon_mode_fn)load("glPolygonMode"); /* absent on ES/WebGL: not required */
    gl->fn.draw_arrays = (gl3_draw_arrays_fn)load("glDrawArrays");
    gl->fn.draw_elements = (gl3_draw_elements_fn)load("glDrawElements");
    gl->fn.active_texture = (gl3_active_texture_fn)load("glActiveTexture");
    gl->fn.gen_textures = (gl3_gen_textures_fn)load("glGenTextures");
    gl->fn.delete_textures = (gl3_delete_textures_fn)load("glDeleteTextures");
    gl->fn.bind_texture = (gl3_bind_texture_fn)load("glBindTexture");
    gl->fn.tex_image_2d = (gl3_tex_image_2d_fn)load("glTexImage2D");
    gl->fn.tex_sub_image_2d = (gl3_tex_sub_image_2d_fn)load("glTexSubImage2D");
    gl->fn.tex_parameteri = (gl3_tex_parameteri_fn)load("glTexParameteri");
    gl->fn.gen_framebuffers = (gl3_gen_fn)load("glGenFramebuffers");
    gl->fn.gen_renderbuffers = (gl3_gen_fn)load("glGenRenderbuffers");
    gl->fn.delete_framebuffers = (gl3_delete_fn)load("glDeleteFramebuffers");
    gl->fn.delete_renderbuffers = (gl3_delete_fn)load("glDeleteRenderbuffers");
    gl->fn.bind_framebuffer = (gl3_bind_fn)load("glBindFramebuffer");
    gl->fn.bind_renderbuffer = (gl3_bind_fn)load("glBindRenderbuffer");
    gl->fn.check_framebuffer_status = (gl3_check_framebuffer_fn)load("glCheckFramebufferStatus");
    gl->fn.framebuffer_texture_2d = (gl3_framebuffer_texture_2d_fn)load("glFramebufferTexture2D");
    gl->fn.framebuffer_renderbuffer =
        (gl3_framebuffer_renderbuffer_fn)load("glFramebufferRenderbuffer");
    gl->fn.renderbuffer_storage = (gl3_renderbuffer_storage_fn)load("glRenderbufferStorage");
    gl->fn.blit_framebuffer = (gl3_blit_framebuffer_fn)load("glBlitFramebuffer");
    gl->fn.gen_vertex_arrays = (gl3_gen_vertex_arrays_fn)load("glGenVertexArrays");
    gl->fn.delete_vertex_arrays = (gl3_delete_vertex_arrays_fn)load("glDeleteVertexArrays");
    gl->fn.bind_vertex_array = (gl3_bind_vertex_array_fn)load("glBindVertexArray");
    gl->fn.enable_vertex_attrib_array = (gl3_enable_vertex_attrib_fn)load("glEnableVertexAttribArray");
    gl->fn.vertex_attrib_pointer = (gl3_vertex_attrib_pointer_fn)load("glVertexAttribPointer");
    gl->fn.gen_buffers = (gl3_gen_buffers_fn)load("glGenBuffers");
    gl->fn.delete_buffers = (gl3_delete_buffers_fn)load("glDeleteBuffers");
    gl->fn.bind_buffer = (gl3_bind_buffer_fn)load("glBindBuffer");
    gl->fn.buffer_data = (gl3_buffer_data_fn)load("glBufferData");
    gl->fn.bind_buffer_base = (gl3_bind_buffer_base_fn)load("glBindBufferBase");
    gl->fn.create_shader = (gl3_create_shader_fn)load("glCreateShader");
    gl->fn.shader_source = (gl3_shader_source_fn)load("glShaderSource");
    gl->fn.compile_shader = (gl3_compile_shader_fn)load("glCompileShader");
    gl->fn.get_shaderiv = (gl3_get_shaderiv_fn)load("glGetShaderiv");
    gl->fn.get_shader_info_log = (gl3_get_shader_log_fn)load("glGetShaderInfoLog");
    gl->fn.delete_shader = (gl3_delete_shader_fn)load("glDeleteShader");
    gl->fn.create_program = (gl3_create_program_fn)load("glCreateProgram");
    gl->fn.attach_shader = (gl3_attach_shader_fn)load("glAttachShader");
    gl->fn.link_program = (gl3_link_program_fn)load("glLinkProgram");
    gl->fn.get_programiv = (gl3_get_programiv_fn)load("glGetProgramiv");
    gl->fn.get_program_info_log = (gl3_get_program_log_fn)load("glGetProgramInfoLog");
    gl->fn.delete_program = (gl3_delete_program_fn)load("glDeleteProgram");
    gl->fn.use_program = (gl3_use_program_fn)load("glUseProgram");
    gl->fn.get_uniform_block_index = (gl3_get_uniform_block_index_fn)load("glGetUniformBlockIndex");
    gl->fn.uniform_block_binding = (gl3_uniform_block_binding_fn)load("glUniformBlockBinding");
    gl->fn.get_uniform_location = (gl3_get_uniform_location_fn)load("glGetUniformLocation");
    gl->fn.uniform1i = (gl3_uniform1i_fn)load("glUniform1i");
    ok = ok && gl->fn.viewport && gl->fn.scissor && gl->fn.enable && gl->fn.disable &&
        gl->fn.blend_func && gl->fn.depth_func && gl->fn.depth_mask && gl->fn.color_mask &&
        gl->fn.stencil_func && gl->fn.stencil_op && gl->fn.stencil_mask && gl->fn.cull_face &&
        gl->fn.front_face && gl->fn.clear_bufferfv && gl->fn.clear_bufferfi && gl->fn.pixel_storei &&
        gl->fn.read_pixels && gl->fn.read_buffer && gl->fn.get_string && gl->fn.draw_arrays &&
        gl->fn.draw_elements && gl->fn.active_texture && gl->fn.gen_textures && gl->fn.delete_textures &&
        gl->fn.bind_texture && gl->fn.tex_image_2d && gl->fn.tex_sub_image_2d && gl->fn.tex_parameteri &&
        gl->fn.gen_framebuffers && gl->fn.gen_renderbuffers && gl->fn.delete_framebuffers &&
        gl->fn.delete_renderbuffers && gl->fn.bind_framebuffer && gl->fn.bind_renderbuffer &&
        gl->fn.check_framebuffer_status && gl->fn.framebuffer_texture_2d &&
        gl->fn.framebuffer_renderbuffer && gl->fn.renderbuffer_storage && gl->fn.blit_framebuffer &&
        gl->fn.gen_vertex_arrays && gl->fn.delete_vertex_arrays && gl->fn.bind_vertex_array &&
        gl->fn.enable_vertex_attrib_array && gl->fn.vertex_attrib_pointer && gl->fn.gen_buffers &&
        gl->fn.delete_buffers && gl->fn.bind_buffer && gl->fn.buffer_data && gl->fn.bind_buffer_base &&
        gl->fn.create_shader && gl->fn.shader_source && gl->fn.compile_shader && gl->fn.get_shaderiv &&
        gl->fn.get_shader_info_log && gl->fn.delete_shader && gl->fn.create_program &&
        gl->fn.attach_shader && gl->fn.link_program && gl->fn.get_programiv &&
        gl->fn.get_program_info_log && gl->fn.delete_program && gl->fn.use_program &&
        gl->fn.get_uniform_block_index && gl->fn.uniform_block_binding &&
        gl->fn.get_uniform_location && gl->fn.uniform1i;
    return ok;
}

/* ============================================================================
 * Shaders
 * ============================================================================ */

static GLuint gl3_compile(const gl3_t *gl, GLenum stage, const char *source)
{
    GLuint shader = gl->fn.create_shader(stage);
    if (shader == 0) {
        return 0;
    }
    GLint length = (GLint)strlen(source);
    gl->fn.shader_source(shader, 1, &source, &length);
    gl->fn.compile_shader(shader);
    GLint status = GL_FALSE;
    gl->fn.get_shaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        char log[1024];
        GLsizei got = 0;
        gl->fn.get_shader_info_log(shader, (GLsizei)sizeof(log), &got, log);
        fprintf(stderr, "Error: GL3 shader failed to compile: %.*s\n", (int)got, log);
        gl->fn.delete_shader(shader);
        return 0;
    }
    return shader;
}

static GLuint gl3_link(const gl3_t *gl, GLuint vertex, GLuint fragment)
{
    GLuint program = gl->fn.create_program();
    if (program == 0) {
        return 0;
    }
    gl->fn.attach_shader(program, vertex);
    gl->fn.attach_shader(program, fragment);
    gl->fn.link_program(program);
    GLint status = GL_FALSE;
    gl->fn.get_programiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        char log[1024];
        GLsizei got = 0;
        gl->fn.get_program_info_log(program, (GLsizei)sizeof(log), &got, log);
        fprintf(stderr, "Error: GL3 shader failed to link: %.*s\n", (int)got, log);
        gl->fn.delete_program(program);
        return 0;
    }
    /* Binding points fixed once, the same for both variants: Transform at 0, Stage at 1,
       texture_2d at unit 0, texture_cube at unit 1 (fixed.frag's set 2 bindings 0 and 1) */
    GLuint transform = gl->fn.get_uniform_block_index(program, "Transform");
    GLuint stage = gl->fn.get_uniform_block_index(program, "Stage");
    if (transform != (GLuint)-1) {
        gl->fn.uniform_block_binding(program, transform, 0);
    }
    if (stage != (GLuint)-1) {
        gl->fn.uniform_block_binding(program, stage, 1);
    }
    gl->fn.use_program(program);
    GLint sampler_2d = gl->fn.get_uniform_location(program, "texture_2d");
    GLint sampler_cube = gl->fn.get_uniform_location(program, "texture_cube");
    if (sampler_2d >= 0) {
        gl->fn.uniform1i(sampler_2d, 0);
    }
    if (sampler_cube >= 0) {
        gl->fn.uniform1i(sampler_cube, 1);
    }
    return program;
}

static bool gl3_make_programs(gl3_t *gl)
{
    /* Generated by tools/scripts/gen_gpu_shaders.py into shaders/fixed_glsl.h: smooth and flat
       shaded, each a separate compiled variant (GLSL ties interpolation qualifiers to the
       pipeline, so a flat varying needs its own shader, as the GPU backend's -DFLAT build does). */
    const char *vertex_src[2] = {
        (const char *)(gl->es ? fixed_es_smooth_vertex_glsl : fixed_core_smooth_vertex_glsl),
        (const char *)(gl->es ? fixed_es_flat_vertex_glsl : fixed_core_flat_vertex_glsl)};
    const char *fragment_src[2] = {
        (const char *)(gl->es ? fixed_es_smooth_fragment_glsl : fixed_core_smooth_fragment_glsl),
        (const char *)(gl->es ? fixed_es_flat_fragment_glsl : fixed_core_flat_fragment_glsl)};
    for (int flat = 0; flat < 2; flat++) {
        gl->vertex_shader[flat] = gl3_compile(gl, GL_VERTEX_SHADER, vertex_src[flat]);
        gl->fragment_shader[flat] = gl3_compile(gl, GL_FRAGMENT_SHADER, fragment_src[flat]);
        if (gl->vertex_shader[flat] == 0 || gl->fragment_shader[flat] == 0) {
            return false;
        }
        gl->program[flat] = gl3_link(gl, gl->vertex_shader[flat], gl->fragment_shader[flat]);
        if (gl->program[flat] == 0) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * Buffers
 * ============================================================================ */

static bool gl3_make_buffers(gl3_t *gl)
{
    gl->fn.gen_vertex_arrays(1, &gl->vao);
    gl->fn.gen_buffers(1, &gl->vertex_buffer);
    gl->fn.gen_buffers(1, &gl->index_buffer);
    gl->fn.gen_buffers(1, &gl->transform_ubo);
    gl->fn.gen_buffers(1, &gl->stage_ubo);
    if (gl->vao == 0 || gl->vertex_buffer == 0 || gl->index_buffer == 0 || gl->transform_ubo == 0 ||
        gl->stage_ubo == 0) {
        return false;
    }
    gl->fn.bind_vertex_array(gl->vao);
    gl->fn.bind_buffer(GL_ARRAY_BUFFER, gl->vertex_buffer);
    gl->fn.bind_buffer(GL_ELEMENT_ARRAY_BUFFER, gl->index_buffer);
    const gl3_vertex_t *null_vertex = NULL;
    gl->fn.vertex_attrib_pointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(gl3_vertex_t),
                                 null_vertex->position);
    gl->fn.vertex_attrib_pointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(gl3_vertex_t),
                                 null_vertex->normal);
    gl->fn.vertex_attrib_pointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(gl3_vertex_t),
                                 null_vertex->uv);
    gl->fn.vertex_attrib_pointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(gl3_vertex_t),
                                 null_vertex->color);
    gl->fn.vertex_attrib_pointer(4, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(gl3_vertex_t),
                                 null_vertex->specular);
    for (GLuint i = 0; i < 5; i++) {
        gl->fn.enable_vertex_attrib_array(i);
    }
    return true;
}

/* Room for `size` bytes in `buffer` (bound already); grows, never shrinks */
static void gl3_reserve(const gl3_t *gl, GLenum target, GLuint buffer, uint32_t *have, uint32_t size)
{
    gl->fn.bind_buffer(target, buffer);
    if (size <= *have) {
        return;
    }
    uint32_t grown = *have > 0 ? *have : 4096u;
    while (grown < size) {
        grown *= 2u;
    }
    gl->fn.buffer_data(target, (GLsizeiptr)grown, NULL, GL_DYNAMIC_DRAW);
    *have = grown;
}

/* ============================================================================
 * Textures
 * ============================================================================ */

static uintptr_t gl3_texture_create(render_backend_t *backend)
{
    gl3_t *gl = (gl3_t *)backend;
    GLuint name = 0;
    gl->fn.gen_textures(1, &name);
    return (uintptr_t)name;
}

static void gl3_texture_destroy(render_backend_t *backend, uintptr_t texture)
{
    gl3_t *gl = (gl3_t *)backend;
    GLuint name = (GLuint)texture;
    gl->fn.delete_textures(1, &name);
}

static void gl3_texture_image(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                              int32_t face, int32_t level, int32_t width, int32_t height,
                              const uint8_t *rgba)
{
    gl3_t *gl = (gl3_t *)backend;
    gl->fn.pixel_storei(GL_UNPACK_ALIGNMENT, 1);
    if (kind == RENDER_TEXTURE_CUBE) {
        gl->fn.bind_texture(GL_TEXTURE_CUBE_MAP, (GLuint)texture);
        gl->fn.tex_image_2d(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, level, GL_RGBA, width, height,
                            0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else {
        gl->fn.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
        gl->fn.tex_image_2d(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                            rgba);
    }
}

static void gl3_texture_levels(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                               int32_t last)
{
    gl3_t *gl = (gl3_t *)backend;
    GLenum target = kind == RENDER_TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    gl->fn.bind_texture(target, (GLuint)texture);
    gl->fn.tex_parameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    gl->fn.tex_parameteri(target, GL_TEXTURE_MAX_LEVEL, last);
}

static void gl3_texture_sub_image(render_backend_t *backend, uintptr_t texture, int32_t x, int32_t y,
                                  int32_t width, int32_t height, const uint8_t *rgba)
{
    gl3_t *gl = (gl3_t *)backend;
    gl->fn.bind_texture(GL_TEXTURE_2D, (GLuint)texture);
    gl->fn.pixel_storei(GL_UNPACK_ALIGNMENT, 1);
    gl->fn.tex_sub_image_2d(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static void gl3_texture_target(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                               int32_t width, int32_t height, bool keep_faces)
{
    /* keep_faces is a hint this backend does not act on: glGetTexLevelParameteriv (to tell
       whether a cube face already has this size) does not exist on OpenGL ES or WebGL, so every
       face is always remade. Ballance's one render-to-cube-map use (env-mapped rails) sets this
       up once a level, so the extra upload is not a per-frame cost. */
    gl3_t *gl = (gl3_t *)backend;
    (void)keep_faces;
    bool cube = kind == RENDER_TEXTURE_CUBE;
    GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    gl->fn.bind_texture(target, (GLuint)texture);
    gl->fn.pixel_storei(GL_UNPACK_ALIGNMENT, 1);
    for (int face = 0; face < (cube ? 6 : 1); face++) {
        gl->fn.tex_image_2d(cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face : GL_TEXTURE_2D, 0,
                            GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    gl->fn.tex_parameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    gl->fn.tex_parameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
    gl->fn.tex_parameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->fn.tex_parameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/* ============================================================================
 * Targets (the screen's FBO, the kept-last-frame FBO, and the one reused for
 * every render-to-texture pass)
 * ============================================================================ */

/* `fbo`'s color (2D or, with `cube`, all six faces) and combined depth+stencil, `width` x
   `height`; made fresh when the size, kind or cube-ness changed. False when it cannot be made. */
static bool gl3_fbo_resize(const gl3_t *gl, gl3_fbo_t *fbo, int32_t width, int32_t height, bool cube)
{
    if (fbo->fbo != 0 && fbo->width == width && fbo->height == height && fbo->cube == cube) {
        return true;
    }
    if (fbo->fbo == 0) {
        gl->fn.gen_framebuffers(1, &fbo->fbo);
        gl->fn.gen_textures(1, &fbo->color);
        gl->fn.gen_renderbuffers(1, &fbo->depth);
    }
    GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    gl->fn.bind_texture(target, fbo->color);
    for (int face = 0; face < (cube ? 6 : 1); face++) {
        gl->fn.tex_image_2d(cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face : GL_TEXTURE_2D, 0,
                            GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    gl->fn.tex_parameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->fn.tex_parameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->fn.bind_renderbuffer(GL_RENDERBUFFER, fbo->depth);
    gl->fn.renderbuffer_storage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, fbo->fbo);
    gl->fn.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : GL_TEXTURE_2D, fbo->color, 0);
    gl->fn.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->depth);
    gl->fn.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fbo->depth);
    bool ok = gl->fn.check_framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    fbo->width = width;
    fbo->height = height;
    fbo->cube = cube;
    return ok;
}

static void gl3_fbo_free(const gl3_t *gl, gl3_fbo_t *fbo)
{
    if (fbo->fbo != 0) {
        gl->fn.delete_framebuffers(1, &fbo->fbo);
    }
    if (fbo->color != 0) {
        gl->fn.delete_textures(1, &fbo->color);
    }
    if (fbo->depth != 0) {
        gl->fn.delete_renderbuffers(1, &fbo->depth);
    }
    memset(fbo, 0, sizeof(*fbo));
}

static bool gl3_begin_screen(render_backend_t *backend, int32_t width, int32_t height, bool preserve)
{
    gl3_t *gl = (gl3_t *)backend;
    bool had = gl->screen.fbo != 0 && gl->screen.width == width && gl->screen.height == height;
    if (!gl3_fbo_resize(gl, &gl->screen, width, height, false)) {
        return false;
    }
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->screen.fbo);
    if (!had) {
        /* Black, far and 0, as a new window's buffers are */
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        gl->fn.clear_bufferfv(GL_COLOR, 0, black);
        gl->fn.clear_bufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0);
    }
    gl->flip = false;
    gl->target_h = height;
    gl->screen_open = true;
    return preserve && had;
}

static void gl3_end_screen(render_backend_t *backend)
{
    gl3_t *gl = (gl3_t *)backend;
    gl->screen_open = false;
    /* A copy for read_last_frame, before the screen is shown */
    if (gl3_fbo_resize(gl, &gl->last, gl->screen.width, gl->screen.height, false)) {
        gl->fn.bind_framebuffer(GL_READ_FRAMEBUFFER, gl->screen.fbo);
        gl->fn.bind_framebuffer(GL_DRAW_FRAMEBUFFER, gl->last.fbo);
        gl->fn.blit_framebuffer(0, 0, gl->screen.width, gl->screen.height, 0, 0, gl->screen.width,
                                gl->screen.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    /* Shown: blitted into the context's own default framebuffer, which the view swaps */
    gl->fn.bind_framebuffer(GL_READ_FRAMEBUFFER, gl->screen.fbo);
    gl->fn.bind_framebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl->fn.blit_framebuffer(0, 0, gl->screen.width, gl->screen.height, 0, 0, gl->screen.width,
                            gl->screen.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, 0);
}

static bool gl3_begin_target(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                             int32_t face, int32_t width, int32_t height)
{
    gl3_t *gl = (gl3_t *)backend;
    if (face < 0 || face >= 6) {
        return false;
    }
    if (gl->target.fbo == 0) {
        gl->fn.gen_framebuffers(1, &gl->target.fbo);
        gl->fn.gen_renderbuffers(1, &gl->target.depth);
    }
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->target.fbo);
    if (gl->target.width != width || gl->target.height != height) {
        gl->fn.bind_renderbuffer(GL_RENDERBUFFER, gl->target.depth);
        gl->fn.renderbuffer_storage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        gl->fn.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                        gl->target.depth);
        gl->fn.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                        gl->target.depth);
        gl->target.width = width;
        gl->target.height = height;
    }
    GLenum attach =
        kind == RENDER_TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face : GL_TEXTURE_2D;
    gl->fn.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, attach, (GLuint)texture, 0);
    if (gl->fn.check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        gl->fn.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, attach, 0, 0);
        gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->screen_open ? gl->screen.fbo : 0);
        return false;
    }
    gl->flip = true;
    gl->target_h = height;
    return true;
}

static void gl3_end_target(render_backend_t *backend)
{
    gl3_t *gl = (gl3_t *)backend;
    gl->flip = false;
    gl->target_h = gl->screen.height;
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->screen_open ? gl->screen.fbo : 0);
}

/* ============================================================================
 * States
 * ============================================================================ */

/* A rectangle from the target's top (viewport, scissor, or a read): GL's bottom-origin when
   drawing the screen; used as given when drawing a render target (render_gl.c's rgl_rect) */
static void gl3_rect(const gl3_t *gl, const int32_t rect[4], GLint out[4])
{
    out[0] = rect[0];
    out[1] = gl->flip ? rect[1] : gl->target_h - rect[1] - rect[3];
    out[2] = rect[2];
    out[3] = rect[3];
}

static void gl3_scissor(const gl3_t *gl, const render_state_t *s)
{
    if (s->scissor) {
        GLint r[4];
        gl3_rect(gl, s->scissor_rect, r);
        gl->fn.enable(GL_SCISSOR_TEST);
        gl->fn.scissor(r[0], r[1], r[2], r[3]);
    } else {
        gl->fn.disable(GL_SCISSOR_TEST);
    }
}

/* render_clip_projection's matrix (Direct3D/Vulkan depth 0..w), mirrored vertically when
   drawing into a render target (gl->flip), as render_gl.c's is. The GLSL shader itself remaps
   depth to GL's -1..1 (tools/scripts/gen_gpu_shaders.py built it with --fixup-clipspace), so
   this stays in Direct3D/Vulkan depth, unlike render_gl.c's own CPU-side remap. */
static void gl3_projection(const gl3_t *gl, const render_state_t *s, float out[16])
{
    render_clip_projection(s, out);
    if (gl->flip) {
        out[1] = -out[1];
        out[5] = -out[5];
        out[9] = -out[9];
        out[13] = -out[13];
    }
}

static void gl3_apply(const gl3_t *gl, const render_state_t *s, gl3_vertex_uniforms_t *vu,
                      gl3_fragment_uniforms_t *fu)
{
    GLint viewport[4];
    gl3_rect(gl, s->viewport, viewport);
    gl->fn.viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    gl3_scissor(gl, s);
    /* No MSAA (s->antialias) and, on OpenGL ES or WebGL, no glPolygonMode: both draw solid,
       the gaps the GPU backend's POINT fill already has */
    if (gl->fn.polygon_mode != NULL) {
        gl->fn.polygon_mode(GL_FRONT_AND_BACK, s->fill == RENDER_FILL_LINE    ? GL_LINE
                                                : s->fill == RENDER_FILL_POINT ? GL_POINT
                                                                               : GL_FILL);
    }
    if (s->cull) {
        gl->fn.enable(GL_CULL_FACE);
    } else {
        gl->fn.disable(GL_CULL_FACE);
    }
    gl->fn.cull_face(GL_BACK);
    gl->fn.front_face(s->front_ccw != gl->flip ? GL_CCW : GL_CW);
    if (s->blend) {
        gl->fn.enable(GL_BLEND);
        gl->fn.blend_func(gl3_blend(s->source_blend), gl3_blend(s->dest_blend));
    } else {
        gl->fn.disable(GL_BLEND);
    }
    if (s->depth_test) {
        gl->fn.enable(GL_DEPTH_TEST);
    } else {
        gl->fn.disable(GL_DEPTH_TEST);
    }
    gl->fn.depth_func(gl3_compare(s->depth_func));
    gl->fn.depth_mask(s->depth_write ? GL_TRUE : GL_FALSE);
    GLboolean color = s->color_write ? GL_TRUE : GL_FALSE;
    gl->fn.color_mask(color, color, color, color);
    if (s->stencil) {
        gl->fn.enable(GL_STENCIL_TEST);
        gl->fn.stencil_func(gl3_compare(s->stencil_func), (GLint)s->stencil_ref, s->stencil_mask);
        gl->fn.stencil_op(gl3_stencil_op(s->stencil_fail), gl3_stencil_op(s->stencil_zfail),
                          gl3_stencil_op(s->stencil_pass));
    } else {
        gl->fn.disable(GL_STENCIL_TEST);
    }
    gl->fn.stencil_mask(s->stencil_write_mask);

    memset(vu, 0, sizeof(*vu));
    memset(fu, 0, sizeof(*fu));
    memcpy(vu->model_view, s->model_view, sizeof(vu->model_view));
    gl3_projection(gl, s, vu->projection);
    if (!render_affine_inverse(vu->normal_matrix, s->model_view)) {
        static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        memcpy(vu->normal_matrix, identity, sizeof(identity));
    }
    memcpy(vu->texture_matrix, s->texture_matrix, sizeof(vu->texture_matrix));
    vu->mode[0] = (int32_t)s->texgen;
    if (s->fog) {
        vu->fog[0] = (float)(s->fog_mode >= 1 && s->fog_mode <= 3 ? s->fog_mode : 3);
        vu->fog[1] = s->fog_start;
        vu->fog[2] = s->fog_end;
        vu->fog[3] = s->fog_density;
    }
    render_color(s->fog_color, fu->fog_color);
    render_color(s->sampler.border_color, fu->border_color);
    fu->alpha[0] = s->alpha_ref;
    fu->flags[0] = s->color_sum;
    fu->flags[1] = s->fog;
    fu->flags[2] = s->alpha_test;
    fu->flags[3] = s->alpha_func;

    const render_texture_kind_t kind = s->texture_kind;
    fu->texture[0] = kind == RENDER_TEXTURE_NONE || s->texture == 0 ? 0 : kind == RENDER_TEXTURE_CUBE ? 2 : 1;
    fu->texture[1] = (int32_t)s->texenv;
    fu->texture[2] = s->sampler.address;
    gl->fn.active_texture(GL_TEXTURE0);
    gl->fn.bind_texture(GL_TEXTURE_2D, kind == RENDER_TEXTURE_CUBE || s->texture == 0 ? 0 : (GLuint)s->texture);
    if (kind != RENDER_TEXTURE_CUBE && s->texture != 0) {
        GLenum wrap = gl3_address(s->sampler.address);
        gl->fn.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
        gl->fn.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
        gl->fn.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl3_min_filter(&s->sampler));
        gl->fn.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                              s->sampler.mag_filter == RENDER_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR);
    }
    gl->fn.active_texture(GL_TEXTURE1);
    gl->fn.bind_texture(GL_TEXTURE_CUBE_MAP, kind == RENDER_TEXTURE_CUBE && s->texture != 0
                                                 ? (GLuint)s->texture
                                                 : 0);
    if (kind == RENDER_TEXTURE_CUBE && s->texture != 0) {
        GLenum wrap = gl3_address(s->sampler.address);
        gl->fn.tex_parameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, (GLint)wrap);
        gl->fn.tex_parameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, (GLint)wrap);
        gl->fn.tex_parameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, (GLint)wrap);
        gl->fn.tex_parameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, gl3_min_filter(&s->sampler));
        gl->fn.tex_parameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,
                              s->sampler.mag_filter == RENDER_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR);
    }
}

/* ============================================================================
 * Drawing
 * ============================================================================ */

static void gl3_clear(render_backend_t *backend, const render_state_t *s, bool color, bool depth,
                      bool stencil, const float value[4])
{
    gl3_t *gl = (gl3_t *)backend;
    gl3_scissor(gl, s);
    gl->fn.depth_mask(s->depth_write ? GL_TRUE : GL_FALSE);
    GLboolean write = s->color_write ? GL_TRUE : GL_FALSE;
    gl->fn.color_mask(write, write, write, write);
    gl->fn.stencil_mask(s->stencil_write_mask);
    if (color) {
        gl->fn.clear_bufferfv(GL_COLOR, 0, value);
    }
    if (depth || stencil) {
        /* Clearing only one of the pair: the other's write mask (set above) keeps it as it was */
        if (!depth) {
            gl->fn.depth_mask(GL_FALSE);
        }
        if (!stencil) {
            gl->fn.stencil_mask(0u);
        }
        gl->fn.clear_bufferfi(GL_DEPTH_STENCIL, 0, 1.0f, (GLint)s->clear_stencil);
    }
}

/* The draw's vertices, interleaved; the state's color and specular where the geometry has none */
static bool gl3_vertices(const render_state_t *s, const render_geometry_t *g, gl3_vertex_t *out)
{
    for (int32_t i = 0; i < g->vertex_count; i++) {
        gl3_vertex_t *v = &out[i];
        memset(v, 0, sizeof(*v));
        memcpy(v->position, (const uint8_t *)g->positions + (size_t)i * g->position_stride,
               sizeof(v->position));
        if (g->normals != NULL) {
            memcpy(v->normal, (const uint8_t *)g->normals + (size_t)i * g->normal_stride,
                   sizeof(v->normal));
        }
        if (g->uvs != NULL) {
            memcpy(v->uv, (const uint8_t *)g->uvs + (size_t)i * g->uv_stride, sizeof(v->uv));
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
        const uint8_t *spec =
            g->speculars != NULL ? &g->speculars[(size_t)i * g->specular_stride] : s->secondary;
        for (int c = 0; c < 3; c++) {
            v->specular[c] = (float)spec[c] / 255.0f;
        }
    }
    return true;
}

static void gl3_draw(render_backend_t *backend, const render_state_t *s, const render_geometry_t *g)
{
    gl3_t *gl = (gl3_t *)backend;
    if (g->positions == NULL || g->vertex_count <= 0) {
        return;
    }
    gl3_vertex_uniforms_t vu;
    gl3_fragment_uniforms_t fu;
    gl3_apply(gl, s, &vu, &fu);

    gl->fn.use_program(gl->program[s->flat ? 1 : 0]);
    gl->fn.bind_vertex_array(gl->vao);
    uint32_t vertex_bytes = (uint32_t)g->vertex_count * (uint32_t)sizeof(gl3_vertex_t);
    gl3_reserve(gl, GL_ARRAY_BUFFER, gl->vertex_buffer, &gl->vertex_buffer_size, vertex_bytes);
    gl3_vertex_t stack_vertices[256];
    gl3_vertex_t *vertices =
        (uint32_t)g->vertex_count <= 256u
            ? stack_vertices
            : (gl3_vertex_t *)malloc((size_t)g->vertex_count * sizeof(gl3_vertex_t));
    if (vertices == NULL) {
        return;
    }
    gl3_vertices(s, g, vertices);
    gl->fn.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)vertex_bytes, vertices, GL_DYNAMIC_DRAW);
    if (vertices != stack_vertices) {
        free(vertices);
    }
    gl->fn.bind_buffer_base(GL_UNIFORM_BUFFER, 0, gl->transform_ubo);
    gl->fn.buffer_data(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(vu), &vu, GL_DYNAMIC_DRAW);
    gl->fn.bind_buffer_base(GL_UNIFORM_BUFFER, 1, gl->stage_ubo);
    gl->fn.buffer_data(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(fu), &fu, GL_DYNAMIC_DRAW);

    GLenum mode = gl3_primitive(g->primitive);
    if (g->indices != NULL) {
        uint32_t index_bytes = (uint32_t)g->index_count * (uint32_t)sizeof(uint16_t);
        gl3_reserve(gl, GL_ELEMENT_ARRAY_BUFFER, gl->index_buffer, &gl->index_buffer_size, index_bytes);
        gl->fn.buffer_data(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)index_bytes, g->indices, GL_DYNAMIC_DRAW);
        gl->fn.draw_elements(mode, g->index_count, GL_UNSIGNED_SHORT, NULL);
    } else {
        gl->fn.draw_arrays(mode, 0, g->vertex_count);
    }
}

/* ============================================================================
 * Reading
 * ============================================================================ */

/* GL's rows of `rect` (bottom first) into `rgba` top first, from whichever framebuffer is bound */
static bool gl3_read_rect(const gl3_t *gl, int32_t target_h, const int32_t rect[4], uint8_t *rgba)
{
    size_t row = (size_t)rect[2] * 4u;
    uint8_t *pixels = (uint8_t *)malloc(row * (size_t)rect[3]);
    if (pixels == NULL) {
        return false;
    }
    gl->fn.pixel_storei(GL_PACK_ALIGNMENT, 1);
    gl->fn.read_pixels(rect[0], target_h - rect[1] - rect[3], rect[2], rect[3], GL_RGBA, GL_UNSIGNED_BYTE,
                       pixels);
    for (int32_t y = 0; y < rect[3]; y++) {
        memcpy(rgba + (size_t)y * row, pixels + (size_t)(rect[3] - 1 - y) * row, row);
    }
    free(pixels);
    return true;
}

static bool gl3_last_frame_ready(render_backend_t *backend, int32_t width, int32_t height)
{
    const gl3_t *gl = (const gl3_t *)backend;
    return gl->last.fbo != 0 && gl->last.width == width && gl->last.height == height;
}

static bool gl3_read_last_frame(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    gl3_t *gl = (gl3_t *)backend;
    if (gl->last.fbo == 0 || rect[0] < 0 || rect[1] < 0 || rect[0] + rect[2] > gl->last.width ||
        rect[1] + rect[3] > gl->last.height) {
        return false;
    }
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->last.fbo);
    gl->fn.read_buffer(GL_COLOR_ATTACHMENT0);
    bool read = gl3_read_rect(gl, gl->last.height, rect, rgba);
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->screen_open ? gl->screen.fbo : 0);
    return read;
}

static nmo_status_t gl3_read_pixels(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    gl3_t *gl = (gl3_t *)backend;
    if (gl->screen.fbo == 0 || rect[0] < 0 || rect[1] < 0 || rect[0] + rect[2] > gl->screen.width ||
        rect[1] + rect[3] > gl->screen.height) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->screen.fbo);
    gl->fn.read_buffer(GL_COLOR_ATTACHMENT0);
    bool read = gl3_read_rect(gl, gl->screen.height, rect, rgba);
    gl->fn.bind_framebuffer(GL_FRAMEBUFFER, gl->screen_open ? gl->screen.fbo : 0);
    return read ? NMO_OK : NMO_ERR_NOMEM;
}

/* ============================================================================
 * The backend
 * ============================================================================ */

static void gl3_free(gl3_t *gl)
{
    gl3_fbo_free(gl, &gl->screen);
    gl3_fbo_free(gl, &gl->last);
    gl3_fbo_free(gl, &gl->target);
    if (gl->vao != 0) {
        gl->fn.delete_vertex_arrays(1, &gl->vao);
    }
    GLuint buffers[4] = {gl->vertex_buffer, gl->index_buffer, gl->transform_ubo, gl->stage_ubo};
    for (int i = 0; i < 4; i++) {
        if (buffers[i] != 0) {
            gl->fn.delete_buffers(1, &buffers[i]);
        }
    }
    for (int i = 0; i < 2; i++) {
        if (gl->program[i] != 0) {
            gl->fn.delete_program(gl->program[i]);
        }
        if (gl->vertex_shader[i] != 0) {
            gl->fn.delete_shader(gl->vertex_shader[i]);
        }
        if (gl->fragment_shader[i] != 0) {
            gl->fn.delete_shader(gl->fragment_shader[i]);
        }
    }
    if (gl->own_context != NULL) {
        SDL_GL_DestroyContext(gl->own_context);
    }
    if (gl->own_window != NULL) {
        SDL_DestroyWindow(gl->own_window);
    }
    free(gl);
}

/* The backend's destroy op: frees everything gl3_free does, the offscreen test's own window and
   context (gl->own_window/own_context) included. */
static void gl3_destroy(render_backend_t *backend)
{
    gl3_free((gl3_t *)backend);
}

static const render_backend_ops_t gl3_ops = {
    gl3_destroy,
    gl3_texture_create,
    gl3_texture_destroy,
    gl3_texture_image,
    gl3_texture_levels,
    gl3_texture_sub_image,
    gl3_texture_target,
    gl3_begin_screen,
    gl3_end_screen,
    gl3_begin_target,
    gl3_end_target,
    gl3_clear,
    gl3_draw,
    gl3_last_frame_ready,
    gl3_read_last_frame,
    gl3_read_pixels,
};

static nmo_status_t gl3_create(nmo_render_gl3_loader_t load, gl3_t **out_gl)
{
    *out_gl = NULL;
    gl3_t *gl = (gl3_t *)calloc(1, sizeof(*gl));
    if (gl == NULL) {
        return NMO_ERR_NOMEM;
    }
    if (!gl3_load_functions(gl, load)) {
        gl3_free(gl);
        return NMO_ERR_NOT_SUPPORTED;
    }
    gl->base.ops = &gl3_ops;
    const char *version = (const char *)gl->fn.get_string(GL_VERSION);
    gl->es = version != NULL && strncmp(version, "OpenGL ES", 9) == 0;
    if (!gl3_make_programs(gl) || !gl3_make_buffers(gl)) {
        gl3_free(gl);
        return NMO_ERR_NOT_SUPPORTED;
    }
    *out_gl = gl;
    return NMO_OK;
}

nmo_status_t nmo_render_create_gl3(nmo_engine_t *engine, nmo_render_gl3_loader_t load,
                                   nmo_render_t **out_render)
{
    if (engine == NULL || load == NULL || out_render == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_render = NULL;
    gl3_t *gl = NULL;
    nmo_status_t status = gl3_create(load, &gl);
    if (status != NMO_OK) {
        return status;
    }
    status = render_create(engine, &gl->base, out_render);
    if (status != NMO_OK) {
        gl3_destroy(&gl->base);
    }
    return status;
}

/* SDL's loader of the current context's functions, for the offscreen test backend */
static nmo_render_gl3_function_t gl3_sdl_load(const char *name)
{
    return (nmo_render_gl3_function_t)SDL_GL_GetProcAddress(name);
}

render_backend_t *render_gl3_backend_create_offscreen(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return NULL;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window *window = SDL_CreateWindow("nmo_render_gl3 (offscreen)", 16, 16, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    SDL_GLContext context = window != NULL ? SDL_GL_CreateContext(window) : NULL;
    if (context == NULL) {
        if (window != NULL) {
            SDL_DestroyWindow(window);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    gl3_t *gl = NULL;
    nmo_status_t status = gl3_create(gl3_sdl_load, &gl);
    if (status != NMO_OK) {
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    gl->own_window = window;
    gl->own_context = context;
    return &gl->base;
}
