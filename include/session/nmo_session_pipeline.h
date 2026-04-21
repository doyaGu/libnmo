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

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;
typedef struct nmo_file_info nmo_file_info_t;
typedef struct nmo_manager_data nmo_manager_data_t;
typedef struct nmo_plugin_dep nmo_plugin_dep_t;
typedef struct nmo_object_index nmo_object_index_t;
typedef struct nmo_runtime_load_stats nmo_runtime_load_stats_t;
typedef struct nmo_session_plugin_dependency_status nmo_session_plugin_dependency_status_t;

/*
 * Pipeline staging and raw session-state setters remain public for advanced C
 * consumers and in-tree loader/save orchestration, but they are not the
 * default binding-facing workflow contract.
 */
#define NMO_SESSION_PIPELINE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SESSION_PIPELINE_API_TIER NMO_API_TIER_ADVANCED_C

/**
 * @brief Set file info.
 *
 * Advanced staging API used by the load pipeline and low-level tooling.
 */
NMO_API nmo_status_t nmo_session_set_file_info(
    nmo_session_t *session,
    const nmo_file_info_t *info);

/**
 * @brief Install manager serialization data for a loaded session.
 *
 * Borrowed pointers are expected to remain arena-owned by the load pipeline.
 */
NMO_API void nmo_session_set_manager_data(
    nmo_session_t *session,
    nmo_manager_data_t *data,
    uint32_t count);

/**
 * @brief Install plugin dependency records for a loaded session.
 */
NMO_API nmo_status_t nmo_session_set_plugin_dependencies(
    nmo_session_t *session,
    nmo_plugin_dep_t *deps,
    uint32_t count);

/**
 * @brief Rebuild plugin dependency diagnostics from current staged file state.
 */
NMO_API nmo_status_t nmo_session_refresh_plugin_diagnostics(
    nmo_session_t *session);

/**
 * @brief Set the session-owned object index directly.
 *
 * Advanced staging helper used by the runtime load pipeline.
 */
NMO_API void nmo_session_set_object_index(
    nmo_session_t *session,
    nmo_object_index_t *index);

/**
 * @brief Store runtime load diagnostics for later retrieval.
 */
NMO_API void nmo_session_set_runtime_load_stats(
    nmo_session_t *session,
    const nmo_runtime_load_stats_t *stats);

/**
 * @brief Install plugin dependency diagnostics directly.
 */
NMO_API void nmo_session_set_plugin_diagnostics(
    nmo_session_t *session,
    const nmo_session_plugin_dependency_status_t *entries,
    size_t entry_count,
    size_t missing_count,
    size_t outdated_count,
    int extension_registry_available);

/**
 * @brief Store opaque parsed file-header bytes in the session arena.
 *
 * Exported for the in-library parser and advanced load tooling.
 */
NMO_API void nmo_session_set_file_header(
    nmo_session_t *session,
    const void *header,
    size_t header_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_PIPELINE_H */
