/**
 * @file nmo_ref_enumerate_type.c
 * @brief Reference enumeration using type system metadata
 *
 * Phase 4.1: Uses type registry + reflection fields (or custom enumerate_refs
 * vtable entries) to enumerate object references without hard-coded classes.
 */

#include "session/nmo_ref_enumerate.h"
#include "format/nmo_object.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <string.h>

typedef struct nmo_ref_field_enum_ctx {
    const nmo_type_descriptor_t *type;
    const void *instance;
    nmo_ref_visitor_fn visitor;
    void *user_data;
} nmo_ref_field_enum_ctx_t;

typedef struct nmo_ref_bridge_ctx {
    nmo_ref_visitor_fn visitor;
    void *user_data;
} nmo_ref_bridge_ctx_t;

static bool nmo_ref_name_has(const char *name, const char *token) {
    return name && token && strstr(name, token) != NULL;
}

static nmo_ref_kind_t nmo_ref_kind_from_field(const nmo_type_field_t *field) {
    if (!field || !field->name) {
        return NMO_REF_UNKNOWN;
    }

    const char *name = field->name;

    if (nmo_ref_name_has(name, "parent")) {
        return NMO_REF_HIERARCHY;
    }
    if (nmo_ref_name_has(name, "mesh")) {
        return NMO_REF_MESH;
    }
    if (nmo_ref_name_has(name, "material")) {
        return NMO_REF_MATERIAL;
    }
    if (nmo_ref_name_has(name, "texture")) {
        return NMO_REF_TEXTURE;
    }
    if (nmo_ref_name_has(name, "owner")) {
        return NMO_REF_OWNER;
    }
    if (nmo_ref_name_has(name, "link")) {
        return NMO_REF_BEHAVIOR_LINK;
    }
    if (nmo_ref_name_has(name, "parameter")) {
        return NMO_REF_PARAMETER;
    }
    if (nmo_ref_name_has(name, "target")) {
        return NMO_REF_TARGET;
    }
    if (nmo_ref_name_has(name, "group")) {
        return NMO_REF_GROUP_MEMBER;
    }
    if (nmo_ref_name_has(name, "scene") || nmo_ref_name_has(name, "level")) {
        return NMO_REF_SCENE;
    }
    if (nmo_ref_name_has(name, "animation") || nmo_ref_name_has(name, "anim")) {
        return NMO_REF_ANIMATION;
    }
    if (nmo_ref_name_has(name, "place")) {
        return NMO_REF_PLACE;
    }
    if (nmo_ref_name_has(name, "bone") || nmo_ref_name_has(name, "body_part")) {
        return NMO_REF_SKIN_BONE;
    }
    if (nmo_ref_name_has(name, "dataarray") || nmo_ref_name_has(name, "data_array")) {
        return NMO_REF_DATA_ARRAY;
    }
    if (nmo_ref_name_has(name, "script")) {
        return NMO_REF_SCRIPT;
    }

    return NMO_REF_UNKNOWN;
}

static bool nmo_ref_get_pointer_array_count(
    const nmo_type_descriptor_t *type,
    const char *field_name,
    const void *instance,
    uint32_t *out_count)
{
    if (!type || !field_name || !instance || !out_count) {
        return false;
    }

    size_t name_len = strlen(field_name);
    size_t base_len = name_len;

    if (name_len > 4 && strcmp(field_name + name_len - 4, "_ids") == 0) {
        base_len = name_len - 4;
    } else if (name_len > 3 && strcmp(field_name + name_len - 3, "_id") == 0) {
        base_len = name_len - 3;
    } else if (name_len > 1 && field_name[name_len - 1] == 's') {
        base_len = name_len - 1;
    }

    if (base_len == 0 || base_len + 6 >= 128) {
        return false;
    }

    char count_name[128];
    memcpy(count_name, field_name, base_len);
    memcpy(count_name + base_len, "_count", 7);

    const nmo_type_field_t *count_field = nmo_type_get_field_by_name(type, count_name);
    if (!count_field && base_len > 0) {
        const char *last_underscore = NULL;
        for (size_t i = 0; i < base_len; ++i) {
            if (field_name[i] == '_') {
                last_underscore = field_name + i;
            }
        }
        if (last_underscore) {
            size_t short_base_len = (size_t)(last_underscore - field_name);
            if (short_base_len > 0 && short_base_len + 6 < sizeof(count_name)) {
                memcpy(count_name, field_name, short_base_len);
                memcpy(count_name + short_base_len, "_count", 7);
                count_field = nmo_type_get_field_by_name(type, count_name);
            }
        }
    }
    if (!count_field) {
        return false;
    }

    *out_count = nmo_field_get_uint32(instance, count_field);
    return true;
}

static bool nmo_ref_field_visitor(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    nmo_ref_field_enum_ctx_t *ctx = (nmo_ref_field_enum_ctx_t *)user_data;
    if (!ctx || !field || !field_ptr) {
        return true;
    }

    nmo_ref_kind_t kind = nmo_ref_kind_from_field(field);

    if (!nmo_field_is_array(field)) {
        nmo_object_id_t id = nmo_field_get_object_id(ctx->instance, field);
        if (id != 0) {
            return ctx->visitor(ctx->user_data, id, kind, field->name, 0);
        }
        return true;
    }

    if (field->size == sizeof(nmo_array_t)) {
        const nmo_array_t *arr = (const nmo_array_t *)field_ptr;
        if (!arr->data || arr->count == 0) {
            return true;
        }
        if (arr->element_size != sizeof(nmo_object_id_t)) {
            return true;
        }

        const nmo_object_id_t *ids = (const nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; ++i) {
            if (ids[i] == 0) {
                continue;
            }
            if (!ctx->visitor(ctx->user_data, ids[i], kind, field->name, (uint32_t)i)) {
                return false;
            }
        }
        return true;
    }

    if (field->size == sizeof(nmo_object_id_t *)) {
        const nmo_object_id_t *ids = *(const nmo_object_id_t *const *)field_ptr;
        if (!ids) {
            return true;
        }

        uint32_t count = 0;
        if (!nmo_ref_get_pointer_array_count(ctx->type, field->name, ctx->instance, &count)) {
            return true;
        }

        for (uint32_t i = 0; i < count; ++i) {
            if (ids[i] == 0) {
                continue;
            }
            if (!ctx->visitor(ctx->user_data, ids[i], kind, field->name, i)) {
                return false;
            }
        }
    }

    return true;
}

static bool nmo_ref_bridge_visitor(
    void *user_data,
    uint32_t target_id,
    uint32_t ref_kind,
    const char *field_name,
    uint32_t index)
{
    nmo_ref_bridge_ctx_t *ctx = (nmo_ref_bridge_ctx_t *)user_data;
    if (!ctx || !ctx->visitor) {
        return false;
    }

    return ctx->visitor(ctx->user_data, target_id, (nmo_ref_kind_t)ref_kind, field_name, index);
}

static nmo_status_t nmo_ref_enumerate_fields(
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    nmo_ref_field_enum_ctx_t ctx = {
        .type = type,
        .instance = instance,
        .visitor = visitor,
        .user_data = user_data
    };

    return nmo_type_foreach_ref_field(type, instance, nmo_ref_field_visitor, &ctx);
}

static const void *nmo_ref_get_base_instance(
    const nmo_type_registry_t *types,
    const nmo_type_descriptor_t *derived_type,
    const void *derived_instance,
    const nmo_type_descriptor_t *current_type,
    const void *current_instance,
    const nmo_type_descriptor_t *base_type)
{
    const nmo_type_field_t *base_field = nmo_type_get_field_by_name(current_type, "base");
    if (base_field && nmo_guid_equals(base_field->type_guid, base_type->guid)) {
        return nmo_field_get_ptr_const(current_instance, base_field);
    }

    if (derived_type && derived_type->ext && derived_type->ext->state_offsets) {
        uint32_t offset = nmo_type_get_state_offset(types, derived_type, base_type);
        if (offset != (uint32_t)-1) {
            return (const char *)derived_instance + offset;
        }
    }

    return NULL;
}

static nmo_status_t nmo_ref_enumerate_type_chain(
    const nmo_type_registry_t *types,
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_type_descriptor_t *current = type;
    const void *current_instance = instance;
    const nmo_type_descriptor_t *derived_type = type;
    const void *derived_instance = instance;

    for (size_t depth = 0; current && current_instance && depth < 64; ++depth) {
        nmo_status_t status = nmo_ref_enumerate_fields(current, current_instance, visitor, user_data);
        if (status != NMO_OK) {
            return status;
        }

        if (nmo_guid_is_null(current->base_type)) {
            break;
        }

        const nmo_type_descriptor_t *base =
            nmo_type_registry_find_by_guid(types, current->base_type);
        if (!base) {
            break;
        }

        const void *base_instance = nmo_ref_get_base_instance(
            types, derived_type, derived_instance, current, current_instance, base);
        if (!base_instance) {
            break;
        }

        current = base;
        current_instance = base_instance;
    }

    NMO_RETURN_OK();
}

NMO_API nmo_status_t nmo_ref_enumerate_object(
    const nmo_type_registry_t *types,
    nmo_object_t *obj,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    NMO_ENSURE(types != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");
    NMO_ENSURE(obj != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL object");
    NMO_ENSURE(visitor != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL visitor");

    const void *state = nmo_object_get_state(obj);
    if (!state) {
        NMO_RETURN_OK();
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id(types, class_id);
    if (!type) {
        NMO_RETURN_OK();
    }

    if (type->vtable && type->vtable->enumerate_refs) {
        nmo_ref_bridge_ctx_t bridge = {
            .visitor = visitor,
            .user_data = user_data
        };

        return type->vtable->enumerate_refs(state, type, nmo_ref_bridge_visitor, &bridge);
    }

    return nmo_ref_enumerate_type_chain(types, type, state, visitor, user_data);
}
