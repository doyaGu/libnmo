#ifndef NMO_LOAD_DIAGNOSTICS_INTERNAL_H
#define NMO_LOAD_DIAGNOSTICS_INTERNAL_H

#include "session/nmo_deserializer.h"

typedef struct nmo_object nmo_object_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_chunk nmo_chunk_t;

nmo_status_t nmo_load_diagnostics_append(
    nmo_load_diagnostics_t *diagnostics,
    const nmo_object_t *object,
    const nmo_type_descriptor_t *schema_type,
    const nmo_chunk_t *chunk,
    uint32_t section_id,
    nmo_status_t status,
    const char *message);

#endif /* NMO_LOAD_DIAGNOSTICS_INTERNAL_H */
