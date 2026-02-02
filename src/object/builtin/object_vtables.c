/**
 * @file object_vtables.c
 * @brief CKObject-derived vtable definitions and copy/validate helpers
 */

#include "object/nmo_object_type_common.h"
#include "nmo_types.h"
#include "core/nmo_math.h"


#include "object/nmo_ck2dentity_schemas.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_ck3dobject_schemas.h"
#include "object/nmo_ckanimation_schemas.h"
#include "object/nmo_ckbehaviorio_schemas.h"
#include "object/nmo_ckbehaviorlink_schemas.h"
#include "object/nmo_ckbehavior_schemas.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_ckcamera_schemas.h"
#include "object/nmo_ckcharacter_schemas.h"
#include "object/nmo_ckcurve_schemas.h"
#include "object/nmo_ckdataarray_schemas.h"
#include "object/nmo_ckgrid_schemas.h"
#include "object/nmo_ckgroup_schemas.h"
#include "object/nmo_ckinterfaceobjectmanager_schemas.h"
#include "object/nmo_ckkinematicchain_schemas.h"
#include "object/nmo_cklayer_schemas.h"
#include "object/nmo_cklevel_schemas.h"
#include "object/nmo_cklight_schemas.h"
#include "object/nmo_ckmaterial_schemas.h"
#include "object/nmo_ckmesh_schemas.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_ckparameterin_schemas.h"
#include "object/nmo_ckparameterlocal_schemas.h"
#include "object/nmo_ckparameteroperation_schemas.h"
#include "object/nmo_ckparameterout_schemas.h"
#include "object/nmo_ckparameter_schemas.h"
#include "object/nmo_ckpatchmesh_schemas.h"
#include "object/nmo_ckplace_schemas.h"
#include "object/nmo_ckrendercontext_schemas.h"
#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_cksceneobject_schemas.h"
#include "object/nmo_ckscene_schemas.h"
#include "object/nmo_cksound_schemas.h"
#include "object/nmo_cksprite3d_schemas.h"
#include "object/nmo_ckspritetext_schemas.h"
#include "object/nmo_cksprite_schemas.h"
#include "object/nmo_cksynchro_schemas.h"
#include "object/nmo_cktargetcamera_schemas.h"
#include "object/nmo_cktargetlight_schemas.h"
#include "object/nmo_cktexture_schemas.h"
#include "object/nmo_class_ids.h"

#include <string.h>

/* ============================================================================
 * Type-specific copy helpers
 * ============================================================================ */

static nmo_result_t nmo_cktexture_copy_reader_slots(
    nmo_arena_t *arena,
    nmo_cktexture_reader_slot_t **dst,
    const nmo_cktexture_reader_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_cktexture_reader_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].data,
                                                  src[i].data, src[i].data_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].alpha_plane,
                                                  src[i].alpha_plane, src[i].alpha_plane_size));
    }
    return nmo_result_ok();
}

static nmo_result_t nmo_cktexture_copy_raw_slots(
    nmo_arena_t *arena,
    nmo_cktexture_raw_slot_t **dst,
    const nmo_cktexture_raw_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_cktexture_raw_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].blue_data,
                                                  src[i].blue_data, src[i].blue_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].green_data,
                                                  src[i].green_data, src[i].green_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].red_data,
                                                  src[i].red_data, src[i].red_size));
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].alpha_data,
                                                  src[i].alpha_data, src[i].alpha_size));
    }
    return nmo_result_ok();
}

static nmo_result_t nmo_cktexture_copy_bitmap2_slots(
    nmo_arena_t *arena,
    nmo_cktexture_bitmap2_slot_t **dst,
    const nmo_cktexture_bitmap2_slot_t *src,
    uint32_t count)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)dst, src,
                                              sizeof(nmo_cktexture_bitmap2_slot_t), count));
    for (uint32_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&(*dst)[i].buffer,
                                                  src[i].buffer, src[i].buffer_size));
    }
    return nmo_result_ok();
}

static nmo_result_t nmo_copy_ckbeobject_state(
    nmo_arena_t *arena,
    nmo_ckbeobject_state_t *dst,
    const nmo_ckbeobject_state_t *src)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->base.raw_tail,
                                              src->base.raw_tail, src->base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->script_ids,
                                              src->script_ids, sizeof(nmo_object_id_t), src->script_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->attribute_parameter_ids,
                                              src->attribute_parameter_ids, sizeof(nmo_object_id_t), src->attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->attribute_types,
                                              src->attribute_types, sizeof(uint32_t), src->attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk_array(arena, &dst->attribute_chunks,
                                                    src->attribute_chunks, src->attribute_chunk_count));
    return nmo_object_copy_bytes(arena, (void **)&dst->legacy_attributes_raw,
                                 src->legacy_attributes_raw, src->legacy_attributes_size);
}

static nmo_result_t nmo_copy_ckparameter_state(
    nmo_arena_t *arena,
    nmo_ckparameter_state_t *dst,
    const nmo_ckparameter_state_t *src)
{
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->buffer_data,
                                              src->buffer_data, src->buffer_size));
    return nmo_object_copy_chunk(arena, &dst->subchunk, src->subchunk);
}

static nmo_result_t nmo_copy_ckmesh_state(
    nmo_arena_t *arena,
    nmo_ck_mesh_state_t *dst,
    const nmo_ck_mesh_state_t *src)
{
    NMO_RETURN_IF_ERROR(nmo_copy_ckbeobject_state(arena, &dst->beobject, &src->beobject));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->faces,
                                              src->faces, sizeof(nmo_ck_face_t), src->face_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->face_vertex_indices,
                                              src->face_vertex_indices, sizeof(uint16_t),
                                              (uint32_t)(src->face_count * 3u)));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->line_indices,
                                              src->line_indices, sizeof(uint16_t),
                                              (uint32_t)(src->line_count * 2u)));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->vertices,
                                              src->vertices, sizeof(nmo_vx_vertex_t), src->vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->vertex_colors,
                                              src->vertex_colors, sizeof(uint32_t), src->vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->vertex_specular,
                                              src->vertex_specular, sizeof(uint32_t), src->vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->vertex_weights,
                                              src->vertex_weights, sizeof(float), src->vertex_weight_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->material_groups,
                                              src->material_groups, sizeof(nmo_ck_material_group_t),
                                              src->material_group_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->material_channels,
                                              src->material_channels, sizeof(nmo_ck_material_channel_t),
                                              src->material_channel_count));
    for (uint32_t i = 0; i < src->material_channel_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena,
                                                  (void **)&dst->material_channels[i].uv_coords,
                                                  src->material_channels[i].uv_coords,
                                                  sizeof(nmo_vx_2d_vector_t),
                                                  src->material_channels[i].uv_count));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, &dst->pm_data, src->pm_data, src->pm_data_size));
    return nmo_result_ok();
}

static nmo_result_t nmo_copy_ckpatchmesh_state(
    nmo_arena_t *arena,
    nmo_ckpatchmesh_state_t *dst,
    const nmo_ckpatchmesh_state_t *src)
{
    NMO_RETURN_IF_ERROR(nmo_copy_ckmesh_state(arena, &dst->base, &src->base));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->vectors,
                                              src->vectors, sizeof(nmo_vector_t), (uint32_t)src->vec_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->patch_material_ids,
                                              src->patch_material_ids, sizeof(nmo_object_id_t), src->patch_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->patches,
                                              src->patches, sizeof(nmo_ckpatchmesh_patch_t), src->patch_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->edge_data,
                                              src->edge_data, src->edge_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->channels,
                                              src->channels, sizeof(nmo_ckpatchmesh_channel_t), src->channel_count));
    for (uint32_t i = 0; i < src->channel_count; ++i) {
        size_t patch_bytes = (size_t)src->channels[i].patch_count * sizeof(nmo_ckpatchmesh_patch_t);
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->channels[i].patches_raw,
                                                  src->channels[i].patches_raw, patch_bytes));
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->channels[i].uvs,
                                                  src->channels[i].uvs, sizeof(nmo_vector2_t),
                                                  src->channels[i].uv_count));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->legacy_patch_data,
                                              src->legacy_patch_data, src->legacy_patch_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->legacy_edge_data,
                                              src->legacy_edge_data, src->legacy_edge_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->legacy_tvpatch_data,
                                              src->legacy_tvpatch_data, src->legacy_tvpatch_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->legacy_uv_data,
                                              src->legacy_uv_data, src->legacy_uv_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->legacy_smoothing_groups,
                                              src->legacy_smoothing_groups, sizeof(uint32_t),
                                              src->legacy_smoothing_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dst->legacy_material_ids,
                                              src->legacy_material_ids, sizeof(nmo_object_id_t),
                                              src->legacy_material_count));
    return nmo_result_ok();
}

static nmo_result_t nmo_copy_cksprite_bitmapdata(
    nmo_arena_t *arena,
    nmo_ckbitmapdata_t *dst,
    const nmo_ckbitmapdata_t *src)
{
    if (src->pixel_data_size > 0) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->pixel_data,
                                                  src->pixel_data, src->pixel_data_size));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->palette_data,
                                              src->palette_data, src->palette_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->system_copy_data,
                                              src->system_copy_data, src->system_copy_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->video_backup_data,
                                              src->video_backup_data, src->video_backup_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&dst->pixels_data,
                                              src->pixels_data, src->pixels_size));
    return nmo_object_copy_bytes(arena, (void **)&dst->raw_chunk_data,
                                 src->raw_chunk_data, src->raw_chunk_size);
}

static nmo_result_t nmo_copy_cktexture_state(
    nmo_arena_t *arena,
    nmo_ck_texture_state_t *dst,
    const nmo_ck_texture_state_t *src)
{
    NMO_RETURN_IF_ERROR(nmo_copy_ckbeobject_state(arena, &dst->base, &src->base));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &dst->movie_filename, src->movie_filename));
    NMO_RETURN_IF_ERROR(nmo_object_copy_string_array(arena, &dst->slot_filenames, src->slot_filenames, src->slot_count));
    dst->reader_slots = NULL;
    dst->raw_slots = NULL;
    dst->bitmap2_slots = NULL;
    if (src->slot_count > 0) {
        if (src->reader_slots) {
            NMO_RETURN_IF_ERROR(nmo_cktexture_copy_reader_slots(arena, &dst->reader_slots,
                                                               src->reader_slots, src->slot_count));
        }
        if (src->raw_slots) {
            NMO_RETURN_IF_ERROR(nmo_cktexture_copy_raw_slots(arena, &dst->raw_slots,
                                                             src->raw_slots, src->slot_count));
        }
        if (src->bitmap2_slots) {
            NMO_RETURN_IF_ERROR(nmo_cktexture_copy_bitmap2_slots(arena, &dst->bitmap2_slots,
                                                                 src->bitmap2_slots, src->slot_count));
        }
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, &dst->save_format_data,
                                              src->save_format_data, src->save_format_size));
    dst->user_mipmaps = NULL;
    if (src->user_mipmap_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_cktexture_copy_raw_slots(arena, &dst->user_mipmaps,
                                                         src->user_mipmaps, src->user_mipmap_count));
    }
    return nmo_result_ok();
}

nmo_result_t nmo_object_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    if (!src || !dst || !type) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL src/dst/type in copy");
    }
    memcpy(dst, src, type->size);

    switch (type->class_id) {
        case NMO_CID_SCENEOBJECT: {
            const nmo_cksceneobject_state_t *s = src;
            nmo_cksceneobject_state_t *d = dst;
            return nmo_object_copy_bytes(arena, (void **)&d->raw_tail,
                                         s->raw_tail, s->raw_tail_size);
        }
        case NMO_CID_BEOBJECT:
        case NMO_CID_RENDEROBJECT: {
            const nmo_ckbeobject_state_t *s = src;
            nmo_ckbeobject_state_t *d = dst;
            return nmo_copy_ckbeobject_state(arena, d, s);
        }
        case NMO_CID_PARAMETER: {
            const nmo_ckparameter_state_t *s = src;
            nmo_ckparameter_state_t *d = dst;
            return nmo_copy_ckparameter_state(arena, d, s);
        }
        case NMO_CID_PARAMETEROUT: {
            const nmo_ckparameterout_state_t *s = src;
            nmo_ckparameterout_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_copy_ckparameter_state(arena, &d->base, &s->base));
            return nmo_object_copy_array(arena, (void **)&d->destination_ids,
                                         s->destination_ids, sizeof(nmo_object_id_t), s->destination_count);
        }
        case NMO_CID_PARAMETEROPERATION: {
            const nmo_ckparameteroperation_state_t *s = src;
            nmo_ckparameteroperation_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->in1_chunk, s->in1_chunk));
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->in2_chunk, s->in2_chunk));
            return nmo_object_copy_chunk(arena, &d->out_chunk, s->out_chunk);
        }
        case NMO_CID_BEHAVIOR: {
            const nmo_ckbehavior_state_t *s = src;
            nmo_ckbehavior_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->sub_behaviors,
                                                      s->sub_behaviors, sizeof(nmo_object_id_t), s->sub_behavior_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk_array(arena, &d->sub_behavior_chunks,
                                                            s->sub_behavior_chunks, s->sub_behavior_chunk_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->sub_behavior_links,
                                                      s->sub_behavior_links, sizeof(nmo_object_id_t), s->sub_behavior_link_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->operations,
                                                      s->operations, sizeof(nmo_object_id_t), s->operation_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->in_parameters,
                                                      s->in_parameters, sizeof(nmo_object_id_t), s->in_parameter_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->out_parameters,
                                                      s->out_parameters, sizeof(nmo_object_id_t), s->out_parameter_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->local_parameters,
                                                      s->local_parameters, sizeof(nmo_object_id_t), s->local_parameter_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk_array(arena, &d->local_parameter_chunks,
                                                            s->local_parameter_chunks, s->local_parameter_chunk_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->inputs,
                                                      s->inputs, sizeof(nmo_object_id_t), s->input_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->outputs,
                                                      s->outputs, sizeof(nmo_object_id_t), s->output_count));
            return nmo_object_copy_chunk(arena, &d->interface_chunk, s->interface_chunk);
        }
        case NMO_CID_DATAARRAY: {
            const nmo_ckdataarray_state_t *s = src;
            nmo_ckdataarray_state_t *d = dst;
            if (s->column_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->column_formats,
                                                          s->column_formats, sizeof(nmo_ckdataarray_column_format_t),
                                                          s->column_count));
                for (uint32_t i = 0; i < s->column_count; ++i) {
                    if (s->column_formats[i].name) {
                        d->column_formats[i].name = nmo_arena_strdup(arena, s->column_formats[i].name);
                    }
                }
            }
            if (s->row_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->rows,
                                                          s->rows, sizeof(nmo_ckdataarray_row_t), s->row_count));
                for (uint32_t r = 0; r < s->row_count; ++r) {
                    const nmo_ckdataarray_row_t *sr = &s->rows[r];
                    nmo_ckdataarray_row_t *dr = &d->rows[r];
                    if (sr->column_count > 0) {
                        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&dr->cells,
                                                                  sr->cells, sizeof(nmo_ckdataarray_cell_t),
                                                                  sr->column_count));
                        for (uint32_t c = 0; c < sr->column_count; ++c) {
                            nmo_ckdataarray_cell_t *cell = &dr->cells[c];
                            if (d->column_formats && c < d->column_count) {
                                nmo_ck_arraytype_t type_id = d->column_formats[c].type;
                                if (type_id == NMO_ARRAYTYPE_STRING && sr->cells[c].string_value) {
                                    cell->string_value = nmo_arena_strdup(arena, sr->cells[c].string_value);
                                } else if (type_id == NMO_ARRAYTYPE_PARAMETER && sr->cells[c].parameter_chunk) {
                                    cell->parameter_chunk = nmo_chunk_clone(sr->cells[c].parameter_chunk, arena);
                                }
                            }
                        }
                    }
                }
            }
            return nmo_result_ok();
        }
        case NMO_CID_SCENE: {
            const nmo_ckscene_state_t *s = src;
            nmo_ckscene_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_copy_ckbeobject_state(arena, &d->base, &s->base));
            if (s->object_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->object_descs,
                                                          s->object_descs, sizeof(nmo_scene_object_desc_t),
                                                          s->object_count));
                for (uint32_t i = 0; i < s->object_count; ++i) {
                    nmo_chunk_t *clone = NULL;
                    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, s->object_descs[i].initial_value));
                    d->object_descs[i].initial_value = clone;
                }
            }
            return nmo_result_ok();
        }
        case NMO_CID_LEVEL: {
            const nmo_cklevel_state_t *s = src;
            nmo_cklevel_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_copy_ckbeobject_state(arena, &d->base, &s->base));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->scene_ids,
                                                      s->scene_ids, sizeof(nmo_object_id_t), s->scene_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->level_scene_chunk, s->level_scene_chunk));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->inactive_manager_guids,
                                                      s->inactive_manager_guids, sizeof(nmo_guid_t), s->inactive_manager_count));
            return nmo_object_copy_string_array(arena, &d->duplicate_manager_names,
                                                s->duplicate_manager_names, s->duplicate_manager_count);
        }
        case NMO_CID_GROUP: {
            const nmo_ckgroup_state_t *s = src;
            nmo_ckgroup_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_copy_ckbeobject_state(arena, &d->base, &s->base));
            return nmo_object_copy_array(arena, (void **)&d->object_ids,
                                         s->object_ids, sizeof(nmo_object_id_t), s->object_count);
        }
        case NMO_CID_CURVE: {
            const nmo_ckcurve_state_t *s = src;
            nmo_ckcurve_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->control_point_ids,
                                                      s->control_point_ids, sizeof(nmo_object_id_t), s->control_point_count));
            if (s->sub_point_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->sub_points,
                                                          s->sub_points, sizeof(nmo_ckcurve_point_subchunk_t),
                                                          s->sub_point_count));
                for (uint32_t i = 0; i < s->sub_point_count; ++i) {
                    nmo_chunk_t *clone = NULL;
                    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, s->sub_points[i].chunk));
                    d->sub_points[i].chunk = clone;
                }
            }
            return nmo_result_ok();
        }
        case NMO_CID_CHARACTER: {
            const nmo_ckcharacter_state_t *s = src;
            nmo_ckcharacter_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->body_part_ids,
                                                      s->body_part_ids, sizeof(nmo_object_id_t), s->body_part_count));
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->animation_ids,
                                                      s->animation_ids, sizeof(nmo_object_id_t), s->animation_count));
            if (s->subpart_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->subparts,
                                                          s->subparts, sizeof(nmo_ckcharacter_subpart_t),
                                                          s->subpart_count));
                for (uint32_t i = 0; i < s->subpart_count; ++i) {
                    nmo_chunk_t *clone = NULL;
                    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, s->subparts[i].chunk));
                    d->subparts[i].chunk = clone;
                }
            }
            return nmo_result_ok();
        }
        case NMO_CID_GRID: {
            const nmo_ckgrid_state_t *s = src;
            nmo_ckgrid_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->layer_ids,
                                                      s->layer_ids, sizeof(nmo_object_id_t), s->layer_count));
            return nmo_object_copy_chunk_array(arena, &d->layer_chunks,
                                               s->layer_chunks, s->layer_chunk_count);
        }
        case NMO_CID_LAYER: {
            const nmo_cklayer_state_t *s = src;
            nmo_cklayer_state_t *d = dst;
            return nmo_object_copy_bytes(arena, (void **)&d->square_data,
                                         s->square_data, s->square_data_size);
        }
        case NMO_CID_ANIMATION: {
            return nmo_result_ok();
        }
        case NMO_CID_KEYEDANIMATION: {
            const nmo_ckkeyedanimation_state_t *s = src;
            nmo_ckkeyedanimation_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->animation_ids,
                                                      s->animation_ids, sizeof(nmo_object_id_t), s->animation_count));
            if (s->subanim_count > 0) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->subanims,
                                                          s->subanims, sizeof(nmo_ckkeyedanimation_subanim_t),
                                                          s->subanim_count));
                for (uint32_t i = 0; i < s->subanim_count; ++i) {
                    nmo_chunk_t *clone = NULL;
                    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, s->subanims[i].chunk));
                    d->subanims[i].chunk = clone;
                }
            }
            return nmo_result_ok();
        }
        case NMO_CID_OBJECTANIMATION: {
            const nmo_ckobjectanimation_state_t *s = src;
            nmo_ckobjectanimation_state_t *d = dst;
            return nmo_object_copy_bytes(arena, &d->raw_tail, s->raw_tail, s->raw_tail_size);
        }
        case NMO_CID_PLACE: {
            const nmo_ckplace_state_t *s = src;
            nmo_ckplace_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->portals,
                                                      s->portals, sizeof(nmo_ckplace_portal_entry_t),
                                                      s->portal_count));
            return nmo_object_copy_array(arena, (void **)&d->reference_ids,
                                         s->reference_ids, sizeof(nmo_object_id_t), s->reference_count);
        }
        case NMO_CID_MESH: {
            const nmo_ck_mesh_state_t *s = src;
            nmo_ck_mesh_state_t *d = dst;
            return nmo_copy_ckmesh_state(arena, d, s);
        }
        case NMO_CID_PATCHMESH: {
            const nmo_ckpatchmesh_state_t *s = src;
            nmo_ckpatchmesh_state_t *d = dst;
            return nmo_copy_ckpatchmesh_state(arena, d, s);
        }
        case NMO_CID_TEXTURE: {
            const nmo_ck_texture_state_t *s = src;
            nmo_ck_texture_state_t *d = dst;
            return nmo_copy_cktexture_state(arena, d, s);
        }
        case NMO_CID_SPRITE: {
            const nmo_cksprite_state_t *s = src;
            nmo_cksprite_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_copy_ckbeobject_state(arena, &d->entity.base.base, &s->entity.base.base));
            if (s->has_bitmap_data) {
                NMO_RETURN_IF_ERROR(nmo_copy_cksprite_bitmapdata(arena, &d->bitmap_data, &s->bitmap_data));
            } else {
                d->bitmap_data.pixel_data = NULL;
                d->bitmap_data.palette_data = NULL;
                d->bitmap_data.system_copy_data = NULL;
                d->bitmap_data.video_backup_data = NULL;
                d->bitmap_data.pixels_data = NULL;
                d->bitmap_data.raw_chunk_data = NULL;
            }
            if (s->has_save_options) {
                NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->bitmap_properties,
                                                          s->bitmap_properties, s->bitmap_properties_size));
            }
            return nmo_result_ok();
        }
        case NMO_CID_SOUND: {
            const nmo_cksound_state_t *s = src;
            nmo_cksound_state_t *d = dst;
            return nmo_object_copy_string(arena, &d->file_name, s->file_name);
        }
        case NMO_CID_WAVESOUND: {
            const nmo_ckwavesound_state_t *s = src;
            nmo_ckwavesound_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->base.file_name, s->base.file_name));
            return nmo_object_copy_string(arena, &d->wave_file_name, s->wave_file_name);
        }
        case NMO_CID_MIDISOUND: {
            const nmo_ckmidisound_state_t *s = src;
            nmo_ckmidisound_state_t *d = dst;
            NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &d->base.file_name, s->base.file_name));
            return nmo_object_copy_string(arena, &d->midi_file_name, s->midi_file_name);
        }
        case NMO_CID_INTERFACEOBJECTMANAGER: {
            const nmo_ckinterfaceobjectmanager_state_t *s = src;
            nmo_ckinterfaceobjectmanager_state_t *d = dst;
            return nmo_object_copy_chunk_array(arena, &d->chunks, s->chunks, (uint32_t)s->chunk_count);
        }
        default:
            return nmo_result_ok();
    }
}

nmo_result_t nmo_object_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)context;
    if (!instance || !type) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL instance/type in validate");
    }

    switch (type->class_id) {
        case NMO_CID_SCENEOBJECT: {
            const nmo_cksceneobject_state_t *s = instance;
            NMO_VALIDATE_BYTES(s->raw_tail, s->raw_tail_size, "raw_tail");
            break;
        }
        case NMO_CID_BEOBJECT:
        case NMO_CID_RENDEROBJECT: {
            const nmo_ckbeobject_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->script_ids, s->script_count, "script_ids");
            NMO_VALIDATE_COUNT(s->attribute_parameter_ids, s->attribute_count, "attribute_parameter_ids");
            NMO_VALIDATE_COUNT(s->attribute_types, s->attribute_count, "attribute_types");
            NMO_VALIDATE_COUNT(s->attribute_chunks, s->attribute_chunk_count, "attribute_chunks");
            NMO_VALIDATE_BYTES(s->legacy_attributes_raw, s->legacy_attributes_size, "legacy_attributes_raw");
            break;
        }
        case NMO_CID_BEHAVIOR: {
            const nmo_ckbehavior_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->sub_behaviors, s->sub_behavior_count, "sub_behaviors");
            NMO_VALIDATE_COUNT(s->sub_behavior_chunks, s->sub_behavior_chunk_count, "sub_behavior_chunks");
            NMO_VALIDATE_COUNT(s->sub_behavior_links, s->sub_behavior_link_count, "sub_behavior_links");
            NMO_VALIDATE_COUNT(s->operations, s->operation_count, "operations");
            NMO_VALIDATE_COUNT(s->in_parameters, s->in_parameter_count, "in_parameters");
            NMO_VALIDATE_COUNT(s->out_parameters, s->out_parameter_count, "out_parameters");
            NMO_VALIDATE_COUNT(s->local_parameters, s->local_parameter_count, "local_parameters");
            NMO_VALIDATE_COUNT(s->local_parameter_chunks, s->local_parameter_chunk_count, "local_parameter_chunks");
            NMO_VALIDATE_COUNT(s->inputs, s->input_count, "inputs");
            NMO_VALIDATE_COUNT(s->outputs, s->output_count, "outputs");
            break;
        }
        case NMO_CID_PARAMETER: {
            const nmo_ckparameter_state_t *s = instance;
            NMO_VALIDATE_BYTES(s->buffer_data, s->buffer_size, "buffer_data");
            break;
        }
        case NMO_CID_PARAMETEROUT: {
            const nmo_ckparameterout_state_t *s = instance;
            NMO_VALIDATE_BYTES(s->base.buffer_data, s->base.buffer_size, "buffer_data");
            NMO_VALIDATE_COUNT(s->destination_ids, s->destination_count, "destination_ids");
            break;
        }
        case NMO_CID_DATAARRAY: {
            const nmo_ckdataarray_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->column_formats, s->column_count, "column_formats");
            NMO_VALIDATE_COUNT(s->rows, s->row_count, "rows");
            break;
        }
        case NMO_CID_SCENE: {
            const nmo_ckscene_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->object_descs, s->object_count, "object_descs");
            break;
        }
        case NMO_CID_LEVEL: {
            const nmo_cklevel_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->scene_ids, s->scene_count, "scene_ids");
            NMO_VALIDATE_COUNT(s->inactive_manager_guids, s->inactive_manager_count,
                               "inactive_manager_guids");
            NMO_VALIDATE_COUNT(s->duplicate_manager_names, s->duplicate_manager_count,
                               "duplicate_manager_names");
            break;
        }
        case NMO_CID_GROUP: {
            const nmo_ckgroup_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->object_ids, s->object_count, "object_ids");
            break;
        }
        case NMO_CID_CURVE: {
            const nmo_ckcurve_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->control_point_ids, s->control_point_count, "control_point_ids");
            NMO_VALIDATE_COUNT(s->sub_points, s->sub_point_count, "sub_points");
            break;
        }
        case NMO_CID_CHARACTER: {
            const nmo_ckcharacter_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->body_part_ids, s->body_part_count, "body_part_ids");
            NMO_VALIDATE_COUNT(s->animation_ids, s->animation_count, "animation_ids");
            NMO_VALIDATE_COUNT(s->subparts, s->subpart_count, "subparts");
            break;
        }
        case NMO_CID_GRID: {
            const nmo_ckgrid_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->layer_ids, s->layer_count, "layer_ids");
            NMO_VALIDATE_COUNT(s->layer_chunks, s->layer_chunk_count, "layer_chunks");
            break;
        }
        case NMO_CID_LAYER: {
            const nmo_cklayer_state_t *s = instance;
            NMO_VALIDATE_BYTES(s->square_data, s->square_data_size, "square_data");
            break;
        }
        case NMO_CID_KEYEDANIMATION: {
            const nmo_ckkeyedanimation_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->animation_ids, s->animation_count, "animation_ids");
            NMO_VALIDATE_COUNT(s->subanims, s->subanim_count, "subanims");
            break;
        }
        case NMO_CID_OBJECTANIMATION: {
            const nmo_ckobjectanimation_state_t *s = instance;
            NMO_VALIDATE_BYTES(s->raw_tail, s->raw_tail_size, "raw_tail");
            break;
        }
        case NMO_CID_PLACE: {
            const nmo_ckplace_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->portals, s->portal_count, "portals");
            NMO_VALIDATE_COUNT(s->reference_ids, s->reference_count, "reference_ids");
            break;
        }
        case NMO_CID_MESH: {
            const nmo_ck_mesh_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->faces, s->face_count, "faces");
            if (s->face_count > 0) {
                NMO_VALIDATE_COUNT(s->face_vertex_indices, (uint32_t)(s->face_count * 3u),
                                   "face_vertex_indices");
            }
            if (s->line_count > 0) {
                NMO_VALIDATE_COUNT(s->line_indices, (uint32_t)(s->line_count * 2u), "line_indices");
            }
            NMO_VALIDATE_COUNT(s->vertices, s->vertex_count, "vertices");
            NMO_VALIDATE_COUNT(s->vertex_colors, s->vertex_count, "vertex_colors");
            NMO_VALIDATE_COUNT(s->vertex_specular, s->vertex_count, "vertex_specular");
            NMO_VALIDATE_COUNT(s->vertex_weights, s->vertex_weight_count, "vertex_weights");
            NMO_VALIDATE_COUNT(s->material_groups, s->material_group_count, "material_groups");
            NMO_VALIDATE_COUNT(s->material_channels, s->material_channel_count, "material_channels");
            if (s->material_channels) {
                for (uint32_t i = 0; i < s->material_channel_count; ++i) {
                    if (s->material_channels[i].uv_count > 0) {
                        NMO_VALIDATE_COUNT(s->material_channels[i].uv_coords,
                                           s->material_channels[i].uv_count,
                                           "material_channels.uv_coords");
                    }
                }
            }
            NMO_VALIDATE_BYTES(s->pm_data, s->pm_data_size, "pm_data");
            break;
        }
        case NMO_CID_PATCHMESH: {
            const nmo_ckpatchmesh_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->vectors, (uint32_t)s->vec_count, "vectors");
            NMO_VALIDATE_COUNT(s->patch_material_ids, s->patch_count, "patch_material_ids");
            NMO_VALIDATE_COUNT(s->patches, s->patch_count, "patches");
            NMO_VALIDATE_BYTES(s->edge_data, s->edge_data_size, "edge_data");
            NMO_VALIDATE_COUNT(s->channels, s->channel_count, "channels");
            if (s->channels) {
                for (uint32_t i = 0; i < s->channel_count; ++i) {
                    if (s->channels[i].patch_count > 0) {
                        NMO_VALIDATE_BYTES(s->channels[i].patches_raw,
                                           (size_t)s->channels[i].patch_count * sizeof(nmo_ckpatchmesh_patch_t),
                                           "channels.patches_raw");
                    }
                    if (s->channels[i].uv_count > 0) {
                        NMO_VALIDATE_COUNT(s->channels[i].uvs,
                                           s->channels[i].uv_count,
                                           "channels.uvs");
                    }
                }
            }
            NMO_VALIDATE_BYTES(s->legacy_patch_data, s->legacy_patch_data_size, "legacy_patch_data");
            NMO_VALIDATE_BYTES(s->legacy_edge_data, s->legacy_edge_data_size, "legacy_edge_data");
            NMO_VALIDATE_BYTES(s->legacy_tvpatch_data, s->legacy_tvpatch_data_size, "legacy_tvpatch_data");
            NMO_VALIDATE_BYTES(s->legacy_uv_data, s->legacy_uv_data_size, "legacy_uv_data");
            NMO_VALIDATE_COUNT(s->legacy_smoothing_groups, s->legacy_smoothing_count,
                               "legacy_smoothing_groups");
            NMO_VALIDATE_COUNT(s->legacy_material_ids, s->legacy_material_count, "legacy_material_ids");
            break;
        }
        case NMO_CID_TEXTURE: {
            const nmo_ck_texture_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->slot_filenames, s->slot_count, "slot_filenames");
            NMO_VALIDATE_BYTES(s->save_format_data, s->save_format_size, "save_format_data");
            NMO_VALIDATE_COUNT(s->user_mipmaps, s->user_mipmap_count, "user_mipmaps");
            break;
        }
        case NMO_CID_SPRITE: {
            const nmo_cksprite_state_t *s = instance;
            if (s->has_bitmap_data) {
                NMO_VALIDATE_BYTES(s->bitmap_data.pixel_data, s->bitmap_data.pixel_data_size,
                                   "bitmap_data.pixel_data");
                NMO_VALIDATE_BYTES(s->bitmap_data.palette_data, s->bitmap_data.palette_size,
                                   "bitmap_data.palette_data");
                NMO_VALIDATE_BYTES(s->bitmap_data.system_copy_data, s->bitmap_data.system_copy_size,
                                   "bitmap_data.system_copy_data");
                NMO_VALIDATE_BYTES(s->bitmap_data.video_backup_data, s->bitmap_data.video_backup_size,
                                   "bitmap_data.video_backup_data");
                NMO_VALIDATE_BYTES(s->bitmap_data.pixels_data, s->bitmap_data.pixels_size,
                                   "bitmap_data.pixels_data");
                NMO_VALIDATE_BYTES(s->bitmap_data.raw_chunk_data, s->bitmap_data.raw_chunk_size,
                                   "bitmap_data.raw_chunk_data");
            }
            NMO_VALIDATE_BYTES(s->bitmap_properties, s->bitmap_properties_size, "bitmap_properties");
            break;
        }
        case NMO_CID_SOUND: {
            break;
        }
        case NMO_CID_WAVESOUND: {
            break;
        }
        case NMO_CID_MIDISOUND: {
            break;
        }
        case NMO_CID_INTERFACEOBJECTMANAGER: {
            const nmo_ckinterfaceobjectmanager_state_t *s = instance;
            NMO_VALIDATE_COUNT(s->chunks, (uint32_t)s->chunk_count, "chunks");
            break;
        }
        default:
            break;
    }

    return nmo_result_ok();
}

