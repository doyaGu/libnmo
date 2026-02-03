/**
 * @file nmo_deserialize_context.c
 * @brief Deserialization Context Implementation
 */

#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include <string.h>

NMO_API nmo_status_t nmo_deserialize_store_remaining(
    nmo_deserialize_context_t *ctx,
    nmo_chunk_t *chunk)
{
    if (ctx == NULL || chunk == NULL) {
        NMO_RETURN_OK();  /* Nothing to do */
    }
    
    if (ctx->object == NULL) {
        NMO_RETURN_OK();  /* No object to store on */
    }
    
    if ((ctx->flags & NMO_DESER_FLAG_PRESERVE_RAW) == 0) {
        NMO_RETURN_OK();  /* Not preserving raw data */
    }
    
    /* Get remaining bytes from chunk */
    size_t data_size = nmo_chunk_get_data_size(chunk);
    size_t pos_dwords = nmo_chunk_get_position(chunk);
    size_t pos_bytes = pos_dwords * sizeof(uint32_t);
    
    if (pos_bytes >= data_size) {
        NMO_RETURN_OK();  /* No remaining bytes */
    }
    
    /* Remaining bytes are captured via shadow storage when enabled. */
    
    NMO_RETURN_OK();
}
