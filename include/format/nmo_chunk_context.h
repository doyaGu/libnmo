#ifndef NMO_CHUNK_CONTEXT_H
#define NMO_CHUNK_CONTEXT_H

#include "format/nmo_id_remap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File-context parameters shared by writers and parsers.
 *
 * When runtime chunks are emitted as part of a file save, object IDs are
 * converted to sequential file indices via SaveFindObjectIndex semantics.
 * Likewise, when parsing file-authored chunks, indices must be translated
 * back to runtime IDs. This lightweight structure wraps the remap tables
 * required for both directions so lower layers can stay independent from
 * session/app layers.
 */
typedef struct nmo_chunk_file_context {
    const nmo_id_remap_t *runtime_to_file; /**< Runtime ID -> file index remap (save path) */
    const nmo_id_remap_t *file_to_runtime; /**< File index -> runtime ID remap (load path) */
} nmo_chunk_file_context_t;

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_CONTEXT_H */
