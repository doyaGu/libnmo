/**
 * @file object_system.c
 * @brief Object-layer serialization/deserialization core.
 */

#include "object/nmo_object_system.h"

#include "object/nmo_object_repository.h"
#include "object/nmo_shadow_storage.h"

#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"

#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"

#include "type/nmo_type_system.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_runtime.h"

#include "core/nmo_logger.h"
#include "core/nmo_error.h"

#include <string.h>

static const char *object_system_name_or_default(const nmo_object_t *obj) {
    return (obj != NULL && obj->name != NULL && obj->name[0] != '\0')
        ? obj->name
        : "(null)";
}

typedef struct object_system_created_layer {
    const nmo_type_descriptor_t *type;
    uint32_t offset;
} object_system_created_layer_t;

#define OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH 64u

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
        schema_type->ext->hierarchy_depth == 0) {
        layers[0].type = schema_type;
        layers[0].offset = 0;
        return 1;
    }

    if (schema_type->ext->hierarchy_depth > layer_cap) {
        /* Hierarchy too deep for static buffer; caller should log warning */
        return 0;
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
        out_created_layers == NULL || out_created_cap == 0 ||
        out_created_count == NULL) {
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
    const object_system_created_layer_t *created_layers,
    size_t created_count)
{
    if (state == NULL || deser_ctx == NULL || created_layers == NULL || created_count == 0) {
        return;
    }

    for (size_t idx = created_count; idx > 0; --idx) {
        const size_t i = idx - 1;
        const nmo_type_descriptor_t *layer_type = created_layers[i].type;
        if (layer_type == NULL || layer_type->vtable == NULL ||
            layer_type->vtable->destroy == NULL) {
            continue;
        }

        uint8_t *layer_state = (uint8_t *)state + created_layers[i].offset;
        layer_type->vtable->destroy(layer_state, layer_type, deser_ctx);
    }
}

static void object_system_clear_failed_object_state(nmo_object_t *obj)
{
    if (obj != NULL) {
        (void)nmo_object_alloc_state(obj, 0);
        nmo_arena_t *storage_arena = nmo_object_get_storage_arena(obj);
        if (storage_arena != NULL) {
            nmo_arena_reset(storage_arena);
        }
    }
}


nmo_status_t nmo_object_system_deserialize_repository(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    nmo_shadow_storage_t *shadow_storage,
    uint32_t deser_flags,
    nmo_object_system_deserialize_stats_t *out_stats)
{
    if (repo == NULL || type_rt == NULL || type_rt->types == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_system_deserialize_stats_t stats = {0};

    size_t repo_count = nmo_object_repository_get_count(repo);

    nmo_deserialize_context_t deser_ctx = nmo_deserialize_context_create(
        arena, repo, type_rt, deser_flags);

    for (size_t i = 0; i < repo_count; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);

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
                        "  Object %zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): chunk has no data buffer, deserializing defaults",
                        i, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj));
            }
        }

        const nmo_type_descriptor_t *schema_type =
            nmo_type_query_find_for_object(type_rt->types, obj);

        if (schema_type == NULL || schema_type->vtable == NULL ||
            schema_type->vtable->deserialize == NULL) {
            stats.no_schema++;
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object %zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): no readable schema found",
                        i, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj));
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
                        "  Object %zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): failed to allocate %u bytes for state",
                        i, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), state_size);
            }
            nmo_chunk_close(obj->chunk);
            if (out_stats != NULL) {
                *out_stats = stats;
            }
            if (alloc_result == NMO_ERR_NOMEM || alloc_result == NMO_ERR_INTERNAL) {
                return alloc_result;
            }
            continue;
        }

        void *state = nmo_object_get_state(obj);
        nmo_deserialize_context_set_object(&deser_ctx, obj);
        deser_ctx.chunk_version = nmo_chunk_get_data_version(obj->chunk);

        object_system_created_layer_t create_plan[OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH];
        object_system_created_layer_t created_layers[OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH];
        size_t plan_count = object_system_build_create_plan(
            schema_type, create_plan, OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH);

        if (plan_count == 0) {
            /* Hierarchy too deep; fall back to single-layer */
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object %zu (schema=%s): hierarchy depth exceeds %u, using single-layer fallback",
                        i, schema_type->name ? schema_type->name : "<unnamed>",
                        (unsigned)OBJECT_SYSTEM_MAX_HIERARCHY_DEPTH);
            }
            create_plan[0].type = schema_type;
            create_plan[0].offset = 0;
            plan_count = 1;
        }

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
                        "  Object %zu (ID=%u, file_id=%u, class=0x%08X, name='%s', schema=%s): create failed: %s",
                        i, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj),
                        schema_type->name ? schema_type->name : "<unnamed>",
                        error_msg);
            }

            object_system_destroy_state_layers(
                state, &deser_ctx, created_layers, created_count);
            object_system_clear_failed_object_state(obj);
            nmo_chunk_close(obj->chunk);
            if (create_result == NMO_ERR_NOMEM || create_result == NMO_ERR_INTERNAL) {
                if (out_stats != NULL) {
                    *out_stats = stats;
                }
                return create_result;
            }
            continue;
        }

        if (!has_chunk_data) {
            stats.deserialized++;
            nmo_chunk_close(obj->chunk);
            continue;
        }

        nmo_status_t read_result = nmo_chunk_start_read(obj->chunk);
        if (read_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object %zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): failed to start chunk read: %d",
                        i, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), read_result);
            }
            object_system_destroy_state_layers(
                state, &deser_ctx, created_layers, created_count);
            object_system_clear_failed_object_state(obj);
            nmo_chunk_close(obj->chunk);
            if (read_result == NMO_ERR_NOMEM ||
                read_result == NMO_ERR_INTERNAL) {
                if (out_stats != NULL) {
                    *out_stats = stats;
                }
                return read_result;
            }
            continue;
        }

        nmo_status_t deser_result = schema_type->vtable->deserialize(
            state, obj->chunk, schema_type, &deser_ctx);

        if (deser_result == NMO_OK) {
            stats.deserialized++;

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
                        if (tail_result != NMO_OK) {
                            if (logger) {
                                nmo_log(logger, NMO_LOG_ERROR,
                                        "  Object %zu (ID=%u, file_id=%u, class=0x%08X, name='%s'): failed to capture chunk tail in shadow (code=%d)",
                                        i, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj), tail_result);
                            }
                            nmo_chunk_close(obj->chunk);
                            if (out_stats != NULL) {
                                *out_stats = stats;
                            }
                            return tail_result;
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
                        "  Object %zu (ID=%u, file_id=%u, class=0x%08X, name='%s', schema=%s): deserialization failed: %s",
                        i, obj->id, obj->file_id, obj->class_id, object_system_name_or_default(obj),
                        schema_type->name ? schema_type->name : "<unnamed>",
                        error_msg);
            }

            object_system_destroy_state_layers(
                state, &deser_ctx, created_layers, created_count);

            object_system_clear_failed_object_state(obj);

            nmo_chunk_close(obj->chunk);
            if (deser_result == NMO_ERR_NOMEM || deser_result == NMO_ERR_INTERNAL) {
                if (out_stats != NULL) {
                    *out_stats = stats;
                }
                return deser_result;
            }
        }
    }

    if (out_stats != NULL) {
        *out_stats = stats;
    }

    return NMO_OK;
}

nmo_chunk_t *nmo_object_system_serialize_object_chunk(
    nmo_object_t *obj,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_arena_t *scratch,
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage,
    const nmo_chunk_file_context_t *file_ctx,
    nmo_status_t *out_status)
{
    if (out_status != NULL) {
        *out_status = NMO_ERR_INVALID_ARGUMENT;
    }
    if (obj == NULL || arena == NULL || type_rt == NULL || type_rt->types == NULL) {
        return NULL;
    }

    if (out_status != NULL) {
        *out_status = NMO_OK;
    }

    void *state = nmo_object_get_state(obj);

    if (obj->chunk != NULL && state == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_DEBUG,
                    "    Reusing existing chunk for object %u (unmodified)", obj->id);
        }
        return obj->chunk;
    }

    const nmo_type_descriptor_t *schema_type =
        nmo_type_query_find_for_object(type_rt->types, obj);

    if (schema_type == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_WARN,
                    "    No schema found for class 0x%08X, preserving raw chunk", obj->class_id);
        }
        return obj->chunk;
    }

    if (schema_type->vtable == NULL || schema_type->vtable->serialize == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_WARN,
                    "    Schema '%s' has no write vtable, preserving raw chunk",
                    schema_type->name ? schema_type->name : "<unnamed>");
        }
        return obj->chunk;
    }

    if (state == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_WARN,
                    "    Object %u has no state to serialize", obj->id);
        }

        if (obj->chunk == NULL) {
            nmo_chunk_t *empty_chunk = nmo_chunk_create(arena);
            if (empty_chunk != NULL) {
                empty_chunk->class_id = obj->class_id;
                empty_chunk->chunk_version = NMO_CHUNK_VERSION4;
                empty_chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
                empty_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
                if (file_ctx != NULL) {
                    nmo_chunk_set_file_context(empty_chunk, file_ctx);
                }
                nmo_status_t empty_result = nmo_chunk_start_write(empty_chunk);
                if (empty_result != NMO_OK) {
                    if (out_status != NULL) {
                        *out_status = empty_result;
                    }
                    return NULL;
                }
                nmo_chunk_close(empty_chunk);
                return empty_chunk;
            }
            if (out_status != NULL) {
                *out_status = NMO_ERR_NOMEM;
            }
        }

        return obj->chunk;
    }

    nmo_chunk_t *new_chunk = nmo_chunk_create(arena);
    if (new_chunk == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "    Failed to create chunk for object %u", obj->id);
        }
        if (out_status != NULL) {
            *out_status = NMO_ERR_NOMEM;
        }
        return NULL;
    }

    const nmo_chunk_t *old_chunk = obj->chunk;

    new_chunk->class_id = obj->class_id;
    if (old_chunk != NULL) {
        new_chunk->chunk_version = old_chunk->chunk_version;
        new_chunk->data_version = old_chunk->data_version;
        new_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    } else {
        new_chunk->chunk_version = NMO_CHUNK_VERSION4;
        new_chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
        new_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    }
    if (file_ctx != NULL) {
        nmo_chunk_set_file_context(new_chunk, file_ctx);
    }

    nmo_status_t result = nmo_chunk_start_write(new_chunk);
    if (result != NMO_OK) {
        if (logger) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "    Failed to start chunk write for object %u", obj->id);
        }
        if (out_status != NULL) {
            *out_status = result;
        }
        return NULL;
    }

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create_with_scratch(
        arena, scratch, repo, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    result = schema_type->vtable->serialize(state, new_chunk, schema_type, &ser_ctx);

    if (result != NMO_OK) {
        if (logger) {
            char error_msg[1024];
            nmo_last_error_message_copy(error_msg, sizeof(error_msg));
            nmo_log(logger, NMO_LOG_ERROR,
                    "    Failed to serialize object %u (class=0x%08X) with schema '%s': %s",
                    obj->id,
                    obj->class_id,
                    schema_type->name ? schema_type->name : "<unnamed>",
                    error_msg[0] ? error_msg : "<no error>");
        }
        /* Clear file_context on the orphaned new_chunk so it does not hold
           a dangling pointer into scratch-arena memory. */
        nmo_chunk_set_file_context(new_chunk, NULL);
        if (out_status != NULL) {
            *out_status = result;
        }
        return NULL;
    }

    if (shadow_storage != NULL) {
        size_t tail_size = 0;
        const void *tail = nmo_shadow_get_chunk_tail(shadow_storage, obj->id, &tail_size);
        if (tail != NULL && tail_size > 0) {
            nmo_status_t tail_result = nmo_chunk_write_buffer_no_size(new_chunk, tail, tail_size);
            if (tail_result != NMO_OK) {
                if (logger) {
                    nmo_log(logger, NMO_LOG_ERROR,
                            "    Failed to append shadow tail for object %u (code=%d)",
                            obj->id, tail_result);
                }
                nmo_chunk_set_file_context(new_chunk, NULL);
                if (out_status != NULL) {
                    *out_status = tail_result;
                }
                return NULL;
            }
        }
    }

    nmo_chunk_close(new_chunk);

    if (logger) {
        nmo_log(logger, NMO_LOG_DEBUG,
                "    Serialized object %u using schema '%s' (%zu bytes)",
                obj->id,
                schema_type->name ? schema_type->name : "<unnamed>",
                new_chunk->data.count * 4);
    }

    return new_chunk;
}
