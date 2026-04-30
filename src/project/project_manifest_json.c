#include "project/nmo_project_manifest_json.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
#include "runtime/nmo_context.h"
#include "type/nmo_type_query.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct manifest_parse_ctx {
    nmo_context_t *context;
    nmo_project_plan_t *plan;
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

static nmo_status_t manifest_parse_color(
    yyjson_val *material,
    float out_color[4])
{
    static const char *const allowed[] = {"color", NULL};
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(material, "material", allowed));

    yyjson_val *color = yyjson_obj_get(material, "color");
    if (!yyjson_is_arr(color) || yyjson_arr_size(color) != 4u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "manifest material.color must contain four numbers");
    }

    for (size_t i = 0u; i < 4u; ++i) {
        yyjson_val *item = yyjson_arr_get(color, i);
        if (!yyjson_is_num(item)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "manifest material.color values must be numbers");
        }
        out_color[i] = (float)yyjson_get_real(item);
    }
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_mesh(
    yyjson_val *mesh,
    nmo_primitive_mesh_t *out_primitive)
{
    static const char *const allowed[] = {"primitive", NULL};
    const char *primitive = NULL;
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(mesh, "mesh", allowed));
    NMO_RETURN_IF_ERROR(manifest_required_string(mesh, "primitive", &primitive));

    if (strcmp(primitive, "cube") != 0) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_SUPPORTED, NMO_SEVERITY_ERROR,
                         "unsupported manifest mesh primitive '%s'", primitive);
    }
    *out_primitive = NMO_PRIMITIVE_CUBE;
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
        static const char *const allowed[] = {"name", "debug_output", NULL};
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

static nmo_status_t manifest_parse_object(
    manifest_parse_ctx_t *ctx,
    yyjson_val *object,
    uint32_t scene_handle)
{
    static const char *const allowed[] = {
        "name", "class", "fields", "mesh", "material", "scripts", NULL};
    const char *name = NULL;
    const char *class_name = NULL;
    nmo_class_id_t class_id = 0;
    nmo_session_field_edit_t *fields = NULL;
    size_t field_count = 0u;
    uint32_t object_handle = 0u;

    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(object, "object", allowed));
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

    yyjson_val *mesh = yyjson_obj_get(object, "mesh");
    if (mesh) {
        nmo_primitive_mesh_t primitive = NMO_PRIMITIVE_CUBE;
        NMO_RETURN_IF_ERROR(manifest_parse_mesh(mesh, &primitive));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_primitive_mesh(
            ctx->plan,
            object_handle,
            primitive));
    }

    yyjson_val *material = yyjson_obj_get(object, "material");
    if (material) {
        float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        NMO_RETURN_IF_ERROR(manifest_parse_color(material, color));
        NMO_RETURN_IF_ERROR(nmo_project_plan_set_material_color(
            ctx->plan,
            object_handle,
            color[0],
            color[1],
            color[2],
            color[3]));
    }

    NMO_RETURN_IF_ERROR(manifest_parse_scripts(
        ctx,
        yyjson_obj_get(object, "scripts"),
        object_handle));
    NMO_RETURN_OK();
}

static nmo_status_t manifest_parse_scene(
    manifest_parse_ctx_t *ctx,
    yyjson_val *scene)
{
    static const char *const allowed[] = {"name", "objects", NULL};
    const char *name = NULL;
    uint32_t scene_handle = 0u;
    NMO_RETURN_IF_ERROR(manifest_reject_unknown_fields(scene, "scene", allowed));
    NMO_RETURN_IF_ERROR(manifest_required_string(scene, "name", &name));
    NMO_RETURN_IF_ERROR(nmo_project_plan_add_scene(ctx->plan, name, &scene_handle));

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
    yyjson_arr_foreach(objects, idx, max, object) {
        NMO_RETURN_IF_ERROR(manifest_parse_object(ctx, object, scene_handle));
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
        NMO_RETURN_IF_ERROR(manifest_parse_scene(ctx, scene));
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
