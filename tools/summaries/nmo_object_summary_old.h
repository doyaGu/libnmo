/**
 * @file nmo_object_summary.h
 * @brief Object semantic summary system for CLI
 *
 * Provides type-specific semantic summaries for Virtools objects.
 * Each object type can register a summary function that extracts
 * meaningful information from the object's state structure.
 */

#ifndef NMO_OBJECT_SUMMARY_H
#define NMO_OBJECT_SUMMARY_H

#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "format/nmo_object.h"
#include "app/nmo_context.h"
#include "nmo_types.h"
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;

/**
 * @brief Summary output context
 *
 * Provides unified interface for both text and JSON output.
 */
typedef struct nmo_summary_output {
    FILE *stream;                       ///< Output stream (for text)
    yyjson_mut_doc *json_doc;           ///< JSON document (for JSON output)
    yyjson_mut_val *json_data;          ///< JSON data object to add fields to
    bool is_json;                       ///< True if JSON output mode
    bool colorize;                      ///< True if colorized text output
    nmo_context_t *ctx;                 ///< Context for name resolution
    nmo_session_t *session;             ///< Session for object reference resolution (optional)
} nmo_summary_output_t;

/**
 * @brief Summary function signature
 *
 * @param obj Object to summarize
 * @param out Output context
 * @return true if summary was generated, false if not supported
 */
typedef bool (*nmo_summary_fn)(nmo_object_t *obj, nmo_summary_output_t *out);

/**
 * @brief Initialize the summary system
 *
 * Must be called before using any summary functions.
 */
void nmo_summary_init(void);

/**
 * @brief Generate semantic summary for an object
 *
 * Dispatches to the appropriate type-specific summary function based on
 * the object's class ID. Falls back to generic summary if no specific
 * handler is registered.
 *
 * @param obj Object to summarize
 * @param out Output context
 * @return true if summary was generated
 */
bool nmo_object_summary(nmo_object_t *obj, nmo_summary_output_t *out);

/**
 * @brief Check if a class has a semantic summary available
 *
 * @param class_id Class ID to check
 * @return true if a summary handler is registered
 */
bool nmo_summary_has_handler(nmo_class_id_t class_id);

/* Individual summary functions (can also be called directly) */
bool nmo_summary_ck3dentity(nmo_object_t *obj, nmo_summary_output_t *out);
bool nmo_summary_ckmesh(nmo_object_t *obj, nmo_summary_output_t *out);
bool nmo_summary_ckmaterial(nmo_object_t *obj, nmo_summary_output_t *out);
bool nmo_summary_cktexture(nmo_object_t *obj, nmo_summary_output_t *out);
bool nmo_summary_ckcamera(nmo_object_t *obj, nmo_summary_output_t *out);
bool nmo_summary_cklight(nmo_object_t *obj, nmo_summary_output_t *out);
bool nmo_summary_ckbehavior(nmo_object_t *obj, nmo_summary_output_t *out);

/* Helper functions for summary output */

/**
 * @brief Add a string field to summary
 */
void nmo_summary_add_string(nmo_summary_output_t *out, const char *key, 
                            const char *value, int label_width);

/**
 * @brief Add an integer field to summary
 */
void nmo_summary_add_int(nmo_summary_output_t *out, const char *key,
                         int64_t value, int label_width);

/**
 * @brief Add an unsigned integer field to summary
 */
void nmo_summary_add_uint(nmo_summary_output_t *out, const char *key,
                          uint64_t value, int label_width);

/**
 * @brief Add a floating point field to summary
 */
void nmo_summary_add_float(nmo_summary_output_t *out, const char *key,
                           double value, int label_width);

/**
 * @brief Add a boolean field to summary
 */
void nmo_summary_add_bool(nmo_summary_output_t *out, const char *key,
                          bool value, int label_width);

/**
 * @brief Add an object reference field to summary
 */
void nmo_summary_add_object_ref(nmo_summary_output_t *out, const char *key,
                                nmo_object_id_t id, const char *name, int label_width);

/**
 * @brief Add a hex value field to summary
 */
void nmo_summary_add_hex(nmo_summary_output_t *out, const char *key,
                         uint32_t value, int label_width);

/**
 * @brief Add a section heading to summary
 */
void nmo_summary_add_section(nmo_summary_output_t *out, const char *title);

/**
 * @brief Add a vector3 field (x, y, z) to summary
 */
void nmo_summary_add_vector3(nmo_summary_output_t *out, const char *key,
                             float x, float y, float z, int label_width);

/**
 * @brief Add a color field (ARGB) to summary
 */
void nmo_summary_add_color(nmo_summary_output_t *out, const char *key,
                           uint32_t argb, int label_width);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_SUMMARY_H */
