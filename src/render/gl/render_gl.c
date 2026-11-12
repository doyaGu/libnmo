/**
 * @file render_gl.c
 * @brief The OpenGL backend: the engine's frame drawn with OpenGL's fixed
 *        function pipeline the way CK2's rasterizer sets up Direct3D.
 *
 * Matrices stay in Virtools' convention (row vectors, left-handed, depth
 * 0..1): the model-view matrix is world times view and the projection is
 * VxMatrix::Perspective followed by a remap of depth to OpenGL's -1..1, so
 * both load as they are. Front faces are clockwise on screen, as Direct3D
 * culls counter-clockwise ones. What Direct3D does that GL's fixed function
 * cannot (its vertex lighting, wrapped coordinates, mipmaps of cutouts) the
 * front end's helpers do on the CPU.
 */

#define GL_SILENCE_DEPRECATION

#include "render/nmo_render_gl.h"
#include "../render_internal.h"
#include "../render_image.h"
#include "../render_light.h"
#include "../render_math.h"
#include "../render_mesh.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <math.h>
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

typedef struct rgl_texture {
    nmo_object_id_t id;
    GLuint name;
    uint32_t version;
    bool empty; /* no image: drawn untextured */
    bool mipmapped; /* uploaded with its mipmaps */
    bool forced;    /* mipmapped by nmo_render_set_mipmaps, not by CK */
    bool target;    /* a render pass's: what it drew, not the engine's image */
    bool cube;      /* a cube map: its six faces, no mipmaps */
    int target_w, target_h;
} rgl_texture_t;

typedef struct rgl {
    render_backend_t base;
    nmo_engine_t *engine;
    rgl_texture_t *textures;
    size_t texture_count, texture_capacity;
    render_mesh_cache_t meshes;
    float anisotropy;                /* the driver's most for mipmapped textures; 0 for none */
    int view_x, view_y, view_w, view_h; /* where the frame being drawn shows in its target, in pixels */
    int scene_view[4]; /* where the frame being drawn draws its 3D: GL x, y (bottom), width, height */
    float eye[16];     /* its camera's world matrix: the scene's root, the texgen referential */
    int32_t shading;     /* the frame's global render mode: 0 wireframe, 1 flat */
    bool wireframe_layer; /* the frame being drawn's global render mode draws a wireframe over meshes */
    bool use_textures;
    /* Drawing a render pass: upside down, so the texture's first row is the frame's top as CK has it */
    bool flip;
    /* The framebuffer render passes draw in, and its depth and stencil */
    GLuint target_fbo, target_depth;
    int target_w, target_h;
    /* The frame kept between frames when the render context does not clear its background
       (Global Blur): drawn there, then shown */
    GLuint kept_fbo, kept_color, kept_depth;
    /* The frame drawn before, for Copy Buffer to Texture (CK copies the back buffer as the block runs) */
    GLuint last_fbo, last_color;
    int last_w, last_h;
    bool last_valid;
    /* The vertex colors and speculars of the lit mesh being drawn */
    uint8_t *lit_colors, *lit_speculars;
    int32_t lit_capacity;
    int kept_w, kept_h;
} rgl_t;

/* A sprite text's texture: its sprite's pixels, uploaded the first time it is drawn; 0 for none */
static GLuint rgl_text_texture(rgl_t *gl, const nmo_engine_draw_t *draw)
{
    render_text_sprite_t *sprite = render_text_sprite(gl->base.render->text, draw);
    if (sprite == NULL) {
        return 0;
    }
    if (sprite->handle == 0) {
        GLuint name = 0;
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sprite->width, sprite->height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, sprite->rgba);
        sprite->handle = (uintptr_t)name;
    }
    return (GLuint)sprite->handle;
}

/* ============================================================================
 * Resources
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


/* The bound texture's levels below the image: its file's own (CKTexture's user mipmaps), else
   made from it down to 1 x 1; how many there are */
static int rgl_upload_mipmaps(rgl_t *gl, nmo_object_id_t id,
                               const nmo_engine_image_t *image)
{
    int saved = nmo_engine_texture_user_mipmap_count(gl->engine, id);
    if (saved > 0) {
        int level = 0;
        nmo_engine_image_t mip;
        while (level < saved && nmo_engine_texture_user_mipmap(gl->engine, id, level + 1, &mip)) {
            level++;
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, mip.width, mip.height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, mip.rgba);
        }
        return level;
    }
    int w = image->width, h = image->height, level = 0;
    const uint8_t *source = image->rgba;
    bool binary = render_image_is_binary(source, w, h);
    uint8_t *made = NULL;
    while (w > 1 || h > 1) {
        int next_w = 0, next_h = 0;
        uint8_t *half = render_half_image(source, w, h, binary, &next_w, &next_h);
        free(made);
        if (half == NULL) {
            break;
        }
        made = half;
        source = half;
        w = next_w;
        h = next_h;
        level++;
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, half);
    }
    free(made);
    return level;
}

static GLuint rgl_texture(rgl_t *gl, nmo_object_id_t id)
{
    if (id == 0) {
        return 0;
    }
    rgl_texture_t *texture = NULL;
    for (size_t i = 0; i < gl->texture_count; i++) {
        if (gl->textures[i].id == id) {
            texture = &gl->textures[i];
            break;
        }
    }
    if (texture != NULL && texture->target) {
        return texture->name;
    }
    /* A cube map's faces, its version the newest of theirs */
    bool cube = nmo_engine_texture_cube_map(gl->engine, id);
    nmo_engine_image_t faces[6];
    nmo_engine_image_t image;
    bool has_image = false;
    if (cube) {
        has_image = true;
        image.version = 0;
        for (int32_t face = 0; face < 6 && has_image; face++) {
            has_image = nmo_engine_texture_face_image(gl->engine, id, face, &faces[face]);
            image.version = faces[face].version > image.version ? faces[face].version : image.version;
        }
    } else {
        has_image = nmo_engine_texture_image(gl->engine, id, &image);
    }
    if (texture == NULL) {
        if (gl->texture_count == gl->texture_capacity) {
            size_t capacity = gl->texture_capacity != 0 ? gl->texture_capacity * 2 : 64;
            rgl_texture_t *grown =
                (rgl_texture_t *)realloc(gl->textures, capacity * sizeof(*grown));
            if (grown == NULL) {
                return 0;
            }
            gl->textures = grown;
            gl->texture_capacity = capacity;
        }
        texture = &gl->textures[gl->texture_count++];
        memset(texture, 0, sizeof(*texture));
        texture->id = id;
        glGenTextures(1, &texture->name);
        texture->version = has_image ? image.version - 1 : 0;
        texture->empty = true;
    }
    /* Forced mipmaps leave out cutouts (clear texels, a transparent color's): small sprites
       of them would gather their dark edges into rings, which CK draws without */
    if (cube) {
        if (has_image && image.version != texture->version) {
            texture->version = image.version;
            texture->empty = false;
            texture->cube = true;
            texture->mipmapped = texture->forced = false;
            glBindTexture(GL_TEXTURE_CUBE_MAP, texture->name);
            for (int32_t face = 0; face < 6; face++) {
                /* CK's face order and rows are GL's */
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, 0, GL_RGBA, faces[face].width,
                             faces[face].height, 0, GL_RGBA, GL_UNSIGNED_BYTE, faces[face].rgba);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
        }
        return texture->empty ? 0 : texture->name;
    }
    bool mipmapped = has_image && (image.mipmapped ||
                                   (gl->base.render->mipmap_all && !render_image_has_clear_texels(&image)));
    if (has_image && (image.version != texture->version || mipmapped != texture->mipmapped)) {
        texture->version = image.version;
        texture->empty = false;
        texture->mipmapped = mipmapped;
        texture->forced = mipmapped && !image.mipmapped;
        glBindTexture(GL_TEXTURE_2D, texture->name);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.rgba);
        /* RCKTexture::SystemToVideoMemory: mipmaps only for a texture that uses them */
        int levels = mipmapped ? rgl_upload_mipmaps(gl, id, &image) : 0;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels);
    }
    return texture->empty ? 0 : texture->name;
}

/* The texture rgl_texture() last gave, as it was uploaded; NULL for none */
static const rgl_texture_t *rgl_uploaded(const rgl_t *gl, nmo_object_id_t id)
{
    for (size_t i = 0; i < gl->texture_count; i++) {
        if (gl->textures[i].id == id) {
            return &gl->textures[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Render states
 * ============================================================================ */

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

/* The draw's stencil test (the render context's states it was made with) and its winding */
static void rgl_draw_state(const rgl_t *gl, const nmo_engine_draw_t *draw)
{
    if (draw->stencil.enabled) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(rgl_compare(draw->stencil.func), (GLint)draw->stencil.ref, draw->stencil.mask);
        glStencilOp(rgl_stencil_op(draw->stencil.fail), rgl_stencil_op(draw->stencil.zfail),
                    rgl_stencil_op(draw->stencil.pass));
        glStencilMask(draw->stencil.write_mask);
    } else {
        glDisable(GL_STENCIL_TEST);
    }
    glFrontFace(draw->mirrored != gl->flip ? GL_CCW : GL_CW); /* INDIRECTMATRIX inverts it */
}

static GLint rgl_texture_env(int32_t blend)
{
    switch (blend) {
    case 1: /* DECAL */
    case 3: /* DECALALPHA */
    case 5: return GL_DECAL;
    case 7: return GL_REPLACE; /* COPY */
    case 8: return GL_ADD;
    default: return GL_MODULATE;
    }
}

static GLint rgl_filter(int32_t filter, bool minify)
{
    switch (filter) {
    case 1: return GL_NEAREST;
    case 3: return minify ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
    case 4: return minify ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST;
    case 5: return minify ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
    case 6:
    case 7: return minify ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    default: return GL_LINEAR;
    }
}


/*
 * RCKMaterial::TexGenEffect: reflect and chrome take the camera space
 * reflection vector or normal, planar the camera space position, transform
 * the mesh's UVs. With a referential, reflect and chrome turn by the basis
 * looking from it to the object drawn (`world`), planar takes it inverted,
 * transform takes its matrix; the result is scaled by 0.4 (0.5 planar), V
 * flipped, about the texture's middle. The eye space here is the camera's,
 * so GL's generation is CK's, and the matrices are CK's row-vector ones.
 * The cube map ones (31 reflect, 32 sky, 33 normals, 34 positions) take
 * the reflection vector, position, normal and position into a cube map
 * turned as the referential, or the camera; positions sends them into the
 * object's space as well. CK's cube faces and their rows are GL's.
 */

static void rgl_set_texgen(rgl_t *gl, const nmo_engine_material_t *m, const float *world)
{
    int32_t texgen = m != NULL ? m->texgen : 0;
    bool cube = texgen >= 31 && texgen <= 34; /* VXEFFECT_TGCUBEMAP_* */
    GLint mode = texgen == 2 || texgen == 31 ? GL_REFLECTION_MAP
                 : texgen == 3 || texgen == 33 ? GL_NORMAL_MAP
                 : texgen == 4 || texgen == 32 || texgen == 34 ? GL_EYE_LINEAR
                                                                : 0;
    float ref[16];
    bool referential = m != NULL && m->texgen_referential != 0 &&
                       nmo_engine_entity_world_matrix(gl->engine, m->texgen_referential, ref);
    float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (cube) {
        /* The referential's world, the scene root's (the camera's) without one; positions
           are then taken into the object's space; no translation either way */
        memcpy(t, referential ? ref : gl->eye, sizeof(t));
        float inverse[16];
        if (texgen == 34 && world != NULL && render_affine_inverse(inverse, world)) {
            render_multiply(t, t, inverse);
        }
        t[12] = t[13] = t[14] = 0.0f;
        t[15] = 1.0f;
    } else if (texgen == 1 && referential) {
        memcpy(t, ref, sizeof(t));
    } else if (mode != 0) {
        if (mode != GL_EYE_LINEAR && referential && world != NULL) {
            float dir[3] = {world[12] - ref[12], world[13] - ref[13], world[14] - ref[14]};
            float length = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (length == 0.0f) {
                dir[0] = dir[1] = 0.0f;
                dir[2] = 1.0f;
            } else {
                for (int k = 0; k < 3; k++) {
                    dir[k] /= length;
                }
            }
            float axis[3] = {0.0f, 1.0f, 0.0f};
            if (fabsf(dir[1]) > 0.999f) {
                axis[0] = 1.0f;
                axis[1] = 0.0f;
            }
            float along = axis[0] * dir[0] + axis[1] * dir[1] + axis[2] * dir[2];
            float up[3] = {axis[0] - dir[0] * along, axis[1] - dir[1] * along,
                           axis[2] - dir[2] * along};
            float up_length = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
            for (int k = 0; k < 3; k++) {
                up[k] /= up_length;
            }
            float right[3] = {up[1] * dir[2] - up[2] * dir[1], up[2] * dir[0] - up[0] * dir[2],
                              up[0] * dir[1] - up[1] * dir[0]};
            /* the basis' rows are right, up and dir: its inverse is its transpose */
            for (int k = 0; k < 3; k++) {
                t[k * 4 + 0] = right[k];
                t[k * 4 + 1] = up[k];
                t[k * 4 + 2] = dir[k];
            }
        } else if (mode == GL_EYE_LINEAR && referential) {
            render_affine_inverse(t, ref);
        }
        float scale = mode == GL_EYE_LINEAR ? 0.5f : 0.4f;
        for (int row = 0; row < 4; row++) {
            t[row * 4 + 0] *= scale;
            t[row * 4 + 1] *= -scale;
            t[row * 4 + 2] *= scale;
        }
        if (mode == GL_EYE_LINEAR) {
            t[12] += 0.5f;
            t[13] += 0.5f;
        } else {
            t[12] = 0.5f;
            t[13] = 0.5f;
            t[14] = 0.0f;
            t[15] = 1.0f;
        }
    }
    glMatrixMode(GL_TEXTURE);
    glLoadMatrixf(t);
    glMatrixMode(GL_MODELVIEW);
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

/* RCKMaterial::SetAsCurrent: lighting, texture, blending and tests */
static void rgl_set_material(rgl_t *gl, const nmo_engine_material_t *m, bool lit,
                              bool depth, const float *world)
{
    if (lit) {
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, m->diffuse);
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, m->ambient);
        /* VXRENDERSTATE_SPECULARENABLE only above a power of 0.05 */
        const float none[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, m->power > 0.05f ? m->specular : none);
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, m->emissive);
        float power = m->power < 0.0f ? 0.0f : m->power > 128.0f ? 128.0f : m->power;
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, power);
    }
    GLuint texture = gl->use_textures ? rgl_texture(gl, m->texture) : 0;
    if (texture != 0) {
        const rgl_texture_t *uploaded = rgl_uploaded(gl, m->texture);
        GLenum target = uploaded != NULL && uploaded->cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        glDisable(target == GL_TEXTURE_2D ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D);
        glEnable(target);
        glBindTexture(target, texture);
        GLint min_filter = rgl_filter(m->min_filter, true);
        /* Without mipmaps the device samples the image itself, whatever the mip filter; forced
           ones are sampled even under a filter that has no mip part */
        bool mipmapped = uploaded != NULL && uploaded->mipmapped;
        if (uploaded != NULL && uploaded->forced) {
            min_filter = min_filter == GL_NEAREST ? GL_NEAREST_MIPMAP_LINEAR
                         : min_filter == GL_LINEAR ? GL_LINEAR_MIPMAP_LINEAR
                                                   : min_filter;
        } else if (!mipmapped) {
            min_filter = min_filter == GL_NEAREST || min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                                 min_filter == GL_NEAREST_MIPMAP_LINEAR
                             ? GL_NEAREST
                             : GL_LINEAR;
        }
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min_filter);
        /* The window has many more pixels than CK's render context: mipmaps seen at a
           slant stay sharp */
        if (gl->anisotropy > 1.0f) {
            glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            mipmapped && min_filter != GL_NEAREST && min_filter != GL_LINEAR
                                ? gl->anisotropy
                                : 1.0f);
        }
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, rgl_filter(m->mag_filter, false));
        /* A cube's faces meet without seams */
        GLint wrap = target == GL_TEXTURE_CUBE_MAP ? GL_CLAMP_TO_EDGE : (GLint)rgl_address(m->address_mode);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
        if (target == GL_TEXTURE_CUBE_MAP) {
            glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
        } else if (wrap == GL_CLAMP_TO_BORDER) { /* the material's border color */
            float border[4];
            render_color(m->border_color, border);
            glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border);
        }
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, rgl_texture_env(m->texture_blend));
        rgl_set_texgen(gl, m, world);
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_CUBE_MAP);
        rgl_set_texgen(gl, NULL, NULL);
    }
    if (m->alpha_blend) {
        glEnable(GL_BLEND);
        glBlendFunc(rgl_blend(m->source_blend), rgl_blend(m->dest_blend));
    } else {
        glDisable(GL_BLEND);
    }
    if (m->alpha_test) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(rgl_compare(m->alpha_func), (float)m->alpha_ref / 255.0f);
    } else {
        glDisable(GL_ALPHA_TEST);
    }
    if (depth) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(rgl_compare(m->z_func));
        glDepthMask(m->z_write ? GL_TRUE : GL_FALSE);
    }
    if (m->two_sided) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
    }
    /* RCKMaterial::SetAsCurrent: the render context's wireframe or flat mode over the material's */
    glShadeModel(gl->shading == 1 || m->shade_mode == 1 ? GL_FLAT : GL_SMOOTH);
    glPolygonMode(GL_FRONT_AND_BACK, gl->shading == 0  ? GL_LINE
                                     : m->fill_mode == 1 ? GL_POINT
                                     : m->fill_mode == 2 ? GL_LINE
                                                         : GL_FILL);
}

/* The backend's buffers for lit colors, grown to `count` vertices */
static bool rgl_lit_buffers(rgl_t *gl, int32_t count)
{
    if (count <= gl->lit_capacity) {
        return true;
    }
    uint8_t *colors = (uint8_t *)realloc(gl->lit_colors, (size_t)count * 4u);
    if (colors != NULL) {
        gl->lit_colors = colors;
    }
    uint8_t *speculars = (uint8_t *)realloc(gl->lit_speculars, (size_t)count * 4u);
    if (speculars != NULL) {
        gl->lit_speculars = speculars;
    }
    if (colors == NULL || speculars == NULL) {
        return false;
    }
    gl->lit_capacity = count;
    return true;
}

/*
 * Light the mesh for a material and point the color arrays at the result
 * (GL's lighting off); the specular summed when any. False when the buffers
 * could not be made.
 */
static bool rgl_use_lit_colors(rgl_t *gl, const nmo_engine_frame_t *frame,
                                const nmo_engine_material_t *m, const nmo_vertex_t *vertices,
                                int32_t count, const float world[16])
{
    if (!rgl_lit_buffers(gl, count)) {
        return false;
    }
    render_lighting_t l = render_lighting(frame, m);
    bool specular = render_light_mesh(&l, vertices, count, world, gl->lit_colors, gl->lit_speculars);
    glDisable(GL_LIGHTING);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, gl->lit_colors);
    if (specular) {
        glEnable(GL_COLOR_SUM);
        glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
        glSecondaryColorPointer(3, GL_UNSIGNED_BYTE, 4, gl->lit_speculars);
    } else {
        glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
        glDisable(GL_COLOR_SUM);
    }
    return true;
}

/* ============================================================================
 * Drawing
 * ============================================================================ */

/* VxMatrix::Perspective (or Orthographic), then Direct3D's depth 0..w to OpenGL's -w..w */
static void rgl_load_projection(const rgl_t *gl, const nmo_engine_frame_t *frame, bool orthographic, float fov,
                                 float near_plane, float far_plane)
{
    float projection[16];
    render_projection(frame, orthographic, fov, near_plane, far_plane, projection);
    const float remap[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, -1, 1};
    render_multiply(projection, projection, remap);
    if (gl->flip) {
        projection[1] = -projection[1], projection[5] = -projection[5];
        projection[9] = -projection[9], projection[13] = -projection[13];
    }
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection);
}

static void rgl_draw_mesh_body(rgl_t *gl, const nmo_engine_frame_t *frame,
                                const nmo_engine_draw_t *draw, const float view_m[16],
                                nmo_engine_mesh_t mesh, render_mesh_t *cached);

static void rgl_draw_mesh_as(rgl_t *gl, const nmo_engine_frame_t *frame,
                              const nmo_engine_draw_t *draw, const float view_m[16])
{
    nmo_engine_mesh_t mesh;
    if (!nmo_engine_mesh(gl->engine, draw->mesh, &mesh) || mesh.vertices == NULL ||
        mesh.indices == NULL) {
        return;
    }
    render_wrapped_t wrapped;
    if ((mesh.wrap_u || mesh.wrap_v) && render_wrap_mesh(&mesh, &wrapped)) {
        render_mesh_t faces;
        memset(&faces, 0, sizeof(faces));
        faces.id = draw->mesh;
        render_mesh_fill(&faces, &mesh);
        if (faces.indices != NULL) {
            rgl_draw_mesh_body(gl, frame, draw, view_m, mesh, &faces);
        }
        render_mesh_release(&faces);
        free(faces.colors);
        free(faces.speculars);
        render_wrapped_free(&wrapped);
        return;
    }
    render_mesh_t *cached = render_mesh_cache_get(&gl->meshes, draw->mesh, &mesh);
    if (cached == NULL || cached->indices == NULL) {
        return;
    }
    rgl_draw_mesh_body(gl, frame, draw, view_m, mesh, cached);
}

static void rgl_draw_mesh_body(rgl_t *gl, const nmo_engine_frame_t *frame,
                                const nmo_engine_draw_t *draw, const float view_m[16],
                                nmo_engine_mesh_t mesh, render_mesh_t *cached)
{
    if (draw->fov > 0.0f) {
        rgl_load_projection(gl, frame, false, draw->fov, draw->near_plane, draw->far_plane);
    }
    if (draw->z_only) {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); /* VX_MOVEABLE_ZBUFONLY */
    }
    float model_view[16];
    render_multiply(model_view, draw->world, view_m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(model_view);

    const GLsizei stride = (GLsizei)sizeof(nmo_vertex_t);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, &mesh.vertices[0].position);
    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, stride, &mesh.vertices[0].normal);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, stride, &mesh.vertices[0].uv);
    if (draw->stencil_only) {
        /* RCKMesh::Render of a VX_MOVEABLE_STENCILONLY entity: 1 in every stencil bit, nothing else */
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0xFF);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        for (int32_t g = 0; g < cached->group_count; g++) {
            if (cached->counts[g] > 0) {
                glDrawElements(GL_TRIANGLES, cached->counts[g], GL_UNSIGNED_SHORT, cached->indices[g]);
            }
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        rgl_draw_state(gl, draw);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        return;
    }
    bool prelit = mesh.prelit && cached->colors != NULL;
    if (prelit) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, cached->colors);
    }
    /* RCKMesh::DefaultRender turns VXRENDERSTATE_SPECULARENABLE on for a prelit mesh: its
       vertices' specular colors are added after texturing */
    bool summed = prelit && cached->speculars != NULL;
    if (summed) {
        glEnable(GL_COLOR_SUM);
        glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
        glSecondaryColorPointer(3, GL_UNSIGNED_BYTE, 4, cached->speculars);
    }
    /* With a folded channel the device adds the specular after the texture stages */
    bool folded = false;
    for (int32_t c = 0; c < mesh.channel_count && !folded; c++) {
        nmo_engine_material_t channel_material;
        folded = mesh.channels[c].active &&
                 nmo_engine_material(gl->engine, mesh.channels[c].material, &channel_material) &&
                 render_channel_folds(&mesh.channels[c], &channel_material, prelit);
    }
    for (int32_t g = 0; g < cached->group_count; g++) {
        if (cached->counts[g] == 0) {
            continue;
        }
        nmo_engine_material_t material;
        nmo_object_id_t material_id = nmo_engine_mesh_material(gl->engine, draw->mesh, g);
        if (draw->replaced_material != 0 && material_id == draw->replaced_material) {
            material_id = draw->replacing_material;
        }
        bool known = material_id != 0 && nmo_engine_material(gl->engine, material_id, &material);
        if (known && draw->texgen != 0) { /* a pre-render's effect, the scene's root for referential */
            material.texgen = draw->texgen;
            material.texgen_referential = 0;
        }
        if (!known) {
            /* RCKRenderManager's default material: CKMaterial's defaults */
            memset(&material, 0, sizeof(material));
            material.diffuse[0] = material.diffuse[1] = material.diffuse[2] = 0.7f;
            material.diffuse[3] = 1.0f;
            material.ambient[0] = material.ambient[1] = material.ambient[2] = 0.3f;
            material.ambient[3] = 1.0f;
            material.z_write = true;
            material.z_func = 4;
            material.source_blend = 2;
            material.dest_blend = 1;
        } else if (draw->blackened) { /* ShadowCaster's BlackFunction */
            const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            memcpy(material.diffuse, black, sizeof(black));
            memcpy(material.ambient, black, sizeof(black));
            memcpy(material.emissive, black, sizeof(black));
        } else if (draw->fade) { /* a Motion Blur trail */
            material.alpha_blend = true;
            material.source_blend = 5; /* VXBLEND_SRCALPHA */
            material.dest_blend = 6;   /* VXBLEND_INVSRCALPHA */
            material.diffuse[3] *= draw->fade_alpha;
        }
        if (folded) {
            material.power = 0.0f; /* its specular comes after the channels */
        }
        glDisable(GL_LIGHTING);
        if (!prelit) {
            glColor4fv(material.diffuse);
            rgl_use_lit_colors(gl, frame, &material, mesh.vertices, mesh.vertex_count, draw->world);
        }
        rgl_set_material(gl, &material, false, true, draw->world);
        if (draw->no_z_test) {
            glDisable(GL_DEPTH_TEST);
        }
        if (draw->no_z_write) {
            glDepthMask(GL_FALSE);
        }
        glDrawElements(GL_TRIANGLES, cached->counts[g], GL_UNSIGNED_SHORT, cached->indices[g]);
    }
    if (summed || !prelit) {
        glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
        glDisable(GL_COLOR_SUM);
    }
    if (!prelit) {
        glDisableClientState(GL_COLOR_ARRAY);
    }
    /* RCKMesh::DefaultRender with the global render mode's wireframe: every face again in lines,
       in the vertex colors, untextured and unlit, with the projection's depth translation times
       1.003 */
    if (gl->wireframe_layer) {
        float projection[16];
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glGetFloatv(GL_PROJECTION_MATRIX, projection);
        projection[14] *= 1.003f;
        glLoadMatrixf(projection);
        glMatrixMode(GL_MODELVIEW);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_CUBE_MAP);
        rgl_set_texgen(gl, NULL, NULL);
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        if (cached->colors != NULL) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, cached->colors);
        } else {
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }
        for (int32_t g = 0; g < cached->group_count; g++) {
            if (cached->counts[g] > 0) {
                glDrawElements(GL_TRIANGLES, cached->counts[g], GL_UNSIGNED_SHORT, cached->indices[g]);
            }
        }
        if (!prelit) {
            glDisableClientState(GL_COLOR_ARRAY);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
    /* The material channels, each over the faces it covers */
    for (int32_t c = 0; c < mesh.channel_count; c++) {
        const nmo_engine_channel_t *channel = &mesh.channels[c];
        nmo_engine_material_t material;
        if (!channel->active || !nmo_engine_material(gl->engine, channel->material, &material)) {
            continue;
        }
        uint16_t *indices = (uint16_t *)malloc((size_t)mesh.face_count * 3u * sizeof(uint16_t));
        int32_t count = 0;
        for (int32_t f = 0; indices != NULL && f < mesh.face_count; f++) {
            if (channel->mask == 0 || mesh.faces == NULL || (mesh.faces[f].channel_mask & channel->mask)) {
                for (int k = 0; k < 3; k++) {
                    indices[count++] = mesh.indices[f * 3 + k];
                }
            }
        }
        bool lit = channel->lit && !prelit;
        if (render_channel_folds(channel, &material, prelit)) {
            lit = false;
            for (int k = 0; k < 4; k++) {
                material.diffuse[k] = 1.0f;
            }
            material.texture_blend = 4; /* MODULATEALPHA: the texture times white */
        }
        glDisable(GL_LIGHTING);
        glColor4fv(material.diffuse);
        if (lit) {
            rgl_use_lit_colors(gl, frame, &material, mesh.vertices, mesh.vertex_count, draw->world);
        } else if (!prelit) {
            glDisableClientState(GL_COLOR_ARRAY);
        }
        material.alpha_blend = true;
        material.source_blend = channel->source_blend;
        material.dest_blend = channel->dest_blend;
        material.z_write = false;
        material.z_func = 4;
        rgl_set_material(gl, &material, false, true, draw->world);
        if (channel->uvs != NULL) {
            glTexCoordPointer(2, GL_FLOAT, 0, channel->uvs);
        } else {
            glTexCoordPointer(2, GL_FLOAT, stride, &mesh.vertices[0].uv);
        }
        if (count > 0) {
            glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, indices);
        }
        if (lit) {
            glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
            glDisable(GL_COLOR_SUM);
            glDisableClientState(GL_COLOR_ARRAY);
        }
        free(indices);
    }
    /* The specular the folded channels left out, added over what they drew */
    for (int32_t g = 0; folded && g < cached->group_count; g++) {
        nmo_engine_material_t material;
        nmo_object_id_t material_id = nmo_engine_mesh_material(gl->engine, draw->mesh, g);
        if (draw->replaced_material != 0 && material_id == draw->replaced_material) {
            material_id = draw->replacing_material;
        }
        if (cached->counts[g] == 0 || material_id == 0 ||
            !nmo_engine_material(gl->engine, material_id, &material) || !(material.power > 0.05f)) {
            continue;
        }
        for (int k = 0; k < 4; k++) {
            material.diffuse[k] = material.ambient[k] = material.emissive[k] = 0.0f;
        }
        material.texture = 0;
        material.texgen = 0;
        material.alpha_blend = true;
        material.source_blend = 2; /* ONE */
        material.dest_blend = 2;
        material.z_write = false;
        material.z_func = 4;
        rgl_use_lit_colors(gl, frame, &material, mesh.vertices, mesh.vertex_count, draw->world);
        rgl_set_material(gl, &material, false, true, draw->world);
        glDrawElements(GL_TRIANGLES, cached->counts[g], GL_UNSIGNED_SHORT, cached->indices[g]);
        glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
        glDisable(GL_COLOR_SUM);
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (draw->fov > 0.0f) {
        rgl_load_projection(gl, frame, frame->orthographic, frame->fov, frame->near_plane,
                             frame->far_plane);
    }
}

/* A mesh draw; one with `wireframe` under the wireframe global render mode for itself */
static void rgl_draw_mesh(rgl_t *gl, const nmo_engine_frame_t *frame,
                           const nmo_engine_draw_t *draw, const float view_m[16])
{
    int32_t shading = gl->shading;
    if (draw->wireframe) {
        gl->shading = 0;
    }
    rgl_draw_mesh_as(gl, frame, draw, view_m);
    gl->shading = shading;
}

/* RCK2dEntity::Draw: a quad of the material's diffuse, textured with its source rect */
static void rgl_draw_quad(rgl_t *gl, const nmo_engine_draw_t *draw)
{
    nmo_engine_material_t material;
    if (draw->material != 0 && nmo_engine_material(gl->engine, draw->material, &material)) {
        rgl_set_material(gl, &material, false, false, NULL);
    } else {
        /* CKTextureFont::SetRenderStates, or a quad's own blending (Planar Filter) */
        memset(&material, 0, sizeof(material));
        material.texture = draw->texture;
        material.texture_blend = 4;
        material.alpha_blend = true;
        material.source_blend = draw->blend ? draw->source_blend : 5;
        material.dest_blend = draw->blend ? draw->dest_blend : 6;
        material.two_sided = true;
        material.address_mode = 3;
        material.min_filter = material.mag_filter = draw->nearest ? 1 : 2;
        material.shade_mode = draw->color2 != draw->color ? 2 : 1;
        if (draw->alpha_test) { /* a transparent sprite: its keyed texels dropped */
            material.alpha_test = true;
            material.alpha_func = 5; /* VXCMP_GREATER */
            material.alpha_ref = 0;
        }
        rgl_set_material(gl, &material, false, false, NULL);
    }
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    float top[4], bottom[4];
    render_color(draw->color, top);
    render_color(draw->color2, bottom);
    bool summed = (draw->specular & 0xFFFFFFu) != 0;
    if (summed) { /* VXRENDERSTATE_SPECULARENABLE and the vertices' specular color */
        glEnable(GL_COLOR_SUM);
        glSecondaryColor3ub((GLubyte)(draw->specular >> 16), (GLubyte)(draw->specular >> 8),
                            (GLubyte)draw->specular);
    }
    glBegin(GL_TRIANGLE_FAN);
    glColor4fv(top);
    glTexCoord2f(draw->uv[0], draw->uv[1]);
    glVertex2f(draw->rect[0] + draw->skew, draw->rect[1]);
    glTexCoord2f(draw->uv[2], draw->uv[1]);
    glVertex2f(draw->rect[2] + draw->skew, draw->rect[1]);
    glColor4fv(bottom);
    glTexCoord2f(draw->uv[2], draw->uv[3]);
    glVertex2f(draw->rect[2], draw->rect[3]);
    glTexCoord2f(draw->uv[0], draw->uv[3]);
    glVertex2f(draw->rect[0], draw->rect[3]);
    glEnd();
    if (summed) {
        glSecondaryColor3ub(0, 0, 0);
        glDisable(GL_COLOR_SUM);
    }
}

/* A sprite text: its sprite's pixels, blended over the frame without Z */
static void rgl_draw_sprite_text(rgl_t *gl, const nmo_engine_draw_t *draw)
{
    GLuint name = rgl_text_texture(gl, draw);
    if (name == 0) {
        return;
    }
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_CUBE_MAP);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    rgl_set_texgen(gl, NULL, NULL);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(draw->rect[0], draw->rect[1]);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(draw->rect[2], draw->rect[1]);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(draw->rect[2], draw->rect[3]);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(draw->rect[0], draw->rect[3]);
    glEnd();
}

/* A particle system's triangles (ParticleEmitter::SetState) or lines (RenderParticles_L);
   smooth shading, which only an Orientable Sprite's colors need */
static void rgl_draw_triangles(rgl_t *gl, const nmo_engine_frame_t *frame,
                                const nmo_engine_draw_t *draw, const float view_m[16])
{
    if (draw->vertex_count <= 0 || frame->vertices == NULL) {
        return;
    }
    bool lines = draw->kind == NMO_ENGINE_DRAW_LINES;
    nmo_engine_material_t material;
    memset(&material, 0, sizeof(material));
    material.texture = lines ? 0 : draw->texture;
    material.texture_blend = 4;
    material.min_filter = 6;
    material.mag_filter = 2;
    material.address_mode = 3;
    material.alpha_blend = draw->blend;
    material.source_blend = draw->source_blend;
    material.dest_blend = draw->dest_blend;
    material.alpha_test = draw->alpha_test;
    material.alpha_func = 5;
    material.two_sided = true;
    material.z_write = !lines && !draw->blend;
    material.z_func = 4;
    material.shade_mode = 2;
    glDisable(GL_LIGHTING);
    rgl_set_material(gl, &material, false, true, NULL);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_m);
    const nmo_engine_vertex_t *v = &frame->vertices[draw->first_vertex];
    glBegin(lines ? GL_LINES : GL_TRIANGLES);
    for (int32_t i = 0; i < draw->vertex_count; i++) {
        float color[4];
        render_color(v[i].color, color);
        glColor4fv(color);
        glTexCoord2f(v[i].u, v[i].v);
        glVertex3f(v[i].x, v[i].y, v[i].z);
    }
    glEnd();
}

/* A 3D sprite: its quad with its material set as current, unlit (CallSprite3DBatches) */
static void rgl_draw_sprite3d(rgl_t *gl, const nmo_engine_frame_t *frame,
                               const nmo_engine_draw_t *draw, const float view_m[16])
{
    nmo_engine_material_t material;
    if (draw->vertex_count <= 0 || frame->vertices == NULL ||
        !nmo_engine_material(gl->engine, draw->material, &material)) {
        return;
    }
    glDisable(GL_LIGHTING);
    rgl_set_material(gl, &material, false, true, NULL);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_m);
    const nmo_engine_vertex_t *v = &frame->vertices[draw->first_vertex];
    glBegin(GL_TRIANGLES);
    for (int32_t i = 0; i < draw->vertex_count; i++) {
        float color[4];
        render_color(v[i].color, color);
        glColor4fv(color);
        glTexCoord2f(v[i].u, v[i].v);
        glVertex3f(v[i].x, v[i].y, v[i].z);
    }
    glEnd();
}

/* A block's DrawPrimitive in a render callback: its material set as current (none: untextured,
   opaque), unlit in the vertices' colors or lit with their normals */
static void rgl_draw_primitive(rgl_t *gl, const nmo_engine_frame_t *frame,
                                const nmo_engine_draw_t *draw, const float view_m[16])
{
    nmo_engine_material_t material;
    if (draw->vertex_count <= 0 || frame->vertices == NULL) {
        return;
    }
    if (draw->material == 0 || !nmo_engine_material(gl->engine, draw->material, &material)) {
        memset(&material, 0, sizeof(material));
        material.diffuse[0] = material.diffuse[1] = material.diffuse[2] = material.diffuse[3] = 1.0f;
        material.z_write = !draw->no_z_write;
        material.z_func = 4;
        material.shade_mode = 2;
        material.alpha_blend = draw->blend;
        material.source_blend = draw->source_blend;
        material.dest_blend = draw->dest_blend;
        /* a texture set as current: modulated, clamped, linear (Lightmap's) */
        material.texture = draw->texture;
        material.texture_blend = 2;                      /* VXTEXTUREBLEND_MODULATE */
        material.address_mode = 3;                       /* VXTEXTURE_ADDRESSCLAMP */
        material.min_filter = material.mag_filter = 2;   /* VXTEXTUREFILTER_LINEAR */
    }
    if (draw->lines) {
        material.texture = 0;
    }
    if (draw->two_sided) {
        material.two_sided = true;
    }
    glDisable(GL_LIGHTING);
    rgl_set_material(gl, &material, false, true, NULL);
    render_lighting_t lighting = render_lighting(frame, &material);
    if (draw->lit && lighting.specular) {
        glEnable(GL_COLOR_SUM);
    }
    if (draw->projection_z_scale != 0.0f) {
        /* GL's depth translation is the matrix's [14], as D3D's is its [3][2] */
        float projection[16];
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glGetFloatv(GL_PROJECTION_MATRIX, projection);
        projection[14] *= draw->projection_z_scale;
        glLoadMatrixf(projection);
    }
    bool fogged = (draw->no_fog || draw->screen) && glIsEnabled(GL_FOG);
    if (fogged) {
        glDisable(GL_FOG);
    }
    if (draw->screen) {
        /* The viewport's pixels, its depth z itself (near 0, far -1) */
        const int32_t *vp = frame->viewport;
        double left = 0.0, top = 0.0, width = frame->width, height = frame->height;
        if (vp[2] > 0 && vp[3] > 0) {
            left = vp[0], top = vp[1], width = vp[2], height = vp[3];
        }
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(left, left + width, gl->flip ? top : top + height, gl->flip ? top + height : top, 0.0, -1.0);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_m);
    if (draw->screen) {
        glLoadIdentity();
    }
    const nmo_engine_vertex_t *v = &frame->vertices[draw->first_vertex];
    glBegin(draw->lines ? GL_LINES : GL_TRIANGLES);
    for (int32_t i = 0; i < draw->vertex_count; i++) {
        if (draw->lit) {
            /* in the world already: lit as they are */
            const float p[3] = {v[i].x, v[i].y, v[i].z};
            float n[3] = {v[i].nx, v[i].ny, v[i].nz};
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0.0f) {
                n[0] /= len, n[1] /= len, n[2] /= len;
            }
            uint8_t color[4], specular[4];
            render_light_vertex(&lighting, p, n, color, specular);
            glColor4ub(color[0], color[1], color[2], color[3]);
            glSecondaryColor3ub(specular[0], specular[1], specular[2]);
        } else {
            float color[4];
            render_color(v[i].color, color);
            glColor4fv(color);
        }
        glTexCoord2f(v[i].u, v[i].v);
        glVertex3f(v[i].x, v[i].y, v[i].z);
    }
    glEnd();
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_SUM);
    if (fogged) {
        glEnable(GL_FOG);
    }
    if (draw->screen) {
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
    if (draw->projection_z_scale != 0.0f) {
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
}

static void rgl_begin_2d(const rgl_t *gl, const nmo_engine_frame_t *frame)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, frame->width, gl->flip ? 0.0 : frame->height, gl->flip ? frame->height : 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/* The 3D pass: CKRenderedScene's camera, default states, fog and lights */
static void rgl_begin_3d(const rgl_t *gl, const nmo_engine_frame_t *frame, float out_view[16])
{
    rgl_load_projection(gl, frame, frame->orthographic, frame->fov, frame->near_plane,
                         frame->far_plane);
    render_inverse(out_view, frame->view);

    glFrontFace(gl->flip ? GL_CCW : GL_CW);
    glCullFace(GL_BACK);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glDisable(GL_COLOR_MATERIAL);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    /* Direct3D adds the specular color after texturing */
    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
    float ambient[4];
    render_color(frame->ambient_light, ambient);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

    if (frame->fog_mode != 0) {
        float color[4];
        render_color(frame->fog_color, color);
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, frame->fog_mode == 1   ? GL_EXP
                            : frame->fog_mode == 2 ? GL_EXP2
                                                   : GL_LINEAR);
        glFogfv(GL_FOG_COLOR, color);
        glFogf(GL_FOG_START, frame->fog_start);
        glFogf(GL_FOG_END, frame->fog_end);
        glFogf(GL_FOG_DENSITY, frame->fog_density);
    } else {
        glDisable(GL_FOG);
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(out_view);
    for (int i = 0; i < 8; i++) {
        glDisable(GL_LIGHT0 + i);
    }
    for (int32_t i = 0; i < frame->light_count && i < 8; i++) {
        const nmo_engine_light_t *light = &frame->lights[i];
        GLenum name = GL_LIGHT0 + (GLenum)i;
        const float black[4] = {0, 0, 0, 1};
        glLightfv(name, GL_DIFFUSE, light->color);
        glLightfv(name, GL_SPECULAR, light->specular ? light->color : black);
        glLightfv(name, GL_AMBIENT, black);
        if (light->type == 3) {
            /* towards the light: against its Z axis */
            const float direction[4] = {-light->direction[0], -light->direction[1],
                                        -light->direction[2], 0.0f};
            glLightfv(name, GL_POSITION, direction);
            glLightf(name, GL_SPOT_CUTOFF, 180.0f);
        } else {
            const float position[4] = {light->position[0], light->position[1],
                                       light->position[2], 1.0f};
            glLightfv(name, GL_POSITION, position);
            glLightf(name, GL_CONSTANT_ATTENUATION, light->attenuation[0]);
            glLightf(name, GL_LINEAR_ATTENUATION, light->attenuation[1]);
            glLightf(name, GL_QUADRATIC_ATTENUATION, light->attenuation[2]);
            if (light->type == 2) {
                glLightfv(name, GL_SPOT_DIRECTION, light->direction);
                glLightf(name, GL_SPOT_CUTOFF, light->outer_cone * 0.5f * 57.29578f);
                glLightf(name, GL_SPOT_EXPONENT, light->falloff);
            } else {
                glLightf(name, GL_SPOT_CUTOFF, 180.0f);
            }
        }
        glEnable(name);
    }
}

/* A Place's viewport clipping */
static void rgl_clip_draw(const rgl_t *gl, const nmo_engine_draw_t *draw, int window_height)
{
    if (!draw->clipped) { glDisable(GL_SCISSOR_TEST); return; }
    (void)window_height;
    int edges[4];
    for (int i = 0; i < 4; i++) {
        float normalized = fminf(fmaxf(draw->clip_rect[i],0),1);
        edges[i] = (int)(normalized*(float)((i & 1) ? gl->scene_view[3] : gl->scene_view[2]));
    }
    int width = edges[2] > edges[0] ? edges[2]-edges[0] : 0, height = edges[3] > edges[1] ? edges[3]-edges[1] : 0;
    glEnable(GL_SCISSOR_TEST);
    glScissor(gl->scene_view[0]+edges[0],gl->scene_view[1]+gl->scene_view[3]-edges[3],width,height);
}

/* A frame's draws into the viewport view_x, view_y, view_w, view_h (from the top) of a target
   `window_h` high: its clear, background, 2D entities and 3D draws */
static void rgl_draw_frame(rgl_t *gl, const nmo_engine_frame_t *frame, int window_h)
{
    gl->wireframe_layer = frame->wireframe_layer;
    /* VXRENDERSTATE_ANTIALIAS: the samples are there; they smooth only while a block turned it on */
    if (frame->antialias) {
        glEnable(GL_MULTISAMPLE);
    } else {
        glDisable(GL_MULTISAMPLE);
    }
    /* The frame's viewport in the window, for its 3D draws (the whole frame without one) */
    int *scene = gl->scene_view;
    const int32_t *viewport = frame->viewport;
    if (viewport[2] > 0 && viewport[3] > 0 && frame->width > 0 && frame->height > 0) {
        float sx = (float)gl->view_w / (float)frame->width, sy = (float)gl->view_h / (float)frame->height;
        int top = gl->view_y + (int)((float)viewport[1] * sy);
        scene[2] = (int)((float)viewport[2] * sx);
        scene[3] = (int)((float)viewport[3] * sy);
        scene[0] = gl->view_x + (int)((float)viewport[0] * sx);
        scene[1] = window_h - top - scene[3];
    } else {
        scene[0] = gl->view_x;
        scene[1] = window_h - gl->view_y - gl->view_h;
        scene[2] = gl->view_w;
        scene[3] = gl->view_h;
    }
    glViewport(gl->view_x, window_h - gl->view_y - gl->view_h, gl->view_w, gl->view_h);
    glEnable(GL_SCISSOR_TEST);
    if (frame->render_options & 0x100u) { /* CK_RENDER_CLEARVIEWPORT: the viewport alone */
        glScissor(scene[0], scene[1], scene[2], scene[3]);
    } else {
        glScissor(gl->view_x, window_h - gl->view_y - gl->view_h, gl->view_w, gl->view_h);
    }
    float background[4];
    render_color(frame->background_color, background);
    glClearColor(background[0], background[1], background[2], 1.0f);
    /* RCKRenderContext::Clear: the background only with CK_RENDER_CLEARBACK, depth with CLEARZ */
    GLbitfield clear = ((frame->render_options & 0x20u) ? GL_COLOR_BUFFER_BIT : 0u) |
                       ((frame->render_options & 0x10u) ? GL_DEPTH_BUFFER_BIT : 0u) |
                       ((frame->render_options & 0x40u) ? GL_STENCIL_BUFFER_BIT : 0u);
    if (clear != 0) {
        glDepthMask(GL_TRUE); /* a clear writes only what the masks let through */
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0xFF);
        glClear(clear);
    }
    glDisable(GL_SCISSOR_TEST);

    if (frame->background_texture != 0 && (frame->render_options & 0x20u)) {
        nmo_engine_draw_t quad;
        memset(&quad, 0, sizeof(quad));
        quad.rect[2] = (float)frame->width;
        quad.rect[3] = (float)frame->height;
        quad.uv[2] = quad.uv[3] = 1.0f;
        quad.texture = frame->background_texture;
        quad.color = quad.color2 = frame->background_color | 0xFF000000u;
        rgl_begin_2d(gl, frame);
        rgl_draw_quad(gl, &quad);
    }

    /* Background 2D entities, then the 3D draws in the frame's order (the engine
       puts the transparent objects after the opaque ones, sorted as the scene
       graph sorts them), the post primitives, and the foreground 2D entities */
    int32_t first_mesh = 0;
    while (first_mesh < frame->draw_count && frame->draws[first_mesh].kind == NMO_ENGINE_DRAW_QUAD &&
           frame->draws[first_mesh].background) {
        first_mesh++;
    }
    rgl_begin_2d(gl, frame);
    for (int32_t i = 0; i < first_mesh; i++) {
        rgl_draw_quad(gl, &frame->draws[i]);
    }
    int32_t end_mesh = first_mesh;
    while (end_mesh < frame->draw_count && frame->draws[end_mesh].kind != NMO_ENGINE_DRAW_QUAD &&
           frame->draws[end_mesh].kind != NMO_ENGINE_DRAW_SPRITE_TEXT) {
        end_mesh++;
    }
    float view_m[16];
    glViewport(scene[0], scene[1], scene[2], scene[3]);
    rgl_begin_3d(gl, frame, view_m);
    memcpy(gl->eye, frame->view, sizeof(gl->eye));
    for (int32_t i = first_mesh; i < end_mesh; i++) {
        const nmo_engine_draw_t *draw = &frame->draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_PRIMITIVE && draw->post) {
            continue; /* after the others */
        }
        rgl_clip_draw(gl, draw, window_h);
        rgl_draw_state(gl, draw);
        if (draw->kind == NMO_ENGINE_DRAW_TRIANGLES || draw->kind == NMO_ENGINE_DRAW_LINES) {
            rgl_draw_triangles(gl, frame, draw, view_m);
        } else if (draw->kind == NMO_ENGINE_DRAW_SPRITE3D) {
            rgl_draw_sprite3d(gl, frame, draw, view_m);
        } else if (draw->kind == NMO_ENGINE_DRAW_PRIMITIVE) {
            rgl_draw_primitive(gl, frame, draw, view_m);
        } else {
            rgl_draw_mesh(gl, frame, draw, view_m);
        }
    }
    for (int32_t i = first_mesh; i < end_mesh; i++) {
        const nmo_engine_draw_t *draw = &frame->draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_PRIMITIVE && draw->post) {
            rgl_clip_draw(gl, draw, window_h);
            if (draw->clears_stencil) {
                glStencilMask(0xFFu);
                glClearStencil((GLint)draw->stencil_clear);
                glClear(GL_STENCIL_BUFFER_BIT);
                continue;
            }
            rgl_draw_state(gl, draw);
            rgl_draw_primitive(gl, frame, draw, view_m);
        }
    }
    glDisable(GL_STENCIL_TEST);
    glFrontFace(GL_CW);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_LIGHTING);
    /* Additional View's renders after the 3D, then the 2D, then those over the 2D */
    for (int32_t v = 0; v < frame->view_count; v++) {
        if (!frame->views[v].over_2d) {
            rgl_draw_frame(gl, &frame->views[v].frame, window_h);
        }
    }
    glViewport(gl->view_x, window_h - gl->view_y - gl->view_h, gl->view_w, gl->view_h);
    rgl_begin_2d(gl, frame);
    for (int32_t i = end_mesh; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_SPRITE_TEXT) {
            rgl_draw_sprite_text(gl, &frame->draws[i]);
        } else if (frame->draws[i].stencil.enabled) {
            rgl_draw_state(gl, &frame->draws[i]); /* a quad drawn where the stencil says (a shadow's) */
            rgl_draw_quad(gl, &frame->draws[i]);
            glDisable(GL_STENCIL_TEST);
        } else {
            rgl_draw_quad(gl, &frame->draws[i]);
        }
    }
    for (int32_t v = 0; v < frame->view_count; v++) {
        if (frame->views[v].over_2d) {
            rgl_draw_frame(gl, &frame->views[v].frame, window_h);
        }
    }
    glViewport(gl->view_x, window_h - gl->view_y - gl->view_h, gl->view_w, gl->view_h);
}

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
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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

/* A render pass: into the texture its pass names (a cube map's face of its slot), which then shows
   what was drawn */
static void rgl_draw_pass(rgl_t *gl, const nmo_engine_pass_t *pass)
{
    if (gl->target_fbo == 0) {
        glGenFramebuffersEXT(1, &gl->target_fbo);
        glGenRenderbuffersEXT(1, &gl->target_depth);
    }
    rgl_texture(gl, pass->texture); /* its entry */
    rgl_texture_t *texture = NULL;
    for (size_t i = 0; i < gl->texture_count; i++) {
        if (gl->textures[i].id == pass->texture) {
            texture = &gl->textures[i];
        }
    }
    if (texture == NULL) {
        return;
    }
    /* A cube map's pass draws the face of its slot (SetRenderTarget's CubeMapFace) */
    bool cube = nmo_engine_texture_cube_map(gl->engine, pass->texture);
    GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    if (!texture->target || texture->cube != cube || texture->target_w != pass->width ||
        texture->target_h != pass->height) {
        bool kept = texture->cube; /* faces uploaded from the image stay until drawn over */
        texture->target = true;
        texture->cube = cube;
        texture->empty = false;
        texture->mipmapped = texture->forced = false;
        texture->target_w = pass->width;
        texture->target_h = pass->height;
        glBindTexture(target, texture->name);
        for (int face = 0; face < (cube ? 6 : 1); face++) {
            if (cube && kept) {
                GLint w = 0, h = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, 0, GL_TEXTURE_WIDTH, &w);
                glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, 0, GL_TEXTURE_HEIGHT, &h);
                if (w == pass->width && h == pass->height) {
                    continue;
                }
            }
            glTexImage2D(cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face : GL_TEXTURE_2D, 0, GL_RGBA,
                         pass->width, pass->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gl->target_fbo);
    if (gl->target_w != pass->width || gl->target_h != pass->height) {
        gl->target_w = pass->width;
        gl->target_h = pass->height;
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, gl->target_depth);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT, pass->width, pass->height);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
                                     gl->target_depth);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
                                     gl->target_depth);
    }
    GLenum attach = cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)(pass->slot >= 0 && pass->slot < 6 ? pass->slot : 0)
                         : GL_TEXTURE_2D;
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, attach, texture->name, 0);
    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT) {
        gl->view_x = pass->viewport[0];
        gl->view_w = pass->viewport[2];
        gl->view_h = pass->viewport[3];
        gl->view_y = pass->height - pass->viewport[1] - pass->viewport[3]; /* drawn upside down */
        gl->flip = true;
        rgl_draw_frame(gl, &pass->frame, pass->height);
        gl->flip = false;
    }
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, attach, 0, 0);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

/*
 * The frame's buffer copies (CopyToTexture), from what was just drawn: each
 * source rectangle of the render context, as the window scaled it, taken
 * nearest to its texel size into the texture's uploaded image, top row
 * first. Only a 2D texture's face 0 is copied.
 */
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

/* Read from the frame before (bound for reading); false when there is none of this size */
static bool rgl_bind_last_frame(rgl_t *gl, int window_w, int window_h)
{
    if (!gl->last_valid || gl->last_w != window_w || gl->last_h != window_h) {
        return false;
    }
    if (gl->last_fbo == 0) {
        glGenFramebuffersEXT(1, &gl->last_fbo);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gl->last_fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, gl->last_color, 0);
    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        return false;
    }
    return true;
}

/*
 * The frame's buffer copies, before it is drawn: CK copies the back buffer
 * as the block runs, so the copies hold the frame drawn before this one.
 */
static void rgl_copy_buffers(rgl_t *gl, const nmo_engine_frame_t *frame, int window_w, int window_h)
{
    if (frame->width <= 0 || frame->height <= 0 || frame->copy_count <= 0) {
        return;
    }
    GLint bound = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &bound);
    if (!rgl_bind_last_frame(gl, window_w, window_h)) {
        return;
    }
    float sx = (float)gl->view_w / (float)frame->width, sy = (float)gl->view_h / (float)frame->height;
    for (int32_t c = 0; c < frame->copy_count; c++) {
        const nmo_engine_buffer_copy_t *copy = &frame->copies[c];
        GLuint name = rgl_texture(gl, copy->texture);
        const rgl_texture_t *uploaded = rgl_uploaded(gl, copy->texture);
        if (name == 0 || uploaded == NULL || uploaded->cube || copy->face != 0) {
            continue;
        }
        int w = (int)(copy->dest[2] - copy->dest[0]), h = (int)(copy->dest[3] - copy->dest[1]);
        int x = gl->view_x + (int)(copy->src[0] * sx);
        int top = gl->view_y + (int)(copy->src[1] * sy);
        int read_w = (int)((float)w * sx), read_h = (int)((float)h * sy);
        if (w <= 0 || h <= 0 || read_w <= 0 || read_h <= 0) {
            continue;
        }
        uint8_t *read = (uint8_t *)malloc((size_t)read_w * (size_t)read_h * 4u);
        uint8_t *texels = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
        if (read != NULL && texels != NULL) {
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(x, window_h - top - read_h, read_w, read_h, GL_RGBA, GL_UNSIGNED_BYTE, read);
            for (int ty = 0; ty < h; ty++) {
                int ry = read_h - 1 - (int)((float)ty * sy); /* GL's rows go up */
                for (int tx = 0; tx < w; tx++) {
                    int rx = (int)((float)tx * sx);
                    memcpy(&texels[((size_t)ty * (size_t)w + (size_t)tx) * 4u],
                           &read[((size_t)ry * (size_t)read_w + (size_t)rx) * 4u], 4u);
                }
            }
            glBindTexture(GL_TEXTURE_2D, name);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)copy->dest[0], (GLint)copy->dest[1], w, h, GL_RGBA,
                            GL_UNSIGNED_BYTE, texels);
        }
        free(read);
        free(texels);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)bound);
}

/* ============================================================================
 * The backend
 * ============================================================================ */

static nmo_status_t rgl_draw(render_backend_t *backend, const nmo_engine_frame_t *frame,
                             int window_w, int window_h, const int rect[4])
{
    rgl_t *gl = (rgl_t *)backend;
    gl->shading = frame->shading;
    gl->use_textures = frame->use_textures;
    for (int32_t p = 0; p < frame->pass_count; p++) {
        rgl_draw_pass(gl, &frame->passes[p]);
    }
    gl->view_x = rect[0];
    gl->view_y = rect[1];
    gl->view_w = rect[2];
    gl->view_h = rect[3];

    /* Without CK_RENDER_CLEARBACK the frames before stay under the new one (Global Blur) */
    rgl_copy_buffers(gl, frame, window_w, window_h);
    if (!(frame->render_options & 0x20u) && rgl_kept_begin(gl, window_w, window_h)) {
        rgl_draw_frame(gl, frame, window_h);
        rgl_kept_end(gl, window_w, window_h);
    } else {
        glViewport(0, 0, window_w, window_h);
        glDepthMask(GL_TRUE); /* a clear writes only what the masks let through */
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        rgl_draw_frame(gl, frame, window_h);
    }
    rgl_keep_last_frame(gl, window_w, window_h);
    return NMO_OK;
}

/* The back buffer, before it is swapped: the front buffer is undefined once swapped (macOS
   composites the window) */
static nmo_status_t rgl_read_pixels(render_backend_t *backend, int window_h, const int rect[4],
                                    uint8_t *rgba)
{
    (void)backend;
    size_t row = (size_t)rect[2] * 4u;
    uint8_t *pixels = (uint8_t *)malloc(row * (size_t)rect[3]);
    if (pixels == NULL) {
        return NMO_ERR_NOMEM;
    }
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(rect[0], window_h - rect[1] - rect[3], rect[2], rect[3], GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);
    for (int y = 0; y < rect[3]; y++) { /* GL's rows go up */
        memcpy(rgba + (size_t)y * row, pixels + (size_t)(rect[3] - 1 - y) * row, row);
    }
    free(pixels);
    return NMO_OK;
}

static void rgl_release_text(render_backend_t *backend, uintptr_t handle)
{
    (void)backend;
    GLuint name = (GLuint)handle;
    glDeleteTextures(1, &name);
}

static void rgl_destroy(render_backend_t *backend)
{
    rgl_t *gl = (rgl_t *)backend;
    for (size_t i = 0; i < gl->texture_count; i++) {
        glDeleteTextures(1, &gl->textures[i].name);
    }
    free(gl->textures);
    if (gl->target_fbo != 0) {
        glDeleteFramebuffersEXT(1, &gl->target_fbo);
        glDeleteRenderbuffersEXT(1, &gl->target_depth);
    }
    free(gl->lit_colors);
    free(gl->lit_speculars);
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
    render_mesh_cache_free(&gl->meshes);
    free(gl);
}

static const render_backend_ops_t rgl_ops = {
    rgl_destroy,
    rgl_draw,
    rgl_read_pixels,
    rgl_release_text,
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
    gl->engine = engine;
    glEnable(GL_MULTISAMPLE);
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (extensions != NULL && strstr(extensions, "GL_EXT_texture_filter_anisotropic") != NULL) {
        GLfloat most = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
        gl->anisotropy = most > 8.0f ? 8.0f : most;
    }
    return render_create(engine, &gl->base, out_render);
}
