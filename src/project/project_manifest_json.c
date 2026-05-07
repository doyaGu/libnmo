#include "project/nmo_project_manifest_json.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
#include "runtime/nmo_context.h"
#include "type/nmo_type_query.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct manifest_parse_ctx {
    nmo_context_t *context;
    nmo_project_plan_t *plan;
    const char **object_ids;
    const char **object_names;
    uint32_t *object_handles;
    size_t object_count;
    size_t object_capacity;
} manifest_parse_ctx_t;

static char *manifest_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }

    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1u);
    return copy;
}

void nmo_project_manifest_init(nmo_project_manifest_t *manifest)
{
    if (!manifest) {
        return;
    }
    manifest->plan = NULL;
    manifest->output_path = NULL;
}

void nmo_project_manifest_dispose(nmo_project_manifest_t *manifest)
{
    if (!manifest) {
        return;
    }
    nmo_project_plan_destroy(manifest->plan);
    free(manifest->output_path);
    nmo_project_manifest_init(manifest);
}

static bool manifest_key_allowed(const char *key, const char *const *allowed)
{
    if (!key || !allowed) {
        return false;
    }
    for (size_t i = 0u; allowed[i] != NULL; ++i) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static nmo_status_t manifest_reject_unknown_fields(
    yyjson_val *obj,
    const char *context_name,
    const char *const *allowed)
{
    if (!yyjson_is_obj(obj)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest %s must be an object", context_name);
    }

    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *key = NULL;
    yyjson_val *val = NULL;
    yyjson_obj_foreach(obj, idx, max, key, val) {
        (void)val;
        const char *name = yyjson_get_str(key);
        if (!manifest_key_allowed(name, allowed)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "unknown manifest field '%s' in %s",
                             name ? name : "(null)",
                             context_name);
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_required_string(
    yyjson_val *obj,
    const char *field,
    const char **out_value)
{
    if (!obj || !field || !out_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "manifest string lookup requires arguments");
    }
    yyjson_val *value = yyjson_obj_get(obj, field);
    if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest field '%s' must be a non-empty string", field);
    }
    *out_value = yyjson_get_str(value);
    NMO_RETURN_OK();
}

static nmo_status_t manifest_optional_string(
    yyjson_val *obj,
    const char *field,
    const char **out_value)
{
    if (!obj || !field || !out_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "manifest string lookup requires arguments");
    }
    *out_value = NULL;
    yyjson_val *value = yyjson_obj_get(obj, field);
    if (!value) {
        NMO_RETURN_OK();
    }
    if (!yyjson_is_str(value)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest field '%s' must be a string", field);
    }
    *out_value = yyjson_get_str(value);
    NMO_RETURN_OK();
}

static double manifest_get_number(yyjson_val *value)
{
    if (yyjson_is_real(value)) {
        return yyjson_get_real(value);
    }
    if (yyjson_is_sint(value)) {
        return (double)yyjson_get_sint(value);
    }
    return (double)yyjson_get_uint(value);
}

static nmo_status_t manifest_parse_color4(
    yyjson_val *value,
    const char *field_name,
    float out_color[4])
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 4u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest %s must contain four numbers", field_name);
    }
    for (size_t i = 0u; i < 4u; ++i) {
        yyjson_val *item = yyjson_arr_get(value, i);
        if (!yyjson_is_num(item)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest %s values must be numbers", field_name);
        }
        out_color[i] = (float)manifest_get_number(item);
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_texture_blend(
    const char *value,
    VXTEXTURE_BLENDMODE *out_mode)
{
    if (!value || !out_mode) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "texture blend output is required");
    }
    if (strcmp(value, "decal") == 0) *out_mode = VXTEXTUREBLEND_DECAL;
    else if (strcmp(value, "modulate") == 0) *out_mode = VXTEXTUREBLEND_MODULATE;
    else if (strcmp(value, "decal_alpha") == 0) *out_mode = VXTEXTUREBLEND_DECALALPHA;
    else if (strcmp(value, "modulate_alpha") == 0) *out_mode = VXTEXTUREBLEND_MODULATEALPHA;
    else if (strcmp(value, "copy") == 0) *out_mode = VXTEXTUREBLEND_COPY;
    else if (strcmp(value, "add") == 0) *out_mode = VXTEXTUREBLEND_ADD;
    else {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "unsupported material texture blend");
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_blend_factor(
    const char *value,
    VXBLEND_MODE *out_mode)
{
    if (!value || !out_mode) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "blend factor output is required");
    }
    if (strcmp(value, "zero") == 0) *out_mode = VXBLEND_ZERO;
    else if (strcmp(value, "one") == 0) *out_mode = VXBLEND_ONE;
    else if (strcmp(value, "src_color") == 0) *out_mode = VXBLEND_SRCCOLOR;
    else if (strcmp(value, "inv_src_color") == 0) *out_mode = VXBLEND_INVSRCCOLOR;
    else if (strcmp(value, "src_alpha") == 0) *out_mode = VXBLEND_SRCALPHA;
    else if (strcmp(value, "inv_src_alpha") == 0) *out_mode = VXBLEND_INVSRCALPHA;
    else if (strcmp(value, "dest_alpha") == 0) *out_mode = VXBLEND_DESTALPHA;
    else if (strcmp(value, "inv_dest_alpha") == 0) *out_mode = VXBLEND_INVDESTALPHA;
    else if (strcmp(value, "dest_color") == 0) *out_mode = VXBLEND_DESTCOLOR;
    else if (strcmp(value, "inv_dest_color") == 0) *out_mode = VXBLEND_INVDESTCOLOR;
    else {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "unsupported material blend factor");
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_filter_mode(
    const char *value,
    VXTEXTURE_FILTERMODE *out_mode)
{
    if (!value || !out_mode) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "filter mode output is required");
    }
    if (strcmp(value, "nearest") == 0) *out_mode = VXTEXTUREFILTER_NEAREST;
    else if (strcmp(value, "linear") == 0) *out_mode = VXTEXTUREFILTER_LINEAR;
    else if (strcmp(value, "mip_nearest") == 0) *out_mode = VXTEXTUREFILTER_MIPNEAREST;
    else if (strcmp(value, "mip_linear") == 0) *out_mode = VXTEXTUREFILTER_MIPLINEAR;
    else if (strcmp(value, "linear_mip_nearest") == 0) *out_mode = VXTEXTUREFILTER_LINEARMIPNEAREST;
    else if (strcmp(value, "linear_mip_linear") == 0) *out_mode = VXTEXTUREFILTER_LINEARMIPLINEAR;
    else if (strcmp(value, "anisotropic") == 0) *out_mode = VXTEXTUREFILTER_ANISOTROPIC;
    else {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "unsupported material filter mode");
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_wrap_mode(
    const char *value,
    VXTEXTURE_ADDRESSMODE *out_mode)
{
    if (!value || !out_mode) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "wrap mode output is required");
    }
    if (strcmp(value, "wrap") == 0) *out_mode = VXTEXTURE_ADDRESSWRAP;
    else if (strcmp(value, "mirror") == 0) *out_mode = VXTEXTURE_ADDRESSMIRROR;
    else if (strcmp(value, "clamp") == 0) *out_mode = VXTEXTURE_ADDRESSCLAMP;
    else if (strcmp(value, "border") == 0) *out_mode = VXTEXTURE_ADDRESSBORDER;
    else {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "unsupported material wrap mode");
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_alpha_func(
    const char *value,
    VXCMPFUNC *out_func)
{
    if (!value || !out_func) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "alpha func output is required");
    }
    if (strcmp(value, "never") == 0) *out_func = VXCMP_NEVER;
    else if (strcmp(value, "less") == 0) *out_func = VXCMP_LESS;
    else if (strcmp(value, "equal") == 0) *out_func = VXCMP_EQUAL;
    else if (strcmp(value, "less_equal") == 0) *out_func = VXCMP_LESSEQUAL;
    else if (strcmp(value, "greater") == 0) *out_func = VXCMP_GREATER;
    else if (strcmp(value, "not_equal") == 0) *out_func = VXCMP_NOTEQUAL;
    else if (strcmp(value, "greater_equal") == 0) *out_func = VXCMP_GREATEREQUAL;
    else if (strcmp(value, "always") == 0) *out_func = VXCMP_ALWAYS;
    else {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "unsupported material alpha func");
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_material_render_flags(
    yyjson_val *material,
    const char *context,
    nmo_project_material_render_flags_t *out_flags,
    bool *out_has_flags)
{
    if (!out_flags || !out_has_flags) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "material render flag outputs are required");
    }
    memset(out_flags, 0, sizeof(*out_flags));
    *out_has_flags = false;

    yyjson_val *blend = yyjson_obj_get(material, "blend");
    if (blend) {
        static const char *const allowed[] = {"texture", "source", "destination", NULL};
        NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(blend, "material.blend", allowed));
        yyjson_val *texture = yyjson_obj_get(blend, "texture");
        if (texture) {
            if (!yyjson_is_str(texture)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest %s.blend.texture must be a string", context);
            }
            NMO_RETURN_IF_ERROR(manifest_parse_texture_blend(
                yyjson_get_str(texture), &out_flags->texture_blend));
            out_flags->has_texture_blend = true;
        }
        yyjson_val *source = yyjson_obj_get(blend, "source");
        if (source) {
            if (!yyjson_is_str(source)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest %s.blend.source must be a string", context);
            }
            NMO_RETURN_IF_ERROR(manifest_parse_blend_factor(
                yyjson_get_str(source), &out_flags->source_blend));
            out_flags->has_source_blend = true;
        }
        yyjson_val *destination = yyjson_obj_get(blend, "destination");
        if (destination) {
            if (!yyjson_is_str(destination)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest %s.blend.destination must be a string", context);
            }
            NMO_RETURN_IF_ERROR(manifest_parse_blend_factor(
                yyjson_get_str(destination), &out_flags->destination_blend));
            out_flags->has_destination_blend = true;
        }
    }

    yyjson_val *filter = yyjson_obj_get(material, "filter");
    if (filter) {
        static const char *const allowed[] = {"min", "mag", NULL};
        NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(filter, "material.filter", allowed));
        yyjson_val *min = yyjson_obj_get(filter, "min");
        if (min) {
            if (!yyjson_is_str(min)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest %s.filter.min must be a string", context);
            }
            NMO_RETURN_IF_ERROR(manifest_parse_filter_mode(
                yyjson_get_str(min), &out_flags->min_filter));
            out_flags->has_min_filter = true;
        }
        yyjson_val *mag = yyjson_obj_get(filter, "mag");
        if (mag) {
            if (!yyjson_is_str(mag)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest %s.filter.mag must be a string", context);
            }
            NMO_RETURN_IF_ERROR(manifest_parse_filter_mode(
                yyjson_get_str(mag), &out_flags->mag_filter));
            out_flags->has_mag_filter = true;
        }
    }

    yyjson_val *wrap = yyjson_obj_get(material, "wrap");
    if (wrap) {
        if (!yyjson_is_str(wrap)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest %s.wrap must be a string", context);
        }
        NMO_RETURN_IF_ERROR(manifest_parse_wrap_mode(
            yyjson_get_str(wrap), &out_flags->wrap));
        out_flags->has_wrap = true;
    }

    yyjson_val *alpha = yyjson_obj_get(material, "alpha");
    if (alpha) {
        static const char *const allowed[] = {"func", NULL};
        NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(alpha, "material.alpha", allowed));
        yyjson_val *func = yyjson_obj_get(alpha, "func");
        if (!yyjson_is_str(func)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest %s.alpha.func must be a string", context);
        }
        NMO_RETURN_IF_ERROR(manifest_parse_alpha_func(
            yyjson_get_str(func), &out_flags->alpha_func));
        out_flags->has_alpha_func = true;
    }

    *out_has_flags =
        out_flags->has_texture_blend ||
        out_flags->has_source_blend ||
        out_flags->has_destination_blend ||
        out_flags->has_min_filter ||
        out_flags->has_mag_filter ||
        out_flags->has_wrap ||
        out_flags->has_alpha_func;
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_material(
    yyjson_val *material,
    bool *out_has_color,
    float out_color[4],
    bool *out_has_ambient,
    float out_ambient[4],
    bool *out_has_specular,
    float out_specular[4],
    bool *out_has_emissive,
    float out_emissive[4],
    bool *out_has_specular_power,
    float *out_specular_power,
    const char **out_texture,
    bool out_has_texture_slots[4],
    const char *out_texture_paths[4],
    size_t out_texture_indices[4],
    bool *out_has_render_flags,
    nmo_project_material_render_flags_t *out_render_flags)
{
    static const char *const allowed[] = {
        "color",
        "diffuse",
        "ambient",
        "specular",
        "emissive",
        "specular_power",
        "texture",
        "textures",
        "blend",
        "filter",
        "wrap",
        "alpha",
        NULL};
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(material, "material", allowed));

    if (!out_has_color || !out_color || !out_texture ||
        !out_has_ambient || !out_ambient ||
        !out_has_specular || !out_specular ||
        !out_has_emissive || !out_emissive ||
        !out_has_specular_power || !out_specular_power ||
        !out_has_texture_slots || !out_texture_paths || !out_texture_indices ||
        !out_has_render_flags || !out_render_flags) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "material output arguments are required");
    }
    *out_has_color = false;
    *out_has_ambient = false;
    *out_has_specular = false;
    *out_has_emissive = false;
    *out_has_specular_power = false;
    *out_specular_power = 0.0f;
    *out_has_render_flags = false;
    memset(out_render_flags, 0, sizeof(*out_render_flags));
    *out_texture = NULL;
    memset(out_has_texture_slots, 0, sizeof(bool) * 4u);
    memset(out_texture_paths, 0, sizeof(const char *) * 4u);
    for (size_t slot = 0u; slot < 4u; ++slot) {
        out_texture_indices[slot] = (size_t)-1;
    }

    yyjson_val *color = yyjson_obj_get(material, "color");
    yyjson_val *diffuse = yyjson_obj_get(material, "diffuse");
    if (color && diffuse) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest material.color and material.diffuse are aliases");
    }
    if (color) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            color,
            "material.color",
            out_color));
        *out_has_color = true;
    }
    if (diffuse) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            diffuse,
            "material.diffuse",
            out_color));
        *out_has_color = true;
    }
    yyjson_val *ambient = yyjson_obj_get(material, "ambient");
    if (ambient) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            ambient,
            "material.ambient",
            out_ambient));
        *out_has_ambient = true;
    }
    yyjson_val *specular = yyjson_obj_get(material, "specular");
    if (specular) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            specular,
            "material.specular",
            out_specular));
        *out_has_specular = true;
    }
    yyjson_val *emissive = yyjson_obj_get(material, "emissive");
    if (emissive) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            emissive,
            "material.emissive",
            out_emissive));
        *out_has_emissive = true;
    }
    yyjson_val *specular_power = yyjson_obj_get(material, "specular_power");
    if (specular_power) {
        if (!yyjson_is_num(specular_power)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest material.specular_power must be a number");
        }
        *out_has_specular_power = true;
        *out_specular_power = (float)manifest_get_number(specular_power);
    }

    yyjson_val *texture = yyjson_obj_get(material, "texture");
    if (texture) {
        if (!yyjson_is_str(texture) || yyjson_get_str(texture)[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest material.texture must be a non-empty string");
        }
        *out_texture = yyjson_get_str(texture);
        out_has_texture_slots[0] = true;
        out_texture_paths[0] = *out_texture;
    }

    yyjson_val *textures = yyjson_obj_get(material, "textures");
    if (textures) {
        if (!yyjson_is_arr(textures)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest material.textures must be an array");
        }
        size_t idx = 0u;
        size_t max = 0u;
        yyjson_val *item = NULL;
        yyjson_arr_foreach(textures, idx, max, item) {
            static const char *const texture_allowed[] = {"slot", "path", NULL};
            NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(
                item,
                "material.textures[]",
                texture_allowed));
            yyjson_val *slot_value = yyjson_obj_get(item, "slot");
            yyjson_val *path_value = yyjson_obj_get(item, "path");
            if (!yyjson_is_uint(slot_value) || yyjson_get_uint(slot_value) >= 4u) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest material.textures[].slot must be 0..3");
            }
            if (!yyjson_is_str(path_value) || yyjson_get_str(path_value)[0] == '\0') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest material.textures[].path must be a non-empty string");
            }
            uint32_t slot = (uint32_t)yyjson_get_uint(slot_value);
            if (out_has_texture_slots[slot]) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest material texture slot is duplicated");
            }
            out_has_texture_slots[slot] = true;
            out_texture_paths[slot] = yyjson_get_str(path_value);
            out_texture_indices[slot] = idx;
        }
    }

    bool has_any_texture = false;
    for (size_t slot = 0u; slot < 4u; ++slot) {
        has_any_texture = has_any_texture || out_has_texture_slots[slot];
    }
    NMO_RETURN_IF_ERROR(manifest_parse_material_render_flags(
        material,
        "material",
        out_render_flags,
        out_has_render_flags));
    if (!*out_has_color && !*out_has_ambient && !*out_has_specular &&
        !*out_has_emissive && !*out_has_specular_power && !has_any_texture &&
        !*out_has_render_flags) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest material requires color channel or texture");
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_obj_material(
    yyjson_val *material,
    nmo_project_material_spec_t *out_spec,
    size_t out_texture_indices[4])
{
    static const char *const allowed[] = {
        "name",
        "color",
        "diffuse",
        "ambient",
        "specular",
        "emissive",
        "specular_power",
        "texture",
        "textures",
        "blend",
        "filter",
        "wrap",
        "alpha",
        NULL};
    if (!out_spec) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "OBJ material output argument is required");
    }
    memset(out_spec, 0, sizeof(*out_spec));
    if (out_texture_indices) {
        for (size_t slot = 0u; slot < 4u; ++slot) {
            out_texture_indices[slot] = (size_t)-1;
        }
    }
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(
        material,
        "materials[]",
        allowed));
    NMO_RETURN_IF_ERROR(manifest_required_string(
        material,
        "name",
        &out_spec->obj_material_name));

    yyjson_val *color = yyjson_obj_get(material, "color");
    yyjson_val *diffuse = yyjson_obj_get(material, "diffuse");
    if (color && diffuse) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest materials[].color and materials[].diffuse are aliases");
    }
    if (color) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            color,
            "materials[].color",
            out_spec->color));
        out_spec->has_color = true;
        out_spec->has_diffuse = true;
        memcpy(out_spec->diffuse, out_spec->color, sizeof(out_spec->diffuse));
    }
    if (diffuse) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            diffuse,
            "materials[].diffuse",
            out_spec->diffuse));
        out_spec->has_diffuse = true;
    }
    yyjson_val *ambient = yyjson_obj_get(material, "ambient");
    if (ambient) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            ambient,
            "materials[].ambient",
            out_spec->ambient));
        out_spec->has_ambient = true;
    }
    yyjson_val *specular = yyjson_obj_get(material, "specular");
    if (specular) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            specular,
            "materials[].specular",
            out_spec->specular));
        out_spec->has_specular = true;
    }
    yyjson_val *emissive = yyjson_obj_get(material, "emissive");
    if (emissive) {
        NMO_RETURN_IF_ERROR(manifest_parse_color4(
            emissive,
            "materials[].emissive",
            out_spec->emissive));
        out_spec->has_emissive = true;
    }
    yyjson_val *specular_power = yyjson_obj_get(material, "specular_power");
    if (specular_power) {
        if (!yyjson_is_num(specular_power)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest materials[].specular_power must be a number");
        }
        out_spec->has_specular_power = true;
        out_spec->specular_power = (float)manifest_get_number(specular_power);
    }

    yyjson_val *texture = yyjson_obj_get(material, "texture");
    if (texture) {
        if (!yyjson_is_str(texture) || yyjson_get_str(texture)[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest materials[].texture must be a non-empty string");
        }
        out_spec->has_texture = true;
        out_spec->texture_path = yyjson_get_str(texture);
        out_spec->has_texture_slots[0] = true;
        out_spec->texture_paths[0] = out_spec->texture_path;
    }

    yyjson_val *textures = yyjson_obj_get(material, "textures");
    if (textures) {
        if (!yyjson_is_arr(textures)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest materials[].textures must be an array");
        }
        size_t idx = 0u;
        size_t max = 0u;
        yyjson_val *item = NULL;
        yyjson_arr_foreach(textures, idx, max, item) {
            static const char *const texture_allowed[] = {"slot", "path", NULL};
            NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(
                item,
                "materials[].textures[]",
                texture_allowed));
            yyjson_val *slot_value = yyjson_obj_get(item, "slot");
            yyjson_val *path_value = yyjson_obj_get(item, "path");
            if (!yyjson_is_uint(slot_value) || yyjson_get_uint(slot_value) >= 4u) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest materials[].textures[].slot must be 0..3");
            }
            if (!yyjson_is_str(path_value) || yyjson_get_str(path_value)[0] == '\0') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest materials[].textures[].path must be a non-empty string");
            }
            uint32_t slot = (uint32_t)yyjson_get_uint(slot_value);
            if (out_spec->has_texture_slots[slot]) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest OBJ material texture slot is duplicated");
            }
            out_spec->has_texture_slots[slot] = true;
            out_spec->texture_paths[slot] = yyjson_get_str(path_value);
            if (out_texture_indices) {
                out_texture_indices[slot] = idx;
            }
        }
        if (out_spec->has_texture_slots[0]) {
            out_spec->has_texture = true;
            out_spec->texture_path = out_spec->texture_paths[0];
        }
    }

    bool has_any_texture = out_spec->has_texture;
    for (size_t slot = 0u; slot < 4u; ++slot) {
        has_any_texture = has_any_texture || out_spec->has_texture_slots[slot];
    }
    NMO_RETURN_IF_ERROR(manifest_parse_material_render_flags(
        material,
        "materials[]",
        &out_spec->render_flags,
        &out_spec->has_render_flags));
    if (!out_spec->has_color && !out_spec->has_diffuse &&
        !out_spec->has_ambient && !out_spec->has_specular &&
        !out_spec->has_emissive && !out_spec->has_specular_power &&
        !has_any_texture && !out_spec->has_render_flags) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest materials[] requires color channel or texture");
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_mesh(
    yyjson_val *mesh,
    bool *out_has_primitive,
    nmo_primitive_mesh_t *out_primitive,
    const char **out_obj_path)
{
    static const char *const allowed[] = {"primitive", "obj", NULL};
    const char *primitive = NULL;
    const char *obj_path = NULL;
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(mesh, "mesh", allowed));
    if (!out_has_primitive || !out_primitive || !out_obj_path) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "mesh output arguments are required");
    }
    *out_has_primitive = false;
    *out_obj_path = NULL;

    yyjson_val *primitive_value = yyjson_obj_get(mesh, "primitive");
    yyjson_val *obj_value = yyjson_obj_get(mesh, "obj");
    if ((primitive_value && obj_value) || (!primitive_value && !obj_value)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest mesh requires exactly one of primitive or obj");
    }

    if (primitive_value) {
        if (!yyjson_is_str(primitive_value) ||
            yyjson_get_str(primitive_value)[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest mesh.primitive must be a non-empty string");
        }
        primitive = yyjson_get_str(primitive_value);
        if (strcmp(primitive, "cube") != 0) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_SUPPORTED, NMO_SEVERITY_ERROR,
                             "unsupported manifest mesh primitive '%s'", primitive);
        }
        *out_primitive = NMO_PRIMITIVE_CUBE;
        *out_has_primitive = true;
    } else {
        if (!yyjson_is_str(obj_value) || yyjson_get_str(obj_value)[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest mesh.obj must be a non-empty string");
        }
        obj_path = yyjson_get_str(obj_value);
        *out_obj_path = obj_path;
    }

    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_vec4(
    yyjson_val *value,
    const char *field_name,
    float out_values[4])
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 4u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest %s must contain four numbers", field_name);
    }

    for (size_t i = 0u; i < 4u; ++i) {
        yyjson_val *item = yyjson_arr_get(value, i);
        if (!yyjson_is_num(item)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest %s values must be numbers", field_name);
        }
        out_values[i] = (float)manifest_get_number(item);
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_vec3(
    yyjson_val *value,
    const char *field_name,
    float out_values[3])
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 3u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest %s must contain three numbers", field_name);
    }

    for (size_t i = 0u; i < 3u; ++i) {
        yyjson_val *item = yyjson_arr_get(value, i);
        if (!yyjson_is_num(item)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest %s values must be numbers", field_name);
        }
        out_values[i] = (float)manifest_get_number(item);
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_sound(
    yyjson_val *sound,
    const char **out_file,
    bool *out_has_gain,
    float *out_gain,
    bool *out_has_pan,
    float *out_pan,
    bool *out_has_pitch,
    float *out_pitch,
    const char **out_attached,
    bool *out_has_position,
    float out_position[3],
    bool *out_has_direction,
    float out_direction[3])
{
    static const char *const allowed[] = {
        "file", "gain", "pan", "pitch", "attached_object",
        "position", "direction", NULL};
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(sound, "sound", allowed));
    if (!out_file || !out_has_gain || !out_gain ||
        !out_has_pan || !out_pan || !out_has_pitch || !out_pitch ||
        !out_attached || !out_has_position || !out_position ||
        !out_has_direction || !out_direction) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "sound output arguments are required");
    }
    *out_file = NULL;
    *out_has_gain = false;
    *out_has_pan = false;
    *out_has_pitch = false;
    *out_attached = NULL;
    *out_has_position = false;
    *out_has_direction = false;

    yyjson_val *file = yyjson_obj_get(sound, "file");
    if (!yyjson_is_str(file) || yyjson_get_str(file)[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest sound.file must be a non-empty string");
    }
    *out_file = yyjson_get_str(file);

    yyjson_val *gain = yyjson_obj_get(sound, "gain");
    if (gain) {
        if (!yyjson_is_num(gain)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest sound.gain must be numeric");
        }
        *out_has_gain = true;
        *out_gain = (float)manifest_get_number(gain);
    }
    yyjson_val *pan = yyjson_obj_get(sound, "pan");
    if (pan) {
        if (!yyjson_is_num(pan)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest sound.pan must be numeric");
        }
        *out_has_pan = true;
        *out_pan = (float)manifest_get_number(pan);
    }
    yyjson_val *pitch = yyjson_obj_get(sound, "pitch");
    if (pitch) {
        if (!yyjson_is_num(pitch)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest sound.pitch must be numeric");
        }
        *out_has_pitch = true;
        *out_pitch = (float)manifest_get_number(pitch);
    }
    NMO_RETURN_IF_ERROR(manifest_optional_string(
        sound,
        "attached_object",
        out_attached));
    yyjson_val *position = yyjson_obj_get(sound, "position");
    if (position) {
        NMO_RETURN_IF_ERROR(manifest_parse_vec3(
            position,
            "sound.position",
            out_position));
        *out_has_position = true;
    }
    yyjson_val *direction = yyjson_obj_get(sound, "direction");
    if (direction) {
        NMO_RETURN_IF_ERROR(manifest_parse_vec3(
            direction,
            "sound.direction",
            out_direction));
        *out_has_direction = true;
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_animation(
    yyjson_val *animation,
    const char **out_target,
    CK_OBJECTANIMATION_FORMAT *out_format,
    bool *out_has_root_position,
    float out_root_position[3],
    bool *out_has_flags,
    uint32_t *out_flags,
    bool *out_has_length,
    float *out_length)
{
    static const char *const allowed[] = {
        "target", "format", "root_position", "flags", "length", NULL};
    NMO_RETURN_IF_ERROR(
        manifest_reject_unknown_fields(animation, "animation", allowed));
    if (!out_target || !out_format || !out_has_root_position ||
        !out_root_position || !out_has_flags || !out_flags ||
        !out_has_length || !out_length) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "animation output arguments are required");
    }
    *out_target = NULL;
    *out_format = CKOBJANIM_FORMAT_CONTROLLERS;
    *out_has_root_position = false;
    *out_has_flags = false;
    *out_has_length = false;

    NMO_RETURN_IF_ERROR(manifest_required_string(
        animation,
        "target",
        out_target));

    const char *format = NULL;
    NMO_RETURN_IF_ERROR(manifest_required_string(
        animation,
        "format",
        &format));
    if (strcmp(format, "controllers") != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest animation.format only supports 'controllers'");
    }

    yyjson_val *root_position = yyjson_obj_get(animation, "root_position");
    if (root_position) {
        NMO_RETURN_IF_ERROR(manifest_parse_vec3(
            root_position,
            "animation.root_position",
            out_root_position));
        *out_has_root_position = true;
    }
    yyjson_val *flags = yyjson_obj_get(animation, "flags");
    if (flags) {
        if (!yyjson_is_uint(flags)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest animation.flags must be an unsigned integer");
        }
        *out_has_flags = true;
        *out_flags = (uint32_t)yyjson_get_uint(flags);
    }
    yyjson_val *length = yyjson_obj_get(animation, "length");
    if (length) {
        if (!yyjson_is_num(length)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest animation.length must be numeric");
        }
        *out_has_length = true;
        *out_length = (float)manifest_get_number(length);
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_camera(
    yyjson_val *camera,
    float *out_fov,
    float *out_near,
    float *out_far)
{
    static const char *const allowed[] = {"fov", "near", "far", "target", NULL};
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(camera, "camera", allowed));
    if (!out_fov || !out_near || !out_far) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "camera output arguments are required");
    }

    yyjson_val *fov = yyjson_obj_get(camera, "fov");
    yyjson_val *near_plane = yyjson_obj_get(camera, "near");
    yyjson_val *far_plane = yyjson_obj_get(camera, "far");
    if (!yyjson_is_num(fov) || !yyjson_is_num(near_plane) || !yyjson_is_num(far_plane)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest camera requires numeric fov, near, and far");
    }
    *out_fov = (float)manifest_get_number(fov);
    *out_near = (float)manifest_get_number(near_plane);
    *out_far = (float)manifest_get_number(far_plane);
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_light_type(
    const char *type,
    VXLIGHT_TYPE *out_type)
{
    if (!type || !out_type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "light type output is required");
    }
    if (strcmp(type, "point") == 0) {
        *out_type = VX_LIGHTPOINT;
    } else if (strcmp(type, "spot") == 0) {
        *out_type = VX_LIGHTSPOT;
    } else if (strcmp(type, "directional") == 0) {
        *out_type = VX_LIGHTDIREC;
    } else if (strcmp(type, "parallel") == 0) {
        *out_type = VX_LIGHTPARA;
    } else {
        NMO_RETURN_ERROR(NMO_ERR_NOT_SUPPORTED, NMO_SEVERITY_ERROR,
                         "unsupported manifest light.type '%s'", type);
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_light(
    yyjson_val *light,
    float out_diffuse[4],
    float *out_range,
    VXLIGHT_TYPE *out_type)
{
    static const char *const allowed[] = {"diffuse", "range", "type", "target", NULL};
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(light, "light", allowed));
    if (!out_diffuse || !out_range || !out_type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "light output arguments are required");
    }

    yyjson_val *diffuse = yyjson_obj_get(light, "diffuse");
    yyjson_val *range = yyjson_obj_get(light, "range");
    yyjson_val *type = yyjson_obj_get(light, "type");
    if (!diffuse || !yyjson_is_num(range) || !yyjson_is_str(type)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest light requires diffuse, range, and type");
    }
    NMO_RETURN_IF_ERROR(manifest_parse_vec4(diffuse, "light.diffuse", out_diffuse));
    *out_range = (float)manifest_get_number(range);
    NMO_RETURN_IF_ERROR(manifest_parse_light_type(yyjson_get_str(type), out_type));
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_fog_mode(
    const char *mode,
    VXFOG_MODE *out_mode)
{
    if (!mode || !out_mode) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "fog mode output is required");
    }
    if (strcmp(mode, "none") == 0) {
        *out_mode = VXFOG_NONE;
    } else if (strcmp(mode, "exp") == 0) {
        *out_mode = VXFOG_EXP;
    } else if (strcmp(mode, "exp2") == 0) {
        *out_mode = VXFOG_EXP2;
    } else if (strcmp(mode, "linear") == 0) {
        *out_mode = VXFOG_LINEAR;
    } else {
        NMO_RETURN_ERROR(NMO_ERR_NOT_SUPPORTED, NMO_SEVERITY_ERROR,
                         "unsupported manifest fog.mode '%s'", mode);
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_scene_environment(
    nmo_project_plan_t *plan,
    uint32_t scene_handle,
    yyjson_val *environment)
{
    static const char *const allowed[] = {
        "background_color", "ambient_light", "fog", NULL};
    if (!environment) {
        NMO_RETURN_OK();
    }
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(
        environment,
        "environment",
        allowed));

    yyjson_val *background = yyjson_obj_get(environment, "background_color");
    if (background) {
        float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        NMO_RETURN_IF_ERROR(manifest_parse_vec4(
            background,
            "environment.background_color",
            color));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_scene_background_color(
            plan,
            scene_handle,
            color[0],
            color[1],
            color[2],
            color[3]));
    }

    yyjson_val *ambient = yyjson_obj_get(environment, "ambient_light");
    if (ambient) {
        float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        NMO_RETURN_IF_ERROR(manifest_parse_vec4(
            ambient,
            "environment.ambient_light",
            color));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_scene_ambient_light(
            plan,
            scene_handle,
            color[0],
            color[1],
            color[2],
            color[3]));
    }

    yyjson_val *fog = yyjson_obj_get(environment, "fog");
    if (fog) {
        static const char *const fog_allowed[] = {
            "mode", "color", "start", "end", "density", NULL};
        NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(
            fog,
            "environment.fog",
            fog_allowed));
        yyjson_val *mode_value = yyjson_obj_get(fog, "mode");
        yyjson_val *color_value = yyjson_obj_get(fog, "color");
        yyjson_val *start_value = yyjson_obj_get(fog, "start");
        yyjson_val *end_value = yyjson_obj_get(fog, "end");
        yyjson_val *density_value = yyjson_obj_get(fog, "density");
        if (!yyjson_is_str(mode_value) || !color_value ||
            !yyjson_is_num(start_value) || !yyjson_is_num(end_value) ||
            !yyjson_is_num(density_value)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest environment.fog requires mode, color, start, end, and density");
        }
        VXFOG_MODE mode = VXFOG_NONE;
        float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        NMO_RETURN_IF_ERROR(manifest_parse_fog_mode(
            yyjson_get_str(mode_value),
            &mode));
        NMO_RETURN_IF_ERROR(manifest_parse_vec4(
            color_value,
            "environment.fog.color",
            color));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_scene_fog(
            plan,
            scene_handle,
            mode,
            color[0],
            color[1],
            color[2],
            color[3],
            (float)manifest_get_number(start_value),
            (float)manifest_get_number(end_value),
            (float)manifest_get_number(density_value)));
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_transform(
    yyjson_val *transform,
    bool *out_has_position,
    float out_position[3],
    bool *out_has_rotation,
    float out_rotation[3],
    bool *out_has_scale,
    float out_scale[3])
{
    static const char *const allowed[] = {
        "position", "rotation_euler_deg", "scale", NULL};
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(transform, "transform", allowed));

    if (!out_has_position || !out_position ||
        !out_has_rotation || !out_rotation ||
        !out_has_scale || !out_scale) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "transform output arguments are required");
    }
    *out_has_position = false;
    *out_has_rotation = false;
    *out_has_scale = false;

    yyjson_val *position = yyjson_obj_get(transform, "position");
    yyjson_val *rotation = yyjson_obj_get(transform, "rotation_euler_deg");
    yyjson_val *scale = yyjson_obj_get(transform, "scale");
    if (!position && !rotation && !scale) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest transform requires position, rotation_euler_deg, or scale");
    }

    yyjson_val *values[] = {position, rotation, scale};
    float *outputs[] = {out_position, out_rotation, out_scale};
    bool *has_outputs[] = {out_has_position, out_has_rotation, out_has_scale};
    const char *names[] = {"position", "rotation_euler_deg", "scale"};
    for (size_t value_index = 0u; value_index < 3u; ++value_index) {
        yyjson_val *value = values[value_index];
        if (!value) {
            continue;
        }
        if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 3u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest transform.%s must contain three numbers",
                             names[value_index]);
        }
        for (size_t i = 0u; i < 3u; ++i) {
            yyjson_val *item = yyjson_arr_get(value, i);
            if (!yyjson_is_num(item)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest transform.%s values must be numbers",
                                 names[value_index]);
            }
            outputs[value_index][i] = (float)manifest_get_number(item);
        }
        *has_outputs[value_index] = true;
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_register_object(
    manifest_parse_ctx_t *ctx,
    const char *id,
    const char *name,
    uint32_t handle)
{
    if (!ctx || !name || handle == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "object registration requires arguments");
    }
    if (id) {
        for (size_t i = 0u; i < ctx->object_count; ++i) {
            if (ctx->object_ids[i] && strcmp(ctx->object_ids[i], id) == 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest object id '%s' is duplicated", id);
            }
        }
    }
    if (ctx->object_count == ctx->object_capacity) {
        size_t new_capacity = ctx->object_capacity ? ctx->object_capacity * 2u : 8u;
        const char **new_ids = (const char **)calloc(
            new_capacity,
            sizeof(*new_ids));
        const char **new_names = (const char **)calloc(
            new_capacity,
            sizeof(*new_names));
        uint32_t *new_handles = (uint32_t *)calloc(
            new_capacity,
            sizeof(*new_handles));
        if (!new_ids || !new_names || !new_handles) {
            free(new_ids);
            free(new_names);
            free(new_handles);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate manifest object map");
        }
        memcpy(new_ids,
               ctx->object_ids,
               ctx->object_count * sizeof(*new_ids));
        memcpy(new_names,
               ctx->object_names,
               ctx->object_count * sizeof(*new_names));
        memcpy(new_handles,
               ctx->object_handles,
               ctx->object_count * sizeof(*new_handles));
        free(ctx->object_ids);
        free(ctx->object_names);
        free(ctx->object_handles);
        ctx->object_ids = new_ids;
        ctx->object_names = new_names;
        ctx->object_handles = new_handles;
        ctx->object_capacity = new_capacity;
    }
    ctx->object_ids[ctx->object_count] = id;
    ctx->object_names[ctx->object_count] = name;
    ctx->object_handles[ctx->object_count] = handle;
    ctx->object_count++;
    NMO_RETURN_OK();
}

static nmo_status_t manifest_resolve_object_ref(
    const manifest_parse_ctx_t *ctx,
    const char *ref,
    uint32_t *out_handle)
{
    if (!ctx || !ref || !out_handle) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "object reference lookup requires arguments");
    }
    *out_handle = 0u;

    for (size_t i = 0u; i < ctx->object_count; ++i) {
        if (ctx->object_ids[i] && strcmp(ctx->object_ids[i], ref) == 0) {
            *out_handle = ctx->object_handles[i];
            NMO_RETURN_OK();
        }
    }

    size_t match_count = 0u;
    uint32_t match_handle = 0u;
    for (size_t i = 0u; i < ctx->object_count; ++i) {
        if (strcmp(ctx->object_names[i], ref) == 0) {
            match_count++;
            match_handle = ctx->object_handles[i];
        }
    }
    if (match_count == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "manifest object reference '%s' was not found", ref);
    }
    if (match_count > 1u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest object reference '%s' is ambiguous", ref);
    }

    *out_handle = match_handle;
    NMO_RETURN_OK();
}

static nmo_status_t manifest_count_fields(
    yyjson_val *fields,
    size_t *out_count)
{
    if (!out_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "out_count is required");
    }
    *out_count = 0u;
    if (!fields) {
        NMO_RETURN_OK();
    }
    if (!yyjson_is_obj(fields)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest object.fields must be an object");
    }

    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *key = NULL;
    yyjson_val *val = NULL;
    yyjson_obj_foreach(fields, idx, max, key, val) {
        if (!yyjson_is_str(key) || !yyjson_is_str(val)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest object fields must map strings to strings");
        }
        (*out_count)++;
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_fill_fields(
    yyjson_val *fields,
    nmo_session_field_edit_t *out_fields)
{
    if (!fields || !out_fields) {
        NMO_RETURN_OK();
    }

    size_t idx = 0u;
    size_t max = 0u;
    size_t out_index = 0u;
    yyjson_val *key = NULL;
    yyjson_val *val = NULL;
    yyjson_obj_foreach(fields, idx, max, key, val) {
        out_fields[out_index].field_name = yyjson_get_str(key);
        out_fields[out_index].value_str = yyjson_get_str(val);
        out_index++;
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_scripts(
    manifest_parse_ctx_t *ctx,
    yyjson_val *scripts,
    uint32_t object_handle)
{
    if (!scripts) {
        NMO_RETURN_OK();
    }
    if (!yyjson_is_arr(scripts)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest object.scripts must be an array");
    }

    size_t script_index = 0u;
    size_t script_max = 0u;
    yyjson_val *script_obj = NULL;
    yyjson_arr_foreach(scripts, script_index, script_max, script_obj) {
        static const char *const allowed[] = {
            "name", "debug_output", "template", "message", NULL};
        const char *name = NULL;
        uint32_t script_handle = 0u;
        NMO_RETURN_IF_ERROR(
            manifest_reject_unknown_fields(script_obj, "script", allowed));
        NMO_RETURN_IF_ERROR(manifest_required_string(script_obj, "name", &name));
        NMO_RETURN_IF_ERROR(nmo_project_plan_add_object_script(
            ctx->plan,
            object_handle,
            name,
            &script_handle));

        const char *template_name = NULL;
        NMO_RETURN_IF_ERROR(manifest_optional_string(
            script_obj,
            "template",
            &template_name));
        if (template_name) {
            const char *message = NULL;
            nmo_project_script_step_kind_t template_kind = 0;
            if (strcmp(template_name, "on_start_debug_output") == 0) {
                template_kind = NMO_PROJECT_SCRIPT_STEP_ON_START_DEBUG_OUTPUT;
            } else if (strcmp(template_name, "scene_on_start_debug_output") == 0) {
                template_kind =
                    NMO_PROJECT_SCRIPT_STEP_SCENE_ON_START_DEBUG_OUTPUT;
            } else if (strcmp(template_name, "timer_debug_output") == 0) {
                template_kind = NMO_PROJECT_SCRIPT_STEP_TIMER_DEBUG_OUTPUT;
            } else if (strcmp(template_name, "input_key_debug_output") == 0) {
                template_kind = NMO_PROJECT_SCRIPT_STEP_INPUT_KEY_DEBUG_OUTPUT;
            } else if (strcmp(template_name, "object_trigger_debug_output") == 0) {
                template_kind =
                    NMO_PROJECT_SCRIPT_STEP_OBJECT_TRIGGER_DEBUG_OUTPUT;
            } else if (strcmp(template_name, "scene_start_then_timer_debug_output") == 0) {
                template_kind =
                    NMO_PROJECT_SCRIPT_STEP_SCENE_START_THEN_TIMER_DEBUG_OUTPUT;
            } else {
                NMO_RETURN_ERROR(NMO_ERR_NOT_SUPPORTED, NMO_SEVERITY_ERROR,
                                 "unsupported manifest script template '%s'",
                                 template_name);
            }
            NMO_RETURN_IF_ERROR(manifest_required_string(
                script_obj,
                "message",
                &message));
            if (template_kind ==
                NMO_PROJECT_SCRIPT_STEP_SCENE_ON_START_DEBUG_OUTPUT) {
                NMO_RETURN_IF_ERROR(
                    nmo_project_plan_script_add_scene_on_start_debug_output(
                        ctx->plan,
                        script_handle,
                        message));
            } else if (template_kind ==
                       NMO_PROJECT_SCRIPT_STEP_TIMER_DEBUG_OUTPUT) {
                NMO_RETURN_IF_ERROR(nmo_project_plan_script_add_timer_debug_output(
                    ctx->plan,
                    script_handle,
                    message));
            } else if (template_kind ==
                       NMO_PROJECT_SCRIPT_STEP_INPUT_KEY_DEBUG_OUTPUT) {
                NMO_RETURN_IF_ERROR(
                    nmo_project_plan_script_add_input_key_debug_output(
                        ctx->plan,
                        script_handle,
                        message));
            } else if (template_kind ==
                       NMO_PROJECT_SCRIPT_STEP_OBJECT_TRIGGER_DEBUG_OUTPUT) {
                NMO_RETURN_IF_ERROR(
                    nmo_project_plan_script_add_object_trigger_debug_output(
                        ctx->plan,
                        script_handle,
                        message));
            } else if (template_kind ==
                       NMO_PROJECT_SCRIPT_STEP_SCENE_START_THEN_TIMER_DEBUG_OUTPUT) {
                NMO_RETURN_IF_ERROR(
                    nmo_project_plan_script_add_scene_start_then_timer_debug_output(
                        ctx->plan,
                        script_handle,
                        message));
            } else {
                NMO_RETURN_IF_ERROR(
                    nmo_project_plan_script_add_on_start_debug_output(
                    ctx->plan,
                    script_handle,
                    message));
            }
        }

        yyjson_val *debug = yyjson_obj_get(script_obj, "debug_output");
        if (!debug) {
            continue;
        }
        if (!yyjson_is_arr(debug)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest script.debug_output must be an array");
        }
        size_t debug_index = 0u;
        size_t debug_max = 0u;
        yyjson_val *message = NULL;
        yyjson_arr_foreach(debug, debug_index, debug_max, message) {
            if (!yyjson_is_str(message)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "manifest debug_output entries must be strings");
            }
            NMO_RETURN_IF_ERROR(nmo_project_plan_script_add_debug_output(
                ctx->plan,
                script_handle,
                yyjson_get_str(message)));
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_object_declare(
    manifest_parse_ctx_t *ctx,
    yyjson_val *object,
    uint32_t scene_handle,
    size_t scene_index,
    size_t object_index)
{
    static const char *const allowed[] = {
        "id", "name", "class", "parent", "fields", "mesh", "material", "materials",
        "transform", "camera", "light", "sound", "animation", "scripts", NULL};
    const char *id = NULL;
    const char *name = NULL;
    const char *class_name = NULL;
    nmo_class_id_t class_id = 0;
    nmo_session_field_edit_t *fields = NULL;
    size_t field_count = 0u;
    uint32_t object_handle = 0u;

    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(object, "object", allowed));
    NMO_RETURN_IF_ERROR(manifest_optional_string(object, "id", &id));
    if (id && id[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest object.id must be a non-empty string");
    }
    NMO_RETURN_IF_ERROR(manifest_required_string(object, "name", &name));
    NMO_RETURN_IF_ERROR(manifest_required_string(object, "class", &class_name));

    class_id = nmo_type_query_class_id_from_name(
        nmo_context_get_type_registry(ctx->context),
        class_name);
    if (class_id == 0 || class_id == NMO_CLASS_ID_INVALID) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "manifest object class '%s' is not registered", class_name);
    }

    yyjson_val *fields_obj = yyjson_obj_get(object, "fields");
    NMO_RETURN_IF_ERROR(manifest_count_fields(fields_obj, &field_count));
    if (field_count > 0u) {
        fields = (nmo_session_field_edit_t *)calloc(field_count, sizeof(*fields));
        if (!fields) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate manifest object fields");
        }
        nmo_status_t field_status = manifest_fill_fields(fields_obj, fields);
        if (field_status != NMO_OK) {
            free(fields);
            return field_status;
        }
    }

    nmo_status_t status = nmo_project_plan_add_object(
        ctx->plan,
        &(nmo_project_object_spec_t){
            .scene_handle = scene_handle,
            .class_id = class_id,
            .name = name,
            .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
            .fields = fields,
            .field_count = field_count,
        },
        &object_handle);
    free(fields);
    if (status != NMO_OK) {
        return status;
    }
    NMO_RETURN_IF_ERROR(manifest_register_object(ctx, id, name, object_handle));
    char source_path[96];
    snprintf(source_path,
             sizeof(source_path),
             "scenes[%zu].objects[%zu]",
             scene_index,
             object_index);
    NMO_RETURN_IF_ERROR(nmo_project_plan_set_object_source_path(
        ctx->plan,
        object_handle,
        source_path));
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_object_details(
    manifest_parse_ctx_t *ctx,
    yyjson_val *object,
    uint32_t object_handle,
    size_t scene_index,
    size_t object_index)
{
    static const char *const allowed[] = {
        "id", "name", "class", "parent", "fields", "mesh", "material", "materials",
        "transform", "camera", "light", "sound", "animation", "scripts", NULL};
    const char *parent_ref = NULL;

    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(object, "object", allowed));
    NMO_RETURN_IF_ERROR(manifest_optional_string(object, "parent", &parent_ref));
    if (parent_ref) {
        uint32_t parent_handle = 0u;
        NMO_RETURN_IF_ERROR(manifest_resolve_object_ref(
            ctx,
            parent_ref,
            &parent_handle));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_object_parent(
            ctx->plan,
            object_handle,
            parent_handle));
    }

    yyjson_val *mesh = yyjson_obj_get(object, "mesh");
    if (mesh) {
        bool has_primitive = false;
        nmo_primitive_mesh_t primitive = NMO_PRIMITIVE_CUBE;
        const char *obj_path = NULL;
        NMO_RETURN_IF_ERROR(manifest_parse_mesh(
            mesh,
            &has_primitive,
            &primitive,
            &obj_path));
        if (has_primitive) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_primitive_mesh(
                ctx->plan,
                object_handle,
                primitive));
        } else {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_external_mesh(
                ctx->plan,
                object_handle,
                obj_path));
            char source_path[128];
            snprintf(source_path,
                     sizeof(source_path),
                     "scenes[%zu].objects[%zu].mesh.obj",
                     scene_index,
                     object_index);
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_external_mesh_source_path(
                ctx->plan,
                object_handle,
                source_path));
        }
    }

    yyjson_val *material = yyjson_obj_get(object, "material");
    if (material) {
        bool has_color = false;
        float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool has_ambient = false;
        float ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool has_specular = false;
        float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool has_emissive = false;
        float emissive[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool has_specular_power = false;
        float specular_power = 0.0f;
        const char *texture = NULL;
        bool has_texture_slots[4] = {false, false, false, false};
        const char *texture_paths[4] = {0};
        size_t texture_indices[4] = {0};
        bool has_render_flags = false;
        nmo_project_material_render_flags_t render_flags = {0};
        NMO_RETURN_IF_ERROR(manifest_parse_material(
            material,
            &has_color,
            color,
            &has_ambient,
            ambient,
            &has_specular,
            specular,
            &has_emissive,
            emissive,
            &has_specular_power,
            &specular_power,
            &texture,
            has_texture_slots,
            texture_paths,
            texture_indices,
            &has_render_flags,
            &render_flags));
        if (has_color) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_color(
                ctx->plan,
                object_handle,
                color[0],
                color[1],
                color[2],
                color[3]));
        }
        if (has_ambient) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_ambient(
                ctx->plan,
                object_handle,
                ambient[0],
                ambient[1],
                ambient[2],
                ambient[3]));
        }
        if (has_specular) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_specular(
                ctx->plan,
                object_handle,
                specular[0],
                specular[1],
                specular[2],
                specular[3]));
        }
        if (has_emissive) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_emissive(
                ctx->plan,
                object_handle,
                emissive[0],
                emissive[1],
                emissive[2],
                emissive[3]));
        }
        if (has_specular_power) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_specular_power(
                ctx->plan,
                object_handle,
                specular_power));
        }
        if (has_render_flags) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_render_flags(
                ctx->plan,
                object_handle,
                &render_flags));
        }
        if (has_texture_slots[0]) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_texture(
                ctx->plan,
                object_handle,
                texture_paths[0]));
            char source_path[128];
            if (texture) {
                snprintf(source_path,
                         sizeof(source_path),
                         "scenes[%zu].objects[%zu].material.texture",
                         scene_index,
                         object_index);
            } else {
                snprintf(source_path,
                         sizeof(source_path),
                         "scenes[%zu].objects[%zu].material.textures[%zu].path",
                         scene_index,
                         object_index,
                         texture_indices[0]);
            }
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_texture_source_path(
                ctx->plan,
                object_handle,
                source_path));
        }
        for (size_t slot = 1u; slot < 4u; ++slot) {
            if (!has_texture_slots[slot]) {
                continue;
            }
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_texture_slot(
                ctx->plan,
                object_handle,
                (uint32_t)slot,
                texture_paths[slot]));
            char source_path[144];
            snprintf(source_path,
                     sizeof(source_path),
                     "scenes[%zu].objects[%zu].material.textures[%zu].path",
                     scene_index,
                     object_index,
                     texture_indices[slot]);
            NMO_RETURN_IF_ERROR(
                nmo_project_plan_set_material_texture_slot_source_path(
                    ctx->plan,
                    object_handle,
                    (uint32_t)slot,
                    source_path));
        }
    }

    yyjson_val *materials = yyjson_obj_get(object, "materials");
    if (materials) {
        if (!yyjson_is_arr(materials)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest materials must be an array");
        }
        size_t idx = 0u;
        size_t max = 0u;
        yyjson_val *item = NULL;
        yyjson_arr_foreach(materials, idx, max, item) {
            nmo_project_material_spec_t spec = {0};
            size_t texture_indices[4] = {0};
            NMO_RETURN_IF_ERROR(manifest_parse_obj_material(
                item,
                &spec,
                texture_indices));
            bool has_texture_shorthand = yyjson_obj_get(item, "texture") != NULL;
            size_t material_plan_index =
                nmo_project_plan_obj_material_count(ctx->plan, object_handle);
            char material_source_path[128];
            char texture_source_paths[4][144];
            snprintf(material_source_path,
                     sizeof(material_source_path),
                     "scenes[%zu].objects[%zu].materials[%zu]",
                     scene_index,
                     object_index,
                     idx);
            spec.source_path = material_source_path;
            const char *texture_source = NULL;
            for (size_t slot = 0u; slot < 4u; ++slot) {
                if (!spec.has_texture_slots[slot]) {
                    continue;
                }
                if (slot == 0u && has_texture_shorthand) {
                    snprintf(texture_source_paths[slot],
                             sizeof(texture_source_paths[slot]),
                             "scenes[%zu].objects[%zu].materials[%zu].texture",
                             scene_index,
                             object_index,
                             idx);
                    texture_source = texture_source_paths[slot];
                } else {
                    snprintf(texture_source_paths[slot],
                             sizeof(texture_source_paths[slot]),
                             "scenes[%zu].objects[%zu].materials[%zu].textures[%zu].path",
                             scene_index,
                             object_index,
                             idx,
                             texture_indices[slot]);
                }
                spec.texture_source_paths[slot] = texture_source_paths[slot];
            }
            spec.texture_source_path = texture_source;
            NMO_RETURN_IF_ERROR(nmo_project_plan_add_obj_material(
                ctx->plan,
                object_handle,
                &spec));
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_obj_material_source_paths(
                ctx->plan,
                object_handle,
                material_plan_index,
                material_source_path,
                texture_source));
        }
    }

    yyjson_val *transform = yyjson_obj_get(object, "transform");
    if (transform) {
        bool has_position = false;
        float position[3] = {0.0f, 0.0f, 0.0f};
        bool has_rotation = false;
        float rotation[3] = {0.0f, 0.0f, 0.0f};
        bool has_scale = false;
        float scale[3] = {0.0f, 0.0f, 0.0f};
        NMO_RETURN_IF_ERROR(manifest_parse_transform(
            transform,
            &has_position,
            position,
            &has_rotation,
            rotation,
            &has_scale,
            scale));
        if (has_position) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_object_position(
                ctx->plan,
                object_handle,
                position[0],
                position[1],
                position[2]));
        }
        if (has_rotation) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_object_rotation_euler_deg(
                ctx->plan,
                object_handle,
                rotation[0],
                rotation[1],
                rotation[2]));
        }
        if (has_scale) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_object_scale(
                ctx->plan,
                object_handle,
                scale[0],
                scale[1],
                scale[2]));
        }
    }

    yyjson_val *camera = yyjson_obj_get(object, "camera");
    if (camera) {
        float fov = 0.0f;
        float near_plane = 0.0f;
        float far_plane = 0.0f;
        NMO_RETURN_IF_ERROR(manifest_parse_camera(
            camera,
            &fov,
            &near_plane,
            &far_plane));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_camera_settings(
            ctx->plan,
            object_handle,
            fov,
            near_plane,
            far_plane));
        const char *target_ref = NULL;
        NMO_RETURN_IF_ERROR(manifest_optional_string(camera, "target", &target_ref));
        if (target_ref) {
            uint32_t target_handle = 0u;
            NMO_RETURN_IF_ERROR(manifest_resolve_object_ref(
                ctx,
                target_ref,
                &target_handle));
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_camera_target(
                ctx->plan,
                object_handle,
                target_handle));
        }
    }

    yyjson_val *light = yyjson_obj_get(object, "light");
    if (light) {
        float diffuse[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float range = 0.0f;
        VXLIGHT_TYPE type = VX_LIGHTPOINT;
        NMO_RETURN_IF_ERROR(manifest_parse_light(
            light,
            diffuse,
            &range,
            &type));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_light_settings(
            ctx->plan,
            object_handle,
            diffuse[0],
            diffuse[1],
            diffuse[2],
            diffuse[3],
            range,
            type));
        const char *target_ref = NULL;
        NMO_RETURN_IF_ERROR(manifest_optional_string(light, "target", &target_ref));
        if (target_ref) {
            uint32_t target_handle = 0u;
            NMO_RETURN_IF_ERROR(manifest_resolve_object_ref(
                ctx,
                target_ref,
                &target_handle));
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_light_target(
                ctx->plan,
                object_handle,
                target_handle));
        }
    }

    yyjson_val *sound = yyjson_obj_get(object, "sound");
    if (sound) {
        const char *file = NULL;
        bool has_gain = false;
        float gain = 0.0f;
        bool has_pan = false;
        float pan = 0.0f;
        bool has_pitch = false;
        float pitch = 0.0f;
        const char *attached_ref = NULL;
        bool has_position = false;
        float position[3] = {0.0f, 0.0f, 0.0f};
        bool has_direction = false;
        float direction[3] = {0.0f, 0.0f, 0.0f};
        NMO_RETURN_IF_ERROR(manifest_parse_sound(
            sound,
            &file,
            &has_gain,
            &gain,
            &has_pan,
            &pan,
            &has_pitch,
            &pitch,
            &attached_ref,
            &has_position,
            position,
            &has_direction,
            direction));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_wavesound_file(
            ctx->plan,
            object_handle,
            file));
        char source_path[128];
        snprintf(source_path,
                 sizeof(source_path),
                 "scenes[%zu].objects[%zu].sound.file",
                 scene_index,
                 object_index);
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_wavesound_file_source_path(
            ctx->plan,
            object_handle,
            source_path));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_wavesound_playback(
            ctx->plan,
            object_handle,
            has_gain,
            gain,
            has_pan,
            pan,
            has_pitch,
            pitch));
        if (attached_ref) {
            uint32_t attached_handle = 0u;
            NMO_RETURN_IF_ERROR(manifest_resolve_object_ref(
                ctx,
                attached_ref,
                &attached_handle));
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_wavesound_attached_object(
                ctx->plan,
                object_handle,
                attached_handle));
        }
        if (has_position || has_direction) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_set_wavesound_spatial(
                ctx->plan,
                object_handle,
                has_position,
                position[0],
                position[1],
                position[2],
                has_direction,
                direction[0],
                direction[1],
                direction[2]));
        }
    }

    yyjson_val *animation = yyjson_obj_get(object, "animation");
    if (animation) {
        const char *target_ref = NULL;
        CK_OBJECTANIMATION_FORMAT format = CKOBJANIM_FORMAT_CONTROLLERS;
        bool has_root_position = false;
        float root_position[3] = {0.0f, 0.0f, 0.0f};
        bool has_flags = false;
        uint32_t flags = 0u;
        bool has_length = false;
        float length = 0.0f;
        uint32_t target_handle = 0u;
        NMO_RETURN_IF_ERROR(manifest_parse_animation(
            animation,
            &target_ref,
            &format,
            &has_root_position,
            root_position,
            &has_flags,
            &flags,
            &has_length,
            &length));
        NMO_RETURN_IF_ERROR(manifest_resolve_object_ref(
            ctx,
            target_ref,
            &target_handle));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_object_animation(
            ctx->plan,
            object_handle,
            target_handle,
            format,
            has_root_position,
            root_position[0],
            root_position[1],
            root_position[2],
            has_flags,
            flags,
            has_length,
            length));
    }

    NMO_RETURN_IF_ERROR(manifest_parse_scripts(
        ctx,
        yyjson_obj_get(object, "scripts"),
        object_handle));
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_scene(
    manifest_parse_ctx_t *ctx,
    yyjson_val *scene,
    size_t scene_index)
{
    static const char *const allowed[] = {
        "name", "objects", "active_camera", "startup_active", "environment", NULL};
    const char *name = NULL;
    uint32_t scene_handle = 0u;
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(scene, "scene", allowed));
    NMO_RETURN_IF_ERROR(manifest_required_string(scene, "name", &name));
    NMO_RETURN_IF_ERROR(nmo_project_plan_add_scene(ctx->plan, name, &scene_handle));
    char scene_source_path[64];
    snprintf(scene_source_path,
             sizeof(scene_source_path),
             "scenes[%zu]",
             scene_index);
    NMO_RETURN_IF_ERROR(nmo_project_plan_set_scene_source_path(
        ctx->plan,
        scene_handle,
        scene_source_path));
    NMO_RETURN_IF_ERROR(manifest_parse_scene_environment(
        ctx->plan,
        scene_handle,
        yyjson_obj_get(scene, "environment")));

    yyjson_val *objects = yyjson_obj_get(scene, "objects");
    if (!objects) {
        NMO_RETURN_OK();
    }
    if (!yyjson_is_arr(objects)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest scene.objects must be an array");
    }

    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *object = NULL;
    size_t object_start = ctx->object_count;
    yyjson_arr_foreach(objects, idx, max, object) {
        NMO_RETURN_IF_ERROR(manifest_parse_object_declare(
            ctx,
            object,
            scene_handle,
            scene_index,
            idx));
    }

    idx = 0u;
    max = 0u;
    object = NULL;
    yyjson_arr_foreach(objects, idx, max, object) {
        NMO_RETURN_IF_ERROR(manifest_parse_object_details(
            ctx,
            object,
            ctx->object_handles[object_start + idx],
            scene_index,
            idx));
    }

    const char *active_camera_ref = NULL;
    NMO_RETURN_IF_ERROR(manifest_optional_string(
        scene,
        "active_camera",
        &active_camera_ref));
    if (active_camera_ref) {
        uint32_t active_camera_handle = 0u;
        NMO_RETURN_IF_ERROR(manifest_resolve_object_ref(
            ctx,
            active_camera_ref,
            &active_camera_handle));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_scene_active_camera(
            ctx->plan,
            scene_handle,
            active_camera_handle));
        char active_camera_source_path[96];
        snprintf(active_camera_source_path,
                 sizeof(active_camera_source_path),
                 "scenes[%zu].active_camera",
                 scene_index);
        NMO_RETURN_IF_ERROR(
            nmo_project_plan_set_scene_active_camera_source_path(
                ctx->plan,
                scene_handle,
                active_camera_source_path));
    }

    yyjson_val *startup_active = yyjson_obj_get(scene, "startup_active");
    if (startup_active) {
        if (!yyjson_is_bool(startup_active)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest scene.startup_active must be a boolean");
        }
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_scene_startup_active(
            ctx->plan,
            scene_handle,
            yyjson_get_bool(startup_active)));
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_root(
    manifest_parse_ctx_t *ctx,
    yyjson_val *root,
    nmo_project_manifest_t *out_manifest)
{
    static const char *const root_allowed[] = {
        "version", "output", "document", "scenes", NULL};
    static const char *const document_allowed[] = {"name", NULL};
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(root, "root", root_allowed));

    yyjson_val *version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != 1u) {
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                         "manifest version must be 1");
    }

    const char *output_path = NULL;
    NMO_RETURN_IF_ERROR(manifest_optional_string(root, "output", &output_path));
    if (output_path) {
        out_manifest->output_path = manifest_strdup(output_path);
        if (!out_manifest->output_path) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate manifest output path");
        }
    }

    yyjson_val *document = yyjson_obj_get(root, "document");
    NMO_RETURN_IF_ERROR(
        manifest_reject_unknown_fields(document, "document", document_allowed));
    const char *document_name = NULL;
    NMO_RETURN_IF_ERROR(manifest_required_string(document, "name", &document_name));
    NMO_RETURN_IF_ERROR(nmo_project_plan_set_document_name(ctx->plan, document_name));

    yyjson_val *scenes = yyjson_obj_get(root, "scenes");
    if (!scenes) {
        NMO_RETURN_OK();
    }
    if (!yyjson_is_arr(scenes)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest scenes must be an array");
    }
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *scene = NULL;
    yyjson_arr_foreach(scenes, idx, max, scene) {
        NMO_RETURN_IF_ERROR(manifest_parse_scene(ctx, scene, idx));
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_manifest_json_read_manifest(
    const char *json,
    size_t json_size,
    nmo_project_manifest_t *out_manifest)
{
    if (!json || json_size == 0u || !out_manifest) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "json data and out_manifest are required");
    }

    nmo_project_manifest_init(out_manifest);
    yyjson_doc *doc = yyjson_read(json, json_size, 0);
    if (!doc) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "failed to parse project manifest JSON");
    }

    nmo_status_t status = nmo_project_plan_create(&out_manifest->plan);
    manifest_parse_ctx_t ctx = {0};
    if (status == NMO_OK) {
        ctx.context = nmo_context_create(&(nmo_context_desc_t){0});
        if (!ctx.context) {
            status = NMO_ERR_NOMEM;
        }
    }
    if (status == NMO_OK) {
        ctx.plan = out_manifest->plan;
        yyjson_val *root = yyjson_doc_get_root(doc);
        status = manifest_parse_root(&ctx, root, out_manifest);
    }

    if (ctx.context) {
        nmo_context_release(ctx.context);
    }
    free(ctx.object_ids);
    free(ctx.object_names);
    free(ctx.object_handles);
    yyjson_doc_free(doc);
    if (status != NMO_OK) {
        nmo_project_manifest_dispose(out_manifest);
        return status;
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_manifest_json_read(
    const char *json,
    size_t json_size,
    nmo_project_plan_t **out_plan)
{
    if (!out_plan) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "out_plan is required");
    }
    *out_plan = NULL;

    nmo_project_manifest_t manifest;
    nmo_project_manifest_init(&manifest);
    nmo_status_t status =
        nmo_project_manifest_json_read_manifest(json, json_size, &manifest);
    if (status != NMO_OK) {
        return status;
    }

    *out_plan = manifest.plan;
    manifest.plan = NULL;
    nmo_project_manifest_dispose(&manifest);
    NMO_RETURN_OK();
}
