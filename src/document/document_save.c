/**
 * @file save_pipeline.c
 * @brief Two-phase commit save pipeline implementation (Phase 1.4)
 *
 * This module implements the two-phase commit architecture for saving Virtools files:
 *
 * Phase 1 (Layout & Serialize):
 *   - Validates session state
 *   - Executes manager pre-save hooks
 *   - Builds ID remap plan (runtime -> file)
 *   - Serializes managers and objects to memory
 *   - Builds Header1 with exact sizes
 *
 * Phase 2 (Pack & Commit):
 *   - Optionally compresses Header1 and Data sections
 *   - Builds File Header with CRC
 *   - Writes atomically to output file
 *   - Executes manager post-save hooks
 */

#include "document/nmo_document_save.h"
#include "nmo_save_buffer.h"
#include "../runtime/runtime_internal.h"
#include "extension/nmo_extension_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "core/nmo_path.h"
#include "core/nmo_utils.h"
#include "core/nmo_guid.h"
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_memory.h"
#include "io/nmo_txn.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_system.h"
#include "object/nmo_shadow_storage.h"
#include "format/nmo_chunk_context.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include <limits.h>
#include <stdio.h>
#include "miniz.h"

#define DEFAULT_COMPRESSION_LEVEL 6
#define DEFAULT_BUFFER_CAPACITY (64 * 1024)  /* 64 KB initial */

/* Helper macro for returning error results (expression form for `return SAVE_ERR(...)`) */
#define SAVE_ERR(code, msg) \
    (nmo_last_error_setf((code), NMO_SEVERITY_ERROR, __FILE__, __LINE__, "%s", (msg)), \
     (nmo_status_t)(code))

static nmo_status_t save_txn_write_u32_le(nmo_txn_handle_t *txn, uint32_t value) {
    uint8_t encoded[4];
    nmo_write_u32_le(encoded, value);
    return nmo_txn_write(txn, encoded, sizeof(encoded));
}

static nmo_txn_durability_t save_txn_durability_from_options(
    nmo_save_durability_t durability)
{
    switch (durability) {
        case NMO_SAVE_DURABILITY_FAST:
            return NMO_TXN_NONE;
        case NMO_SAVE_DURABILITY_FSYNC:
        case NMO_SAVE_DURABILITY_DEFAULT:
        default:
            return NMO_TXN_FSYNC;
    }
}

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * @brief Save context internal structure
 *
 * Holds all intermediate state between Phase 1 and Phase 2.
 */
struct nmo_serializer {
    /* Input references (borrowed) */
    nmo_session_t *session;
    nmo_context_t *context;
    nmo_arena_t *arena;
    nmo_arena_t *scratch;             /**< Scratch arena for save-only temporaries */
    nmo_object_repository_t *repo;
    nmo_logger_t *logger;
    nmo_type_registry_t *type_reg;
    const nmo_type_runtime_t *type_rt;
    nmo_manager_registry_t *manager_reg;

    /* Options */
    nmo_save_options_t options;

    /* Phase 1 outputs: Uncompressed buffers */
    void *header1_buffer;         /**< Serialized Header1 (uncompressed) */
    size_t header1_unpack_size;
    void *data_buffer;            /**< Serialized Data section (uncompressed) */
    size_t data_unpack_size;

    /* Phase 1 outputs: Object info */
    nmo_object_t **objects;
    size_t object_count;
    uint8_t *reference_map;       /**< 1 = save as reference only */
    nmo_object_desc_t *obj_descs;
    nmo_save_id_remap_plan_t *remap_plan;
    nmo_id_remap_t *file_index_remap;
    nmo_chunk_file_context_t *chunk_file_ctx;

    /* Phase 1 outputs: Manager info */
    nmo_manager_data_t *manager_entries;
    uint32_t manager_entry_count;

    /* Phase 1 outputs: Plugin dependencies */
    nmo_plugin_dep_t *plugin_deps;
    size_t plugin_count;

    /* Phase 1 outputs: File info snapshot */
    nmo_file_info_t file_info;

    /* Phase 2 outputs: Compressed data (may alias uncompressed if no gain) */
    void *header1_packed;
    uint32_t header1_pack_size;
    void *data_packed;
    uint32_t data_pack_size;

    /* Statistics */
    nmo_save_stats_t stats;
    nmo_save_perf_stats_t local_perf_stats;
    nmo_save_perf_stats_t *perf_stats;

    /* Phase tracking */
    int phase1_complete;
    int phase2_complete;
};

static inline nmo_arena_t *save_scratch(const nmo_serializer_t *ctx) {
    return ctx->scratch;
}

static uint64_t save_perf_begin(const nmo_serializer_t *ctx) {
    return (ctx != NULL && ctx->perf_stats != NULL) ? nmo_perf_now_ticks() : 0u;
}

static void save_perf_end(const nmo_serializer_t *ctx,
                          nmo_save_perf_phase_t phase,
                          uint64_t start_ticks) {
    if (ctx == NULL || ctx->perf_stats == NULL) {
        return;
    }
    uint64_t end_ticks = nmo_perf_now_ticks();
    nmo_save_perf_stats_record(ctx->perf_stats, phase,
                               nmo_perf_elapsed_ms(start_ticks, end_ticks));
}

/* ============================================================================
 * Forward Declarations (Internal Functions)
 * ============================================================================ */

static nmo_status_t save_validate_session(nmo_serializer_t *ctx);
static nmo_status_t save_execute_pre_hooks(nmo_serializer_t *ctx);
static nmo_status_t save_build_remap_plan(nmo_serializer_t *ctx);
static nmo_status_t save_serialize_managers(nmo_serializer_t *ctx);
static nmo_status_t save_serialize_objects(nmo_serializer_t *ctx);
static nmo_status_t save_build_data_section(nmo_serializer_t *ctx);
static nmo_status_t save_build_header1(nmo_serializer_t *ctx);
static nmo_status_t save_get_chunk_size(nmo_chunk_t *chunk, nmo_arena_t *arena, size_t *out_size);
static nmo_status_t save_compute_manager_data_size(nmo_serializer_t *ctx, size_t *out_size);
static nmo_status_t save_fill_file_indices(nmo_serializer_t *ctx,
                                           size_t header1_unpack_size,
                                           uint32_t file_version);

static nmo_status_t save_compress_sections(nmo_serializer_t *ctx);
static nmo_status_t save_write_file(nmo_serializer_t *ctx, const char *path);
static nmo_status_t save_execute_post_hooks(nmo_serializer_t *ctx);

static nmo_chunk_t *serialize_object_with_schema(
    nmo_object_t *obj,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_arena_t *scratch,
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage,
    const nmo_chunk_file_context_t *file_ctx,
    int require_schema,
    nmo_status_t *out_status);

static int should_save_as_reference(const nmo_object_t *obj, uint32_t flags);
static void save_clear_chunk_file_context(nmo_chunk_t *chunk);
static nmo_status_t save_report_progress(nmo_serializer_t *ctx,
                                         nmo_serialize_phase_t phase,
                                         float progress,
                                         const char *status);
static void save_log_require_schema_failure(nmo_logger_t *logger,
                                             const nmo_object_t *obj,
                                             const char *reason);

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

nmo_save_options_t nmo_save_options_default(void) {
    nmo_save_options_t opts = {0};
    opts.flags = NMO_SAVE_DEFAULT;
    opts.durability = NMO_SAVE_DURABILITY_DEFAULT;
    opts.compress_header = true;
    opts.compress_data = true;
    opts.compute_crc = true;
    opts.validate_before_write = false;
    opts.compression_level = DEFAULT_COMPRESSION_LEVEL;
    opts.progress_fn = NULL;
    opts.progress_user_data = NULL;
    opts.allow_cancel = false;
    return opts;
}

static void save_log_require_schema_failure(nmo_logger_t *logger,
                                             const nmo_object_t *obj,
                                             const char *reason) {
    uint32_t obj_id = obj ? obj->id : 0;
    uint32_t class_id = obj ? obj->class_id : 0;
    uint32_t file_id = obj ? obj->file_id : 0;
    const char *name = (obj && obj->name) ? obj->name : "(null)";
    if (logger != NULL) {
        nmo_log(logger, NMO_LOG_ERROR,
                "%s (object %u file_id=%u class 0x%08X name='%s')",
                reason, obj_id, file_id, class_id, name);
    } else {
        fprintf(stderr, "[ERROR] %s (object %u file_id=%u class 0x%08X name='%s')\n",
                reason, obj_id, file_id, class_id, name);
    }
}

static nmo_status_t save_report_progress(nmo_serializer_t *ctx,
                                         nmo_serialize_phase_t phase,
                                         float progress,
                                         const char *status) {
    if (ctx == NULL || ctx->options.progress_fn == NULL) {
        NMO_RETURN_OK();
    }

    bool keep_going = ctx->options.progress_fn(
        ctx->options.progress_user_data,
        phase,
        progress,
        status);

    if (!keep_going && ctx->options.allow_cancel) {
        return SAVE_ERR(NMO_ERR_CANCELLED, "Save cancelled by callback");
    }

    NMO_RETURN_OK();
}

static nmo_status_t save_get_chunk_size(nmo_chunk_t *chunk, nmo_arena_t *arena, size_t *out_size) {
    if (out_size == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid chunk size output pointer");
    }

    *out_size = 0;
    if (chunk == NULL) {
        NMO_RETURN_OK();
    }

    if (chunk->raw_data != NULL && chunk->raw_size > 0) {
        *out_size = chunk->raw_size;
        NMO_RETURN_OK();
    }

    void *serialized = NULL;
    size_t serialized_size = 0;
    nmo_status_t result = nmo_chunk_serialize(chunk, &serialized, &serialized_size, arena);
    if (result != NMO_OK) {
        return result;
    }

    *out_size = serialized_size;
    NMO_RETURN_OK();
}

static nmo_status_t save_compute_manager_data_size(nmo_serializer_t *ctx, size_t *out_size) {
    if (ctx == NULL || out_size == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid manager size arguments");
    }

    size_t total = 0;
    for (uint32_t i = 0; i < ctx->manager_entry_count; i++) {
        const nmo_manager_data_t *mgr = &ctx->manager_entries[i];
        size_t chunk_size = 0;

        if (mgr->data_size > 0) {
            chunk_size = mgr->data_size;
        } else if (mgr->chunk != NULL) {
            NMO_RETURN_IF_ERROR(save_get_chunk_size(mgr->chunk, save_scratch(ctx), &chunk_size));
        }

        size_t entry_size = 0;
        if (!nmo_safe_add_size(8u, 4u, &entry_size) ||
            !nmo_safe_add_size(entry_size, chunk_size, &entry_size)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Manager data size overflow");
        }

        if (!nmo_safe_add_size(total, entry_size, &total)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Manager data size overflow");
        }
    }

    *out_size = total;
    NMO_RETURN_OK();
}

static nmo_status_t save_fill_file_indices(nmo_serializer_t *ctx,
                                           size_t header1_unpack_size,
                                           uint32_t file_version) {
    if (ctx == NULL || ctx->obj_descs == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid file index context");
    }

    size_t header_size = (file_version >= 5) ? 64u : 32u;
    size_t manager_data_size = 0;
    NMO_RETURN_IF_ERROR(save_compute_manager_data_size(ctx, &manager_data_size));

    size_t offset = 0;
    if (!nmo_safe_add_size(header_size, header1_unpack_size, &offset) ||
        !nmo_safe_add_size(offset, manager_data_size, &offset)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index base offset overflow");
    }

    for (size_t i = 0; i < ctx->object_count; i++) {
        nmo_object_t *obj = ctx->objects[i];
        size_t chunk_size = 0;
        if (obj != NULL) {
            NMO_RETURN_IF_ERROR(save_get_chunk_size(obj->chunk, save_scratch(ctx), &chunk_size));
        }

        if (offset > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index exceeds 32-bit range");
        }

        ctx->obj_descs[i].file_index = (nmo_object_id_t)offset;
        if (obj != NULL) {
            obj->file_index = ctx->obj_descs[i].file_index;
        }

        size_t entry_size = 0;
        size_t size_field_bytes = 4u;
        if (file_version < 7) {
            size_field_bytes += 4u; /* legacy object_id field */
        }
        if (!nmo_safe_add_size(size_field_bytes, chunk_size, &entry_size)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index entry size overflow");
        }
        if (!nmo_safe_add_size(offset, entry_size, &offset)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index overflow");
        }
    }

    NMO_RETURN_OK();
}


nmo_serializer_t *nmo_serializer_create(
    nmo_session_t *session,
    const nmo_save_options_t *options)
{
    if (session == NULL) {
        return NULL;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_arena_t *arena = nmo_session_get_arena(session);

    if (ctx == NULL || arena == NULL) {
        return NULL;
    }

    nmo_serializer_t *save_ctx = (nmo_serializer_t *)nmo_arena_alloc(
        arena, sizeof(nmo_serializer_t), alignof(nmo_serializer_t));

    if (save_ctx == NULL) {
        return NULL;
    }

    memset(save_ctx, 0, sizeof(nmo_serializer_t));

    /* Store borrowed references */
    save_ctx->session = session;
    save_ctx->context = ctx;
    save_ctx->arena = arena;
    save_ctx->repo = nmo_session_get_repository(session);
    save_ctx->logger = nmo_context_get_logger(ctx);
    save_ctx->type_reg = nmo_context_get_type_registry(ctx);
    save_ctx->type_rt = nmo_context_get_type_runtime(ctx);
    save_ctx->manager_reg = nmo_context_get_manager_registry(ctx);

    /* Store options */
    if (options != NULL) {
        save_ctx->options = *options;
    } else {
        save_ctx->options = nmo_save_options_default();
    }
    if (save_ctx->options.collect_perf_stats) {
        save_ctx->perf_stats = (save_ctx->options.perf_stats != NULL)
            ? save_ctx->options.perf_stats
            : &save_ctx->local_perf_stats;
        save_ctx->options.perf_stats = save_ctx->perf_stats;
    }

    /* Get file info from session */
    save_ctx->file_info = nmo_session_get_file_info(session);

    return save_ctx;
}

void nmo_serializer_destroy(nmo_serializer_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* Clean up remap plan if allocated */
    if (ctx->remap_plan != NULL) {
        nmo_save_id_remap_plan_destroy(ctx->remap_plan);
        ctx->remap_plan = NULL;
    }

    /* Note: All other allocations are arena-based, no explicit free needed */
}

nmo_status_t nmo_serializer_layout(nmo_serializer_t *ctx) {
    if (ctx == NULL) {
        return SAVE_ERR(NMO_ERR_INVALID_ARGUMENT, "NULL context");
    }
    if (nmo_session_is_partial_load(ctx->session)) {
        return SAVE_ERR(NMO_ERR_INVALID_STATE, "Cannot save partial-load session");
    }

    nmo_log(ctx->logger, NMO_LOG_INFO, "=== Save Pipeline Phase 1: Layout & Serialize ===");

    nmo_status_t result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 0.0f,
                                  "Validating session");
    if (result != NMO_OK) return result;

    /* Step 1.1: Validate session state */
    result = save_validate_session(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 0.1f,
                                  "Executing pre-save hooks");
    if (result != NMO_OK) return result;

    /* Step 1.2: Execute manager pre-save hooks */
    uint64_t pre_hooks_start = save_perf_begin(ctx);
    result = save_execute_pre_hooks(ctx);
    save_perf_end(ctx, NMO_SAVE_PERF_PRE_HOOKS, pre_hooks_start);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 0.2f,
                                  "Building ID remap plan");
    if (result != NMO_OK) return result;

    /* Step 1.3: Build ID remap plan */
    uint64_t remap_start = save_perf_begin(ctx);
    result = save_build_remap_plan(ctx);
    save_perf_end(ctx, NMO_SAVE_PERF_REMAP_PLAN, remap_start);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 0.35f,
                                  "Serializing managers");
    if (result != NMO_OK) return result;

    /* Step 1.4: Serialize managers */
    uint64_t managers_start = save_perf_begin(ctx);
    result = save_serialize_managers(ctx);
    save_perf_end(ctx, NMO_SAVE_PERF_MANAGER_SERIALIZE, managers_start);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 0.55f,
                                  "Serializing objects");
    if (result != NMO_OK) return result;

    /* Step 1.5: Serialize objects */
    uint64_t objects_start = save_perf_begin(ctx);
    result = save_serialize_objects(ctx);
    save_perf_end(ctx, NMO_SAVE_PERF_OBJECT_SERIALIZE, objects_start);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 0.75f,
                                  "Building data section");
    if (result != NMO_OK) return result;

    /* Step 1.6: Build data section buffer */
    result = save_build_data_section(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 0.9f,
                                  "Building header1 section");
    if (result != NMO_OK) return result;

    /* Step 1.7: Build header1 buffer */
    result = save_build_header1(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_SERIALIZE, 1.0f,
                                  "Phase 1 complete");
    if (result != NMO_OK) return result;

    ctx->phase1_complete = 1;

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "Phase 1 complete: header1=%zu bytes, data=%zu bytes, %zu objects",
            ctx->header1_unpack_size, ctx->data_unpack_size, ctx->object_count);

    NMO_RETURN_OK();
}

nmo_status_t nmo_serializer_commit(nmo_serializer_t *ctx, const char *path) {
    if (ctx == NULL || path == NULL) {
        return SAVE_ERR(NMO_ERR_INVALID_ARGUMENT, "NULL argument");
    }

    if (!ctx->phase1_complete) {
        return SAVE_ERR(NMO_ERR_INVALID_STATE, "Phase 1 not complete");
    }

    nmo_log(ctx->logger, NMO_LOG_INFO, "=== Save Pipeline Phase 2: Pack & Commit ===");

    nmo_status_t result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_COMPRESS, 0.0f,
                                  "Compressing sections");
    if (result != NMO_OK) return result;

    /* Step 2.1: Compress sections (optional) */
    result = save_compress_sections(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_COMPRESS, 1.0f,
                                  "Compression complete");
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_CRC, 0.0f,
                                  "Computing CRC");
    if (result != NMO_OK) return result;

    /* Step 2.2: Write to file with CRC */
    result = save_write_file(ctx, path);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_CRC, 1.0f,
                                  "CRC complete");
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_WRITE, 1.0f,
                                  "Write complete");
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_POST_HOOKS, 0.0f,
                                  "Executing post-save hooks");
    if (result != NMO_OK) return result;

    /* Step 2.3: Execute post-save hooks */
    uint64_t post_hooks_start = save_perf_begin(ctx);
    result = save_execute_post_hooks(ctx);
    save_perf_end(ctx, NMO_SAVE_PERF_POST_HOOKS, post_hooks_start);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SERIALIZE_PHASE_POST_HOOKS, 1.0f,
                                  "Post-save hooks complete");
    if (result != NMO_OK) return result;

    ctx->phase2_complete = 1;

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "Phase 2 complete: %zu bytes written to %s (CRC=0x%08X)",
            ctx->stats.total_file_size, path, ctx->stats.crc);

    NMO_RETURN_OK();
}

nmo_save_stats_t nmo_serializer_get_stats(const nmo_serializer_t *ctx) {
    if (ctx == NULL) {
        nmo_save_stats_t empty = {0};
        return empty;
    }
    return ctx->stats;
}

nmo_save_perf_stats_t nmo_serializer_get_perf_stats(const nmo_serializer_t *ctx) {
    if (ctx == NULL || ctx->perf_stats == NULL) {
        nmo_save_perf_stats_t empty = {0};
        return empty;
    }
    return *ctx->perf_stats;
}

nmo_status_t nmo_save_file(
    nmo_session_t *session,
    const char *path,
    const nmo_save_options_t *options)
{
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_session_is_partial_load(session)) {
        return NMO_ERR_INVALID_STATE;
    }

    /* Resolve compression settings before handing off to the two-phase
     * pipeline.  Priority (high to low):
     *   1. NMO_SAVE_COMPRESSED flag  - forces both sections compressed.
     *   2. Caller-supplied bools     - when opts != NULL and the flag is
     *                                  absent the compress_header /
     *                                  compress_data fields are used as-is.
     *   3. Session file_info         - when opts == NULL compression is
     *                                  inherited from the original file
     *                                  (round-trip safe).
     */
    nmo_save_options_t resolved;
    if (options == NULL) {
        resolved = nmo_save_options_default();

        /* Inherit compression from the session's original file */
        nmo_file_info_t fi = nmo_session_get_file_info(session);
        resolved.compress_header = (fi.write_mode & NMO_FILE_WRITE_COMPRESS_HEADER) != 0;
        resolved.compress_data   = (fi.write_mode & NMO_FILE_WRITE_COMPRESS_DATA)   != 0;
    } else {
        resolved = *options;
        if (resolved.flags & NMO_SAVE_COMPRESSED) {
            resolved.compress_header = true;
            resolved.compress_data   = true;
        }
    }

    nmo_serializer_t *ctx = nmo_serializer_create(session, &resolved);
    if (ctx == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Failed to create save context");
    }

    /* Create scratch arena for save-only temporaries.  All intermediate
       allocations (data buffers, header1, compression, remap backups, etc.)
       go here and are reclaimed in one shot when the arena is destroyed. */
    nmo_arena_t *scratch = nmo_arena_create(NULL, 256 * 1024);
    if (scratch == NULL) {
        nmo_serializer_destroy(ctx);
        return SAVE_ERR(NMO_ERR_NOMEM, "Failed to create save scratch arena");
    }
    ctx->scratch = scratch;

    nmo_status_t result = nmo_serializer_layout(ctx);
    if (result != NMO_OK) {
        nmo_arena_destroy(scratch);
        nmo_serializer_destroy(ctx);
        return result;
    }

    result = nmo_serializer_commit(ctx, path);

    nmo_arena_destroy(scratch);
    nmo_serializer_destroy(ctx);

    return result;
}

/* ============================================================================
 * Phase 1 Implementation: Layout & Serialize
 * ============================================================================ */

static nmo_status_t save_validate_session(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.1: Validating session state");

    if ((ctx->options.flags & NMO_SAVE_CHANGED_OBJECTS_ONLY) != 0u) {
        const uint32_t incompatible =
            NMO_SAVE_AS_OBJECTS | NMO_SAVE_SEQUENTIAL_IDS |
            NMO_SAVE_REQUIRE_SCHEMA;
        if ((ctx->options.flags & incompatible) != 0u ||
            ctx->options.include_ids != NULL) {
            return SAVE_ERR(
                NMO_ERR_INVALID_ARGUMENT,
                "Changed-object save cannot filter or remap object IDs");
        }
        if (ctx->options.changed_object_count > 0u &&
            ctx->options.changed_object_ids == NULL) {
            return SAVE_ERR(
                NMO_ERR_INVALID_ARGUMENT,
                "Changed-object save requires changed object IDs");
        }
        for (size_t i = 0; i < ctx->options.changed_object_count; ++i) {
            const nmo_object_id_t id = ctx->options.changed_object_ids[i];
            if ((i > 0u && id <= ctx->options.changed_object_ids[i - 1u]) ||
                nmo_object_repository_find_by_id(ctx->repo, id) == NULL) {
                return SAVE_ERR(
                    NMO_ERR_INVALID_ARGUMENT,
                    "Changed object IDs must be unique, sorted, and present");
            }
        }
    }

    /* Apply object filter if specified */
    if (ctx->options.include_ids != NULL && ctx->options.include_count > 0) {
        size_t cap = ctx->options.include_count;
        ctx->objects = (nmo_object_t **)nmo_arena_alloc(
            save_scratch(ctx), cap * sizeof(nmo_object_t *), _Alignof(nmo_object_t *));
        if (ctx->objects == NULL) {
            return SAVE_ERR(NMO_ERR_NOMEM, "Object filter list allocation failed");
        }
        ctx->object_count = 0;
        for (size_t i = 0; i < cap; i++) {
            nmo_object_t *obj = nmo_object_repository_find_by_id(
                ctx->repo, ctx->options.include_ids[i]);
            if (obj != NULL) {
                ctx->objects[ctx->object_count++] = obj;
            }
        }
    } else {
        ctx->objects = nmo_object_repository_get_all(ctx->repo, &ctx->object_count);
    }

    if (ctx->object_count == 0) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Cannot save empty session");
        return SAVE_ERR(NMO_ERR_INVALID_ARGUMENT, "Empty session");
    }

    if (ctx->type_reg == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Type registry not available");
        return SAVE_ERR(NMO_ERR_INVALID_STATE, "No type registry");
    }

    if (ctx->type_rt == NULL || ctx->type_rt->types == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Type runtime not available");
        return SAVE_ERR(NMO_ERR_INVALID_STATE, "No type runtime");
    }

    /* Build reference map */
    ctx->reference_map = (uint8_t *)nmo_arena_alloc(
        save_scratch(ctx), ctx->object_count * sizeof(uint8_t), 1);

    if (ctx->reference_map == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Failed to allocate reference map");
    }

    memset(ctx->reference_map, 0, ctx->object_count);

    size_t reference_count = 0;
    for (size_t i = 0; i < ctx->object_count; i++) {
        if (should_save_as_reference(ctx->objects[i], ctx->options.flags)) {
            ctx->reference_map[i] = 1;
            reference_count++;
        }
    }

    ctx->stats.object_count = ctx->object_count;
    ctx->stats.reference_count = reference_count;

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Session has %zu objects (%zu references)",
            ctx->object_count, reference_count);

    NMO_RETURN_OK();
}

static bool save_should_serialize_changed_object(
    const nmo_save_options_t *options,
    nmo_object_id_t id)
{
    if ((options->flags & NMO_SAVE_CHANGED_OBJECTS_ONLY) == 0u) {
        return true;
    }
    size_t low = 0u;
    size_t high = options->changed_object_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const nmo_object_id_t candidate = options->changed_object_ids[middle];
        if (candidate < id) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low < options->changed_object_count &&
        options->changed_object_ids[low] == id;
}

static nmo_status_t save_execute_pre_hooks(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.2: Executing manager pre-save hooks");

    if (ctx->manager_reg == NULL) {
        NMO_RETURN_OK();
    }

    uint32_t manager_count = nmo_manager_registry_get_count(ctx->manager_reg);
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Found %u registered managers", manager_count);

    for (uint32_t i = 0; i < manager_count; i++) {
        uint32_t manager_id = nmo_manager_registry_get_id_at(ctx->manager_reg, i);
        nmo_manager_t *manager = (nmo_manager_t *)nmo_manager_registry_get(
            ctx->manager_reg, manager_id);

        if (manager != NULL) {
            nmo_runtime_event_ctx_t event_ctx = {
                .event = NMO_RUNTIME_EVENT_PRE_SAVE,
                .manager_id = manager_id,
                .manager_guid = manager->guid
            };
            int hook_result = nmo_manager_invoke_event(manager, ctx->session, &event_ctx);
            if (hook_result != NMO_OK) {
                nmo_log(ctx->logger, NMO_LOG_WARN,
                        "  Manager %u pre-save hook failed: %d", manager_id, hook_result);
            }
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t save_build_remap_plan(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.3: Building ID remap plan");

    ctx->remap_plan = nmo_save_id_remap_plan_create(
        ctx->repo, ctx->objects, ctx->object_count);

    if (ctx->remap_plan == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Failed to create ID remap plan");
        return SAVE_ERR(NMO_ERR_NOMEM, "ID remap plan allocation failed");
    }

    nmo_id_remap_t *remap_table = nmo_save_id_remap_plan_get_table(ctx->remap_plan);
    size_t remap_count = nmo_id_remap_get_count(remap_table);

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Created remap plan with %zu entries", remap_count);

    ctx->file_index_remap = nmo_id_remap_create(save_scratch(ctx));
    if (ctx->file_index_remap == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "File index remap allocation failed");
    }

    for (size_t i = 0; i < ctx->object_count; i++) {
        nmo_object_t *obj = ctx->objects[i];
        if (obj == NULL) {
            continue;
        }

        nmo_object_id_t file_object_index = (nmo_object_id_t) i; /* SaveFindObjectIndex (0-based) */

        nmo_status_t add_result = nmo_id_remap_add(ctx->file_index_remap, obj->id, file_object_index);
        if (add_result != NMO_OK) {
            /* Continue even if one fails */
        }
    }

    ctx->chunk_file_ctx = (nmo_chunk_file_context_t *)nmo_arena_alloc(
        save_scratch(ctx), sizeof(nmo_chunk_file_context_t), alignof(nmo_chunk_file_context_t));
    if (ctx->chunk_file_ctx == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Chunk file context allocation failed");
    }
    ctx->chunk_file_ctx->runtime_to_file = ctx->file_index_remap;
    ctx->chunk_file_ctx->file_to_runtime = NULL;
    ctx->chunk_file_ctx->repository = ctx->repo;

    NMO_RETURN_OK();
}

static nmo_status_t save_serialize_managers(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.4: Serializing manager chunks");

    nmo_id_remap_t *remap_table = ctx->file_index_remap;

    /* Get session manager data for round-trip preservation */
    const nmo_file_state_t *mgr_fstate = nmo_session_get_file_state(ctx->session);
    uint32_t session_manager_count = mgr_fstate ? mgr_fstate->manager_data_count : 0;
    nmo_manager_data_t *session_managers = mgr_fstate ? mgr_fstate->manager_data : NULL;

    uint32_t registered_count = 0;
    if (ctx->manager_reg != NULL) {
        registered_count = nmo_manager_registry_get_count(ctx->manager_reg);
    }

    uint32_t manager_capacity = registered_count + session_manager_count;
    ctx->manager_entry_count = 0;

    if (manager_capacity == 0) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  No managers to serialize");
        NMO_RETURN_OK();
    }

    ctx->manager_entries = (nmo_manager_data_t *)nmo_arena_alloc(
        save_scratch(ctx),
        sizeof(nmo_manager_data_t) * manager_capacity,
        alignof(nmo_manager_data_t));

    if (ctx->manager_entries == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Manager entries allocation failed");
    }

    memset(ctx->manager_entries, 0, sizeof(nmo_manager_data_t) * manager_capacity);

    /* Serialize registered managers */
    if (ctx->manager_reg != NULL) {
        for (uint32_t i = 0; i < registered_count; i++) {
            uint32_t manager_id = nmo_manager_registry_get_id_at(ctx->manager_reg, i);
            nmo_manager_t *manager = (nmo_manager_t *)nmo_manager_registry_get(
                ctx->manager_reg, manager_id);

            if (manager == NULL) continue;

            nmo_chunk_t *chunk = NULL;
            nmo_runtime_event_ctx_t event_ctx = {
                .event = NMO_RUNTIME_EVENT_PRE_SAVE,
                .manager_id = manager_id,
                .manager_guid = manager->guid,
                .manager_chunk_out = &chunk
            };
            int save_event_result = nmo_manager_invoke_event(manager, ctx->session, &event_ctx);
            if (save_event_result != NMO_OK) {
                nmo_log(ctx->logger, NMO_LOG_WARN,
                        "  Manager %s pre-save event failed (code=%d)",
                        manager->name ? manager->name : "<unnamed>",
                        save_event_result);
                continue;
            }
            if (chunk == NULL) continue;

            nmo_manager_data_t *entry = &ctx->manager_entries[ctx->manager_entry_count++];
            entry->guid = manager->guid;
            entry->chunk = chunk;
            entry->data_size = (chunk->raw_data != NULL) ? (uint32_t)chunk->raw_size : 0;
            entry->flags = NMO_MANAGER_DATA_FLAG_DISPATCHED;

            if (chunk->raw_data == NULL && remap_table != NULL) {
                nmo_status_t remap_result = nmo_chunk_remap_object_ids_ex(chunk, remap_table, save_scratch(ctx));
                if (remap_result != NMO_OK) {
                    nmo_log(ctx->logger, NMO_LOG_WARN,
                            "  Manager %s: failed to remap object IDs (code=%d)",
                            manager->name ? manager->name : "<unnamed>", remap_result);
                }
            }

            nmo_log(ctx->logger, NMO_LOG_DEBUG, "  Manager %s: chunk size=%u",
                    manager->name ? manager->name : "<unnamed>", entry->data_size);
        }
    }

    /* Preserve unmanaged session chunks for round-trip */
    if (session_managers != NULL) {
        for (uint32_t i = 0; i < session_manager_count; i++) {
            nmo_manager_data_t *fallback = &session_managers[i];
            if ((fallback->flags & NMO_MANAGER_DATA_FLAG_DISPATCHED) != 0) {
                continue;
            }

            ctx->manager_entries[ctx->manager_entry_count++] = *fallback;
            nmo_log(ctx->logger, NMO_LOG_DEBUG, "  Preserving unmanaged chunk for round-trip");
        }
    }

    ctx->stats.manager_count = ctx->manager_entry_count;
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Serialized %u manager chunks", ctx->manager_entry_count);

    NMO_RETURN_OK();
}

static nmo_status_t save_serialize_objects(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.5: Serializing object chunks");

    size_t serialized_count = 0;
    size_t reused_count = 0;
    size_t skipped_count = 0;
    const nmo_shadow_storage_t *shadow_storage = nmo_session_get_shadow_storage(ctx->session);

    nmo_id_remap_t *remap_table = ctx->file_index_remap;
    const int require_schema = (ctx->options.flags & NMO_SAVE_REQUIRE_SCHEMA) != 0;
    nmo_class_id_t first_require_fail_class = 0;
    nmo_object_id_t first_require_fail_id = 0;

    for (size_t i = 0; i < ctx->object_count; i++) {
        nmo_object_t *obj = ctx->objects[i];

        /* Reference-only objects: preserve existing chunk if any, do not reserialize */
        if (ctx->reference_map[i]) {
            if (obj->chunk != NULL) {
                reused_count++;
            } else {
                skipped_count++;
            }
            continue;
        }

        if (obj->chunk != NULL &&
            !save_should_serialize_changed_object(&ctx->options, obj->id)) {
            reused_count++;
            continue;
        }

        if (require_schema && obj != NULL && nmo_object_get_state(obj) == NULL) {
            save_log_require_schema_failure(
                ctx->logger, obj, "Schema required but object has no deserialized state");
            if (first_require_fail_id == 0) {
                first_require_fail_id = obj->id;
                first_require_fail_class = obj->class_id;
            }
            return SAVE_ERR(NMO_ERR_INTERNAL, "Schema serialization required but object has no state");
        }

        nmo_chunk_t *old_chunk = obj->chunk;
        nmo_status_t serialize_status = NMO_OK;
        obj->chunk = serialize_object_with_schema(
            obj, ctx->type_rt, ctx->arena, save_scratch(ctx), ctx->repo, ctx->logger,
            shadow_storage, ctx->chunk_file_ctx, require_schema, &serialize_status);

        if (obj->chunk == NULL) {
            obj->chunk = old_chunk;
            char serialize_detail[512];
            size_t serialize_detail_len =
                nmo_last_error_message_copy(serialize_detail, sizeof(serialize_detail));
            if (require_schema) {
                save_log_require_schema_failure(
                    ctx->logger, obj, "Schema required but serialization returned NULL");
            }
            if (require_schema && first_require_fail_id == 0 && obj != NULL) {
                first_require_fail_id = obj->id;
                first_require_fail_class = obj->class_id;
            }
            nmo_log(ctx->logger, NMO_LOG_ERROR,
                    "Failed to serialize object %u ('%s')",
                    obj->id, obj->name ? obj->name : "<unnamed>");
            nmo_status_t failure_status =
                serialize_status != NMO_OK ? serialize_status : NMO_ERR_INTERNAL;
            nmo_last_error_setf(
                failure_status, NMO_SEVERITY_ERROR, __FILE__, __LINE__,
                "Object %u ('%s') serialization failed%s%s",
                obj->id, obj->name ? obj->name : "<unnamed>",
                serialize_detail_len > 0u ? ": " : "",
                serialize_detail_len > 0u ? serialize_detail : "");
            return failure_status;
        }

        if (obj->chunk == old_chunk) {
            if (require_schema) {
                nmo_log(ctx->logger, NMO_LOG_ERROR,
                        "Object %u reused raw chunk with schema requirement enabled",
                        obj->id);
                if (first_require_fail_id == 0 && obj != NULL) {
                    first_require_fail_id = obj->id;
                    first_require_fail_class = obj->class_id;
                }
                return SAVE_ERR(NMO_ERR_INTERNAL, "Schema serialization required but chunk reused");
            }
            reused_count++;
        } else {
            if (obj->chunk != NULL && obj->chunk->raw_data == NULL && remap_table != NULL) {
                nmo_status_t remap_result = nmo_chunk_remap_object_ids_ex(obj->chunk, remap_table, save_scratch(ctx));
                if (remap_result != NMO_OK) {
                    nmo_log(ctx->logger, NMO_LOG_WARN,
                            "    Failed to remap object IDs for object %u (code=%d)",
                            obj->id, remap_result);
                }
            }
            /* Clear file_context pointers that refer to scratch-arena memory.
               Must happen after remap (which no longer needs file_context)
               and before scratch arena destruction. */
            save_clear_chunk_file_context(obj->chunk);
            serialized_count++;
        }
    }

    ctx->stats.serialized_count = serialized_count;
    ctx->stats.reused_count = reused_count;

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "  Serialization: %zu new, %zu reused, %zu skipped",
            serialized_count, reused_count, skipped_count);

    if (require_schema && first_require_fail_id != 0) {
        nmo_log(ctx->logger, NMO_LOG_ERROR,
                "  First schema requirement failure: object %u class 0x%08X",
                first_require_fail_id, first_require_fail_class);
    }

    NMO_RETURN_OK();
}

static nmo_status_t save_build_data_section(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.6: Building data section buffer");

    /* Build data section structure */
    nmo_data_section_t data_sect = {0};
    data_sect.manager_count = ctx->manager_entry_count;
    data_sect.managers = (ctx->manager_entry_count > 0) ? ctx->manager_entries : NULL;
    data_sect.object_count = (uint32_t)ctx->object_count;

    /* Allocate object data array */
    data_sect.objects = (nmo_object_data_t *)nmo_arena_alloc(
        save_scratch(ctx),
        sizeof(nmo_object_data_t) * ctx->object_count,
        sizeof(void *));

    if (data_sect.objects == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Object data array allocation failed");
    }

    uint32_t file_version = ctx->file_info.file_version;
    if (file_version == 0) file_version = 8;

    nmo_id_remap_t *remap_table = NULL;
    if (file_version < 7) {
        remap_table = nmo_save_id_remap_plan_get_table(ctx->remap_plan);
        if (remap_table == NULL) {
            return SAVE_ERR(NMO_ERR_INVALID_STATE, "Missing ID remap table for legacy save");
        }
    }

    /* Copy chunk pointers */
    for (size_t i = 0; i < ctx->object_count; i++) {
        if (ctx->reference_map[i] && ctx->objects[i]->chunk == NULL) {
            data_sect.objects[i].object_id = 0;
            data_sect.objects[i].chunk = NULL;
            data_sect.objects[i].data_size = 0;
        } else {
            nmo_chunk_t *chunk = ctx->objects[i]->chunk;
            data_sect.objects[i].object_id = 0;
            data_sect.objects[i].chunk = chunk;
            data_sect.objects[i].data_size = (chunk && chunk->raw_data != NULL)
                ? (uint32_t)chunk->raw_size : 0;
        }

        if (file_version < 7) {
            nmo_object_id_t file_id = 0;
            int lookup_result = nmo_id_remap_lookup_id(remap_table, ctx->objects[i]->id, &file_id);
            if (lookup_result != NMO_OK || file_id == 0) {
                nmo_log(ctx->logger, NMO_LOG_ERROR,
                        "Failed to map object ID for legacy save (runtime=%u)",
                        ctx->objects[i]->id);
                return SAVE_ERR(lookup_result, "Legacy object ID remap failed");
            }
            data_sect.objects[i].object_id = file_id;
        }
    }

    /* Build data section plan and serialize generated chunks once. */
    nmo_data_section_plan_t data_plan = {0};
    uint64_t data_plan_start = save_perf_begin(ctx);
    nmo_status_t result = nmo_data_section_plan_build(&data_sect, file_version, save_scratch(ctx), &data_plan);
    save_perf_end(ctx, NMO_SAVE_PERF_DATA_PLAN, data_plan_start);
    if (result != NMO_OK) {
        return result;
    }

    size_t data_size = data_plan.total_size;
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Data section unpack size: %zu bytes", data_size);
    if (ctx->perf_stats != NULL) {
        ctx->perf_stats->planned_chunk_bytes = data_size;
    }

    /* Allocate buffer */
    ctx->data_buffer = nmo_arena_alloc(save_scratch(ctx), data_size, 16);
    if (ctx->data_buffer == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Data buffer allocation failed");
    }

    /* Serialize data section */
    uint64_t data_write_start = save_perf_begin(ctx);
    result = nmo_data_section_plan_write(
        &data_sect, &data_plan, file_version, (uint8_t *)ctx->data_buffer, data_size);
    save_perf_end(ctx, NMO_SAVE_PERF_DATA_WRITE, data_write_start);

    if (result != NMO_OK) {
        return result;
    }

    ctx->data_unpack_size = data_size;
    ctx->stats.data_unpack_size = data_size;
    if (ctx->perf_stats != NULL) {
        ctx->perf_stats->data_unpacked_bytes = data_size;
    }

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Data section serialized: %zu bytes", data_size);

    NMO_RETURN_OK();
}

static nmo_status_t save_build_header1(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.7: Building header1 buffer");

    nmo_id_remap_t *remap_table = nmo_save_id_remap_plan_get_table(ctx->remap_plan);
    uint32_t file_version = ctx->file_info.file_version;
    if (file_version == 0) {
        file_version = 8;
    }

    /* Build object descriptors */
    ctx->obj_descs = (nmo_object_desc_t *)nmo_arena_alloc(
        save_scratch(ctx),
        sizeof(nmo_object_desc_t) * ctx->object_count,
        sizeof(void *));

    if (ctx->obj_descs == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Object descriptors allocation failed");
    }

    for (size_t i = 0; i < ctx->object_count; i++) {
        nmo_object_t *obj = ctx->objects[i];

        nmo_object_id_t file_id = 0;
        int lookup_result = nmo_id_remap_lookup_id(remap_table, obj->id, &file_id);
        if (lookup_result != NMO_OK) {
            nmo_log(ctx->logger, NMO_LOG_ERROR,
                    "Failed to lookup file ID for object %u", obj->id);
            return SAVE_ERR(lookup_result, "ID remap lookup failed");
        }

        ctx->obj_descs[i].file_id = file_id;
        ctx->obj_descs[i].class_id = obj->class_id;
        ctx->obj_descs[i].name = (char *)obj->name;
        ctx->obj_descs[i].file_index = 0;

        ctx->obj_descs[i].flags = ctx->reference_map[i] ? NMO_OBJECT_REFERENCE_FLAG : 0u;
    }

    /* Build plugin dependencies */
    {
        const nmo_file_state_t *dep_fstate = nmo_session_get_file_state(ctx->session);
        uint32_t stored_plugin_count = dep_fstate ? dep_fstate->plugin_dep_count : 0;
        nmo_plugin_dep_t *stored_plugin_deps = dep_fstate ? dep_fstate->plugin_deps : NULL;

        if (stored_plugin_deps != NULL && stored_plugin_count > 0) {
            ctx->plugin_deps = stored_plugin_deps;
            ctx->plugin_count = stored_plugin_count;
        } else {
            nmo_extension_registry_t *ext_registry = nmo_context_get_extension_registry(ctx->context);
            size_t plugin_count = 0;
            const nmo_extension_plugin_info_t *plugins =
                (ext_registry != NULL)
                    ? nmo_extension_registry_list(ext_registry, &plugin_count)
                    : NULL;

            if (plugins != NULL && plugin_count > 0) {
                nmo_plugin_dep_t *deps = (nmo_plugin_dep_t *)nmo_arena_alloc(
                    save_scratch(ctx),
                    plugin_count * sizeof(nmo_plugin_dep_t),
                    alignof(nmo_plugin_dep_t));
                if (deps == NULL) {
                    return SAVE_ERR(NMO_ERR_NOMEM, "Plugin dependency allocation failed");
                }

                size_t written = 0;
                for (size_t i = 0; i < plugin_count; ++i) {
                    const nmo_extension_plugin_info_t *plugin = &plugins[i];
                    if (nmo_guid_is_null(plugin->guid)) {
                        continue;
                    }

                    deps[written].guid = plugin->guid;
                    deps[written].version = plugin->version;
                    deps[written].category = plugin->category;
                    written++;
                }

                if (written > 0) {
                    ctx->plugin_deps = deps;
                    ctx->plugin_count = written;
                } else {
                    ctx->plugin_deps = NULL;
                    ctx->plugin_count = 0;
                }
            } else {
                ctx->plugin_deps = NULL;
                ctx->plugin_count = 0;
            }
        }
    }

    /* Build Header1 structure */
    nmo_header1_t hdr1 = {0};
    hdr1.object_count = (uint32_t)ctx->object_count;
    hdr1.objects = ctx->obj_descs;
    hdr1.plugin_dep_count = ctx->plugin_count;
    hdr1.plugin_deps = ctx->plugin_deps;

    /* Include files handling */
    uint32_t session_included_count = 0;
    nmo_included_file_t *session_included_files = nmo_session_get_included_files(
        ctx->session, &session_included_count);

    bool strip_included = (ctx->options.flags & NMO_SAVE_STRIP_INCLUDED_FILES) != 0;

    if (!strip_included && session_included_files != NULL && session_included_count > 0) {
        hdr1.included_file_count = session_included_count;
        hdr1.included_files = NULL;
    }

    /* Plan Header1 size before FileIndex values are filled. The final values
     * do not affect serialized width, so the same layout can be reused.
     */
    nmo_header1_layout_t header1_layout = {0};
    uint64_t header_plan_start = save_perf_begin(ctx);
    nmo_status_t result = nmo_header1_plan(&hdr1, save_scratch(ctx), &header1_layout);

    if (result != NMO_OK) {
        save_perf_end(ctx, NMO_SAVE_PERF_HEADER1_PLAN, header_plan_start);
        return result;
    }

    /* Fill FileIndex offsets (uncompressed file buffer) */
    result = save_fill_file_indices(ctx, header1_layout.total_size, file_version);
    save_perf_end(ctx, NMO_SAVE_PERF_HEADER1_PLAN, header_plan_start);
    if (result != NMO_OK) {
        return result;
    }

    /* Final serialize with correct FileIndex values */
    uint64_t header_write_start = save_perf_begin(ctx);
    uint8_t *header1_buffer = NULL;
    result = nmo_header1_write_planned(
        &hdr1, &header1_layout, save_scratch(ctx), &header1_buffer, &ctx->header1_unpack_size);
    ctx->header1_buffer = header1_buffer;
    save_perf_end(ctx, NMO_SAVE_PERF_HEADER1_WRITE, header_write_start);

    if (result != NMO_OK) {
        return result;
    }

    ctx->stats.header1_unpack_size = ctx->header1_unpack_size;
    if (ctx->perf_stats != NULL) {
        ctx->perf_stats->header1_unpacked_bytes = ctx->header1_unpack_size;
    }

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Header1 serialized: %zu bytes", ctx->header1_unpack_size);

    NMO_RETURN_OK();
}

/* ============================================================================
 * Phase 2 Implementation: Pack & Commit
 * ============================================================================ */

static nmo_status_t save_compress_sections(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 2.1: Compressing sections");

    int compression_level = ctx->options.compression_level;
    if (compression_level <= 0) compression_level = DEFAULT_COMPRESSION_LEVEL;

    /* Default: use uncompressed buffers */
    ctx->header1_packed = ctx->header1_buffer;
    ctx->header1_pack_size = (uint32_t)ctx->header1_unpack_size;
    ctx->data_packed = ctx->data_buffer;
    ctx->data_pack_size = (uint32_t)ctx->data_unpack_size;

    ctx->stats.header_compressed = false;
    ctx->stats.data_compressed = false;

    /* Compress Header1 if requested */
    if (ctx->options.compress_header && ctx->header1_unpack_size > 0) {
        mz_ulong bound = mz_compressBound((mz_ulong)ctx->header1_unpack_size);
        void *compressed = nmo_arena_alloc(save_scratch(ctx), bound, 16);
        if (compressed == NULL) {
            return SAVE_ERR(NMO_ERR_NOMEM, "Header1 compression buffer allocation failed");
        }

        mz_ulong dest_len = bound;
        uint64_t header_compress_start = save_perf_begin(ctx);
        int comp_result = mz_compress2(
            (unsigned char *)compressed,
            &dest_len,
            (const unsigned char *)ctx->header1_buffer,
            (mz_ulong)ctx->header1_unpack_size,
            compression_level);
        save_perf_end(ctx, NMO_SAVE_PERF_HEADER1_COMPRESS, header_compress_start);

        if (comp_result != MZ_OK) {
            return SAVE_ERR(NMO_ERR_INTERNAL, "Header1 compression failed");
        }

        /* Only use compressed if smaller */
        if (dest_len < (mz_ulong)ctx->header1_unpack_size) {
            ctx->header1_packed = compressed;
            ctx->header1_pack_size = (uint32_t)dest_len;
            ctx->stats.header_compressed = true;

            nmo_log(ctx->logger, NMO_LOG_INFO,
                    "  Header1 compressed: %zu -> %u bytes (%.2fx)",
                    ctx->header1_unpack_size, ctx->header1_pack_size,
                    (double)ctx->header1_pack_size / (double)ctx->header1_unpack_size);
        } else {
            nmo_log(ctx->logger, NMO_LOG_INFO,
                    "  Header1 compression skipped (no gain)");
        }
    }

    /* Compress Data section if requested */
    if (ctx->options.compress_data && ctx->data_unpack_size > 0) {
        mz_ulong bound = mz_compressBound((mz_ulong)ctx->data_unpack_size);
        void *compressed = nmo_arena_alloc(save_scratch(ctx), bound, 16);
        if (compressed == NULL) {
            return SAVE_ERR(NMO_ERR_NOMEM, "Data compression buffer allocation failed");
        }

        mz_ulong dest_len = bound;
        uint64_t data_compress_start = save_perf_begin(ctx);
        int comp_result = mz_compress2(
            (unsigned char *)compressed,
            &dest_len,
            (const unsigned char *)ctx->data_buffer,
            (mz_ulong)ctx->data_unpack_size,
            compression_level);
        save_perf_end(ctx, NMO_SAVE_PERF_DATA_COMPRESS, data_compress_start);

        if (comp_result != MZ_OK) {
            return SAVE_ERR(NMO_ERR_INTERNAL, "Data compression failed");
        }

        /* Only use compressed if smaller */
        if (dest_len < (mz_ulong)ctx->data_unpack_size) {
            ctx->data_packed = compressed;
            ctx->data_pack_size = (uint32_t)dest_len;
            ctx->stats.data_compressed = true;

            nmo_log(ctx->logger, NMO_LOG_INFO,
                    "  Data compressed: %zu -> %u bytes (%.2fx)",
                    ctx->data_unpack_size, ctx->data_pack_size,
                    (double)ctx->data_pack_size / (double)ctx->data_unpack_size);
        } else {
            nmo_log(ctx->logger, NMO_LOG_INFO,
                    "  Data compression skipped (no gain)");
        }
    }

    ctx->stats.header1_pack_size = ctx->header1_pack_size;
    ctx->stats.data_pack_size = ctx->data_pack_size;
    if (ctx->perf_stats != NULL) {
        ctx->perf_stats->header1_packed_bytes = ctx->header1_pack_size;
        ctx->perf_stats->data_packed_bytes = ctx->data_pack_size;
    }

    /* Calculate overall compression ratio */
    size_t total_unpack = ctx->header1_unpack_size + ctx->data_unpack_size;
    size_t total_pack = ctx->header1_pack_size + ctx->data_pack_size;
    ctx->stats.compression_ratio = (total_unpack > 0)
        ? (double)total_pack / (double)total_unpack : 1.0;

    NMO_RETURN_OK();
}

static nmo_status_t save_write_file(nmo_serializer_t *ctx, const char *path) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 2.2: Writing file with CRC");

    /* Calculate total file size */
    uint32_t file_size = sizeof(nmo_file_header_t) + ctx->header1_pack_size + ctx->data_pack_size;
    ctx->stats.total_file_size = file_size;

    /* Build file header */
    nmo_file_header_t header = {0};

    /* Signature */
    memcpy(header.signature, "Nemo Fi\0", 8);

    /* Versions */
    header.file_version = (ctx->file_info.file_version != 0)
        ? ctx->file_info.file_version : 8;
    header.file_version2 = ctx->file_info.file_version2;
    header.ck_version = (ctx->file_info.ck_version != 0)
        ? ctx->file_info.ck_version : 0x13022002;
    header.product_version = ctx->file_info.product_version;
    header.product_build = ctx->file_info.product_build;

    /* Counts */
    header.object_count = (uint32_t)ctx->object_count;
    header.manager_count = ctx->manager_entry_count;

    /* Sizes */
    header.hdr1_pack_size = ctx->header1_pack_size;
    header.hdr1_unpack_size = (uint32_t)ctx->header1_unpack_size;
    header.data_pack_size = ctx->data_pack_size;
    header.data_unpack_size = (uint32_t)ctx->data_unpack_size;

    /* Max ID */
    nmo_object_id_t max_file_id = 0;
    for (size_t i = 0; i < ctx->object_count; i++) {
        if (ctx->obj_descs[i].file_id > max_file_id) {
            max_file_id = ctx->obj_descs[i].file_id;
        }
    }
    header.max_id_saved = max_file_id;

    /* Write mode flags */
    uint32_t write_mode = ctx->file_info.write_mode;
    if (ctx->stats.header_compressed) {
        write_mode |= NMO_FILE_WRITE_COMPRESS_HEADER;
    } else {
        write_mode &= ~NMO_FILE_WRITE_COMPRESS_HEADER;
    }
    if (ctx->stats.data_compressed) {
        write_mode |= NMO_FILE_WRITE_COMPRESS_DATA;
    } else {
        write_mode &= ~NMO_FILE_WRITE_COMPRESS_DATA;
    }
    header.file_write_mode = write_mode;

    /* Calculate CRC (Adler-32) over all sections */
    uint32_t crc = 0;
    if (ctx->options.compute_crc) {
        uint64_t crc_start = save_perf_begin(ctx);
        crc = nmo_file_header_compute_crc(&header,
                                          (const uint8_t *)ctx->header1_packed,
                                          ctx->header1_pack_size,
                                          (const uint8_t *)ctx->data_packed,
                                          ctx->data_pack_size);
        save_perf_end(ctx, NMO_SAVE_PERF_CRC, crc_start);
    }
    header.crc = crc;
    ctx->stats.crc = crc;

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "  File header: version=%u, objects=%u, managers=%u, max_id=%u, CRC=0x%08X",
            header.file_version, header.object_count, header.manager_count,
            header.max_id_saved, crc);

    /* Validate before write if requested */
    if (ctx->options.validate_before_write) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Validating buffer integrity...");
        /* Could add checksum verification, pointer validation, etc. */
    }

    /* Open atomic transaction */
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Opening output transaction: %s", path);
    uint64_t txn_write_start = save_perf_begin(ctx);
    nmo_txn_desc_t txn_desc = {
        .path = path,
        .durability = save_txn_durability_from_options(ctx->options.durability),
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&txn_desc);
    if (txn == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Failed to open output transaction: %s", path);
        return SAVE_ERR(NMO_ERR_CANT_WRITE_FILE, "Cannot open output transaction");
    }

    /* Serialize header using canonical IO serializer into memory, then write via transaction */
    nmo_io_interface_t *header_io = nmo_memory_io_open_write(sizeof(nmo_file_header_t));
    if (header_io == NULL) {
        nmo_txn_rollback(txn);
        nmo_txn_close(txn);
        return SAVE_ERR(NMO_ERR_NOMEM, "Cannot allocate header serialization buffer");
    }

    nmo_status_t header_result = nmo_file_header_serialize(&header, header_io);
    if (header_result != NMO_OK) {
        nmo_io_close(header_io);
        nmo_txn_rollback(txn);
        nmo_txn_close(txn);
        return header_result;
    }

    size_t serialized_header_size = 0;
    const void *serialized_header = nmo_memory_io_get_data(header_io, &serialized_header_size);
    if (serialized_header == NULL || serialized_header_size == 0) {
        nmo_io_close(header_io);
        nmo_txn_rollback(txn);
        nmo_txn_close(txn);
        return SAVE_ERR(NMO_ERR_INTERNAL, "Header serialization produced no data");
    }

    nmo_status_t write_result = nmo_txn_write(txn, serialized_header, serialized_header_size);
    nmo_io_close(header_io);
    if (write_result != NMO_OK) {
        nmo_txn_rollback(txn);
        nmo_txn_close(txn);
        return SAVE_ERR(write_result, "Header write failed");
    }

    /* Write Header1 */
    if (ctx->header1_pack_size > 0) {
        write_result = nmo_txn_write(txn, ctx->header1_packed, ctx->header1_pack_size);
        if (write_result != NMO_OK) {
            nmo_txn_rollback(txn);
            nmo_txn_close(txn);
            return SAVE_ERR(write_result, "Header1 write failed");
        }
    }

    /* Write Data section */
    if (ctx->data_pack_size > 0) {
        write_result = nmo_txn_write(txn, ctx->data_packed, ctx->data_pack_size);
        if (write_result != NMO_OK) {
            nmo_txn_rollback(txn);
            nmo_txn_close(txn);
            return SAVE_ERR(write_result, "Data section write failed");
        }
    }

    /* Write included files */
    uint32_t session_included_count = 0;
    nmo_included_file_t *session_included_files = nmo_session_get_included_files(
        ctx->session, &session_included_count);
    bool strip_included = (ctx->options.flags & NMO_SAVE_STRIP_INCLUDED_FILES) != 0;
    const nmo_shadow_storage_t *shadow_storage = nmo_session_get_shadow_storage(ctx->session);
    size_t shadow_size = 0;
    const void *shadow_blob = NULL;
    bool use_shadow_included = false;

    if (!strip_included && shadow_storage != NULL && nmo_shadow_has_included_files(shadow_storage)) {
        bool all_borrowed = true;
        for (uint32_t i = 0; i < session_included_count; i++) {
            if ((session_included_files[i].attributes & NMO_INCLUDED_FILE_ATTR_BORROWED) == 0) {
                all_borrowed = false;
                break;
            }
        }

        if (all_borrowed) {
            shadow_blob = nmo_shadow_get_included_files(shadow_storage, &shadow_size);
            if (shadow_blob != NULL && shadow_size > 0) {
                use_shadow_included = true;
            }
        }
    }

    if (!strip_included && use_shadow_included) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Writing shadow included files blob (%zu bytes)",
                shadow_size);
        if (nmo_txn_write(txn, shadow_blob, shadow_size) != NMO_OK) {
            nmo_txn_rollback(txn);
            nmo_txn_close(txn);
            return SAVE_ERR(NMO_ERR_CANT_WRITE_FILE, "Included files shadow blob write failed");
        }
    } else if (!strip_included && session_included_files != NULL && session_included_count > 0) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Writing %u included files", session_included_count);

        for (uint32_t i = 0; i < session_included_count; i++) {
            const nmo_included_file_t *entry = &session_included_files[i];
            const char *name = nmo_path_basename(entry->name ? entry->name : "");
            uint32_t name_len = (uint32_t)strlen(name);
            const int metadata_only = (entry->attributes & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) != 0;
            uint32_t payload_size = (entry->data != NULL && !metadata_only) ? entry->size : 0u;

            if (save_txn_write_u32_le(txn, name_len) != NMO_OK ||
                (name_len > 0 && nmo_txn_write(txn, name, name_len) != NMO_OK) ||
                save_txn_write_u32_le(txn, payload_size) != NMO_OK) {
                nmo_txn_rollback(txn);
                nmo_txn_close(txn);
                return SAVE_ERR(NMO_ERR_CANT_WRITE_FILE, "Included file metadata write failed");
            }

            if (payload_size > 0 && entry->data != NULL) {
                if (nmo_txn_write(txn, entry->data, payload_size) != NMO_OK) {
                    nmo_txn_rollback(txn);
                    nmo_txn_close(txn);
                    return SAVE_ERR(NMO_ERR_CANT_WRITE_FILE, "Included file payload write failed");
                }
            } else if (entry->size > 0 && metadata_only) {
                nmo_log(ctx->logger, NMO_LOG_WARN,
                        "  Included file '%s' metadata-only; payload omitted", name);
            }
        }
    }
    save_perf_end(ctx, NMO_SAVE_PERF_TXN_WRITE, txn_write_start);

    uint64_t txn_commit_start = save_perf_begin(ctx);
    write_result = nmo_txn_commit(txn);
    save_perf_end(ctx, NMO_SAVE_PERF_TXN_COMMIT, txn_commit_start);
    if (write_result != NMO_OK) {
        nmo_txn_rollback(txn);
        nmo_txn_close(txn);
        return SAVE_ERR(write_result, "Atomic commit failed");
    }
    nmo_txn_close(txn);

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Write complete: %u bytes", file_size);

    NMO_RETURN_OK();
}

static nmo_status_t save_execute_post_hooks(nmo_serializer_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 2.3: Executing manager post-save hooks");

    if (ctx->manager_reg == NULL) {
        NMO_RETURN_OK();
    }

    uint32_t manager_count = nmo_manager_registry_get_count(ctx->manager_reg);

    for (uint32_t i = 0; i < manager_count; i++) {
        uint32_t manager_id = nmo_manager_registry_get_id_at(ctx->manager_reg, i);
        nmo_manager_t *manager = (nmo_manager_t *)nmo_manager_registry_get(
            ctx->manager_reg, manager_id);

        if (manager != NULL) {
            nmo_runtime_event_ctx_t event_ctx = {
                .event = NMO_RUNTIME_EVENT_POST_SAVE,
                .manager_id = manager_id,
                .manager_guid = manager->guid
            };
            int hook_result = nmo_manager_invoke_event(manager, ctx->session, &event_ctx);
            if (hook_result != NMO_OK) {
                nmo_log(ctx->logger, NMO_LOG_WARN,
                        "  Manager %u post-save hook failed: %d", manager_id, hook_result);
            }
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Recursively clear file_context pointers from a chunk and all its sub-chunks.
 * Must be called after serialization before scratch arena destruction to avoid
 * dangling pointers into freed memory.
 */
static void save_clear_chunk_file_context(nmo_chunk_t *chunk) {
    if (chunk == NULL) return;
    nmo_chunk_set_file_context(chunk, NULL);
    size_t sub_count = chunk->chunks.count;
    nmo_chunk_t **subs = (nmo_chunk_t **)chunk->chunks.data;
    for (size_t i = 0; i < sub_count; i++) {
        save_clear_chunk_file_context(subs[i]);
    }
}

static int should_save_as_reference(const nmo_object_t *obj, uint32_t flags) {
    if (obj == NULL) {
        return 0;
    }

    if (flags & NMO_SAVE_AS_OBJECTS) {
        return 1;
    }

    if (obj->save_flags & NMO_OBJECT_REFERENCE_FLAG) {
        return 1;
    }

    if (obj->flags & NMO_OBJECT_REFERENCE_FLAG) {
        return 1;
    }

    return 0;
}

nmo_status_t nmo_document_save_file(
    nmo_document_t *document,
    const char *path,
    const nmo_save_options_t *options)
{
    if (document == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_document_internal_save_file(document, path, options);
}

static nmo_chunk_t *serialize_object_with_schema(
    nmo_object_t *obj,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_arena_t *scratch,
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage,
    const nmo_chunk_file_context_t *file_ctx,
    int require_schema,
    nmo_status_t *out_status)
{
    nmo_chunk_t *chunk = nmo_object_system_serialize_object_chunk(
        obj, type_rt, arena, scratch, repo, logger,
        shadow_storage, file_ctx, out_status);

    if (!require_schema) {
        return chunk;
    }

    if (chunk == NULL) {
        return NULL;
    }

    if (obj != NULL && chunk == obj->chunk) {
        save_log_require_schema_failure(
            logger, obj, "Schema required but raw chunk reuse occurred");
        return NULL;
    }

    if (chunk->raw_data != NULL && chunk->raw_size > 0) {
        save_log_require_schema_failure(
            logger, obj, "Schema required but raw chunk data remains");
        return NULL;
    }

    return chunk;

}

