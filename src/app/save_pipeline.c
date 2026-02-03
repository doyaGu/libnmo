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

#include "app/nmo_save_pipeline.h"
#include "app/nmo_save_buffer.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "app/nmo_plugin.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "core/nmo_guid.h"
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "object/nmo_schema_interface.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_object_repository.h"
#include "session/nmo_shadow_storage.h"
#include "type/type_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include <limits.h>
#include "miniz.h"

#define DEFAULT_COMPRESSION_LEVEL 6
#define DEFAULT_BUFFER_CAPACITY (64 * 1024)  /* 64 KB initial */

/* Helper macro for returning error results (expression form for `return SAVE_ERR(...)`) */
#define SAVE_ERR(code, msg) \
    (nmo_last_error_setf((code), NMO_SEVERITY_ERROR, __FILE__, __LINE__, "%s", (msg)), \
     (nmo_status_t)(code))

static int save_safe_add_size(size_t a, size_t b, size_t *out) {
    if (SIZE_MAX - a < b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * @brief Save context internal structure
 *
 * Holds all intermediate state between Phase 1 and Phase 2.
 */
struct nmo_save_context {
    /* Input references (borrowed) */
    nmo_session_t *session;
    nmo_context_t *context;
    nmo_arena_t *arena;
    nmo_object_repository_t *repo;
    nmo_logger_t *logger;
    nmo_type_registry_t *type_reg;
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
    nmo_id_remap_plan_t *remap_plan;
    nmo_id_remap_table_t *file_index_remap;

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

    /* Phase tracking */
    int phase1_complete;
    int phase2_complete;
};

/* ============================================================================
 * Forward Declarations (Internal Functions)
 * ============================================================================ */

static nmo_status_t save_validate_session(nmo_save_context_t *ctx);
static nmo_status_t save_execute_pre_hooks(nmo_save_context_t *ctx);
static nmo_status_t save_build_remap_plan(nmo_save_context_t *ctx);
static nmo_status_t save_serialize_managers(nmo_save_context_t *ctx);
static nmo_status_t save_serialize_objects(nmo_save_context_t *ctx);
static nmo_status_t save_build_data_section(nmo_save_context_t *ctx);
static nmo_status_t save_build_header1(nmo_save_context_t *ctx);
static nmo_status_t save_get_chunk_size(nmo_chunk_t *chunk, nmo_arena_t *arena, size_t *out_size);
static nmo_status_t save_compute_manager_data_size(nmo_save_context_t *ctx, size_t *out_size);
static nmo_status_t save_fill_file_indices(nmo_save_context_t *ctx,
                                           size_t header1_unpack_size,
                                           uint32_t file_version);

static nmo_status_t save_compress_sections(nmo_save_context_t *ctx);
static nmo_status_t save_write_file(nmo_save_context_t *ctx, const char *path);
static nmo_status_t save_execute_post_hooks(nmo_save_context_t *ctx);

static nmo_chunk_t *serialize_object_with_schema(
    nmo_object_t *obj,
    nmo_type_registry_t *type_reg,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage);

static int should_save_as_reference(const nmo_object_t *obj, uint32_t flags);

static const char *nmo_basename(const char *path);
static nmo_status_t save_report_progress(nmo_save_context_t *ctx,
                                         nmo_save_phase_t phase,
                                         float progress,
                                         const char *status);

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

nmo_save_options_t nmo_save_options_default(void) {
    nmo_save_options_t opts = {0};
    opts.flags = NMO_SAVE_DEFAULT;
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

static const char *nmo_basename(const char *path) {
    if (path == NULL) {
        return "";
    }

    const char *last_slash = strrchr(path, '/');
    const char *last_backslash = strrchr(path, '\\');

    const char *base = path;
    if (last_slash && last_backslash) {
        base = (last_slash > last_backslash) ? last_slash + 1 : last_backslash + 1;
    } else if (last_slash) {
        base = last_slash + 1;
    } else if (last_backslash) {
        base = last_backslash + 1;
    }

    return base;
}

static nmo_status_t save_report_progress(nmo_save_context_t *ctx,
                                         nmo_save_phase_t phase,
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

static nmo_status_t save_compute_manager_data_size(nmo_save_context_t *ctx, size_t *out_size) {
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
            NMO_RETURN_IF_ERROR(save_get_chunk_size(mgr->chunk, ctx->arena, &chunk_size));
        }

        size_t entry_size = 0;
        if (!save_safe_add_size(8u, 4u, &entry_size) ||
            !save_safe_add_size(entry_size, chunk_size, &entry_size)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Manager data size overflow");
        }

        if (!save_safe_add_size(total, entry_size, &total)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Manager data size overflow");
        }
    }

    *out_size = total;
    NMO_RETURN_OK();
}

static nmo_status_t save_fill_file_indices(nmo_save_context_t *ctx,
                                           size_t header1_unpack_size,
                                           uint32_t file_version) {
    if (ctx == NULL || ctx->obj_descs == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid file index context");
    }

    size_t header_size = (file_version >= 5) ? 64u : 32u;
    size_t manager_data_size = 0;
    NMO_RETURN_IF_ERROR(save_compute_manager_data_size(ctx, &manager_data_size));

    size_t offset = 0;
    if (!save_safe_add_size(header_size, header1_unpack_size, &offset) ||
        !save_safe_add_size(offset, manager_data_size, &offset)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index base offset overflow");
    }

    for (size_t i = 0; i < ctx->object_count; i++) {
        nmo_object_t *obj = ctx->objects[i];
        size_t chunk_size = 0;
        if (obj != NULL) {
            NMO_RETURN_IF_ERROR(save_get_chunk_size(obj->chunk, ctx->arena, &chunk_size));
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
        if (!save_safe_add_size(size_field_bytes, chunk_size, &entry_size)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index entry size overflow");
        }
        if (!save_safe_add_size(offset, entry_size, &offset)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index overflow");
        }
    }

    NMO_RETURN_OK();
}


nmo_save_context_t *nmo_save_context_create(
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

    nmo_save_context_t *save_ctx = (nmo_save_context_t *)nmo_arena_alloc(
        arena, sizeof(nmo_save_context_t), alignof(nmo_save_context_t));

    if (save_ctx == NULL) {
        return NULL;
    }

    memset(save_ctx, 0, sizeof(nmo_save_context_t));

    /* Store borrowed references */
    save_ctx->session = session;
    save_ctx->context = ctx;
    save_ctx->arena = arena;
    save_ctx->repo = nmo_session_get_repository(session);
    save_ctx->logger = nmo_context_get_logger(ctx);
    save_ctx->type_reg = nmo_context_get_type_registry(ctx);
    save_ctx->manager_reg = nmo_context_get_manager_registry(ctx);

    /* Store options */
    if (options != NULL) {
        save_ctx->options = *options;
    } else {
        save_ctx->options = nmo_save_options_default();
    }

    /* Get file info from session */
    save_ctx->file_info = nmo_session_get_file_info(session);

    return save_ctx;
}

void nmo_save_context_destroy(nmo_save_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* Clean up remap plan if allocated */
    if (ctx->remap_plan != NULL) {
        nmo_id_remap_plan_destroy(ctx->remap_plan);
        ctx->remap_plan = NULL;
    }

    /* Note: All other allocations are arena-based, no explicit free needed */
}

nmo_status_t nmo_save_phase1_layout(nmo_save_context_t *ctx) {
    if (ctx == NULL) {
        return SAVE_ERR(NMO_ERR_INVALID_ARGUMENT, "NULL context");
    }

    nmo_log(ctx->logger, NMO_LOG_INFO, "=== Save Pipeline Phase 1: Layout & Serialize ===");

    nmo_status_t result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 0.0f,
                                  "Validating session");
    if (result != NMO_OK) return result;

    /* Step 1.1: Validate session state */
    result = save_validate_session(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 0.1f,
                                  "Executing pre-save hooks");
    if (result != NMO_OK) return result;

    /* Step 1.2: Execute manager pre-save hooks */
    result = save_execute_pre_hooks(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 0.2f,
                                  "Building ID remap plan");
    if (result != NMO_OK) return result;

    /* Step 1.3: Build ID remap plan */
    result = save_build_remap_plan(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 0.35f,
                                  "Serializing managers");
    if (result != NMO_OK) return result;

    /* Step 1.4: Serialize managers */
    result = save_serialize_managers(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 0.55f,
                                  "Serializing objects");
    if (result != NMO_OK) return result;

    /* Step 1.5: Serialize objects */
    result = save_serialize_objects(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 0.75f,
                                  "Building data section");
    if (result != NMO_OK) return result;

    /* Step 1.6: Build data section buffer */
    result = save_build_data_section(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 0.9f,
                                  "Building header1 section");
    if (result != NMO_OK) return result;

    /* Step 1.7: Build header1 buffer */
    result = save_build_header1(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_SERIALIZE, 1.0f,
                                  "Phase 1 complete");
    if (result != NMO_OK) return result;

    ctx->phase1_complete = 1;

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "Phase 1 complete: header1=%zu bytes, data=%zu bytes, %zu objects",
            ctx->header1_unpack_size, ctx->data_unpack_size, ctx->object_count);

    NMO_RETURN_OK();
}

nmo_status_t nmo_save_phase2_commit(nmo_save_context_t *ctx, const char *path) {
    if (ctx == NULL || path == NULL) {
        return SAVE_ERR(NMO_ERR_INVALID_ARGUMENT, "NULL argument");
    }

    if (!ctx->phase1_complete) {
        return SAVE_ERR(NMO_ERR_INVALID_STATE, "Phase 1 not complete");
    }

    nmo_log(ctx->logger, NMO_LOG_INFO, "=== Save Pipeline Phase 2: Pack & Commit ===");

    nmo_status_t result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_COMPRESS, 0.0f,
                                  "Compressing sections");
    if (result != NMO_OK) return result;

    /* Step 2.1: Compress sections (optional) */
    result = save_compress_sections(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_COMPRESS, 1.0f,
                                  "Compression complete");
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_CRC, 0.0f,
                                  "Computing CRC");
    if (result != NMO_OK) return result;

    /* Step 2.2: Write to file with CRC */
    result = save_write_file(ctx, path);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_CRC, 1.0f,
                                  "CRC complete");
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_WRITE, 1.0f,
                                  "Write complete");
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_POST_HOOKS, 0.0f,
                                  "Executing post-save hooks");
    if (result != NMO_OK) return result;

    /* Step 2.3: Execute post-save hooks */
    result = save_execute_post_hooks(ctx);
    if (result != NMO_OK) return result;

    result = save_report_progress(ctx, NMO_SAVE_PHASE_POST_HOOKS, 1.0f,
                                  "Post-save hooks complete");
    if (result != NMO_OK) return result;

    ctx->phase2_complete = 1;

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "Phase 2 complete: %zu bytes written to %s (CRC=0x%08X)",
            ctx->stats.total_file_size, path, ctx->stats.crc);

    NMO_RETURN_OK();
}

nmo_save_stats_t nmo_save_context_get_stats(const nmo_save_context_t *ctx) {
    if (ctx == NULL) {
        nmo_save_stats_t empty = {0};
        return empty;
    }
    return ctx->stats;
}

nmo_status_t nmo_save_file_ex(
    nmo_session_t *session,
    const char *path,
    const nmo_save_options_t *options)
{
    nmo_save_context_t *ctx = nmo_save_context_create(session, options);
    if (ctx == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Failed to create save context");
    }

    nmo_status_t result = nmo_save_phase1_layout(ctx);
    if (result != NMO_OK) {
        nmo_save_context_destroy(ctx);
        return result;
    }

    result = nmo_save_phase2_commit(ctx, path);
    nmo_save_context_destroy(ctx);

    return result;
}

/* ============================================================================
 * Phase 1 Implementation: Layout & Serialize
 * ============================================================================ */

static nmo_status_t save_validate_session(nmo_save_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.1: Validating session state");

    ctx->objects = nmo_object_repository_get_all(ctx->repo, &ctx->object_count);

    if (ctx->object_count == 0) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Cannot save empty session");
        return SAVE_ERR(NMO_ERR_INVALID_ARGUMENT, "Empty session");
    }

    if (ctx->type_reg == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Type registry not available");
        return SAVE_ERR(NMO_ERR_INVALID_STATE, "No type registry");
    }

    /* Build reference map */
    ctx->reference_map = (uint8_t *)nmo_arena_alloc(
        ctx->arena, ctx->object_count * sizeof(uint8_t), 1);

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

static nmo_status_t save_execute_pre_hooks(nmo_save_context_t *ctx) {
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
            int hook_result = nmo_manager_invoke_pre_save(manager, ctx->session);
            if (hook_result != NMO_OK) {
                nmo_log(ctx->logger, NMO_LOG_WARN,
                        "  Manager %u pre-save hook failed: %d", manager_id, hook_result);
            }
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t save_build_remap_plan(nmo_save_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.3: Building ID remap plan");

    ctx->remap_plan = nmo_id_remap_plan_create(
        ctx->repo, ctx->objects, ctx->object_count);

    if (ctx->remap_plan == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Failed to create ID remap plan");
        return SAVE_ERR(NMO_ERR_NOMEM, "ID remap plan allocation failed");
    }

    nmo_id_remap_table_t *remap_table = nmo_id_remap_plan_get_table(ctx->remap_plan);
    size_t remap_count = nmo_id_remap_table_get_count(remap_table);

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Created remap plan with %zu entries", remap_count);

    ctx->file_index_remap = nmo_id_remap_create(ctx->arena);
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

    NMO_RETURN_OK();
}

static nmo_status_t save_serialize_managers(nmo_save_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.4: Serializing manager chunks");

    nmo_id_remap_table_t *remap_table = ctx->file_index_remap;

    /* Get session manager data for round-trip preservation */
    uint32_t session_manager_count = 0;
    nmo_manager_data_t *session_managers = nmo_session_get_manager_data(
        ctx->session, &session_manager_count);

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
        ctx->arena,
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

            nmo_chunk_t *chunk = nmo_manager_invoke_save_data(manager, ctx->session);
            if (chunk == NULL) continue;

            nmo_manager_data_t *entry = &ctx->manager_entries[ctx->manager_entry_count++];
            entry->guid = manager->guid;
            entry->chunk = chunk;
            entry->data_size = (chunk->raw_data != NULL) ? (uint32_t)chunk->raw_size : 0;
            entry->flags = NMO_MANAGER_DATA_FLAG_DISPATCHED;

            if (chunk->raw_data == NULL && remap_table != NULL) {
                nmo_status_t remap_result = nmo_chunk_remap_object_ids(chunk, remap_table);
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

static nmo_status_t save_serialize_objects(nmo_save_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.5: Serializing object chunks");

    size_t serialized_count = 0;
    size_t reused_count = 0;
    size_t skipped_count = 0;
    const nmo_shadow_storage_t *shadow_storage = nmo_session_get_shadow_storage(ctx->session);

    nmo_id_remap_table_t *remap_table = ctx->file_index_remap;

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

        nmo_chunk_t *old_chunk = obj->chunk;
        obj->chunk = serialize_object_with_schema(
            obj, ctx->type_reg, ctx->arena, ctx->logger, shadow_storage);

        if (obj->chunk == NULL) {
            nmo_log(ctx->logger, NMO_LOG_ERROR,
                    "Failed to serialize object %u ('%s')",
                    obj->id, obj->name ? obj->name : "<unnamed>");
            return SAVE_ERR(NMO_ERR_INTERNAL, "Object serialization failed");
        }

        if (obj->chunk == old_chunk) {
            reused_count++;
        } else {
            if (obj->chunk != NULL && obj->chunk->raw_data == NULL && remap_table != NULL) {
                nmo_status_t remap_result = nmo_chunk_remap_object_ids(obj->chunk, remap_table);
                if (remap_result != NMO_OK) {
                    nmo_log(ctx->logger, NMO_LOG_WARN,
                            "    Failed to remap object IDs for object %u (code=%d)",
                            obj->id, remap_result);
                }
            }
            serialized_count++;
        }
    }

    ctx->stats.serialized_count = serialized_count;
    ctx->stats.reused_count = reused_count;

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "  Serialization: %zu new, %zu reused, %zu skipped",
            serialized_count, reused_count, skipped_count);

    NMO_RETURN_OK();
}

static nmo_status_t save_build_data_section(nmo_save_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.6: Building data section buffer");

    /* Build data section structure */
    nmo_data_section_t data_sect = {0};
    data_sect.manager_count = ctx->manager_entry_count;
    data_sect.managers = (ctx->manager_entry_count > 0) ? ctx->manager_entries : NULL;
    data_sect.object_count = (uint32_t)ctx->object_count;

    /* Allocate object data array */
    data_sect.objects = (nmo_object_data_t *)nmo_arena_alloc(
        ctx->arena,
        sizeof(nmo_object_data_t) * ctx->object_count,
        sizeof(void *));

    if (data_sect.objects == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Object data array allocation failed");
    }

    uint32_t file_version = ctx->file_info.file_version;
    if (file_version == 0) file_version = 8;

    nmo_id_remap_table_t *remap_table = NULL;
    if (file_version < 7) {
        remap_table = nmo_id_remap_plan_get_table(ctx->remap_plan);
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
            int lookup_result = nmo_id_remap_lookup(remap_table, ctx->objects[i]->id, &file_id);
            if (lookup_result != NMO_OK || file_id == 0) {
                nmo_log(ctx->logger, NMO_LOG_ERROR,
                        "Failed to map object ID for legacy save (runtime=%u)",
                        ctx->objects[i]->id);
                return SAVE_ERR(lookup_result, "Legacy object ID remap failed");
            }
            data_sect.objects[i].object_id = file_id;
        }
    }

    /* Calculate data section size */
    size_t data_size = nmo_data_section_calculate_size(&data_sect, file_version, ctx->arena);
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Data section unpack size: %zu bytes", data_size);

    /* Allocate buffer */
    ctx->data_buffer = nmo_arena_alloc(ctx->arena, data_size, 16);
    if (ctx->data_buffer == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Data buffer allocation failed");
    }

    /* Serialize data section */
    size_t bytes_written = 0;
    nmo_status_t result = nmo_data_section_serialize(
        &data_sect, file_version, ctx->data_buffer, data_size, &bytes_written, ctx->arena);

    if (result != NMO_OK) {
        return result;
    }

    ctx->data_unpack_size = bytes_written;
    ctx->stats.data_unpack_size = bytes_written;

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Data section serialized: %zu bytes", bytes_written);

    NMO_RETURN_OK();
}

static nmo_status_t save_build_header1(nmo_save_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "Step 1.7: Building header1 buffer");

    nmo_id_remap_table_t *remap_table = nmo_id_remap_plan_get_table(ctx->remap_plan);
    uint32_t file_version = ctx->file_info.file_version;
    if (file_version == 0) {
        file_version = 8;
    }

    /* Build object descriptors */
    ctx->obj_descs = (nmo_object_desc_t *)nmo_arena_alloc(
        ctx->arena,
        sizeof(nmo_object_desc_t) * ctx->object_count,
        sizeof(void *));

    if (ctx->obj_descs == NULL) {
        return SAVE_ERR(NMO_ERR_NOMEM, "Object descriptors allocation failed");
    }

    for (size_t i = 0; i < ctx->object_count; i++) {
        nmo_object_t *obj = ctx->objects[i];

        nmo_object_id_t file_id = 0;
        int lookup_result = nmo_id_remap_lookup(remap_table, obj->id, &file_id);
        if (lookup_result != NMO_OK) {
            nmo_log(ctx->logger, NMO_LOG_ERROR,
                    "Failed to lookup file ID for object %u", obj->id);
            return SAVE_ERR(lookup_result, "ID remap lookup failed");
        }

        ctx->obj_descs[i].file_id = file_id;
        ctx->obj_descs[i].class_id = obj->class_id;
        ctx->obj_descs[i].name = (char *)obj->name;
        ctx->obj_descs[i].file_index = 0;

        uint32_t descriptor_flags = obj->flags;
        if (ctx->reference_map[i]) {
            descriptor_flags |= NMO_OBJECT_REFERENCE_FLAG;
        }
        ctx->obj_descs[i].flags = descriptor_flags;
    }

    /* Build plugin dependencies */
    uint32_t stored_plugin_count = 0;
    nmo_plugin_dep_t *stored_plugin_deps = nmo_session_get_plugin_dependencies(
        ctx->session, &stored_plugin_count);

    if (stored_plugin_deps != NULL && stored_plugin_count > 0) {
        ctx->plugin_deps = stored_plugin_deps;
        ctx->plugin_count = stored_plugin_count;
    } else {
        ctx->plugin_deps = NULL;
        ctx->plugin_count = 0;
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
        hdr1.included_files = (nmo_included_file_desc_t *)nmo_arena_alloc(
            ctx->arena,
            sizeof(nmo_included_file_desc_t) * session_included_count,
            sizeof(void *));

        if (hdr1.included_files == NULL) {
            return SAVE_ERR(NMO_ERR_NOMEM, "Included file descriptors allocation failed");
        }

        for (uint32_t i = 0; i < session_included_count; i++) {
            const nmo_included_file_t *entry = &session_included_files[i];
            const char *base_name = nmo_basename(entry->name);
            const char *name_copy = nmo_arena_strdup(ctx->arena, base_name);
            if (name_copy == NULL) {
                return SAVE_ERR(NMO_ERR_NOMEM, "Included file name allocation failed");
            }

            const int metadata_only = (entry->attributes & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) != 0;
            uint32_t data_size = (entry->data != NULL && !metadata_only) ? entry->size : 0u;

            hdr1.included_files[i].name = (char *)name_copy;
            hdr1.included_files[i].data_size = data_size;
        }
    }

    /* First pass: serialize to get Header1 unpack size */
    void *header1_probe = NULL;
    size_t header1_probe_size = 0;
    nmo_status_t result = nmo_header1_serialize(
        &hdr1, &header1_probe, &header1_probe_size, ctx->arena);

    if (result != NMO_OK) {
        return result;
    }
    (void)header1_probe;

    /* Fill FileIndex offsets (uncompressed file buffer) */
    result = save_fill_file_indices(ctx, header1_probe_size, file_version);
    if (result != NMO_OK) {
        return result;
    }

    /* Final serialize with correct FileIndex values */
    result = nmo_header1_serialize(
        &hdr1, &ctx->header1_buffer, &ctx->header1_unpack_size, ctx->arena);

    if (result != NMO_OK) {
        return result;
    }

    ctx->stats.header1_unpack_size = ctx->header1_unpack_size;

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Header1 serialized: %zu bytes", ctx->header1_unpack_size);

    NMO_RETURN_OK();
}

/* ============================================================================
 * Phase 2 Implementation: Pack & Commit
 * ============================================================================ */

static nmo_status_t save_compress_sections(nmo_save_context_t *ctx) {
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
        void *compressed = nmo_arena_alloc(ctx->arena, bound, 16);
        if (compressed == NULL) {
            return SAVE_ERR(NMO_ERR_NOMEM, "Header1 compression buffer allocation failed");
        }

        mz_ulong dest_len = bound;
        int comp_result = mz_compress2(
            (unsigned char *)compressed,
            &dest_len,
            (const unsigned char *)ctx->header1_buffer,
            (mz_ulong)ctx->header1_unpack_size,
            compression_level);

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
        void *compressed = nmo_arena_alloc(ctx->arena, bound, 16);
        if (compressed == NULL) {
            return SAVE_ERR(NMO_ERR_NOMEM, "Data compression buffer allocation failed");
        }

        mz_ulong dest_len = bound;
        int comp_result = mz_compress2(
            (unsigned char *)compressed,
            &dest_len,
            (const unsigned char *)ctx->data_buffer,
            (mz_ulong)ctx->data_unpack_size,
            compression_level);

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

    /* Calculate overall compression ratio */
    size_t total_unpack = ctx->header1_unpack_size + ctx->data_unpack_size;
    size_t total_pack = ctx->header1_pack_size + ctx->data_pack_size;
    ctx->stats.compression_ratio = (total_unpack > 0)
        ? (double)total_pack / (double)total_unpack : 1.0;

    NMO_RETURN_OK();
}

static nmo_status_t save_write_file(nmo_save_context_t *ctx, const char *path) {
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
        crc = mz_adler32(crc, (const uint8_t *)&header, 32);               /* Part0 */
        crc = mz_adler32(crc, (const uint8_t *)&header.object_count, 56);  /* Part1 */
        crc = mz_adler32(crc, (const uint8_t *)ctx->header1_packed, ctx->header1_pack_size);
        crc = mz_adler32(crc, (const uint8_t *)ctx->data_packed, ctx->data_pack_size);
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

    /* Open output file */
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Opening output file: %s", path);
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_WRITE | NMO_IO_CREATE);
    if (io == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "Failed to open output file: %s", path);
        return SAVE_ERR(NMO_ERR_FILE_NOT_FOUND, "Cannot open output file");
    }

    /* Write file header */
    nmo_status_t header_result = nmo_file_header_serialize(&header, io);
    if (header_result != NMO_OK) {
        nmo_io_close(io);
        return header_result;
    }

    /* Write Header1 */
    if (ctx->header1_pack_size > 0) {
        int write_result = nmo_io_write(io, ctx->header1_packed, ctx->header1_pack_size);
        if (write_result != NMO_OK) {
            nmo_io_close(io);
            return SAVE_ERR(write_result, "Header1 write failed");
        }
    }

    /* Write Data section */
    if (ctx->data_pack_size > 0) {
        int write_result = nmo_io_write(io, ctx->data_packed, ctx->data_pack_size);
        if (write_result != NMO_OK) {
            nmo_io_close(io);
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
        if (nmo_io_write(io, shadow_blob, shadow_size) != NMO_OK) {
            nmo_io_close(io);
            return SAVE_ERR(NMO_ERR_CANT_WRITE_FILE, "Included files shadow blob write failed");
        }
    } else if (!strip_included && session_included_files != NULL && session_included_count > 0) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Writing %u included files", session_included_count);

        for (uint32_t i = 0; i < session_included_count; i++) {
            const nmo_included_file_t *entry = &session_included_files[i];
            const char *name = nmo_basename(entry->name ? entry->name : "");
            uint32_t name_len = (uint32_t)strlen(name);
            const int metadata_only = (entry->attributes & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) != 0;
            uint32_t payload_size = (entry->data != NULL && !metadata_only) ? entry->size : 0u;

            if (nmo_io_write_u32(io, name_len) != NMO_OK ||
                (name_len > 0 && nmo_io_write(io, name, name_len) != NMO_OK) ||
                nmo_io_write_u32(io, payload_size) != NMO_OK) {
                nmo_io_close(io);
                return SAVE_ERR(NMO_ERR_CANT_WRITE_FILE, "Included file metadata write failed");
            }

            if (payload_size > 0 && entry->data != NULL) {
                if (nmo_io_write(io, entry->data, payload_size) != NMO_OK) {
                    nmo_io_close(io);
                    return SAVE_ERR(NMO_ERR_CANT_WRITE_FILE, "Included file payload write failed");
                }
            } else if (entry->size > 0 && metadata_only) {
                nmo_log(ctx->logger, NMO_LOG_WARN,
                        "  Included file '%s' metadata-only; payload omitted", name);
            }
        }
    }

    nmo_io_close(io);

    nmo_log(ctx->logger, NMO_LOG_INFO, "  Write complete: %u bytes", file_size);

    NMO_RETURN_OK();
}

static nmo_status_t save_execute_post_hooks(nmo_save_context_t *ctx) {
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
            int hook_result = nmo_manager_invoke_post_save(manager, ctx->session);
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

static nmo_chunk_t *serialize_object_with_schema(
    nmo_object_t *obj,
    nmo_type_registry_t *type_reg,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage)
{
    if (!obj || !arena || !type_reg) {
        return NULL;
    }

    /* If object already has a chunk and no data (not modified), reuse it */
    if (obj->chunk != NULL && obj->data == NULL) {
        nmo_log(logger, NMO_LOG_DEBUG,
                "    Reusing existing chunk for object %u (unmodified)", obj->id);
        return obj->chunk;
    }

    /* Find schema with inheritance fallback */
    const nmo_type_descriptor_t *schema_type =
        nmo_type_registry_find_by_class_id_inherited(type_reg, obj->class_id);

    if (schema_type == NULL) {
        nmo_log(logger, NMO_LOG_WARN,
                "    No schema found for class 0x%08X, preserving raw chunk", obj->class_id);
        return obj->chunk;
    }

    /* Check if schema has vtable with serialize function */
    if (schema_type->vtable == NULL || schema_type->vtable->serialize == NULL) {
        nmo_log(logger, NMO_LOG_WARN,
                "    Schema '%s' has no write vtable, preserving raw chunk", schema_type->name);
        return obj->chunk;
    }

    /* If object has no data, preserve existing chunk or create empty */
    if (obj->data == NULL) {
        nmo_log(logger, NMO_LOG_WARN, "    Object %u has no data to serialize", obj->id);
        if (obj->chunk == NULL) {
            nmo_chunk_t *empty_chunk = nmo_chunk_create(arena);
            if (empty_chunk) {
                empty_chunk->class_id = obj->class_id;
                empty_chunk->chunk_version = 7;
                empty_chunk->data_version = 7;
                empty_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
                nmo_chunk_start_write(empty_chunk);
                nmo_chunk_close(empty_chunk);
                return empty_chunk;
            }
        }
        return obj->chunk;
    }

    /* Create new chunk for writing */
    nmo_chunk_t *new_chunk = nmo_chunk_create(arena);
    if (new_chunk == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "    Failed to create chunk for object %u", obj->id);
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

    nmo_status_t result = nmo_chunk_start_write(new_chunk);
    if (result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR,
                "    Failed to start chunk write for object %u", obj->id);
        return NULL;
    }

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE);

    /* Call vtable serialize function */
    result = schema_type->vtable->serialize(obj->data, new_chunk, schema_type, &ser_ctx);

    if (result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR,
                "    Failed to serialize object %u with schema '%s'",
                obj->id, schema_type->name);
        return obj->chunk;  /* Fall back to existing chunk */
    }

    if (shadow_storage != NULL) {
        size_t tail_size = 0;
        const void *tail = nmo_shadow_get_chunk_tail(shadow_storage, obj->id, &tail_size);
        if (tail != NULL && tail_size > 0) {
            nmo_status_t tail_result = nmo_chunk_write_buffer_no_size(new_chunk, tail, tail_size);
            if (tail_result != NMO_OK) {
                nmo_log(logger, NMO_LOG_WARN,
                        "    Failed to append shadow tail for object %u (code=%d)",
                        obj->id, tail_result);
            }
        }
    }

    nmo_chunk_close(new_chunk);

    if (old_chunk != NULL && new_chunk->data.count == 0) {
        nmo_log(logger, NMO_LOG_WARN,
                "    Serialized object %u is empty; preserving original chunk", obj->id);
        return (nmo_chunk_t *)old_chunk;
    }

    nmo_log(logger, NMO_LOG_DEBUG,
            "    Serialized object %u using schema '%s' (%zu bytes)",
            obj->id, schema_type->name, new_chunk->data.count * 4);

    return new_chunk;
}
