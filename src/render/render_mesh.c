/**
 * @file render_mesh.c
 * @brief A mesh made ready to draw, and the Direct3D rules a mesh draws by.
 */

#include "render_mesh.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The prelit colors as RGBA bytes, and the specular ones when one is not black */
static void mesh_colors(render_mesh_t *cached, const nmo_engine_mesh_t *mesh)
{
    free(cached->colors);
    cached->colors = NULL;
    free(cached->speculars);
    cached->speculars = NULL;
    cached->version = mesh->version;
    bool specular = false;
    for (int32_t v = 0; mesh->speculars != NULL && v < mesh->vertex_count && !specular; v++) {
        specular = (mesh->speculars[v] & 0xFFFFFFu) != 0;
    }
    if (specular) {
        cached->speculars = (uint8_t *)malloc((size_t)mesh->vertex_count * 4u);
        for (int32_t v = 0; cached->speculars != NULL && v < mesh->vertex_count; v++) {
            uint32_t c = mesh->speculars[v];
            cached->speculars[v * 4 + 0] = (uint8_t)(c >> 16);
            cached->speculars[v * 4 + 1] = (uint8_t)(c >> 8);
            cached->speculars[v * 4 + 2] = (uint8_t)c;
            cached->speculars[v * 4 + 3] = (uint8_t)(c >> 24);
        }
    }
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

void render_mesh_fill(render_mesh_t *cached, const nmo_engine_mesh_t *mesh)
{
    int32_t groups = mesh->material_count > 0 ? mesh->material_count : 1;
    cached->group_count = groups;
    cached->indices = (uint16_t **)calloc((size_t)groups, sizeof(uint16_t *));
    cached->counts = (int32_t *)calloc((size_t)groups, sizeof(int32_t));
    if (cached->indices == NULL || cached->counts == NULL) {
        return;
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
    mesh_colors(cached, mesh);
}

void render_mesh_release(render_mesh_t *cached)
{
    for (int32_t g = 0; cached->indices != NULL && g < cached->group_count; g++) {
        free(cached->indices[g]);
    }
    free(cached->indices);
    free(cached->counts);
    cached->indices = NULL;
    cached->counts = NULL;
}

render_mesh_t *render_mesh_cache_get(render_mesh_cache_t *cache, nmo_object_id_t id,
                                     const nmo_engine_mesh_t *mesh)
{
    for (size_t i = 0; i < cache->count; i++) {
        if (cache->meshes[i].id == id) {
            if (cache->meshes[i].version != mesh->version) {
                /* a block changed it: its faces may have too (indices, material groups) */
                render_mesh_release(&cache->meshes[i]);
                render_mesh_fill(&cache->meshes[i], mesh);
            }
            return &cache->meshes[i];
        }
    }
    if (cache->count == cache->capacity) {
        size_t capacity = cache->capacity != 0 ? cache->capacity * 2 : 64;
        render_mesh_t *grown = (render_mesh_t *)realloc(cache->meshes, capacity * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        cache->meshes = grown;
        cache->capacity = capacity;
    }
    render_mesh_t *cached = &cache->meshes[cache->count++];
    memset(cached, 0, sizeof(*cached));
    cached->id = id;
    render_mesh_fill(cached, mesh);
    return cached;
}

void render_mesh_cache_free(render_mesh_cache_t *cache)
{
    for (size_t i = 0; i < cache->count; i++) {
        render_mesh_release(&cache->meshes[i]);
        free(cache->meshes[i].colors);
        free(cache->meshes[i].speculars);
    }
    free(cache->meshes);
    memset(cache, 0, sizeof(*cache));
}

bool render_channel_folds(const nmo_engine_channel_t *channel, const nmo_engine_material_t *material,
                          bool prelit)
{
    return channel->lit && !prelit && material->texture != 0 &&
           ((channel->source_blend == 1 && channel->dest_blend == 3) ||
            (channel->source_blend == 9 && channel->dest_blend == 1));
}

void render_wrapped_free(render_wrapped_t *w)
{
    free(w->vertices);
    free(w->colors);
    free(w->speculars);
    free(w->indices);
    for (int32_t c = 0; w->channel_uvs != NULL && c < w->channel_count; c++) {
        free(w->channel_uvs[c]);
    }
    free(w->channel_uvs);
    free(w->channels);
    memset(w, 0, sizeof(*w));
}

/* The nearest wrap of `d` (D3DRENDERSTATE_WRAP0: coordinates on a cylinder) */
static float mesh_wrap_delta(float d)
{
    return d - floorf(d + 0.5f);
}

bool render_wrap_mesh(nmo_engine_mesh_t *mesh, render_wrapped_t *w)
{
    memset(w, 0, sizeof(*w));
    const int32_t count = mesh->face_count * 3;
    if (mesh->face_count <= 0 || count > 65535) {
        return false;
    }
    w->vertices = (nmo_vertex_t *)malloc((size_t)count * sizeof(nmo_vertex_t));
    w->indices = (uint16_t *)malloc((size_t)count * sizeof(uint16_t));
    w->colors = mesh->colors != NULL ? (uint32_t *)malloc((size_t)count * sizeof(uint32_t)) : NULL;
    w->speculars = mesh->speculars != NULL ? (uint32_t *)malloc((size_t)count * sizeof(uint32_t)) : NULL;
    w->channel_count = mesh->channel_count;
    w->channels = mesh->channel_count > 0
                      ? (nmo_engine_channel_t *)malloc((size_t)mesh->channel_count * sizeof(nmo_engine_channel_t))
                      : NULL;
    w->channel_uvs = mesh->channel_count > 0 ? (float **)calloc((size_t)mesh->channel_count, sizeof(float *))
                                             : NULL;
    if (w->vertices == NULL || w->indices == NULL || (mesh->colors != NULL && w->colors == NULL) ||
        (mesh->speculars != NULL && w->speculars == NULL) ||
        (mesh->channel_count > 0 && (w->channels == NULL || w->channel_uvs == NULL))) {
        render_wrapped_free(w);
        return false;
    }
    for (int32_t f = 0; f < mesh->face_count; f++) {
        for (int k = 0; k < 3; k++) {
            uint16_t index = mesh->indices[f * 3 + k];
            if (index >= (uint32_t)mesh->vertex_count) {
                index = 0;
            }
            const int32_t at = f * 3 + k;
            w->vertices[at] = mesh->vertices[index];
            w->indices[at] = (uint16_t)at;
            if (w->colors != NULL) {
                w->colors[at] = mesh->colors[index];
            }
            if (w->speculars != NULL) {
                w->speculars[at] = mesh->speculars[index];
            }
        }
        nmo_vertex_t *first = &w->vertices[f * 3];
        for (int k = 1; k < 3; k++) {
            if (mesh->wrap_u) {
                first[k].uv.x = first[0].uv.x + mesh_wrap_delta(first[k].uv.x - first[0].uv.x);
            }
            if (mesh->wrap_v) {
                first[k].uv.y = first[0].uv.y + mesh_wrap_delta(first[k].uv.y - first[0].uv.y);
            }
        }
    }
    for (int32_t c = 0; c < mesh->channel_count; c++) {
        w->channels[c] = mesh->channels[c];
        if (mesh->channels[c].uvs == NULL) {
            continue;
        }
        float *uvs = (float *)malloc((size_t)count * 2u * sizeof(float));
        if (uvs == NULL) {
            render_wrapped_free(w);
            return false;
        }
        for (int32_t at = 0; at < count; at++) {
            uint16_t index = mesh->indices[at];
            index = index < (uint32_t)mesh->vertex_count ? index : 0;
            uvs[at * 2] = mesh->channels[c].uvs[index * 2];
            uvs[at * 2 + 1] = mesh->channels[c].uvs[index * 2 + 1];
        }
        w->channel_uvs[c] = uvs;
        w->channels[c].uvs = uvs;
    }
    mesh->vertex_count = count;
    mesh->vertices = w->vertices;
    mesh->indices = w->indices;
    mesh->colors = w->colors;
    mesh->speculars = w->speculars;
    mesh->channels = w->channels;
    return true;
}
