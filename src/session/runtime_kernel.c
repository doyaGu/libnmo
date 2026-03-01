#include "session/nmo_runtime_kernel.h"

#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "app/nmo_saver.h"
#include "app/nmo_session.h"
#include "session/nmo_session_internal.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_object.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_reference_resolver.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include <string.h>

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
        if (nmo_object_alloc_state(object, type->size) == NMO_OK &&
            object->state != NULL &&
            type->vtable != NULL &&
            type->vtable->create != NULL) {
            (void)type->vtable->create(object->state, type, session);
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

static nmo_object_t *runtime_clone_object(
    nmo_session_t *session,
    const nmo_object_t *source,
    const nmo_type_descriptor_t *type)
{
    if (session == NULL || source == NULL) {
        return NULL;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    const nmo_allocator_t *alloc = (ctx != NULL) ? nmo_context_get_allocator(ctx) : NULL;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    if (arena == NULL) {
        return NULL;
    }

    nmo_object_t *clone = nmo_object_create(alloc, NMO_OBJECT_ID_NONE, source->class_id);
    if (clone == NULL) {
        return NULL;
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
            return NULL;
        }

        if (type != NULL && type->vtable != NULL && type->vtable->copy != NULL) {
            int copy_result = type->vtable->copy(source->state, clone->state, type, arena);
            if (copy_result != NMO_OK) {
                nmo_object_destroy(clone);
                return NULL;
            }
        } else {
            memcpy(clone->state, source->state, source->state_size);
        }
    }

    return clone;
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

    for (size_t i = 0; i < count; i++) {
        nmo_object_t *src = nmo_object_repository_find_by_id(repo, ids[i]);
        if (src == NULL) {
            if (request->flags & NMO_RUNTIME_REQUEST_STRICT) {
                return NMO_ERR_NOT_FOUND;
            }
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, src);
        nmo_object_t *clone = runtime_clone_object(session, src, type);
        if (clone == NULL) {
            return NMO_ERR_NOMEM;
        }

        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->prepare_dependencies != NULL &&
            clone->state != NULL) {
            (void)type->vtable->prepare_dependencies(clone->state, type, repo);
        }

        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->remap_dependencies != NULL &&
            clone->state != NULL) {
            (void)type->vtable->remap_dependencies(clone->state, type, repo);
        }

        nmo_object_t *owned = clone;
        int add_result = nmo_object_repository_add(repo, &owned);
        if (add_result != NMO_OK) {
            nmo_object_destroy(clone);
            return add_result;
        }

        if (report != NULL) {
            report->copied_objects++;
            report->affected_objects++;
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

    const nmo_object_id_t *ids = request->payload.destroy.ids;
    size_t count = request->payload.destroy.count;
    if (ids == NULL || count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    const nmo_type_runtime_t *type_rt = (ctx != NULL) ? nmo_context_get_type_runtime(ctx) : NULL;
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, ids[i]);
        if (obj == NULL) {
            if (request->flags & NMO_RUNTIME_REQUEST_STRICT) {
                return NMO_ERR_NOT_FOUND;
            }
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, obj);
        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->pre_delete != NULL &&
            obj->state != NULL) {
            int hook_result = type->vtable->pre_delete(obj->state, type, repo);
            if (hook_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
                return hook_result;
            }
        }

        nmo_object_t *detached = NULL;
        int remove_result = nmo_object_repository_take(repo, ids[i], &detached);
        if (remove_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
            return remove_result;
        }

        if (remove_result == NMO_OK && detached != NULL) {
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
