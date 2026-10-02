#ifndef NMO_CHUNK_CONTEXT_H
#define NMO_CHUNK_CONTEXT_H

#include "format/nmo_id_remap.h"
#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tokens that stand for the object ids a file refers to but does not hold.
 *
 * Reading such an id from a file chunk interns it as a token, and writing the
 * token for a file gives the raw id back, so the reference survives a load and
 * a save. The object repository owns the table
 * (nmo_object_repository_ref_tokens()); the chunk code only calls through it.
 */
typedef struct nmo_ref_tokens {
    void *owner;
    /** Return the token of raw_id, interning it on first use. */
    nmo_status_t (*intern)(void *owner, nmo_object_id_t raw_id, nmo_object_id_t *out_token);
    /** Give the raw id a token stands for; false when id is not a token. */
    bool (*get_raw)(const void *owner, nmo_object_id_t token, nmo_object_id_t *out_raw_id);
} nmo_ref_tokens_t;

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
    const nmo_id_remap_t *runtime_to_file; /**< Runtime ID -> file object index (0-based) remap (save path) */
    const nmo_id_remap_t *file_to_runtime; /**< File object index (0-based) -> runtime ID remap (load path) */
    const nmo_ref_tokens_t *ref_tokens; /**< Unresolved-reference tokens; NULL keeps such ids as they are. */
} nmo_chunk_file_context_t;

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_CONTEXT_H */
