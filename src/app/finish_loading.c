/**
 * @file finish_loading.c
 * @brief FinishLoading phase implementation (Phase 5.1)
 * 
 * ARCHITECTURE NOTE: This file belongs in the APP layer, not SESSION layer.
 * It orchestrates high-level operations using session, context, and lower layers.
 * 
 * Implements the finish loading stage that occurs after initial object parsing.
 * This includes:
 * - Reference resolution integration
 * - Object index building
 * - Manager post-load processing
 * - Statistics gathering
 * 
 * Reference: CKFile::FinishLoading in reference/src/CKFile.cpp
 */

#include "app/nmo_finish_loading.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_reference_resolver.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "core/nmo_logger.h"
#include "core/nmo_arena.h"
#include <string.h>

/**
 * @brief Finish loading context
 * 
 * Contains state for the finish loading operation
 */
typedef struct nmo_finish_loading_context {
    nmo_session_t *session;
    nmo_arena_t *arena;
    nmo_logger_t *logger;
    
    /* Reference resolution results */
    nmo_reference_resolver_t *resolver;
    
    /* Object index */
    nmo_object_index_t *index;
    
    /* Flags */
    uint32_t flags;

    /* Diagnostics */
    nmo_finish_loading_stats_t stats;
    uint32_t object_postload_invoked;
    uint32_t object_postload_errors;
    uint32_t manager_errors;
} nmo_finish_loading_context_t;

/**
 * Phase 9: Resolve references
 * 
 * Integrates the reference resolver to resolve all object references
 * that were loaded from the file.
 */
static int finish_loading_phase_9_resolve_references(nmo_finish_loading_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "FinishLoading Phase 9: Resolving references");
    
    /* Check if reference resolution is enabled */
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Checking flags...");
    if (!(ctx->flags & NMO_FINISH_LOAD_RESOLVE_REFERENCES)) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Reference resolution disabled by flags");
        return NMO_OK;
    }
    
    /* Reuse session-owned resolver created during load path, or create on demand */
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Getting session reference resolver...");
    ctx->resolver = nmo_session_get_reference_resolver(ctx->session);
    if (ctx->resolver == NULL) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Resolver missing, creating session-owned resolver...");
        ctx->resolver = nmo_session_ensure_reference_resolver(ctx->session);
    }
    
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Reference resolver created: %p", (void*)ctx->resolver);
    
    if (ctx->resolver == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "  Failed to create reference resolver");
        return NMO_ERR_NOMEM;
    }
    
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Reference resolver created successfully");

    /* Resolver strategy chain is implemented inside resolver:
     * custom-per-class -> ID/name/class default -> fuzzy fallback.
     * Current finish-loading flags do not expose additional strategy knobs. */
    nmo_log(ctx->logger, NMO_LOG_INFO,
            "  Using built-in resolver strategy chain (custom/default/fuzzy)");
    
    /* Resolve all references */
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Calling nmo_reference_resolver_resolve_all...");
    int result = nmo_reference_resolver_resolve_all(ctx->resolver);
    nmo_log(ctx->logger, NMO_LOG_INFO, "  nmo_reference_resolver_resolve_all returned %d", result);
    if (result != NMO_OK) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "  Reference resolution failed: %d", result);
        
        /* Check if strict mode is enabled */
        if (ctx->flags & NMO_FINISH_LOAD_STRICT_REFERENCES) {
            return result;
        } else {
            nmo_log(ctx->logger, NMO_LOG_WARN, "  Continuing despite reference resolution failures");
        }
    }
    
    /* Get statistics */
    nmo_reference_stats_t stats;
    result = nmo_reference_resolver_get_stats(ctx->resolver, &stats);
    if (result == NMO_OK) {
        if (stats.total_count == 0) {
            nmo_log(ctx->logger, NMO_LOG_INFO, "  No reference descriptors registered during load");
        } else {
            nmo_log(ctx->logger, NMO_LOG_INFO,
                    "  References resolved: %u total, %u resolved, %u unresolved",
                    stats.total_count, stats.resolved_count, stats.unresolved_count);

            ctx->stats.references.total = stats.total_count;
            ctx->stats.references.resolved = stats.resolved_count;
            ctx->stats.references.unresolved = stats.unresolved_count;
            ctx->stats.references.ambiguous = stats.ambiguous_count;

            if (stats.unresolved_count > 0) {
                nmo_log(ctx->logger, NMO_LOG_WARN, "  %u references remain unresolved", 
                        stats.unresolved_count);

                nmo_object_ref_t **unresolved_refs = NULL;
                size_t unresolved_ref_count = 0;
                if (nmo_reference_resolver_get_unresolved(ctx->resolver,
                                                          &unresolved_refs,
                                                          &unresolved_ref_count) == NMO_OK &&
                    unresolved_refs != NULL && unresolved_ref_count > 0) {
                    size_t preview_count = unresolved_ref_count < 8 ? unresolved_ref_count : 8;
                    ctx->stats.references.unresolved_preview_count = (uint32_t)preview_count;
                    for (size_t i = 0; i < preview_count; i++) {
                        nmo_object_ref_t *ref = unresolved_refs[i];
                        if (ref == NULL) {
                            continue;
                        }
                        ctx->stats.references.unresolved_preview[i].id = ref->id;
                        ctx->stats.references.unresolved_preview[i].class_id = ref->class_id;
                        nmo_log(ctx->logger, NMO_LOG_WARN,
                                "    unresolved[%zu]: id=%u class=0x%08X name='%s'",
                                i,
                                ref->id,
                                ref->class_id,
                                ref->name ? ref->name : "");
                    }

                    if (unresolved_ref_count > preview_count) {
                        nmo_log(ctx->logger, NMO_LOG_WARN,
                                "    ... %zu more unresolved references omitted",
                                unresolved_ref_count - preview_count);
                    }
                }

                if (ctx->flags & NMO_FINISH_LOAD_STRICT_REFERENCES) {
                    nmo_log(ctx->logger, NMO_LOG_ERROR,
                            "  Strict reference resolution enabled - aborting load");
                    return NMO_ERR_VALIDATION_FAILED;
                }
            }
        }
    }
    
    return NMO_OK;
}

/**
 * Phase 10: Object post-load hooks
 *
 * Invokes per-type finish_loading hooks (if registered) for all loaded objects.
 */
static int finish_loading_phase_10_object_postload(nmo_finish_loading_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "FinishLoading Phase 10: Object post-load hooks");

    if (!(ctx->flags & NMO_FINISH_LOAD_OBJECT_POSTLOAD)) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Object post-load disabled by flags");
        return NMO_OK;
    }

    nmo_context_t *context = nmo_session_get_context(ctx->session);
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(context);
    nmo_object_repository_t *repo = nmo_session_get_repository(ctx->session);

    if (type_rt == NULL || type_rt->types == NULL || repo == NULL) {
        nmo_log(ctx->logger, NMO_LOG_WARN, "  Type runtime or repository unavailable, skipping object post-load");
        return NMO_OK;
    }

    size_t object_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    if (object_count > 0 && objects == NULL) {
        return NMO_ERR_NOMEM;
    }

    uint32_t hook_count = 0;
    uint32_t error_count = 0;

    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects[i];
        if (obj == NULL || obj->state == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_class_id_inherited(type_rt->types, obj->class_id);
        if (type == NULL || type->finish_loading == NULL) {
            continue;
        }

        hook_count++;
        nmo_status_t hook_result = type->finish_loading(obj->state, ctx->arena, repo);
        if (hook_result != NMO_OK) {
            error_count++;
            nmo_log(ctx->logger, NMO_LOG_WARN,
                    "  Object ID=%u class=0x%08X (%s) finish_loading hook failed: %d",
                    obj->id,
                    obj->class_id,
                    type->name ? type->name : "<unnamed>",
                    hook_result);
        }
    }

    nmo_log(ctx->logger, NMO_LOG_INFO,
            "  Object post-load hooks: invoked=%u errors=%u",
            hook_count,
            error_count);

    ctx->object_postload_invoked = hook_count;
    ctx->object_postload_errors = error_count;

    if (error_count > 0 && (ctx->flags & NMO_FINISH_LOAD_STRICT_OBJECTS)) {
        nmo_log(ctx->logger, NMO_LOG_ERROR,
                "  Strict object post-load enabled - aborting load");
        return NMO_ERR_VALIDATION_FAILED;
    }

    return NMO_OK;
}

/**
 * Phase 11: Build object indexes
 * 
 * Builds indexes for fast object lookup by class, name, and GUID
 */
static int finish_loading_phase_10_build_indexes(nmo_finish_loading_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "FinishLoading Phase 11: Building object indexes");
    
    /* Check if index building is enabled */
    if (!(ctx->flags & NMO_FINISH_LOAD_BUILD_INDEXES)) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Index building disabled by flags");
        return NMO_OK;
    }
    
    /* Create object index */
    nmo_object_repository_t *repo = nmo_session_get_repository(ctx->session);
    ctx->index = nmo_object_index_create(repo, ctx->arena, NULL);
    
    if (ctx->index == NULL) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "  Failed to create object index");
        return NMO_ERR_NOMEM;
    }
    
    /* Determine which indexes to build based on flags */
    uint32_t index_flags = 0;
    
    if (ctx->flags & NMO_FINISH_LOAD_INDEX_CLASS) {
        index_flags |= NMO_INDEX_BUILD_CLASS;
    }
    if (ctx->flags & NMO_FINISH_LOAD_INDEX_NAME) {
        index_flags |= NMO_INDEX_BUILD_NAME;
    }
    if (ctx->flags & NMO_FINISH_LOAD_INDEX_GUID) {
        index_flags |= NMO_INDEX_BUILD_GUID;
    }
    
    /* Default: build all indexes */
    if (index_flags == 0) {
        index_flags = NMO_INDEX_BUILD_ALL;
    }
    
    /* Build indexes */
    int result = nmo_object_index_build(ctx->index, index_flags);
    if (result != NMO_OK) {
        nmo_log(ctx->logger, NMO_LOG_ERROR, "  Failed to build indexes: %d", result);
        return result;
    }
    
    /* Get statistics */
    nmo_index_stats_t stats;
    result = nmo_object_index_get_stats(ctx->index, &stats);
    if (result == NMO_OK) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Indexes built: %zu objects", stats.total_objects);
        nmo_log(ctx->logger, NMO_LOG_INFO, "    Class index: %zu entries", stats.class_index_entries);
        nmo_log(ctx->logger, NMO_LOG_INFO, "    Name index: %zu entries", stats.name_index_entries);
        nmo_log(ctx->logger, NMO_LOG_INFO, "    GUID index: %zu entries", stats.guid_index_entries);
        nmo_log(ctx->logger, NMO_LOG_INFO, "    Memory usage: %zu bytes", stats.memory_usage);
        ctx->stats.indexes.class_entries = stats.class_index_entries;
        ctx->stats.indexes.name_entries = stats.name_index_entries;
        ctx->stats.indexes.guid_entries = stats.guid_index_entries;
        ctx->stats.indexes.memory_usage = stats.memory_usage;
        ctx->stats.total_objects = stats.total_objects;
    }
    
    return NMO_OK;
}

/**
 * Phase 12: Manager post-load processing
 * 
 * Invokes manager post-load hooks to allow managers to process
 * loaded data and update internal state.
 */
static int finish_loading_phase_11_manager_postload(nmo_finish_loading_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "FinishLoading Phase 12: Manager post-load processing");
    
    /* Check if manager post-load is enabled */
    if (!(ctx->flags & NMO_FINISH_LOAD_MANAGER_POSTLOAD)) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  Manager post-load disabled by flags");
        return NMO_OK;
    }
    
    /* Get manager registry from context */
    nmo_context_t *context = nmo_session_get_context(ctx->session);
    nmo_manager_registry_t *manager_reg = nmo_context_get_manager_registry(context);
    
    if (manager_reg == NULL) {
        nmo_log(ctx->logger, NMO_LOG_INFO, "  No manager registry available");
        return NMO_OK;
    }
    
    uint32_t manager_count = nmo_manager_registry_get_count(manager_reg);
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Processing %u managers", manager_count);
    
    int errors = 0;
    
    /* Invoke post-load hook for each manager */
    for (uint32_t i = 0; i < manager_count; i++) {
        uint32_t manager_id = nmo_manager_registry_get_id_at(manager_reg, i);
        nmo_manager_t *manager = (nmo_manager_t *)nmo_manager_registry_get(manager_reg, manager_id);
        
        if (manager != NULL) {
            nmo_log(ctx->logger, NMO_LOG_INFO, "  Invoking post-load for manager %u", manager_id);
            
            int result = nmo_manager_invoke_post_load(manager, ctx->session);
            if (result != NMO_OK) {
                nmo_log(ctx->logger, NMO_LOG_WARN, "  Manager %u post-load failed: %d", 
                        manager_id, result);
                errors++;
            }
        }
    }
    
    if (errors > 0) {
        nmo_log(ctx->logger, NMO_LOG_WARN, "  %d manager(s) reported errors during post-load", errors);
    }
    ctx->manager_errors = errors;
    
    return NMO_OK;
}

/**
 * Phase 13: Gather statistics
 * 
 * Collects and logs final loading statistics
 */
static int finish_loading_phase_12_gather_stats(nmo_finish_loading_context_t *ctx) {
    nmo_log(ctx->logger, NMO_LOG_INFO, "FinishLoading Phase 13: Gathering statistics");
    
    /* Get object count */
    nmo_object_repository_t *repo = nmo_session_get_repository(ctx->session);
    size_t object_count = nmo_object_repository_get_count(repo);
    
    nmo_log(ctx->logger, NMO_LOG_INFO, "  Total objects loaded: %zu", object_count);
    ctx->stats.flags = ctx->flags;
    ctx->stats.total_objects = object_count;
    ctx->stats.object_postload.invoked = ctx->object_postload_invoked;
    ctx->stats.object_postload.errors = ctx->object_postload_errors;
    ctx->stats.manager_errors = ctx->manager_errors;
    
    /* Reference resolution stats */
    if (ctx->resolver != NULL) {
        nmo_reference_stats_t ref_stats;
        if (nmo_reference_resolver_get_stats(ctx->resolver, &ref_stats) == NMO_OK) {
            nmo_log(ctx->logger, NMO_LOG_INFO, "  References: %u total, %u resolved, %u unresolved",
                    ref_stats.total_count, ref_stats.resolved_count, 
                    ref_stats.unresolved_count);
        }
    }
    
    /* Index stats */
    if (ctx->index != NULL) {
        nmo_index_stats_t idx_stats;
        if (nmo_object_index_get_stats(ctx->index, &idx_stats) == NMO_OK) {
            nmo_log(ctx->logger, NMO_LOG_INFO, "  Index entries: class=%zu, name=%zu, GUID=%zu",
                    idx_stats.class_index_entries, idx_stats.name_index_entries,
                    idx_stats.guid_index_entries);
        }
    }
    
    /* File info */
    nmo_file_info_t file_info = nmo_session_get_file_info(ctx->session);
    nmo_log(ctx->logger, NMO_LOG_INFO, "  File version: %u, CK version: 0x%08X",
            file_info.file_version, file_info.ck_version);

    nmo_session_set_finish_loading_stats(ctx->session, &ctx->stats);
    
    return NMO_OK;
}

/* ==================== Public API ==================== */

/**
 * Execute finish loading phase
 * 
 * This function coordinates the final stages of file loading:
 * - Phase 9: Reference resolution
 * - Phase 10: Object post-load hooks
 * - Phase 11: Index building
 * - Phase 12: Manager post-load
 * - Phase 13: Statistics
 */
int nmo_session_finish_loading(nmo_session_t *session, uint32_t flags) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Initialize context */
    nmo_finish_loading_context_t ctx;
    memset(&ctx, 0, sizeof(nmo_finish_loading_context_t));
    
    ctx.session = session;
    ctx.arena = nmo_session_get_arena(session);
    ctx.flags = flags;
    
    nmo_context_t *context = nmo_session_get_context(session);
    ctx.logger = nmo_context_get_logger(context);
    
    nmo_log(ctx.logger, NMO_LOG_INFO, "Starting FinishLoading phase");
    
    /* Phase 9: Resolve references */
    int result = finish_loading_phase_9_resolve_references(&ctx);
    if (result != NMO_OK) {
        nmo_log(ctx.logger, NMO_LOG_ERROR, "FinishLoading Phase 9 failed: %d", result);
        return result;
    }
    
    /* Phase 10: Object post-load hooks */
    result = finish_loading_phase_10_object_postload(&ctx);
    if (result != NMO_OK) {
        nmo_log(ctx.logger, NMO_LOG_ERROR, "FinishLoading Phase 10 failed: %d", result);
        return result;
    }

    /* Phase 11: Build indexes */
    result = finish_loading_phase_10_build_indexes(&ctx);
    if (result != NMO_OK) {
        nmo_log(ctx.logger, NMO_LOG_ERROR, "FinishLoading Phase 11 failed: %d", result);
        return result;
    }
    
    /* Store index in session for later use */
    if (ctx.index != NULL) {
        nmo_session_set_object_index(session, ctx.index);
    }
    
    /* Phase 12: Manager post-load */
    result = finish_loading_phase_11_manager_postload(&ctx);
    if (result != NMO_OK) {
        nmo_log(ctx.logger, NMO_LOG_ERROR, "FinishLoading Phase 12 failed: %d", result);
        /* Continue despite manager errors unless strict mode */
        if (flags & NMO_FINISH_LOAD_STRICT_MANAGERS) {
            return result;
        }
    }
    
    /* Phase 13: Gather statistics */
    result = finish_loading_phase_12_gather_stats(&ctx);
    if (result != NMO_OK) {
        nmo_log(ctx.logger, NMO_LOG_WARN, "FinishLoading Phase 13 failed: %d", result);
        /* Statistics failure is not critical */
    }
    
    nmo_log(ctx.logger, NMO_LOG_INFO, "FinishLoading phase completed successfully");
    
    return NMO_OK;
}
