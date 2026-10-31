/**
 * @file view.c
 * @brief The engine's frame in an SDL3 window, drawn with OpenGL's fixed
 *        function pipeline the way CK2's rasterizer sets up Direct3D.
 *
 * Matrices stay in Virtools' convention (row vectors, left-handed, depth
 * 0..1): the model-view matrix is world times view and the projection is
 * VxMatrix::Perspective followed by a remap of depth to OpenGL's -1..1, so
 * both load as they are. Front faces are clockwise on screen, as Direct3D
 * culls counter-clockwise ones.
 */

#define GL_SILENCE_DEPRECATION

#include "engine/nmo_view.h"
#include "engine/nmo_engine_render.h"
#include "view_audio.h"
#include "view_text.h"
#include "core/nmo_arena.h"
#include "format/nmo_image.h"
#include "format/nmo_stb_adapter.h"

#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
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

typedef struct view_texture {
    nmo_object_id_t id;
    GLuint name;
    uint32_t version;
    bool empty; /* no image: drawn untextured */
} view_texture_t;

/* A mesh's faces split by material group, and its prelit colors as RGBA bytes */
typedef struct view_mesh {
    nmo_object_id_t id;
    int32_t group_count;
    uint16_t **indices;
    int32_t *counts;
    uint8_t *colors;
    uint32_t version; /* the mesh's colors were converted at */
} view_mesh_t;

struct nmo_view {
    nmo_engine_t *engine;
    SDL_Window *window;
    SDL_GLContext context;
    view_texture_t *textures;
    size_t texture_count, texture_capacity;
    view_mesh_t *meshes;
    size_t mesh_count, mesh_capacity;
    int frame_width, frame_height;   /* the render context */
    int view_x, view_y, view_w, view_h; /* where it shows in the window, in pixels */
    uint64_t start;
    bool cursor_shown;
    view_audio_t *audio; /* NULL when there is no audio device */
    view_text_t *text;   /* the sprite texts' fonts and textures */
};

/* ============================================================================
 * Input
 * ============================================================================ */

/* SDL scan codes to DirectInput's */
static uint32_t view_scan_code(SDL_Scancode code)
{
    static const struct {
        SDL_Scancode sdl;
        uint8_t dik;
    } keys[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03},
        {SDL_SCANCODE_3, 0x04}, {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06},
        {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08}, {SDL_SCANCODE_8, 0x09},
        {SDL_SCANCODE_9, 0x0A}, {SDL_SCANCODE_0, 0x0B}, {SDL_SCANCODE_MINUS, 0x0C},
        {SDL_SCANCODE_EQUALS, 0x0D}, {SDL_SCANCODE_BACKSPACE, 0x0E}, {SDL_SCANCODE_TAB, 0x0F},
        {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12},
        {SDL_SCANCODE_R, 0x13}, {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15},
        {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17}, {SDL_SCANCODE_O, 0x18},
        {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1A},
        {SDL_SCANCODE_RIGHTBRACKET, 0x1B}, {SDL_SCANCODE_RETURN, 0x1C},
        {SDL_SCANCODE_LCTRL, 0x1D}, {SDL_SCANCODE_A, 0x1E}, {SDL_SCANCODE_S, 0x1F},
        {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_F, 0x21}, {SDL_SCANCODE_G, 0x22},
        {SDL_SCANCODE_H, 0x23}, {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25},
        {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_SEMICOLON, 0x27},
        {SDL_SCANCODE_APOSTROPHE, 0x28}, {SDL_SCANCODE_GRAVE, 0x29},
        {SDL_SCANCODE_LSHIFT, 0x2A}, {SDL_SCANCODE_BACKSLASH, 0x2B}, {SDL_SCANCODE_Z, 0x2C},
        {SDL_SCANCODE_X, 0x2D}, {SDL_SCANCODE_C, 0x2E}, {SDL_SCANCODE_V, 0x2F},
        {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31}, {SDL_SCANCODE_M, 0x32},
        {SDL_SCANCODE_COMMA, 0x33}, {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35},
        {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_KP_MULTIPLY, 0x37},
        {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39}, {SDL_SCANCODE_CAPSLOCK, 0x3A},
        {SDL_SCANCODE_F1, 0x3B}, {SDL_SCANCODE_F2, 0x3C}, {SDL_SCANCODE_F3, 0x3D},
        {SDL_SCANCODE_F4, 0x3E}, {SDL_SCANCODE_F5, 0x3F}, {SDL_SCANCODE_F6, 0x40},
        {SDL_SCANCODE_F7, 0x41}, {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43},
        {SDL_SCANCODE_F10, 0x44}, {SDL_SCANCODE_KP_7, 0x47}, {SDL_SCANCODE_KP_8, 0x48},
        {SDL_SCANCODE_KP_9, 0x49}, {SDL_SCANCODE_KP_MINUS, 0x4A}, {SDL_SCANCODE_KP_4, 0x4B},
        {SDL_SCANCODE_KP_5, 0x4C}, {SDL_SCANCODE_KP_6, 0x4D}, {SDL_SCANCODE_KP_PLUS, 0x4E},
        {SDL_SCANCODE_KP_1, 0x4F}, {SDL_SCANCODE_KP_2, 0x50}, {SDL_SCANCODE_KP_3, 0x51},
        {SDL_SCANCODE_KP_0, 0x52}, {SDL_SCANCODE_KP_PERIOD, 0x53}, {SDL_SCANCODE_F11, 0x57},
        {SDL_SCANCODE_F12, 0x58}, {SDL_SCANCODE_KP_ENTER, 0x9C}, {SDL_SCANCODE_RCTRL, 0x9D},
        {SDL_SCANCODE_KP_DIVIDE, 0xB5}, {SDL_SCANCODE_RALT, 0xB8}, {SDL_SCANCODE_HOME, 0xC7},
        {SDL_SCANCODE_UP, 0xC8}, {SDL_SCANCODE_PAGEUP, 0xC9}, {SDL_SCANCODE_LEFT, 0xCB},
        {SDL_SCANCODE_RIGHT, 0xCD}, {SDL_SCANCODE_END, 0xCF}, {SDL_SCANCODE_DOWN, 0xD0},
        {SDL_SCANCODE_PAGEDOWN, 0xD1}, {SDL_SCANCODE_INSERT, 0xD2},
        {SDL_SCANCODE_DELETE, 0xD3},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].sdl == code) {
            return keys[i].dik;
        }
    }
    return 0;
}

/* A window point in render context pixels */
static void view_to_frame(const nmo_view_t *view, float x, float y, float *out_x, float *out_y)
{
    float density = SDL_GetWindowPixelDensity(view->window);
    float px = x * density - (float)view->view_x;
    float py = y * density - (float)view->view_y;
    *out_x = view->view_w > 0 ? px * (float)view->frame_width / (float)view->view_w : 0.0f;
    *out_y = view->view_h > 0 ? py * (float)view->frame_height / (float)view->view_h : 0.0f;
}

bool nmo_view_poll(nmo_view_t *view)
{
    SDL_Event event;
    bool open = true;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            open = false;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            uint32_t key = view_scan_code(event.key.scancode);
            if (key != 0 && !event.key.repeat) {
                nmo_engine_set_key(view->engine, key, event.type == SDL_EVENT_KEY_DOWN);
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            float x = 0.0f, y = 0.0f;
            view_to_frame(view, event.motion.x, event.motion.y, &x, &y);
            nmo_engine_set_mouse_position(view->engine, x, y);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int32_t button = event.button.button == SDL_BUTTON_LEFT    ? 0
                             : event.button.button == SDL_BUTTON_RIGHT ? 1
                             : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                         : -1;
            if (button >= 0) {
                nmo_engine_set_mouse_button(view->engine, button,
                                            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            nmo_engine_add_mouse_wheel(view->engine, event.wheel.y * 120.0f);
            break;
        default:
            break;
        }
    }
    bool cursor = nmo_engine_cursor_is_visible(view->engine);
    if (cursor != view->cursor_shown) {
        view->cursor_shown = cursor;
        if (cursor) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
    return open;
}

/* ============================================================================
 * Resources
 * ============================================================================ */

static GLenum view_address(int32_t mode)
{
    switch (mode) {
    case 2: return GL_MIRRORED_REPEAT;
    case 3: return GL_CLAMP_TO_EDGE;
    case 4: return GL_CLAMP_TO_BORDER;
    default: return GL_REPEAT;
    }
}

/* The GL texture of a CKTexture, uploaded again when its image changes; 0 for none */
static GLuint view_texture(nmo_view_t *view, nmo_object_id_t id)
{
    if (id == 0) {
        return 0;
    }
    view_texture_t *texture = NULL;
    for (size_t i = 0; i < view->texture_count; i++) {
        if (view->textures[i].id == id) {
            texture = &view->textures[i];
            break;
        }
    }
    nmo_engine_image_t image;
    bool has_image = nmo_engine_texture_image(view->engine, id, &image);
    if (texture == NULL) {
        if (view->texture_count == view->texture_capacity) {
            size_t capacity = view->texture_capacity != 0 ? view->texture_capacity * 2 : 64;
            view_texture_t *grown =
                (view_texture_t *)realloc(view->textures, capacity * sizeof(*grown));
            if (grown == NULL) {
                return 0;
            }
            view->textures = grown;
            view->texture_capacity = capacity;
        }
        texture = &view->textures[view->texture_count++];
        memset(texture, 0, sizeof(*texture));
        texture->id = id;
        glGenTextures(1, &texture->name);
        texture->version = has_image ? image.version - 1 : 0;
        texture->empty = true;
    }
    if (has_image && image.version != texture->version) {
        texture->version = image.version;
        texture->empty = false;
        glBindTexture(GL_TEXTURE_2D, texture->name);
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.rgba);
    }
    return texture->empty ? 0 : texture->name;
}

/* The prelit colors as RGBA bytes */
static void view_mesh_colors(view_mesh_t *cached, const nmo_engine_mesh_t *mesh)
{
    free(cached->colors);
    cached->colors = NULL;
    cached->version = mesh->version;
    if (mesh->colors == NULL || mesh->vertex_count <= 0) {
        return;
    }
    cached->colors = (uint8_t *)malloc((size_t)mesh->vertex_count * 4u);
    for (int32_t v = 0; cached->colors != NULL && v < mesh->vertex_count; v++) {
        uint32_t c = mesh->colors[v];
        cached->colors[v * 4 + 0] = (uint8_t)(c >> 16);
        cached->colors[v * 4 + 1] = (uint8_t)(c >> 8);
        cached->colors[v * 4 + 2] = (uint8_t)c;
        cached->colors[v * 4 + 3] = (uint8_t)(c >> 24);
    }
}

static view_mesh_t *view_mesh(nmo_view_t *view, nmo_object_id_t id, const nmo_engine_mesh_t *mesh)
{
    for (size_t i = 0; i < view->mesh_count; i++) {
        if (view->meshes[i].id == id) {
            if (view->meshes[i].version != mesh->version) {
                view_mesh_colors(&view->meshes[i], mesh);
            }
            return &view->meshes[i];
        }
    }
    if (view->mesh_count == view->mesh_capacity) {
        size_t capacity = view->mesh_capacity != 0 ? view->mesh_capacity * 2 : 64;
        view_mesh_t *grown = (view_mesh_t *)realloc(view->meshes, capacity * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        view->meshes = grown;
        view->mesh_capacity = capacity;
    }
    view_mesh_t *cached = &view->meshes[view->mesh_count++];
    memset(cached, 0, sizeof(*cached));
    cached->id = id;
    int32_t groups = mesh->material_count > 0 ? mesh->material_count : 1;
    cached->group_count = groups;
    cached->indices = (uint16_t **)calloc((size_t)groups, sizeof(uint16_t *));
    cached->counts = (int32_t *)calloc((size_t)groups, sizeof(int32_t));
    if (cached->indices == NULL || cached->counts == NULL) {
        return cached;
    }
    for (int32_t g = 0; g < groups; g++) {
        cached->indices[g] = (uint16_t *)malloc((size_t)mesh->face_count * 3u * sizeof(uint16_t));
    }
    for (int32_t f = 0; f < mesh->face_count; f++) {
        uint32_t g = mesh->faces != NULL ? mesh->faces[f].material_group_idx : 0;
        if (g >= (uint32_t)groups || cached->indices[g] == NULL) {
            g = 0;
        }
        if (cached->indices[g] == NULL) {
            continue;
        }
        for (int k = 0; k < 3; k++) {
            uint16_t index = mesh->indices[f * 3 + k];
            cached->indices[g][cached->counts[g]++] =
                index < (uint32_t)mesh->vertex_count ? index : 0;
        }
    }
    view_mesh_colors(cached, mesh);
    return cached;
}

/* ============================================================================
 * Render states
 * ============================================================================ */

static GLenum view_blend(int32_t factor)
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

static GLenum view_compare(int32_t func)
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

static GLint view_texture_env(int32_t blend)
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

static GLint view_filter(int32_t filter, bool minify)
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

/* The inverse of an affine row-vector matrix (rotation, scale and translation) */
static bool view_affine_inverse(float out[16], const float m[16])
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

/*
 * RCKMaterial::TexGenEffect: reflect and chrome take the camera space
 * reflection vector or normal, planar the camera space position, transform
 * the mesh's UVs. With a referential, reflect and chrome turn by the basis
 * looking from it to the object drawn (`world`), planar takes it inverted,
 * transform takes its matrix; the result is scaled by 0.4 (0.5 planar), V
 * flipped, about the texture's middle. The eye space here is the camera's,
 * so GL's generation is CK's, and the matrices are CK's row-vector ones.
 */
static void view_set_texgen(nmo_view_t *view, const nmo_engine_material_t *m, const float *world)
{
    int32_t texgen = m != NULL ? m->texgen : 0;
    GLint mode = texgen == 2 ? GL_REFLECTION_MAP : texgen == 3 ? GL_NORMAL_MAP
                 : texgen == 4 ? GL_EYE_LINEAR : 0;
    float ref[16];
    bool referential = m != NULL && m->texgen_referential != 0 &&
                       nmo_engine_entity_world_matrix(view->engine, m->texgen_referential, ref);
    float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (texgen == 1 && referential) {
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
            view_affine_inverse(t, ref);
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
static void view_set_material(nmo_view_t *view, const nmo_engine_material_t *m, bool lit,
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
    GLuint texture = view_texture(view, m->texture);
    if (texture != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, view_filter(m->min_filter, true));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, view_filter(m->mag_filter, false));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)view_address(m->address_mode));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)view_address(m->address_mode));
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, view_texture_env(m->texture_blend));
        view_set_texgen(view, m, world);
    } else {
        glDisable(GL_TEXTURE_2D);
        view_set_texgen(view, NULL, NULL);
    }
    if (m->alpha_blend) {
        glEnable(GL_BLEND);
        glBlendFunc(view_blend(m->source_blend), view_blend(m->dest_blend));
    } else {
        glDisable(GL_BLEND);
    }
    if (m->alpha_test) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(view_compare(m->alpha_func), (float)m->alpha_ref / 255.0f);
    } else {
        glDisable(GL_ALPHA_TEST);
    }
    if (depth) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(view_compare(m->z_func));
        glDepthMask(m->z_write ? GL_TRUE : GL_FALSE);
    }
    if (m->two_sided) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
    }
    glShadeModel(m->shade_mode == 1 ? GL_FLAT : GL_SMOOTH);
    glPolygonMode(GL_FRONT_AND_BACK, m->fill_mode == 1   ? GL_POINT
                                     : m->fill_mode == 2 ? GL_LINE
                                                         : GL_FILL);
}

static void view_color(uint32_t argb, float out[4])
{
    out[0] = (float)((argb >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((argb >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(argb & 0xFF) / 255.0f;
    out[3] = (float)(argb >> 24) / 255.0f;
}

/* Row-vector matrices: a then b */
static void view_multiply(float out[16], const float a[16], const float b[16])
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

/* The inverse of a rigid (scaled) world matrix: Vx3DInverseMatrix */
static void view_inverse(float out[16], const float m[16])
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

/* ============================================================================
 * Drawing
 * ============================================================================ */

/* VxMatrix::Perspective (or Orthographic), then Direct3D's depth 0..w to OpenGL's -w..w */
static void view_load_projection(const nmo_engine_frame_t *frame, bool orthographic, float fov,
                                 float near_plane, float far_plane)
{
    float aspect = (float)frame->width / (float)frame->height;
    float projection[16] = {0};
    float depth = far_plane - near_plane;
    if (depth <= 0.0f) {
        depth = 1.0f;
    }
    if (!orthographic) {
        double half = (double)fov * 0.5;
        float scale = (float)(cos(half) / sin(half));
        projection[0] = scale;
        projection[5] = scale * aspect;
        projection[10] = far_plane / depth;
        projection[14] = -near_plane * far_plane / depth;
        projection[11] = 1.0f;
    } else {
        projection[0] = frame->zoom;
        projection[5] = frame->zoom * aspect;
        projection[10] = 1.0f / depth;
        projection[14] = -near_plane / depth;
        projection[15] = 1.0f;
    }
    const float remap[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, -1, 1};
    view_multiply(projection, projection, remap);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection);
}

static void view_draw_mesh(nmo_view_t *view, const nmo_engine_frame_t *frame,
                           const nmo_engine_draw_t *draw, const float view_m[16])
{
    nmo_engine_mesh_t mesh;
    if (!nmo_engine_mesh(view->engine, draw->mesh, &mesh) || mesh.vertices == NULL ||
        mesh.indices == NULL) {
        return;
    }
    view_mesh_t *cached = view_mesh(view, draw->mesh, &mesh);
    if (cached == NULL || cached->indices == NULL) {
        return;
    }
    if (draw->fov > 0.0f) {
        view_load_projection(frame, false, draw->fov, draw->near_plane, draw->far_plane);
    }
    float model_view[16];
    view_multiply(model_view, draw->world, view_m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(model_view);

    const GLsizei stride = (GLsizei)sizeof(nmo_vertex_t);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, &mesh.vertices[0].position);
    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, stride, &mesh.vertices[0].normal);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, stride, &mesh.vertices[0].uv);
    bool prelit = mesh.prelit && cached->colors != NULL;
    if (prelit) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, cached->colors);
    }
    for (int32_t g = 0; g < cached->group_count; g++) {
        if (cached->counts[g] == 0) {
            continue;
        }
        nmo_engine_material_t material;
        nmo_object_id_t material_id = nmo_engine_mesh_material(view->engine, draw->mesh, g);
        if (material_id == 0 || !nmo_engine_material(view->engine, material_id, &material)) {
            memset(&material, 0, sizeof(material));
            material.diffuse[0] = material.diffuse[1] = material.diffuse[2] = 0.7f;
            material.diffuse[3] = 1.0f;
            material.z_write = true;
            material.z_func = 4;
            material.source_blend = 2;
            material.dest_blend = 1;
        }
        if (prelit) {
            glDisable(GL_LIGHTING);
        } else {
            glEnable(GL_LIGHTING);
            glColor4fv(material.diffuse);
        }
        view_set_material(view, &material, !prelit, true, draw->world);
        if (draw->no_z_test) {
            glDisable(GL_DEPTH_TEST);
        }
        if (draw->no_z_write) {
            glDepthMask(GL_FALSE);
        }
        glDrawElements(GL_TRIANGLES, cached->counts[g], GL_UNSIGNED_SHORT, cached->indices[g]);
    }
    /* The material channels, each over the faces it covers */
    for (int32_t c = 0; c < mesh.channel_count; c++) {
        const nmo_engine_channel_t *channel = &mesh.channels[c];
        nmo_engine_material_t material;
        if (!channel->active || !nmo_engine_material(view->engine, channel->material, &material)) {
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
        if (lit) {
            glEnable(GL_LIGHTING);
        } else {
            glDisable(GL_LIGHTING);
            glColor4fv(material.diffuse);
        }
        material.alpha_blend = true;
        material.source_blend = channel->source_blend;
        material.dest_blend = channel->dest_blend;
        material.z_write = false;
        material.z_func = 4;
        view_set_material(view, &material, lit, true, draw->world);
        if (channel->uvs != NULL) {
            glTexCoordPointer(2, GL_FLOAT, 0, channel->uvs);
        } else {
            glTexCoordPointer(2, GL_FLOAT, stride, &mesh.vertices[0].uv);
        }
        if (count > 0) {
            glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, indices);
        }
        free(indices);
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (draw->fov > 0.0f) {
        view_load_projection(frame, frame->orthographic, frame->fov, frame->near_plane,
                             frame->far_plane);
    }
}

/* RCK2dEntity::Draw: a quad of the material's diffuse, textured with its source rect */
static void view_draw_quad(nmo_view_t *view, const nmo_engine_draw_t *draw)
{
    nmo_engine_material_t material;
    if (draw->material != 0 && nmo_engine_material(view->engine, draw->material, &material)) {
        view_set_material(view, &material, false, false, NULL);
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
        view_set_material(view, &material, false, false, NULL);
    }
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    float top[4], bottom[4];
    view_color(draw->color, top);
    view_color(draw->color2, bottom);
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
}

/* A sprite text: its sprite's pixels, blended over the frame without Z */
static void view_draw_sprite_text(nmo_view_t *view, const nmo_engine_draw_t *draw)
{
    GLuint name = view_text_texture(view->text, draw);
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
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    view_set_texgen(view, NULL, NULL);
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
static void view_draw_triangles(nmo_view_t *view, const nmo_engine_frame_t *frame,
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
    view_set_material(view, &material, false, true, NULL);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_m);
    const nmo_engine_vertex_t *v = &frame->vertices[draw->first_vertex];
    glBegin(lines ? GL_LINES : GL_TRIANGLES);
    for (int32_t i = 0; i < draw->vertex_count; i++) {
        float color[4];
        view_color(v[i].color, color);
        glColor4fv(color);
        glTexCoord2f(v[i].u, v[i].v);
        glVertex3f(v[i].x, v[i].y, v[i].z);
    }
    glEnd();
}

/* A 3D sprite: its quad with its material set as current, unlit (CallSprite3DBatches) */
static void view_draw_sprite3d(nmo_view_t *view, const nmo_engine_frame_t *frame,
                               const nmo_engine_draw_t *draw, const float view_m[16])
{
    nmo_engine_material_t material;
    if (draw->vertex_count <= 0 || frame->vertices == NULL ||
        !nmo_engine_material(view->engine, draw->material, &material)) {
        return;
    }
    glDisable(GL_LIGHTING);
    view_set_material(view, &material, false, true, NULL);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_m);
    const nmo_engine_vertex_t *v = &frame->vertices[draw->first_vertex];
    glBegin(GL_TRIANGLES);
    for (int32_t i = 0; i < draw->vertex_count; i++) {
        float color[4];
        view_color(v[i].color, color);
        glColor4fv(color);
        glTexCoord2f(v[i].u, v[i].v);
        glVertex3f(v[i].x, v[i].y, v[i].z);
    }
    glEnd();
}

static void view_begin_2d(const nmo_engine_frame_t *frame)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, frame->width, frame->height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/* The 3D pass: CKRenderedScene's camera, default states, fog and lights */
static void view_begin_3d(const nmo_engine_frame_t *frame, float out_view[16])
{
    view_load_projection(frame, frame->orthographic, frame->fov, frame->near_plane,
                         frame->far_plane);
    view_inverse(out_view, frame->view);

    glFrontFace(GL_CW);
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
    view_color(frame->ambient_light, ambient);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

    if (frame->fog_mode != 0) {
        float color[4];
        view_color(frame->fog_color, color);
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

/* The topmost drawn 2D entity under the mouse, for the engine's picks */
static void view_pick(nmo_view_t *view, const nmo_engine_frame_t *frame)
{
    float x = 0.0f, y = 0.0f;
    nmo_engine_mouse_position(view->engine, &x, &y);
    nmo_object_id_t under = 0;
    for (int32_t i = frame->draw_count - 1; i >= 0 && under == 0; i--) {
        const nmo_engine_draw_t *draw = &frame->draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_QUAD && draw->material != 0 && x >= draw->rect[0] &&
            x <= draw->rect[2] && y >= draw->rect[1] && y <= draw->rect[3]) {
            under = draw->object;
        }
    }
    nmo_engine_set_entity_under_mouse(view->engine, under);
}

/* Draw the engine's current frame into the back buffer. */
static nmo_status_t view_render(nmo_view_t *view, nmo_engine_frame_t *out_frame)
{
    nmo_engine_frame_t frame;
    nmo_status_t status = nmo_engine_render_frame(view->engine, &frame);
    if (status != NMO_OK) {
        return status;
    }
    view->frame_width = frame.width;
    view->frame_height = frame.height;
    int window_w = 0, window_h = 0;
    SDL_GetWindowSizeInPixels(view->window, &window_w, &window_h);
    /* The frame keeps its aspect, centered in the window */
    float scale = fminf((float)window_w / (float)frame.width, (float)window_h / (float)frame.height);
    view->view_w = (int)((float)frame.width * scale);
    view->view_h = (int)((float)frame.height * scale);
    view->view_x = (window_w - view->view_w) / 2;
    view->view_y = (window_h - view->view_h) / 2;

    glViewport(0, 0, window_w, window_h);
    glDepthMask(GL_TRUE); /* a clear writes only what the masks let through */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(view->view_x, window_h - view->view_y - view->view_h, view->view_w, view->view_h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(view->view_x, window_h - view->view_y - view->view_h, view->view_w, view->view_h);
    float background[4];
    view_color(frame.background_color, background);
    glClearColor(background[0], background[1], background[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    if (frame.background_texture != 0) {
        nmo_engine_draw_t quad;
        memset(&quad, 0, sizeof(quad));
        quad.rect[2] = (float)frame.width;
        quad.rect[3] = (float)frame.height;
        quad.uv[2] = quad.uv[3] = 1.0f;
        quad.texture = frame.background_texture;
        quad.color = quad.color2 = frame.background_color | 0xFF000000u;
        view_begin_2d(&frame);
        view_draw_quad(view, &quad);
    }

    /* Background 2D entities, then the opaque meshes, the transparent ones back
       to front, and the foreground 2D entities */
    int32_t first_mesh = 0;
    while (first_mesh < frame.draw_count && frame.draws[first_mesh].kind == NMO_ENGINE_DRAW_QUAD &&
           frame.draws[first_mesh].background) {
        first_mesh++;
    }
    view_begin_2d(&frame);
    for (int32_t i = 0; i < first_mesh; i++) {
        view_draw_quad(view, &frame.draws[i]);
    }
    int32_t end_mesh = first_mesh;
    while (end_mesh < frame.draw_count && frame.draws[end_mesh].kind != NMO_ENGINE_DRAW_QUAD &&
           frame.draws[end_mesh].kind != NMO_ENGINE_DRAW_SPRITE_TEXT) {
        end_mesh++;
    }
    float view_m[16];
    view_begin_3d(&frame, view_m);
    int32_t *transparent = (int32_t *)malloc((size_t)(end_mesh - first_mesh + 1) * sizeof(int32_t));
    float *depths = (float *)malloc((size_t)(end_mesh - first_mesh + 1) * sizeof(float));
    int32_t transparent_count = 0;
    for (int32_t i = first_mesh; i < end_mesh; i++) {
        const nmo_engine_draw_t *draw = &frame.draws[i];
        if (draw->transparent && transparent != NULL && depths != NULL) {
            const float *p = &draw->world[12];
            depths[transparent_count] = p[0] * view_m[2] + p[1] * view_m[6] + p[2] * view_m[10] + view_m[14];
            transparent[transparent_count++] = i;
            continue;
        }
        if (draw->kind == NMO_ENGINE_DRAW_SPRITE3D) {
            view_draw_sprite3d(view, &frame, draw, view_m);
            continue;
        }
        view_draw_mesh(view, &frame, draw, view_m);
    }
    for (int32_t i = 1; i < transparent_count; i++) {
        for (int32_t j = i; j > 0 && depths[j - 1] < depths[j]; j--) {
            float d = depths[j];
            depths[j] = depths[j - 1];
            depths[j - 1] = d;
            int32_t t = transparent[j];
            transparent[j] = transparent[j - 1];
            transparent[j - 1] = t;
        }
    }
    for (int32_t i = 0; i < transparent_count; i++) {
        const nmo_engine_draw_t *draw = &frame.draws[transparent[i]];
        if (draw->kind == NMO_ENGINE_DRAW_TRIANGLES || draw->kind == NMO_ENGINE_DRAW_LINES) {
            view_draw_triangles(view, &frame, draw, view_m);
        } else if (draw->kind == NMO_ENGINE_DRAW_SPRITE3D) {
            view_draw_sprite3d(view, &frame, draw, view_m);
        } else {
            view_draw_mesh(view, &frame, draw, view_m);
        }
    }
    free(transparent);
    free(depths);
    glDisable(GL_LIGHTING);
    view_begin_2d(&frame);
    for (int32_t i = end_mesh; i < frame.draw_count; i++) {
        if (frame.draws[i].kind == NMO_ENGINE_DRAW_SPRITE_TEXT) {
            view_draw_sprite_text(view, &frame.draws[i]);
        } else {
            view_draw_quad(view, &frame.draws[i]);
        }
    }
    view_text_end_frame(view->text);
    *out_frame = frame;
    return NMO_OK;
}

nmo_status_t nmo_view_draw(nmo_view_t *view)
{
    if (view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_engine_frame_t frame;
    nmo_status_t status = view_render(view, &frame);
    if (status != NMO_OK) {
        return status;
    }
    view_pick(view, &frame);
    SDL_GL_SwapWindow(view->window);
    return NMO_OK;
}

nmo_status_t nmo_view_screenshot(nmo_view_t *view, const char *path)
{
    if (view == NULL || path == NULL || view->view_w <= 0 || view->view_h <= 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    /* The front buffer is undefined once swapped (macOS composites the
       window), so draw the frame again and read it before any swap. */
    nmo_engine_frame_t frame;
    nmo_status_t render_status = view_render(view, &frame);
    if (render_status != NMO_OK) {
        return render_status;
    }
    int window_w = 0, window_h = 0;
    SDL_GetWindowSizeInPixels(view->window, &window_w, &window_h);
    size_t row = (size_t)view->view_w * 4u;
    uint8_t *pixels = (uint8_t *)malloc(row * (size_t)view->view_h);
    uint8_t *flipped = (uint8_t *)malloc(row * (size_t)view->view_h);
    nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 20);
    nmo_status_t status = NMO_ERR_NOMEM;
    if (pixels != NULL && flipped != NULL && arena != NULL) {
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(view->view_x, window_h - view->view_y - view->view_h, view->view_w,
                     view->view_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (int y = 0; y < view->view_h; y++) {
            memcpy(flipped + (size_t)y * row, pixels + (size_t)(view->view_h - 1 - y) * row, row);
        }
        size_t size = 0;
        uint8_t *png = nmo_stbi_write_to_memory(arena, NMO_BITMAP_FORMAT_PNG, view->view_w,
                                                view->view_h, 4, flipped, 0, &size);
        FILE *file = png != NULL ? fopen(path, "wb") : NULL;
        status = file != NULL && fwrite(png, 1, size, file) == size ? NMO_OK : NMO_ERR_CANT_WRITE_FILE;
        if (file != NULL) {
            fclose(file);
        }
    }
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    free(pixels);
    free(flipped);
    return status;
}

void nmo_view_play_sounds(nmo_view_t *view)
{
    if (view != NULL) {
        view_audio_play(view->audio, view->engine);
    }
}

void nmo_view_wait(const nmo_view_t *view, double ms)
{
    if (view != NULL && ms > 0.0) {
        SDL_DelayPrecise((Uint64)(ms * 1.0e6));
    }
}

double nmo_view_time(const nmo_view_t *view)
{
    return view != NULL ? (double)(SDL_GetTicksNS() - view->start) / 1.0e6 : 0.0;
}

nmo_status_t nmo_view_open(nmo_engine_t *engine, const char *title, int scale,
                           nmo_view_t **out_view)
{
    if (engine == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_view = NULL;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error: SDL_Init failed (%s)\n", SDL_GetError());
        return NMO_ERR_INVALID_STATE;
    }
    nmo_view_t *view = (nmo_view_t *)calloc(1, sizeof(*view));
    if (view == NULL) {
        SDL_Quit();
        return NMO_ERR_NOMEM;
    }
    view->engine = engine;
    view->frame_width = 640;
    view->frame_height = 480;
    if (scale < 1) {
        scale = 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    view->window = SDL_CreateWindow(title != NULL ? title : "nmo", 640 * scale, 480 * scale,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                        SDL_WINDOW_HIGH_PIXEL_DENSITY);
    view->context = view->window != NULL ? SDL_GL_CreateContext(view->window) : NULL;
    if (view->context == NULL) {
        fprintf(stderr, "Error: Could not open an OpenGL window (%s)\n", SDL_GetError());
        nmo_view_close(view);
        return NMO_ERR_INVALID_STATE;
    }
    SDL_GL_SetSwapInterval(1);
    view->audio = view_audio_open();
    view->text = view_text_open();
    view->start = SDL_GetTicksNS();
    view->cursor_shown = true;
    *out_view = view;
    return NMO_OK;
}

void nmo_view_close(nmo_view_t *view)
{
    if (view == NULL) {
        return;
    }
    for (size_t i = 0; i < view->texture_count; i++) {
        glDeleteTextures(1, &view->textures[i].name);
    }
    free(view->textures);
    for (size_t i = 0; i < view->mesh_count; i++) {
        for (int32_t g = 0; view->meshes[i].indices != NULL && g < view->meshes[i].group_count; g++) {
            free(view->meshes[i].indices[g]);
        }
        free(view->meshes[i].indices);
        free(view->meshes[i].counts);
        free(view->meshes[i].colors);
    }
    free(view->meshes);
    view_audio_close(view->audio);
    view_text_close(view->text);
    if (view->context != NULL) {
        SDL_GL_DestroyContext(view->context);
    }
    if (view->window != NULL) {
        SDL_DestroyWindow(view->window);
    }
    free(view);
    SDL_Quit();
}
