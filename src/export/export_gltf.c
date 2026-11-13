/**
 * @file export_gltf.c
 * @brief The 3D scene of a file as glTF 2.0 binary (.glb)
 */

#include "export/nmo_export_gltf.h"
#include "export_json_util_internal.h"
#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include "format/nmo_stb_adapter.h"
#include "io/nmo_txn.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "yyjson.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* glTF constants */
#define GLTF_FLOAT 5126
#define GLTF_UNSIGNED_BYTE 5121
#define GLTF_UNSIGNED_SHORT 5123
#define GLTF_ARRAY_BUFFER 34962
#define GLTF_ELEMENT_ARRAY_BUFFER 34963
#define GLTF_NEAREST 9728
#define GLTF_LINEAR 9729
#define GLTF_NEAREST_MIPMAP_NEAREST 9984
#define GLTF_LINEAR_MIPMAP_NEAREST 9985
#define GLTF_NEAREST_MIPMAP_LINEAR 9986
#define GLTF_LINEAR_MIPMAP_LINEAR 9987
#define GLTF_CLAMP_TO_EDGE 33071
#define GLTF_MIRRORED_REPEAT 33648
#define GLTF_REPEAT 10497

#define GLB_MAGIC 0x46546C67u      /* "glTF" */
#define GLB_CHUNK_JSON 0x4E4F534Au /* "JSON" */
#define GLB_CHUNK_BIN 0x004E4942u  /* "BIN\0" */

/* An object id and the glTF index made for it */
typedef struct id_index {
    nmo_object_id_t id;
    int32_t index;
} id_index_t;

typedef struct id_map {
    id_index_t *items;
    size_t count;
    size_t capacity;
} id_map_t;

/* A sampler and the image of a texture, by value */
typedef struct sampler_key {
    int wrap;
    int min_filter;
    int mag_filter;
} sampler_key_t;

typedef struct texture_key {
    int32_t image;
    int32_t sampler;
} texture_key_t;

typedef struct entity {
    nmo_object_t *object;
    const nmo_3dentity_state_t *state;
    int32_t parent;  /* node index, -1 for a root */
    float local[16]; /* relative to the parent, right-handed */
} entity_t;

typedef struct gltf_ctx {
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *types;
    nmo_gltf_export_options_t options;
    nmo_arena_t *arena;
    yyjson_mut_doc *doc;
    yyjson_mut_val *meshes;
    yyjson_mut_val *materials;
    yyjson_mut_val *textures;
    yyjson_mut_val *samplers;
    yyjson_mut_val *images;
    yyjson_mut_val *accessors;
    yyjson_mut_val *buffer_views;
    uint8_t *bin;
    size_t bin_size;
    size_t bin_capacity;
    id_map_t mesh_map;
    id_map_t material_map;
    id_map_t image_map;
    sampler_key_t *sampler_keys;
    size_t sampler_count;
    size_t sampler_capacity;
    texture_key_t *texture_keys;
    size_t texture_count;
    size_t texture_capacity;
    nmo_gltf_export_stats_t stats;
    nmo_status_t error;
} gltf_ctx_t;

/* ---------------------------------------------------------------------------
 * Small containers
 * --------------------------------------------------------------------------- */

static bool grow(void **items, size_t *capacity, size_t count, size_t item_size)
{
    if (count < *capacity) return true;
    size_t next = *capacity ? *capacity * 2u : 16u;
    if (next > SIZE_MAX / item_size) return false;
    void *grown = realloc(*items, next * item_size);
    if (grown == NULL) return false;
    *items = grown;
    *capacity = next;
    return true;
}

static int32_t id_map_get(const id_map_t *map, nmo_object_id_t id)
{
    for (size_t i = 0; i < map->count; ++i) {
        if (map->items[i].id == id) return map->items[i].index;
    }
    return INT32_MIN;
}

static bool id_map_put(id_map_t *map, nmo_object_id_t id, int32_t index)
{
    if (!grow((void **)&map->items, &map->capacity, map->count, sizeof(id_index_t))) return false;
    map->items[map->count++] = (id_index_t){id, index};
    return true;
}

static void fail(gltf_ctx_t *ctx, nmo_status_t status)
{
    if (ctx->error == NMO_OK) ctx->error = status;
}

/* ---------------------------------------------------------------------------
 * Binary buffer
 * --------------------------------------------------------------------------- */

/* Appends data at a 4-byte boundary as a buffer view; returns its index. */
static int32_t add_buffer_view(gltf_ctx_t *ctx, const void *data, size_t size, int target)
{
    const size_t offset = (ctx->bin_size + 3u) & ~(size_t)3u;
    if (offset < ctx->bin_size || size > SIZE_MAX - offset || offset + size > UINT32_MAX) {
        fail(ctx, NMO_ERR_INVALID_ARGUMENT);
        return -1;
    }
    if (offset + size > ctx->bin_capacity) {
        size_t capacity = ctx->bin_capacity ? ctx->bin_capacity : 65536u;
        while (capacity < offset + size) {
            if (capacity > SIZE_MAX / 2u) {
                fail(ctx, NMO_ERR_NOMEM);
                return -1;
            }
            capacity *= 2u;
        }
        uint8_t *grown = (uint8_t *)realloc(ctx->bin, capacity);
        if (grown == NULL) {
            fail(ctx, NMO_ERR_NOMEM);
            return -1;
        }
        ctx->bin = grown;
        ctx->bin_capacity = capacity;
    }
    memset(ctx->bin + ctx->bin_size, 0, offset - ctx->bin_size);
    memcpy(ctx->bin + offset, data, size);
    ctx->bin_size = offset + size;

    yyjson_mut_val *view = yyjson_mut_obj(ctx->doc);
    if (view == NULL || !yyjson_mut_arr_append(ctx->buffer_views, view)) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    yyjson_mut_obj_add_uint(ctx->doc, view, "buffer", 0);
    yyjson_mut_obj_add_uint(ctx->doc, view, "byteOffset", offset);
    yyjson_mut_obj_add_uint(ctx->doc, view, "byteLength", size);
    if (target != 0) yyjson_mut_obj_add_int(ctx->doc, view, "target", target);
    return (int32_t)yyjson_mut_arr_size(ctx->buffer_views) - 1;
}

static int32_t add_accessor(gltf_ctx_t *ctx, const void *data, size_t size, int target,
                            int component_type, bool normalized, size_t count, const char *type,
                            const float *min, const float *max, size_t components)
{
    const int32_t view = add_buffer_view(ctx, data, size, target);
    if (view < 0) return -1;
    yyjson_mut_val *accessor = yyjson_mut_obj(ctx->doc);
    if (accessor == NULL || !yyjson_mut_arr_append(ctx->accessors, accessor)) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    yyjson_mut_obj_add_int(ctx->doc, accessor, "bufferView", view);
    yyjson_mut_obj_add_int(ctx->doc, accessor, "componentType", component_type);
    if (normalized) yyjson_mut_obj_add_bool(ctx->doc, accessor, "normalized", true);
    yyjson_mut_obj_add_uint(ctx->doc, accessor, "count", count);
    yyjson_mut_obj_add_str(ctx->doc, accessor, "type", type);
    if (min != NULL && max != NULL) {
        yyjson_mut_val *lo = yyjson_mut_obj_add_arr(ctx->doc, accessor, "min");
        yyjson_mut_val *hi = yyjson_mut_obj_add_arr(ctx->doc, accessor, "max");
        for (size_t i = 0; lo != NULL && hi != NULL && i < components; ++i) {
            yyjson_mut_arr_add_real(ctx->doc, lo, min[i]);
            yyjson_mut_arr_add_real(ctx->doc, hi, max[i]);
        }
    }
    return (int32_t)yyjson_mut_arr_size(ctx->accessors) - 1;
}

/* ---------------------------------------------------------------------------
 * Matrices: column-major, column vectors. A VxMatrix (row vectors, rows the
 * axes then the position) has the same 16 floats.
 * --------------------------------------------------------------------------- */

static void mat4_multiply(const float a[16], const float b[16], float out[16])
{
    float r[16];
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

static bool mat4_inverse(const float m[16], float out[16])
{
    double inv[16];
    inv[0] = (double)m[5] * m[10] * m[15] - (double)m[5] * m[11] * m[14] - (double)m[9] * m[6] * m[15] +
             (double)m[9] * m[7] * m[14] + (double)m[13] * m[6] * m[11] - (double)m[13] * m[7] * m[10];
    inv[4] = -(double)m[4] * m[10] * m[15] + (double)m[4] * m[11] * m[14] + (double)m[8] * m[6] * m[15] -
             (double)m[8] * m[7] * m[14] - (double)m[12] * m[6] * m[11] + (double)m[12] * m[7] * m[10];
    inv[8] = (double)m[4] * m[9] * m[15] - (double)m[4] * m[11] * m[13] - (double)m[8] * m[5] * m[15] +
             (double)m[8] * m[7] * m[13] + (double)m[12] * m[5] * m[11] - (double)m[12] * m[7] * m[9];
    inv[12] = -(double)m[4] * m[9] * m[14] + (double)m[4] * m[10] * m[13] + (double)m[8] * m[5] * m[14] -
              (double)m[8] * m[6] * m[13] - (double)m[12] * m[5] * m[10] + (double)m[12] * m[6] * m[9];
    inv[1] = -(double)m[1] * m[10] * m[15] + (double)m[1] * m[11] * m[14] + (double)m[9] * m[2] * m[15] -
             (double)m[9] * m[3] * m[14] - (double)m[13] * m[2] * m[11] + (double)m[13] * m[3] * m[10];
    inv[5] = (double)m[0] * m[10] * m[15] - (double)m[0] * m[11] * m[14] - (double)m[8] * m[2] * m[15] +
             (double)m[8] * m[3] * m[14] + (double)m[12] * m[2] * m[11] - (double)m[12] * m[3] * m[10];
    inv[9] = -(double)m[0] * m[9] * m[15] + (double)m[0] * m[11] * m[13] + (double)m[8] * m[1] * m[15] -
             (double)m[8] * m[3] * m[13] - (double)m[12] * m[1] * m[11] + (double)m[12] * m[3] * m[9];
    inv[13] = (double)m[0] * m[9] * m[14] - (double)m[0] * m[10] * m[13] - (double)m[8] * m[1] * m[14] +
              (double)m[8] * m[2] * m[13] + (double)m[12] * m[1] * m[10] - (double)m[12] * m[2] * m[9];
    inv[2] = (double)m[1] * m[6] * m[15] - (double)m[1] * m[7] * m[14] - (double)m[5] * m[2] * m[15] +
             (double)m[5] * m[3] * m[14] + (double)m[13] * m[2] * m[7] - (double)m[13] * m[3] * m[6];
    inv[6] = -(double)m[0] * m[6] * m[15] + (double)m[0] * m[7] * m[14] + (double)m[4] * m[2] * m[15] -
             (double)m[4] * m[3] * m[14] - (double)m[12] * m[2] * m[7] + (double)m[12] * m[3] * m[6];
    inv[10] = (double)m[0] * m[5] * m[15] - (double)m[0] * m[7] * m[13] - (double)m[4] * m[1] * m[15] +
              (double)m[4] * m[3] * m[13] + (double)m[12] * m[1] * m[7] - (double)m[12] * m[3] * m[5];
    inv[14] = -(double)m[0] * m[5] * m[14] + (double)m[0] * m[6] * m[13] + (double)m[4] * m[1] * m[14] -
              (double)m[4] * m[2] * m[13] - (double)m[12] * m[1] * m[6] + (double)m[12] * m[2] * m[5];
    inv[3] = -(double)m[1] * m[6] * m[11] + (double)m[1] * m[7] * m[10] + (double)m[5] * m[2] * m[11] -
             (double)m[5] * m[3] * m[10] - (double)m[9] * m[2] * m[7] + (double)m[9] * m[3] * m[6];
    inv[7] = (double)m[0] * m[6] * m[11] - (double)m[0] * m[7] * m[10] - (double)m[4] * m[2] * m[11] +
             (double)m[4] * m[3] * m[10] + (double)m[8] * m[2] * m[7] - (double)m[8] * m[3] * m[6];
    inv[11] = -(double)m[0] * m[5] * m[11] + (double)m[0] * m[7] * m[9] + (double)m[4] * m[1] * m[11] -
              (double)m[4] * m[3] * m[9] - (double)m[8] * m[1] * m[7] + (double)m[8] * m[3] * m[5];
    inv[15] = (double)m[0] * m[5] * m[10] - (double)m[0] * m[6] * m[9] - (double)m[4] * m[1] * m[10] +
              (double)m[4] * m[2] * m[9] + (double)m[8] * m[1] * m[6] - (double)m[8] * m[2] * m[5];
    const double det = (double)m[0] * inv[0] + (double)m[1] * inv[4] +
                       (double)m[2] * inv[8] + (double)m[3] * inv[12];
    if (!isfinite(det) || fabs(det) < 1e-12) return false;
    for (int i = 0; i < 16; ++i) out[i] = (float)(inv[i] / det);
    return true;
}

/* From left-handed to right-handed: S·M·S with S = diag(1, 1, -1, 1) */
static void mat4_flip_z(float m[16])
{
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            if ((row == 2) != (c == 2)) m[c * 4 + row] = -m[c * 4 + row];
        }
    }
}

static bool mat4_is_identity(const float m[16])
{
    for (int i = 0; i < 16; ++i) {
        const float expected = (i % 5 == 0) ? 1.0f : 0.0f;
        if (fabsf(m[i] - expected) > 1e-6f) return false;
    }
    return true;
}

static bool mat4_is_finite(const float m[16])
{
    for (int i = 0; i < 16; ++i) {
        if (!isfinite(m[i])) return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Names and colours
 * --------------------------------------------------------------------------- */

static void add_name(gltf_ctx_t *ctx, yyjson_mut_val *obj, const nmo_object_t *object)
{
    const char *name = nmo_object_get_name(object);
    if (name != NULL && name[0] != '\0') {
        nmo_json_add_str_safe(ctx->doc, obj, "name", name);
    }
}

/* Packed ARGB to linear-free factors in [0, 1], RGBA order */
static void argb_floats(uint32_t argb, float out[4])
{
    out[0] = (float)((argb >> 16) & 0xFFu) / 255.0f;
    out[1] = (float)((argb >> 8) & 0xFFu) / 255.0f;
    out[2] = (float)(argb & 0xFFu) / 255.0f;
    out[3] = (float)((argb >> 24) & 0xFFu) / 255.0f;
}

/* A file name as a relative URI: its last component, percent-encoded */
static char *file_uri(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') base = p + 1;
    }
    const size_t length = strlen(base);
    if (length == 0 || length > (SIZE_MAX - 1u) / 3u) return NULL;
    char *uri = (char *)malloc(length * 3u + 1u);
    if (uri == NULL) return NULL;
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char ch = (unsigned char)base[i];
        const bool plain = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (plain) {
            uri[n++] = (char)ch;
        } else {
            uri[n++] = '%';
            uri[n++] = hex[ch >> 4];
            uri[n++] = hex[ch & 0xFu];
        }
    }
    uri[n] = '\0';
    return uri;
}

/* ---------------------------------------------------------------------------
 * Images, samplers, textures
 * --------------------------------------------------------------------------- */

static void apply_color_key(const nmo_texture_state_t *state, nmo_texture_image_t *image)
{
    const uint32_t key = state->transparent_color;
    const uint8_t r = (uint8_t)(key >> 16), g = (uint8_t)(key >> 8), b = (uint8_t)key;
    const size_t count = (size_t)image->width * (size_t)image->height;
    for (size_t i = 0; i < count; ++i) {
        uint8_t *p = &image->rgba[i * 4u];
        if (p[0] == r && p[1] == g && p[2] == b) p[3] = 0;
    }
}

/* The image of a CKTexture; -1 when it has neither pixels nor a file name */
static int32_t export_image(gltf_ctx_t *ctx, nmo_object_id_t texture_id)
{
    const int32_t known = id_map_get(&ctx->image_map, texture_id);
    if (known != INT32_MIN) return known;

    int32_t index = -1;
    nmo_object_t *object = nmo_object_repository_find_by_id(ctx->repo, texture_id);
    const nmo_texture_state_t *state =
        object != NULL && nmo_object_get_class_id(object) == NMO_CID_TEXTURE
            ? (const nmo_texture_state_t *)nmo_object_get_state(object)
            : NULL;
    if (state != NULL && state->slot_count > 0) {
        const uint32_t slot = state->has_current_slot && state->current_slot >= 0 &&
                                      (uint32_t)state->current_slot < state->slot_count
                                  ? (uint32_t)state->current_slot
                                  : 0u;
        nmo_texture_image_t image = {0};
        size_t png_size = 0;
        uint8_t *png = NULL;
        if (ctx->options.embed_textures &&
            nmo_texture_decode_slot(state, slot, ctx->arena, &image, NULL) == NMO_OK) {
            if (ctx->options.apply_color_key && state->is_transparent) {
                apply_color_key(state, &image);
            }
            png = nmo_stbi_write_to_memory(ctx->arena, NMO_BITMAP_FORMAT_PNG, image.width,
                                           image.height, 4, image.rgba, 0, &png_size);
        }
        const char *file = state->slot_filenames != NULL ? state->slot_filenames[slot] : NULL;
        char *uri = png == NULL && file != NULL && file[0] != '\0' ? file_uri(file) : NULL;
        if (png != NULL || uri != NULL) {
            yyjson_mut_val *json = yyjson_mut_obj(ctx->doc);
            if (json == NULL || !yyjson_mut_arr_append(ctx->images, json)) {
                free(uri);
                fail(ctx, NMO_ERR_NOMEM);
                return -1;
            }
            add_name(ctx, json, object);
            if (png != NULL) {
                const int32_t view = add_buffer_view(ctx, png, png_size, 0);
                yyjson_mut_obj_add_str(ctx->doc, json, "mimeType", "image/png");
                yyjson_mut_obj_add_int(ctx->doc, json, "bufferView", view);
                ctx->stats.images_embedded++;
            } else {
                yyjson_mut_obj_add_strcpy(ctx->doc, json, "uri", uri);
                ctx->stats.images_external++;
            }
            free(uri);
            index = (int32_t)yyjson_mut_arr_size(ctx->images) - 1;
        }
    }
    if (index < 0) ctx->stats.images_missing++;
    if (!id_map_put(&ctx->image_map, texture_id, index)) fail(ctx, NMO_ERR_NOMEM);
    return index;
}

static int gltf_wrap(uint32_t address_mode)
{
    switch (address_mode) {
    case VXTEXTURE_ADDRESSMIRROR: return GLTF_MIRRORED_REPEAT;
    case VXTEXTURE_ADDRESSCLAMP:
    case VXTEXTURE_ADDRESSBORDER: return GLTF_CLAMP_TO_EDGE;
    default: return GLTF_REPEAT;
    }
}

static int gltf_min_filter(uint32_t filter)
{
    switch (filter) {
    case VXTEXTUREFILTER_NEAREST: return GLTF_NEAREST;
    case VXTEXTUREFILTER_LINEAR: return GLTF_LINEAR;
    case VXTEXTUREFILTER_MIPNEAREST: return GLTF_NEAREST_MIPMAP_NEAREST;
    case VXTEXTUREFILTER_MIPLINEAR: return GLTF_NEAREST_MIPMAP_LINEAR;
    case VXTEXTUREFILTER_LINEARMIPNEAREST: return GLTF_LINEAR_MIPMAP_NEAREST;
    default: return GLTF_LINEAR_MIPMAP_LINEAR;
    }
}

static int32_t export_sampler(gltf_ctx_t *ctx, uint32_t packed_modes)
{
    const sampler_key_t key = {
        gltf_wrap((packed_modes >> 28) & 0xFu),
        gltf_min_filter((packed_modes >> 4) & 0xFu),
        ((packed_modes >> 8) & 0xFu) == VXTEXTUREFILTER_NEAREST ? GLTF_NEAREST : GLTF_LINEAR,
    };
    for (size_t i = 0; i < ctx->sampler_count; ++i) {
        if (memcmp(&ctx->sampler_keys[i], &key, sizeof(key)) == 0) return (int32_t)i;
    }
    if (!grow((void **)&ctx->sampler_keys, &ctx->sampler_capacity, ctx->sampler_count, sizeof(key))) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    yyjson_mut_val *json = yyjson_mut_obj(ctx->doc);
    if (json == NULL || !yyjson_mut_arr_append(ctx->samplers, json)) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    yyjson_mut_obj_add_int(ctx->doc, json, "magFilter", key.mag_filter);
    yyjson_mut_obj_add_int(ctx->doc, json, "minFilter", key.min_filter);
    yyjson_mut_obj_add_int(ctx->doc, json, "wrapS", key.wrap);
    yyjson_mut_obj_add_int(ctx->doc, json, "wrapT", key.wrap);
    ctx->sampler_keys[ctx->sampler_count] = key;
    return (int32_t)ctx->sampler_count++;
}

static int32_t export_texture(gltf_ctx_t *ctx, nmo_object_id_t texture_id, uint32_t packed_modes)
{
    const int32_t image = export_image(ctx, texture_id);
    if (image < 0) return -1;
    const int32_t sampler = export_sampler(ctx, packed_modes);
    if (sampler < 0) return -1;
    const texture_key_t key = {image, sampler};
    for (size_t i = 0; i < ctx->texture_count; ++i) {
        if (ctx->texture_keys[i].image == image && ctx->texture_keys[i].sampler == sampler) {
            return (int32_t)i;
        }
    }
    if (!grow((void **)&ctx->texture_keys, &ctx->texture_capacity, ctx->texture_count, sizeof(key))) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    yyjson_mut_val *json = yyjson_mut_obj(ctx->doc);
    if (json == NULL || !yyjson_mut_arr_append(ctx->textures, json)) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    yyjson_mut_obj_add_int(ctx->doc, json, "sampler", sampler);
    yyjson_mut_obj_add_int(ctx->doc, json, "source", image);
    ctx->texture_keys[ctx->texture_count] = key;
    ctx->stats.textures++;
    return (int32_t)ctx->texture_count++;
}

/* ---------------------------------------------------------------------------
 * Materials
 * --------------------------------------------------------------------------- */

static int32_t export_material(gltf_ctx_t *ctx, nmo_object_id_t material_id)
{
    if (material_id == NMO_OBJECT_ID_NONE) return -1;
    const int32_t known = id_map_get(&ctx->material_map, material_id);
    if (known != INT32_MIN) return known;

    int32_t index = -1;
    nmo_object_t *object = nmo_object_repository_find_by_id(ctx->repo, material_id);
    const nmo_material_state_t *state =
        object != NULL && nmo_object_get_class_id(object) == NMO_CID_MATERIAL
            ? (const nmo_material_state_t *)nmo_object_get_state(object)
            : NULL;
    if (state != NULL) {
        /* The texture first: it may add images and samplers */
        const nmo_object_id_t texture_id = nmo_material_texture_id(state, 0);
        const int32_t texture = texture_id != NMO_OBJECT_ID_NONE
            ? export_texture(ctx, texture_id, state->packed_modes) : -1;
        const nmo_object_t *texture_object = texture_id != NMO_OBJECT_ID_NONE
            ? nmo_object_repository_find_by_id(ctx->repo, texture_id) : NULL;
        const nmo_texture_state_t *texture_state =
            texture_object != NULL && nmo_object_get_class_id(texture_object) == NMO_CID_TEXTURE
                ? (const nmo_texture_state_t *)nmo_object_get_state(texture_object)
                : NULL;

        yyjson_mut_val *json = yyjson_mut_obj(ctx->doc);
        if (json == NULL || !yyjson_mut_arr_append(ctx->materials, json)) {
            fail(ctx, NMO_ERR_NOMEM);
            return -1;
        }
        add_name(ctx, json, object);
        yyjson_mut_val *pbr = yyjson_mut_obj_add_obj(ctx->doc, json, "pbrMetallicRoughness");
        if (pbr != NULL) {
            float diffuse[4];
            argb_floats(state->diffuse_color, diffuse);
            yyjson_mut_val *factor = yyjson_mut_obj_add_arr(ctx->doc, pbr, "baseColorFactor");
            for (int i = 0; factor != NULL && i < 4; ++i) {
                yyjson_mut_arr_add_real(ctx->doc, factor, diffuse[i]);
            }
            if (texture >= 0) {
                yyjson_mut_val *info = yyjson_mut_obj_add_obj(ctx->doc, pbr, "baseColorTexture");
                if (info != NULL) yyjson_mut_obj_add_int(ctx->doc, info, "index", texture);
            }
            yyjson_mut_obj_add_real(ctx->doc, pbr, "metallicFactor", 0.0);
            /* Blinn-Phong exponent to roughness: sqrt(2 / (power + 2)) */
            const double power = state->specular_power > 0.0f && isfinite(state->specular_power)
                ? (double)state->specular_power : 0.0;
            yyjson_mut_obj_add_real(ctx->doc, pbr, "roughnessFactor", sqrt(2.0 / (power + 2.0)));
        }
        float emissive[4];
        argb_floats(state->emissive_color, emissive);
        if (emissive[0] > 0.0f || emissive[1] > 0.0f || emissive[2] > 0.0f) {
            yyjson_mut_val *factor = yyjson_mut_obj_add_arr(ctx->doc, json, "emissiveFactor");
            for (int i = 0; factor != NULL && i < 3; ++i) {
                yyjson_mut_arr_add_real(ctx->doc, factor, emissive[i]);
            }
        }
        const uint32_t flags = state->packed_flags;
        if ((flags & 0x08u) != 0u) {
            yyjson_mut_obj_add_str(ctx->doc, json, "alphaMode", "BLEND");
        } else if ((flags & 0x10u) != 0u ||
                   (texture >= 0 && texture_state != NULL && texture_state->is_transparent)) {
            /* An alpha test, or a colour-keyed texture, which the engine tests */
            const uint8_t ref = (flags & 0x10u) != 0u ? (uint8_t)(flags >> 24) : 0u;
            yyjson_mut_obj_add_str(ctx->doc, json, "alphaMode", "MASK");
            yyjson_mut_obj_add_real(ctx->doc, json, "alphaCutoff", ref > 0 ? ref / 255.0 : 0.5);
        }
        if ((flags & 0x01u) != 0u) yyjson_mut_obj_add_bool(ctx->doc, json, "doubleSided", true);
        index = (int32_t)yyjson_mut_arr_size(ctx->materials) - 1;
        ctx->stats.materials++;
    }
    if (!id_map_put(&ctx->material_map, material_id, index)) fail(ctx, NMO_ERR_NOMEM);
    return index;
}

/* ---------------------------------------------------------------------------
 * Meshes
 * --------------------------------------------------------------------------- */

static bool face_is_valid(const nmo_mesh_state_t *state, uint32_t face)
{
    const uint16_t *v = &state->face_vertex_indices[(size_t)face * 3u];
    return v[0] < state->vertex_count && v[1] < state->vertex_count && v[2] < state->vertex_count;
}

static int32_t write_mesh(gltf_ctx_t *ctx, nmo_object_t *object, const nmo_mesh_state_t *state)
{
    const uint32_t vertex_count = state->vertex_count;
    const size_t floats3 = (size_t)vertex_count * 3u;

    /* Positions and normals, Z negated */
    float *positions = (float *)nmo_arena_alloc(ctx->arena, floats3 * sizeof(float), sizeof(float));
    float *normals = (float *)nmo_arena_alloc(ctx->arena, floats3 * sizeof(float), sizeof(float));
    float *uvs = (float *)nmo_arena_alloc(ctx->arena, (size_t)vertex_count * 2u * sizeof(float), sizeof(float));
    nmo_vector_t *built = nmo_mesh_normals_are_derived(state)
        ? (nmo_vector_t *)nmo_arena_alloc(ctx->arena, (size_t)vertex_count * sizeof(nmo_vector_t),
                                          sizeof(float))
        : NULL;
    if (positions == NULL || normals == NULL || uvs == NULL ||
        (built == NULL && nmo_mesh_normals_are_derived(state))) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    if (built != NULL && nmo_mesh_build_vertex_normals(state, built) != NMO_OK) built = NULL;
    float min[3] = {INFINITY, INFINITY, INFINITY};
    float max[3] = {-INFINITY, -INFINITY, -INFINITY};
    for (uint32_t i = 0; i < vertex_count; ++i) {
        const nmo_vertex_t *v = &state->vertices[i];
        const float p[3] = {v->position.x, v->position.y, -v->position.z};
        nmo_vector_t n = built != NULL ? built[i] : v->normal;
        float length = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (!(length > 1e-12f) || !isfinite(length)) {
            n = (nmo_vector_t){0.0f, 1.0f, 0.0f};
            length = 1.0f;
        }
        for (int k = 0; k < 3; ++k) {
            const float value = isfinite(p[k]) ? p[k] : 0.0f;
            positions[i * 3u + (uint32_t)k] = value;
            if (value < min[k]) min[k] = value;
            if (value > max[k]) max[k] = value;
        }
        normals[i * 3u + 0u] = n.x / length;
        normals[i * 3u + 1u] = n.y / length;
        normals[i * 3u + 2u] = -n.z / length;
        uvs[i * 2u + 0u] = isfinite(v->uv.x) ? v->uv.x : 0.0f;
        uvs[i * 2u + 1u] = isfinite(v->uv.y) ? v->uv.y : 0.0f;
    }

    /* Faces by material group; faces past the groups have no material */
    const uint32_t groups = state->material_groups != NULL ? state->material_group_count : 0u;
    size_t *counts = (size_t *)nmo_arena_alloc(ctx->arena, ((size_t)groups + 1u) * sizeof(size_t),
                                               sizeof(size_t));
    if (counts == NULL) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    memset(counts, 0, ((size_t)groups + 1u) * sizeof(size_t));
    size_t triangles = 0;
    for (uint32_t f = 0; f < state->face_count; ++f) {
        if (!face_is_valid(state, f)) continue;
        const uint32_t group = state->faces != NULL ? state->faces[f].material_group_idx : groups;
        counts[group < groups ? group : groups]++;
        triangles++;
    }
    if (triangles == 0) return -1;

    const int32_t position = add_accessor(ctx, positions, floats3 * sizeof(float), GLTF_ARRAY_BUFFER,
                                          GLTF_FLOAT, false, vertex_count, "VEC3", min, max, 3);
    const int32_t normal = add_accessor(ctx, normals, floats3 * sizeof(float), GLTF_ARRAY_BUFFER,
                                        GLTF_FLOAT, false, vertex_count, "VEC3", NULL, NULL, 0);
    const int32_t uv = add_accessor(ctx, uvs, (size_t)vertex_count * 2u * sizeof(float),
                                    GLTF_ARRAY_BUFFER, GLTF_FLOAT, false, vertex_count, "VEC2",
                                    NULL, NULL, 0);
    int32_t color = -1;
    if ((state->flags & VXMESH_PRELITMODE) != 0u && state->vertex_colors != NULL) {
        uint8_t *rgba = (uint8_t *)nmo_arena_alloc(ctx->arena, (size_t)vertex_count * 4u, 4u);
        if (rgba == NULL) {
            fail(ctx, NMO_ERR_NOMEM);
            return -1;
        }
        for (uint32_t i = 0; i < vertex_count; ++i) {
            const uint32_t argb = state->vertex_colors[i];
            rgba[i * 4u + 0u] = (uint8_t)(argb >> 16);
            rgba[i * 4u + 1u] = (uint8_t)(argb >> 8);
            rgba[i * 4u + 2u] = (uint8_t)argb;
            rgba[i * 4u + 3u] = (uint8_t)(argb >> 24);
        }
        color = add_accessor(ctx, rgba, (size_t)vertex_count * 4u, GLTF_ARRAY_BUFFER,
                             GLTF_UNSIGNED_BYTE, true, vertex_count, "VEC4", NULL, NULL, 0);
    }
    if (ctx->error != NMO_OK) return -1;

    yyjson_mut_val *json = yyjson_mut_obj(ctx->doc);
    if (json == NULL) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    add_name(ctx, json, object);
    yyjson_mut_val *primitives = yyjson_mut_obj_add_arr(ctx->doc, json, "primitives");
    uint16_t *indices = (uint16_t *)nmo_arena_alloc(ctx->arena, triangles * 3u * sizeof(uint16_t),
                                                    sizeof(uint16_t));
    if (primitives == NULL || indices == NULL) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    for (uint32_t group = 0; group <= groups; ++group) {
        if (counts[group] == 0) continue;
        size_t n = 0;
        for (uint32_t f = 0; f < state->face_count; ++f) {
            if (!face_is_valid(state, f)) continue;
            const uint32_t face_group = state->faces != NULL ? state->faces[f].material_group_idx : groups;
            if ((face_group < groups ? face_group : groups) != group) continue;
            const uint16_t *v = &state->face_vertex_indices[(size_t)f * 3u];
            /* Reversed winding for the right-handed frame */
            indices[n++] = v[0];
            indices[n++] = v[2];
            indices[n++] = v[1];
        }
        const int32_t accessor = add_accessor(ctx, indices, n * sizeof(uint16_t),
                                              GLTF_ELEMENT_ARRAY_BUFFER, GLTF_UNSIGNED_SHORT, false,
                                              n, "SCALAR", NULL, NULL, 0);
        const int32_t material = group < groups
            ? export_material(ctx, nmo_ref_runtime_id(&state->material_groups[group].material))
            : -1;
        if (ctx->error != NMO_OK) return -1;
        yyjson_mut_val *primitive = yyjson_mut_obj(ctx->doc);
        yyjson_mut_val *attributes = primitive != NULL
            ? yyjson_mut_obj_add_obj(ctx->doc, primitive, "attributes") : NULL;
        if (attributes == NULL || !yyjson_mut_arr_append(primitives, primitive)) {
            fail(ctx, NMO_ERR_NOMEM);
            return -1;
        }
        yyjson_mut_obj_add_int(ctx->doc, attributes, "POSITION", position);
        yyjson_mut_obj_add_int(ctx->doc, attributes, "NORMAL", normal);
        yyjson_mut_obj_add_int(ctx->doc, attributes, "TEXCOORD_0", uv);
        if (color >= 0) yyjson_mut_obj_add_int(ctx->doc, attributes, "COLOR_0", color);
        yyjson_mut_obj_add_int(ctx->doc, primitive, "indices", accessor);
        if (material >= 0) yyjson_mut_obj_add_int(ctx->doc, primitive, "material", material);
        ctx->stats.primitives++;
    }
    if (!yyjson_mut_arr_append(ctx->meshes, json)) {
        fail(ctx, NMO_ERR_NOMEM);
        return -1;
    }
    ctx->stats.meshes++;
    ctx->stats.vertices += vertex_count;
    ctx->stats.triangles += triangles;
    return (int32_t)yyjson_mut_arr_size(ctx->meshes) - 1;
}

/* The glTF mesh of a CKMesh, written once; -1 when it draws nothing */
static int32_t export_mesh(gltf_ctx_t *ctx, nmo_object_id_t mesh_id)
{
    if (mesh_id == NMO_OBJECT_ID_NONE) return -1;
    const int32_t known = id_map_get(&ctx->mesh_map, mesh_id);
    if (known != INT32_MIN) return known;

    int32_t index = -1;
    nmo_object_t *object = nmo_object_repository_find_by_id(ctx->repo, mesh_id);
    const nmo_mesh_state_t *state = object != NULL &&
        nmo_type_query_object_is_derived_from_class(ctx->types, object, NMO_CID_MESH)
            ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                  ctx->types, object, CKPGUID_MESH)
            : NULL;
    if (state != NULL && state->vertex_count > 0 && state->vertices != NULL &&
        state->face_count > 0 && state->face_vertex_indices != NULL) {
        index = write_mesh(ctx, object, state);
    }
    if (!id_map_put(&ctx->mesh_map, mesh_id, index)) fail(ctx, NMO_ERR_NOMEM);
    return index;
}

/* ---------------------------------------------------------------------------
 * Entities
 * --------------------------------------------------------------------------- */

static int compare_ids(const void *a, const void *b)
{
    const nmo_object_id_t x = ((const id_index_t *)a)->id;
    const nmo_object_id_t y = ((const id_index_t *)b)->id;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int32_t node_of(const id_index_t *sorted, size_t count, nmo_object_id_t id)
{
    const id_index_t key = {id, 0};
    const id_index_t *found = (const id_index_t *)bsearch(&key, sorted, count, sizeof(key), compare_ids);
    return found != NULL ? found->index : -1;
}

/* The entities, each under its parent (a parent chain that loops is cut) */
static nmo_status_t collect_entities(gltf_ctx_t *ctx, entity_t **out_entities, size_t *out_count)
{
    const size_t total = nmo_object_repository_get_count(ctx->repo);
    entity_t *entities = total > 0 ? (entity_t *)calloc(total, sizeof(entity_t)) : NULL;
    id_index_t *sorted = total > 0 ? (id_index_t *)calloc(total, sizeof(id_index_t)) : NULL;
    if (total > 0 && (entities == NULL || sorted == NULL)) {
        free(entities);
        free(sorted);
        return NMO_ERR_NOMEM;
    }
    size_t count = 0;
    for (size_t i = 0; i < total; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(ctx->repo, i);
        if (object == NULL ||
            !nmo_type_query_object_is_derived_from_class(ctx->types, object, NMO_CID_3DENTITY)) {
            continue;
        }
        const nmo_3dentity_state_t *state = (const nmo_3dentity_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(ctx->types, object, CKPGUID_3DENTITY);
        if (state == NULL) continue;
        entities[count] = (entity_t){object, state, -1, {0}};
        sorted[count] = (id_index_t){nmo_object_get_id(object), (int32_t)count};
        count++;
    }
    qsort(sorted, count, sizeof(id_index_t), compare_ids);
    for (size_t i = 0; i < count; ++i) {
        const int32_t parent = node_of(sorted, count, nmo_ref_runtime_id(&entities[i].state->parent));
        entities[i].parent = parent != (int32_t)i ? parent : -1;
    }
    free(sorted);
    for (size_t i = 0; i < count; ++i) {
        size_t steps = 0;
        for (int32_t at = entities[i].parent; at >= 0; at = entities[at].parent) {
            if (++steps > count) {
                entities[i].parent = -1;
                break;
            }
        }
    }

    /* Each matrix relative to its parent's world matrix */
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (size_t i = 0; i < count; ++i) {
        entity_t *e = &entities[i];
        const float *world = mat4_is_finite(e->state->world_matrix) ? e->state->world_matrix : identity;
        memcpy(e->local, world, sizeof(e->local));
        if (e->parent >= 0) {
            const float *parent_world = entities[e->parent].state->world_matrix;
            float inverse[16];
            if (mat4_is_finite(parent_world) && mat4_inverse(parent_world, inverse)) {
                mat4_multiply(inverse, world, e->local);
            } else {
                e->parent = -1;
            }
        }
        mat4_flip_z(e->local);
    }
    *out_entities = entities;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t write_nodes(gltf_ctx_t *ctx, yyjson_mut_val *root,
                                const entity_t *entities, size_t count)
{
    yyjson_mut_val *nodes = yyjson_mut_arr(ctx->doc);
    yyjson_mut_val *roots = yyjson_mut_arr(ctx->doc);
    if (nodes == NULL || roots == NULL) return NMO_ERR_NOMEM;
    for (size_t i = 0; i < count; ++i) {
        const entity_t *e = &entities[i];
        yyjson_mut_val *node = yyjson_mut_obj(ctx->doc);
        if (node == NULL || !yyjson_mut_arr_append(nodes, node)) return NMO_ERR_NOMEM;
        add_name(ctx, node, e->object);
        if (!mat4_is_identity(e->local)) {
            yyjson_mut_val *matrix = yyjson_mut_obj_add_arr(ctx->doc, node, "matrix");
            for (int k = 0; matrix != NULL && k < 16; ++k) {
                yyjson_mut_arr_add_real(ctx->doc, matrix, e->local[k]);
            }
        }
        yyjson_mut_val *children = NULL;
        for (size_t c = 0; c < count; ++c) {
            if (entities[c].parent != (int32_t)i) continue;
            if (children == NULL) children = yyjson_mut_obj_add_arr(ctx->doc, node, "children");
            if (children == NULL) return NMO_ERR_NOMEM;
            yyjson_mut_arr_add_uint(ctx->doc, children, c);
        }
        const int32_t mesh = export_mesh(ctx, nmo_ref_runtime_id(&e->state->current_mesh));
        if (ctx->error != NMO_OK) return ctx->error;
        if (mesh >= 0) yyjson_mut_obj_add_int(ctx->doc, node, "mesh", mesh);
        if (e->parent < 0) yyjson_mut_arr_add_uint(ctx->doc, roots, i);
    }
    ctx->stats.nodes = count;

    yyjson_mut_obj_add_uint(ctx->doc, root, "scene", 0);
    yyjson_mut_val *scenes = yyjson_mut_obj_add_arr(ctx->doc, root, "scenes");
    yyjson_mut_val *scene = yyjson_mut_obj(ctx->doc);
    if (scenes == NULL || scene == NULL || !yyjson_mut_arr_append(scenes, scene)) return NMO_ERR_NOMEM;
    if (yyjson_mut_arr_size(roots) > 0) yyjson_mut_obj_add_val(ctx->doc, scene, "nodes", roots);
    if (count > 0) yyjson_mut_obj_add_val(ctx->doc, root, "nodes", nodes);
    return NMO_OK;
}

/* ---------------------------------------------------------------------------
 * The file
 * --------------------------------------------------------------------------- */

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static nmo_status_t write_glb(const char *path, const char *json, size_t json_size,
                              const uint8_t *bin, size_t bin_size)
{
    const size_t json_padded = (json_size + 3u) & ~(size_t)3u;
    const size_t bin_padded = (bin_size + 3u) & ~(size_t)3u;
    const size_t total = 12u + 8u + json_padded + (bin_size > 0 ? 8u + bin_padded : 0u);
    if (json_padded < json_size || total > UINT32_MAX) return NMO_ERR_INVALID_ARGUMENT;

    nmo_txn_desc_t desc = {path, NMO_TXN_NONE, NULL};
    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    if (txn == NULL) return NMO_ERR_CANT_OPEN_FILE;
    uint8_t header[20];
    put_u32(header, GLB_MAGIC);
    put_u32(header + 4, 2u);
    put_u32(header + 8, (uint32_t)total);
    put_u32(header + 12, (uint32_t)json_padded);
    put_u32(header + 16, GLB_CHUNK_JSON);
    static const uint8_t spaces[3] = {' ', ' ', ' '};
    static const uint8_t zeros[3] = {0, 0, 0};
    nmo_status_t status = nmo_txn_write(txn, header, sizeof(header));
    if (status == NMO_OK) status = nmo_txn_write(txn, json, json_size);
    if (status == NMO_OK && json_padded > json_size) {
        status = nmo_txn_write(txn, spaces, json_padded - json_size);
    }
    if (status == NMO_OK && bin_size > 0) {
        uint8_t chunk[8];
        put_u32(chunk, (uint32_t)bin_padded);
        put_u32(chunk + 4, GLB_CHUNK_BIN);
        status = nmo_txn_write(txn, chunk, sizeof(chunk));
        if (status == NMO_OK) status = nmo_txn_write(txn, bin, bin_size);
        if (status == NMO_OK && bin_padded > bin_size) {
            status = nmo_txn_write(txn, zeros, bin_padded - bin_size);
        }
    }
    if (status == NMO_OK) {
        status = nmo_txn_commit(txn);
    } else {
        nmo_txn_rollback(txn);
    }
    nmo_txn_close(txn);
    return status;
}

nmo_gltf_export_options_t nmo_gltf_export_options_default(void)
{
    nmo_gltf_export_options_t options;
    memset(&options, 0, sizeof(options));
    options.embed_textures = true;
    options.apply_color_key = true;
    return options;
}

/* Puts arr under key in root when it holds anything */
static void add_if_any(gltf_ctx_t *ctx, yyjson_mut_val *root, const char *key, yyjson_mut_val *arr)
{
    if (yyjson_mut_arr_size(arr) > 0) yyjson_mut_obj_add_val(ctx->doc, root, key, arr);
}

nmo_status_t nmo_export_gltf(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    const char *path,
    const nmo_gltf_export_options_t *options,
    nmo_gltf_export_stats_t *out_stats)
{
    if (out_stats != NULL) memset(out_stats, 0, sizeof(*out_stats));
    if (repo == NULL || types == NULL || path == NULL) return NMO_ERR_INVALID_ARGUMENT;

    gltf_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.repo = repo;
    ctx.types = types;
    ctx.options = options != NULL ? *options : nmo_gltf_export_options_default();
    ctx.arena = nmo_arena_create(NULL, 1u << 16);
    ctx.doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = ctx.doc != NULL ? yyjson_mut_obj(ctx.doc) : NULL;
    entity_t *entities = NULL;
    size_t entity_count = 0;
    char *json = NULL;
    size_t json_size = 0;
    nmo_status_t status = NMO_OK;
    if (ctx.arena == NULL || root == NULL) {
        status = NMO_ERR_NOMEM;
        goto done;
    }
    yyjson_mut_doc_set_root(ctx.doc, root);
    yyjson_mut_val *asset = yyjson_mut_obj_add_obj(ctx.doc, root, "asset");
    if (asset == NULL) {
        status = NMO_ERR_NOMEM;
        goto done;
    }
    yyjson_mut_obj_add_str(ctx.doc, asset, "version", "2.0");
    yyjson_mut_obj_add_str(ctx.doc, asset, "generator", "libnmo");
    ctx.meshes = yyjson_mut_arr(ctx.doc);
    ctx.materials = yyjson_mut_arr(ctx.doc);
    ctx.textures = yyjson_mut_arr(ctx.doc);
    ctx.samplers = yyjson_mut_arr(ctx.doc);
    ctx.images = yyjson_mut_arr(ctx.doc);
    ctx.accessors = yyjson_mut_arr(ctx.doc);
    ctx.buffer_views = yyjson_mut_arr(ctx.doc);
    if (ctx.meshes == NULL || ctx.materials == NULL || ctx.textures == NULL ||
        ctx.samplers == NULL || ctx.images == NULL || ctx.accessors == NULL ||
        ctx.buffer_views == NULL) {
        status = NMO_ERR_NOMEM;
        goto done;
    }

    status = collect_entities(&ctx, &entities, &entity_count);
    if (status == NMO_OK) status = write_nodes(&ctx, root, entities, entity_count);
    if (status == NMO_OK) status = ctx.error;
    if (status != NMO_OK) goto done;

    add_if_any(&ctx, root, "meshes", ctx.meshes);
    add_if_any(&ctx, root, "materials", ctx.materials);
    add_if_any(&ctx, root, "textures", ctx.textures);
    add_if_any(&ctx, root, "images", ctx.images);
    add_if_any(&ctx, root, "samplers", ctx.samplers);
    add_if_any(&ctx, root, "accessors", ctx.accessors);
    add_if_any(&ctx, root, "bufferViews", ctx.buffer_views);
    if (ctx.bin_size > 0) {
        yyjson_mut_val *buffers = yyjson_mut_obj_add_arr(ctx.doc, root, "buffers");
        yyjson_mut_val *buffer = yyjson_mut_obj(ctx.doc);
        if (buffers == NULL || buffer == NULL || !yyjson_mut_arr_append(buffers, buffer)) {
            status = NMO_ERR_NOMEM;
            goto done;
        }
        yyjson_mut_obj_add_uint(ctx.doc, buffer, "byteLength", ctx.bin_size);
    }

    json = yyjson_mut_write(ctx.doc, 0, &json_size);
    if (json == NULL) {
        status = NMO_ERR_NOMEM;
        goto done;
    }
    status = write_glb(path, json, json_size, ctx.bin, ctx.bin_size);
    if (status == NMO_OK && out_stats != NULL) *out_stats = ctx.stats;

done:
    free(json);
    free(entities);
    free(ctx.bin);
    free(ctx.mesh_map.items);
    free(ctx.material_map.items);
    free(ctx.image_map.items);
    free(ctx.sampler_keys);
    free(ctx.texture_keys);
    if (ctx.doc != NULL) yyjson_mut_doc_free(ctx.doc);
    nmo_arena_destroy(ctx.arena);
    return status;
}
