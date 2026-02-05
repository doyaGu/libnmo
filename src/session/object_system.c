/**
 * @file object_system.c
 * @brief Session-layer object lifecycle orchestration.
 */

#include "session/nmo_object_system.h"

#include "session/nmo_object_repository.h"
#include "session/nmo_load_session.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_id_sanitizer.h"
#include "session/nmo_shadow_storage.h"

#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"

#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"

#include "type/nmo_type_system.h"

#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "core/nmo_error.h"

#include <string.h>
#include <stdalign.h>

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

nmo_status_t nmo_object_system_create_objects_from_header1(
    const nmo_allocator_t *object_allocator,
    nmo_arena_t *scratch_arena,
    nmo_object_repository_t *repo,
    nmo_id_sanitizer_t *id_sanitizer,
    nmo_load_session_t *load_session,
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

        uint32_t raw_id = desc->file_id;
        if (desc->flags & NMO_OBJECT_REFERENCE_FLAG) {
            raw_id |= NMO_ID_REF_MASK;
        }

        const int32_t signed_raw_id = (int32_t)raw_id;
        const int is_reference_only = (desc->flags & NMO_OBJECT_REFERENCE_FLAG) != 0;
        const int is_external_ref = (signed_raw_id < 0);

        if (id_sanitizer != NULL && is_external_ref) {
            int32_t runtime_ext = nmo_id_register_external(id_sanitizer, signed_raw_id);
            if (runtime_ext != (int32_t)NMO_OBJECT_ID_INVALID) {
                if (logger) {
                    nmo_log(logger, NMO_LOG_INFO,
                            "  Object %zu: external reference %d registered as runtime %d",
                            i, signed_raw_id, runtime_ext);
                }
            } else {
                if (logger) {
                    nmo_log(logger, NMO_LOG_WARN,
                            "  Object %zu: failed to register external reference %d",
                            i, signed_raw_id);
                }
            }
        }

        const uint32_t sanitized_id = nmo_id_sanitize(raw_id);

        nmo_object_t *obj = nmo_object_create(object_allocator, NMO_OBJECT_ID_NONE, desc->class_id);
        if (obj == NULL) {
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return NMO_ERR_NOMEM;
        }

        obj->flags = desc->flags;
        if (is_external_ref && (obj->flags & NMO_OBJECT_REFERENCE_FLAG) == 0) {
            obj->flags |= NMO_OBJECT_REFERENCE_FLAG;
        }
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

        int add_result = nmo_object_repository_add(repo, obj);
        if (add_result != NMO_OK) {
            nmo_object_destroy(obj);
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return add_result;
        }

        /* Repository owns obj beyond this point. */
        created[i] = obj;

        int file_index_result = nmo_object_set_file_index(obj, desc->file_index);
        if (file_index_result != NMO_OK) {
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return file_index_result;
        }

        int reg_result = nmo_load_session_register(load_session, obj, (nmo_object_id_t)i);
        if (reg_result != NMO_OK) {
            object_system_rollback_created(repo, created, desc_count);
            if (id_sanitizer != NULL) {
                nmo_id_sanitizer_reset(id_sanitizer);
            }
            return reg_result;
        }

        if (id_sanitizer != NULL && signed_raw_id >= 0 && sanitized_id != 0) {
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
            } else if (is_external_ref) {
                nmo_log(logger, NMO_LOG_INFO,
                        "  Created external reference object %zu: file_id=%u, file_object_index=%u, runtime_id=%u, class=0x%08X, name='%s'",
                        i, raw_id, (uint32_t)i, obj->id, obj->class_id, obj->name ? obj->name : "(null)");
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

nmo_status_t nmo_object_system_deserialize_repository(
    nmo_object_repository_t *repo,
    nmo_type_registry_t *type_reg,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    nmo_shadow_storage_t *shadow_storage,
    uint32_t deser_flags,
    nmo_object_system_deserialize_stats_t *out_stats)
{
    if (repo == NULL || type_reg == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_system_deserialize_stats_t stats = {0};

    size_t repo_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &repo_count);

    if (repo_count > 0 && objects == NULL) {
        return NMO_ERR_NOMEM;
    }

    nmo_deserialize_context_t deser_ctx = nmo_deserialize_context_create(
        arena, repo, type_reg, deser_flags);

    for (size_t i = 0; i < repo_count; i++) {
        nmo_object_t *obj = objects[i];

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
        if (chunk_buffer == NULL || chunk_buffer_size == 0) {
            stats.skipped_empty_chunk++;
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object %zu (ID=%u): chunk has no data buffer, skipping",
                        i, obj->id);
            }
            continue;
        }

        nmo_status_t read_result = nmo_chunk_start_read(obj->chunk);
        if (read_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object %zu (ID=%u): failed to start chunk read: %d",
                        i, obj->id, read_result);
            }
            continue;
        }

        const nmo_type_descriptor_t *schema_type =
            nmo_type_registry_find_by_class_id_inherited(type_reg, obj->class_id);

        if (schema_type == NULL || schema_type->vtable == NULL ||
            schema_type->vtable->deserialize == NULL) {
            stats.no_schema++;
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object %zu (ID=%u, class=0x%08X): no readable schema found",
                        i, obj->id, obj->class_id);
            }
            nmo_chunk_close(obj->chunk);
            continue;
        }

        uint32_t state_size = schema_type->total_state_size > 0 ?
                              schema_type->total_state_size : schema_type->size;

        nmo_status_t alloc_result = nmo_object_alloc_state(obj, state_size);
        if (alloc_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object %zu (ID=%u): failed to allocate %u bytes for state",
                        i, obj->id, state_size);
            }
            nmo_chunk_close(obj->chunk);
            continue;
        }

        void *state = nmo_object_get_state(obj);
        nmo_deserialize_context_set_object(&deser_ctx, obj);
        deser_ctx.chunk_version = nmo_chunk_get_data_version(obj->chunk);

        if (schema_type->vtable->create != NULL) {
            nmo_status_t create_result = schema_type->vtable->create(state, schema_type, &deser_ctx);
            if (create_result != NMO_OK) {
                stats.errors++;
                if (logger) {
                    char error_msg[1024];
                    nmo_last_error_message_copy(error_msg, sizeof(error_msg));
                    nmo_log(logger, NMO_LOG_ERROR,
                            "  Object %zu (ID=%u, class=0x%08X, schema=%s): create failed: %s",
                            i, obj->id, obj->class_id, schema_type->name ? schema_type->name : "<unnamed>",
                            error_msg);
                }

                if (schema_type->vtable->destroy != NULL) {
                    schema_type->vtable->destroy(state, schema_type, &deser_ctx);
                }

                nmo_chunk_close(obj->chunk);
                continue;
            }
        }

        nmo_status_t deser_result = schema_type->vtable->deserialize(
            state, obj->chunk, schema_type, &deser_ctx);

        if (deser_result == NMO_OK) {
            (void)nmo_object_set_data(obj, state);
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
                        if (tail_result != NMO_OK && logger) {
                            nmo_log(logger, NMO_LOG_WARN,
                                    "  Object %zu (ID=%u): failed to capture chunk tail in shadow (code=%d)",
                                    i, obj->id, tail_result);
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
                        "  Object %zu (ID=%u, class=0x%08X, schema=%s): deserialization failed: %s",
                        i, obj->id, obj->class_id, schema_type->name ? schema_type->name : "<unnamed>",
                        error_msg);
            }

            if (schema_type->vtable->destroy != NULL) {
                schema_type->vtable->destroy(state, schema_type, &deser_ctx);
            }

            nmo_chunk_close(obj->chunk);
        }
    }

    if (out_stats != NULL) {
        *out_stats = stats;
    }

    return NMO_OK;
}

nmo_chunk_t *nmo_object_system_serialize_object_chunk(
    nmo_object_t *obj,
    nmo_type_registry_t *type_reg,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage,
    const nmo_chunk_file_context_t *file_ctx)
{
    if (obj == NULL || arena == NULL || type_reg == NULL) {
        return NULL;
    }

    if (obj->chunk != NULL && obj->data == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_DEBUG,
                    "    Reusing existing chunk for object %u (unmodified)", obj->id);
        }
        return obj->chunk;
    }

    const nmo_type_descriptor_t *schema_type =
        nmo_type_registry_find_by_class_id_inherited(type_reg, obj->class_id);

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

    if (obj->data == NULL) {
        if (logger) {
            nmo_log(logger, NMO_LOG_WARN,
                    "    Object %u has no data to serialize", obj->id);
        }

        if (obj->chunk == NULL) {
            nmo_chunk_t *empty_chunk = nmo_chunk_create(arena);
            if (empty_chunk != NULL) {
                empty_chunk->class_id = obj->class_id;
                empty_chunk->chunk_version = 7;
                empty_chunk->data_version = 7;
                empty_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
                if (file_ctx != NULL) {
                    nmo_chunk_set_file_context(empty_chunk, file_ctx);
                }
                nmo_chunk_start_write(empty_chunk);
                nmo_chunk_close(empty_chunk);
                return empty_chunk;
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
        return NULL;
    }

    const nmo_chunk_t *old_chunk = obj->chunk;

    new_chunk->class_id = obj->class_id;
    if (old_chunk != NULL) {
        new_chunk->chunk_version = old_chunk->chunk_version;
        new_chunk->data_version = old_chunk->data_version;
        new_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    } else {
        new_chunk->chunk_version = 7;
        new_chunk->data_version = 7;
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
        return NULL;
    }

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE);

    result = schema_type->vtable->serialize(obj->data, new_chunk, schema_type, &ser_ctx);

    if (result != NMO_OK) {
        if (logger) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "    Failed to serialize object %u with schema '%s'",
                    obj->id, schema_type->name ? schema_type->name : "<unnamed>");
        }
        return obj->chunk;
    }

    if (shadow_storage != NULL) {
        size_t tail_size = 0;
        const void *tail = nmo_shadow_get_chunk_tail(shadow_storage, obj->id, &tail_size);
        if (tail != NULL && tail_size > 0) {
            nmo_status_t tail_result = nmo_chunk_write_buffer_no_size(new_chunk, tail, tail_size);
            if (tail_result != NMO_OK && logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "    Failed to append shadow tail for object %u (code=%d)",
                        obj->id, tail_result);
            }
        }
    }

    nmo_chunk_close(new_chunk);

    if (old_chunk != NULL && new_chunk->data.count == 0) {
        if (logger) {
            nmo_log(logger, NMO_LOG_WARN,
                    "    Serialized object %u is empty; preserving original chunk", obj->id);
        }
        return (nmo_chunk_t *)old_chunk;
    }

    if (logger) {
        nmo_log(logger, NMO_LOG_DEBUG,
                "    Serialized object %u using schema '%s' (%zu bytes)",
                obj->id,
                schema_type->name ? schema_type->name : "<unnamed>",
                new_chunk->data.count * 4);
    }

    return new_chunk;
}

nmo_status_t nmo_object_system_prepare_loaded_objects(
    const nmo_allocator_t *object_allocator,
    nmo_arena_t *scratch_arena,
    nmo_object_repository_t *repo,
    nmo_id_sanitizer_t *id_sanitizer,
    nmo_load_session_t *load_session,
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
    nmo_type_registry_t *type_reg,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    nmo_shadow_storage_t *shadow_storage,
    uint32_t deser_flags,
    const nmo_load_session_t *load_session,
    size_t file_object_count,
    nmo_object_system_deserialize_stats_t *out_stats)
{
    if (repo == NULL || type_reg == NULL || arena == NULL || load_session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_system_deserialize_stats_t stats = {0};

    nmo_deserialize_context_t deser_ctx = nmo_deserialize_context_create(
        arena, repo, type_reg, deser_flags);

    for (size_t file_index = 0; file_index < file_object_count; file_index++) {
        nmo_object_id_t runtime_id = NMO_OBJECT_ID_INVALID;
        int id_result = nmo_load_session_get_runtime_id(load_session,
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
        if (chunk_buffer == NULL || chunk_buffer_size == 0) {
            stats.skipped_empty_chunk++;
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object file_index=%zu (ID=%u): chunk has no data buffer, skipping",
                        file_index, obj->id);
            }
            continue;
        }

        nmo_status_t read_result = nmo_chunk_start_read(obj->chunk);
        if (read_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object file_index=%zu (ID=%u): failed to start chunk read: %d",
                        file_index, obj->id, read_result);
            }
            continue;
        }

        const nmo_type_descriptor_t *schema_type =
            nmo_type_registry_find_by_class_id_inherited(type_reg, obj->class_id);

        if (schema_type == NULL || schema_type->vtable == NULL ||
            schema_type->vtable->deserialize == NULL) {
            stats.no_schema++;
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Object file_index=%zu (ID=%u, class=0x%08X): no readable schema found",
                        file_index, obj->id, obj->class_id);
            }
            nmo_chunk_close(obj->chunk);
            continue;
        }

        uint32_t state_size = schema_type->total_state_size > 0 ?
                              schema_type->total_state_size : schema_type->size;

        nmo_status_t alloc_result = nmo_object_alloc_state(obj, state_size);
        if (alloc_result != NMO_OK) {
            stats.errors++;
            if (logger) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "  Object file_index=%zu (ID=%u): failed to allocate %u bytes for state",
                        file_index, obj->id, state_size);
            }
            nmo_chunk_close(obj->chunk);
            continue;
        }

        void *state = nmo_object_get_state(obj);
        nmo_deserialize_context_set_object(&deser_ctx, obj);
        deser_ctx.chunk_version = nmo_chunk_get_data_version(obj->chunk);

        if (schema_type->vtable->create != NULL) {
            nmo_status_t create_result = schema_type->vtable->create(state, schema_type, &deser_ctx);
            if (create_result != NMO_OK) {
                stats.errors++;
                if (logger) {
                    char error_msg[1024];
                    nmo_last_error_message_copy(error_msg, sizeof(error_msg));
                    nmo_log(logger, NMO_LOG_ERROR,
                            "  Object file_index=%zu (ID=%u, class=0x%08X, schema=%s): create failed: %s",
                            file_index, obj->id, obj->class_id,
                            schema_type->name ? schema_type->name : "<unnamed>",
                            error_msg);
                }

                if (schema_type->vtable->destroy != NULL) {
                    schema_type->vtable->destroy(state, schema_type, &deser_ctx);
                }

                nmo_chunk_close(obj->chunk);
                continue;
            }
        }

        nmo_status_t deser_result = schema_type->vtable->deserialize(
            state, obj->chunk, schema_type, &deser_ctx);

        if (deser_result == NMO_OK) {
            (void)nmo_object_set_data(obj, state);
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
                        if (tail_result != NMO_OK && logger) {
                            nmo_log(logger, NMO_LOG_WARN,
                                    "  Object file_index=%zu (ID=%u): failed to capture chunk tail in shadow (code=%d)",
                                    file_index, obj->id, tail_result);
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
                        "  Object file_index=%zu (ID=%u, class=0x%08X, schema=%s): deserialization failed: %s",
                        file_index, obj->id, obj->class_id,
                        schema_type->name ? schema_type->name : "<unnamed>",
                        error_msg);
            }

            if (schema_type->vtable->destroy != NULL) {
                schema_type->vtable->destroy(state, schema_type, &deser_ctx);
            }

            nmo_chunk_close(obj->chunk);
        }
    }

    if (out_stats != NULL) {
        *out_stats = stats;
    }

    return NMO_OK;
}
