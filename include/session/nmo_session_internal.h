#ifndef NMO_SESSION_INTERNAL_H
#define NMO_SESSION_INTERNAL_H

#include "app/nmo_session.h"
#include "session/nmo_reference_resolver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal-only runtime resolver accessors. */
nmo_reference_resolver_t *nmo_session_get_reference_resolver(
    const nmo_session_t *session);

nmo_reference_resolver_t *nmo_session_ensure_reference_resolver(
    nmo_session_t *session);

void nmo_session_reset_reference_resolver(nmo_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_INTERNAL_H */
