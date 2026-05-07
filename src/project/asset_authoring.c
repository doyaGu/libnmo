#include "project_internal.h"

#include "object/nmo_asset_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct project_material_authoring_desc {
    bool has_diffuse;
    float diffuse[4];
    bool has_ambient;
    float ambient[4];
    bool has_specular;
    float specular[4];
    bool has_emissive;
    float emissive[4];
    bool has_specular_power;
    float specular_power;
    bool has_render_flags;
    nmo_project_material_render_flags_t render_flags;
} project_material_authoring_desc_t;

static nmo_object_id_t project_authoring_find_object_id(
    const nmo_project_runtime_object_t *objects,
    size_t object_count,
    uint32_t plan_handle)
{
    for (size_t i = 0; i < object_count; ++i) {
        if (objects[i].plan_handle == plan_handle) {
            return objects[i].object_id;
        }
    }
    return 0;
}

static nmo_status_t project_authoring_get_object_desc(
    const nmo_project_plan_t *plan,
    uint32_t object_handle,
    nmo_project_object_desc_t *out_object)
{
    for (size_t i = 0; i < nmo_project_plan_object_count(plan); ++i) {
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, i, out_object));
        if (out_object->handle == object_handle) {
            NMO_RETURN_OK();
        }
    }
    NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                     "asset target object handle not found");
}

static nmo_status_t project_authoring_create_material(
    nmo_workspace_edit_t *edit,
    const char *material_name,
    const project_material_authoring_desc_t *material,
    nmo_object_id_t *out_material_id)
{
    if (!edit || !material_name || !material || !out_material_id) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "material authoring arguments are required");
    }

    nmo_object_id_t material_id = 0;
    NMO_RETURN_IF_ERROR(nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_MATERIAL,
            .name = material_name,
            .type_guid = NMO_GUID_NULL,
        },
        &material_id));
    NMO_RETURN_IF_ERROR(nmo_asset_edit_set_material_color(
        edit,
        material_id,
        material->has_diffuse ? material->diffuse[0] : 1.0f,
        material->has_diffuse ? material->diffuse[1] : 1.0f,
        material->has_diffuse ? material->diffuse[2] : 1.0f,
        material->has_diffuse ? material->diffuse[3] : 1.0f));

    nmo_session_field_edit_t fields[7];
    char diffuse_value[64];
    char ambient_value[64];
    char specular_value[64];
    char emissive_value[64];
    char power_value[64];
    char packed_modes_value[32];
    char packed_flags_value[32];
    size_t field_count = 0u;

    if (material->has_diffuse) {
        int wrote = snprintf(diffuse_value,
                             sizeof(diffuse_value),
                             "(%.9g, %.9g, %.9g, %.9g)",
                             material->diffuse[0],
                             material->diffuse[1],
                             material->diffuse[2],
                             material->diffuse[3]);
        if (wrote < 0 || (size_t)wrote >= sizeof(diffuse_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "material diffuse color string is too long");
        }
        fields[field_count++] =
            (nmo_session_field_edit_t){"diffuse_color", diffuse_value};
    }
    if (material->has_ambient) {
        int wrote = snprintf(ambient_value,
                             sizeof(ambient_value),
                             "(%.9g, %.9g, %.9g, %.9g)",
                             material->ambient[0],
                             material->ambient[1],
                             material->ambient[2],
                             material->ambient[3]);
        if (wrote < 0 || (size_t)wrote >= sizeof(ambient_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "material ambient color string is too long");
        }
        fields[field_count++] =
            (nmo_session_field_edit_t){"ambient_color", ambient_value};
    }
    if (material->has_specular) {
        int wrote = snprintf(specular_value,
                             sizeof(specular_value),
                             "(%.9g, %.9g, %.9g, %.9g)",
                             material->specular[0],
                             material->specular[1],
                             material->specular[2],
                             material->specular[3]);
        if (wrote < 0 || (size_t)wrote >= sizeof(specular_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "material specular color string is too long");
        }
        fields[field_count++] =
            (nmo_session_field_edit_t){"specular_color", specular_value};
    }
    if (material->has_emissive) {
        int wrote = snprintf(emissive_value,
                             sizeof(emissive_value),
                             "(%.9g, %.9g, %.9g, %.9g)",
                             material->emissive[0],
                             material->emissive[1],
                             material->emissive[2],
                             material->emissive[3]);
        if (wrote < 0 || (size_t)wrote >= sizeof(emissive_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "material emissive color string is too long");
        }
        fields[field_count++] =
            (nmo_session_field_edit_t){"emissive_color", emissive_value};
    }
    if (material->has_specular_power) {
        int wrote = snprintf(power_value,
                             sizeof(power_value),
                             "%.9g",
                             material->specular_power);
        if (wrote < 0 || (size_t)wrote >= sizeof(power_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "material specular power string is too long");
        }
        fields[field_count++] =
            (nmo_session_field_edit_t){"specular_power", power_value};
    }
    if (material->has_render_flags) {
        uint32_t packed_modes = 0u;
        uint32_t packed_flags = 0u;
        const nmo_project_material_render_flags_t *flags =
            &material->render_flags;
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
        if (flags->has_alpha_func) {
            packed_flags = (packed_flags & ~(0x1Fu << 16)) |
                           (((uint32_t)flags->alpha_func & 0x1Fu) << 16);
        }
        int wrote = snprintf(packed_modes_value,
                             sizeof(packed_modes_value),
                             "%u",
                             packed_modes);
        if (wrote < 0 || (size_t)wrote >= sizeof(packed_modes_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "material packed modes string is too long");
        }
        wrote = snprintf(packed_flags_value,
                         sizeof(packed_flags_value),
                         "%u",
                         packed_flags);
        if (wrote < 0 || (size_t)wrote >= sizeof(packed_flags_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "material packed flags string is too long");
        }
        fields[field_count++] =
            (nmo_session_field_edit_t){"packed_modes", packed_modes_value};
        fields[field_count++] =
            (nmo_session_field_edit_t){"packed_flags", packed_flags_value};
    }
    if (field_count > 0u) {
        nmo_session_field_edit_result_t result = {0};
        NMO_RETURN_IF_ERROR(nmo_object_edit_set_fields(
            edit,
            material_id,
            fields,
            field_count,
            &result));
        if (result.failed > 0u) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "failed to set generated material fields");
        }
    }
    *out_material_id = material_id;
    NMO_RETURN_OK();
}

static bool project_authoring_asset_has_material(
    const nmo_project_asset_desc_t *asset)
{
    return asset->has_material_color ||
           asset->has_material_diffuse ||
           asset->has_material_ambient ||
           asset->has_material_specular ||
           asset->has_material_emissive ||
           asset->has_material_specular_power ||
           asset->has_material_render_flags ||
           asset->has_material_texture;
}

static project_material_authoring_desc_t project_authoring_material_from_asset(
    const nmo_project_asset_desc_t *asset)
{
    project_material_authoring_desc_t material = {0};
    if (asset->has_material_diffuse) {
        material.has_diffuse = true;
        memcpy(material.diffuse, asset->material_diffuse, sizeof(material.diffuse));
    } else if (asset->has_material_color) {
        material.has_diffuse = true;
        memcpy(material.diffuse, asset->material_color, sizeof(material.diffuse));
    }
    material.has_ambient = asset->has_material_ambient;
    memcpy(material.ambient, asset->material_ambient, sizeof(material.ambient));
    material.has_specular = asset->has_material_specular;
    memcpy(material.specular, asset->material_specular, sizeof(material.specular));
    material.has_emissive = asset->has_material_emissive;
    memcpy(material.emissive, asset->material_emissive, sizeof(material.emissive));
    material.has_specular_power = asset->has_material_specular_power;
    material.specular_power = asset->material_specular_power;
    material.has_render_flags = asset->has_material_render_flags;
    material.render_flags = asset->material_render_flags;
    return material;
}

static project_material_authoring_desc_t project_authoring_material_from_obj(
    const nmo_project_material_spec_t *spec)
{
    project_material_authoring_desc_t material = {0};
    if (spec->has_diffuse) {
        material.has_diffuse = true;
        memcpy(material.diffuse, spec->diffuse, sizeof(material.diffuse));
    } else if (spec->has_color) {
        material.has_diffuse = true;
        memcpy(material.diffuse, spec->color, sizeof(material.diffuse));
    }
    material.has_ambient = spec->has_ambient;
    memcpy(material.ambient, spec->ambient, sizeof(material.ambient));
    material.has_specular = spec->has_specular;
    memcpy(material.specular, spec->specular, sizeof(material.specular));
    material.has_emissive = spec->has_emissive;
    memcpy(material.emissive, spec->emissive, sizeof(material.emissive));
    material.has_specular_power = spec->has_specular_power;
    material.specular_power = spec->specular_power;
    material.has_render_flags = spec->has_render_flags;
    material.render_flags = spec->render_flags;
    return material;
}

static nmo_status_t project_authoring_texture_name(
    const char *base_name,
    uint32_t slot,
    char *out_name,
    size_t out_size)
{
    int len = slot == 0u
        ? snprintf(out_name, out_size, "%s_Texture", base_name)
        : snprintf(out_name, out_size, "%s_Texture%u", base_name, slot);
    if (len < 0 || (size_t)len >= out_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "generated texture asset name is too long");
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_bind_texture_slot(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const char *texture_name,
    const char *texture_path,
    uint32_t slot)
{
    if (!edit || material_id == 0u || !texture_name || !texture_path) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "texture slot authoring arguments are required");
    }

    nmo_object_id_t texture_id = 0;
    NMO_RETURN_IF_ERROR(nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_TEXTURE,
            .name = texture_name,
            .type_guid = NMO_GUID_NULL,
        },
        &texture_id));
    NMO_RETURN_IF_ERROR(nmo_asset_edit_set_texture_from_file(
        edit,
        texture_id,
        texture_path));
    NMO_RETURN_IF_ERROR(nmo_asset_edit_bind_material_texture(
        edit,
        material_id,
        texture_id,
        slot));
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_author_assets(
    nmo_workspace_edit_t *edit,
    const nmo_project_plan_t *plan,
    const nmo_project_runtime_object_t *objects,
    size_t object_count)
{
    if (!edit || !plan || (object_count > 0u && !objects)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "workspace edit, plan, and object map are required");
    }

    size_t asset_count = nmo_project_plan_asset_count(plan);
    for (size_t i = 0; i < asset_count; ++i) {
        nmo_project_asset_desc_t asset = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_asset(plan, i, &asset));

        nmo_object_id_t object_id =
            project_authoring_find_object_id(objects, object_count, asset.object_handle);
        if (object_id == 0) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "asset target object was not generated");
        }

        nmo_project_object_desc_t object = {0};
        NMO_RETURN_IF_ERROR(project_authoring_get_object_desc(
            plan, asset.object_handle, &object));

        char material_name[256];
        char mesh_name[256];
        int material_len = snprintf(material_name, sizeof(material_name), "%s_Material", object.name);
        int mesh_len = snprintf(mesh_name, sizeof(mesh_name), "%s_Mesh", object.name);
        if (material_len < 0 || mesh_len < 0 ||
            (size_t)material_len >= sizeof(material_name) ||
            (size_t)mesh_len >= sizeof(mesh_name)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "generated asset name is too long");
        }

        nmo_object_id_t material_id = 0;
        if (project_authoring_asset_has_material(&asset)) {
            project_material_authoring_desc_t material =
                project_authoring_material_from_asset(&asset);
            NMO_RETURN_IF_ERROR(project_authoring_create_material(
                edit,
                material_name,
                &material,
                &material_id));
        }

        if (asset.has_material_texture) {
            for (uint32_t slot = 0u; slot < 4u; ++slot) {
                const char *texture_path = asset.has_material_texture_slots[slot]
                    ? asset.material_texture_paths[slot]
                    : NULL;
                if (!texture_path && slot == 0u && asset.material_texture_path) {
                    texture_path = asset.material_texture_path;
                }
                if (!texture_path) {
                    continue;
                }
                char texture_name[256];
                NMO_RETURN_IF_ERROR(project_authoring_texture_name(
                    object.name,
                    slot,
                    texture_name,
                    sizeof(texture_name)));
                NMO_RETURN_IF_ERROR(project_authoring_bind_texture_slot(
                    edit,
                    material_id,
                    texture_name,
                    texture_path,
                    slot));
            }
        }

        size_t obj_material_count =
            nmo_project_plan_obj_material_count(plan, asset.object_handle);
        nmo_asset_mesh_material_binding_t *obj_material_bindings = NULL;
        if (obj_material_count > 0u) {
            obj_material_bindings =
                (nmo_asset_mesh_material_binding_t *)calloc(
                    obj_material_count,
                    sizeof(*obj_material_bindings));
            if (!obj_material_bindings) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "failed to allocate OBJ material bindings");
            }
        }
        for (size_t material_index = 0u;
             material_index < obj_material_count;
             ++material_index) {
            nmo_project_material_spec_t obj_material = {0};
            nmo_status_t status = nmo_project_plan_get_obj_material(
                plan,
                asset.object_handle,
                material_index,
                &obj_material);
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }

            char obj_material_name[256];
            int obj_material_len = snprintf(
                obj_material_name,
                sizeof(obj_material_name),
                "%s_%s_Material",
                object.name,
                obj_material.obj_material_name);
            if (obj_material_len < 0 ||
                (size_t)obj_material_len >= sizeof(obj_material_name)) {
                free(obj_material_bindings);
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "generated OBJ material asset name is too long");
            }

            nmo_object_id_t obj_material_id = 0;
            project_material_authoring_desc_t obj_material_desc =
                project_authoring_material_from_obj(&obj_material);
            status = project_authoring_create_material(
                edit,
                obj_material_name,
                &obj_material_desc,
                &obj_material_id);
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }

            bool obj_material_has_texture = obj_material.has_texture;
            for (uint32_t slot = 0u; slot < 4u; ++slot) {
                obj_material_has_texture =
                    obj_material_has_texture || obj_material.has_texture_slots[slot];
            }
            if (obj_material_has_texture) {
                char obj_texture_base[256];
                int obj_texture_base_len = snprintf(
                    obj_texture_base,
                    sizeof(obj_texture_base),
                    "%s_%s",
                    object.name,
                    obj_material.obj_material_name);
                if (obj_texture_base_len < 0 ||
                    (size_t)obj_texture_base_len >= sizeof(obj_texture_base)) {
                    free(obj_material_bindings);
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                     "generated OBJ material texture asset name is too long");
                }
                for (uint32_t slot = 0u; slot < 4u; ++slot) {
                    const char *texture_path = obj_material.has_texture_slots[slot]
                        ? obj_material.texture_paths[slot]
                        : NULL;
                    if (!texture_path && slot == 0u && obj_material.texture_path) {
                        texture_path = obj_material.texture_path;
                    }
                    if (!texture_path) {
                        continue;
                    }
                    char obj_texture_name[256];
                    status = project_authoring_texture_name(
                        obj_texture_base,
                        slot,
                        obj_texture_name,
                        sizeof(obj_texture_name));
                    if (status != NMO_OK) {
                        free(obj_material_bindings);
                        return status;
                    }
                    status = project_authoring_bind_texture_slot(
                        edit,
                        obj_material_id,
                        obj_texture_name,
                        texture_path,
                        slot);
                    if (status != NMO_OK) {
                        free(obj_material_bindings);
                        return status;
                    }
                }
            }

            obj_material_bindings[material_index].name =
                obj_material.obj_material_name;
            obj_material_bindings[material_index].material_id =
                obj_material_id;
        }

        if (asset.has_primitive_mesh) {
            nmo_object_id_t mesh_id = 0;
            nmo_status_t status = nmo_object_edit_create(
                edit,
                &(nmo_object_create_desc_t){
                    .class_id = NMO_CID_MESH,
                    .name = mesh_name,
                    .type_guid = NMO_GUID_NULL,
                },
                &mesh_id);
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }
            status = nmo_asset_edit_set_primitive_mesh(
                edit,
                mesh_id,
                asset.primitive_mesh,
                material_id);
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }
            status = nmo_asset_edit_bind_entity_mesh(edit, object_id, mesh_id);
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }
        }

        if (asset.has_external_mesh) {
            nmo_object_id_t mesh_id = 0;
            nmo_status_t status = nmo_object_edit_create(
                edit,
                &(nmo_object_create_desc_t){
                    .class_id = NMO_CID_MESH,
                    .name = mesh_name,
                    .type_guid = NMO_GUID_NULL,
                },
                &mesh_id);
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }
            status = nmo_asset_edit_set_obj_mesh_from_file(
                edit,
                mesh_id,
                asset.external_mesh_path,
                &(nmo_asset_mesh_import_options_t){
                    .default_material_id = material_id,
                    .materials = obj_material_bindings,
                    .material_count = obj_material_count,
                });
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }
            status = nmo_asset_edit_bind_entity_mesh(edit, object_id, mesh_id);
            if (status != NMO_OK) {
                free(obj_material_bindings);
                return status;
            }
        }
        free(obj_material_bindings);
    }

    NMO_RETURN_OK();
}
