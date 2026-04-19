/**
 * @file virtools_loader.c
 * @brief Load Virtools data from JSON files into registries
 *
 * Uses yyjson for parsing.
 */

#include "extension/nmo_virtools_loader.h"
#include "behavior/nmo_bb_registry.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_operation_system.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_abi.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_utils.h"

#include "yyjson.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const yyjson_read_flag NMO_VIRTOOLS_JSON_READ_FLAGS =
    YYJSON_READ_ALLOW_BOM;

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

static bool is_unbased_uint32_primitive_guid(nmo_guid_t guid) {
    return nmo_guid_equals(guid, CKPGUID_COPYDEPENDENCIES) ||
           nmo_guid_equals(guid, CKPGUID_DELETEDEPENDENCIES) ||
           nmo_guid_equals(guid, CKPGUID_REPLACEDEPENDENCIES) ||
           nmo_guid_equals(guid, CKPGUID_SAVEDEPENDENCIES) ||
           nmo_guid_equals(guid, CKPGUID_MESSAGE) ||
           nmo_guid_equals(guid, CKPGUID_ATTRIBUTE) ||
           nmo_guid_equals(guid, CKPGUID_OBJECTARRAY) ||
           nmo_guid_equals(guid, CKPGUID_2DCURVE);
}

/* ============================================================================
 * Parameter types
 * ============================================================================ */

static uint16_t category_from_str(const char *cat) {
    if (!cat) return NMO_TYPE_CATEGORY_SCALAR;
    if (strcmp(cat, "enum") == 0) return NMO_TYPE_CATEGORY_ENUM;
    if (strcmp(cat, "flags") == 0) return NMO_TYPE_CATEGORY_FLAGS;
    if (strcmp(cat, "struct") == 0) return NMO_TYPE_CATEGORY_STRUCT;
    if (strcmp(cat, "object_ref") == 0) return NMO_TYPE_CATEGORY_OBJECT_REF;
    return NMO_TYPE_CATEGORY_SCALAR;
}

static nmo_status_t register_enum_metadata(nmo_type_registry_t *registry, nmo_type_id_t tid, yyjson_val *values) {
    if (!values || !yyjson_is_arr(values)) NMO_RETURN_OK();
    size_t count = yyjson_arr_size(values);
    if (count == 0) NMO_RETURN_OK();

    /* Allocate descriptors on the stack for small enums, heap for large */
    nmo_enum_descriptor_t *descs = (nmo_enum_descriptor_t *)calloc(count, sizeof(*descs));
    if (!descs) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate enum metadata descriptors");
    }

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
    nmo_status_t status = nmo_type_registry_register_metadata(registry, &meta);
    free(descs);
    return status;
}

static nmo_status_t register_flags_metadata(nmo_type_registry_t *registry, nmo_type_id_t tid, yyjson_val *values) {
    if (!values || !yyjson_is_arr(values)) NMO_RETURN_OK();
    size_t count = yyjson_arr_size(values);
    if (count == 0) NMO_RETURN_OK();

    nmo_flags_descriptor_t *descs = (nmo_flags_descriptor_t *)calloc(count, sizeof(*descs));
    if (!descs) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate flags metadata descriptors");
    }

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
    nmo_status_t status = nmo_type_registry_register_metadata(registry, &meta);
    free(descs);
    return status;
}

static nmo_status_t register_struct_metadata(nmo_type_registry_t *registry, nmo_type_id_t tid, yyjson_val *members) {
    if (!members || !yyjson_is_arr(members)) NMO_RETURN_OK();
    size_t count = yyjson_arr_size(members);
    if (count == 0) NMO_RETURN_OK();

    nmo_struct_descriptor_t *descs = (nmo_struct_descriptor_t *)calloc(count, sizeof(*descs));
    if (!descs) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate struct metadata descriptors");
    }

    size_t idx = 0;
    uint32_t offset = 0;
    yyjson_val *item;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(members, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        descs[idx].name = get_str(item, "name");
        descs[idx].type_guid = get_guid(item, "type_guid");
        const nmo_type_descriptor_t *field_type =
            nmo_type_registry_find_by_guid(registry, descs[idx].type_guid);
        if (field_type) {
            uint32_t align = field_type->alignment > 0 ? field_type->alignment : 1u;
            offset = (uint32_t)nmo_align((size_t)offset, (size_t)align);
            descs[idx].offset = offset;
            descs[idx].size = field_type->size;
            offset += field_type->size;
        }
        idx++;
    }

    nmo_specialized_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.type_id = tid;
    meta.metadata_type = NMO_METADATA_TYPE_STRUCT;
    meta.struct_meta.fields = descs;
    meta.struct_meta.field_count = count;
    nmo_status_t status = nmo_type_registry_register_metadata(registry, &meta);
    free(descs);
    return status;
}

nmo_status_t nmo_virtools_load_param_types(nmo_type_registry_t *registry, const char *path) {
    if (!registry || !path)
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, NMO_VIRTOOLS_JSON_READ_FLAGS, NULL, &err);
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
        char generated_name[64];
        if (!desc.name || desc.name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name),
                     "Virtools Type 0x%08X-0x%08X", guid.d1, guid.d2);
            desc.name = generated_name;
        }
        desc.size = get_uint(item, "size");
        desc.category = category_from_str(get_str(item, "category"));
        desc.class_id = get_uint(item, "class_id");
        desc.base_type = get_guid(item, "derived_from");
        desc.valid = true;

        if (nmo_guid_is_null(desc.base_type) &&
            desc.category == NMO_TYPE_CATEGORY_SCALAR &&
            desc.size == sizeof(uint32_t) &&
            is_unbased_uint32_primitive_guid(desc.guid)) {
            desc.base_type = CKPGUID_UINT32;
        }

        if ((desc.category == NMO_TYPE_CATEGORY_OBJECT_REF) ||
            (desc.category == NMO_TYPE_CATEGORY_SCALAR &&
             desc.class_id != 0 &&
             desc.size == sizeof(nmo_object_id_t))) {
            desc.category = NMO_TYPE_CATEGORY_OBJECT_REF;
            desc.class_id = 0;
        }

        nmo_status_t register_status = nmo_type_registry_register(registry, &desc);
        if (register_status == NMO_ERR_ALREADY_EXISTS) {
            continue;
        }
        if (register_status != NMO_OK) {
            yyjson_doc_free(doc);
            return register_status;
        }

        registered++;

        /* Register metadata for enum/flags/struct */
        nmo_type_id_t tid = nmo_type_registry_guid_to_type_id(registry, guid);
        if (tid != NMO_TYPE_ID_INVALID) {
            const char *cat = get_str(item, "category");
            nmo_status_t metadata_status = NMO_OK;
            if (cat && strcmp(cat, "enum") == 0) {
                metadata_status = register_enum_metadata(
                    registry, tid, yyjson_obj_get(item, "values"));
            } else if (cat && strcmp(cat, "flags") == 0) {
                metadata_status = register_flags_metadata(
                    registry, tid, yyjson_obj_get(item, "values"));
            } else if (cat && strcmp(cat, "struct") == 0) {
                metadata_status = register_struct_metadata(
                    registry, tid, yyjson_obj_get(item, "members"));
            }
            if (metadata_status == NMO_ERR_ALREADY_EXISTS) {
                continue;
            }
            if (metadata_status != NMO_OK) {
                yyjson_doc_free(doc);
                return metadata_status;
            }
        }
    }

    yyjson_doc_free(doc);
    return NMO_OK;
}

/* ============================================================================
 * Operation types
 * ============================================================================ */

nmo_status_t nmo_virtools_load_operations(
    nmo_type_registry_t *type_registry,
    nmo_operation_registry_t *op_registry,
    const char *path)
{
    if (!type_registry || !path)
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, NMO_VIRTOOLS_JSON_READ_FLAGS, NULL, &err);
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

        const char *name = get_str(item, "name");

        /* Register GUID→name in type_registry */
        nmo_type_descriptor_t desc;
        memset(&desc, 0, sizeof(desc));
        desc.guid = guid;
        desc.name = name;
        desc.category = NMO_TYPE_CATEGORY_OPERATION;
        desc.valid = true;
        nmo_type_registry_register(type_registry, &desc);

        /* Register signatures in operation_registry (function=NULL) */
        if (!op_registry) continue;
        yyjson_val *sigs = yyjson_obj_get(item, "signatures");
        if (!yyjson_is_arr(sigs)) continue;

        yyjson_val *sig;
        yyjson_arr_iter sig_iter;
        yyjson_arr_iter_init(sigs, &sig_iter);
        while ((sig = yyjson_arr_iter_next(&sig_iter)) != NULL) {
            nmo_guid_t p1 = get_guid(sig, "p1_guid");
            nmo_guid_t p2 = get_guid(sig, "p2_guid");
            nmo_guid_t result = get_guid(sig, "result_guid");
            if (nmo_guid_is_null(p1) || nmo_guid_is_null(result)) continue;

            nmo_operation_desc_t op;
            memset(&op, 0, sizeof(op));
            op.operation_guid = guid;
            op.p1_type_guid = p1;
            op.p2_type_guid = p2;
            op.result_type_guid = result;
            op.function = NULL; /* signature-only, no implementation */
            op.name = name;
            /* Detect unary: p2 is CKPGUID_NONE {0x1CA1B823, 0x41A80E45} */
            if (p2.d1 == 0x1CA1B823 && p2.d2 == 0x41A80E45)
                op.flags = NMO_OP_UNARY;
            nmo_operation_registry_register(op_registry, &op, type_registry);
        }
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
    yyjson_doc *doc = yyjson_read_file(path, NMO_VIRTOOLS_JSON_READ_FLAGS, NULL, &err);
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
 * Plugin metadata
 * ============================================================================ */

nmo_status_t nmo_virtools_load_plugins(nmo_extension_registry_t *ext_registry, const char *path) {
    if (!ext_registry || !path)
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, NMO_VIRTOOLS_JSON_READ_FLAGS, NULL, &err);
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

        if (nmo_extension_registry_find(ext_registry, guid) != NULL) {
            continue;
        }

        const char *name = get_str(item, "name");
        if (name == NULL || name[0] == '\0') {
            name = get_str(item, "description");
        }
        if (name == NULL || name[0] == '\0') {
            name = get_str(item, "dll");
        }

        nmo_extension_plugin_t plugin;
        memset(&plugin, 0, sizeof(plugin));
        plugin.abi_version = NMO_EXTENSION_ABI_VERSION;
        plugin.struct_size = sizeof(plugin);
        plugin.guid = guid;
        plugin.version = get_uint(item, "version");
        plugin.category = (nmo_plugin_category_t)get_uint(item, "category");
        plugin.name = name;
        plugin.init = NULL;
        plugin.shutdown = NULL;

        nmo_status_t status =
            nmo_extension_registry_register_static(ext_registry, &plugin, 1);
        if (status != NMO_OK) {
            yyjson_doc_free(doc);
            return status;
        }
    }

    yyjson_doc_free(doc);
    return NMO_OK;
}

/* ============================================================================
 * Load all from directory
 * ============================================================================ */

nmo_status_t nmo_virtools_load_data_dir(
    nmo_type_registry_t *type_registry,
    nmo_operation_registry_t *op_registry,
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

    if (type_registry) {
        snprintf(path, path_cap, "%s/virtools_parameter_types.json", data_dir);
        if (nmo_virtools_load_param_types(type_registry, path) == NMO_OK)
            loaded++;

        snprintf(path, path_cap, "%s/virtools_operation_types.json", data_dir);
        if (nmo_virtools_load_operations(type_registry, op_registry, path) == NMO_OK)
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
