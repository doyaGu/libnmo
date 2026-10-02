#include "edit/nmo_edit_plan_json.h"

#include "core/nmo_guid.h"
#include "object/nmo_manager_guids.h"
#include "yyjson.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_str_safe(yyjson_mut_doc *doc,
                         yyjson_mut_val *obj,
                         const char *key,
                         const char *value)
{
    if (value != NULL) {
        yyjson_mut_obj_add_strcpy(doc, obj, key, value);
    }
}

static void add_guid_json(yyjson_mut_doc *doc,
                          yyjson_mut_val *obj,
                          const char *key,
                          nmo_guid_t guid)
{
    char guid_text[32];
    if (nmo_guid_format(guid, guid_text, sizeof(guid_text)) > 0) {
        yyjson_mut_obj_add_strcpy(doc, obj, key, guid_text);
    }
}

static void add_optional_id_json(yyjson_mut_doc *doc,
                                 yyjson_mut_val *obj,
                                 const char *key,
                                 nmo_object_id_t id)
{
    if (id != 0u) {
        yyjson_mut_obj_add_uint(doc, obj, key, (uint64_t)id);
    }
}

static const char *manager_entry_policy_string(
    nmo_manager_entry_policy_t policy)
{
    return policy == NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING
               ? "create_missing"
               : "require_existing";
}

static const char *manager_entry_schema_string(
    nmo_manager_entry_schema_t schema)
{
    switch (schema) {
        case NMO_MANAGER_ENTRY_SCHEMA_MESSAGE:
            return "message";
        case NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE:
            return "attribute";
        case NMO_MANAGER_ENTRY_SCHEMA_AUTO:
        default:
            return "auto";
    }
}

static bool parse_manager_entry_policy_value(
    yyjson_val *policy_val,
    nmo_manager_entry_policy_t *out_policy)
{
    if (policy_val == NULL || out_policy == NULL || !yyjson_is_str(policy_val)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid manager_entry.policy");
        return false;
    }
    const char *policy_text = yyjson_get_str(policy_val);
    if (strcmp(policy_text, "require_existing") == 0) {
        *out_policy = NMO_MANAGER_ENTRY_POLICY_REQUIRE_EXISTING;
        return true;
    }
    if (strcmp(policy_text, "create_missing") == 0) {
        *out_policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING;
        return true;
    }
    nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                        __FILE__, __LINE__, "Invalid manager_entry.policy");
    return false;
}

static bool parse_manager_entry_schema_value(
    yyjson_val *schema_val,
    nmo_manager_entry_schema_t *out_schema)
{
    if (schema_val == NULL || out_schema == NULL || !yyjson_is_str(schema_val)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__, "Invalid manager_entry.schema");
        return false;
    }
    const char *schema_text = yyjson_get_str(schema_val);
    if (strcmp(schema_text, "auto") == 0) {
        *out_schema = NMO_MANAGER_ENTRY_SCHEMA_AUTO;
        return true;
    }
    if (strcmp(schema_text, "message") == 0) {
        *out_schema = NMO_MANAGER_ENTRY_SCHEMA_MESSAGE;
        return true;
    }
    if (strcmp(schema_text, "attribute") == 0) {
        *out_schema = NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE;
        return true;
    }
    nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                        __FILE__, __LINE__, "Invalid manager_entry.schema");
    return false;
}

static bool manager_entry_json_key_allowed(const char *key,
                                           const char *const *allowed)
{
    if (key == NULL || allowed == NULL) {
        return false;
    }
    for (size_t i = 0; allowed[i] != NULL; ++i) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool reject_unknown_manager_entry_fields(yyjson_val *obj,
                                                const char *where,
                                                const char *const *allowed)
{
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *key = NULL;
    yyjson_val *val = NULL;
    yyjson_obj_foreach(obj, idx, max, key, val) {
        (void)val;
        const char *name = yyjson_get_str(key);
        if (!manager_entry_json_key_allowed(name, allowed)) {
            nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                __FILE__, __LINE__,
                                "Unknown field '%s' in %s",
                                name != NULL ? name : "(null)", where);
            return false;
        }
    }
    return true;
}

static bool parse_manager_entry_create_options_value(
    yyjson_val *create_val,
    nmo_manager_entry_create_options_t *out_create)
{
    static const char *allowed[] = {
        "attribute_type_guid", "category", "compatible_class_id", "flags",
        NULL
    };
    if (create_val == NULL || out_create == NULL || !yyjson_is_obj(create_val)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__, "Invalid manager_entry.create");
        return false;
    }
    if (!reject_unknown_manager_entry_fields(
            create_val, "manager_entry.create", allowed)) {
        return false;
    }
    out_create->enabled = true;

    yyjson_val *type_val = yyjson_obj_get(create_val, "attribute_type_guid");
    if (type_val != NULL) {
        if (!yyjson_is_str(type_val)) {
            nmo_last_error_setf(
                NMO_ERR_INVALID_FORMAT,
                NMO_SEVERITY_ERROR,
                __FILE__,
                __LINE__,
                "Invalid manager_entry.create.attribute_type_guid");
            return false;
        }
        out_create->attribute_type_guid =
            nmo_guid_parse(yyjson_get_str(type_val));
        if (nmo_guid_is_null(out_create->attribute_type_guid)) {
            nmo_last_error_setf(
                NMO_ERR_INVALID_FORMAT,
                NMO_SEVERITY_ERROR,
                __FILE__,
                __LINE__,
                "Invalid manager_entry.create.attribute_type_guid");
            return false;
        }
    }

    yyjson_val *category_val = yyjson_obj_get(create_val, "category");
    if (category_val != NULL) {
        if (!yyjson_is_str(category_val)) {
            nmo_last_error_setf(
                NMO_ERR_INVALID_FORMAT,
                NMO_SEVERITY_ERROR,
                __FILE__,
                __LINE__,
                "Invalid manager_entry.create.category");
            return false;
        }
        out_create->category = yyjson_get_str(category_val);
    }

    yyjson_val *class_val = yyjson_obj_get(create_val, "compatible_class_id");
    if (class_val != NULL) {
        if (!yyjson_is_uint(class_val)) {
            nmo_last_error_setf(
                NMO_ERR_INVALID_FORMAT,
                NMO_SEVERITY_ERROR,
                __FILE__,
                __LINE__,
                "Invalid manager_entry.create.compatible_class_id");
            return false;
        }
        out_create->has_compatible_class_id = true;
        out_create->compatible_class_id = (uint32_t)yyjson_get_uint(class_val);
    }

    yyjson_val *flags_val = yyjson_obj_get(create_val, "flags");
    if (flags_val != NULL) {
        if (!yyjson_is_uint(flags_val)) {
            nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                __FILE__, __LINE__,
                                "Invalid manager_entry.create.flags");
            return false;
        }
        out_create->has_flags = true;
        out_create->flags = (uint32_t)yyjson_get_uint(flags_val);
    }
    return true;
}

static bool parse_manager_entry_options_value(
    yyjson_val *entry_val,
    nmo_manager_entry_options_t *out_options)
{
    if (entry_val == NULL || out_options == NULL || !yyjson_is_obj(entry_val)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__, "Invalid manager_entry");
        return false;
    }

    *out_options = nmo_manager_entry_options_default();
    static const char *allowed[] = {
        "policy", "schema", "manager_guid", "key", "create", NULL
    };
    if (!reject_unknown_manager_entry_fields(
            entry_val, "manager_entry", allowed)) {
        return false;
    }
    yyjson_val *policy_val = yyjson_obj_get(entry_val, "policy");
    if (policy_val != NULL &&
        !parse_manager_entry_policy_value(policy_val, &out_options->policy)) {
        return false;
    }
    yyjson_val *schema_val = yyjson_obj_get(entry_val, "schema");
    if (schema_val != NULL &&
        !parse_manager_entry_schema_value(schema_val, &out_options->schema)) {
        return false;
    }
    yyjson_val *guid_val = yyjson_obj_get(entry_val, "manager_guid");
    if (guid_val != NULL) {
        if (!yyjson_is_str(guid_val)) {
            nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                __FILE__, __LINE__,
                                "Invalid manager_entry.manager_guid");
            return false;
        }
        out_options->manager_guid = nmo_guid_parse(yyjson_get_str(guid_val));
        if (nmo_guid_is_null(out_options->manager_guid)) {
            nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                __FILE__, __LINE__,
                                "Invalid manager_entry.manager_guid");
            return false;
        }
    } else if (out_options->schema == NMO_MANAGER_ENTRY_SCHEMA_MESSAGE) {
        out_options->manager_guid = NMO_MANAGER_GUID_MESSAGE;
    } else if (out_options->schema == NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE) {
        out_options->manager_guid = NMO_MANAGER_GUID_ATTRIBUTE;
    }
    yyjson_val *key_val = yyjson_obj_get(entry_val, "key");
    if (key_val != NULL) {
        if (!yyjson_is_str(key_val)) {
            nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                __FILE__, __LINE__, "Invalid manager_entry.key");
            return false;
        }
        out_options->key = yyjson_get_str(key_val);
    }
    yyjson_val *create_val = yyjson_obj_get(entry_val, "create");
    if (create_val != NULL &&
        !parse_manager_entry_create_options_value(create_val,
                                                 &out_options->create)) {
        return false;
    }
    return true;
}

static void add_manager_entry_json(yyjson_mut_doc *doc,
                                   yyjson_mut_val *obj,
                                   const nmo_manager_entry_options_t *options)
{
    if (doc == NULL || obj == NULL || options == NULL) {
        return;
    }
    yyjson_mut_val *entry = yyjson_mut_obj(doc);
    if (entry == NULL) {
        return;
    }
    yyjson_mut_obj_add_str(doc, entry, "policy",
                           manager_entry_policy_string(options->policy));
    yyjson_mut_obj_add_str(doc, entry, "schema",
                           manager_entry_schema_string(options->schema));
    nmo_guid_t manager_guid = options->manager_guid;
    if (nmo_guid_is_null(manager_guid) &&
        options->schema == NMO_MANAGER_ENTRY_SCHEMA_MESSAGE) {
        manager_guid = NMO_MANAGER_GUID_MESSAGE;
    } else if (nmo_guid_is_null(manager_guid) &&
               options->schema == NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE) {
        manager_guid = NMO_MANAGER_GUID_ATTRIBUTE;
    }
    if (!nmo_guid_is_null(manager_guid)) {
        add_guid_json(doc, entry, "manager_guid", manager_guid);
    }
    add_str_safe(doc, entry, "key", options->key);
    if (options->create.enabled) {
        yyjson_mut_val *create = yyjson_mut_obj(doc);
        if (create != NULL) {
            if (!nmo_guid_is_null(options->create.attribute_type_guid)) {
                add_guid_json(doc, create, "attribute_type_guid",
                              options->create.attribute_type_guid);
            }
            add_str_safe(doc, create, "category", options->create.category);
            if (options->create.has_compatible_class_id) {
                yyjson_mut_obj_add_uint(
                    doc, create, "compatible_class_id",
                    (uint64_t)options->create.compatible_class_id);
            }
            if (options->create.has_flags) {
                yyjson_mut_obj_add_uint(
                    doc, create, "flags", (uint64_t)options->create.flags);
            }
            yyjson_mut_obj_add_val(doc, entry, "create", create);
        }
    }
    yyjson_mut_obj_add_val(doc, obj, "manager_entry", entry);
}

static void add_ref_json(yyjson_mut_doc *doc,
                         yyjson_mut_val *obj,
                         const char *operation_key,
                         const char *handle_key,
                         size_t operation_index,
                         const char *handle_name)
{
    yyjson_mut_obj_add_uint(doc, obj, operation_key,
                            (uint64_t)(operation_index + 1u));
    add_str_safe(doc, obj, handle_key, handle_name);
}

static yyjson_mut_val *probe_candidate_to_json(
    yyjson_mut_doc *doc,
    const nmo_probe_selector_candidate_t *candidate)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || candidate == NULL) {
        return obj;
    }
    add_optional_id_json(doc, obj, "node_id", candidate->node_id);
    add_optional_id_json(doc, obj, "parent_id", candidate->parent_id);
    add_optional_id_json(doc, obj, "boundary_behavior_id",
                         candidate->boundary_behavior_id);
    add_optional_id_json(doc, obj, "link_id", candidate->link_id);
    add_optional_id_json(doc, obj, "operation_id", candidate->operation_id);
    add_optional_id_json(doc, obj, "from_io_id", candidate->from_io_id);
    add_optional_id_json(doc, obj, "to_io_id", candidate->to_io_id);
    if (candidate->has_delay) {
        yyjson_mut_obj_add_uint(doc, obj, "delay",
                                (uint64_t)candidate->delay);
    }
    add_optional_id_json(doc, obj, "source_parameter_id",
                         candidate->source_parameter_id);
    add_optional_id_json(doc, obj, "value_parameter_id",
                         candidate->value_parameter_id);
    add_optional_id_json(doc, obj, "dataarray_id", candidate->dataarray_id);
    if (!nmo_guid_is_null(candidate->column_type_guid)) {
        add_guid_json(doc, obj, "column_type_guid",
                      candidate->column_type_guid);
    }
    if (candidate->confidence != 0.0) {
        yyjson_mut_obj_add_real(doc, obj, "confidence",
                                candidate->confidence);
    }
    if (!nmo_guid_is_null(candidate->bb_guid)) {
        add_guid_json(doc, obj, "bb_guid", candidate->bb_guid);
    }
    add_str_safe(doc, obj, "proto_name", candidate->proto_name);
    const char *role_name = nmo_probe_candidate_role_name(candidate->role);
    if (role_name[0] != '\0') {
        yyjson_mut_obj_add_str(doc, obj, "role", role_name);
    }
    add_str_safe(doc, obj, "rejection_code", candidate->rejection_code);
    return obj;
}

static yyjson_mut_val *probe_safe_insertion_to_json(
    yyjson_mut_doc *doc,
    const nmo_probe_safe_insertion_t *safe)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || safe == NULL) {
        return obj;
    }
    yyjson_mut_obj_add_bool(doc, obj, "selected", safe->selected);
    add_optional_id_json(doc, obj, "selected_node_id",
                         safe->selected_node_id);
    add_optional_id_json(doc, obj, "selected_link_id",
                         safe->selected_link_id);
    add_optional_id_json(doc, obj, "selected_operation_id",
                         safe->selected_operation_id);
    add_optional_id_json(doc, obj, "remove_link_id", safe->remove_link_id);
    add_optional_id_json(doc, obj, "insert_from_io_id",
                         safe->insert_from_io_id);
    add_optional_id_json(doc, obj, "insert_to_io_id",
                         safe->insert_to_io_id);
    if (safe->has_preserved_delay) {
        yyjson_mut_obj_add_uint(doc, obj, "preserved_delay",
                                (uint64_t)safe->preserved_delay);
    }
    return obj;
}

static yyjson_mut_val *probe_analysis_to_json(
    yyjson_mut_doc *doc,
    const nmo_probe_selector_result_t *analysis)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || analysis == NULL) {
        return obj;
    }
    yyjson_mut_obj_add_str(doc, obj, "mode",
                           nmo_probe_selector_mode_name(analysis->mode));
    yyjson_mut_obj_add_str(doc, obj, "status",
                           nmo_probe_selector_status_name(analysis->status));
    add_str_safe(doc, obj, "rejection_code", analysis->rejection_code);
    add_str_safe(doc, obj, "message", analysis->message);
    add_optional_id_json(doc, obj, "selected_node_id",
                         analysis->selected_node_id);
    add_optional_id_json(doc, obj, "selected_link_id",
                         analysis->selected_link_id);
    add_optional_id_json(doc, obj, "selected_operation_id",
                         analysis->selected_operation_id);
    add_optional_id_json(doc, obj, "from_io_id", analysis->from_io_id);
    add_optional_id_json(doc, obj, "to_io_id", analysis->to_io_id);
    if (analysis->has_delay) {
        yyjson_mut_obj_add_uint(doc, obj, "delay",
                                (uint64_t)analysis->delay);
    }
    yyjson_mut_obj_add_val(
        doc, obj, "safe_insertion",
        probe_safe_insertion_to_json(doc, &analysis->safe_insertion));
    yyjson_mut_val *candidates = yyjson_mut_arr(doc);
    if (candidates != NULL) {
        for (size_t i = 0; i < analysis->candidate_count; ++i) {
            yyjson_mut_arr_add_val(
                candidates,
                probe_candidate_to_json(doc, &analysis->candidates[i]));
        }
        yyjson_mut_obj_add_val(doc, obj, "candidates", candidates);
    }
    return obj;
}

static char *bytes_to_hex(const uint8_t *bytes, size_t byte_count)
{
    static const char hex[] = "0123456789ABCDEF";
    if (byte_count == 0u) {
        char *empty = (char *)malloc(1u);
        if (empty != NULL) {
            empty[0] = '\0';
        }
        return empty;
    }
    if (bytes == NULL || byte_count > (SIZE_MAX - 1u) / 2u) {
        return NULL;
    }
    char *out = (char *)malloc(byte_count * 2u + 1u);
    if (out == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < byte_count; ++i) {
        out[i * 2u] = hex[(bytes[i] >> 4) & 0x0Fu];
        out[i * 2u + 1u] = hex[bytes[i] & 0x0Fu];
    }
    out[byte_count * 2u] = '\0';
    return out;
}

static yyjson_mut_val *fold_maps_to_json(
    yyjson_mut_doc *doc,
    const nmo_behavior_fold_map_t *maps,
    size_t count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (arr == NULL) {
        return NULL;
    }
    for (size_t i = 0; maps != NULL && i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        if (item == NULL) {
            return arr;
        }
        yyjson_mut_obj_add_uint(doc, item, "old_index",
                                (uint64_t)maps[i].old_index);
        yyjson_mut_obj_add_uint(doc, item, "new_index",
                                (uint64_t)maps[i].new_index);
        add_optional_id_json(doc, item, "old_id", maps[i].old_id);
        add_optional_id_json(doc, item, "new_id", maps[i].new_id);
        add_str_safe(doc, item, "label", maps[i].label);
        yyjson_mut_arr_add_val(arr, item);
    }
    return arr;
}

typedef enum edit_op_json_field_type {
    EDIT_OP_JSON_ID,        /**< Required non-zero uint32 */
    EDIT_OP_JSON_U32,       /**< Required uint32, zero allowed */
    EDIT_OP_JSON_OPT_U32,   /**< Optional uint32, defaults to zero */
    EDIT_OP_JSON_OPT_BOOL,  /**< Optional bool, defaults to false */
    EDIT_OP_JSON_STRING,    /**< Required non-empty string */
    EDIT_OP_JSON_OPT_STRING, /**< Optional non-empty string, NULL when absent */
    EDIT_OP_JSON_GUID,      /**< Required non-null GUID string */
    EDIT_OP_JSON_ENUM,      /**< Required enum name */
    EDIT_OP_JSON_OPT_ENUM,  /**< Optional enum name, zero when absent */
    EDIT_OP_JSON_REF,       /**< A non-zero id, or an earlier operation and the name of its handle */
    EDIT_OP_JSON_OPT_REF,   /**< Like REF, but may be absent; the id may be zero */
    EDIT_OP_JSON_HEX,       /**< Required hex string, stored as bytes and a count */
    EDIT_OP_JSON_ID_ARRAY,  /**< Required non-empty array of non-zero ids and a count */
    EDIT_OP_JSON_FOLD_MAPS, /**< Optional array of fold maps and a count */
    EDIT_OP_JSON_MANAGER_ENTRY, /**< Optional manager entry options object */
} edit_op_json_field_type_t;

/** Enum name table entry; the first entry names unknown values on write. */
typedef struct edit_op_json_enum_name {
    const char *name;
    int value;
} edit_op_json_enum_name_t;

typedef struct edit_op_json_field {
    const char *key;            /**< The id key of a REF */
    edit_op_json_field_type_t type;
    size_t offset;              /**< In nmo_edit_op_t */
    const edit_op_json_enum_name_t *names;
    const char *operation_key;  /**< REF: key of the earlier operation (1-based) */
    const char *handle_key;     /**< REF: key of the handle name */
    size_t aux_offset;          /**< REF: the nmo_edit_handle_ref_t; HEX, ID_ARRAY, FOLD_MAPS: the count */
    int map_kind;               /**< FOLD_MAPS: nmo_behavior_fold_map_kind_t */
    /**
     * Non-zero for a field of an optional group: reading the field sets the
     * group when one of its keys is present, and the field is written only
     * when the group is set. The group is a bool, or with presence_mask a bit
     * of a uint32.
     */
    size_t presence_offset;
    uint32_t presence_mask;
} edit_op_json_field_t;

typedef nmo_status_t (*edit_op_json_build_fn)(nmo_edit_plan_t *plan,
                                              const nmo_edit_op_t *op);

/** JSON codec of an op: its fields in the order they are written. */
typedef struct edit_op_json_codec {
    nmo_edit_op_kind_t kind;
    const edit_op_json_field_t *fields;
    size_t field_count;
    edit_op_json_build_fn build;
} edit_op_json_codec_t;

_Static_assert(sizeof(nmo_object_id_t) == sizeof(uint32_t),
               "edit op id fields are stored as uint32");
_Static_assert(sizeof(nmo_script_edit_io_kind_t) == sizeof(int) &&
                   sizeof(nmo_script_edit_parameter_kind_t) == sizeof(int) &&
                   sizeof(nmo_script_edit_interface_mode_t) == sizeof(int) &&
                   sizeof(nmo_behavior_fold_interface_mode_t) == sizeof(int),
               "edit op enum fields are stored as int");

static const edit_op_json_enum_name_t IO_KIND_NAMES[] = {
    {"input", NMO_SCRIPT_EDIT_IO_INPUT},
    {"output", NMO_SCRIPT_EDIT_IO_OUTPUT},
    {"in", NMO_SCRIPT_EDIT_IO_INPUT},
    {"out", NMO_SCRIPT_EDIT_IO_OUTPUT},
    {NULL, 0},
};

static const edit_op_json_enum_name_t PARAMETER_KIND_NAMES[] = {
    {"in", NMO_SCRIPT_EDIT_PARAM_IN},
    {"out", NMO_SCRIPT_EDIT_PARAM_OUT},
    {"local", NMO_SCRIPT_EDIT_PARAM_LOCAL},
    {"shared", NMO_SCRIPT_EDIT_PARAM_SHARED},
    {"input", NMO_SCRIPT_EDIT_PARAM_IN},
    {"output", NMO_SCRIPT_EDIT_PARAM_OUT},
    {NULL, 0},
};

static const edit_op_json_enum_name_t INTERFACE_MODE_NAMES[] = {
    {"preserve", NMO_SCRIPT_EDIT_INTERFACE_PRESERVE},
    {"canonicalize", NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE},
    {"remove", NMO_SCRIPT_EDIT_INTERFACE_REMOVE},
    {NULL, 0},
};

static const edit_op_json_enum_name_t FOLD_INTERFACE_MODE_NAMES[] = {
    {"preserve", NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE},
    {"canonicalize", NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE},
    {"remove", NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE},
    {NULL, 0},
};

#define OP_DATA(_member) offsetof(nmo_edit_op_t, data._member)
#define OP_PRIMARY_ID offsetof(nmo_edit_op_t, primary_id)
#define OP_FIELD(_key, _type, _member) \
    {.key = (_key), .type = EDIT_OP_JSON_##_type, .offset = OP_DATA(_member)}
#define OP_ENUM(_key, _type, _member, _names) \
    {.key = (_key), .type = EDIT_OP_JSON_##_type, .offset = OP_DATA(_member), \
     .names = (_names)}
#define OP_COUNTED(_key, _type, _member, _count_member) \
    {.key = (_key), .type = EDIT_OP_JSON_##_type, .offset = OP_DATA(_member), \
     .aux_offset = OP_DATA(_count_member)}
#define OP_FOLD_MAPS(_key, _member, _count_member, _map_kind) \
    {.key = (_key), .type = EDIT_OP_JSON_FOLD_MAPS, .offset = OP_DATA(_member), \
     .aux_offset = OP_DATA(_count_member), .map_kind = (_map_kind)}
#define OP_OPTION(_key, _type, _member, _has_member) \
    {.key = (_key), .type = EDIT_OP_JSON_##_type, .offset = OP_DATA(_member), \
     .presence_offset = OP_DATA(_has_member)}
/* _prefix_id is the id key, _prefix_operation and _prefix_handle the reference keys */
#define OP_REF(_type, _prefix, _id_key, _id_offset, _ref_member) \
    {.key = (_id_key), .type = EDIT_OP_JSON_##_type, .offset = (_id_offset), \
     .operation_key = _prefix "_operation", .handle_key = _prefix "_handle", \
     .aux_offset = OP_DATA(_ref_member)}
#define OP_SLOT_REF(_prefix, _id_member, _ref_member, _flags_member, _mask) \
    {.key = _prefix "_id", .type = EDIT_OP_JSON_OPT_REF, .offset = OP_DATA(_id_member), \
     .operation_key = _prefix "_operation", .handle_key = _prefix "_handle", \
     .aux_offset = OP_DATA(_ref_member), \
     .presence_offset = OP_DATA(_flags_member), .presence_mask = (_mask)}

static const nmo_edit_handle_ref_t *edit_op_ref_or_null(
    const nmo_edit_handle_ref_t *ref)
{
    return ref->has_ref ? ref : NULL;
}

static const edit_op_json_field_t SET_PARAMETER_VALUE_FIELDS[] = {
    OP_REF(REF, "parameter", "parameter_id", OP_PRIMARY_ID,
           set_value.parameter_ref),
    OP_FIELD("value", STRING, set_value.value),
    OP_OPTION("resize", OPT_BOOL, set_value.options.resize,
              set_value.has_options),
    OP_OPTION("manager_entry", MANAGER_ENTRY,
              set_value.options.manager_entry, set_value.has_options),
};

static nmo_status_t build_set_parameter_value(nmo_edit_plan_t *plan,
                                              const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_set_parameter_value(
        plan, op->primary_id,
        edit_op_ref_or_null(&op->data.set_value.parameter_ref),
        op->data.set_value.value,
        op->data.set_value.has_options ? &op->data.set_value.options : NULL);
}

static const edit_op_json_field_t SET_PARAMETER_BYTES_FIELDS[] = {
    OP_REF(REF, "parameter", "parameter_id", OP_PRIMARY_ID,
           set_bytes.parameter_ref),
    OP_COUNTED("hex", HEX, set_bytes.bytes, set_bytes.byte_count),
    OP_OPTION("resize", OPT_BOOL, set_bytes.options.resize,
              set_bytes.has_options),
};

static nmo_status_t build_set_parameter_bytes(nmo_edit_plan_t *plan,
                                              const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_set_parameter_bytes(
        plan, op->primary_id,
        edit_op_ref_or_null(&op->data.set_bytes.parameter_ref),
        op->data.set_bytes.bytes, op->data.set_bytes.byte_count,
        op->data.set_bytes.has_options ? &op->data.set_bytes.options : NULL);
}

static const edit_op_json_field_t ADD_NODE_FIELDS[] = {
    OP_FIELD("behavior_id", ID, add_node.parent_behavior_id),
    OP_FIELD("guid", GUID, add_node.bb_guid),
    OP_FIELD("name", OPT_STRING, add_node.name),
    OP_OPTION("manager_entry", MANAGER_ENTRY,
              add_node.options.manager_entry, add_node.has_options),
};

static nmo_status_t build_add_node(nmo_edit_plan_t *plan,
                                   const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_node_ex(
        plan, op->data.add_node.parent_behavior_id, op->data.add_node.bb_guid,
        op->data.add_node.name,
        op->data.add_node.has_options ? &op->data.add_node.options : NULL);
}

static const edit_op_json_field_t REMOVE_NODE_FIELDS[] = {
    OP_FIELD("parent_id", ID, remove_node.parent_behavior_id),
    OP_FIELD("node_id", ID, remove_node.node_id),
    OP_FIELD("delete_flags", OPT_U32, remove_node.delete_flags),
};

static nmo_status_t build_remove_node(nmo_edit_plan_t *plan,
                                      const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_remove_node(
        plan, op->data.remove_node.parent_behavior_id,
        op->data.remove_node.node_id, op->data.remove_node.delete_flags);
}

static const edit_op_json_field_t ADD_IO_FIELDS[] = {
    OP_FIELD("behavior_id", ID, add_io.behavior_id),
    OP_ENUM("kind", ENUM, add_io.kind, IO_KIND_NAMES),
    OP_FIELD("name", STRING, add_io.name),
};

static nmo_status_t build_add_io(nmo_edit_plan_t *plan,
                                 const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_io(plan, op->data.add_io.behavior_id,
                                op->data.add_io.kind, op->data.add_io.name);
}

static const edit_op_json_field_t RENAME_IO_FIELDS[] = {
    OP_FIELD("io_id", ID, rename_io.io_id),
    OP_FIELD("name", STRING, rename_io.name),
};

static nmo_status_t build_rename_io(nmo_edit_plan_t *plan,
                                    const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_rename_io(plan, op->data.rename_io.io_id,
                                       op->data.rename_io.name);
}

static const edit_op_json_field_t REMOVE_IO_FIELDS[] = {
    OP_FIELD("io_id", ID, remove_io.io_id),
    OP_FIELD("detach_links", OPT_BOOL, remove_io.detach_links),
};

static nmo_status_t build_remove_io(nmo_edit_plan_t *plan,
                                    const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_remove_io(plan, op->data.remove_io.io_id,
                                       op->data.remove_io.detach_links);
}

static const edit_op_json_field_t ADD_BEHAVIOR_LINK_FIELDS[] = {
    OP_FIELD("parent_id", ID, add_link.parent_behavior_id),
    OP_REF(REF, "from", "from_io_id", OP_DATA(add_link.from_io_id),
           add_link.from_io_ref),
    OP_REF(REF, "to", "to_io_id", OP_DATA(add_link.to_io_id),
           add_link.to_io_ref),
    OP_FIELD("activation_delay", OPT_U32, add_link.activation_delay),
};

static nmo_status_t build_add_behavior_link(nmo_edit_plan_t *plan,
                                            const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_behavior_link(
        plan, op->data.add_link.parent_behavior_id,
        op->data.add_link.from_io_id,
        edit_op_ref_or_null(&op->data.add_link.from_io_ref),
        op->data.add_link.to_io_id,
        edit_op_ref_or_null(&op->data.add_link.to_io_ref),
        op->data.add_link.activation_delay);
}

static const edit_op_json_field_t REWIRE_LINK_FIELDS[] = {
    OP_FIELD("link_id", ID, rewire_link.link_id),
    OP_FIELD("from_io_id", U32, rewire_link.from_io_id),
    OP_FIELD("to_io_id", U32, rewire_link.to_io_id),
};

static nmo_status_t build_rewire_link(nmo_edit_plan_t *plan,
                                      const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_rewire_behavior_link(
        plan, op->data.rewire_link.link_id, op->data.rewire_link.from_io_id,
        op->data.rewire_link.to_io_id);
}

static const edit_op_json_field_t SET_LINK_DELAY_FIELDS[] = {
    OP_FIELD("link_id", ID, set_link_delay.link_id),
    OP_FIELD("activation_delay", U32, set_link_delay.activation_delay),
};

static nmo_status_t build_set_link_delay(nmo_edit_plan_t *plan,
                                         const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_set_behavior_link_delay(
        plan, op->data.set_link_delay.link_id,
        op->data.set_link_delay.activation_delay);
}

static const edit_op_json_field_t REMOVE_LINK_FIELDS[] = {
    OP_FIELD("parent_id", ID, remove_link.parent_behavior_id),
    OP_FIELD("link_id", ID, remove_link.link_id),
};

static nmo_status_t build_remove_link(nmo_edit_plan_t *plan,
                                      const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_remove_behavior_link(
        plan, op->data.remove_link.parent_behavior_id,
        op->data.remove_link.link_id);
}

static const edit_op_json_field_t ADD_PARAMETER_FIELDS[] = {
    OP_FIELD("owner_id", ID, add_parameter.owner_behavior_id),
    OP_ENUM("kind", ENUM, add_parameter.kind, PARAMETER_KIND_NAMES),
    OP_FIELD("type_guid", GUID, add_parameter.type_guid),
    OP_FIELD("name", STRING, add_parameter.name),
};

static nmo_status_t build_add_parameter(nmo_edit_plan_t *plan,
                                        const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_parameter(
        plan, op->data.add_parameter.owner_behavior_id,
        op->data.add_parameter.kind, op->data.add_parameter.type_guid,
        op->data.add_parameter.name);
}

static const edit_op_json_field_t CONNECT_PARAMETER_FIELDS[] = {
    OP_FIELD("source_id", ID, connect_parameter.source_parameter_id),
    OP_REF(REF, "target", "target_id",
           OP_DATA(connect_parameter.target_parameter_id),
           connect_parameter.target_parameter_ref),
};

static nmo_status_t build_connect_parameter(nmo_edit_plan_t *plan,
                                            const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_connect_parameter(
        plan, op->data.connect_parameter.source_parameter_id,
        op->data.connect_parameter.target_parameter_id,
        edit_op_ref_or_null(&op->data.connect_parameter.target_parameter_ref));
}

static const edit_op_json_field_t DISCONNECT_PARAMETER_FIELDS[] = {
    OP_FIELD("target_id", ID, disconnect_parameter.target_parameter_id),
};

static nmo_status_t build_disconnect_parameter(nmo_edit_plan_t *plan,
                                               const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_disconnect_parameter(
        plan, op->data.disconnect_parameter.target_parameter_id);
}

static const edit_op_json_field_t REMOVE_PARAMETER_FIELDS[] = {
    OP_FIELD("parameter_id", ID, remove_parameter.parameter_id),
    OP_FIELD("detach", OPT_BOOL, remove_parameter.detach),
};

static nmo_status_t build_remove_parameter(nmo_edit_plan_t *plan,
                                           const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_remove_parameter(
        plan, op->data.remove_parameter.parameter_id,
        op->data.remove_parameter.detach);
}

static const edit_op_json_field_t ADD_OPERATION_FIELDS[] = {
    OP_FIELD("parent_id", ID, add_operation.parent_behavior_id),
    OP_FIELD("operation_guid", GUID, add_operation.operation_guid),
    OP_REF(OPT_REF, "in1", "in1_id", OP_DATA(add_operation.in1_parameter_id),
           add_operation.in1_parameter_ref),
    OP_REF(OPT_REF, "in2", "in2_id", OP_DATA(add_operation.in2_parameter_id),
           add_operation.in2_parameter_ref),
    OP_REF(OPT_REF, "out", "out_id", OP_DATA(add_operation.out_parameter_id),
           add_operation.out_parameter_ref),
};

static nmo_status_t build_add_operation(nmo_edit_plan_t *plan,
                                        const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_operation(
        plan, op->data.add_operation.parent_behavior_id,
        op->data.add_operation.operation_guid,
        op->data.add_operation.in1_parameter_id,
        edit_op_ref_or_null(&op->data.add_operation.in1_parameter_ref),
        op->data.add_operation.in2_parameter_id,
        edit_op_ref_or_null(&op->data.add_operation.in2_parameter_ref),
        op->data.add_operation.out_parameter_id,
        edit_op_ref_or_null(&op->data.add_operation.out_parameter_ref));
}

static const edit_op_json_field_t REWIRE_OPERATION_FIELDS[] = {
    OP_FIELD("operation_id", ID, rewire_operation.operation_id),
    OP_SLOT_REF("in1", rewire_operation.in1_parameter_id,
                rewire_operation.in1_parameter_ref,
                rewire_operation.slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_IN1),
    OP_SLOT_REF("in2", rewire_operation.in2_parameter_id,
                rewire_operation.in2_parameter_ref,
                rewire_operation.slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_IN2),
    OP_SLOT_REF("out", rewire_operation.out_parameter_id,
                rewire_operation.out_parameter_ref,
                rewire_operation.slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_OUT),
};

static nmo_status_t build_rewire_operation(nmo_edit_plan_t *plan,
                                           const nmo_edit_op_t *op)
{
    if (op->data.rewire_operation.slot_flags == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "rewire_operation requires in1_id, in1_operation, in2_id, in2_operation, out_id, or out_operation");
    }
    return nmo_edit_plan_add_rewire_operation(
        plan, op->data.rewire_operation.operation_id,
        op->data.rewire_operation.slot_flags,
        op->data.rewire_operation.in1_parameter_id,
        edit_op_ref_or_null(&op->data.rewire_operation.in1_parameter_ref),
        op->data.rewire_operation.in2_parameter_id,
        edit_op_ref_or_null(&op->data.rewire_operation.in2_parameter_ref),
        op->data.rewire_operation.out_parameter_id,
        edit_op_ref_or_null(&op->data.rewire_operation.out_parameter_ref));
}

static const edit_op_json_field_t REMOVE_OPERATION_FIELDS[] = {
    OP_FIELD("operation_id", ID, remove_operation.operation_id),
};

static nmo_status_t build_remove_operation(nmo_edit_plan_t *plan,
                                           const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_remove_operation(
        plan, op->data.remove_operation.operation_id);
}

static const edit_op_json_field_t INTERFACE_POLICY_FIELDS[] = {
    OP_FIELD("behavior_id", ID, interface_policy.behavior_id),
    OP_ENUM("mode", ENUM, interface_policy.mode, INTERFACE_MODE_NAMES),
};

static nmo_status_t build_interface_policy(nmo_edit_plan_t *plan,
                                           const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_interface_policy(
        plan, op->data.interface_policy.behavior_id,
        op->data.interface_policy.mode);
}

static const edit_op_json_field_t SET_DATA_CELL_FIELDS[] = {
    OP_FIELD("dataarray_id", ID, data_cell.dataarray_id),
    OP_FIELD("row", U32, data_cell.row),
    OP_FIELD("col", U32, data_cell.col),
    OP_FIELD("value", STRING, data_cell.value),
};

static nmo_status_t build_set_data_cell(nmo_edit_plan_t *plan,
                                        const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_data_cell(
        plan, op->data.data_cell.dataarray_id, op->data.data_cell.row,
        op->data.data_cell.col, op->data.data_cell.value);
}

static const edit_op_json_field_t FOLD_FIELDS[] = {
    OP_FIELD("parent_id", ID, fold.desc.parent_id),
    OP_COUNTED("nodes", ID_ARRAY, fold.desc.node_ids, fold.desc.node_count),
    OP_FIELD("anchor_id", ID, fold.desc.anchor_id),
    OP_FIELD("guid", GUID, fold.desc.block_guid),
    OP_FIELD("name", STRING, fold.desc.name),
    OP_FIELD("version", OPT_U32, fold.desc.block_version),
    OP_FIELD("preserve_boundary", OPT_BOOL, fold.desc.preserve_boundary),
    OP_FIELD("preserve_links", OPT_BOOL, fold.desc.preserve_links),
    OP_FIELD("preserve_params", OPT_BOOL, fold.desc.preserve_params),
    OP_ENUM("interface", OPT_ENUM, fold.desc.interface_mode,
            FOLD_INTERFACE_MODE_NAMES),
    OP_FOLD_MAPS("inputs", fold.desc.input_maps, fold.desc.input_map_count,
                 NMO_BEHAVIOR_FOLD_MAP_INPUT),
    OP_FOLD_MAPS("outputs", fold.desc.output_maps, fold.desc.output_map_count,
                 NMO_BEHAVIOR_FOLD_MAP_OUTPUT),
    OP_FOLD_MAPS("parameters", fold.desc.parameter_maps,
                 fold.desc.parameter_map_count,
                 NMO_BEHAVIOR_FOLD_MAP_PARAMETER),
};

static nmo_status_t build_fold(nmo_edit_plan_t *plan,
                               const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_fold(plan, &op->data.fold.desc);
}

static const edit_op_json_field_t REPLACE_BB_FIELDS[] = {
    OP_FIELD("behavior_id", ID, replace_bb.desc.behavior_id),
    OP_FIELD("name", STRING, replace_bb.desc.name),
    OP_FIELD("guid", GUID, replace_bb.desc.block_guid),
    OP_FIELD("version", OPT_U32, replace_bb.desc.block_version),
    OP_FIELD("preserve_links", OPT_BOOL, replace_bb.desc.preserve_links),
    OP_FIELD("preserve_params", OPT_BOOL, replace_bb.desc.preserve_params),
};

static nmo_status_t build_replace_bb(nmo_edit_plan_t *plan,
                                     const nmo_edit_op_t *op)
{
    return nmo_edit_plan_add_replace_bb(plan, &op->data.replace_bb.desc);
}

#undef OP_DATA
#undef OP_PRIMARY_ID
#undef OP_FIELD
#undef OP_ENUM
#undef OP_COUNTED
#undef OP_FOLD_MAPS
#undef OP_OPTION
#undef OP_REF
#undef OP_SLOT_REF

#define OP_CODEC(_kind, _fields, _build) \
    {(_kind), (_fields), sizeof(_fields) / sizeof((_fields)[0]), (_build)}

static const edit_op_json_codec_t EDIT_OP_JSON_CODECS[] = {
    OP_CODEC(NMO_EDIT_OP_SET_PARAMETER_VALUE, SET_PARAMETER_VALUE_FIELDS,
             build_set_parameter_value),
    OP_CODEC(NMO_EDIT_OP_SET_PARAMETER_BYTES, SET_PARAMETER_BYTES_FIELDS,
             build_set_parameter_bytes),
    OP_CODEC(NMO_EDIT_OP_ADD_NODE, ADD_NODE_FIELDS, build_add_node),
    OP_CODEC(NMO_EDIT_OP_REMOVE_NODE, REMOVE_NODE_FIELDS, build_remove_node),
    OP_CODEC(NMO_EDIT_OP_ADD_IO, ADD_IO_FIELDS, build_add_io),
    OP_CODEC(NMO_EDIT_OP_RENAME_IO, RENAME_IO_FIELDS, build_rename_io),
    OP_CODEC(NMO_EDIT_OP_REMOVE_IO, REMOVE_IO_FIELDS, build_remove_io),
    OP_CODEC(NMO_EDIT_OP_ADD_BEHAVIOR_LINK, ADD_BEHAVIOR_LINK_FIELDS,
             build_add_behavior_link),
    OP_CODEC(NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK, REWIRE_LINK_FIELDS,
             build_rewire_link),
    OP_CODEC(NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY, SET_LINK_DELAY_FIELDS,
             build_set_link_delay),
    OP_CODEC(NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK, REMOVE_LINK_FIELDS,
             build_remove_link),
    OP_CODEC(NMO_EDIT_OP_ADD_PARAMETER, ADD_PARAMETER_FIELDS,
             build_add_parameter),
    OP_CODEC(NMO_EDIT_OP_CONNECT_PARAMETER, CONNECT_PARAMETER_FIELDS,
             build_connect_parameter),
    OP_CODEC(NMO_EDIT_OP_DISCONNECT_PARAMETER, DISCONNECT_PARAMETER_FIELDS,
             build_disconnect_parameter),
    OP_CODEC(NMO_EDIT_OP_REMOVE_PARAMETER, REMOVE_PARAMETER_FIELDS,
             build_remove_parameter),
    OP_CODEC(NMO_EDIT_OP_ADD_OPERATION, ADD_OPERATION_FIELDS,
             build_add_operation),
    OP_CODEC(NMO_EDIT_OP_REWIRE_OPERATION, REWIRE_OPERATION_FIELDS,
             build_rewire_operation),
    OP_CODEC(NMO_EDIT_OP_REMOVE_OPERATION, REMOVE_OPERATION_FIELDS,
             build_remove_operation),
    OP_CODEC(NMO_EDIT_OP_INTERFACE_POLICY, INTERFACE_POLICY_FIELDS,
             build_interface_policy),
    OP_CODEC(NMO_EDIT_OP_SET_DATA_CELL, SET_DATA_CELL_FIELDS,
             build_set_data_cell),
    OP_CODEC(NMO_EDIT_OP_FOLD, FOLD_FIELDS, build_fold),
    OP_CODEC(NMO_EDIT_OP_REPLACE_BB, REPLACE_BB_FIELDS, build_replace_bb),
};

#undef OP_CODEC

static const edit_op_json_codec_t *edit_op_json_codec_find(
    nmo_edit_op_kind_t kind)
{
    for (size_t i = 0u;
         i < sizeof(EDIT_OP_JSON_CODECS) / sizeof(EDIT_OP_JSON_CODECS[0]);
         ++i) {
        if (EDIT_OP_JSON_CODECS[i].kind == kind) {
            return &EDIT_OP_JSON_CODECS[i];
        }
    }
    return NULL;
}

static const char *edit_op_json_enum_name(
    const edit_op_json_enum_name_t *names,
    int value)
{
    for (const edit_op_json_enum_name_t *entry = names; entry->name != NULL;
         ++entry) {
        if (entry->value == value) {
            return entry->name;
        }
    }
    return names[0].name;
}

/** Whether the optional group of a field is set; true for a field outside a group. */
static bool edit_op_json_field_present(const edit_op_json_field_t *field,
                                       const nmo_edit_op_t *op)
{
    if (field->presence_offset == 0u) {
        return true;
    }
    const unsigned char *ptr = (const unsigned char *)op + field->presence_offset;
    if (field->presence_mask != 0u) {
        uint32_t flags = 0u;
        memcpy(&flags, ptr, sizeof(flags));
        return (flags & field->presence_mask) != 0u;
    }
    bool present = false;
    memcpy(&present, ptr, sizeof(present));
    return present;
}

static void edit_op_json_write_fields(yyjson_mut_doc *doc,
                                      yyjson_mut_val *obj,
                                      const edit_op_json_codec_t *codec,
                                      const nmo_edit_op_t *op)
{
    for (size_t i = 0u; i < codec->field_count; ++i) {
        const edit_op_json_field_t *field = &codec->fields[i];
        const unsigned char *ptr = (const unsigned char *)op + field->offset;
        const unsigned char *aux = (const unsigned char *)op + field->aux_offset;
        if (!edit_op_json_field_present(field, op)) {
            continue;
        }
        switch (field->type) {
            case EDIT_OP_JSON_ID:
            case EDIT_OP_JSON_U32:
            case EDIT_OP_JSON_OPT_U32: {
                uint32_t value = 0u;
                memcpy(&value, ptr, sizeof(value));
                yyjson_mut_obj_add_uint(doc, obj, field->key, (uint64_t)value);
                break;
            }
            case EDIT_OP_JSON_OPT_BOOL: {
                bool value = false;
                memcpy(&value, ptr, sizeof(value));
                yyjson_mut_obj_add_bool(doc, obj, field->key, value);
                break;
            }
            case EDIT_OP_JSON_STRING:
            case EDIT_OP_JSON_OPT_STRING: {
                const char *value = NULL;
                memcpy(&value, ptr, sizeof(value));
                add_str_safe(doc, obj, field->key, value);
                break;
            }
            case EDIT_OP_JSON_GUID: {
                nmo_guid_t value;
                memcpy(&value, ptr, sizeof(value));
                add_guid_json(doc, obj, field->key, value);
                break;
            }
            case EDIT_OP_JSON_ENUM:
            case EDIT_OP_JSON_OPT_ENUM: {
                int value = 0;
                memcpy(&value, ptr, sizeof(value));
                yyjson_mut_obj_add_str(
                    doc, obj, field->key,
                    edit_op_json_enum_name(field->names, value));
                break;
            }
            case EDIT_OP_JSON_REF:
            case EDIT_OP_JSON_OPT_REF: {
                nmo_edit_handle_ref_t ref;
                nmo_object_id_t id = 0u;
                memcpy(&ref, aux, sizeof(ref));
                memcpy(&id, ptr, sizeof(id));
                if (ref.has_ref) {
                    add_ref_json(doc, obj, field->operation_key,
                                 field->handle_key, ref.operation_index,
                                 ref.handle_name);
                } else if (field->type == EDIT_OP_JSON_REF ||
                           field->presence_offset != 0u) {
                    yyjson_mut_obj_add_uint(doc, obj, field->key, (uint64_t)id);
                } else {
                    add_optional_id_json(doc, obj, field->key, id);
                }
                break;
            }
            case EDIT_OP_JSON_HEX: {
                const uint8_t *bytes = NULL;
                size_t count = 0u;
                memcpy(&bytes, ptr, sizeof(bytes));
                memcpy(&count, aux, sizeof(count));
                char *hex = bytes_to_hex(bytes, count);
                if (hex != NULL) {
                    yyjson_mut_obj_add_strcpy(doc, obj, field->key, hex);
                    free(hex);
                }
                break;
            }
            case EDIT_OP_JSON_ID_ARRAY: {
                const nmo_object_id_t *ids = NULL;
                size_t count = 0u;
                memcpy(&ids, ptr, sizeof(ids));
                memcpy(&count, aux, sizeof(count));
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                if (arr != NULL) {
                    for (size_t j = 0u; j < count; ++j) {
                        yyjson_mut_arr_add_uint(doc, arr, (uint64_t)ids[j]);
                    }
                    yyjson_mut_obj_add_val(doc, obj, field->key, arr);
                }
                break;
            }
            case EDIT_OP_JSON_FOLD_MAPS: {
                const nmo_behavior_fold_map_t *maps = NULL;
                size_t count = 0u;
                memcpy(&maps, ptr, sizeof(maps));
                memcpy(&count, aux, sizeof(count));
                yyjson_mut_obj_add_val(doc, obj, field->key,
                                       fold_maps_to_json(doc, maps, count));
                break;
            }
            case EDIT_OP_JSON_MANAGER_ENTRY:
                add_manager_entry_json(
                    doc, obj, (const nmo_manager_entry_options_t *)(const void *)ptr);
                break;
        }
    }
}

static yyjson_mut_val *edit_op_to_json(yyjson_mut_doc *doc,
                                       const nmo_edit_op_t *op)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || op == NULL) {
        return obj;
    }

    yyjson_mut_obj_add_str(doc, obj, "op", nmo_edit_op_kind_name(op->kind));

    const edit_op_json_codec_t *codec = edit_op_json_codec_find(op->kind);
    if (codec != NULL) {
        edit_op_json_write_fields(doc, obj, codec, op);
    }
    return obj;
}

static nmo_status_t edit_plan_json_write_root(
    const nmo_edit_plan_t *plan,
    const char *input_path,
    const char *output_path,
    bool include_paths,
    char **out_json)
{
    if (plan == NULL || out_json == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (include_paths &&
        (input_path == NULL || input_path[0] == '\0' ||
         output_path == NULL || output_path[0] == '\0')) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_json = NULL;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NMO_ERR_NOMEM;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_val *ops = yyjson_mut_arr(doc);
    if (root == NULL || ops == NULL) {
        yyjson_mut_doc_free(doc);
        return NMO_ERR_NOMEM;
    }
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_uint(doc, root, "version", 2u);
    if (include_paths) {
        add_str_safe(doc, root, "input", input_path);
        add_str_safe(doc, root, "output", output_path);
    }

    size_t count = nmo_edit_plan_count(plan);
    for (size_t i = 0; i < count; ++i) {
        const nmo_edit_op_t *op = nmo_edit_plan_get(plan, i);
        yyjson_mut_val *op_obj = edit_op_to_json(doc, op);
        if (op_obj == NULL) {
            yyjson_mut_doc_free(doc);
            return NMO_ERR_NOMEM;
        }
        yyjson_mut_arr_add_val(ops, op_obj);
    }
    yyjson_mut_obj_add_val(doc, root, "operations", ops);
    const nmo_probe_selector_result_t *analysis =
        nmo_edit_plan_get_probe_selector_analysis(plan);
    if (analysis != NULL) {
        yyjson_mut_val *analysis_obj = probe_analysis_to_json(doc, analysis);
        if (analysis_obj == NULL) {
            yyjson_mut_doc_free(doc);
            return NMO_ERR_NOMEM;
        }
        yyjson_mut_obj_add_val(doc, root, "probe_selector_analysis",
                               analysis_obj);
    }

    size_t json_len = 0u;
    char *json = yyjson_mut_write(doc, 0, &json_len);
    yyjson_mut_doc_free(doc);
    if (json == NULL) {
        return NMO_ERR_NOMEM;
    }

    *out_json = json;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_json_write(
    const nmo_edit_plan_t *plan,
    char **out_json)
{
    return edit_plan_json_write_root(plan, NULL, NULL, false, out_json);
}

nmo_status_t nmo_edit_plan_manifest_json_write(
    const nmo_edit_plan_t *plan,
    const char *input_path,
    const char *output_path,
    char **out_json)
{
    return edit_plan_json_write_root(
        plan, input_path, output_path, true, out_json);
}

static char *dup_string(const char *value)
{
    if (value == NULL) {
        return NULL;
    }
    size_t len = strlen(value);
    char *copy = (char *)malloc(len + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, len + 1u);
    return copy;
}

static bool json_key_allowed(const char *key,
                             const char *const *allowed,
                             size_t allowed_count)
{
    if (key == NULL) {
        return false;
    }
    for (size_t i = 0; i < allowed_count; ++i) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static nmo_status_t reject_unknown_fields(yyjson_val *obj,
                                          const char *where,
                                          const char *const *allowed,
                                          size_t allowed_count)
{
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *key = NULL;
    yyjson_val *val = NULL;
    yyjson_obj_foreach(obj, idx, max, key, val) {
        (void)val;
        const char *name = yyjson_get_str(key);
        if (!json_key_allowed(name, allowed, allowed_count)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Unknown field '%s' in %s",
                             name != NULL ? name : "(null)", where);
        }
    }
    NMO_RETURN_OK();
}

#define RETURN_IF_UNKNOWN_FIELDS(obj, where, allowed) \
    NMO_RETURN_IF_ERROR(reject_unknown_fields( \
        (obj), (where), (allowed), sizeof(allowed) / sizeof((allowed)[0])))

static bool read_required_u32(yyjson_val *obj,
                              const char *key,
                              uint32_t *out_value,
                              bool allow_zero)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL || !yyjson_is_uint(value) ||
        yyjson_get_uint(value) > UINT32_MAX) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Missing or invalid %s", key);
        return false;
    }
    if (!allow_zero && yyjson_get_uint(value) == 0u) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Missing or invalid %s", key);
        return false;
    }
    *out_value = (uint32_t)yyjson_get_uint(value);
    return true;
}

static bool read_optional_u32(yyjson_val *obj,
                              const char *key,
                              uint32_t *out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL) {
        return true;
    }
    if (!yyjson_is_uint(value) || yyjson_get_uint(value) > UINT32_MAX) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid %s", key);
        return false;
    }
    *out_value = (uint32_t)yyjson_get_uint(value);
    return true;
}

static bool read_optional_bool(yyjson_val *obj,
                               const char *key,
                               bool default_value,
                               bool *out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL) {
        *out_value = default_value;
        return true;
    }
    if (!yyjson_is_bool(value)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid %s", key);
        return false;
    }
    *out_value = yyjson_get_bool(value);
    return true;
}

static bool read_required_string(yyjson_val *obj,
                                 const char *key,
                                 const char **out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL || !yyjson_is_str(value) ||
        yyjson_get_str(value)[0] == '\0') {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Missing or invalid %s", key);
        return false;
    }
    *out_value = yyjson_get_str(value);
    return true;
}

static bool read_optional_string(yyjson_val *obj,
                                 const char *key,
                                 const char **out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL) {
        *out_value = NULL;
        return true;
    }
    if (!yyjson_is_str(value)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__, "Invalid %s", key);
        return false;
    }
    *out_value = yyjson_get_str(value);
    return true;
}

static bool read_optional_guid(yyjson_val *obj,
                               const char *key,
                               nmo_guid_t *out_guid)
{
    const char *text = NULL;
    if (!read_optional_string(obj, key, &text)) {
        return false;
    }
    if (text == NULL) {
        *out_guid = NMO_GUID_NULL;
        return true;
    }
    *out_guid = nmo_guid_parse(text);
    if (nmo_guid_is_null(*out_guid)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__, "Invalid %s", key);
        return false;
    }
    return true;
}

static bool read_optional_double(yyjson_val *obj,
                                 const char *key,
                                 double *out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL) {
        *out_value = 0.0;
        return true;
    }
    if (!yyjson_is_num(value)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__, "Invalid %s", key);
        return false;
    }
    *out_value = yyjson_is_real(value)
                     ? yyjson_get_real(value)
                     : (double)yyjson_get_uint(value);
    return true;
}

static bool parse_probe_selector_mode_value(
    yyjson_val *value,
    nmo_probe_selector_mode_t *out_mode)
{
    if (value == NULL || out_mode == NULL || !yyjson_is_str(value)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid probe_selector_analysis.mode");
        return false;
    }
    const char *text = yyjson_get_str(value);
    if (strcmp(text, "auto") == 0) {
        *out_mode = NMO_PROBE_SELECTOR_MODE_AUTO;
    } else if (strcmp(text, "explicit_node") == 0) {
        *out_mode = NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE;
    } else if (strcmp(text, "explicit_link") == 0) {
        *out_mode = NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK;
    } else if (strcmp(text, "explicit_operation") == 0) {
        *out_mode = NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION;
    } else if (strcmp(text, "explicit_data_cell") == 0) {
        *out_mode = NMO_PROBE_SELECTOR_MODE_EXPLICIT_DATA_CELL;
    } else if (strcmp(text, "explicit") == 0) {
        *out_mode = NMO_PROBE_SELECTOR_MODE_EXPLICIT;
    } else {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid probe_selector_analysis.mode");
        return false;
    }
    return true;
}

static bool parse_probe_selector_status_value(
    yyjson_val *value,
    nmo_probe_selector_status_t *out_status)
{
    if (value == NULL || out_status == NULL || !yyjson_is_str(value)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid probe_selector_analysis.status");
        return false;
    }
    const char *text = yyjson_get_str(value);
    if (strcmp(text, "selected") == 0) {
        *out_status = NMO_PROBE_SELECTOR_STATUS_SELECTED;
    } else if (strcmp(text, "none") == 0) {
        *out_status = NMO_PROBE_SELECTOR_STATUS_NONE;
    } else if (strcmp(text, "ambiguous") == 0) {
        *out_status = NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS;
    } else if (strcmp(text, "unsafe") == 0) {
        *out_status = NMO_PROBE_SELECTOR_STATUS_UNSAFE;
    } else {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid probe_selector_analysis.status");
        return false;
    }
    return true;
}

static bool parse_probe_candidate_role_value(
    yyjson_val *value,
    nmo_probe_candidate_role_t *out_role)
{
    if (out_role == NULL) {
        nmo_last_error_setf(
            NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, __FILE__, __LINE__,
            "Invalid probe_selector_analysis.candidates.role");
        return false;
    }
    if (value == NULL) {
        *out_role = NMO_PROBE_CANDIDATE_UNKNOWN;
        return true;
    }
    if (!yyjson_is_str(value)) {
        nmo_last_error_setf(
            NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, __FILE__, __LINE__,
            "Invalid probe_selector_analysis.candidates.role");
        return false;
    }
    const char *text = yyjson_get_str(value);
    if (text[0] == '\0') {
        *out_role = NMO_PROBE_CANDIDATE_UNKNOWN;
    } else if (strcmp(text, "message") == 0) {
        *out_role = NMO_PROBE_CANDIDATE_MESSAGE;
    } else if (strcmp(text, "sender") == 0) {
        *out_role = NMO_PROBE_CANDIDATE_MESSAGE_SENDER;
    } else if (strcmp(text, "waiter") == 0) {
        *out_role = NMO_PROBE_CANDIDATE_MESSAGE_WAITER;
    } else if (strcmp(text, "receiver") == 0) {
        *out_role = NMO_PROBE_CANDIDATE_MESSAGE_RECEIVER;
    } else if (strcmp(text, "data_writer") == 0) {
        *out_role = NMO_PROBE_CANDIDATE_DATA_WRITER;
    } else if (strcmp(text, "data_write_operation") == 0) {
        *out_role = NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION;
    } else if (strcmp(text, "data_write_link") == 0) {
        *out_role = NMO_PROBE_CANDIDATE_DATA_WRITE_LINK;
    } else {
        nmo_last_error_setf(
            NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, __FILE__, __LINE__,
            "Invalid probe_selector_analysis.candidates.role");
        return false;
    }
    return true;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static nmo_status_t parse_hex_bytes(const char *hex,
                                    uint8_t **out_bytes,
                                    size_t *out_count)
{
    if (hex == NULL || out_bytes == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    size_t len = strlen(hex);
    if ((len % 2u) != 0u) {
        return NMO_ERR_INVALID_FORMAT;
    }
    size_t count = len / 2u;
    uint8_t *bytes = NULL;
    if (count != 0u) {
        bytes = (uint8_t *)malloc(count);
        if (bytes == NULL) {
            return NMO_ERR_NOMEM;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            free(bytes);
            return NMO_ERR_INVALID_FORMAT;
        }
        bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_bytes = bytes;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t parse_id_array(yyjson_val *arr,
                                   nmo_object_id_t **out_ids,
                                   size_t *out_count)
{
    if (arr == NULL || !yyjson_is_arr(arr) || yyjson_arr_size(arr) == 0u ||
        out_ids == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_FORMAT;
    }
    size_t count = yyjson_arr_size(arr);
    nmo_object_id_t *ids = (nmo_object_id_t *)calloc(count, sizeof(*ids));
    if (ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *item = NULL;
    yyjson_arr_foreach(arr, idx, max, item) {
        if (!yyjson_is_uint(item) || yyjson_get_uint(item) == 0u ||
            yyjson_get_uint(item) > UINT32_MAX) {
            free(ids);
            return NMO_ERR_INVALID_FORMAT;
        }
        ids[idx] = (nmo_object_id_t)yyjson_get_uint(item);
    }
    *out_ids = ids;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t parse_fold_maps(yyjson_val *arr,
                                    nmo_behavior_fold_map_kind_t kind,
                                    nmo_behavior_fold_map_t **out_maps,
                                    size_t *out_count)
{
    *out_maps = NULL;
    *out_count = 0u;
    if (arr == NULL) {
        return NMO_OK;
    }
    if (!yyjson_is_arr(arr)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    size_t count = yyjson_arr_size(arr);
    if (count == 0u) {
        return NMO_OK;
    }
    nmo_behavior_fold_map_t *maps =
        (nmo_behavior_fold_map_t *)calloc(count, sizeof(*maps));
    if (maps == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *item = NULL;
    yyjson_arr_foreach(arr, idx, max, item) {
        static const char *const allowed[] = {
            "old_index", "new_index", "old_id", "new_id",
            "old_io_id", "new_io_id",
            "old_parameter_id", "new_parameter_id", "label",
        };
        uint32_t old_index = 0u;
        uint32_t new_index = 0u;
        uint32_t old_id = 0u;
        uint32_t new_id = 0u;
        if (!yyjson_is_obj(item) ||
            reject_unknown_fields(
                item, "fold map", allowed,
                sizeof(allowed) / sizeof(allowed[0])) != NMO_OK ||
            !read_required_u32(item, "old_index", &old_index, true) ||
            !read_required_u32(item, "new_index", &new_index, true)) {
            free(maps);
            return NMO_ERR_INVALID_FORMAT;
        }
        const char *old_id_key =
            yyjson_obj_get(item, "old_id") != NULL ? "old_id" :
            (kind == NMO_BEHAVIOR_FOLD_MAP_PARAMETER
                 ? "old_parameter_id"
                 : "old_io_id");
        const char *new_id_key =
            yyjson_obj_get(item, "new_id") != NULL ? "new_id" :
            (kind == NMO_BEHAVIOR_FOLD_MAP_PARAMETER
                 ? "new_parameter_id"
                 : "new_io_id");
        if (!read_optional_u32(item, old_id_key, &old_id) ||
            !read_optional_u32(item, new_id_key, &new_id)) {
            free(maps);
            return NMO_ERR_INVALID_FORMAT;
        }
        maps[idx].kind = kind;
        maps[idx].old_index = old_index;
        maps[idx].new_index = new_index;
        maps[idx].old_id = old_id;
        maps[idx].new_id = new_id;
        yyjson_val *label_val = yyjson_obj_get(item, "label");
        maps[idx].label =
            label_val != NULL && yyjson_is_str(label_val)
                ? yyjson_get_str(label_val)
                : NULL;
    }
    *out_maps = maps;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t validate_operation_ref_index(
    const char *key,
    size_t ref_index,
    size_t current_index)
{
    if (ref_index >= current_index) {
        NMO_RETURN_ERROR(
            NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR,
            "%s must reference an earlier operation",
            key != NULL ? key : "operation reference");
    }
    return NMO_OK;
}

static bool edit_op_json_is_ref(const edit_op_json_field_t *field)
{
    return field->type == EDIT_OP_JSON_REF || field->type == EDIT_OP_JSON_OPT_REF;
}

/** Check that every handle reference of an op names an earlier operation. */
static nmo_status_t validate_parsed_op_refs(
    const nmo_edit_op_t *op,
    size_t current_index)
{
    if (op == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const edit_op_json_codec_t *codec = edit_op_json_codec_find(op->kind);
    for (size_t i = 0; codec != NULL && i < codec->field_count; ++i) {
        const edit_op_json_field_t *field = &codec->fields[i];
        if (!edit_op_json_is_ref(field)) {
            continue;
        }
        nmo_edit_handle_ref_t ref;
        memcpy(&ref, (const unsigned char *)op + field->aux_offset, sizeof(ref));
        if (ref.has_ref) {
            NMO_RETURN_IF_ERROR(validate_operation_ref_index(
                field->operation_key, ref.operation_index, current_index));
        }
    }
    return NMO_OK;
}

static nmo_status_t edit_op_json_read_ref(yyjson_val *obj,
                                          const edit_op_json_field_t *field,
                                          nmo_edit_op_kind_t kind,
                                          unsigned char *id_ptr,
                                          unsigned char *ref_ptr)
{
    yyjson_val *id_val = yyjson_obj_get(obj, field->key);
    yyjson_val *operation_val = yyjson_obj_get(obj, field->operation_key);
    yyjson_val *handle_val = yyjson_obj_get(obj, field->handle_key);
    bool has_id = id_val != NULL;
    bool has_ref = operation_val != NULL || handle_val != NULL;
    bool required = field->type == EDIT_OP_JSON_REF;

    if (!has_id && !has_ref && !required) {
        return NMO_OK;
    }
    if (has_id == has_ref) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "%s%s requires either %s or %s plus %s",
                         nmo_edit_op_kind_name(kind),
                         required ? "" : " operation",
                         field->key, field->operation_key, field->handle_key);
    }
    if (has_id) {
        if (!yyjson_is_uint(id_val) || yyjson_get_uint(id_val) > UINT32_MAX ||
            (required && yyjson_get_uint(id_val) == 0u)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Missing or invalid %s", field->key);
        }
        nmo_object_id_t id = (nmo_object_id_t)yyjson_get_uint(id_val);
        memcpy(id_ptr, &id, sizeof(id));
        return NMO_OK;
    }
    if (operation_val == NULL || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid %s", field->operation_key);
    }
    if (handle_val == NULL || !yyjson_is_str(handle_val) ||
        yyjson_get_str(handle_val)[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid %s", field->handle_key);
    }
    nmo_edit_handle_ref_t ref = {
        .has_ref = true,
        .operation_index = (size_t)(yyjson_get_uint(operation_val) - 1u),
        .handle_name = yyjson_get_str(handle_val),
    };
    memcpy(ref_ptr, &ref, sizeof(ref));
    return NMO_OK;
}

static nmo_status_t edit_op_json_read_field(yyjson_val *obj,
                                            const edit_op_json_field_t *field,
                                            nmo_edit_op_t *op)
{
    unsigned char *ptr = (unsigned char *)op + field->offset;
    unsigned char *aux = (unsigned char *)op + field->aux_offset;
    switch (field->type) {
        case EDIT_OP_JSON_ID:
        case EDIT_OP_JSON_U32:
        case EDIT_OP_JSON_OPT_U32: {
            uint32_t value = 0u;
            bool ok = field->type == EDIT_OP_JSON_OPT_U32
                          ? read_optional_u32(obj, field->key, &value)
                          : read_required_u32(obj, field->key, &value,
                                              field->type == EDIT_OP_JSON_U32);
            memcpy(ptr, &value, sizeof(value));
            return ok ? NMO_OK : NMO_ERR_INVALID_FORMAT;
        }
        case EDIT_OP_JSON_OPT_BOOL: {
            bool value = false;
            if (!read_optional_bool(obj, field->key, false, &value)) {
                return NMO_ERR_INVALID_FORMAT;
            }
            memcpy(ptr, &value, sizeof(value));
            return NMO_OK;
        }
        case EDIT_OP_JSON_STRING: {
            const char *value = NULL;
            if (!read_required_string(obj, field->key, &value)) {
                return NMO_ERR_INVALID_FORMAT;
            }
            memcpy(ptr, &value, sizeof(value));
            return NMO_OK;
        }
        case EDIT_OP_JSON_OPT_STRING: {
            yyjson_val *value = yyjson_obj_get(obj, field->key);
            if (value == NULL) {
                return NMO_OK;
            }
            if (!yyjson_is_str(value) || yyjson_get_str(value)[0] == '\0') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Invalid %s", field->key);
            }
            const char *text = yyjson_get_str(value);
            memcpy(ptr, &text, sizeof(text));
            return NMO_OK;
        }
        case EDIT_OP_JSON_GUID:
        case EDIT_OP_JSON_ENUM:
        case EDIT_OP_JSON_OPT_ENUM: {
            const char *text = NULL;
            if (field->type == EDIT_OP_JSON_OPT_ENUM) {
                yyjson_val *value = yyjson_obj_get(obj, field->key);
                if (value == NULL) {
                    return NMO_OK;
                }
                text = yyjson_get_str(value);
            } else if (!read_required_string(obj, field->key, &text)) {
                return NMO_ERR_INVALID_FORMAT;
            }
            if (field->type == EDIT_OP_JSON_GUID) {
                nmo_guid_t value = nmo_guid_parse(text);
                if (!nmo_guid_is_null(value)) {
                    memcpy(ptr, &value, sizeof(value));
                    return NMO_OK;
                }
            } else {
                for (const edit_op_json_enum_name_t *entry = field->names;
                     text != NULL && entry->name != NULL; ++entry) {
                    if (strcmp(entry->name, text) == 0) {
                        memcpy(ptr, &entry->value, sizeof(entry->value));
                        return NMO_OK;
                    }
                }
            }
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid %s", field->key);
        }
        case EDIT_OP_JSON_REF:
        case EDIT_OP_JSON_OPT_REF:
            return edit_op_json_read_ref(obj, field, op->kind, ptr, aux);
        case EDIT_OP_JSON_HEX: {
            const char *hex = NULL;
            uint8_t *bytes = NULL;
            size_t count = 0u;
            if (!read_required_string(obj, field->key, &hex)) {
                return NMO_ERR_INVALID_FORMAT;
            }
            NMO_RETURN_IF_ERROR(parse_hex_bytes(hex, &bytes, &count));
            memcpy(ptr, &bytes, sizeof(bytes));
            memcpy(aux, &count, sizeof(count));
            return NMO_OK;
        }
        case EDIT_OP_JSON_ID_ARRAY: {
            nmo_object_id_t *ids = NULL;
            size_t count = 0u;
            NMO_RETURN_IF_ERROR(
                parse_id_array(yyjson_obj_get(obj, field->key), &ids, &count));
            memcpy(ptr, &ids, sizeof(ids));
            memcpy(aux, &count, sizeof(count));
            return NMO_OK;
        }
        case EDIT_OP_JSON_FOLD_MAPS: {
            nmo_behavior_fold_map_t *maps = NULL;
            size_t count = 0u;
            NMO_RETURN_IF_ERROR(parse_fold_maps(
                yyjson_obj_get(obj, field->key),
                (nmo_behavior_fold_map_kind_t)field->map_kind, &maps, &count));
            memcpy(ptr, &maps, sizeof(maps));
            memcpy(aux, &count, sizeof(count));
            return NMO_OK;
        }
        case EDIT_OP_JSON_MANAGER_ENTRY: {
            nmo_manager_entry_options_t options = nmo_manager_entry_options_default();
            yyjson_val *value = yyjson_obj_get(obj, field->key);
            if (value != NULL && !parse_manager_entry_options_value(value, &options)) {
                return NMO_ERR_INVALID_FORMAT;
            }
            memcpy(ptr, &options, sizeof(options));
            return NMO_OK;
        }
    }
    return NMO_ERR_INVALID_FORMAT;
}

/** Free what reading the fields allocated; the plan keeps copies. */
static void edit_op_json_release_fields(const edit_op_json_codec_t *codec,
                                        nmo_edit_op_t *op)
{
    for (size_t i = 0u; i < codec->field_count; ++i) {
        const edit_op_json_field_t *field = &codec->fields[i];
        if (field->type == EDIT_OP_JSON_HEX ||
            field->type == EDIT_OP_JSON_ID_ARRAY ||
            field->type == EDIT_OP_JSON_FOLD_MAPS) {
            void *allocation = NULL;
            memcpy(&allocation, (unsigned char *)op + field->offset,
                   sizeof(allocation));
            free(allocation);
        }
    }
}

static bool edit_op_json_field_has_key(const edit_op_json_field_t *field,
                                       const char *name)
{
    return strcmp(name, field->key) == 0 ||
           (field->operation_key != NULL &&
            strcmp(name, field->operation_key) == 0) ||
           (field->handle_key != NULL && strcmp(name, field->handle_key) == 0);
}

static void edit_op_json_mark_present(yyjson_val *op_obj,
                                      const edit_op_json_field_t *field,
                                      nmo_edit_op_t *op)
{
    if (field->presence_offset == 0u) {
        return;
    }
    bool present = yyjson_obj_get(op_obj, field->key) != NULL ||
                   (field->operation_key != NULL &&
                    yyjson_obj_get(op_obj, field->operation_key) != NULL) ||
                   (field->handle_key != NULL &&
                    yyjson_obj_get(op_obj, field->handle_key) != NULL);
    if (!present) {
        return;
    }
    unsigned char *ptr = (unsigned char *)op + field->presence_offset;
    if (field->presence_mask != 0u) {
        uint32_t flags = 0u;
        memcpy(&flags, ptr, sizeof(flags));
        flags |= field->presence_mask;
        memcpy(ptr, &flags, sizeof(flags));
    } else {
        memcpy(ptr, &present, sizeof(present));
    }
}

static nmo_status_t parse_codec_op(yyjson_val *op_obj,
                                   const edit_op_json_codec_t *codec,
                                   nmo_edit_plan_t *plan)
{
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *key = NULL;
    yyjson_val *val = NULL;
    yyjson_obj_foreach(op_obj, idx, max, key, val) {
        (void)val;
        const char *name = yyjson_get_str(key);
        bool allowed = name != NULL && strcmp(name, "op") == 0;
        for (size_t i = 0u; !allowed && name != NULL && i < codec->field_count;
             ++i) {
            allowed = edit_op_json_field_has_key(&codec->fields[i], name);
        }
        if (!allowed) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Unknown field '%s' in %s operation",
                             name != NULL ? name : "(null)",
                             nmo_edit_op_kind_name(codec->kind));
        }
    }

    nmo_edit_op_t op;
    memset(&op, 0, sizeof(op));
    op.kind = codec->kind;
    nmo_status_t st = NMO_OK;
    for (size_t i = 0u; st == NMO_OK && i < codec->field_count; ++i) {
        edit_op_json_mark_present(op_obj, &codec->fields[i], &op);
        st = edit_op_json_read_field(op_obj, &codec->fields[i], &op);
    }
    if (st == NMO_OK) {
        st = codec->build(plan, &op);
    }
    edit_op_json_release_fields(codec, &op);
    return st;
}

static nmo_status_t parse_operations_array(yyjson_val *ops,
                                           nmo_edit_plan_t *plan)
{
    yyjson_val *op_obj = NULL;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(ops, &iter);
    while ((op_obj = yyjson_arr_iter_next(&iter)) != NULL) {
        nmo_status_t st = NMO_OK;
        size_t op_index = nmo_edit_plan_count(plan);
        if (!yyjson_is_obj(op_obj)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        const char *op_name = NULL;
        if (!read_required_string(op_obj, "op", &op_name)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        nmo_edit_op_kind_t kind = (nmo_edit_op_kind_t)0;
        st = nmo_edit_op_kind_parse(op_name, &kind);
        if (st != NMO_OK) {
            return st;
        }
        const edit_op_json_codec_t *codec = edit_op_json_codec_find(kind);
        st = codec != NULL ? parse_codec_op(op_obj, codec, plan)
                           : NMO_ERR_NOT_SUPPORTED;
        if (st != NMO_OK) {
            return st;
        }
        const nmo_edit_op_t *op = nmo_edit_plan_get(plan, op_index);
        st = validate_parsed_op_refs(op, op_index);
        if (st != NMO_OK) {
            return st;
        }
    }
    return NMO_OK;
}

static nmo_status_t parse_probe_safe_insertion(
    yyjson_val *obj,
    nmo_probe_safe_insertion_t *out_safe)
{
    if (obj == NULL) {
        memset(out_safe, 0, sizeof(*out_safe));
        return NMO_OK;
    }
    if (!yyjson_is_obj(obj)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid probe_selector_analysis.safe_insertion");
    }
    static const char *const allowed[] = {
        "selected", "selected_node_id", "selected_link_id",
        "selected_operation_id", "remove_link_id", "insert_from_io_id",
        "insert_to_io_id", "preserved_delay",
    };
    NMO_RETURN_IF_ERROR(reject_unknown_fields(
        obj, "probe_selector_analysis.safe_insertion", allowed,
        sizeof(allowed) / sizeof(allowed[0])));
    bool selected = false;
    if (!read_optional_bool(obj, "selected", false, &selected) ||
        !read_optional_u32(obj, "selected_node_id",
                           &out_safe->selected_node_id) ||
        !read_optional_u32(obj, "selected_link_id",
                           &out_safe->selected_link_id) ||
        !read_optional_u32(obj, "selected_operation_id",
                           &out_safe->selected_operation_id) ||
        !read_optional_u32(obj, "remove_link_id",
                           &out_safe->remove_link_id) ||
        !read_optional_u32(obj, "insert_from_io_id",
                           &out_safe->insert_from_io_id) ||
        !read_optional_u32(obj, "insert_to_io_id",
                           &out_safe->insert_to_io_id)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    out_safe->selected = selected;
    yyjson_val *delay = yyjson_obj_get(obj, "preserved_delay");
    if (delay != NULL) {
        if (!yyjson_is_uint(delay) || yyjson_get_uint(delay) > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid probe_selector_analysis.safe_insertion.preserved_delay");
        }
        out_safe->has_preserved_delay = true;
        out_safe->preserved_delay = (uint32_t)yyjson_get_uint(delay);
    }
    return NMO_OK;
}

static nmo_status_t parse_probe_candidate(
    yyjson_val *obj,
    nmo_probe_selector_candidate_t *out_candidate)
{
    if (obj == NULL || out_candidate == NULL || !yyjson_is_obj(obj)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid probe_selector_analysis.candidates");
    }
    static const char *const allowed[] = {
        "node_id", "parent_id", "boundary_behavior_id", "link_id",
        "operation_id", "from_io_id", "to_io_id", "delay",
        "source_parameter_id", "value_parameter_id", "dataarray_id",
        "column_type_guid", "confidence", "bb_guid", "proto_name", "role",
        "rejection_code",
    };
    NMO_RETURN_IF_ERROR(reject_unknown_fields(
        obj, "probe_selector_analysis.candidates", allowed,
        sizeof(allowed) / sizeof(allowed[0])));
    memset(out_candidate, 0, sizeof(*out_candidate));
    if (!read_optional_u32(obj, "node_id", &out_candidate->node_id) ||
        !read_optional_u32(obj, "parent_id", &out_candidate->parent_id) ||
        !read_optional_u32(obj, "boundary_behavior_id",
                           &out_candidate->boundary_behavior_id) ||
        !read_optional_u32(obj, "link_id", &out_candidate->link_id) ||
        !read_optional_u32(obj, "operation_id",
                           &out_candidate->operation_id) ||
        !read_optional_u32(obj, "from_io_id", &out_candidate->from_io_id) ||
        !read_optional_u32(obj, "to_io_id", &out_candidate->to_io_id) ||
        !read_optional_u32(obj, "source_parameter_id",
                           &out_candidate->source_parameter_id) ||
        !read_optional_u32(obj, "value_parameter_id",
                           &out_candidate->value_parameter_id) ||
        !read_optional_u32(obj, "dataarray_id",
                           &out_candidate->dataarray_id) ||
        !read_optional_guid(obj, "column_type_guid",
                            &out_candidate->column_type_guid) ||
        !read_optional_guid(obj, "bb_guid", &out_candidate->bb_guid) ||
        !read_optional_double(obj, "confidence",
                              &out_candidate->confidence)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *delay = yyjson_obj_get(obj, "delay");
    if (delay != NULL) {
        if (!yyjson_is_uint(delay) || yyjson_get_uint(delay) > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid probe_selector_analysis.candidates.delay");
        }
        out_candidate->has_delay = true;
        out_candidate->delay = (uint32_t)yyjson_get_uint(delay);
    }
    const char *proto_name = NULL;
    const char *rejection_code = NULL;
    if (!read_optional_string(obj, "proto_name", &proto_name) ||
        !read_optional_string(obj, "rejection_code", &rejection_code)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    if (proto_name != NULL) {
        snprintf(out_candidate->proto_name,
                 sizeof(out_candidate->proto_name), "%s", proto_name);
    }
    if (rejection_code != NULL) {
        snprintf(out_candidate->rejection_code,
                 sizeof(out_candidate->rejection_code), "%s",
                 rejection_code);
    }
    if (!parse_probe_candidate_role_value(yyjson_obj_get(obj, "role"),
                                          &out_candidate->role)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return NMO_OK;
}

static nmo_status_t parse_probe_analysis(
    yyjson_val *obj,
    nmo_probe_selector_result_t *out_analysis)
{
    if (obj == NULL) {
        return NMO_OK;
    }
    if (!yyjson_is_obj(obj)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid probe_selector_analysis");
    }
    static const char *const allowed[] = {
        "mode", "status", "rejection_code", "message",
        "selected_node_id", "selected_link_id", "selected_operation_id",
        "from_io_id", "to_io_id", "delay", "safe_insertion", "candidates",
    };
    NMO_RETURN_IF_ERROR(reject_unknown_fields(
        obj, "probe_selector_analysis", allowed,
        sizeof(allowed) / sizeof(allowed[0])));
    if (!parse_probe_selector_mode_value(yyjson_obj_get(obj, "mode"),
                                         &out_analysis->mode) ||
        !parse_probe_selector_status_value(yyjson_obj_get(obj, "status"),
                                           &out_analysis->status) ||
        !read_optional_u32(obj, "selected_node_id",
                           &out_analysis->selected_node_id) ||
        !read_optional_u32(obj, "selected_link_id",
                           &out_analysis->selected_link_id) ||
        !read_optional_u32(obj, "selected_operation_id",
                           &out_analysis->selected_operation_id) ||
        !read_optional_u32(obj, "from_io_id", &out_analysis->from_io_id) ||
        !read_optional_u32(obj, "to_io_id", &out_analysis->to_io_id)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    const char *rejection_code = NULL;
    const char *message = NULL;
    if (!read_optional_string(obj, "rejection_code", &rejection_code) ||
        !read_optional_string(obj, "message", &message)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    if (rejection_code != NULL) {
        snprintf(out_analysis->rejection_code,
                 sizeof(out_analysis->rejection_code), "%s",
                 rejection_code);
    }
    if (message != NULL) {
        snprintf(out_analysis->message, sizeof(out_analysis->message), "%s",
                 message);
    }
    yyjson_val *delay = yyjson_obj_get(obj, "delay");
    if (delay != NULL) {
        if (!yyjson_is_uint(delay) || yyjson_get_uint(delay) > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid probe_selector_analysis.delay");
        }
        out_analysis->has_delay = true;
        out_analysis->delay = (uint32_t)yyjson_get_uint(delay);
    }
    NMO_RETURN_IF_ERROR(parse_probe_safe_insertion(
        yyjson_obj_get(obj, "safe_insertion"),
        &out_analysis->safe_insertion));

    yyjson_val *candidates = yyjson_obj_get(obj, "candidates");
    if (candidates == NULL || !yyjson_is_arr(candidates)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid probe_selector_analysis.candidates");
    }
    yyjson_val *candidate_obj = NULL;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(candidates, &iter);
    while ((candidate_obj = yyjson_arr_iter_next(&iter)) != NULL) {
        nmo_probe_selector_candidate_t candidate;
        NMO_RETURN_IF_ERROR(parse_probe_candidate(candidate_obj, &candidate));
        NMO_RETURN_IF_ERROR(nmo_probe_selector_result_add_candidate(
            out_analysis, &candidate));
    }
    if (out_analysis->safe_insertion.selected &&
        out_analysis->safe_insertion.selected_node_id != 0u &&
        out_analysis->selected_node_id != 0u &&
        out_analysis->safe_insertion.selected_node_id !=
            out_analysis->selected_node_id) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "probe_selector_analysis safe_insertion conflicts with selected_node_id");
    }
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_json_read(
    const char *json,
    size_t json_len,
    nmo_edit_plan_t **out_plan)
{
    if (json == NULL || out_plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_plan = NULL;

    yyjson_doc *doc = yyjson_read(json, json_len, 0);
    if (doc == NULL) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan root must be an object");
    }
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *ops = yyjson_obj_get(root, "operations");
    if (version == NULL || !yyjson_is_uint(version) ||
        yyjson_get_uint(version) != 2u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Current edit plan version 2 is required");
    }
    if (ops == NULL || !yyjson_is_arr(ops) ||
        yyjson_arr_size(ops) == 0u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan operations must be a non-empty array");
    }
    static const char *const root_allowed[] = {
        "version", "operations", "probe_selector_analysis",
    };
    nmo_status_t st = reject_unknown_fields(
        root, "edit plan root", root_allowed,
        sizeof(root_allowed) / sizeof(root_allowed[0]));
    if (st != NMO_OK) {
        yyjson_doc_free(doc);
        return st;
    }

    nmo_edit_plan_t *plan = NULL;
    st = nmo_edit_plan_create(&plan);
    if (st == NMO_OK) {
        st = parse_operations_array(ops, plan);
    }
    yyjson_val *analysis_val = yyjson_obj_get(root, "probe_selector_analysis");
    if (st == NMO_OK && analysis_val != NULL) {
        nmo_probe_selector_result_t analysis;
        nmo_probe_selector_result_init(&analysis);
        st = parse_probe_analysis(analysis_val, &analysis);
        if (st == NMO_OK) {
            st = nmo_edit_plan_set_probe_selector_analysis(plan, &analysis);
        }
        nmo_probe_analysis_dispose(&analysis);
    }
    yyjson_doc_free(doc);
    if (st != NMO_OK) {
        nmo_edit_plan_destroy(plan);
        return st;
    }
    *out_plan = plan;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_manifest_json_read(
    const char *json,
    size_t json_len,
    nmo_edit_plan_manifest_t *out_manifest)
{
    if (json == NULL || out_manifest == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(out_manifest, 0, sizeof(*out_manifest));

    yyjson_doc *doc = yyjson_read(json, json_len, 0);
    if (doc == NULL) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest root must be an object");
    }
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *input = yyjson_obj_get(root, "input");
    yyjson_val *output = yyjson_obj_get(root, "output");
    yyjson_val *ops = yyjson_obj_get(root, "operations");
    if (version == NULL || !yyjson_is_uint(version) ||
        yyjson_get_uint(version) != 2u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Current edit plan manifest version 2 is required");
    }
    if (input == NULL || !yyjson_is_str(input) ||
        yyjson_get_str(input)[0] == '\0') {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest requires input");
    }
    if (output == NULL || !yyjson_is_str(output) ||
        yyjson_get_str(output)[0] == '\0') {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest requires output");
    }
    if (ops == NULL || !yyjson_is_arr(ops) ||
        yyjson_arr_size(ops) == 0u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest operations must be a non-empty array");
    }
    static const char *const root_allowed[] = {
        "version", "input", "output", "operations",
        "probe_selector_analysis",
    };
    nmo_status_t st = reject_unknown_fields(
        root, "edit plan manifest root", root_allowed,
        sizeof(root_allowed) / sizeof(root_allowed[0]));
    if (st != NMO_OK) {
        yyjson_doc_free(doc);
        return st;
    }

    nmo_edit_plan_t *plan = NULL;
    st = nmo_edit_plan_create(&plan);
    if (st != NMO_OK) {
        yyjson_doc_free(doc);
        return st;
    }

    st = parse_operations_array(ops, plan);
    yyjson_val *analysis_val = yyjson_obj_get(root, "probe_selector_analysis");
    if (st == NMO_OK && analysis_val != NULL) {
        nmo_probe_selector_result_t analysis;
        nmo_probe_selector_result_init(&analysis);
        st = parse_probe_analysis(analysis_val, &analysis);
        if (st == NMO_OK) {
            st = nmo_edit_plan_set_probe_selector_analysis(plan, &analysis);
        }
        nmo_probe_analysis_dispose(&analysis);
    }

    if (st == NMO_OK) {
        out_manifest->input_path = dup_string(yyjson_get_str(input));
        out_manifest->output_path = dup_string(yyjson_get_str(output));
        if (out_manifest->input_path == NULL ||
            out_manifest->output_path == NULL) {
            st = NMO_ERR_NOMEM;
        } else {
            out_manifest->plan = plan;
            plan = NULL;
        }
    }

    nmo_edit_plan_destroy(plan);
    yyjson_doc_free(doc);
    if (st != NMO_OK) {
        nmo_edit_plan_manifest_dispose(out_manifest);
    }
    return st;
}

nmo_status_t nmo_edit_plan_manifest_json_read_file(
    const char *path,
    nmo_edit_plan_manifest_t *out_manifest)
{
    FILE *fp = NULL;
    long size = 0;
    char *json = NULL;
    size_t bytes_read = 0u;
    nmo_status_t st = NMO_OK;

    if (path == NULL || out_manifest == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(out_manifest, 0, sizeof(*out_manifest));

    fp = fopen(path, "rb");
    if (fp == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_OPEN_FILE, NMO_SEVERITY_ERROR,
                         "Failed to open edit plan manifest file: %s", path);
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to seek edit plan manifest file: %s", path);
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to size edit plan manifest file: %s", path);
    }
    rewind(fp);

    json = (char *)malloc((size_t)size + 1u);
    if (json == NULL) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate edit plan manifest buffer");
    }
    bytes_read = fread(json, 1u, (size_t)size, fp);
    fclose(fp);
    if (bytes_read != (size_t)size) {
        free(json);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to read edit plan manifest file: %s", path);
    }
    json[bytes_read] = '\0';

    st = nmo_edit_plan_manifest_json_read(json, bytes_read, out_manifest);
    free(json);
    return st;
}

void nmo_edit_plan_manifest_dispose(nmo_edit_plan_manifest_t *manifest)
{
    if (manifest == NULL) {
        return;
    }
    free(manifest->input_path);
    free(manifest->output_path);
    nmo_edit_plan_destroy(manifest->plan);
    memset(manifest, 0, sizeof(*manifest));
}

void nmo_edit_plan_manifest_json_free(char *json)
{
    free(json);
}
