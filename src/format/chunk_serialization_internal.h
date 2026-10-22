#ifndef NMO_CHUNK_SERIALIZATION_INTERNAL_H
#define NMO_CHUNK_SERIALIZATION_INTERNAL_H

#include "format/nmo_chunk.h"

/* Validate the storage metadata and measure the VERSION1 envelope without
 * allocating or dereferencing its payload. Shared by file-section planning. */
nmo_status_t nmo_chunk_measure_version1(const nmo_chunk_t *chunk, size_t *out_size);

#endif
