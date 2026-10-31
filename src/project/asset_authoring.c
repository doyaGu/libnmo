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

    NMO_RETURN_IF_ERROR(nmo_asset_edit_set_material_channels(
        edit,
        material_id,
        &(nmo_asset_material_channels_t){
            .has_diffuse = material->has_diffuse,
            .diffuse = {
                material->diffuse[0],
                material->diffuse[1],
                material->diffuse[2],
                material->diffuse[3],
            },
            .has_ambient = material->has_ambient,
            .ambient = {
                material->ambient[0],
                material->ambient[1],
                material->ambient[2],
                material->ambient[3],
            },
            .has_specular = material->has_specular,
            .specular = {
                material->specular[0],
                material->specular[1],
                material->specular[2],
                material->specular[3],
            },
            .has_emissive = material->has_emissive,
            .emissive = {
                material->emissive[0],
                material->emissive[1],
                material->emissive[2],
                material->emissive[3],
            },
            .has_specular_power = material->has_specular_power,
            .specular_power = material->specular_power,
        }));
    if (material->has_render_flags) {
        const nmo_project_material_render_flags_t *flags =
            &material->render_flags;
        NMO_RETURN_IF_ERROR(nmo_asset_edit_set_material_render_flags(
            edit,
            material_id,
            &(nmo_asset_material_render_flags_t){
                .has_texture_blend = flags->has_texture_blend,
                .texture_blend = flags->texture_blend,
                .has_source_blend = flags->has_source_blend,
                .source_blend = flags->source_blend,
                .has_destination_blend = flags->has_destination_blend,
                .destination_blend = flags->destination_blend,
                .has_min_filter = flags->has_min_filter,
                .min_filter = flags->min_filter,
                .has_mag_filter = flags->has_mag_filter,
                .mag_filter = flags->mag_filter,
                .has_wrap = flags->has_wrap,
                .wrap = flags->wrap,
                .has_alpha_func = flags->has_alpha_func,
                .alpha_func = flags->alpha_func,
            }));
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

void nmo_project_texture_file_stem(
    const char *path,
    const char **out_start,
    size_t *out_length)
{
    const char *start = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            start = p + 1;
        }
    }
    const char *dot = strrchr(start, '.');
    size_t length = dot != NULL && dot != start ? (size_t)(dot - start) : strlen(start);
    if (length == 0u) {
        start = path;
        length = strlen(path);
    }
    *out_start = start;
    *out_length = length;
}

/* The external textures made so far, by path. */
typedef struct project_external_textures {
    const char **paths;
    nmo_object_id_t *ids;
    size_t count;
    size_t capacity;
} project_external_textures_t;

static void project_external_textures_dispose(project_external_textures_t *textures)
{
    free(textures->paths);
    free(textures->ids);
}

static nmo_status_t project_authoring_external_texture(
    nmo_workspace_edit_t *edit,
    project_external_textures_t *textures,
    const char *texture_path,
    nmo_object_id_t *out_texture_id)
{
    for (size_t i = 0u; i < textures->count; ++i) {
        if (strcmp(textures->paths[i], texture_path) == 0) {
            *out_texture_id = textures->ids[i];
            NMO_RETURN_OK();
        }
    }
    if (textures->count == textures->capacity) {
        size_t capacity = textures->capacity ? textures->capacity * 2u : 8u;
        const char **paths =
            (const char **)realloc((void *)textures->paths, capacity * sizeof(*paths));
        if (!paths) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate external texture list");
        }
        textures->paths = paths;
        nmo_object_id_t *ids =
            (nmo_object_id_t *)realloc(textures->ids, capacity * sizeof(*ids));
        if (!ids) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate external texture list");
        }
        textures->ids = ids;
        textures->capacity = capacity;
    }

    const char *stem = NULL;
    size_t stem_length = 0u;
    nmo_project_texture_file_stem(texture_path, &stem, &stem_length);
    char *name = (char *)malloc(stem_length + 1u);
    if (!name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate external texture name");
    }
    memcpy(name, stem, stem_length);
    name[stem_length] = '\0';

    nmo_object_id_t texture_id = 0;
    nmo_status_t status = nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_TEXTURE,
            .name = name,
            .type_guid = NMO_GUID_NULL,
        },
        &texture_id);
    free(name);
    NMO_RETURN_IF_ERROR(status);
    NMO_RETURN_IF_ERROR(nmo_asset_edit_set_texture_external(
        edit,
        texture_id,
        texture_path));
    textures->paths[textures->count] = texture_path;
    textures->ids[textures->count] = texture_id;
    textures->count++;
    *out_texture_id = texture_id;
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_bind_texture_slot(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t material_id,
    const char *texture_name,
    const char *texture_path,
    project_external_textures_t *external_textures,
    uint32_t slot)
{
    if (!edit || material_id == 0u || !texture_name || !texture_path) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "texture slot authoring arguments are required");
    }

    nmo_object_id_t texture_id = 0;
    if (external_textures) {
        NMO_RETURN_IF_ERROR(project_authoring_external_texture(
            edit,
            external_textures,
            texture_path,
            &texture_id));
    } else {
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
    }
    NMO_RETURN_IF_ERROR(nmo_asset_edit_bind_material_texture(
        edit,
        material_id,
        texture_id,
        slot));
    NMO_RETURN_OK();
}

static nmo_status_t project_authoring_assets(
    nmo_workspace_edit_t *edit,
    const nmo_project_plan_t *plan,
    const nmo_project_runtime_object_t *objects,
    size_t object_count,
    project_external_textures_t *external_textures)
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
                    asset.material_texture_external ? external_textures : NULL,
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
                        obj_material.texture_external ? external_textures : NULL,
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

nmo_status_t nmo_project_author_assets(
    nmo_workspace_edit_t *edit,
    const nmo_project_plan_t *plan,
    const nmo_project_runtime_object_t *objects,
    size_t object_count)
{
    project_external_textures_t external_textures = {0};
    nmo_status_t status = project_authoring_assets(
        edit,
        plan,
        objects,
        object_count,
        &external_textures);
    project_external_textures_dispose(&external_textures);
    return status;
}
