#ifndef NMO_SESSION_QUERY_H
#define NMO_SESSION_QUERY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;
typedef struct nmo_object nmo_object_t;

/*
 * Narrow stable read/query entry points that do not require consumers to model
 * session-owned indexes, callback visitors, or arena-backed result buffers.
 */
#define NMO_SESSION_QUERY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SESSION_QUERY_READ_API_TIER NMO_API_TIER_STABLE_CONSUMER

/**
 * @brief Count all session objects using the stable session-query facade.
 */
NMO_API nmo_status_t nmo_session_query_count_objects(
    nmo_session_t *session,
    size_t *out_count);

/**
 * @brief Find the first session object with an exact name match.
 * @ownership borrowed (owned by the session repository)
 */
NMO_API nmo_status_t nmo_session_query_find_object_by_name(
    nmo_session_t *session,
    const char *name,
    nmo_object_t **out_object);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_QUERY_H */
