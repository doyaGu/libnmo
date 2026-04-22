#ifndef NMO_CHUNK_INSPECT_H
#define NMO_CHUNK_INSPECT_H

#include "app/nmo_inspector.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_chunk_inspect_validate(
    const nmo_chunk_t *chunk,
    nmo_chunk_validation_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_INSPECT_H */
