/**
 * @file virtools_loader.c
 * @brief Load Virtools data from JSON files into registries
 *
 * Uses yyjson for parsing.
 */

#include "app/nmo_virtools_loader.h"
#include "app/nmo_bb_registry.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"

#include "yyjson.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Helpers
 * ============================================================================ */

static nmo_guid_t read_guid(yyjson_val *arr) {
    if (!arr || !yyjson_is_arr(arr) || yyjson_arr_size(arr) != 2)
        return NMO_GUID_NULL;
    uint32_t d1 = (uint32_t)yyjson_get_uint(yyjson_arr_get(arr, 0));
    uint32_t d2 = (uint32_t)yyjson_get_uint(yyjson_arr_get(arr, 1));
    return nmo_guid_create(d1, d2);
}

static const char *get_str(yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    return (v && yyjson_is_str(v)) ? yyjson_get_str(v) : NULL;
}

static int get_int(yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    return (v && yyjson_is_int(v)) ? (int)yyjson_get_sint(v) : 0;
}

static uint32_t get_uint(yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    return (v && yyjson_is_num(v)) ? (uint32_t)yyjson_get_uint(v) : 0;
}

static nmo_guid_t get_guid(yyjson_val *obj, const char *key) {
    return read_guid(yyjson_obj_get(obj, key));
}

/* ============================================================================
 * Parameter types
 * ============================================================================ */

static uint16_t category_from_str(const char *cat) {
    if (!cat) return NMO_TYPE_CATEGORY_SCALAR;
    if (strcmp(cat, "enum") == 0) return NMO_TYPE_CATEGORY_ENUM;
    if (strcmp(cat, "flags") == 0) return NMO_TYPE_CATEGORY_FLAGS;
    if (strcmp(cat, "struct") == 0) return NMO_TYPE_CATEGORY_STRUCT;
    return NMO_TYPE_CATEGORY_SCALAR;
}

static void register_enum_metadata(nmo_type_registry_t *registry, nmo_type_id_t tid, yyjson_val *values) {
    if (!values || !yyjson_is_arr(values)) return;
    size_t count = yyjson_arr_size(values);
    if (count == 0) return;

    /* Allocate descriptors on the stack for small enums, heap for large */
    nmo_enum_descriptor_t *descs = (nmo_enum_descriptor_t *)calloc(count, sizeof(*descs));
    if (!descs) return;

    size_t idx = 0;
    yyjson_val *item;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(values, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        descs[idx].name = get_str(item, "name");
        descs[idx].value = get_int(item, "value");
        idx++;
    }

    nmo_specialized_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.type_id = tid;
    meta.metadata_type = NMO_METADATA_TYPE_ENUM;
    meta.enum_meta.values = descs;
    meta.enum_meta.value_count = count;
    nmo_type_registry_register_metadata(registry, &meta);
    free(descs);
}

static void register_flags_metadata(nmo_type_registry_t *registry, nmo_type_id_t tid, yyjson_val *values) {
    if (!values || !yyjson_is_arr(values)) return;
    size_t count = yyjson_arr_size(values);
    if (count == 0) return;

    nmo_flags_descriptor_t *descs = (nmo_flags_descriptor_t *)calloc(count, sizeof(*descs));
    if (!descs) return;

    size_t idx = 0;
    yyjson_val *item;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(values, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        descs[idx].name = get_str(item, "name");
        descs[idx].mask = (uint64_t)get_uint(item, "value");
        idx++;
    }

    nmo_specialized_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.type_id = tid;
    meta.metadata_type = NMO_METADATA_TYPE_FLAGS;
    meta.flags_meta.bits = descs;
    meta.flags_meta.bit_count = count;
    nmo_type_registry_register_metadata(registry, &meta);
    free(descs);
}

static void register_struct_metadata(nmo_type_registry_t *registry, nmo_type_id_t tid, yyjson_val *members) {
    if (!members || !yyjson_is_arr(members)) return;
    size_t count = yyjson_arr_size(members);
    if (count == 0) return;

    nmo_struct_descriptor_t *descs = (nmo_struct_descriptor_t *)calloc(count, sizeof(*descs));
    if (!descs) return;

    size_t idx = 0;
    yyjson_val *item;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(members, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        descs[idx].name = get_str(item, "name");
        descs[idx].type_guid = get_guid(item, "type_guid");
        idx++;
    }

    nmo_specialized_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.type_id = tid;
    meta.metadata_type = NMO_METADATA_TYPE_STRUCT;
    meta.struct_meta.fields = descs;
    meta.struct_meta.field_count = count;
    nmo_type_registry_register_metadata(registry, &meta);
    free(descs);
}

nmo_status_t nmo_virtools_load_param_types(nmo_type_registry_t *registry, const char *path) {
    if (!registry || !path)
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, &err);
    if (!doc)
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR, "JSON parse error: %s", err.msg);

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "expected JSON array");
    }

    size_t registered = 0;
    yyjson_val *item;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(root, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        nmo_guid_t guid = get_guid(item, "guid");
        if (nmo_guid_is_null(guid)) continue;

        /* Skip if already registered */
        if (nmo_type_registry_guid_to_type_id(registry, guid) != NMO_TYPE_ID_INVALID)
            continue;

        nmo_type_descriptor_t desc;
        memset(&desc, 0, sizeof(desc));
        desc.guid = guid;
        desc.name = get_str(item, "name");
        desc.size = get_uint(item, "size");
        desc.category = category_from_str(get_str(item, "category"));
        desc.class_id = get_uint(item, "class_id");
        desc.base_type = get_guid(item, "derived_from");
        desc.valid = true;

        if (nmo_type_registry_register(registry, &desc) == NMO_OK) {
            registered++;

            /* Register metadata for enum/flags/struct */
            nmo_type_id_t tid = nmo_type_registry_guid_to_type_id(registry, guid);
            if (tid != NMO_TYPE_ID_INVALID) {
                const char *cat = get_str(item, "category");
                if (cat && strcmp(cat, "enum") == 0)
                    register_enum_metadata(registry, tid, yyjson_obj_get(item, "values"));
                else if (cat && strcmp(cat, "flags") == 0)
                    register_flags_metadata(registry, tid, yyjson_obj_get(item, "values"));
                else if (cat && strcmp(cat, "struct") == 0)
                    register_struct_metadata(registry, tid, yyjson_obj_get(item, "members"));
            }
        }
    }

    yyjson_doc_free(doc);
    return NMO_OK;
}

/* ============================================================================
 * Operation types
 * ============================================================================ */

nmo_status_t nmo_virtools_load_operations(nmo_type_registry_t *registry, const char *path) {
    if (!registry || !path)
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, &err);
    if (!doc)
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR, "JSON parse error: %s", err.msg);

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "expected JSON array");
    }

    yyjson_val *item;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(root, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        nmo_guid_t guid = get_guid(item, "guid");
        if (nmo_guid_is_null(guid)) continue;

        nmo_type_descriptor_t desc;
        memset(&desc, 0, sizeof(desc));
        desc.guid = guid;
        desc.name = get_str(item, "name");
        desc.category = NMO_TYPE_CATEGORY_OPERATION;
        desc.valid = true;
        nmo_type_registry_register(registry, &desc);
    }

    yyjson_doc_free(doc);
    return NMO_OK;
}

/* ============================================================================
 * Building blocks
 * ============================================================================ */

static uint32_t read_json_string_array(yyjson_val *arr, const char ***out) {
    *out = NULL;
    if (!arr || !yyjson_is_arr(arr)) return 0;
    uint32_t n = (uint32_t)yyjson_arr_size(arr);
    if (n == 0) return 0;
    const char **ptrs = (const char **)calloc(n, sizeof(const char *));
    if (!ptrs) return 0;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(arr, &iter);
    for (uint32_t i = 0; i < n; i++) {
        yyjson_val *v = yyjson_arr_iter_next(&iter);
        ptrs[i] = (v && yyjson_is_str(v)) ? yyjson_get_str(v) : "";
    }
    *out = ptrs;
    return n;
}

static uint32_t read_json_param_array(yyjson_val *arr, nmo_bb_param_desc_t **out) {
    *out = NULL;
    if (!arr || !yyjson_is_arr(arr)) return 0;
    uint32_t n = (uint32_t)yyjson_arr_size(arr);
    if (n == 0) return 0;
    nmo_bb_param_desc_t *descs = (nmo_bb_param_desc_t *)calloc(n, sizeof(*descs));
    if (!descs) return 0;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(arr, &iter);
    for (uint32_t i = 0; i < n; i++) {
        yyjson_val *p = yyjson_arr_iter_next(&iter);
        descs[i].name = get_str(p, "name");
        descs[i].type_guid = get_guid(p, "type_guid");
    }
    *out = descs;
    return n;
}

nmo_status_t nmo_virtools_load_building_blocks(nmo_bb_registry_t *bb_registry, const char *path) {
    if (!bb_registry || !path)
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, &err);
    if (!doc)
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR, "JSON parse error: %s at %zu", err.msg, err.pos);

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "expected JSON array");
    }

    yyjson_val *item;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(root, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        nmo_guid_t guid = get_guid(item, "guid");
        if (nmo_guid_is_null(guid)) continue;

        /* Read IO names */
        const char **inputs = NULL, **outputs = NULL;
        uint32_t in_count = read_json_string_array(yyjson_obj_get(item, "inputs"), &inputs);
        uint32_t out_count = read_json_string_array(yyjson_obj_get(item, "outputs"), &outputs);

        /* Read parameter descs */
        nmo_bb_param_desc_t *ip = NULL, *op = NULL, *lp = NULL, *st = NULL;
        uint32_t ip_n = read_json_param_array(yyjson_obj_get(item, "input_params"), &ip);
        uint32_t op_n = read_json_param_array(yyjson_obj_get(item, "output_params"), &op);
        uint32_t lp_n = read_json_param_array(yyjson_obj_get(item, "local_params"), &lp);
        uint32_t st_n = read_json_param_array(yyjson_obj_get(item, "settings"), &st);

        nmo_bb_proto_t proto;
        memset(&proto, 0, sizeof(proto));
        proto.guid = guid;
        proto.name = get_str(item, "name");
        proto.description = get_str(item, "description");
        proto.category = get_str(item, "category");
        proto.dll = get_str(item, "dll");
        proto.version = get_uint(item, "version");
        proto.compatible_class_id = (int32_t)get_int(item, "compatible_class_id");
        proto.behavior_flags = get_uint(item, "behavior_flags");
        proto.inputs = inputs;
        proto.input_count = in_count;
        proto.outputs = outputs;
        proto.output_count = out_count;
        proto.input_params = ip;
        proto.input_param_count = ip_n;
        proto.output_params = op;
        proto.output_param_count = op_n;
        proto.local_params = lp;
        proto.local_param_count = lp_n;
        proto.settings = st;
        proto.setting_count = st_n;

        nmo_bb_registry_add(bb_registry, &proto); /* deep-copies all data */

        /* Free temp arrays (strings point into yyjson doc, still valid) */
        free(inputs);
        free(outputs);
        free(ip);
        free(op);
        free(lp);
        free(st);
    }

    yyjson_doc_free(doc);
    return NMO_OK;
}

/* ============================================================================
 * Load all from directory
 * ============================================================================ */

nmo_status_t nmo_virtools_load_data_dir(
    nmo_type_registry_t *registry,
    nmo_bb_registry_t *bb_registry,
    const char *data_dir)
{
    if (!data_dir)
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null data_dir");

    size_t dir_len = strlen(data_dir);
    size_t path_cap = dir_len + 64; /* enough for any filename */
    char *path = (char *)malloc(path_cap);
    if (!path)
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "path alloc failed");
    int loaded = 0;

    if (registry) {
        snprintf(path, path_cap, "%s/virtools_parameter_types.json", data_dir);
        if (nmo_virtools_load_param_types(registry, path) == NMO_OK)
            loaded++;

        snprintf(path, path_cap, "%s/virtools_operation_types.json", data_dir);
        if (nmo_virtools_load_operations(registry, path) == NMO_OK)
            loaded++;
    }

    if (bb_registry) {
        snprintf(path, path_cap, "%s/virtools_building_blocks.json", data_dir);
        if (nmo_virtools_load_building_blocks(bb_registry, path) == NMO_OK)
            loaded++;

        /* Also try extended BBs */
        snprintf(path, path_cap, "%s/virtools_building_blocks_ext.json", data_dir);
        nmo_virtools_load_building_blocks(bb_registry, path); /* optional */
    }

    free(path);

    if (loaded == 0)
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_WARNING, "no Virtools data files found in %s", data_dir);

    return NMO_OK;
}
