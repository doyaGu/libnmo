/**
 * @file object_system.c
 * @brief Session-layer object lifecycle orchestration.
 */

#include "session/nmo_object_system.h"

#include "object/nmo_object_repository.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_id_sanitizer.h"
#include "object/nmo_shadow_storage.h"
#include "session/nmo_reference_resolver.h"
#include "session/nmo_ref_enumerate.h"

#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"

#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"

#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"

#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "core/nmo_error.h"
#include "core/nmo_debug.h"

#include <string.h>
#include <stdalign.h>

typedef struct object_system_ref_capture_ctx {
    nmo_reference_resolver_t *resolver;
    nmo_object_repository_t *repo;
    nmo_object_t *source;
    nmo_logger_t *logger;
} object_system_ref_capture_ctx_t;

typedef struct object_system_created_layer {
    const nmo_type_descriptor_t *type;
    uint32_t offset;
} object_system_created_layer_t;

#define OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH 64u

static const char *object_system_name_or_default(const nmo_object_t *obj) {
    return (obj != NULL && obj->name != NULL && obj->name[0] != '\0')
        ? obj->name
        : "(null)";
}

static bool object_system_capture_ref(
    void *user_data,
    uint32_t target_id,
    nmo_ref_kind_t kind,
    const char *field_name,
    uint32_t index)
{
    (void)field_name;
    (void)index;

    object_system_ref_capture_ctx_t *ctx = (object_system_ref_capture_ctx_t *)user_data;
    if (ctx == NULL || ctx->resolver == NULL || target_id == 0) {
        return true;
    }

    nmo_object_ref_t ref = {0};
    ref.id = target_id;
    ref.flags = (uint32_t)kind;
    ref.file_index = -1;

    nmo_object_t *target = nmo_object_repository_find_by_id(ctx->repo, target_id);
    if (target != NULL) {
        ref.class_id = target->class_id;
        ref.name = (char *)nmo_object_get_name(target);
        ref.type_guid = nmo_object_get_type_guid(target);
    } else {
        ref.class_id = 0;
        ref.name = NULL;
        ref.type_guid = (nmo_guid_t){0, 0};

        if (ctx->logger != NULL) {
            nmo_log(ctx->logger, NMO_LOG_DEBUG,
                    "  Object ID=%u: unresolved target id=%u queued without metadata",
                    ctx->source ? ctx->source->id : 0,
                    target_id);
        }
    }

    if (nmo_reference_resolver_register_reference(ctx->resolver, &ref) == NULL) {
        if (ctx->logger != NULL) {
            nmo_log(ctx->logger, NMO_LOG_WARN,
                    "  Object ID=%u: failed to register reference target id=%u",
                    ctx->source ? ctx->source->id : 0,
                    target_id);
        }
    }

    return true;
}

static void object_system_rollback_created(
    nmo_object_repository_t *repo,
    nmo_object_t **created,
    size_t count)
{
    if (repo == NULL || created == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (created[i] != NULL) {
            (void)nmo_object_repository_remove(repo, created[i]->id);
            created[i] = NULL;
        }
    }
}

static size_t object_system_build_create_plan(
    const nmo_type_descriptor_t *schema_type,
    object_system_created_layer_t *layers,
    size_t layer_cap)
{
    if (schema_type == NULL || layers == NULL || layer_cap == 0) {
        return 0;
    }

    if (schema_type->ext == NULL ||
        schema_type->ext->hierarchy == NULL ||
        schema_type->ext->hierarchy_depth == 0 ||
        schema_type->ext->hierarchy_depth > layer_cap) {
        layers[0].type = schema_type;
        layers[0].offset = 0;
        return 1;
    }

    size_t count = 0;
    for (size_t idx = schema_type->ext->hierarchy_depth; idx > 0; --idx) {
        const size_t i = idx - 1;
        const nmo_type_descriptor_t *layer_type = schema_type->ext->hierarchy[i];
        if (layer_type == NULL) {
            continue;
        }

        uint32_t offset = 0;
        if (schema_type->ext->state_offsets != NULL) {
            offset = schema_type->ext->state_offsets[i];
        }

        if (count < layer_cap) {
            layers[count].type = layer_type;
            layers[count].offset = offset;
            count++;
        }
    }

    if (count == 0) {
        layers[0].type = schema_type;
        layers[0].offset = 0;
        return 1;
    }

    return count;
}

static nmo_status_t object_system_create_state_layers(
    void *state,
    nmo_deserialize_context_t *deser_ctx,
    const object_system_created_layer_t *plan,
    size_t plan_count,
    object_system_created_layer_t *out_created_layers,
    size_t out_created_cap,
    size_t *out_created_count)
{
    if (state == NULL || deser_ctx == NULL || plan == NULL || plan_count == 0 ||
        out_created_layers == NULL || out_created_cap == 0 || out_created_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_created_count = 0;

    for (size_t i = 0; i < plan_count; ++i) {
        const nmo_type_descriptor_t *layer_type = plan[i].type;
        if (layer_type == NULL || layer_type->vtable == NULL ||
            layer_type->vtable->create == NULL) {
            continue;
        }

        uint8_t *layer_state = (uint8_t *)state + plan[i].offset;
        nmo_status_t result = layer_type->vtable->create(
            layer_state, layer_type, deser_ctx);
        if (result != NMO_OK) {
            return result;
        }

        if (*out_created_count < out_created_cap) {
            out_created_layers[*out_created_count] = plan[i];
            (*out_created_count)++;
        } else {
            return NMO_ERR_INTERNAL;
        }
    }

    return NMO_OK;
}

static void object_system_destroy_state_layers(
    void *state,
    nmo_deserialize_context_t *deser_ctx,
    const object_system_created_layer_t *plan,
    size_t created_count)
{
    if (state == NULL || deser_ctx == NULL || plan == NULL || created_count == 0) {
        return;
    }

    for (size_t idx = created_count; idx > 0; --idx) {
        const size_t i = idx - 1;
        const nmo_type_descriptor_t *layer_type = plan[i].type;
        if (layer_type == NULL || layer_type->vtable == NULL ||
            layer_type->vtable->destroy == NULL) {
            continue;
        }

        uint8_t *layer_state = (uint8_t *)state + plan[i].offset;
        layer_type->vtable->destroy(layer_state, layer_type, deser_ctx);
    }
}

static void object_system_clear_failed_object_state(nmo_object_t *obj)
{
    if (obj == NULL) {
        return;
    }

    obj->data = NULL;
    obj->state = NULL;
    obj->state_size = 0;
}

nmo_status_t nmo_object_system_create_objects_from_header1(
    const nmo_allocator_t *object_allocator,
    nmo_arena_t *scratch_arena,
    nmo_object_repository_t *repo,
    nmo_id_sanitizer_t *id_sanitizer,
    nmo_deserializer_t *load_session,
    const nmo_object_desc_t *descs,
    size_t desc_count,
    nmo_logger_t *logger,
    nmo_object_t ***out_created_objects)
{
    if (scratch_arena == NULL || repo == NULL || load_session == NULL ||
        out_created_objects == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_created_objects = NULL;

    if (descs == NULL || desc_count == 0) {
        return NMO_OK;
    }

    nmo_object_t **created = (nmo_object_t **)nmo_arena_alloc(
        scratch_arena, sizeof(nmo_object_t *) * desc_count, alignof(void *));

    if (created == NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(created, 0, sizeof(nmo_object_t *) * desc_count);

    for (size_t i = 0; i < desc_count; i++) {
        const nmo_object_desc_t *desc = &descs[i];

        const int is_reference_only = (desc->flags & NMO_OBJECT_REFERENCE_FLAG) != 0;
        const uint32_t sanitized_id = nmo_id_sanitize(desc->file_id);

        nmo_object_t *obj = nmo_object_create(object_allocator, NMO_OBJECT_ID_NONE, desc->class_id);
        if (obj == NULL) {
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return NMO_ERR_NOMEM;
        }

        obj->flags = desc->flags;
        obj->save_flags = desc->flags;
        obj->file_id = desc->file_id;

        if (desc->name != NULL && desc->name[0] != '\0') {
            int name_result = nmo_object_set_name(obj, desc->name);
            if (name_result != NMO_OK) {
                nmo_object_destroy(obj);
                object_system_rollback_created(repo, created, desc_count);
                if (id_sanitizer != NULL) {
                    nmo_id_sanitizer_reset(id_sanitizer);
                }
                return name_result;
            }
        }

        nmo_object_t *repo_obj = obj;
        int add_result = nmo_object_repository_add(repo, &obj);
        if (add_result != NMO_OK) {
            nmo_object_destroy(obj);
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return add_result;
        }

        /* Repository owns obj beyond this point. */
        NMO_DEBUG_ASSERT(obj == NULL);
        created[i] = repo_obj;
        obj = repo_obj;

        int file_index_result = nmo_object_set_file_index(obj, desc->file_index);
        if (file_index_result != NMO_OK) {
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return file_index_result;
        }

        int reg_result = nmo_deserializer_register(load_session, obj, (nmo_object_id_t)i);
        if (reg_result != NMO_OK) {
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return reg_result;
        }

        if (id_sanitizer != NULL && sanitized_id != 0) {
            int sanitize_result = nmo_id_sanitizer_register(id_sanitizer, sanitized_id, obj->id);
            if (sanitize_result != NMO_OK && logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Failed to register ID sanitizer mapping (file_id=%u, runtime_id=%u)",
                        sanitized_id, obj->id);
            }
        }

        if (logger) {
            if (is_reference_only) {
                nmo_log(logger, NMO_LOG_INFO,
                        "  Created reference-only object %zu: file_id=%u, file_object_index=%u, runtime_id=%u, class=0x%08X, name='%s'",
                        i, sanitized_id, (uint32_t)i, obj->id, obj->class_id, obj->name ? obj->name : "(null)");
            } else {
                nmo_log(logger, NMO_LOG_INFO,
                        "  Created object %zu: file_id=%u, file_object_index=%u, runtime_id=%u, class=0x%08X, name='%s'",
                        i, sanitized_id, (uint32_t)i, obj->id, obj->class_id, obj->name ? obj->name : "(null)");
            }
        }
    }

    *out_created_objects = created;
    return NMO_OK;
}

nmo_status_t nmo_object_system_prepare_loaded_objects(
    const nmo_allocator_t *object_allocator,
    nmo_arena_t *scratch_arena,
    nmo_object_repository_t *repo,
    nmo_id_sanitizer_t *id_sanitizer,
    nmo_deserializer_t *load_session,
    const nmo_object_desc_t *descs,
    size_t desc_count,
    const nmo_object_data_t *object_data,
    size_t object_data_count,
    nmo_manager_data_t *manager_data,
    size_t manager_data_count,
    nmo_logger_t *logger,
    size_t *out_remap_errors)
{
    if (scratch_arena == NULL || repo == NULL || load_session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (out_remap_errors != NULL) {
        *out_remap_errors = 0;
    }

    nmo_object_t **created_objects = NULL;
    nmo_status_t create_result = nmo_object_system_create_objects_from_header1(
        object_allocator,
        scratch_arena,
        repo,
        id_sanitizer,
        load_session,
        descs,
        desc_count,
        logger,
        &created_objects);

    if (create_result != NMO_OK) {
        return create_result;
    }

    if (logger) {
        nmo_log(logger, NMO_LOG_INFO, "Phase 11 (object_system): Attaching object chunks");
    }

    if (created_objects != NULL && object_data != NULL) {
        size_t attach_count = desc_count;
        if (attach_count > object_data_count) {
            attach_count = object_data_count;
        }

        for (size_t i = 0; i < attach_count; i++) {
            nmo_object_t *obj = created_objects[i];
            if (obj == NULL) {
                continue;
            }

            obj->chunk = object_data[i].chunk;

            if (logger && object_data[i].chunk != NULL) {
                nmo_log(logger, NMO_LOG_INFO,
                        "  Object %zu: runtime_id=%u, chunk attached (size=%u, version=%u)",
                        i,
                        obj->id,
                        object_data[i].data_size,
                        object_data[i].chunk->chunk_version);
            }
        }
    }

    if (logger) {
        nmo_log(logger, NMO_LOG_INFO, "Phase 12 (object_system): Building ID remap table");
    }

    nmo_id_remap_table_t *remap_table = NULL;
    if (created_objects != NULL) {
        remap_table = nmo_id_remap_create(scratch_arena);
        if (remap_table == NULL) {
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Failed to allocate ID remap table");
            }
            return NMO_ERR_NOMEM;
        }

        for (size_t i = 0; i < desc_count; i++) {
            if (created_objects[i] != NULL) {
                (void)nmo_id_remap_add(remap_table, (nmo_object_id_t)i, created_objects[i]->id);
            }
        }
    }

    if (remap_table == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_WARN,
                    "  Failed to build ID remap table (may be empty session)");
        }
        return NMO_OK;
    }

    nmo_chunk_file_context_t *file_ctx = (nmo_chunk_file_context_t *)nmo_arena_alloc(
        scratch_arena, sizeof(nmo_chunk_file_context_t), alignof(nmo_chunk_file_context_t));
    if (file_ctx == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "  Failed to allocate chunk file context");
        }
        return NMO_ERR_NOMEM;
    }
    file_ctx->file_to_runtime = remap_table;
    file_ctx->runtime_to_file = NULL;

    if (created_objects != NULL) {
        for (size_t i = 0; i < desc_count; i++) {
            if (created_objects[i] != NULL && created_objects[i]->chunk != NULL) {
                nmo_chunk_set_file_context(created_objects[i]->chunk, file_ctx);
            }
        }
    }

    if (manager_data != NULL) {
        for (size_t i = 0; i < manager_data_count; i++) {
            if (manager_data[i].chunk != NULL) {
                nmo_chunk_set_file_context(manager_data[i].chunk, file_ctx);
            }
        }
    }

    if (logger) {
        nmo_log(logger, NMO_LOG_INFO,
                "  Built remap table with %zu entries",
                nmo_id_remap_table_get_count(remap_table));
        nmo_log(logger, NMO_LOG_INFO, "Phase 13 (object_system): Remapping IDs in chunks");
    }

    size_t remap_error_count = 0;

    if (created_objects != NULL) {
        for (size_t i = 0; i < desc_count; i++) {
            if (created_objects[i] != NULL && created_objects[i]->chunk != NULL) {
                if (created_objects[i]->chunk->file_context != NULL) {
                    continue;
                }
                nmo_status_t remap_result = nmo_chunk_remap_object_ids(created_objects[i]->chunk,
                                                                       remap_table);
                if (remap_result != NMO_OK) {
                    remap_error_count++;
                    if (logger) {
                        nmo_log(logger, NMO_LOG_ERROR,
                                "  Failed to remap IDs in object %zu chunk (code=%d)",
                                i,
                                remap_result);
                    }
                }
            }
        }
    }

    if (manager_data != NULL) {
        for (size_t i = 0; i < manager_data_count; i++) {
            if (manager_data[i].chunk != NULL) {
                if (manager_data[i].chunk->file_context != NULL) {
                    continue;
                }
                nmo_status_t remap_result = nmo_chunk_remap_object_ids(manager_data[i].chunk,
                                                                       remap_table);
                if (remap_result != NMO_OK) {
                    remap_error_count++;
                    if (logger) {
                        nmo_log(logger, NMO_LOG_ERROR,
                                "  Failed to remap IDs in manager %zu chunk (code=%d)",
                                i,
                                remap_result);
                    }
                }
            }
        }
    }

    if (out_remap_errors != NULL) {
        *out_remap_errors = remap_error_count;
    }

    if (logger && remap_error_count > 0) {
        nmo_log(logger, NMO_LOG_WARN,
                "  ID remapping completed with %zu errors",
                remap_error_count);
    }

    return NMO_OK;
}

nmo_status_t nmo_object_system_deserialize_loaded_objects(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    nmo_shadow_storage_t *shadow_storage,
    uint32_t deser_flags,
    nmo_reference_resolver_t *reference_resolver,
    const nmo_deserializer_t *load_session,
    size_t file_object_count,
    nmo_object_system_deserialize_stats_t *out_stats)
{
    if (repo == NULL || type_rt == NULL || type_rt->types == NULL ||
        arena == NULL || load_session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_system_deserialize_stats_t stats = {0};

    nmo_deserialize_context_t deser_ctx = nmo_deserialize_context_create(
        arena, repo, type_rt, deser_flags);

    for (size_t file_index = 0; file_index < file_object_count; file_index++) {
        nmo_object_id_t runtime_id = NMO_OBJECT_ID_INVALID;
        int id_result = nmo_deserializer_get_runtime_id(load_session,
                                                       (nmo_object_id_t)file_index,
                                                       &runtime_id);
        if (id_result != NMO_OK) {
            stats.skipped_null++;
            continue;
        }

        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, runtime_id);
        if (obj == NULL) {
            stats.skipped_null++;
            continue;
        }

        if (obj->chunk == NULL) {
            stats.skipped_no_chunk++;
            continue;
        }

        size_t chunk_buffer_size = 0;
        const void *chunk_buffer = nmo_chunk_get_data(obj->chunk, &chunk_buffer_size);
        const bool has_chunk_data = (chunk_buffer != NULL && chunk_buffer_size > 0);
        if (!has_chunk_data) {
            stats.skipped_empty_chunk++;
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): chunk has no data buffer, deserializing defaults",
                        file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj));
            }
        }

        const nmo_type_descriptor_t *schema_type =
            nmo_type_registry_find_by_class_id_inherited(type_rt->types, obj->class_id);

        if (schema_type == NULL || schema_type->vtable == NULL ||
            schema_type->vtable->deserialize == NULL) {
            stats.no_schema++;
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): no readable schema found",
                        file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj));
            }
            nmo_chunk_close(obj->chunk);
            continue;
        }

        uint32_t state_size = schema_type->size;
        if (schema_type->ext && schema_type->ext->total_state_size > 0) {
            state_size = schema_type->ext->total_state_size;
        }

        nmo_status_t alloc_result = nmo_object_alloc_state(obj, state_size);
        if (alloc_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): failed to allocate %u bytes for state",
                        file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), state_size);
            }
            nmo_chunk_close(obj->chunk);
            continue;
        }

        void *state = nmo_object_get_state(obj);
        nmo_deserialize_context_set_object(&deser_ctx, obj);
        deser_ctx.chunk_version = nmo_chunk_get_data_version(obj->chunk);

        object_system_created_layer_t create_plan[OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH];
        object_system_created_layer_t created_layers[OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH];
        size_t plan_count = object_system_build_create_plan(
            schema_type, create_plan, OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH);
        size_t created_count = 0;

        nmo_status_t create_result = object_system_create_state_layers(
            state, &deser_ctx, create_plan, plan_count,
            created_layers, OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH, &created_count);
        if (create_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                char error_msg[1024];
                nmo_last_error_message_copy(error_msg, sizeof(error_msg));
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s', schema=%s): create failed: %s",
                        file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj),
                        schema_type->name ? schema_type->name : "<unnamed>",
                        error_msg);
            }

            object_system_destroy_state_layers(
                state, &deser_ctx, created_layers, created_count);
            object_system_clear_failed_object_state(obj);
            nmo_chunk_close(obj->chunk);
            continue;
        }

        if (!has_chunk_data) {
            (void)nmo_object_set_data(obj, state);
            stats.deserialized++;

            if (reference_resolver != NULL) {
                object_system_ref_capture_ctx_t ref_ctx = {
                    .resolver = reference_resolver,
                    .repo = repo,
                    .source = obj,
                    .logger = logger
                };

                nmo_status_t ref_result = nmo_ref_enumerate_object(
                    type_rt->types,
                    obj,
                    object_system_capture_ref,
                    &ref_ctx);

                if (ref_result != NMO_OK && logger != NULL) {
                    nmo_log(logger, NMO_LOG_WARN,
                            "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): reference enumeration failed: %d",
                            file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), ref_result);
                }
            }

            nmo_chunk_close(obj->chunk);
            continue;
        }

        nmo_status_t read_result = nmo_chunk_start_read(obj->chunk);
        if (read_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): failed to start chunk read: %d",
                        file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), read_result);
            }
            object_system_destroy_state_layers(
                state, &deser_ctx, created_layers, created_count);
            object_system_clear_failed_object_state(obj);
            nmo_chunk_close(obj->chunk);
            continue;
        }

        nmo_status_t deser_result = schema_type->vtable->deserialize(
            state, obj->chunk, schema_type, &deser_ctx);

        if (deser_result == NMO_OK) {
            (void)nmo_object_set_data(obj, state);
            stats.deserialized++;

            if (reference_resolver != NULL) {
                object_system_ref_capture_ctx_t ref_ctx = {
                    .resolver = reference_resolver,
                    .repo = repo,
                    .source = obj,
                    .logger = logger
                };

                nmo_status_t ref_result = nmo_ref_enumerate_object(
                    type_rt->types,
                    obj,
                    object_system_capture_ref,
                    &ref_ctx);

                if (ref_result != NMO_OK && logger != NULL) {
                    nmo_log(logger, NMO_LOG_WARN,
                            "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): reference enumeration failed: %d",
                            file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), ref_result);
                }
            }

            if ((deser_flags & NMO_DESER_FLAG_PRESERVE_RAW) != 0 && shadow_storage != NULL) {
                size_t data_size = nmo_chunk_get_data_size(obj->chunk);
                size_t pos_dwords = nmo_chunk_get_position(obj->chunk);
                size_t pos_bytes = pos_dwords * sizeof(uint32_t);

                if (pos_bytes < data_size) {
                    size_t tail_size = data_size - pos_bytes;
                    size_t buffer_size = 0;
                    const uint8_t *data = (const uint8_t *)nmo_chunk_get_data(obj->chunk, &buffer_size);

                    if (data != NULL && pos_bytes + tail_size <= buffer_size) {
                        int tail_result = nmo_shadow_capture_chunk_tail(
                            shadow_storage, obj->id, data + pos_bytes, tail_size);
                        if (tail_result != NMO_OK && logger) {
                            nmo_log(logger, NMO_LOG_WARN,
                                    "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): failed to capture chunk tail in shadow (code=%d)",
                                    file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), tail_result);
                        }
                    }
                }
            }

            nmo_chunk_close(obj->chunk);
        } else {
            stats.errors++;
            if (logger) {
                char error_msg[1024];
                nmo_last_error_message_copy(error_msg, sizeof(error_msg));
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object file_index=%zu (ID=%u, file_id=%u, class=0x%08X, name='%s', schema=%s): deserialization failed: %s",
                        file_index, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj),
                        schema_type->name ? schema_type->name : "<unnamed>",
                        error_msg);
            }

            object_system_destroy_state_layers(
                state, &deser_ctx, created_layers, created_count);
            object_system_clear_failed_object_state(obj);

            nmo_chunk_close(obj->chunk);
        }
    }

    if (out_stats != NULL) {
        *out_stats = stats;
    }

    return NMO_OK;
}
