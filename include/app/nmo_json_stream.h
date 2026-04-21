/**
 * @file nmo_json_stream.h
 * @brief Lightweight streaming JSON writer for FILE* outputs.
 *
 * This is an advanced C/CLI helper for textual emission only. Stable
 * binding-facing consumers should prefer structured result helpers and
 * structured import/export APIs rather than modeling FILE* JSON streams.
 */

#ifndef NMO_JSON_STREAM_H
#define NMO_JSON_STREAM_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define NMO_JSON_STREAM_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_JSON_STREAM_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_json_stream_ctx {
    uint8_t type;       /* 1=object, 2=array */
    bool first;         /* first child/value in this container */
    bool pending_key;   /* object: key emitted, waiting for value */
} nmo_json_stream_ctx_t;

typedef struct nmo_json_stream {
    FILE *out;
    bool pretty;
    bool wrote_root;
    bool failed;
    size_t depth;
    nmo_json_stream_ctx_t stack[64];
} nmo_json_stream_t;

/**
 * @brief Initialize a streaming writer.
 * @param writer Writer object to initialize.
 * @param out Output stream.
 * @param pretty Whether to emit pretty-printed JSON.
 */
NMO_API void nmo_json_stream_init(nmo_json_stream_t *writer, FILE *out, bool pretty);

/**
 * @brief Return true if the writer has not failed.
 */
NMO_API bool nmo_json_stream_ok(const nmo_json_stream_t *writer);

NMO_API bool nmo_json_stream_begin_object(nmo_json_stream_t *writer);
NMO_API bool nmo_json_stream_end_object(nmo_json_stream_t *writer);
NMO_API bool nmo_json_stream_begin_array(nmo_json_stream_t *writer);
NMO_API bool nmo_json_stream_end_array(nmo_json_stream_t *writer);

/**
 * @brief Emit a key inside the current JSON object.
 */
NMO_API bool nmo_json_stream_key(nmo_json_stream_t *writer, const char *key);

NMO_API bool nmo_json_stream_value_string(nmo_json_stream_t *writer, const char *value);
NMO_API bool nmo_json_stream_value_uint(nmo_json_stream_t *writer, uint64_t value);
NMO_API bool nmo_json_stream_value_sint(nmo_json_stream_t *writer, int64_t value);
NMO_API bool nmo_json_stream_value_real(nmo_json_stream_t *writer, double value);
NMO_API bool nmo_json_stream_value_bool(nmo_json_stream_t *writer, bool value);
NMO_API bool nmo_json_stream_value_null(nmo_json_stream_t *writer);

/**
 * @brief Emit a JSON string containing hex-encoded bytes.
 */
NMO_API bool nmo_json_stream_value_hex_bytes(nmo_json_stream_t *writer,
                                             const void *bytes,
                                             size_t len,
                                             bool uppercase);

#ifdef __cplusplus
}
#endif

#endif /* NMO_JSON_STREAM_H */
