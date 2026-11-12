/**
 * @file render_draw.c
 * @brief The engine's draws as CK2's rasterizer makes them: materials set
 *        as current (RCKMaterial::SetAsCurrent), meshes with their groups,
 *        channels and wireframe (RCKMesh::DefaultRender), 2D entities, sprite
 *        texts, particles, sprites and render callbacks' primitives.
 *
 * Each changes the renderer's state as Direct3D's would change and hands the
 * backend its vertices. Direct3D's vertex lighting is done here
 * (render_light.c), so the backend lights nothing.
 */

#include "render_internal.h"
#include "render_light.h"
#include "render_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const float draw_identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void draw_emit(nmo_render_t *render, const render_geometry_t *geometry)
{
    render->backend->ops->draw(render->backend, &render->state, geometry);
}

/* ============================================================================
 * States
 * ============================================================================ */

void render_draw_state(nmo_render_t *render, const nmo_engine_draw_t *draw)
{
    render_state_t *s = &render->state;
    if (draw->stencil.enabled) {
        s->stencil = true;
        s->stencil_func = draw->stencil.func;
        s->stencil_ref = draw->stencil.ref;
        s->stencil_mask = draw->stencil.mask;
        s->stencil_fail = draw->stencil.fail;
        s->stencil_zfail = draw->stencil.zfail;
        s->stencil_pass = draw->stencil.pass;
        s->stencil_write_mask = draw->stencil.write_mask;
    } else {
        s->stencil = false;
    }
    s->front_ccw = draw->mirrored; /* INDIRECTMATRIX inverts the winding */
}

void render_set_projection(nmo_render_t *render, const nmo_engine_frame_t *frame,
                           bool orthographic, float fov, float near_plane, float far_plane)
{
    render_state_t *s = &render->state;
    s->projection_kind = RENDER_PROJECTION_MATRIX;
    render_projection(frame, orthographic, fov, near_plane, far_plane, s->projection);
    s->depth_scale = 0.0f;
}

/* A VXTEXTURE_FILTERMODE as the device samples with it: minifying, with its mip part */
static void draw_filter(int32_t filter, bool minify, render_filter_t *out_filter, render_mip_t *out_mip)
{
    *out_mip = RENDER_MIP_NONE;
    switch (filter) {
    case 1:
        *out_filter = RENDER_FILTER_NEAREST;
        break;
    case 3:
    case 4:
        *out_filter = RENDER_FILTER_NEAREST;
        if (minify) {
            *out_mip = filter == 3 ? RENDER_MIP_NEAREST : RENDER_MIP_LINEAR;
        }
        break;
    case 5:
    case 6:
    case 7:
        *out_filter = RENDER_FILTER_LINEAR;
        if (minify) {
            *out_mip = filter == 5 ? RENDER_MIP_NEAREST : RENDER_MIP_LINEAR;
        }
        break;
    default:
        *out_filter = RENDER_FILTER_LINEAR;
        break;
    }
}

/* VXTEXTURE_BLENDMODE as a texture stage operation */
static render_texenv_t draw_texenv(int32_t blend)
{
    switch (blend) {
    case 1: /* DECAL */
    case 3: /* DECALALPHA */
    case 5:
        return RENDER_TEXENV_DECAL;
    case 7: /* COPY */
        return RENDER_TEXENV_REPLACE;
    case 8:
        return RENDER_TEXENV_ADD;
    default:
        return RENDER_TEXENV_MODULATE;
    }
}

/*
 * RCKMaterial::TexGenEffect: reflect and chrome take the camera space
 * reflection vector or normal, planar the camera space position, transform
 * the mesh's UVs. With a referential, reflect and chrome turn by the basis
 * looking from it to the object drawn (`world`), planar takes it inverted,
 * transform takes its matrix; the result is scaled by 0.4 (0.5 planar), V
 * flipped, about the texture's middle. The eye space here is the camera's,
 * and the matrices are CK's row-vector ones. The cube map ones (31 reflect,
 * 32 sky, 33 normals, 34 positions) take the reflection vector, position,
 * normal and position into a cube map turned as the referential, or the
 * camera; positions sends them into the object's space as well.
 */
void render_set_texgen(nmo_render_t *render, const nmo_engine_material_t *m, const float *world)
{
    render_state_t *s = &render->state;
    int32_t texgen = m != NULL ? m->texgen : 0;
    bool cube = texgen >= 31 && texgen <= 34; /* VXEFFECT_TGCUBEMAP_* */
    render_texgen_t mode = texgen == 2 || texgen == 31 ? RENDER_TEXGEN_REFLECT
                           : texgen == 3 || texgen == 33 ? RENDER_TEXGEN_NORMAL
                           : texgen == 4 || texgen == 32 || texgen == 34 ? RENDER_TEXGEN_EYE
                                                                          : RENDER_TEXGEN_NONE;
    float ref[16];
    bool referential = m != NULL && m->texgen_referential != 0 &&
                       nmo_engine_entity_world_matrix(render->engine, m->texgen_referential, ref);
    float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (cube) {
        /* The referential's world, the scene root's (the camera's) without one; positions
           are then taken into the object's space; no translation either way */
        memcpy(t, referential ? ref : render->eye, sizeof(t));
        float inverse[16];
        if (texgen == 34 && world != NULL && render_affine_inverse(inverse, world)) {
            render_multiply(t, t, inverse);
        }
        t[12] = t[13] = t[14] = 0.0f;
        t[15] = 1.0f;
    } else if (texgen == 1 && referential) {
        memcpy(t, ref, sizeof(t));
    } else if (mode != RENDER_TEXGEN_NONE) {
        if (mode != RENDER_TEXGEN_EYE && referential && world != NULL) {
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
        } else if (mode == RENDER_TEXGEN_EYE && referential) {
            render_affine_inverse(t, ref);
        }
        float scale = mode == RENDER_TEXGEN_EYE ? 0.5f : 0.4f;
        for (int row = 0; row < 4; row++) {
            t[row * 4 + 0] *= scale;
            t[row * 4 + 1] *= -scale;
            t[row * 4 + 2] *= scale;
        }
        if (mode == RENDER_TEXGEN_EYE) {
            t[12] += 0.5f;
            t[13] += 0.5f;
        } else {
            t[12] = 0.5f;
            t[13] = 0.5f;
            t[14] = 0.0f;
            t[15] = 1.0f;
        }
    }
    memcpy(s->texture_matrix, t, sizeof(t));
    s->texgen = mode;
}

void render_set_material(nmo_render_t *render, const nmo_engine_material_t *m, bool depth,
                         const float *world)
{
    render_state_t *s = &render->state;
    uintptr_t texture = render->use_textures ? render_texture(render, m->texture) : 0;
    if (texture != 0) {
        const render_texture_t *uploaded = render_texture_entry(render, m->texture);
        bool cube = uploaded != NULL && uploaded->cube;
        s->texture = texture;
        s->texture_kind = cube ? RENDER_TEXTURE_CUBE : RENDER_TEXTURE_2D;
        draw_filter(m->min_filter, true, &s->sampler.min_filter, &s->sampler.mip);
        /* Without mipmaps the device samples the image itself, whatever the mip filter; forced
           ones are sampled even under a filter that has no mip part */
        bool mipmapped = uploaded != NULL && uploaded->mipmapped;
        if (uploaded != NULL && uploaded->forced) {
            if (s->sampler.mip == RENDER_MIP_NONE) {
                s->sampler.mip = RENDER_MIP_LINEAR;
            }
        } else if (!mipmapped) {
            s->sampler.mip = RENDER_MIP_NONE;
        }
        /* The window has many more pixels than CK's render context: mipmaps seen at a
           slant stay sharp */
        s->sampler.anisotropic = mipmapped && s->sampler.mip != RENDER_MIP_NONE;
        render_mip_t unused;
        draw_filter(m->mag_filter, false, &s->sampler.mag_filter, &unused);
        /* A cube's faces meet without seams */
        s->sampler.address = cube ? 3 : m->address_mode;
        s->sampler.border_color = m->border_color;
        s->texenv = draw_texenv(m->texture_blend);
        render_set_texgen(render, m, world);
    } else {
        s->texture = 0;
        s->texture_kind = RENDER_TEXTURE_NONE;
        render_set_texgen(render, NULL, NULL);
    }
    s->blend = m->alpha_blend;
    if (m->alpha_blend) {
        s->source_blend = m->source_blend;
        s->dest_blend = m->dest_blend;
    }
    s->alpha_test = m->alpha_test;
    if (m->alpha_test) {
        s->alpha_func = m->alpha_func;
        s->alpha_ref = (float)m->alpha_ref / 255.0f;
    }
    if (depth) {
        s->depth_test = true;
        s->depth_func = m->z_func;
        s->depth_write = m->z_write;
    }
    s->cull = !m->two_sided;
    /* RCKMaterial::SetAsCurrent: the render context's wireframe or flat mode over the material's */
    s->flat = render->shading == 1 || m->shade_mode == 1;
    s->fill = render->shading == 0  ? RENDER_FILL_LINE
              : m->fill_mode == 1 ? RENDER_FILL_POINT
              : m->fill_mode == 2 ? RENDER_FILL_LINE
                                  : RENDER_FILL_SOLID;
}

/* The renderer's buffers for lit colors, grown to `count` vertices */
static bool draw_lit_buffers(nmo_render_t *render, int32_t count)
{
    if (count <= render->lit_capacity) {
        return true;
    }
    uint8_t *colors = (uint8_t *)realloc(render->lit_colors, (size_t)count * 4u);
    if (colors != NULL) {
        render->lit_colors = colors;
    }
    uint8_t *speculars = (uint8_t *)realloc(render->lit_speculars, (size_t)count * 4u);
    if (speculars != NULL) {
        render->lit_speculars = speculars;
    }
    if (colors == NULL || speculars == NULL) {
        return false;
    }
    render->lit_capacity = count;
    return true;
}

/* The vertex streams a mesh draw takes its colors from */
typedef struct draw_streams {
    const uint8_t *colors;
    const uint8_t *speculars;
} draw_streams_t;

/*
 * Light the mesh for a material and take the colors from the result; the
 * specular summed when any. False when the buffers could not be made.
 */
static bool draw_lit_colors(nmo_render_t *render, const nmo_engine_frame_t *frame,
                            const nmo_engine_material_t *m, const nmo_vertex_t *vertices,
                            int32_t count, const float world[16], draw_streams_t *streams)
{
    if (!draw_lit_buffers(render, count)) {
        return false;
    }
    render_lighting_t l = render_lighting(frame, m);
    bool specular = render_light_mesh(&l, vertices, count, world, render->lit_colors,
                                      render->lit_speculars);
    streams->colors = render->lit_colors;
    if (specular) {
        render->state.color_sum = true;
        streams->speculars = render->lit_speculars;
    } else {
        streams->speculars = NULL;
        render->state.color_sum = false;
    }
    return true;
}

/* ============================================================================
 * Meshes
 * ============================================================================ */

/* A mesh's group `g` drawn with the streams as they are */
static void draw_mesh_group(nmo_render_t *render, const nmo_engine_mesh_t *mesh,
                            const draw_streams_t *streams, const float *uvs, size_t uv_stride,
                            const uint16_t *indices, int32_t count)
{
    render_geometry_t g;
    memset(&g, 0, sizeof(g));
    g.primitive = RENDER_PRIMITIVE_TRIANGLES;
    g.vertex_count = mesh->vertex_count;
    g.positions = &mesh->vertices[0].position.x;
    g.position_stride = sizeof(nmo_vertex_t);
    g.normals = &mesh->vertices[0].normal.x;
    g.normal_stride = sizeof(nmo_vertex_t);
    g.uvs = uvs;
    g.uv_stride = uv_stride;
    g.colors = streams->colors;
    g.speculars = streams->speculars;
    g.specular_stride = 4;
    g.indices = indices;
    g.index_count = count;
    draw_emit(render, &g);
}

/* The material of a mesh's group as the draw has it; false for none */
static bool draw_group_material(nmo_render_t *render, const nmo_engine_draw_t *draw, int32_t g,
                                nmo_engine_material_t *out)
{
    nmo_object_id_t material_id = nmo_engine_mesh_material(render->engine, draw->mesh, g);
    if (draw->replaced_material != 0 && material_id == draw->replaced_material) {
        material_id = draw->replacing_material;
    }
    return material_id != 0 && nmo_engine_material(render->engine, material_id, out);
}

static void draw_mesh_body(nmo_render_t *render, const nmo_engine_frame_t *frame,
                           const nmo_engine_draw_t *draw, const float view_m[16],
                           nmo_engine_mesh_t mesh, render_mesh_t *cached)
{
    render_state_t *s = &render->state;
    if (draw->fov > 0.0f) {
        render_set_projection(render, frame, false, draw->fov, draw->near_plane, draw->far_plane);
    }
    if (draw->z_only) {
        s->color_write = false; /* VX_MOVEABLE_ZBUFONLY */
    }
    render_multiply(s->model_view, draw->world, view_m);
    const float *uvs = &mesh.vertices[0].uv.x;
    const size_t stride = sizeof(nmo_vertex_t);
    draw_streams_t streams = {NULL, NULL};
    if (draw->stencil_only) {
        /* RCKMesh::Render of a VX_MOVEABLE_STENCILONLY entity: 1 in every stencil bit, nothing else */
        s->blend = false;
        s->alpha_test = false;
        if (s->texture_kind == RENDER_TEXTURE_2D) {
            s->texture_kind = RENDER_TEXTURE_NONE;
            s->texture = 0;
        }
        s->depth_test = true;
        s->depth_write = false;
        s->depth_func = 4; /* VXCMP_LESSEQUAL */
        s->stencil = true;
        s->stencil_func = 8; /* VXCMP_ALWAYS */
        s->stencil_ref = 1;
        s->stencil_mask = 0xFF;
        s->stencil_fail = s->stencil_zfail = 1; /* VXSTENCILOP_KEEP */
        s->stencil_pass = 3;                    /* VXSTENCILOP_REPLACE */
        s->stencil_write_mask = 0xFF;
        s->color_write = false;
        for (int32_t g = 0; g < cached->group_count; g++) {
            if (cached->counts[g] > 0) {
                draw_mesh_group(render, &mesh, &streams, uvs, stride, cached->indices[g],
                                cached->counts[g]);
            }
        }
        s->color_write = true;
        render_draw_state(render, draw);
        return;
    }
    bool prelit = mesh.prelit && cached->colors != NULL;
    if (prelit) {
        streams.colors = cached->colors;
    }
    /* RCKMesh::DefaultRender turns VXRENDERSTATE_SPECULARENABLE on for a prelit mesh: its
       vertices' specular colors are added after texturing */
    bool summed = prelit && cached->speculars != NULL;
    if (summed) {
        s->color_sum = true;
        streams.speculars = cached->speculars;
    }
    /* With a folded channel the device adds the specular after the texture stages */
    bool folded = false;
    for (int32_t c = 0; c < mesh.channel_count && !folded; c++) {
        nmo_engine_material_t channel_material;
        folded = mesh.channels[c].active &&
                 nmo_engine_material(render->engine, mesh.channels[c].material, &channel_material) &&
                 render_channel_folds(&mesh.channels[c], &channel_material, prelit);
    }
    for (int32_t g = 0; g < cached->group_count; g++) {
        if (cached->counts[g] == 0) {
            continue;
        }
        nmo_engine_material_t material;
        bool known = draw_group_material(render, draw, g, &material);
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
        if (!prelit) {
            memcpy(s->color, material.diffuse, sizeof(s->color));
            draw_lit_colors(render, frame, &material, mesh.vertices, mesh.vertex_count, draw->world,
                            &streams);
        }
        render_set_material(render, &material, true, draw->world);
        if (draw->no_z_test) {
            s->depth_test = false;
        }
        if (draw->no_z_write) {
            s->depth_write = false;
        }
        draw_mesh_group(render, &mesh, &streams, uvs, stride, cached->indices[g], cached->counts[g]);
    }
    if (summed || !prelit) {
        streams.speculars = NULL;
        s->color_sum = false;
    }
    if (!prelit) {
        streams.colors = NULL;
    }
    /* RCKMesh::DefaultRender with the global render mode's wireframe: every face again in lines,
       in the vertex colors, untextured and unlit, with the projection's depth translation times
       1.003 */
    if (render->wireframe_layer) {
        float depth_scale = s->depth_scale;
        s->depth_scale = 1.003f;
        s->texture = 0;
        s->texture_kind = RENDER_TEXTURE_NONE;
        render_set_texgen(render, NULL, NULL);
        s->fill = RENDER_FILL_LINE;
        if (cached->colors != NULL) {
            streams.colors = cached->colors;
        } else {
            s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f;
        }
        for (int32_t g = 0; g < cached->group_count; g++) {
            if (cached->counts[g] > 0) {
                draw_mesh_group(render, &mesh, &streams, uvs, stride, cached->indices[g],
                                cached->counts[g]);
            }
        }
        if (!prelit) {
            streams.colors = NULL;
        }
        s->fill = RENDER_FILL_SOLID;
        s->depth_scale = depth_scale;
    }
    /* The material channels, each over the faces it covers */
    for (int32_t c = 0; c < mesh.channel_count; c++) {
        const nmo_engine_channel_t *channel = &mesh.channels[c];
        nmo_engine_material_t material;
        if (!channel->active || !nmo_engine_material(render->engine, channel->material, &material)) {
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
        memcpy(s->color, material.diffuse, sizeof(s->color));
        if (lit) {
            draw_lit_colors(render, frame, &material, mesh.vertices, mesh.vertex_count, draw->world,
                            &streams);
        } else if (!prelit) {
            streams.colors = NULL;
        }
        material.alpha_blend = true;
        material.source_blend = channel->source_blend;
        material.dest_blend = channel->dest_blend;
        material.z_write = false;
        material.z_func = 4;
        render_set_material(render, &material, true, draw->world);
        if (count > 0) {
            if (channel->uvs != NULL) {
                draw_mesh_group(render, &mesh, &streams, channel->uvs, 2 * sizeof(float), indices, count);
            } else {
                draw_mesh_group(render, &mesh, &streams, uvs, stride, indices, count);
            }
        }
        if (lit) {
            streams.speculars = NULL;
            s->color_sum = false;
            streams.colors = NULL;
        }
        free(indices);
    }
    /* The specular the folded channels left out, added over what they drew */
    for (int32_t g = 0; folded && g < cached->group_count; g++) {
        nmo_engine_material_t material;
        if (cached->counts[g] == 0 || !draw_group_material(render, draw, g, &material) ||
            !(material.power > 0.05f)) {
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
        draw_lit_colors(render, frame, &material, mesh.vertices, mesh.vertex_count, draw->world,
                        &streams);
        render_set_material(render, &material, true, draw->world);
        draw_mesh_group(render, &mesh, &streams, uvs, stride, cached->indices[g], cached->counts[g]);
        streams.speculars = NULL;
        s->color_sum = false;
    }
    s->color_write = true;
    if (draw->fov > 0.0f) {
        render_set_projection(render, frame, frame->orthographic, frame->fov, frame->near_plane,
                              frame->far_plane);
    }
}

static void draw_mesh_as(nmo_render_t *render, const nmo_engine_frame_t *frame,
                         const nmo_engine_draw_t *draw, const float view_m[16])
{
    nmo_engine_mesh_t mesh;
    if (!nmo_engine_mesh(render->engine, draw->mesh, &mesh) || mesh.vertices == NULL ||
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
            draw_mesh_body(render, frame, draw, view_m, mesh, &faces);
        }
        render_mesh_release(&faces);
        free(faces.colors);
        free(faces.speculars);
        render_wrapped_free(&wrapped);
        return;
    }
    render_mesh_t *cached = render_mesh_cache_get(&render->meshes, draw->mesh, &mesh);
    if (cached == NULL || cached->indices == NULL) {
        return;
    }
    draw_mesh_body(render, frame, draw, view_m, mesh, cached);
}

/* A mesh draw; one with `wireframe` under the wireframe global render mode for itself */
void render_draw_mesh(nmo_render_t *render, const nmo_engine_frame_t *frame,
                      const nmo_engine_draw_t *draw, const float view_m[16])
{
    int32_t shading = render->shading;
    if (draw->wireframe) {
        render->shading = 0;
    }
    draw_mesh_as(render, frame, draw, view_m);
    render->shading = shading;
}

/* ============================================================================
 * 2D
 * ============================================================================ */

/* RCK2dEntity::Draw: a quad of the material's diffuse, textured with its source rect */
void render_draw_quad(nmo_render_t *render, const nmo_engine_draw_t *draw)
{
    render_state_t *s = &render->state;
    nmo_engine_material_t material;
    if (draw->material != 0 && nmo_engine_material(render->engine, draw->material, &material)) {
        render_set_material(render, &material, false, NULL);
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
        render_set_material(render, &material, false, NULL);
    }
    s->cull = false;
    s->flat = false;
    float colors[16];
    render_color(draw->color, &colors[0]);
    render_color(draw->color, &colors[4]);
    render_color(draw->color2, &colors[8]);
    render_color(draw->color2, &colors[12]);
    bool summed = (draw->specular & 0xFFFFFFu) != 0;
    if (summed) { /* VXRENDERSTATE_SPECULARENABLE and the vertices' specular color */
        s->color_sum = true;
        s->secondary[0] = (uint8_t)(draw->specular >> 16);
        s->secondary[1] = (uint8_t)(draw->specular >> 8);
        s->secondary[2] = (uint8_t)draw->specular;
    }
    const float positions[12] = {draw->rect[0] + draw->skew, draw->rect[1], 0.0f,
                                 draw->rect[2] + draw->skew, draw->rect[1], 0.0f,
                                 draw->rect[2], draw->rect[3], 0.0f,
                                 draw->rect[0], draw->rect[3], 0.0f};
    const float uvs[8] = {draw->uv[0], draw->uv[1], draw->uv[2], draw->uv[1],
                          draw->uv[2], draw->uv[3], draw->uv[0], draw->uv[3]};
    render_geometry_t g;
    memset(&g, 0, sizeof(g));
    g.primitive = RENDER_PRIMITIVE_TRIANGLE_FAN;
    g.vertex_count = 4;
    g.positions = positions;
    g.position_stride = 3 * sizeof(float);
    g.uvs = uvs;
    g.uv_stride = 2 * sizeof(float);
    g.float_colors = colors;
    draw_emit(render, &g);
    if (summed) {
        s->secondary[0] = s->secondary[1] = s->secondary[2] = 0;
        s->color_sum = false;
    }
}

/* A sprite text: its sprite's pixels, blended over the frame without Z */
void render_draw_sprite_text(nmo_render_t *render, const nmo_engine_draw_t *draw)
{
    render_backend_t *backend = render->backend;
    render_text_sprite_t *sprite = render_text_sprite(render->text, draw);
    if (sprite == NULL) {
        return;
    }
    if (sprite->handle == 0) {
        sprite->handle = backend->ops->texture_create(backend);
        if (sprite->handle == 0) {
            return;
        }
        backend->ops->texture_image(backend, sprite->handle, RENDER_TEXTURE_2D, 0, 0, sprite->width,
                                    sprite->height, sprite->rgba);
        backend->ops->texture_levels(backend, sprite->handle, RENDER_TEXTURE_2D, 0);
    }
    render_state_t *s = &render->state;
    s->depth_test = false;
    s->depth_write = false;
    s->cull = false;
    s->alpha_test = false;
    s->fog = false;
    s->blend = true;
    s->source_blend = 5; /* VXBLEND_SRCALPHA */
    s->dest_blend = 6;   /* VXBLEND_INVSRCALPHA */
    s->texture = sprite->handle;
    s->texture_kind = RENDER_TEXTURE_2D;
    s->sampler.min_filter = s->sampler.mag_filter = RENDER_FILTER_NEAREST;
    s->sampler.mip = RENDER_MIP_NONE;
    s->sampler.address = 1; /* VXTEXTURE_ADDRESSWRAP */
    s->sampler.anisotropic = false;
    s->texenv = RENDER_TEXENV_MODULATE;
    render_set_texgen(render, NULL, NULL);
    s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f;
    const float positions[12] = {draw->rect[0], draw->rect[1], 0.0f, draw->rect[2], draw->rect[1], 0.0f,
                                 draw->rect[2], draw->rect[3], 0.0f, draw->rect[0], draw->rect[3], 0.0f};
    const float uvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    render_geometry_t g;
    memset(&g, 0, sizeof(g));
    g.primitive = RENDER_PRIMITIVE_TRIANGLE_FAN;
    g.vertex_count = 4;
    g.positions = positions;
    g.position_stride = 3 * sizeof(float);
    g.uvs = uvs;
    g.uv_stride = 2 * sizeof(float);
    draw_emit(render, &g);
}

/* ============================================================================
 * The frame's vertices
 * ============================================================================ */

/* The frame's vertices of a draw, in their own colors */
static void draw_frame_vertices(nmo_render_t *render, const nmo_engine_frame_t *frame,
                                const nmo_engine_draw_t *draw, render_primitive_t primitive)
{
    const nmo_engine_vertex_t *v = &frame->vertices[draw->first_vertex];
    float *colors = (float *)malloc((size_t)draw->vertex_count * 4u * sizeof(float));
    if (colors == NULL) {
        return;
    }
    for (int32_t i = 0; i < draw->vertex_count; i++) {
        render_color(v[i].color, &colors[i * 4]);
    }
    render_geometry_t g;
    memset(&g, 0, sizeof(g));
    g.primitive = primitive;
    g.vertex_count = draw->vertex_count;
    g.positions = &v[0].x;
    g.position_stride = sizeof(nmo_engine_vertex_t);
    g.uvs = &v[0].u;
    g.uv_stride = sizeof(nmo_engine_vertex_t);
    g.float_colors = colors;
    draw_emit(render, &g);
    free(colors);
}

/* A particle system's triangles (ParticleEmitter::SetState) or lines (RenderParticles_L);
   smooth shading, which only an Orientable Sprite's colors need */
void render_draw_triangles(nmo_render_t *render, const nmo_engine_frame_t *frame,
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
    render_set_material(render, &material, true, NULL);
    memcpy(render->state.model_view, view_m, sizeof(render->state.model_view));
    draw_frame_vertices(render, frame, draw,
                        lines ? RENDER_PRIMITIVE_LINES : RENDER_PRIMITIVE_TRIANGLES);
}

/* A 3D sprite: its quad with its material set as current, unlit (CallSprite3DBatches) */
void render_draw_sprite3d(nmo_render_t *render, const nmo_engine_frame_t *frame,
                          const nmo_engine_draw_t *draw, const float view_m[16])
{
    nmo_engine_material_t material;
    if (draw->vertex_count <= 0 || frame->vertices == NULL ||
        !nmo_engine_material(render->engine, draw->material, &material)) {
        return;
    }
    render_set_material(render, &material, true, NULL);
    memcpy(render->state.model_view, view_m, sizeof(render->state.model_view));
    draw_frame_vertices(render, frame, draw, RENDER_PRIMITIVE_TRIANGLES);
}

/* A block's DrawPrimitive in a render callback: its material set as current (none: untextured,
   opaque), unlit in the vertices' colors or lit with their normals */
void render_draw_primitive(nmo_render_t *render, const nmo_engine_frame_t *frame,
                           const nmo_engine_draw_t *draw, const float view_m[16])
{
    render_state_t *s = &render->state;
    nmo_engine_material_t material;
    if (draw->vertex_count <= 0 || frame->vertices == NULL) {
        return;
    }
    if (draw->material == 0 || !nmo_engine_material(render->engine, draw->material, &material)) {
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
    render_set_material(render, &material, true, NULL);
    render_lighting_t lighting = render_lighting(frame, &material);
    if (draw->lit && lighting.specular) {
        s->color_sum = true;
    }
    /* The projection's depth translation scaled: a block's trick against z fighting */
    float depth_scale = s->depth_scale;
    if (draw->projection_z_scale != 0.0f) {
        s->depth_scale = depth_scale != 0.0f ? depth_scale * draw->projection_z_scale
                                             : draw->projection_z_scale;
    }
    bool fogged = (draw->no_fog || draw->screen) && s->fog;
    if (fogged) {
        s->fog = false;
    }
    render_projection_kind_t projection_kind = s->projection_kind;
    float projection_rect[4];
    memcpy(projection_rect, s->projection_rect, sizeof(projection_rect));
    if (draw->screen) {
        /* The viewport's pixels, its depth z itself */
        const int32_t *vp = frame->viewport;
        float left = 0.0f, top = 0.0f, width = (float)frame->width, height = (float)frame->height;
        if (vp[2] > 0 && vp[3] > 0) {
            left = (float)vp[0], top = (float)vp[1], width = (float)vp[2], height = (float)vp[3];
        }
        s->projection_kind = RENDER_PROJECTION_SCREEN;
        s->projection_rect[0] = left;
        s->projection_rect[1] = top;
        s->projection_rect[2] = width;
        s->projection_rect[3] = height;
        memcpy(s->model_view, draw_identity, sizeof(s->model_view));
    } else {
        memcpy(s->model_view, view_m, sizeof(s->model_view));
    }
    const nmo_engine_vertex_t *v = &frame->vertices[draw->first_vertex];
    render_geometry_t g;
    memset(&g, 0, sizeof(g));
    g.primitive = draw->lines ? RENDER_PRIMITIVE_LINES : RENDER_PRIMITIVE_TRIANGLES;
    g.vertex_count = draw->vertex_count;
    g.positions = &v[0].x;
    g.position_stride = sizeof(nmo_engine_vertex_t);
    g.uvs = &v[0].u;
    g.uv_stride = sizeof(nmo_engine_vertex_t);
    uint8_t *bytes = NULL;
    float *colors = NULL;
    if (draw->lit) {
        /* in the world already: lit as they are */
        bytes = (uint8_t *)malloc((size_t)draw->vertex_count * 8u);
        for (int32_t i = 0; bytes != NULL && i < draw->vertex_count; i++) {
            const float p[3] = {v[i].x, v[i].y, v[i].z};
            float n[3] = {v[i].nx, v[i].ny, v[i].nz};
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0.0f) {
                n[0] /= len, n[1] /= len, n[2] /= len;
            }
            render_light_vertex(&lighting, p, n, &bytes[i * 4],
                                &bytes[(size_t)draw->vertex_count * 4u + (size_t)i * 4u]);
        }
        if (bytes != NULL) {
            g.colors = bytes;
            g.speculars = bytes + (size_t)draw->vertex_count * 4u;
            g.specular_stride = 4;
        }
    } else {
        colors = (float *)malloc((size_t)draw->vertex_count * 4u * sizeof(float));
        for (int32_t i = 0; colors != NULL && i < draw->vertex_count; i++) {
            render_color(v[i].color, &colors[i * 4]);
        }
        g.float_colors = colors;
    }
    if (g.colors != NULL || g.float_colors != NULL) {
        draw_emit(render, &g);
    }
    if (g.speculars != NULL) { /* the device keeps the last vertex's */
        memcpy(s->secondary, &g.speculars[(size_t)(draw->vertex_count - 1) * 4u], 3);
    }
    free(bytes);
    free(colors);
    s->color_sum = false;
    if (fogged) {
        s->fog = true;
    }
    if (draw->screen) {
        s->projection_kind = projection_kind;
        memcpy(s->projection_rect, projection_rect, sizeof(projection_rect));
    }
    s->depth_scale = depth_scale;
}
