#ifndef NMO_TOOL_COMMON_H
#define NMO_TOOL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Case-insensitive string compare (ASCII, locale-independent). */
int nmo_tool_stricmp(const char *a, const char *b);

/** Case-insensitive equality check (ASCII, locale-independent). */
bool nmo_tool_streq_ci(const char *a, const char *b);

/** Simple wildcard matcher supporting '*' and '?' (case-insensitive ASCII). */
bool nmo_tool_match_wildcard_ci(const char *pattern, const char *value);

/** Heap-duplicate a string (malloc). Returns NULL on OOM or if src is NULL. */
char *nmo_tool_strdup(const char *src);

/** Parse an unsigned 32-bit decimal integer. Returns false on failure. */
bool nmo_tool_parse_u32_dec(const char *text, uint32_t *out);

/** Parse an unsigned 32-bit integer (base 0: supports 123, 0x7B). Returns false on failure. */
bool nmo_tool_parse_u32(const char *text, uint32_t *out);

/** Parse a decimal size_t. Returns false on failure. */
bool nmo_tool_parse_size_dec(const char *text, size_t *out);

/** Parse a size_t (base 0: supports 123, 0x7B). Returns false on failure. */
bool nmo_tool_parse_size(const char *text, size_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TOOL_COMMON_H */
