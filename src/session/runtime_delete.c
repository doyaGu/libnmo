#include "session/nmo_runtime_kernel.h"

#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_bit_array.h"
#include "object/nmo_ref_graph.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_object_guids.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "core/nmo_logger.h"
#include "../runtime/runtime_internal.h"
#include <string.h>

/* 鈹€鈹€ ID set (private) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

typedef struct runtime_id_set {
    nmo_arena_array_t ids;  /**< Ordered list of member IDs (for iteration) */
    nmo_bit_array_t bits;   /**< Bit array for O(1) membership test */
} runtime_id_set_t;

#define ID_SET_COUNT(s)   ((s)->ids.count)
#define ID_SET_AT(s, i)   (((nmo_object_id_t *)(s)->ids.data)[(i)])
#define ID_SET_DATA(s)    ((nmo_object_id_t *)(s)->ids.data)

static bool runtime_id_set_contains(const runtime_id_set_t *set, nmo_object_id_t id)
{
    if (set == NULL || id == NMO_OBJECT_ID_NONE) {
        return false;
    }
    return nmo_bit_array_test(&set->bits, (size_t)id) != 0;
}

static int runtime_id_set_init(
    runtime_id_set_t *set,
    nmo_arena_t *arena,
    size_t initial_capacity)
{
    if (set == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(set, 0, sizeof(*set));

    if (initial_capacity == 0) {
        initial_capacity = 1;
    }

    nmo_status_t arr_st = nmo_arena_array_init(
        &set->ids, sizeof(nmo_object_id_t), initial_capacity, arena);
    if (arr_st != NMO_OK) {
        return arr_st;
    }

    size_t bit_cap = initial_capacity > 1024 ? initial_capacity : 1024;
    nmo_status_t bit_st = nmo_bit_array_init(&set->bits, bit_cap, NULL);
    if (bit_st != NMO_OK) {
        return bit_st;
    }

    return NMO_OK;
}

static int runtime_id_set_add(runtime_id_set_t *set, nmo_object_id_t id)
{
    if (set == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (id == NMO_OBJECT_ID_NONE || runtime_id_set_contains(set, id)) {
        return NMO_OK;
    }

    nmo_status_t st = nmo_arena_array_append(&set->ids, &id);
    if (st != NMO_OK) {
        return st;
    }

    nmo_status_t bit_st = nmo_bit_array_set(&set->bits, (size_t)id);
    if (bit_st != NMO_OK) {
        return bit_st;
    }
    return NMO_OK;
}

/* 鈹€鈹€ Delete-set collection 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

static int runtime_collect_delete_set(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    const nmo_runtime_request_t *request,
    runtime_id_set_t *out_set)
{
    if (repo == NULL || arena == NULL || request == NULL || out_set == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int init_result = runtime_id_set_init(out_set, arena, request->payload.destroy.count);
    if (init_result != NMO_OK) {
        return init_result;
    }

    const nmo_object_id_t *ids = request->payload.destroy.ids;
    size_t count = request->payload.destroy.count;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, ids[i]);
        if (obj == NULL) {
            if (request->flags & NMO_RUNTIME_REQUEST_STRICT) {
                return NMO_ERR_NOT_FOUND;
            }
            continue;
        }

        int add_result = runtime_id_set_add(out_set, obj->id);
        if (add_result != NMO_OK) {
            return add_result;
        }
    }

    if ((request->flags & NMO_RUNTIME_REQUEST_CASCADE) == 0 ||
        type_rt == NULL || type_rt->types == NULL) {
        return NMO_OK;
    }

    /* Worklist-based cascade using reverse reference index.
     * Build the ref graph once (O(E)), then for each deleted ID,
     * find all incoming references (objects that depend on it) and
     * add them to the delete set. Total work: O(E) vs O(rounds * N * E). */
    nmo_ref_graph_t *cascade_graph = nmo_ref_graph_create(repo, type_rt->types, arena);
    if (cascade_graph == NULL) {
        return NMO_ERR_NOMEM;
    }

    /* Worklist: index into the id set's array. New IDs appended
     * by runtime_id_set_add are automatically reached. */
    size_t worklist_head = 0;

    while (worklist_head < ID_SET_COUNT(out_set)) {
        nmo_object_id_t target_id = ID_SET_AT(out_set, worklist_head++);

        nmo_ref_edge_t *in_edges = NULL;
        size_t in_count = 0;
        nmo_status_t edge_result = nmo_ref_graph_get_object_edges(
            cascade_graph, target_id, NMO_REF_DIR_INCOMING,
            &in_edges, &in_count);
        if (edge_result != NMO_OK) {
            nmo_ref_graph_destroy(cascade_graph);
            return edge_result;
        }

        for (size_t e = 0; e < in_count; e++) {
            nmo_object_id_t referrer_id = in_edges[e].from;
            if (referrer_id == NMO_OBJECT_ID_NONE ||
                runtime_id_set_contains(out_set, referrer_id)) {
                continue;
            }

            int add_result = runtime_id_set_add(out_set, referrer_id);
            if (add_result != NMO_OK) {
                nmo_ref_graph_destroy(cascade_graph);
                return add_result;
            }
        }
    }

    nmo_ref_graph_destroy(cascade_graph);

    return NMO_OK;
}

/* 鈹€鈹€ Safe-detach pre-validation 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

/**
 * @brief Validate that safe-detach supports every surviving referrer.
 *
 * For each surviving object that references a delete-set member, verify its
 * type is registered and exposes the existing dependency hook contract.
 *
 * @return NMO_OK if all referencing objects support remap, error otherwise
 */
static int runtime_validate_safe_detach(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_ref_graph_t *graph,
    const runtime_id_set_t *delete_set,
    uint32_t request_flags,
    nmo_logger_t *logger)
{
    if (graph == NULL || type_rt == NULL || type_rt->types == NULL) {
        return NMO_OK;
    }

    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    nmo_ref_graph_get_edges(graph, &edges, &edge_count);

    int result = NMO_OK;
    for (size_t i = 0; i < edge_count; i++) {
        /* Only care about edges TO a deleted object FROM a surviving object */
        if (!runtime_id_set_contains(delete_set, edges[i].to)) {
            continue;
        }
        if (runtime_id_set_contains(delete_set, edges[i].from)) {
            continue;
        }

        nmo_object_t *referrer = nmo_object_repository_find_by_id(repo, edges[i].from);
        if (referrer == NULL || referrer->state == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, referrer);
        if (type != NULL && type->vtable != NULL &&
            type->vtable->remap_dependencies != NULL) {
            continue;
        }

        /* Surviving object references a deleted object but cannot remap */
        if (request_flags & NMO_RUNTIME_REQUEST_STRICT) {
            result = NMO_ERR_VALIDATION_FAILED;
            break;
        }
        if (logger != NULL) {
            nmo_log(logger, NMO_LOG_WARN,
                    "Object %u references deleted object %u but its type lacks "
                    "remap_dependencies; safe detach is not supported",
                    edges[i].from, edges[i].to);
        }
    }

    return result;
}

static nmo_status_t runtime_remove_deleted_ids_from_array(
    nmo_array_t *array,
    nmo_array_t *parallel_chunks,
    const runtime_id_set_t *delete_set)
{
    if (array == NULL || delete_set == NULL ||
        (array->count > 0u && array->data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (parallel_chunks != NULL && parallel_chunks->count != 0u &&
        (parallel_chunks->count != array->count ||
         parallel_chunks->data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, array);
    for (size_t i = array->count; i > 0u; --i) {
        size_t index = i - 1u;
        if (!runtime_id_set_contains(delete_set, ids[index])) {
            continue;
        }
        if (parallel_chunks != NULL && parallel_chunks->count != 0u) {
            NMO_RETURN_IF_ERROR(nmo_array_remove(parallel_chunks, index, NULL));
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(array, index, NULL));
    }
    return NMO_OK;
}

static nmo_status_t runtime_remove_deleted_behavior_refs(
    nmo_array_t *array,
    const runtime_id_set_t *delete_set)
{
    if (array == NULL || delete_set == NULL ||
        array->element_size != sizeof(nmo_behavior_ref_t) ||
        (array->count > 0u && array->data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(nmo_behavior_ref_t, array);
    for (size_t i = array->count; i > 0u; --i) {
        const size_t index = i - 1u;
        const nmo_object_id_t id = nmo_behavior_ref_runtime_id(&refs[index]);
        if (id == NMO_OBJECT_ID_NONE ||
            !runtime_id_set_contains(delete_set, id)) {
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(array, index, NULL));
    }
    return NMO_OK;
}

static bool runtime_delete_is_atomic_ref_field(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *field)
{
    if (type == NULL || field == NULL || field->name == NULL) return false;
    return strcmp(field->name, "control_point_ids") == 0 ||
           (nmo_guid_equals(type->guid, CKPGUID_KEYEDANIMATION) &&
            strcmp(field->name, "animation_ids") == 0);
}

typedef struct runtime_delete_ref_ctx {
    const runtime_id_set_t *delete_set;
    const nmo_type_descriptor_t *type;
    void *instance;
    bool validate_only;
    nmo_status_t status;
} runtime_delete_ref_ctx_t;

static bool runtime_delete_ref_field(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    (void)field_ptr;
    runtime_delete_ref_ctx_t *ctx = (runtime_delete_ref_ctx_t *)user_data;
    if (ctx == NULL || field == NULL || !nmo_field_is_ref(field) ||
        runtime_delete_is_atomic_ref_field(ctx->type, field)) {
        return true;
    }

    if (!nmo_field_is_array(field) &&
        field->size == sizeof(nmo_ref_t)) {
        nmo_ref_t *ref = (nmo_ref_t *)nmo_field_get_ptr(
            ctx->instance, field);
        if (!ctx->validate_only && ref != NULL &&
            runtime_id_set_contains(
                ctx->delete_set, nmo_ref_runtime_id(ref))) {
            *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        }
        return true;
    }

    if (!nmo_field_is_array(field) &&
        field->size == sizeof(nmo_object_id_t)) {
        nmo_object_id_t *id = (nmo_object_id_t *)nmo_field_get_ptr(
            ctx->instance, field);
        if (!ctx->validate_only && id != NULL &&
            runtime_id_set_contains(ctx->delete_set, *id)) {
            *id = NMO_OBJECT_ID_NONE;
        }
        return true;
    }

    if (field->size == sizeof(nmo_array_t)) {
        nmo_array_t *array = (nmo_array_t *)nmo_field_get_ptr(
            ctx->instance, field);
        if (array == NULL) {
            return true;
        }
        if (nmo_field_uses_ref_records(field)) {
            if (array->element_size != sizeof(nmo_ref_t)) return true;
            if (array->count > 0u && array->data == NULL) {
                ctx->status = NMO_ERR_VALIDATION_FAILED;
                return false;
            }
            if (!ctx->validate_only) {
                for (size_t i = array->count; i > 0u; --i) {
                    nmo_ref_t *refs = NMO_ARRAY_DATA(nmo_ref_t, array);
                    if (runtime_id_set_contains(
                            ctx->delete_set,
                            nmo_ref_runtime_id(&refs[i - 1u]))) {
                        ctx->status = nmo_array_remove(array, i - 1u, NULL);
                        if (ctx->status != NMO_OK) return false;
                    }
                }
            }
            return true;
        }
        if (array->element_size != sizeof(nmo_object_id_t)) return true;
        if (array->count > 0u && array->data == NULL) {
            ctx->status = NMO_ERR_VALIDATION_FAILED;
            return false;
        }
        if (!ctx->validate_only) {
            ctx->status = runtime_remove_deleted_ids_from_array(
                array, NULL, ctx->delete_set);
            if (ctx->status != NMO_OK) return false;
        }
        return true;
    }

    if (field->size == sizeof(void *)) {
        if (nmo_field_uses_ref_records(field)) {
            nmo_ref_t **refs = (nmo_ref_t **)nmo_field_get_ptr(
                ctx->instance, field);
            uint32_t count = 0;
            if (refs == NULL || nmo_field_resolve_count(
                    ctx->type, field, ctx->instance, &count) != NMO_OK ||
                (count > 0u && *refs == NULL)) {
                ctx->status = NMO_ERR_VALIDATION_FAILED;
                return false;
            }
            const nmo_type_field_t *count_field = field->count_field_name
                ? nmo_type_get_field_by_name(ctx->type, field->count_field_name)
                : NULL;
            uint32_t *count_ptr = count_field &&
                count_field->size == sizeof(uint32_t)
                ? (uint32_t *)nmo_field_get_ptr(ctx->instance, count_field)
                : NULL;
            if (count_ptr == NULL) {
                ctx->status = NMO_ERR_VALIDATION_FAILED;
                return false;
            }
            if (!ctx->validate_only) {
                uint32_t kept = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    if (!runtime_id_set_contains(
                            ctx->delete_set,
                            nmo_ref_runtime_id(&(*refs)[i]))) {
                        (*refs)[kept++] = (*refs)[i];
                    }
                }
                *count_ptr = kept;
                if (kept == 0u) *refs = NULL;
            }
            return true;
        }
        nmo_object_id_t **ids = (nmo_object_id_t **)nmo_field_get_ptr(
            ctx->instance, field);
        uint32_t count = 0;
        if (ids == NULL || nmo_field_resolve_count(
                ctx->type, field, ctx->instance, &count) != NMO_OK ||
            (count > 0u && *ids == NULL)) {
            ctx->status = NMO_ERR_VALIDATION_FAILED;
            return false;
        }
        const nmo_type_field_t *count_field = field->count_field_name
            ? nmo_type_get_field_by_name(ctx->type, field->count_field_name)
            : NULL;
        uint32_t *count_ptr = count_field &&
            count_field->size == sizeof(uint32_t)
            ? (uint32_t *)nmo_field_get_ptr(ctx->instance, count_field)
            : NULL;
        if (count_ptr == NULL) {
            ctx->status = NMO_ERR_VALIDATION_FAILED;
            return false;
        }
        if (!ctx->validate_only) {
            uint32_t kept = 0;
            for (uint32_t i = 0; i < count; ++i) {
                if (!runtime_id_set_contains(ctx->delete_set, (*ids)[i])) {
                    (*ids)[kept++] = (*ids)[i];
                }
            }
            *count_ptr = kept;
            if (kept == 0u) *ids = NULL;
        }
    }
    return true;
}

static nmo_status_t runtime_delete_visit_ref_fields(
    const nmo_type_runtime_t *type_rt,
    nmo_object_t *obj,
    const runtime_id_set_t *delete_set,
    bool validate_only)
{
    const nmo_type_descriptor_t *derived =
        runtime_find_type_for_object(type_rt, obj);
    if (derived == NULL) return NMO_OK;
    const nmo_type_descriptor_ext_t *layout = derived->ext;
    const bool has_layout = layout != NULL && layout->hierarchy != NULL &&
                            layout->hierarchy_depth > 0;
    const size_t level_count = has_layout ? layout->hierarchy_depth : 1u;
    for (size_t level = level_count; level > 0; --level) {
        const size_t index = level - 1u;
        const nmo_type_descriptor_t *current =
            has_layout ? layout->hierarchy[index] : derived;
        const uint32_t offset = has_layout && layout->state_offsets != NULL
            ? layout->state_offsets[index]
            : 0u;
        void *current_instance = (uint8_t *)obj->state + offset;
        if (current == NULL) continue;
        runtime_delete_ref_ctx_t ctx = {
            .delete_set = delete_set,
            .type = current,
            .instance = current_instance,
            .validate_only = validate_only,
            .status = NMO_OK,
        };
        nmo_status_t status = nmo_type_foreach_ref_field(
            current, current_instance, runtime_delete_ref_field, &ctx);
        if (status != NMO_OK) return status;
        if (ctx.status != NMO_OK) return ctx.status;
    }
    return NMO_OK;
}

static nmo_status_t runtime_delete_validate_atomic_refs(
    const nmo_type_runtime_t *type_rt,
    nmo_object_t *obj)
{
    nmo_beobject_state_t *beobject = (nmo_beobject_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_BEOBJECT);
    if (beobject != NULL &&
        ((beobject->attributes.element_size != 0 &&
          beobject->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
         (beobject->attributes.count > 0u &&
          beobject->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
         (beobject->attributes.count > 0u &&
          beobject->attributes.data == NULL) ||
         (beobject->legacy_attributes.element_size != 0 &&
          beobject->legacy_attributes.element_size !=
              sizeof(nmo_beobject_legacy_attribute_t)) ||
         (beobject->legacy_attributes.count > 0u &&
          (beobject->legacy_attributes.data == NULL ||
           beobject->legacy_attributes.element_size !=
               sizeof(nmo_beobject_legacy_attribute_t))))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_grid_state_t *grid = (nmo_grid_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_GRID);
    if (grid != NULL && grid->layers.count > 0u && grid->layers.data == NULL) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_patchmesh_state_t *patchmesh = (nmo_patchmesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_PATCHMESH);
    if (patchmesh != NULL) {
        if ((patchmesh->patch_count > 0u && patchmesh->patches == NULL) ||
            (patchmesh->channel_count > 0u && patchmesh->channels == NULL) ||
            (patchmesh->legacy_material_count > 0u &&
             patchmesh->legacy_materials == NULL)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }

    nmo_mesh_state_t *mesh = (nmo_mesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_MESH);
    if (mesh != NULL &&
        ((mesh->material_group_count > 0u && mesh->material_groups == NULL) ||
         (mesh->material_channel_count > 0u &&
          mesh->material_channels == NULL) ||
         (mesh->face_count > 0u && mesh->faces == NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_keyedanimation_state_t *keyed = (nmo_keyedanimation_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_KEYEDANIMATION);
    if (keyed != NULL &&
        ((keyed->animation_count > 0u && keyed->animation_ids == NULL) ||
         (keyed->subanim_count > 0u && keyed->subanims == NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_character_state_t *character = (nmo_character_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_CHARACTER);
    if (character != NULL &&
        ((character->body_parts.element_size != 0u &&
          character->body_parts.element_size != sizeof(nmo_character_part_t)) ||
         (character->body_parts.count > 0u &&
          (character->body_parts.data == NULL ||
           character->body_parts.element_size !=
               sizeof(nmo_character_part_t))))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_curve_state_t *curve = (nmo_curve_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_CURVE);
    if (curve != NULL &&
        ((curve->control_point_count > 0u && curve->control_point_ids == NULL) ||
         (curve->sub_point_count > 0u && curve->sub_points == NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_place_state_t *place = (nmo_place_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_PLACE);
    if (place != NULL &&
        (place->portals.element_size != sizeof(nmo_place_portal_entry_t) ||
         (place->portals.count > 0u && place->portals.data == NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_3dentity_state_t *entity3d = (nmo_3dentity_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_3DENTITY);
    if (entity3d != NULL && entity3d->skin != NULL &&
        entity3d->skin->bone_count > 0u &&
        entity3d->skin->bones == NULL) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_dataarray_state_t *dataarray = (nmo_dataarray_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_DATAARRAY);
    if (dataarray != NULL) {
        if ((dataarray->column_count > 0u &&
             dataarray->column_formats == NULL) ||
            (dataarray->row_count > 0u && dataarray->rows == NULL)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        for (uint32_t column = 0u;
             column < dataarray->column_count;
             ++column) {
            switch (dataarray->column_formats[column].type) {
            case CKARRAYTYPE_INT:
            case CKARRAYTYPE_FLOAT:
            case CKARRAYTYPE_STRING:
            case CKARRAYTYPE_OBJECT:
            case CKARRAYTYPE_PARAMETER:
                break;
            default:
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        for (uint32_t row = 0u; row < dataarray->row_count; ++row) {
            if (dataarray->rows[row].column_count !=
                    dataarray->column_count ||
                (dataarray->rows[row].column_count > 0u &&
                 dataarray->rows[row].cells == NULL)) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
    }

    nmo_scene_state_t *scene = (nmo_scene_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_SCENE);
    if (scene != NULL &&
        (scene->object_descs.element_size !=
             sizeof(nmo_scene_object_desc_t) ||
         (scene->object_descs.count > 0u &&
          scene->object_descs.data == NULL))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    return NMO_OK;
}

static nmo_status_t runtime_delete_detach_atomic_refs(
    const nmo_type_runtime_t *type_rt,
    nmo_object_t *obj,
    const runtime_id_set_t *delete_set)
{
    nmo_beobject_state_t *beobject = (nmo_beobject_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_BEOBJECT);
    if (beobject != NULL) {
        nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
            nmo_beobject_attribute_t, &beobject->attributes);
        for (size_t i = beobject->attributes.count; i > 0u; --i) {
            size_t index = i - 1u;
            const nmo_object_id_t id = nmo_ref_runtime_id(
                &attributes[index].parameter);
            if (!runtime_id_set_contains(delete_set, id)) continue;
            NMO_RETURN_IF_ERROR(nmo_array_remove(
                &beobject->attributes, index, NULL));
        }
        for (size_t i = beobject->legacy_attributes.count; i > 0u; --i) {
            nmo_beobject_legacy_attribute_t *legacy = NMO_ARRAY_DATA(
                nmo_beobject_legacy_attribute_t,
                &beobject->legacy_attributes);
            const size_t index = i - 1u;
            if (!runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&legacy[index].parameter))) {
                continue;
            }
            NMO_RETURN_IF_ERROR(nmo_array_remove(
                &beobject->legacy_attributes, index, NULL));
        }
    }

    nmo_grid_state_t *grid = (nmo_grid_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_GRID);
    if (grid != NULL) {
        nmo_grid_layer_t *layers = NMO_ARRAY_DATA(nmo_grid_layer_t, &grid->layers);
        for (size_t i = grid->layers.count; i > 0u; --i) {
            size_t index = i - 1u;
            if (!runtime_id_set_contains(delete_set, layers[index].ref.id)) continue;
            NMO_RETURN_IF_ERROR(nmo_array_remove(&grid->layers, index, NULL));
        }
    }

    nmo_patchmesh_state_t *patchmesh = (nmo_patchmesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_PATCHMESH);
    if (patchmesh != NULL) {
        uint32_t count = patchmesh->patch_count;
        for (uint32_t i = 0u; i < count;) {
            if (!runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&patchmesh->patches[i].material))) {
                ++i;
                continue;
            }
            uint32_t remaining = count - i - 1u;
            if (remaining > 0u) {
                memmove(&patchmesh->patches[i], &patchmesh->patches[i + 1u],
                        (size_t)remaining * sizeof(*patchmesh->patches));
            }
            patchmesh->patch_count = --count;
        }
        for (uint32_t i = 0u; i < patchmesh->channel_count; ++i) {
            if (runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&patchmesh->channels[i].material))) {
                patchmesh->channels[i].material =
                    nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            }
        }
    }

    nmo_mesh_state_t *mesh = (nmo_mesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_MESH);
    if (mesh != NULL) {
        uint32_t count = mesh->material_group_count;
        for (uint32_t i = 0u; i < count;) {
            if (!runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&mesh->material_groups[i].material))) {
                ++i;
                continue;
            }
            uint32_t remaining = count - i - 1u;
            if (remaining > 0u) {
                memmove(&mesh->material_groups[i],
                        &mesh->material_groups[i + 1u],
                        (size_t)remaining * sizeof(*mesh->material_groups));
            }
            mesh->material_group_count = --count;
            for (uint32_t face = 0u; face < mesh->face_count; ++face) {
                uint16_t *index = &mesh->faces[face].material_group_idx;
                if (*index == i) {
                    *index = 0;
                } else if (*index > i) {
                    --*index;
                }
            }
        }
        for (uint32_t i = 0u; i < mesh->material_channel_count; ++i) {
            if (runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&mesh->material_channels[i].material))) {
                mesh->material_channels[i].material =
                    nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            }
        }
    }

    nmo_keyedanimation_state_t *keyed = (nmo_keyedanimation_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_KEYEDANIMATION);
    if (keyed != NULL) {
        uint32_t count = keyed->animation_count;
        for (uint32_t i = 0u; i < count;) {
            if (!runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&keyed->animation_ids[i]))) {
                ++i;
                continue;
            }
            uint32_t remaining = count - i - 1u;
            if (remaining > 0u) {
                memmove(&keyed->animation_ids[i], &keyed->animation_ids[i + 1u],
                        (size_t)remaining * sizeof(*keyed->animation_ids));
            }
            keyed->animation_count = --count;
        }

        count = keyed->subanim_count;
        for (uint32_t i = 0u; i < count;) {
            if (!runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&keyed->subanims[i].ref))) {
                ++i;
                continue;
            }
            if (keyed->subanims[i].chunk != NULL) {
                nmo_chunk_destroy(keyed->subanims[i].chunk);
                keyed->subanims[i].chunk = NULL;
            }
            const uint32_t remaining = count - i - 1u;
            if (remaining > 0u) {
                memmove(&keyed->subanims[i], &keyed->subanims[i + 1u],
                        (size_t)remaining * sizeof(*keyed->subanims));
            }
            keyed->subanim_count = --count;
            keyed->subanims[count].chunk = NULL;
        }
    }

    nmo_character_state_t *character = (nmo_character_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_CHARACTER);
    if (character != NULL) {
        for (size_t i = character->body_parts.count; i > 0u; --i) {
            nmo_character_part_t *parts = NMO_ARRAY_DATA(
                nmo_character_part_t, &character->body_parts);
            size_t index = i - 1u;
            if (!runtime_id_set_contains(
                    delete_set, nmo_ref_runtime_id(&parts[index].ref))) {
                continue;
            }
            NMO_RETURN_IF_ERROR(nmo_array_remove(
                &character->body_parts, index, NULL));
        }
    }

    nmo_curve_state_t *curve = (nmo_curve_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_CURVE);
    if (curve != NULL) {
        uint32_t count = curve->control_point_count;
        for (uint32_t i = 0u; i < count;) {
            if (!runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&curve->control_point_ids[i]))) {
                ++i;
                continue;
            }
            uint32_t remaining = count - i - 1u;
            if (remaining > 0u) {
                memmove(&curve->control_point_ids[i],
                        &curve->control_point_ids[i + 1u],
                        (size_t)remaining * sizeof(*curve->control_point_ids));
            }
            curve->control_point_count = --count;
        }

        count = curve->sub_point_count;
        for (uint32_t i = 0u; i < count;) {
            if (!runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&curve->sub_points[i].ref))) {
                ++i;
                continue;
            }
            if (curve->sub_points[i].chunk != NULL) {
                nmo_chunk_destroy(curve->sub_points[i].chunk);
                curve->sub_points[i].chunk = NULL;
            }
            const uint32_t remaining = count - i - 1u;
            if (remaining > 0u) {
                memmove(&curve->sub_points[i], &curve->sub_points[i + 1u],
                        (size_t)remaining * sizeof(*curve->sub_points));
            }
            curve->sub_point_count = --count;
            curve->sub_points[count].chunk = NULL;
        }
    }

    nmo_place_state_t *place = (nmo_place_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_PLACE);
    if (place != NULL) {
        for (size_t i = place->portals.count; i > 0u; --i) {
            nmo_place_portal_entry_t *entries = NMO_ARRAY_DATA(
                nmo_place_portal_entry_t, &place->portals);
            const size_t index = i - 1u;
            if (!runtime_id_set_contains(
                    delete_set, nmo_ref_runtime_id(&entries[index].place)) &&
                !runtime_id_set_contains(
                    delete_set, nmo_ref_runtime_id(&entries[index].portal))) {
                continue;
            }
            NMO_RETURN_IF_ERROR(nmo_array_remove(
                &place->portals, index, NULL));
        }
    }

    nmo_3dentity_state_t *entity3d = (nmo_3dentity_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_3DENTITY);
    if (entity3d != NULL && entity3d->skin != NULL) {
        nmo_3dentity_skin_t *skin = entity3d->skin;
        for (uint32_t i = 0u; i < skin->bone_count; ++i) {
            if (runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&skin->bones[i].bone))) {
                skin->bones[i].bone =
                    nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            }
        }
    }

    nmo_dataarray_state_t *dataarray = (nmo_dataarray_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_DATAARRAY);
    if (dataarray != NULL) {
        for (uint32_t row = 0u; row < dataarray->row_count; ++row) {
            for (uint32_t column = 0u;
                 column < dataarray->column_count;
                 ++column) {
                nmo_ref_t *ref = NULL;
                if (dataarray->column_formats[column].type ==
                        CKARRAYTYPE_OBJECT) {
                    ref = &dataarray->rows[row].cells[column].object_ref;
                } else if (dataarray->column_formats[column].type ==
                               CKARRAYTYPE_PARAMETER) {
                    ref = &dataarray->rows[row].cells[column].parameter.ref;
                }
                if (ref != NULL && runtime_id_set_contains(
                        delete_set, nmo_ref_runtime_id(ref))) {
                    *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                }
            }
        }
    }

    nmo_scene_state_t *scene = (nmo_scene_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            type_rt->types, obj, CKPGUID_SCENE);
    if (scene != NULL) {
        for (size_t i = scene->object_descs.count; i > 0u; --i) {
            nmo_scene_object_desc_t *descs = NMO_ARRAY_DATA(
                nmo_scene_object_desc_t, &scene->object_descs);
            const size_t index = i - 1u;
            if (runtime_id_set_contains(
                    delete_set,
                    nmo_ref_runtime_id(&descs[index].ref))) {
                NMO_RETURN_IF_ERROR(nmo_array_remove(
                    &scene->object_descs, index, NULL));
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_detach_deleted_references(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    const runtime_id_set_t *delete_set)
{
    if (type_rt == NULL || type_rt->types == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    /* Validate every atomic ID/chunk lane before mutating any object. */
    size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t oi = 0; oi < object_count; ++oi) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, oi);
        if (!obj || !obj->state || runtime_id_set_contains(delete_set, obj->id)) {
            continue;
        }
        NMO_RETURN_IF_ERROR(runtime_delete_validate_atomic_refs(type_rt, obj));
        NMO_RETURN_IF_ERROR(runtime_delete_visit_ref_fields(
            type_rt, obj, delete_set, true));
        if (obj->class_id == NMO_CID_GROUP) {
            nmo_group_state_t *group = (nmo_group_state_t *)obj->state;
            if (group->object_ids.count > 0u && group->object_ids.data == NULL) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (!obj || obj->class_id != NMO_CID_BEHAVIOR || !obj->state ||
            runtime_id_set_contains(delete_set, obj->id)) {
            continue;
        }

        nmo_behavior_state_t *behavior = (nmo_behavior_state_t *)obj->state;
        const nmo_array_t *plain_arrays[] = {
            &behavior->sub_behaviors,
            &behavior->local_parameters,
            &behavior->sub_behavior_links,
            &behavior->operations,
            &behavior->in_parameters,
            &behavior->out_parameters,
            &behavior->inputs,
            &behavior->outputs,
        };
        for (size_t ai = 0u;
             ai < sizeof(plain_arrays) / sizeof(plain_arrays[0]); ++ai) {
            if (plain_arrays[ai]->count > 0u &&
                plain_arrays[ai]->data == NULL) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
    }

    for (size_t oi = 0; oi < object_count; ++oi) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, oi);
        if (!obj || !obj->state || runtime_id_set_contains(delete_set, obj->id)) {
            continue;
        }
        NMO_RETURN_IF_ERROR(runtime_delete_detach_atomic_refs(
            type_rt, obj, delete_set));
        if (obj->class_id == NMO_CID_BEHAVIORLINK) {
            nmo_behaviorlink_state_t *link =
                (nmo_behaviorlink_state_t *)obj->state;
            if (runtime_id_set_contains(
                    delete_set, nmo_behaviorlink_in_io_id(link))) {
                nmo_behaviorlink_set_in_io_id(link, NMO_OBJECT_ID_NONE);
            }
            if (runtime_id_set_contains(
                    delete_set, nmo_behaviorlink_out_io_id(link))) {
                nmo_behaviorlink_set_out_io_id(link, NMO_OBJECT_ID_NONE);
            }
            continue;
        }
        if (!obj || obj->class_id != NMO_CID_BEHAVIOR || !obj->state ||
            runtime_id_set_contains(delete_set, obj->id)) {
            NMO_RETURN_IF_ERROR(runtime_delete_visit_ref_fields(
                type_rt, obj, delete_set, false));
            continue;
        }

        nmo_behavior_state_t *behavior = (nmo_behavior_state_t *)obj->state;
        if (runtime_id_set_contains(
                delete_set, nmo_behavior_owner_id(behavior))) {
            behavior->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        }
        if (runtime_id_set_contains(
                delete_set,
                nmo_behavior_target_parameter_id(behavior))) {
            behavior->target_parameter =
                nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        }

        NMO_RETURN_IF_ERROR(runtime_remove_deleted_behavior_refs(
            &behavior->sub_behaviors, delete_set));
        NMO_RETURN_IF_ERROR(runtime_remove_deleted_behavior_refs(
            &behavior->local_parameters, delete_set));
        NMO_RETURN_IF_ERROR(runtime_remove_deleted_behavior_refs(
            &behavior->operations, delete_set));
        NMO_RETURN_IF_ERROR(runtime_remove_deleted_behavior_refs(
            &behavior->in_parameters, delete_set));
        NMO_RETURN_IF_ERROR(runtime_remove_deleted_behavior_refs(
            &behavior->out_parameters, delete_set));
        NMO_RETURN_IF_ERROR(runtime_remove_deleted_behavior_refs(
            &behavior->inputs, delete_set));
        NMO_RETURN_IF_ERROR(runtime_remove_deleted_behavior_refs(
            &behavior->outputs, delete_set));

        nmo_behavior_ref_t *link_refs = NMO_ARRAY_DATA(
            nmo_behavior_ref_t, &behavior->sub_behavior_links);
        for (size_t i = behavior->sub_behavior_links.count; i > 0u; --i) {
            const size_t index = i - 1u;
            const nmo_object_id_t link_id =
                nmo_behavior_ref_runtime_id(&link_refs[index]);
            if (link_id == NMO_OBJECT_ID_NONE) continue;
            nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
            const nmo_behaviorlink_state_t *link = link_obj && link_obj->state
                ? (const nmo_behaviorlink_state_t *)link_obj->state : NULL;
            if (runtime_id_set_contains(delete_set, link_id) ||
                (link && (runtime_id_set_contains(
                              delete_set, nmo_behaviorlink_in_io_id(link)) ||
                          runtime_id_set_contains(
                              delete_set, nmo_behaviorlink_out_io_id(link))))) {
                NMO_RETURN_IF_ERROR(nmo_array_remove(
                    &behavior->sub_behavior_links, index, NULL));
            }
        }
        NMO_RETURN_IF_ERROR(runtime_delete_visit_ref_fields(
            type_rt, obj, delete_set, false));
    }
    return NMO_OK;
}

/* 鈹€鈹€ Public API 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

nmo_status_t nmo_runtime_preview_delete(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_object_id_t **out_ids,
    size_t *out_count)
{
    if (repo == NULL || arena == NULL || object_ids == NULL || object_count == 0 ||
        out_ids == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_ids = NULL;
    *out_count = 0;

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_DELETE;
    request.flags = flags;
    request.payload.destroy.ids = object_ids;
    request.payload.destroy.count = object_count;

    runtime_id_set_t delete_set;
    memset(&delete_set, 0, sizeof(delete_set));
    nmo_status_t result = runtime_collect_delete_set(repo, type_rt, arena, &request, &delete_set);
    if (result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return result;
    }

    *out_ids = ID_SET_DATA(&delete_set);
    *out_count = ID_SET_COUNT(&delete_set);
    nmo_bit_array_dispose(&delete_set.bits);
    return NMO_OK;
}

nmo_status_t nmo_runtime_execute_delete(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *report)
{
    if (session == NULL || request == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (request->payload.destroy.ids == NULL || request->payload.destroy.count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    const nmo_type_runtime_t *type_rt = (ctx != NULL) ? nmo_context_get_type_runtime(ctx) : NULL;
    nmo_logger_t *logger = (ctx != NULL) ? nmo_context_get_logger(ctx) : NULL;
    if (repo == NULL || arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    runtime_id_set_t delete_set;
    memset(&delete_set, 0, sizeof(delete_set));
    nmo_status_t collect_result = runtime_collect_delete_set(repo, type_rt, arena, request, &delete_set);
    if (collect_result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return collect_result;
    }

    /* Pre-validate safe-detach: ensure all surviving referrers can remap */
    if (request->flags & NMO_RUNTIME_REQUEST_SAFE_DETACH) {
        nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
        nmo_status_t validate_result = runtime_validate_safe_detach(
            repo, type_rt, graph, &delete_set, request->flags, logger);
        if (validate_result != NMO_OK) {
            nmo_bit_array_dispose(&delete_set.bits);
            return validate_result;
        }
    }

    /* Three-phase delete: validate hooks, then detach, then destroy.
     * Phase 1a runs pre_delete hooks without mutating the repository so that
     * all hooks see a fully consistent world.  If any hook fails under STRICT,
     * the operation aborts before any state change.
     * Phase 1b detaches objects from the repository (no destroy yet).
     * Phase 2 runs post_delete hooks and destroys detached objects. */

    /* Phase 1a: validate pre_delete hooks (no mutation) */
    for (size_t i = 0; i < ID_SET_COUNT(&delete_set); i++) {
        nmo_object_id_t object_id = ID_SET_AT(&delete_set, i);
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
        if (obj == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, obj);
        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->pre_delete != NULL &&
            obj->state != NULL) {
            nmo_status_t hook_result = type->vtable->pre_delete(obj->state, type, repo);
            if (hook_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
                nmo_bit_array_dispose(&delete_set.bits);
                return hook_result;
            }
        }
    }

    /* Phase 1b: batch detach from repository (all hooks passed) */
    nmo_object_t **detached_objects = (nmo_object_t **)nmo_arena_alloc(
        arena, ID_SET_COUNT(&delete_set) * sizeof(nmo_object_t *),
        _Alignof(nmo_object_t *));
    if (detached_objects == NULL && ID_SET_COUNT(&delete_set) > 0) {
        nmo_bit_array_dispose(&delete_set.bits);
        return NMO_ERR_NOMEM;
    }
    size_t detached_count = 0;

    if (request->flags & NMO_RUNTIME_REQUEST_SAFE_DETACH) {
        nmo_status_t detach_result =
            runtime_detach_deleted_references(repo, type_rt, &delete_set);
        if (detach_result != NMO_OK) {
            nmo_bit_array_dispose(&delete_set.bits);
            return detach_result;
        }
    }

    for (size_t i = 0; i < ID_SET_COUNT(&delete_set); i++) {
        nmo_object_id_t object_id = ID_SET_AT(&delete_set, i);
        nmo_object_t *detached = NULL;
        nmo_status_t remove_result = nmo_object_repository_take(repo, object_id, &detached);
        /* All IDs were validated in Phase 1a and runtime_collect_delete_set.
         * A take failure here indicates an internal consistency error. */
        if (remove_result != NMO_OK || detached == NULL) {
            if (logger != NULL) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Phase 1b: take(%u) failed unexpectedly (status=%d)",
                        object_id, remove_result);
            }
            continue;
        }
        if (remove_result == NMO_OK && detached != NULL) {
            detached_objects[detached_count++] = detached;
        }
    }

    /* Phase 2: post_delete hooks + destroy (all objects already detached) */
    for (size_t i = 0; i < detached_count; i++) {
        nmo_object_t *detached = detached_objects[i];
        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, detached);

        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->post_delete != NULL &&
            detached->state != NULL) {
            type->vtable->post_delete(detached->state, type, repo);
        }
        nmo_runtime_destroy_object_state(session, detached);
        nmo_object_destroy(detached);

        if (report != NULL) {
            report->deleted_objects++;
            report->affected_objects++;
        }
    }

    nmo_bit_array_dispose(&delete_set.bits);

    if (request->flags & NMO_RUNTIME_REQUEST_SAFE_DETACH) {
        if (type_rt != NULL && type_rt->types != NULL) {
            nmo_status_t detach_result = nmo_runtime_remap_all_refs(repo, type_rt, request->flags);
            if (detach_result != NMO_OK) {
                return detach_result;
            }
        }
    }

    /* Clean up included files whose owners are all deleted.
     * Iterate in reverse so index shifts from remove don't skip entries. */
    if (request->flags & NMO_RUNTIME_REQUEST_CASCADE) {
        uint32_t file_count = 0;
        nmo_included_file_t *files = nmo_session_get_included_files(session, &file_count);
        for (uint32_t fi = file_count; fi > 0; fi--) {
            nmo_included_file_t *f = &files[fi - 1];
            if (f->owner_ids.count == 0) {
                continue; /* no owners recorded 鈥?not managed */
            }
            const nmo_object_id_t *owners =
                (const nmo_object_id_t *)f->owner_ids.data;
            bool any_alive = false;
            for (size_t oi = 0; oi < f->owner_ids.count; oi++) {
                if (nmo_object_repository_find_by_id(repo, owners[oi]) != NULL) {
                    any_alive = true;
                    break;
                }
            }
            if (!any_alive) {
                if (logger != NULL) {
                    nmo_log(logger, NMO_LOG_DEBUG,
                            "Removing orphaned included file %u (%s): all owners deleted",
                            fi - 1, f->name ? f->name : "<unnamed>");
                }
                nmo_session_remove_included_file(session, fi - 1);
                if (report != NULL) {
                    report->affected_objects++;
                }
            }
        }
    }

    return NMO_OK;
}

