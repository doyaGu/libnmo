/**
 * @file nmo_script_view.h
 * @brief Stable read-only summaries for discovered scripts
 */

#ifndef NMO_SCRIPT_VIEW_H
#define NMO_SCRIPT_VIEW_H

#include "behavior/nmo_behavior_query.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/*
 * Stable facade over scene-level script discovery. Consumers can enumerate
 * script summaries without depending on nmo_array_t or script_walker result
 * buffers directly.
 */
#define NMO_SCRIPT_VIEW_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_VIEW_READ_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef nmo_behavior_script_view_t nmo_script_view_t;

NMO_API nmo_status_t nmo_script_view_count(
    nmo_session_t *session,
    size_t *out_count);

NMO_API nmo_status_t nmo_script_view_at(
    nmo_session_t *session,
    size_t index,
    nmo_script_view_t *out_view);

NMO_API nmo_status_t nmo_script_view_from_script_id(
    nmo_session_t *session,
    nmo_object_id_t script_id,
    nmo_script_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_VIEW_H */
