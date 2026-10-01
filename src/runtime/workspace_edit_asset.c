/**
 * @file workspace_edit_asset.c
 * @brief Asset edits: materials, textures, meshes.
 */

#include "object/nmo_asset_edit.h"
#include "runtime_internal.h"
#include "workspace_edit_journal_internal.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_stb_adapter.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "workspace_edit_internal.h"

/* RCKMesh constructor: a new mesh is visible and renders its channels. A
 * mesh without VXMESH_VISIBLE is skipped by RCK3dEntity::Render. */
#define WORKSPACE_NEW_MESH_FLAGS ((uint32_t)(VXMESH_VISIBLE | VXMESH_RENDERCHANNELS))
/* RCKMesh::SetFaceCount gives every new face the channel mask 0xFFFF. */
#define WORKSPACE_NEW_FACE_CHANNEL_MASK 0xFFFFu

static nmo_status_t workspace_edit_alloc_cube_mesh(
    nmo_arena_t *arena,
    nmo_vertex_t **out_vertices,
    nmo_face_t **out_faces,
    uint16_t **out_indices,
    uint32_t **out_vertex_colors,
    uint32_t **out_vertex_specular,
    nmo_material_group_t **out_groups)
{
    if (arena == NULL || out_vertices == NULL || out_faces == NULL ||
        out_indices == NULL || out_vertex_colors == NULL ||
        out_vertex_specular == NULL || out_groups == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_vertex_t *vertices = (nmo_vertex_t *)nmo_arena_alloc(
        arena,
        sizeof(*vertices) * 8u,
        _Alignof(nmo_vertex_t));
    nmo_face_t *faces = (nmo_face_t *)nmo_arena_alloc(
        arena,
        sizeof(*faces) * 12u,
        _Alignof(nmo_face_t));
    uint16_t *indices = (uint16_t *)nmo_arena_alloc(
        arena,
        sizeof(*indices) * 36u,
        _Alignof(uint16_t));
    uint32_t *vertex_colors = (uint32_t *)nmo_arena_alloc(
        arena,
        sizeof(*vertex_colors) * 8u,
        _Alignof(uint32_t));
    uint32_t *vertex_specular = (uint32_t *)nmo_arena_alloc(
        arena,
        sizeof(*vertex_specular) * 8u,
        _Alignof(uint32_t));
    nmo_material_group_t *groups = (nmo_material_group_t *)nmo_arena_alloc(
        arena,
        sizeof(*groups),
        _Alignof(nmo_material_group_t));
    if (vertices == NULL || faces == NULL || indices == NULL ||
        vertex_colors == NULL || vertex_specular == NULL || groups == NULL) {
        return NMO_ERR_NOMEM;
    }

    static const float coords[8][3] = {
        {-0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
    };
    static const uint16_t cube_indices[36] = {
        0u, 2u, 1u, 0u, 3u, 2u,
        4u, 5u, 6u, 4u, 6u, 7u,
        0u, 1u, 5u, 0u, 5u, 4u,
        3u, 6u, 2u, 3u, 7u, 6u,
        1u, 2u, 6u, 1u, 6u, 5u,
        0u, 4u, 7u, 0u, 7u, 3u,
    };

    for (size_t i = 0; i < 8u; ++i) {
        vertices[i].position.x = coords[i][0];
        vertices[i].position.y = coords[i][1];
        vertices[i].position.z = coords[i][2];
        vertices[i].normal.x = coords[i][0] * 1.1547005f;
        vertices[i].normal.y = coords[i][1] * 1.1547005f;
        vertices[i].normal.z = coords[i][2] * 1.1547005f;
        vertices[i].uv.x = 0.0f;
        vertices[i].uv.y = 0.0f;
        vertex_colors[i] = 0xFFFFFFFFu;
        vertex_specular[i] = 0xFF000000u;
    }
    memset(faces, 0, sizeof(*faces) * 12u);
    for (size_t face_index = 0; face_index < 12u; ++face_index) {
        faces[face_index].channel_mask = WORKSPACE_NEW_FACE_CHANNEL_MASK;
    }
    memcpy(indices, cube_indices, sizeof(cube_indices));
    memset(groups, 0, sizeof(*groups));

    *out_vertices = vertices;
    *out_faces = faces;
    *out_indices = indices;
    *out_vertex_colors = vertex_colors;
    *out_vertex_specular = vertex_specular;
    *out_groups = groups;
    return NMO_OK;
}

typedef struct workspace_obj_mesh_slot {
    int32_t pos_idx;
    int32_t uv_idx;
    int32_t normal_idx;
    uint16_t idx_plus1;
} workspace_obj_mesh_slot_t;

typedef struct workspace_obj_mesh_counts {
    size_t face_vertex_count;
    size_t line_vertex_count;
    size_t max_vertices;
    size_t vertex_bytes;
    size_t vertex_color_bytes;
    size_t vertex_specular_bytes;
    size_t face_bytes;
    size_t face_index_bytes;
    size_t line_index_bytes;
    size_t dedup_capacity;
    size_t dedup_bytes;
} workspace_obj_mesh_counts_t;

static nmo_status_t workspace_edit_require_object_class(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id);

static nmo_status_t workspace_edit_find_typed_object(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    nmo_object_t **out_object,
    void **out_state)
{
    if (out_object != NULL) {
        *out_object = NULL;
    }
    if (out_state != NULL) {
        *out_state = NULL;
    }
    if (edit == NULL || edit->finished || object_id == 0 || out_state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(edit->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    void *state = NULL;
    if (nmo_guid_is_null(nmo_object_get_type_guid(object)) &&
        nmo_object_get_class_id(object) == class_id) {
        state = nmo_object_get_state(object);
    } else {
        const nmo_type_registry_t *registry = workspace_edit_type_registry(edit);
        if (!workspace_edit_session_object_derives(registry, object, class_id)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        const nmo_type_descriptor_t *base_type =
            nmo_type_query_find_by_class_id(registry, class_id);
        if (base_type == NULL) {
            return NMO_ERR_INVALID_STATE;
        }
        state = nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, base_type->guid);
    }
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (out_object != NULL) {
        *out_object = object;
    }
    *out_state = state;
    return NMO_OK;
}

static nmo_status_t workspace_edit_find_typed_state(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    void **out_state)
{
    return workspace_edit_find_typed_object(
        edit,
        object_id,
        class_id,
        NULL,
        out_state);
}

static nmo_status_t workspace_edit_snapshot_typed_state(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id,
    size_t state_size,
    void **out_state)
{
    if (out_state != NULL) {
        *out_state = NULL;
    }
    if (out_state == NULL || state_size == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    void *state = NULL;
    nmo_status_t status =
        workspace_edit_find_typed_state(edit, object_id, class_id, &state);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_workspace_edit_snapshot_bytes(edit, state, state_size);
    if (status != NMO_OK) {
        return status;
    }

    *out_state = state;
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_material_color(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    float r,
    float g,
    float b,
    float a)
{
    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_snapshot_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        sizeof(*state),
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    uint32_t color = workspace_edit_pack_argb(r, g, b, a);
    state->diffuse_color = color;
    state->ambient_color = color;
    state->specular_color = 0xFFFFFFFFu;
    state->emissive_color = 0xFF000000u;
    state->specular_power = 0.0f;

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_material_channels(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const nmo_asset_material_channels_t *channels)
{
    if (channels == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_snapshot_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        sizeof(*state),
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    if (channels->has_diffuse) {
        state->diffuse_color = workspace_edit_pack_argb(
            channels->diffuse[0],
            channels->diffuse[1],
            channels->diffuse[2],
            channels->diffuse[3]);
    }
    if (channels->has_ambient) {
        state->ambient_color = workspace_edit_pack_argb(
            channels->ambient[0],
            channels->ambient[1],
            channels->ambient[2],
            channels->ambient[3]);
    }
    if (channels->has_specular) {
        state->specular_color = workspace_edit_pack_argb(
            channels->specular[0],
            channels->specular[1],
            channels->specular[2],
            channels->specular[3]);
    }
    if (channels->has_emissive) {
        state->emissive_color = workspace_edit_pack_argb(
            channels->emissive[0],
            channels->emissive[1],
            channels->emissive[2],
            channels->emissive[3]);
    }
    if (channels->has_specular_power) {
        state->specular_power = channels->specular_power;
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_material_render_flags(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const nmo_asset_material_render_flags_t *flags)
{
    if (flags == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_snapshot_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        sizeof(*state),
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    uint32_t packed_modes = state->packed_modes;
    uint32_t packed_flags = state->packed_flags;
    if (flags->has_texture_blend) {
        packed_modes = (packed_modes & ~0xFu) |
                       ((uint32_t)flags->texture_blend & 0xFu);
    }
    if (flags->has_min_filter) {
        packed_modes = (packed_modes & ~(0xFu << 4)) |
                       (((uint32_t)flags->min_filter & 0xFu) << 4);
    }
    if (flags->has_mag_filter) {
        packed_modes = (packed_modes & ~(0xFu << 8)) |
                       (((uint32_t)flags->mag_filter & 0xFu) << 8);
    }
    if (flags->has_source_blend) {
        packed_modes = (packed_modes & ~(0xFu << 12)) |
                       (((uint32_t)flags->source_blend & 0xFu) << 12);
    }
    if (flags->has_destination_blend) {
        packed_modes = (packed_modes & ~(0xFu << 16)) |
                       (((uint32_t)flags->destination_blend & 0xFu) << 16);
    }
    if (flags->has_wrap) {
        packed_modes = (packed_modes & ~(0xFu << 28)) |
                       (((uint32_t)flags->wrap & 0xFu) << 28);
    }
    /* The blend factors only apply while the material's alpha blend bit (bit 3
       of the low flag byte) is set, and the alpha function only while its
       alpha test bit (bit 4) is: RCKMaterial::SetAsCurrent ignores them
       otherwise. Setting either one here is a request to use it. */
    if (flags->has_source_blend || flags->has_destination_blend) {
        packed_flags |= 0x08u;
    }
    if (flags->has_alpha_func) {
        /* The file stores the function in four bits. */
        packed_flags = (packed_flags & ~(0xFu << 16)) |
                       (((uint32_t)flags->alpha_func & 0xFu) << 16);
        if (flags->alpha_func != VXCMP_ALWAYS) {
            packed_flags |= 0x10u;
        }
    }

    state->packed_modes = packed_modes;
    state->packed_flags = packed_flags;
    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_texture_rgba(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t texture_id,
    const void *rgba_pixels,
    uint32_t width,
    uint32_t height)
{
    if (edit == NULL || texture_id == 0u || rgba_pixels == NULL ||
        width == 0u || height == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_texture_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_state(
        edit,
        texture_id,
        NMO_CID_TEXTURE,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    nmo_arena_t *arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_texture_replace_bitmap(state, arena, rgba_pixels, width, height);
    if (status != NMO_OK) {
        return status;
    }

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_RESOURCES);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_bind_material_texture(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    nmo_object_id_t texture_id,
    uint32_t slot)
{
    if (slot >= 4u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_material_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_state(
        edit,
        material_id,
        NMO_CID_MATERIAL,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }
    status = workspace_edit_require_object_class(edit, texture_id, NMO_CID_TEXTURE);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    nmo_material_set_texture_id(state, slot, texture_id);
    state->has_additional_textures =
        (nmo_material_texture_id(state, 1) != NMO_OBJECT_ID_NONE ||
         nmo_material_texture_id(state, 2) != NMO_OBJECT_ID_NONE ||
         nmo_material_texture_id(state, 3) != NMO_OBJECT_ID_NONE)
            ? 1u
            : 0u;
    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

static nmo_status_t workspace_edit_require_object_class(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    nmo_class_id_t class_id)
{
    void *state = NULL;
    return workspace_edit_find_typed_state(edit, object_id, class_id, &state);
}

nmo_status_t nmo_asset_edit_bind_entity_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t entity_id,
    nmo_object_id_t mesh_id)
{
    nmo_3dentity_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_state(
        edit,
        entity_id,
        NMO_CID_3DENTITY,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }
    status = workspace_edit_require_object_class(edit, mesh_id, NMO_CID_MESH);
    if (status != NMO_OK) {
        return status;
    }

    nmo_arena_t *arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_ref_t *mesh_ids = (nmo_ref_t *)nmo_arena_alloc(
        arena,
        sizeof(*mesh_ids),
        _Alignof(nmo_ref_t));
    if (mesh_ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    mesh_ids[0] = nmo_ref_from_id(mesh_id);

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }

    state->current_mesh = nmo_ref_from_id(mesh_id);
    state->mesh_count = 1u;
    state->mesh_ids = mesh_ids;
    state->has_mesh_chunk = 1u;

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

static bool workspace_obj_mesh_checked_mul(
    size_t count,
    size_t elem_size,
    size_t *out_size)
{
    if (out_size == NULL || elem_size == 0u) {
        return false;
    }
    if (count > SIZE_MAX / elem_size) {
        return false;
    }
    *out_size = count * elem_size;
    return true;
}

static uint32_t workspace_obj_mesh_rgb_to_argb(const float *rgb)
{
    uint32_t r = workspace_edit_float_color_channel(rgb[0]);
    uint32_t g = workspace_edit_float_color_channel(rgb[1]);
    uint32_t b = workspace_edit_float_color_channel(rgb[2]);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static size_t workspace_obj_mesh_tuple_hash(
    int32_t pos_idx,
    int32_t uv_idx,
    int32_t normal_idx,
    size_t capacity)
{
    uint64_t h = 1469598103934665603ULL;
    h ^= (uint32_t)pos_idx;
    h *= 1099511628211ULL;
    h ^= (uint32_t)uv_idx;
    h *= 1099511628211ULL;
    h ^= (uint32_t)normal_idx;
    h *= 1099511628211ULL;
    return (size_t)(h % capacity);
}

static nmo_status_t workspace_obj_mesh_validate_material_id(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id)
{
    if (material_id == 0 || material_id == NMO_OBJECT_ID_NONE) {
        return NMO_OK;
    }
    return workspace_edit_require_object_class(edit, material_id, NMO_CID_MATERIAL);
}

static nmo_status_t workspace_obj_mesh_validate_materials(
    nmo_workspace_edit_t *edit,
    const nmo_asset_mesh_import_options_t *options)
{
    if (options == NULL) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(workspace_obj_mesh_validate_material_id(
        edit,
        options->default_material_id));
    if (options->material_count > 0 && options->materials == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < options->material_count; ++i) {
        NMO_RETURN_IF_ERROR(workspace_obj_mesh_validate_material_id(
            edit,
            options->materials[i].material_id));
    }
    return NMO_OK;
}

static nmo_object_id_t workspace_obj_mesh_material_for_name(
    const nmo_asset_mesh_import_options_t *options,
    const char *name)
{
    if (options == NULL) {
        return NMO_OBJECT_ID_NONE;
    }
    if (name != NULL && options->materials != NULL) {
        for (size_t i = 0; i < options->material_count; ++i) {
            if (options->materials[i].name != NULL &&
                strcmp(options->materials[i].name, name) == 0) {
                return options->materials[i].material_id;
            }
        }
    }
    return options->default_material_id;
}

static nmo_status_t workspace_obj_mesh_get_or_add_vertex(
    const nmo_obj_data_t *obj_data,
    const nmo_obj_face_vertex_t *fv,
    nmo_vertex_t *vertices,
    uint32_t *vertex_colors,
    uint32_t *vertex_specular,
    workspace_obj_mesh_slot_t *dedup_table,
    size_t dedup_capacity,
    uint32_t *unique_count,
    uint16_t *out_index)
{
    int32_t pi = fv->pos_idx;
    int32_t ui = fv->uv_idx;
    int32_t ni = fv->normal_idx;
    size_t slot = workspace_obj_mesh_tuple_hash(pi, ui, ni, dedup_capacity);

    for (;;) {
        workspace_obj_mesh_slot_t *entry = &dedup_table[slot];
        if (entry->idx_plus1 == 0) {
            if (*unique_count >= 65535u) {
                return NMO_ERR_INVALID_ARGUMENT;
            }

            nmo_vertex_t *vertex = &vertices[*unique_count];
            memset(vertex, 0, sizeof(*vertex));

            if (pi >= 0 && (size_t)pi < obj_data->pos_count) {
                vertex->position.x = obj_data->positions[(size_t)pi * 3u + 0u];
                vertex->position.y = obj_data->positions[(size_t)pi * 3u + 1u];
                vertex->position.z = obj_data->positions[(size_t)pi * 3u + 2u];
            }
            if (ui >= 0 && (size_t)ui < obj_data->uv_count) {
                vertex->uv.x = obj_data->uvs[(size_t)ui * 2u + 0u];
                vertex->uv.y = obj_data->uvs[(size_t)ui * 2u + 1u];
            }
            if (ni >= 0 && (size_t)ni < obj_data->normal_count) {
                vertex->normal.x = obj_data->normals[(size_t)ni * 3u + 0u];
                vertex->normal.y = obj_data->normals[(size_t)ni * 3u + 1u];
                vertex->normal.z = obj_data->normals[(size_t)ni * 3u + 2u];
            }

            uint32_t color = 0xFFFFFFFFu;
            if (pi >= 0 && obj_data->position_has_color != NULL &&
                obj_data->colors != NULL &&
                (size_t)pi < obj_data->pos_count &&
                obj_data->position_has_color[pi]) {
                color = workspace_obj_mesh_rgb_to_argb(
                    &obj_data->colors[(size_t)pi * 3u]);
            }
            vertex_colors[*unique_count] = color;
            vertex_specular[*unique_count] = 0xFF000000u;

            entry->pos_idx = pi;
            entry->uv_idx = ui;
            entry->normal_idx = ni;
            entry->idx_plus1 = (uint16_t)(*unique_count + 1u);
            *out_index = (uint16_t)*unique_count;
            (*unique_count)++;
            return NMO_OK;
        }

        if (entry->pos_idx == pi &&
            entry->uv_idx == ui &&
            entry->normal_idx == ni) {
            *out_index = (uint16_t)(entry->idx_plus1 - 1u);
            return NMO_OK;
        }

        slot = (slot + 1u) % dedup_capacity;
    }
}

static uint32_t workspace_obj_mesh_material_group_count(
    const nmo_obj_data_t *obj_data,
    uint32_t *out_material_offset)
{
    bool has_unassigned = false;
    for (size_t i = 0; i < obj_data->face_count; ++i) {
        if (obj_data->faces[i].material_group == NMO_OBJ_NO_MATERIAL) {
            has_unassigned = true;
            break;
        }
    }
    uint32_t offset = has_unassigned ? 1u : 0u;
    uint32_t count = (uint32_t)obj_data->material_name_count + offset;
    if (obj_data->face_count > 0 && count == 0) {
        count = 1u;
        offset = 1u;
    }
    *out_material_offset = offset;
    return count;
}

static nmo_status_t workspace_obj_mesh_compute_counts(
    const nmo_obj_data_t *obj_data,
    workspace_obj_mesh_counts_t *out_counts)
{
    if (obj_data == NULL || out_counts == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (obj_data->face_count > SIZE_MAX / 3u ||
        obj_data->line_count > SIZE_MAX / 2u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_obj_mesh_counts_t counts = {0};
    counts.face_vertex_count = obj_data->face_count * 3u;
    counts.line_vertex_count = obj_data->line_count * 2u;
    if (counts.face_vertex_count > SIZE_MAX - counts.line_vertex_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    counts.max_vertices = counts.face_vertex_count + counts.line_vertex_count;
    if (counts.max_vertices == 0 || counts.max_vertices > 65535u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!workspace_obj_mesh_checked_mul(
            counts.max_vertices,
            sizeof(nmo_vertex_t),
            &counts.vertex_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.max_vertices,
            sizeof(uint32_t),
            &counts.vertex_color_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.max_vertices,
            sizeof(uint32_t),
            &counts.vertex_specular_bytes) ||
        !workspace_obj_mesh_checked_mul(
            obj_data->face_count,
            sizeof(nmo_face_t),
            &counts.face_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.face_vertex_count,
            sizeof(uint16_t),
            &counts.face_index_bytes) ||
        !workspace_obj_mesh_checked_mul(
            counts.line_vertex_count,
            sizeof(uint16_t),
            &counts.line_index_bytes)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (counts.max_vertices > SIZE_MAX / 2u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    counts.dedup_capacity = counts.max_vertices * 2u;
    if (counts.dedup_capacity < 64u) {
        counts.dedup_capacity = 64u;
    }
    if (!workspace_obj_mesh_checked_mul(
            counts.dedup_capacity,
            sizeof(workspace_obj_mesh_slot_t),
            &counts.dedup_bytes)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_counts = counts;
    return NMO_OK;
}

static nmo_status_t workspace_obj_mesh_build(
    nmo_arena_t *scratch,
    nmo_arena_t *document_arena,
    const nmo_obj_data_t *obj_data,
    const nmo_asset_mesh_import_options_t *options,
    nmo_mesh_state_t *out_state)
{
    if (scratch == NULL || document_arena == NULL ||
        obj_data == NULL || out_state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (obj_data->face_count == 0 && obj_data->line_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((obj_data->face_count > 0 && obj_data->faces == NULL) ||
        (obj_data->line_count > 0 && obj_data->lines == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (obj_data->material_name_count > UINT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    workspace_obj_mesh_counts_t counts = {0};
    NMO_RETURN_IF_ERROR(workspace_obj_mesh_compute_counts(obj_data, &counts));

    uint32_t material_offset = 0;
    uint32_t material_group_count =
        workspace_obj_mesh_material_group_count(obj_data, &material_offset);
    if (material_group_count > UINT16_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_vertex_t *vertices = (nmo_vertex_t *)nmo_arena_alloc(
        document_arena,
        counts.vertex_bytes,
        _Alignof(nmo_vertex_t));
    uint32_t *vertex_colors = (uint32_t *)nmo_arena_alloc(
        document_arena,
        counts.vertex_color_bytes,
        _Alignof(uint32_t));
    uint32_t *vertex_specular = (uint32_t *)nmo_arena_alloc(
        document_arena,
        counts.vertex_specular_bytes,
        _Alignof(uint32_t));
    if (vertices == NULL || vertex_colors == NULL || vertex_specular == NULL) {
        return NMO_ERR_NOMEM;
    }

    nmo_face_t *faces = NULL;
    uint16_t *face_indices = NULL;
    if (obj_data->face_count > 0) {
        faces = (nmo_face_t *)nmo_arena_alloc(
            document_arena,
            counts.face_bytes,
            _Alignof(nmo_face_t));
        face_indices = (uint16_t *)nmo_arena_alloc(
            document_arena,
            counts.face_index_bytes,
            _Alignof(uint16_t));
        if (faces == NULL || face_indices == NULL) {
            return NMO_ERR_NOMEM;
        }
    }

    uint16_t *line_indices = NULL;
    if (obj_data->line_count > 0) {
        line_indices = (uint16_t *)nmo_arena_alloc(
            document_arena,
            counts.line_index_bytes,
            _Alignof(uint16_t));
        if (line_indices == NULL) {
            return NMO_ERR_NOMEM;
        }
    }

    nmo_material_group_t *material_groups = NULL;
    if (material_group_count > 0) {
        material_groups = (nmo_material_group_t *)nmo_arena_alloc(
            document_arena,
            material_group_count * sizeof(*material_groups),
            _Alignof(nmo_material_group_t));
        if (material_groups == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(material_groups, 0, material_group_count * sizeof(*material_groups));
        for (uint32_t i = 0; i < material_group_count; ++i) {
            material_groups[i].material = nmo_ref_from_id(
                options != NULL ? options->default_material_id :
                                  NMO_OBJECT_ID_NONE);
        }
        for (size_t i = 0; i < obj_data->material_name_count; ++i) {
            uint32_t group_index = (uint32_t)i + material_offset;
            if (group_index < material_group_count) {
                material_groups[group_index].material = nmo_ref_from_id(
                    workspace_obj_mesh_material_for_name(
                        options,
                        obj_data->material_names != NULL
                            ? obj_data->material_names[i]
                            : NULL));
            }
        }
    }

    workspace_obj_mesh_slot_t *dedup_table =
        (workspace_obj_mesh_slot_t *)nmo_arena_alloc(
            scratch,
            counts.dedup_bytes,
            _Alignof(workspace_obj_mesh_slot_t));
    if (dedup_table == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(dedup_table, 0, counts.dedup_bytes);

    uint32_t unique_count = 0;
    for (size_t face_index = 0; face_index < obj_data->face_count; ++face_index) {
        const nmo_obj_face_t *obj_face = &obj_data->faces[face_index];
        for (size_t vertex_index = 0; vertex_index < 3u; ++vertex_index) {
            uint16_t out_index = 0;
            NMO_RETURN_IF_ERROR(workspace_obj_mesh_get_or_add_vertex(
                obj_data,
                &obj_face->verts[vertex_index],
                vertices,
                vertex_colors,
                vertex_specular,
                dedup_table,
                counts.dedup_capacity,
                &unique_count,
                &out_index));
            face_indices[face_index * 3u + vertex_index] = out_index;
        }

        nmo_vertex_t *v0 = &vertices[face_indices[face_index * 3u + 0u]];
        nmo_vertex_t *v1 = &vertices[face_indices[face_index * 3u + 1u]];
        nmo_vertex_t *v2 = &vertices[face_indices[face_index * 3u + 2u]];
        float e1x = v1->position.x - v0->position.x;
        float e1y = v1->position.y - v0->position.y;
        float e1z = v1->position.z - v0->position.z;
        float e2x = v2->position.x - v0->position.x;
        float e2y = v2->position.y - v0->position.y;
        float e2z = v2->position.z - v0->position.z;
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }

        faces[face_index].normal = (nmo_vector_t){nx, ny, nz};
        faces[face_index].material_group_idx =
            obj_face->material_group == NMO_OBJ_NO_MATERIAL
                ? 0u
                : (uint16_t)(obj_face->material_group + material_offset);
        faces[face_index].channel_mask = WORKSPACE_NEW_FACE_CHANNEL_MASK;
    }

    for (size_t line_index = 0; line_index < obj_data->line_count; ++line_index) {
        const nmo_obj_line_t *obj_line = &obj_data->lines[line_index];
        for (size_t vertex_index = 0; vertex_index < 2u; ++vertex_index) {
            uint16_t out_index = 0;
            NMO_RETURN_IF_ERROR(workspace_obj_mesh_get_or_add_vertex(
                obj_data,
                &obj_line->verts[vertex_index],
                vertices,
                vertex_colors,
                vertex_specular,
                dedup_table,
                counts.dedup_capacity,
                &unique_count,
                &out_index));
            line_indices[line_index * 2u + vertex_index] = out_index;
        }
    }

    memset(out_state, 0, sizeof(*out_state));
    out_state->flags = WORKSPACE_NEW_MESH_FLAGS;
    out_state->face_count = (uint32_t)obj_data->face_count;
    out_state->faces = faces;
    out_state->face_vertex_indices = face_indices;
    out_state->line_count = (uint32_t)obj_data->line_count;
    out_state->line_indices = line_indices;
    out_state->vertex_count = unique_count;
    out_state->vertices = vertices;
    out_state->vertex_colors = vertex_colors;
    out_state->vertex_specular = vertex_specular;
    out_state->vertex_weights = NULL;
    out_state->vertex_weight_count = 0u;
    out_state->material_group_count = material_group_count;
    out_state->material_groups = material_groups;
    out_state->material_channel_count = 0u;
    out_state->material_channels = NULL;
    out_state->is_valid = true;
    nmo_mesh_update_bounding_volumes(out_state);
    return NMO_OK;
}

nmo_status_t nmo_asset_edit_set_obj_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    const nmo_obj_data_t *obj_data,
    const nmo_asset_mesh_import_options_t *options)
{
    if (edit == NULL || mesh_id == 0 || obj_data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *mesh_object = NULL;
    nmo_mesh_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_object(
        edit,
        mesh_id,
        NMO_CID_MESH,
        &mesh_object,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }

    status = workspace_obj_mesh_validate_materials(edit, options);
    if (status != NMO_OK) {
        return status;
    }

    nmo_arena_t *document_arena =
        nmo_workspace_internal_document_arena(edit->workspace);
    if (document_arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_arena_t *scratch = nmo_arena_create(NULL, 0);
    if (scratch == NULL) {
        return NMO_ERR_NOMEM;
    }

    nmo_mesh_state_t next = {0};
    status = workspace_obj_mesh_build(
        scratch,
        document_arena,
        obj_data,
        options,
        &next);
    if (status == NMO_OK) {
        status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    }
    if (status == NMO_OK) {
        status = nmo_workspace_edit_snapshot_object_chunk(edit, mesh_id);
    }

    nmo_chunk_t *chunk = NULL;
    if (status == NMO_OK) {
        /* Only the geometry is replaced; the scripts, attributes, priority and
           visibility of the object stay as they are. */
        next.beobject = state->beobject;
        *state = next;
        chunk = nmo_object_get_chunk(mesh_object);
        if (chunk == NULL) {
            chunk = nmo_chunk_create(document_arena);
            if (chunk == NULL) {
                status = NMO_ERR_NOMEM;
            } else {
                status = nmo_object_set_chunk(mesh_object, chunk);
            }
        }
    }
    if (status == NMO_OK) {
        chunk->class_id = NMO_CID_MESH;
        chunk->chunk_class_id = (uint8_t)(NMO_CID_MESH & 0xFFu);
        chunk->chunk_version = 7u;
        nmo_chunk_set_data_version(chunk, 9u);
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        status = nmo_chunk_start_write(chunk);
    }
    if (status == NMO_OK) {
        nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
            document_arena,
            nmo_workspace_internal_repository(edit->workspace),
            NMO_SERIALIZE_FLAG_FILE_MODE,
            0);
        status = nmo_mesh_serialize(state, chunk, NULL, &ser_ctx);
    }
    if (status == NMO_OK) {
        nmo_workspace_edit_mark(
            edit,
            NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    }

    nmo_arena_destroy(scratch);
    return status;
}

static uint8_t *workspace_edit_read_file_to_heap(
    const char *path,
    size_t *out_size)
{
    if (path == NULL || out_size == NULL) {
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size_long = ftell(file);
    if (size_long < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    size_t size = (size_t)size_long;
    uint8_t *bytes = (uint8_t *)malloc(size > 0 ? size : 1u);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(bytes, 1u, size, file);
    fclose(file);
    if (read_size != size) {
        free(bytes);
        return NULL;
    }

    *out_size = size;
    return bytes;
}

nmo_status_t nmo_asset_edit_set_texture_from_file(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t texture_id,
    const char *path)
{
    if (edit == NULL || texture_id == 0u || path == NULL || *path == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t size = 0u;
    uint8_t *bytes = workspace_edit_read_file_to_heap(path, &size);
    if (bytes == NULL) {
        return NMO_ERR_CANT_OPEN_FILE;
    }
    if (size > (size_t)INT_MAX) {
        free(bytes);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_arena_t *decode_arena = nmo_arena_create(NULL, 0);
    if (decode_arena == NULL) {
        free(bytes);
        return NMO_ERR_NOMEM;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t *pixels = nmo_stbi_load_from_memory(
        decode_arena,
        bytes,
        (int)size,
        &width,
        &height,
        &channels,
        4);
    free(bytes);
    if (pixels == NULL || width <= 0 || height <= 0) {
        nmo_arena_destroy(decode_arena);
        return NMO_ERR_INVALID_FORMAT;
    }

    nmo_status_t status = nmo_asset_edit_set_texture_rgba(
        edit,
        texture_id,
        pixels,
        (uint32_t)width,
        (uint32_t)height);
    nmo_arena_destroy(decode_arena);
    return status;
}

nmo_status_t nmo_asset_edit_set_obj_mesh_from_file(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    const char *path,
    const nmo_asset_mesh_import_options_t *options)
{
    if (edit == NULL || mesh_id == 0 || path == NULL || *path == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t size = 0;
    uint8_t *bytes = workspace_edit_read_file_to_heap(path, &size);
    if (bytes == NULL) {
        return NMO_ERR_CANT_OPEN_FILE;
    }

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 0);
    if (parse_arena == NULL) {
        free(bytes);
        return NMO_ERR_NOMEM;
    }

    nmo_obj_data_t obj_data = {0};
    nmo_status_t status =
        nmo_obj_parse(parse_arena, (const char *)bytes, size, &obj_data);
    free(bytes);
    if (status == NMO_OK) {
        status = nmo_asset_edit_set_obj_mesh(edit, mesh_id, &obj_data, options);
    }
    nmo_arena_destroy(parse_arena);
    return status;
}

nmo_status_t nmo_asset_edit_set_primitive_mesh(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t mesh_id,
    nmo_primitive_mesh_t primitive,
    nmo_object_id_t material_id)
{
    if (primitive != NMO_PRIMITIVE_CUBE) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *mesh_object = NULL;
    nmo_mesh_state_t *state = NULL;
    nmo_status_t status = workspace_edit_find_typed_object(
        edit,
        mesh_id,
        NMO_CID_MESH,
        &mesh_object,
        (void **)&state);
    if (status != NMO_OK) {
        return status;
    }
    if (material_id != 0) {
        status = workspace_edit_require_object_class(edit, material_id, NMO_CID_MATERIAL);
        if (status != NMO_OK) {
            return status;
        }
    }

    nmo_arena_t *arena = nmo_workspace_internal_document_arena(edit->workspace);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_vertex_t *vertices = NULL;
    nmo_face_t *faces = NULL;
    uint16_t *indices = NULL;
    uint32_t *vertex_colors = NULL;
    uint32_t *vertex_specular = NULL;
    nmo_material_group_t *groups = NULL;
    status = workspace_edit_alloc_cube_mesh(
        arena,
        &vertices,
        &faces,
        &indices,
        &vertex_colors,
        &vertex_specular,
        &groups);
    if (status != NMO_OK) {
        return status;
    }
    groups[0].material = nmo_ref_from_id(material_id);

    status = nmo_workspace_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (status != NMO_OK) {
        return status;
    }
    status = nmo_workspace_edit_snapshot_object_chunk(edit, mesh_id);
    if (status != NMO_OK) {
        return status;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(mesh_object);
    if (chunk == NULL) {
        chunk = nmo_chunk_create(arena);
        if (chunk == NULL) {
            return NMO_ERR_NOMEM;
        }
        status = nmo_object_set_chunk(mesh_object, chunk);
        if (status != NMO_OK) {
            return status;
        }
    }
    nmo_chunk_set_data_version(chunk, 9u);

    state->flags = WORKSPACE_NEW_MESH_FLAGS;
    state->bary_center = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    state->radius = 0.8660254f;
    state->local_box_min = (nmo_vector_t){-0.5f, -0.5f, -0.5f};
    state->local_box_max = (nmo_vector_t){0.5f, 0.5f, 0.5f};
    state->face_count = 12u;
    state->faces = faces;
    state->face_vertex_indices = indices;
    state->line_count = 0u;
    state->line_indices = NULL;
    state->vertex_count = 8u;
    state->vertices = vertices;
    state->vertex_colors = vertex_colors;
    state->vertex_specular = vertex_specular;
    state->vertex_weights = NULL;
    state->vertex_weight_count = 0u;
    state->material_group_count = 1u;
    state->material_groups = groups;
    state->material_channel_count = 0u;
    state->material_channels = NULL;
    state->is_valid = true;

    nmo_workspace_edit_mark(
        edit,
        NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}
