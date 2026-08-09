/**
 * @file nmo_ref_enumerate_type.c
 * @brief Reference enumeration using type system metadata
 *
 * Phase 4.1: Uses type registry + reflection fields (or custom enumerate_refs
 * vtable entries) to enumerate object references without hard-coded classes.
 */

#include "object/nmo_ref_enumerate.h"
#include "object/nmo_ref.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_object_guids.h"
#include "format/nmo_object.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"
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
        return NMO_REF_KIND_UNKNOWN;
    }

    const char *name = field->name;

    if (nmo_ref_name_has(name, "parent")) {
        return NMO_REF_KIND_HIERARCHY;
    }
    if (nmo_ref_name_has(name, "mesh")) {
        return NMO_REF_KIND_MESH;
    }
    if (nmo_ref_name_has(name, "material")) {
        return NMO_REF_KIND_MATERIAL;
    }
    if (nmo_ref_name_has(name, "texture")) {
        return NMO_REF_KIND_TEXTURE;
    }
    if (nmo_ref_name_has(name, "owner")) {
        return NMO_REF_KIND_OWNER;
    }
    if (nmo_ref_name_has(name, "link")) {
        return NMO_REF_KIND_BEHAVIOR_LINK;
    }
    if (nmo_ref_name_has(name, "parameter")) {
        return NMO_REF_KIND_PARAMETER;
    }
    if (nmo_ref_name_has(name, "target")) {
        return NMO_REF_KIND_TARGET;
    }
    if (nmo_ref_name_has(name, "group")) {
        return NMO_REF_KIND_GROUP_MEMBER;
    }
    if (nmo_ref_name_has(name, "scene") || nmo_ref_name_has(name, "level")) {
        return NMO_REF_KIND_SCENE;
    }
    if (nmo_ref_name_has(name, "animation") || nmo_ref_name_has(name, "anim")) {
        return NMO_REF_KIND_ANIMATION;
    }
    if (nmo_ref_name_has(name, "place")) {
        return NMO_REF_KIND_PLACE;
    }
    if (nmo_ref_name_has(name, "bone") || nmo_ref_name_has(name, "body_part")) {
        return NMO_REF_KIND_SKIN_BONE;
    }
    if (nmo_ref_name_has(name, "dataarray") || nmo_ref_name_has(name, "data_array")) {
        return NMO_REF_KIND_DATA_ARRAY;
    }
    if (nmo_ref_name_has(name, "script")) {
        return NMO_REF_KIND_SCRIPT;
    }

    return NMO_REF_KIND_UNKNOWN;
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
        nmo_object_id_t id = field->size == sizeof(nmo_ref_t)
            ? nmo_ref_runtime_id((const nmo_ref_t *)field_ptr)
            : nmo_field_get_object_id(ctx->instance, field);
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
        if (nmo_field_uses_ref_records(field)) {
            if (arr->element_size != sizeof(nmo_ref_t)) return true;
            const nmo_ref_t *refs = (const nmo_ref_t *)arr->data;
            for (size_t i = 0; i < arr->count; ++i) {
                const nmo_object_id_t id = nmo_ref_runtime_id(&refs[i]);
                if (id != NMO_OBJECT_ID_NONE && !ctx->visitor(
                        ctx->user_data, id, kind, field->name, (uint32_t)i)) {
                    return false;
                }
            }
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

    if (field->size == sizeof(void *)) {
        if (nmo_field_uses_ref_records(field)) {
            const nmo_ref_t *refs = *(const nmo_ref_t *const *)field_ptr;
            if (!refs) return true;
            uint32_t count = 0;
            if (nmo_field_resolve_count(
                    ctx->type, field, ctx->instance, &count) != NMO_OK) {
                return true;
            }
            for (uint32_t i = 0; i < count; ++i) {
                const nmo_object_id_t id = nmo_ref_runtime_id(&refs[i]);
                if (id != NMO_OBJECT_ID_NONE && !ctx->visitor(
                        ctx->user_data, id, kind, field->name, i)) {
                    return false;
                }
            }
            return true;
        }
        const nmo_object_id_t *ids = *(const nmo_object_id_t *const *)field_ptr;
        if (!ids) {
            return true;
        }

        uint32_t count = 0;
        if (nmo_field_resolve_count(ctx->type, field, ctx->instance, &count) != NMO_OK) {
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

/**
 * Enumerate references inside struct array fields.
 *
 * Struct arrays (NMO_FIELD_REPEATED without NMO_FIELD_REFERENCE) may contain
 * nested object ID fields (e.g. CKMesh material_groups[].material_id).
 * The standard ref-field walker skips these because the array field itself
 * is not tagged REFERENCE.  This function looks up the element type in the
 * registry and, if any of its fields carry NMO_FIELD_REFERENCE, iterates
 * each element and delegates to the normal field enumerator.
 */
static nmo_status_t nmo_ref_enumerate_struct_arrays(
    const nmo_type_registry_t *types,
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    if (!type->fields || type->field_count == 0) {
        NMO_RETURN_OK();
    }

    for (size_t i = 0; i < type->field_count; ++i) {
        const nmo_type_field_t *field = &type->fields[i];

        /* Only repeated (array) fields that are NOT already reference fields */
        if (!(field->flags & NMO_FIELD_REPEATED)) continue;
        if (field->flags & NMO_FIELD_REFERENCE) continue;
        if (nmo_guid_is_null(field->type_guid)) continue;

        /* Look up the element type — must be a struct with fields */
        const nmo_type_descriptor_t *elem_type =
            nmo_type_registry_find_by_guid(types, field->type_guid);
        if (!elem_type) continue;
        if (!(elem_type->category & NMO_TYPE_CATEGORY_STRUCT)) continue;
        if (!elem_type->fields || elem_type->field_count == 0) continue;

        /* Quick check: does the struct have any reference fields? */
        bool has_refs = false;
        for (size_t j = 0; j < elem_type->field_count; ++j) {
            if (elem_type->fields[j].flags & NMO_FIELD_REFERENCE) {
                has_refs = true;
                break;
            }
        }
        if (!has_refs) continue;

        const void *field_ptr = nmo_field_get_ptr_const(instance, field);
        if (!field_ptr) return NMO_ERR_INVALID_STATE;

        const size_t element_size =
            nmo_field_resolve_element_size(field, elem_type);
        if (element_size == 0) return NMO_ERR_VALIDATION_FAILED;

        /* nmo_array_t: inline dynamic array */
        if (field->size == sizeof(nmo_array_t)) {
            const nmo_array_t *arr = (const nmo_array_t *)field_ptr;
            if (((arr->element_size != 0 || arr->count > 0) &&
                 arr->element_size != element_size) ||
                (arr->count > 0 && arr->data == NULL)) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            if (arr->count == 0) continue;

            for (size_t k = 0; k < arr->count; ++k) {
                const void *elem =
                    (const char *)arr->data + k * element_size;
                nmo_status_t st = nmo_ref_enumerate_fields(
                    elem_type, elem, visitor, user_data);
                if (st != NMO_OK) return st;
            }
        }
        /* T* pointer array with metadata-declared count field */
        else if (field->size == sizeof(void *)) {
            const void *ptr = *(const void *const *)field_ptr;

            uint32_t count = 0;
            nmo_status_t count_status = nmo_field_resolve_count(
                type, field, instance, &count);
            if (count_status != NMO_OK) {
                return count_status;
            }
            if (count > 0 && ptr == NULL) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            if (count == 0) continue;

            for (uint32_t k = 0; k < count; ++k) {
                const void *elem =
                    (const char *)ptr + (size_t)k * element_size;
                nmo_status_t st = nmo_ref_enumerate_fields(
                    elem_type, elem, visitor, user_data);
                if (st != NMO_OK) return st;
            }
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_ref_enumerate_3dentity_skin(
    const nmo_3dentity_state_t *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    if (state == NULL || state->skin == NULL) return NMO_OK;
    const nmo_3dentity_skin_t *skin = state->skin;
    if (skin->bone_count > 0 && skin->bones == NULL) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < skin->bone_count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &skin->bones[i].bone);
        if (id != NMO_OBJECT_ID_NONE && !visitor(
                user_data, id, NMO_REF_KIND_SKIN_BONE,
                "skin.bones", i)) {
            break;
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_ref_enumerate_type_chain(
    const nmo_type_registry_t *types,
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_type_descriptor_ext_t *layout = type->ext;
    bool has_layout = layout && layout->hierarchy &&
                      layout->hierarchy_depth > 0;
    size_t level_count = has_layout ? layout->hierarchy_depth : 1u;

    for (size_t level = level_count; level > 0; --level) {
        size_t index = level - 1u;
        const nmo_type_descriptor_t *current =
            has_layout ? layout->hierarchy[index] : type;
        uint32_t state_offset =
            has_layout && layout->state_offsets
                ? layout->state_offsets[index]
                : 0u;
        const void *current_instance =
            (const uint8_t *)instance + state_offset;
        if (!current || !current_instance) {
            continue;
        }
        nmo_status_t status = NMO_OK;
        if (current->vtable && current->vtable->enumerate_refs) {
            nmo_ref_bridge_ctx_t bridge = {
                .visitor = visitor,
                .user_data = user_data
            };
            status = current->vtable->enumerate_refs(
                current_instance, current, nmo_ref_bridge_visitor, &bridge);
        } else {
            status = nmo_ref_enumerate_fields(
                current, current_instance, visitor, user_data);
        }
        if (status != NMO_OK) {
            return status;
        }

        if (!(current->vtable && current->vtable->enumerate_refs)) {
            status = nmo_ref_enumerate_struct_arrays(
                types, current, current_instance, visitor, user_data);
            if (status != NMO_OK) {
                return status;
            }
        }

        if (nmo_guid_equals(current->guid, CKPGUID_3DENTITY)) {
            status = nmo_ref_enumerate_3dentity_skin(
                (const nmo_3dentity_state_t *)current_instance,
                visitor, user_data);
            if (status != NMO_OK) return status;
        }

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

    const nmo_type_descriptor_t *type =
        nmo_type_query_find_for_object(types, obj);
    if (!type) {
        NMO_RETURN_OK();
    }

    return nmo_ref_enumerate_type_chain(types, type, state, visitor, user_data);
}
