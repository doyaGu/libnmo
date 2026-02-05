/**
 * @file nmo_cli_json.h
 * @brief CLI JSON output with schema versioning
 */

#ifndef NMO_CLI_JSON_H
#define NMO_CLI_JSON_H

#include "yyjson.h"

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** JSON schema version for CLI output */
#define NMO_CLI_JSON_SCHEMA_VERSION "3.0.0"

/**
 * @brief Create a new JSON document for CLI output
 * @return New mutable JSON document, or NULL on error
 */
yyjson_mut_doc *nmo_cli_json_create_doc(void);

/**
 * @brief Add standard envelope to JSON output
 *
 * All CLI JSON outputs include:
 * - schema_version: "3.0.0"
 * - tool: "nmo"
 * - command: group.action (e.g., "file.info")
 * - timestamp: ISO 8601 timestamp
 * - data: the actual command output
 *
 * @param doc JSON document
 * @param data Data object to wrap
 * @param command Command string (e.g., "file.info")
 * @param input_file Input file path (optional, can be NULL)
 * @return Root object with envelope, or NULL on error
 */
yyjson_mut_val *nmo_cli_json_add_envelope(yyjson_mut_doc *doc,
                                          yyjson_mut_val *data,
                                          const char *command,
                                          const char *input_file);

/**
 * @brief Write JSON document to stream
 * @param doc JSON document
 * @param out Output stream
 * @param pretty Use pretty printing if true
 * @return true on success
 */
bool nmo_cli_json_write(yyjson_mut_doc *doc, FILE *out, bool pretty);

/**
 * @brief Write JSON document to string
 * @param doc JSON document
 * @param pretty Use pretty printing if true
 * @param out_len Output: string length (optional)
 * @return Allocated string (caller must free), or NULL on error
 */
char *nmo_cli_json_write_string(yyjson_mut_doc *doc, bool pretty, size_t *out_len);

/**
 * @brief Free JSON document
 * @param doc Document to free (can be NULL)
 */
void nmo_cli_json_free_doc(yyjson_mut_doc *doc);

/**
 * @brief Add a string to JSON object, sanitizing invalid UTF-8 bytes
 *
 * Virtools files may contain strings with Windows codepage characters
 * that are not valid UTF-8. This function replaces invalid byte sequences
 * with the Unicode replacement character (U+FFFD).
 *
 * @param doc JSON document
 * @param obj Parent object
 * @param key Key name
 * @param str String value (may contain invalid UTF-8)
 * @return true if the string was added (sanitized if necessary), false on error
 */
bool nmo_cli_json_add_str_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, const char *str);

/**
 * @brief Add a string to JSON array, sanitizing invalid UTF-8 bytes
 *
 * @param doc JSON document
 * @param arr Parent array
 * @param str String value (may contain invalid UTF-8)
 * @return true if the string was added (sanitized if necessary), false on error
 */
bool nmo_cli_json_add_str_safe_to_arr(yyjson_mut_doc *doc, yyjson_mut_val *arr,
                                      const char *str);

/**
 * @brief Add integer value with copied key
 *
 * Unlike yyjson's built-in functions, these copy the key string so it can
 * be a local variable that goes out of scope.
 */
bool nmo_cli_json_add_int_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, int64_t val);

/** @brief Add unsigned integer value with copied key */
bool nmo_cli_json_add_uint_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, uint64_t val);

/** @brief Add floating-point value with copied key */
bool nmo_cli_json_add_real_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, double val);

/** @brief Add boolean value with copied key */
bool nmo_cli_json_add_bool_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key, bool val);

/** @brief Add null value with copied key */
bool nmo_cli_json_add_null_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const char *key);

/** @brief Add arbitrary JSON value with copied key */
bool nmo_cli_json_add_val_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const char *key, yyjson_mut_val *val);

/**
 * @brief Add a hex-encoded byte blob as data_hex (+ truncation metadata).
 *
 * Adds:
 * - data_hex: hex string (lowercase/uppercase controlled by uppercase)
 * - data_emit_size: number of bytes encoded into data_hex
 * - data_truncated: true if emit_size < data_size
 * - data_total_size: original total size (only when truncated)
 *
 * @param doc JSON document
 * @param obj Parent object
 * @param bytes Raw bytes (may be NULL if data_size is 0)
 * @param data_size Total byte size
 * @param max_bytes Max bytes to emit (0 = no limit)
 * @param uppercase Emit 'A'..'F' instead of 'a'..'f'
 * @return true on success (or if there is nothing to add), false on allocation error
 */
bool nmo_cli_json_add_data_hex(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                               const void *bytes, size_t data_size,
                               size_t max_bytes, bool uppercase);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLI_JSON_H */
