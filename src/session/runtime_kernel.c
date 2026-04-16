#include "session/nmo_runtime_kernel.h"
#include "session/nmo_runtime_ref_remap.h"
#include "session/nmo_runtime_delete.h"

#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "format/nmo_id_remap.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_object.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_reference_resolver.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "runtime_internal.h"
#include <string.h>

/* ── Report init ───────────────────────────────────────────────── */

static int runtime_init_report(nmo_runtime_report_t *report)
{
    if (report == NULL) {
        return NMO_OK;
    }
    memset(report, 0, sizeof(*report));
    report->status = NMO_OK;
    return NMO_OK;
}

/* ── Manager event dispatch ────────────────────────────────────── */

static nmo_load_perf_stats_t *runtime_load_perf_stats(const nmo_runtime_request_t *request)
{
    if (request == NULL || request->payload.load.options == NULL ||
        !request->payload.load.options->collect_perf_stats) {
        return NULL;
    }
    return request->payload.load.options->perf_stats;
}

static uint64_t runtime_load_perf_begin(nmo_load_perf_stats_t *stats)
{
    return (stats != NULL) ? nmo_perf_now_ticks() : 0u;
}

static void runtime_load_perf_end(nmo_load_perf_stats_t *stats,
                                  nmo_load_perf_phase_t phase,
                                  uint64_t start_ticks)
{
    if (stats == NULL) {
        return;
    }
    uint64_t end_ticks = nmo_perf_now_ticks();
    nmo_load_perf_stats_record(stats, phase, nmo_perf_elapsed_ms(start_ticks, end_ticks));
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

/* ── State layer management ────────────────────────────────────── */

typedef struct runtime_created_layer {
    const nmo_type_descriptor_t *type;
    uint32_t offset;
} runtime_created_layer_t;

#define RUNTIME_MAX_HIERARCHY_DEPTH 64

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

/* ── Create operation ──────────────────────────────────────────── */

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

/* ── Clone + Copy operation ────────────────────────────────────── */

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

        (void)nmo_runtime_remap_copy_refs(
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

/* ── Finalize load ─────────────────────────────────────────────── */

int nmo_runtime_kernel_finalize_load(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report)
{
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    runtime_init_report(out_report);

    /* Extract strict flag from load options if available */
    int strict_mode = 0;
    if (request != NULL &&
        request->payload.load.options != NULL &&
        (request->payload.load.options->flags & NMO_LOAD_STRICT)) {
        strict_mode = 1;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_logger_t *logger = (ctx != NULL) ? nmo_context_get_logger(ctx) : NULL;
    const nmo_type_runtime_t *type_rt = (ctx != NULL) ? nmo_context_get_type_runtime(ctx) : NULL;

    nmo_runtime_load_stats_t finish_stats;
    memset(&finish_stats, 0, sizeof(finish_stats));
    nmo_load_perf_stats_t *perf_stats = runtime_load_perf_stats(request);

    nmo_reference_resolver_t *resolver = nmo_session_get_reference_resolver(session);
    if (resolver != NULL) {
        uint64_t reference_start = runtime_load_perf_begin(perf_stats);
        (void)nmo_reference_resolver_resolve_all(resolver);

        nmo_reference_stats_t resolver_stats;
        if (nmo_reference_resolver_get_stats(resolver, &resolver_stats) == NMO_OK) {
            finish_stats.references.total = resolver_stats.total_count;
            finish_stats.references.resolved = resolver_stats.resolved_count;
            finish_stats.references.unresolved = resolver_stats.unresolved_count;
            finish_stats.references.ambiguous = resolver_stats.ambiguous_count;
        }

        /* Populate unresolved reference preview (up to 8 samples) */
        if (finish_stats.references.unresolved > 0) {
            nmo_object_ref_t **unresolved_refs = NULL;
            size_t unresolved_count = 0;
            if (nmo_reference_resolver_get_unresolved(resolver, &unresolved_refs,
                                                       &unresolved_count) == NMO_OK) {
                size_t preview_count = unresolved_count < 8 ? unresolved_count : 8;
                finish_stats.references.unresolved_preview_count = (uint32_t)preview_count;
                for (size_t i = 0; i < preview_count; i++) {
                    finish_stats.references.unresolved_preview[i].id =
                        unresolved_refs[i]->id;
                    finish_stats.references.unresolved_preview[i].class_id =
                        unresolved_refs[i]->class_id;
                }
            }
        }
        runtime_load_perf_end(perf_stats, NMO_LOAD_PERF_REFERENCE_RESOLVE, reference_start);
    }

    /* Strict mode: fail if any references are unresolved */
    if (strict_mode && finish_stats.references.unresolved > 0) {
        nmo_session_set_runtime_load_stats(session, &finish_stats);
        if (logger != NULL) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Strict mode: %u unresolved references",
                    finish_stats.references.unresolved);
        }
        return NMO_ERR_VALIDATION_FAILED;
    }

    if (repo != NULL && type_rt != NULL && type_rt->types != NULL) {
        size_t object_count = nmo_object_repository_get_count(repo);
        finish_stats.total_objects = object_count;

        for (size_t i = 0; i < object_count; i++) {
            nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
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

    /* Post-load callback (e.g., build behavior index) */
    {
        const nmo_runtime_ops_t *ops = nmo_session_get_runtime_ops(session);
        if (ops && ops->post_load) {
            uint64_t behavior_start = runtime_load_perf_begin(perf_stats);
            ops->post_load(session);
            runtime_load_perf_end(perf_stats, NMO_LOAD_PERF_BEHAVIOR_POST_LOAD, behavior_start);
        }
    }

    uint32_t manager_errors = 0;
    uint64_t manager_start = runtime_load_perf_begin(perf_stats);
    (void)runtime_dispatch_manager_event(
        session,
        NMO_RUNTIME_EVENT_POST_LOAD,
        NULL,
        &manager_errors);
    runtime_load_perf_end(perf_stats, NMO_LOAD_PERF_MANAGER_POST_LOAD, manager_start);
    finish_stats.manager_errors = manager_errors;

    if (out_report != NULL) {
        out_report->manager_event_errors += manager_errors;
    }

    uint64_t index_start = runtime_load_perf_begin(perf_stats);
    int index_result = nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL);
    runtime_load_perf_end(perf_stats, NMO_LOAD_PERF_INDEX_REBUILD, index_start);
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

/* ── Kernel dispatcher ─────────────────────────────────────────── */

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
        case NMO_RUNTIME_OP_LOAD: {
            const nmo_runtime_ops_t *ops = nmo_session_get_runtime_ops(session);
            if (ops && ops->load_file)
                result = ops->load_file(session,
                    request->payload.load.path,
                    request->payload.load.options);
            else
                result = NMO_ERR_NOT_IMPLEMENTED;
            break;
        }
        case NMO_RUNTIME_OP_SAVE: {
            const nmo_runtime_ops_t *ops = nmo_session_get_runtime_ops(session);
            if (ops && ops->save_file)
                result = ops->save_file(session,
                    request->payload.save.path,
                    request->payload.save.options);
            else
                result = NMO_ERR_NOT_IMPLEMENTED;
            break;
        }
        case NMO_RUNTIME_OP_CREATE:
            result = runtime_execute_create(session, request, out_report);
            if (result == NMO_OK &&
                (request->flags & NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION) == 0u) {
                (void)nmo_session_apply_edit_flags(
                    session,
                    NMO_SESSION_EDIT_BEHAVIOR_GRAPH | NMO_SESSION_EDIT_REFERENCES);
            }
            break;
        case NMO_RUNTIME_OP_COPY:
            (void)runtime_dispatch_manager_event(
                session, NMO_RUNTIME_EVENT_PRE_COPY, NULL,
                out_report != NULL ? &out_report->manager_event_errors : NULL);
            result = runtime_execute_copy(session, request, out_report);
            if (result == NMO_OK &&
                (request->flags & NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION) == 0u) {
                (void)nmo_session_apply_edit_flags(
                    session,
                    NMO_SESSION_EDIT_BEHAVIOR_GRAPH | NMO_SESSION_EDIT_REFERENCES);
            }
            (void)runtime_dispatch_manager_event(
                session, NMO_RUNTIME_EVENT_POST_COPY, NULL,
                out_report != NULL ? &out_report->manager_event_errors : NULL);
            break;
        case NMO_RUNTIME_OP_DELETE:
            (void)runtime_dispatch_manager_event(
                session, NMO_RUNTIME_EVENT_PRE_DELETE, NULL,
                out_report != NULL ? &out_report->manager_event_errors : NULL);
            result = nmo_runtime_execute_delete(session, request, out_report);
            if (result == NMO_OK &&
                (request->flags & NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION) == 0u) {
                (void)nmo_session_apply_edit_flags(
                    session,
                    NMO_SESSION_EDIT_BEHAVIOR_GRAPH | NMO_SESSION_EDIT_REFERENCES);
            }
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
