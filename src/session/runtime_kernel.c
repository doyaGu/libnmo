#include "session/nmo_runtime_kernel.h"
#include "session/runtime_kernel_internal.h"

#include "session/nmo_context.h"
#include "app/nmo_load.h"
#include "session/nmo_saver.h"
#include "session/nmo_session.h"
#include "session/nmo_session_internal.h"
#include "core/nmo_arena.h"
#include "core/nmo_bit_array.h"
#include "session/nmo_ref_graph.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_logger.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_object.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_reference_resolver.h"
#include "type/nmo_reflection.h"
#include "behavior/nmo_behavior_index.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "core/nmo_array.h"
#include <string.h>

typedef struct runtime_id_set {
    nmo_arena_array_t ids;  /**< Ordered list of member IDs (for iteration) */
    nmo_bit_array_t bits;   /**< Bit array for O(1) membership test */
} runtime_id_set_t;

#define ID_SET_COUNT(s)   ((s)->ids.count)
#define ID_SET_AT(s, i)   (((nmo_object_id_t *)(s)->ids.data)[(i)])
#define ID_SET_DATA(s)    ((nmo_object_id_t *)(s)->ids.data)

typedef struct runtime_created_layer {
    const nmo_type_descriptor_t *type;
    uint32_t offset;
} runtime_created_layer_t;

#define RUNTIME_MAX_HIERARCHY_DEPTH 64

static int runtime_init_report(nmo_runtime_report_t *report)
{
    if (report == NULL) {
        return NMO_OK;
    }
    memset(report, 0, sizeof(*report));
    report->status = NMO_OK;
    return NMO_OK;
}

static int runtime_dispatch_manager_event(
    nmo_session_t *session,
    nmo_runtime_event_kind_t event_kind,
    const nmo_runtime_event_ctx_t *template_ctx,
    uint32_t *out_errors)
{
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    if (ctx == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_manager_registry_t *manager_reg = nmo_context_get_manager_registry(ctx);
    if (manager_reg == NULL) {
        if (out_errors != NULL) {
            *out_errors = 0;
        }
        return NMO_OK;
    }

    uint32_t errors = 0;
    uint32_t manager_count = nmo_manager_registry_get_count(manager_reg);
    for (uint32_t i = 0; i < manager_count; i++) {
        uint32_t manager_id = nmo_manager_registry_get_id_at(manager_reg, i);
        nmo_manager_t *manager = (nmo_manager_t *)nmo_manager_registry_get(manager_reg, manager_id);
        if (manager == NULL) {
            continue;
        }

        nmo_runtime_event_ctx_t event_ctx;
        memset(&event_ctx, 0, sizeof(event_ctx));
        if (template_ctx != NULL) {
            event_ctx = *template_ctx;
        }
        event_ctx.event = event_kind;
        if (event_ctx.manager_id == 0) {
            event_ctx.manager_id = manager_id;
        }
        if (nmo_guid_is_null(event_ctx.manager_guid)) {
            event_ctx.manager_guid = manager->guid;
        }

        int event_result = nmo_manager_invoke_event(manager, session, &event_ctx);
        if (event_result != NMO_OK) {
            errors++;
        }
    }

    if (out_errors != NULL) {
        *out_errors = errors;
    }
    return NMO_OK;
}

static const nmo_type_descriptor_t *runtime_find_type_for_object(
    const nmo_type_runtime_t *type_rt,
    const nmo_object_t *object)
{
    if (type_rt == NULL || type_rt->types == NULL || object == NULL) {
        return NULL;
    }

    return nmo_type_registry_find_by_class_id_inherited(type_rt->types, object->class_id);
}

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

static size_t runtime_build_create_plan(
    const nmo_type_registry_t *types,
    const nmo_type_descriptor_t *type,
    runtime_created_layer_t *layers,
    size_t layer_cap)
{
    if (types == NULL || type == NULL || layers == NULL || layer_cap == 0) {
        return 0;
    }

    const nmo_type_descriptor_t *lineage[RUNTIME_MAX_HIERARCHY_DEPTH];
    uint32_t lineage_offsets[RUNTIME_MAX_HIERARCHY_DEPTH];
    size_t depth = 0;
    const nmo_type_descriptor_t *current = type;
    uint32_t current_offset = 0;

    while (current != NULL && depth < layer_cap && depth < RUNTIME_MAX_HIERARCHY_DEPTH) {
        lineage[depth++] = current;
        lineage_offsets[depth - 1] = current_offset;
        if (nmo_guid_is_null(current->base_type)) {
            break;
        }

        const nmo_type_descriptor_t *base =
            nmo_type_registry_find_by_guid(types, current->base_type);
        if (base == NULL) {
            break;
        }

        const nmo_type_field_t *base_field = nmo_type_get_field_by_name(current, "base");
        if (base_field != NULL && nmo_guid_equals(base_field->type_guid, base->guid)) {
            current_offset += (uint32_t)base_field->offset;
        } else {
            uint32_t offset = nmo_type_get_state_offset(types, type, base);
            if (offset == (uint32_t)-1) {
                break;
            }
            current_offset = offset;
        }

        current = base;
    }

    if (depth == 0) {
        layers[0].type = type;
        layers[0].offset = 0;
        return 1;
    }

    for (size_t i = 0; i < depth; ++i) {
        layers[i].type = lineage[i];
        layers[i].offset = lineage_offsets[i];
    }

    return depth;
}

static int runtime_create_state_layers(
    void *state,
    void *context,
    const runtime_created_layer_t *plan,
    size_t plan_count,
    runtime_created_layer_t *out_created_layers,
    size_t out_created_cap,
    size_t *out_created_count)
{
    if (state == NULL || plan == NULL || plan_count == 0 ||
        out_created_layers == NULL || out_created_cap == 0 || out_created_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_created_count = 0;

    for (size_t i = 0; i < plan_count; ++i) {
        const nmo_type_descriptor_t *layer_type = plan[i].type;
        if (layer_type == NULL || layer_type->vtable == NULL || layer_type->vtable->create == NULL) {
            continue;
        }

        uint8_t *layer_state = (uint8_t *)state + plan[i].offset;
        int result = layer_type->vtable->create(layer_state, layer_type, context);
        if (result != NMO_OK) {
            return result;
        }

        if (*out_created_count >= out_created_cap) {
            return NMO_ERR_INTERNAL;
        }

        out_created_layers[*out_created_count] = plan[i];
        (*out_created_count)++;
    }

    return NMO_OK;
}

static void runtime_destroy_state_layers(
    void *state,
    void *context,
    const runtime_created_layer_t *created_layers,
    size_t created_count)
{
    if (state == NULL || created_layers == NULL || created_count == 0) {
        return;
    }

    for (size_t idx = created_count; idx > 0; --idx) {
        size_t i = idx - 1;
        const nmo_type_descriptor_t *layer_type = created_layers[i].type;
        if (layer_type == NULL || layer_type->vtable == NULL || layer_type->vtable->destroy == NULL) {
            continue;
        }

        uint8_t *layer_state = (uint8_t *)state + created_layers[i].offset;
        layer_type->vtable->destroy(layer_state, layer_type, context);
    }
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

static bool runtime_get_pointer_array_count(
    const nmo_type_descriptor_t *type,
    const char *field_name,
    const void *instance,
    uint32_t *out_count)
{
    if (type == NULL || field_name == NULL || instance == NULL || out_count == NULL) {
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
    if (count_field == NULL && base_len > 0) {
        const char *last_underscore = NULL;
        for (size_t i = 0; i < base_len; ++i) {
            if (field_name[i] == '_') {
                last_underscore = field_name + i;
            }
        }

        if (last_underscore != NULL) {
            size_t short_base_len = (size_t)(last_underscore - field_name);
            if (short_base_len > 0 && short_base_len + 6 < sizeof(count_name)) {
                memcpy(count_name, field_name, short_base_len);
                memcpy(count_name + short_base_len, "_count", 7);
                count_field = nmo_type_get_field_by_name(type, count_name);
            }
        }
    }

    if (count_field == NULL) {
        return false;
    }

    *out_count = nmo_field_get_uint32(instance, count_field);
    return true;
}

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
        if (!runtime_get_pointer_array_count(ctx->type, field->name, ctx->instance, &count)) {
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

static int runtime_remap_object_copy_refs(
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
     * add them to the delete set. Total work: O(E) vs O(rounds × N × E). */
    nmo_ref_graph_t *cascade_graph = nmo_ref_graph_create(repo, type_rt->types, arena);
    if (cascade_graph == NULL) {
        /* Fallback: no cascade if graph creation fails */
        return NMO_OK;
    }

    /* Worklist: index into the id set's array. New IDs appended
     * by runtime_id_set_add are automatically reached. */
    size_t worklist_head = 0;

    while (worklist_head < ID_SET_COUNT(out_set)) {
        nmo_object_id_t target_id = ID_SET_AT(out_set, worklist_head++);

        nmo_ref_edge_t *in_edges = NULL;
        size_t in_count = 0;
        nmo_ref_graph_get_object_edges(cascade_graph, target_id,
                                       NMO_REF_DIR_INCOMING, &in_edges, &in_count);

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
            /* New ID appended to out_set->ids — worklist_head will reach it */
        }
    }

    nmo_ref_graph_destroy(cascade_graph);

    return NMO_OK;
}

int runtime_kernel_preview_delete(
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
    int result = runtime_collect_delete_set(repo, type_rt, arena, &request, &delete_set);
    if (result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return result;
    }

    *out_ids = ID_SET_DATA(&delete_set);
    *out_count = ID_SET_COUNT(&delete_set);
    nmo_bit_array_dispose(&delete_set.bits);
    return NMO_OK;
}

static int runtime_remap_all_objects(
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

static int runtime_execute_create(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *report)
{
    if (session == NULL || request == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    if (ctx == NULL || repo == NULL || arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    const nmo_allocator_t *alloc = nmo_context_get_allocator(ctx);
    nmo_object_t *object = nmo_object_create(
        alloc,
        NMO_OBJECT_ID_NONE,
        request->payload.create.class_id);
    if (object == NULL) {
        return NMO_ERR_NOMEM;
    }

    if (request->payload.create.name != NULL) {
        (void)nmo_object_set_name(object, request->payload.create.name);
    }
    if (!nmo_guid_is_null(request->payload.create.type_guid)) {
        (void)nmo_object_set_type_guid(object, request->payload.create.type_guid);
    }

    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
    const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, object);
    if (type != NULL && type->size > 0) {
        uint32_t state_size = type->size;
        runtime_created_layer_t create_plan[RUNTIME_MAX_HIERARCHY_DEPTH];
        runtime_created_layer_t created_layers[RUNTIME_MAX_HIERARCHY_DEPTH];
        size_t created_count = 0;
        if (type->ext != NULL && type->ext->total_state_size > 0) {
            state_size = type->ext->total_state_size;
        }

        int alloc_result = nmo_object_alloc_state(object, state_size);
        if (alloc_result != NMO_OK) {
            nmo_object_destroy(object);
            return alloc_result;
        }

        if (object->state != NULL) {
            size_t plan_count = runtime_build_create_plan(
                type_rt->types, type, create_plan, RUNTIME_MAX_HIERARCHY_DEPTH);
            int create_result = runtime_create_state_layers(
                object->state,
                session,
                create_plan,
                plan_count,
                created_layers,
                RUNTIME_MAX_HIERARCHY_DEPTH,
                &created_count);
            if (create_result != NMO_OK) {
                runtime_destroy_state_layers(object->state, session, created_layers, created_count);
                nmo_object_destroy(object);
                return create_result;
            }
        }
    }

    nmo_object_t *owned = object;
    int add_result = nmo_object_repository_add(repo, &owned);
    if (add_result != NMO_OK) {
        nmo_object_destroy(object);
        return add_result;
    }

    if (request->payload.create.out_created_id != NULL) {
        *request->payload.create.out_created_id = object->id;
    }

    if (report != NULL) {
        report->created_objects = 1;
        report->affected_objects = 1;
    }
    return NMO_OK;
}

static int runtime_clone_object(
    nmo_session_t *session,
    const nmo_object_t *source,
    const nmo_type_descriptor_t *type,
    nmo_object_t **out_clone)
{
    if (session == NULL || source == NULL || out_clone == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_clone = NULL;

    nmo_context_t *ctx = nmo_session_get_context(session);
    const nmo_allocator_t *alloc = (ctx != NULL) ? nmo_context_get_allocator(ctx) : NULL;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *clone = nmo_object_create(alloc, NMO_OBJECT_ID_NONE, source->class_id);
    if (clone == NULL) {
        return NMO_ERR_NOMEM;
    }

    if (source->name != NULL) {
        (void)nmo_object_set_name(clone, source->name);
    }
    clone->flags = source->flags;
    clone->creation_flags = source->creation_flags;
    clone->save_flags = source->save_flags;
    clone->type_guid = source->type_guid;

    if (source->chunk != NULL) {
        nmo_chunk_t *cloned_chunk = nmo_chunk_clone(source->chunk, arena);
        clone->chunk = cloned_chunk;
    }

    if (source->state != NULL && source->state_size > 0) {
        if (nmo_object_alloc_state(clone, source->state_size) != NMO_OK) {
            nmo_object_destroy(clone);
            return NMO_ERR_NOMEM;
        }

        if (type != NULL && type->vtable != NULL && type->vtable->copy != NULL) {
            int copy_result = type->vtable->copy(source->state, clone->state, type, arena);
            if (copy_result != NMO_OK) {
                nmo_object_destroy(clone);
                return copy_result;
            }
        } else {
            memcpy(clone->state, source->state, source->state_size);
        }
    }

    *out_clone = clone;
    return NMO_OK;
}

static int runtime_execute_copy(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *report)
{
    if (session == NULL || request == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_object_id_t *ids = request->payload.copy.ids;
    size_t count = request->payload.copy.count;
    if (ids == NULL || count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    const nmo_type_runtime_t *type_rt = (ctx != NULL) ? nmo_context_get_type_runtime(ctx) : NULL;
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_arena_t *arena = nmo_session_get_arena(session);
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t **sources = (nmo_object_t **)nmo_arena_alloc(
        arena,
        sizeof(nmo_object_t *) * count,
        _Alignof(nmo_object_t *));
    nmo_object_t **clones = (nmo_object_t **)nmo_arena_alloc(
        arena,
        sizeof(nmo_object_t *) * count,
        _Alignof(nmo_object_t *));
    const nmo_type_descriptor_t **types = (const nmo_type_descriptor_t **)nmo_arena_alloc(
        arena,
        sizeof(nmo_type_descriptor_t *) * count,
        _Alignof(nmo_type_descriptor_t *));
    if (sources == NULL || clones == NULL || types == NULL) {
        return NMO_ERR_NOMEM;
    }

    size_t copied_count = 0;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *src = nmo_object_repository_find_by_id(repo, ids[i]);
        if (src == NULL) {
            if (request->flags & NMO_RUNTIME_REQUEST_STRICT) {
                return NMO_ERR_NOT_FOUND;
            }
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, src);
        nmo_object_t *clone = NULL;
        int clone_result = runtime_clone_object(session, src, type, &clone);
        if (clone_result != NMO_OK) {
            return clone_result;
        }

        nmo_object_t *owned = clone;
        int add_result = nmo_object_repository_add(repo, &owned);
        if (add_result != NMO_OK) {
            nmo_object_destroy(clone);
            return add_result;
        }

        sources[copied_count] = src;
        clones[copied_count] = clone;
        types[copied_count] = type;
        copied_count++;

        if (report != NULL) {
            report->copied_objects++;
            report->affected_objects++;
        }
    }

    if (copied_count == 0) {
        return NMO_OK;
    }

    nmo_id_remap_t *copy_remap = nmo_id_remap_create(arena);
    if (copy_remap == NULL) {
        return NMO_ERR_NOMEM;
    }

    for (size_t i = 0; i < copied_count; i++) {
        nmo_status_t add_st = nmo_id_remap_add(copy_remap, sources[i]->id, clones[i]->id);
        if (add_st != NMO_OK) {
            return add_st;
        }
    }

    for (size_t i = 0; i < copied_count; i++) {
        nmo_object_t *clone = clones[i];
        const nmo_type_descriptor_t *type = types[i];
        if (clone == NULL || clone->state == NULL || type == NULL) {
            continue;
        }

        (void)runtime_remap_object_copy_refs(
            type_rt,
            type,
            clone->state,
            copy_remap);

        if (type->vtable != NULL && type->vtable->prepare_dependencies != NULL) {
            (void)type->vtable->prepare_dependencies(clone->state, type, repo);
        }

        if (type->vtable != NULL && type->vtable->remap_dependencies != NULL) {
            (void)type->vtable->remap_dependencies(clone->state, type, repo);
        }
    }

    return NMO_OK;
}

static int runtime_execute_delete(
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
    if (repo == NULL || arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    runtime_id_set_t delete_set;
    memset(&delete_set, 0, sizeof(delete_set));
    int collect_result = runtime_collect_delete_set(repo, type_rt, arena, request, &delete_set);
    if (collect_result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return collect_result;
    }

    /* Two-pass delete: separate detach from destroy to prevent pre_delete
     * hooks from accessing state data of objects already freed in an earlier
     * iteration. Pass 1 runs hooks and detaches all; pass 2 destroys. */

    /* Pass 1: pre_delete hooks + detach from repository */
    nmo_object_t **detached_objects = (nmo_object_t **)nmo_arena_alloc(
        arena, ID_SET_COUNT(&delete_set) * sizeof(nmo_object_t *),
        _Alignof(nmo_object_t *));
    if (detached_objects == NULL && ID_SET_COUNT(&delete_set) > 0) {
        nmo_bit_array_dispose(&delete_set.bits);
        return NMO_ERR_NOMEM;
    }
    size_t detached_count = 0;

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
            int hook_result = type->vtable->pre_delete(obj->state, type, repo);
            if (hook_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
                nmo_bit_array_dispose(&delete_set.bits);
                return hook_result;
            }
        }

        nmo_object_t *detached = NULL;
        int remove_result = nmo_object_repository_take(repo, object_id, &detached);
        if (remove_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
            nmo_bit_array_dispose(&delete_set.bits);
            return remove_result;
        }

        if (remove_result == NMO_OK && detached != NULL) {
            detached_objects[detached_count++] = detached;
        }
    }

    /* Pass 2: post_delete hooks + destroy (all objects already detached) */
    for (size_t i = 0; i < detached_count; i++) {
        nmo_object_t *detached = detached_objects[i];
        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, detached);

        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->post_delete != NULL &&
            detached->state != NULL) {
            type->vtable->post_delete(detached->state, type, repo);
        }
        nmo_object_destroy(detached);

        if (report != NULL) {
            report->deleted_objects++;
            report->affected_objects++;
        }
    }

    nmo_bit_array_dispose(&delete_set.bits);

    if (request->flags & NMO_RUNTIME_REQUEST_SAFE_DETACH) {
        if (type_rt != NULL && type_rt->types != NULL) {
            int detach_result = runtime_remap_all_objects(repo, type_rt, request->flags);
            if (detach_result != NMO_OK) {
                return detach_result;
            }
        }
    }

    return NMO_OK;
}

int nmo_runtime_kernel_finalize_load(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report)
{
    (void)request;
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    runtime_init_report(out_report);

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_logger_t *logger = (ctx != NULL) ? nmo_context_get_logger(ctx) : NULL;
    const nmo_type_runtime_t *type_rt = (ctx != NULL) ? nmo_context_get_type_runtime(ctx) : NULL;

    nmo_runtime_load_stats_t finish_stats;
    memset(&finish_stats, 0, sizeof(finish_stats));

    nmo_reference_resolver_t *resolver = nmo_session_get_reference_resolver(session);
    if (resolver != NULL) {
        (void)nmo_reference_resolver_resolve_all(resolver);

        nmo_reference_stats_t resolver_stats;
        if (nmo_reference_resolver_get_stats(resolver, &resolver_stats) == NMO_OK) {
            finish_stats.references.total = resolver_stats.total_count;
            finish_stats.references.resolved = resolver_stats.resolved_count;
            finish_stats.references.unresolved = resolver_stats.unresolved_count;
            finish_stats.references.ambiguous = resolver_stats.ambiguous_count;
        }
    }

    if (repo != NULL && type_rt != NULL && type_rt->types != NULL) {
        size_t object_count = 0;
        nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
        finish_stats.total_objects = object_count;

        for (size_t i = 0; i < object_count; i++) {
            nmo_object_t *obj = objects[i];
            if (obj == NULL || obj->state == NULL) {
                continue;
            }

            const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, obj);
            if (type == NULL) {
                continue;
            }

            int hook_result = NMO_OK;
            if (type->vtable != NULL && type->vtable->prepare_dependencies != NULL) {
                hook_result = type->vtable->prepare_dependencies(obj->state, type, repo);
                if (hook_result != NMO_OK) {
                    finish_stats.object_postload.errors++;
                    if (out_report != NULL) {
                        out_report->object_hook_errors++;
                    }
                    if (logger != NULL) {
                        nmo_log(logger, NMO_LOG_WARN,
                                "Runtime prepare hook failed for object %u: %d",
                                obj->id,
                                hook_result);
                    }
                    continue;
                }
            }

            if (type->vtable != NULL && type->vtable->remap_dependencies != NULL) {
                finish_stats.object_postload.invoked++;
                hook_result = type->vtable->remap_dependencies(obj->state, type, repo);
            }

            if (hook_result != NMO_OK) {
                finish_stats.object_postload.errors++;
                if (out_report != NULL) {
                    out_report->object_hook_errors++;
                }
                if (logger != NULL) {
                    nmo_log(logger, NMO_LOG_WARN,
                            "Runtime remap/post hook failed for object %u: %d",
                            obj->id,
                            hook_result);
                }
            }
        }
    }

    /* Build behavior ownership index now that all objects are remapped */
    nmo_session_build_behavior_index(session);

    uint32_t manager_errors = 0;
    (void)runtime_dispatch_manager_event(
        session,
        NMO_RUNTIME_EVENT_POST_LOAD,
        NULL,
        &manager_errors);
    finish_stats.manager_errors = manager_errors;

    if (out_report != NULL) {
        out_report->manager_event_errors += manager_errors;
    }

    int index_result = nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL);
    if (index_result != NMO_OK && logger != NULL) {
        nmo_log(logger, NMO_LOG_WARN,
                "Runtime index rebuild failed: %d", index_result);
    }

    finish_stats.flags = 0;
    nmo_session_set_runtime_load_stats(session, &finish_stats);

    if (out_report != NULL) {
        out_report->status = NMO_OK;
    }
    return NMO_OK;
}

int nmo_runtime_kernel_execute(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report)
{
    if (session == NULL || request == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    runtime_init_report(out_report);

    int result = NMO_OK;
    switch (request->kind) {
        case NMO_RUNTIME_OP_LOAD:
            result = nmo_load_file(
                session,
                request->payload.load.path,
                request->payload.load.options);
            break;
        case NMO_RUNTIME_OP_SAVE:
            result = nmo_save_file(
                session,
                request->payload.save.path,
                request->payload.save.options);
            break;
        case NMO_RUNTIME_OP_CREATE:
            result = runtime_execute_create(session, request, out_report);
            break;
        case NMO_RUNTIME_OP_COPY:
            (void)runtime_dispatch_manager_event(
                session, NMO_RUNTIME_EVENT_PRE_COPY, NULL,
                out_report != NULL ? &out_report->manager_event_errors : NULL);
            result = runtime_execute_copy(session, request, out_report);
            (void)runtime_dispatch_manager_event(
                session, NMO_RUNTIME_EVENT_POST_COPY, NULL,
                out_report != NULL ? &out_report->manager_event_errors : NULL);
            break;
        case NMO_RUNTIME_OP_DELETE:
            (void)runtime_dispatch_manager_event(
                session, NMO_RUNTIME_EVENT_PRE_DELETE, NULL,
                out_report != NULL ? &out_report->manager_event_errors : NULL);
            result = runtime_execute_delete(session, request, out_report);
            (void)runtime_dispatch_manager_event(
                session, NMO_RUNTIME_EVENT_POST_DELETE, NULL,
                out_report != NULL ? &out_report->manager_event_errors : NULL);
            break;
        default:
            result = NMO_ERR_INVALID_ARGUMENT;
            break;
    }

    if (out_report != NULL) {
        out_report->status = result;
    }
    return result;
}
