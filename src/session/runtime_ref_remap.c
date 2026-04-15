#include "session/nmo_runtime_ref_remap.h"
#include "session/nmo_runtime_kernel.h"

#include "format/nmo_id_remap.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "core/nmo_array.h"
#include "runtime_internal.h"

/* ── ID remap lookup ───────────────────────────────────────────── */

static bool runtime_lookup_mapping(
    const nmo_id_remap_t *remap,
    nmo_object_id_t old_id,
    nmo_object_id_t *out_new_id)
{
    if (remap == NULL || out_new_id == NULL || old_id == NMO_OBJECT_ID_NONE) {
        return false;
    }
    return nmo_id_remap_lookup_id(remap, old_id, out_new_id) == NMO_OK;
}

/* ── Ref-field remap callback ──────────────────────────────────── */

typedef struct runtime_ref_remap_ctx {
    const nmo_id_remap_t *remap;
    const nmo_type_descriptor_t *type;
    void *instance;
} runtime_ref_remap_ctx_t;

static bool runtime_remap_ref_field(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    (void)field_ptr;

    runtime_ref_remap_ctx_t *ctx = (runtime_ref_remap_ctx_t *)user_data;
    if (ctx == NULL || field == NULL || ctx->instance == NULL) {
        return true;
    }

    if (!nmo_field_is_ref(field)) {
        return true;
    }

    if (!nmo_field_is_array(field)) {
        if (field->size == sizeof(nmo_object_id_t)) {
            nmo_object_id_t *id_ptr = (nmo_object_id_t *)nmo_field_get_ptr(ctx->instance, field);
            if (id_ptr != NULL && *id_ptr != NMO_OBJECT_ID_NONE) {
                nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
                if (runtime_lookup_mapping(ctx->remap, *id_ptr, &mapped)) {
                    *id_ptr = mapped;
                }
            }
        }
        return true;
    }

    if (field->size == sizeof(nmo_array_t)) {
        nmo_array_t *arr = (nmo_array_t *)nmo_field_get_ptr(ctx->instance, field);
        if (arr == NULL || arr->data == NULL || arr->count == 0 ||
            arr->element_size != sizeof(nmo_object_id_t)) {
            return true;
        }

        nmo_object_id_t *ids = (nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
            if (runtime_lookup_mapping(ctx->remap, ids[i], &mapped)) {
                ids[i] = mapped;
            }
        }
        return true;
    }

    if (field->size == sizeof(nmo_object_id_t *)) {
        nmo_object_id_t **ids_ptr = (nmo_object_id_t **)nmo_field_get_ptr(ctx->instance, field);
        if (ids_ptr == NULL || *ids_ptr == NULL) {
            return true;
        }

        uint32_t count = 0;
        if (nmo_field_resolve_count(ctx->type, field, ctx->instance, &count) != NMO_OK) {
            return true;
        }

        nmo_object_id_t *ids = *ids_ptr;
        for (uint32_t i = 0; i < count; i++) {
            nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
            if (runtime_lookup_mapping(ctx->remap, ids[i], &mapped)) {
                ids[i] = mapped;
            }
        }
    }

    return true;
}

/* ── Base-instance resolution ──────────────────────────────────── */

static const void *runtime_get_base_instance(
    const nmo_type_registry_t *types,
    const nmo_type_descriptor_t *derived_type,
    const void *derived_instance,
    const nmo_type_descriptor_t *current_type,
    const void *current_instance,
    const nmo_type_descriptor_t *base_type)
{
    const nmo_type_field_t *base_field = nmo_type_get_field_by_name(current_type, "base");
    if (base_field != NULL && nmo_guid_equals(base_field->type_guid, base_type->guid)) {
        return nmo_field_get_ptr_const(current_instance, base_field);
    }

    if (derived_type != NULL && derived_type->ext != NULL && derived_type->ext->state_offsets != NULL) {
        uint32_t offset = nmo_type_get_state_offset(types, derived_type, base_type);
        if (offset != (uint32_t)-1) {
            return (const char *)derived_instance + offset;
        }
    }

    return NULL;
}

/* ── Public API ────────────────────────────────────────────────── */

int nmo_runtime_remap_copy_refs(
    const nmo_type_runtime_t *type_rt,
    const nmo_type_descriptor_t *type,
    void *instance,
    const nmo_id_remap_t *remap)
{
    if (type_rt == NULL || type_rt->types == NULL || type == NULL || instance == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (remap == NULL || nmo_id_remap_get_count(remap) == 0) {
        return NMO_OK;
    }

    const nmo_type_descriptor_t *current = type;
    void *current_instance = instance;
    const nmo_type_descriptor_t *derived_type = type;
    const void *derived_instance = instance;

    for (size_t depth = 0; current != NULL && current_instance != NULL && depth < 64; ++depth) {
        runtime_ref_remap_ctx_t remap_ctx = {
            .remap = remap,
            .type = current,
            .instance = current_instance
        };

        int remap_result = nmo_type_foreach_ref_field(
            current,
            current_instance,
            runtime_remap_ref_field,
            &remap_ctx);
        if (remap_result != NMO_OK) {
            return remap_result;
        }

        if (nmo_guid_is_null(current->base_type)) {
            break;
        }

        const nmo_type_descriptor_t *base =
            nmo_type_registry_find_by_guid(type_rt->types, current->base_type);
        if (base == NULL) {
            break;
        }

        void *base_instance = (void *)runtime_get_base_instance(
            type_rt->types, derived_type, derived_instance, current, current_instance, base);
        if (base_instance == NULL) {
            break;
        }

        current = base;
        current_instance = base_instance;
    }

    return NMO_OK;
}

int nmo_runtime_remap_all_refs(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    uint32_t request_flags)
{
    if (repo == NULL || type_rt == NULL || type_rt->types == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t object_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects[i];
        if (obj == NULL || obj->state == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, obj);
        if (type == NULL || type->vtable == NULL || type->vtable->remap_dependencies == NULL) {
            continue;
        }

        int hook_result = type->vtable->remap_dependencies(obj->state, type, repo);
        if (hook_result != NMO_OK && (request_flags & NMO_RUNTIME_REQUEST_STRICT)) {
            return hook_result;
        }
    }

    return NMO_OK;
}
