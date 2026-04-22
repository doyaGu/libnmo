/**
 * @file nmo_runtime_result.h
 * @brief Stable structured workflow results for session/runtime object operations
 */

#ifndef NMO_SESSION_RUNTIME_RESULT_H
#define NMO_SESSION_RUNTIME_RESULT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/*
 * Stable structured result snapshots for binding-facing workflow consumers.
 * These helpers preserve the narrow session/runtime workflow boundary without
 * exposing raw runtime request/report contracts directly.
 */
#define NMO_RUNTIME_RESULT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_RUNTIME_RESULT_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef struct nmo_copy_result {
    size_t copied_count;
    size_t affected_count;
    uint32_t manager_event_errors;
    uint32_t object_hook_errors;
} nmo_copy_result_t;

typedef struct nmo_destroy_result {
    size_t deleted_count;
    size_t affected_count;
    uint32_t manager_event_errors;
    uint32_t object_hook_errors;
} nmo_destroy_result_t;

typedef struct nmo_preview_destroy_result {
    nmo_object_id_t *ids;
    size_t count;
} nmo_preview_destroy_result_t;

NMO_API nmo_status_t nmo_session_copy_objects_result(
    nmo_session_t *session,
    const nmo_object_id_t *ids,
    size_t count,
    uint32_t flags,
    nmo_copy_result_t *out_result);

NMO_API nmo_status_t nmo_session_destroy_objects_result(
    nmo_session_t *session,
    const nmo_object_id_t *ids,
    size_t count,
    uint32_t flags,
    nmo_destroy_result_t *out_result);

NMO_API nmo_status_t nmo_session_preview_destroy_result(
    nmo_session_t *session,
    const nmo_object_id_t *ids,
    size_t count,
    uint32_t flags,
    nmo_preview_destroy_result_t *out_result);

NMO_API size_t nmo_preview_destroy_result_count(
    const nmo_preview_destroy_result_t *result);

NMO_API nmo_status_t nmo_preview_destroy_result_at(
    const nmo_preview_destroy_result_t *result,
    size_t index,
    nmo_object_id_t *out_id);

NMO_API void nmo_preview_destroy_result_destroy(
    nmo_preview_destroy_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_RUNTIME_RESULT_H */
