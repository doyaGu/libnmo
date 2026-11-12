/**
 * @file nmo_session_pipeline.h
 * @brief Advanced session pipeline/state staging helpers.
 *
 * These APIs are public for in-tree load/save plumbing, diagnostics staging,
 * and specialized C consumers. They are intentionally not part of the default
 * stable workflow/query surface exposed by nmo_session.h.
 */

#ifndef NMO_SESSION_PIPELINE_H
#define NMO_SESSION_PIPELINE_H

#include "session/nmo_session.h"
#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object_index nmo_object_index_t;
typedef struct nmo_session_plugin_dependency_status nmo_session_plugin_dependency_status_t;
typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_chunk_pool nmo_chunk_pool_t;
typedef struct nmo_shadow_storage nmo_shadow_storage_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_ref_graph nmo_ref_graph_t;
typedef struct nmo_behavior_index nmo_behavior_index_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_index_stats nmo_index_stats_t;

/*
 * Pipeline staging and raw session-state setters remain public for advanced C
 * consumers and in-tree loader/save orchestration, but they are not the
 * default binding-facing workflow contract.
 */
#define NMO_SESSION_PIPELINE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SESSION_PIPELINE_API_TIER NMO_API_TIER_ADVANCED_C

typedef struct nmo_session_behavior_interface_diagnostics {
    int attempted;
    int available;
    nmo_status_t status;
    size_t attempted_count;
    size_t parsed_count;
    size_t failed_count;
    size_t skipped_no_arena_count;
    size_t allocation_failure_count;
    nmo_object_id_t first_error_object_id;
    uint32_t first_error_file_id;
    uint32_t first_error_chunk_version;
    uint32_t first_error_data_version;
    size_t first_error_reader_offset;
    size_t first_error_chunk_dwords;
} nmo_session_behavior_interface_diagnostics_t;

NMO_API nmo_chunk_pool_t *nmo_session_get_chunk_pool(
    const nmo_session_t *session);
NMO_API nmo_shadow_storage_t *nmo_session_get_shadow_storage(
    const nmo_session_t *session);
NMO_API nmo_chunk_pool_t *nmo_session_ensure_chunk_pool(
    nmo_session_t *session,
    size_t initial_capacity_hint);
/**
 * @brief An object id without the reference flag
 *
 * A file marks the ids of objects it only refers to with
 * NMO_OBJECT_REFERENCE_FLAG; the id itself is the rest.
 */
NMO_API uint32_t nmo_id_sanitize(uint32_t raw_id);
NMO_API nmo_object_index_t *nmo_session_get_object_index(
    const nmo_session_t *session);
NMO_API nmo_status_t nmo_session_get_object_index_stats(
    const nmo_session_t *session,
    nmo_index_stats_t *stats);

/**
 * @brief Set the session-owned object index directly.
 *
 * Advanced staging helper used by the runtime load pipeline.
 */
NMO_API void nmo_session_set_object_index(
    nmo_session_t *session,
    nmo_object_index_t *index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_PIPELINE_H */
