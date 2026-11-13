#ifndef NMO_CHUNK_SERIALIZATION_INTERNAL_H
#define NMO_CHUNK_SERIALIZATION_INTERNAL_H

#include "format/nmo_chunk.h"

/* Validate the storage metadata and measure the VERSION1 envelope without
 * allocating or dereferencing its payload. Shared by file-section planning. */
nmo_status_t nmo_chunk_measure_version1(const nmo_chunk_t *chunk, size_t *out_size);

/* Reads the chunk's sub-chunks from its data again, as parsing does, in place
 * of the ones it lists. For a chunk whose data gained sub-chunks it did not
 * write itself. */
nmo_status_t nmo_chunk_rebuild_sub_chunks(nmo_chunk_t *chunk);

#endif
