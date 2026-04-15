/**
 * @file nmo_cmd_core.h
 * @brief Shared command core - reusable logic for CLI and REPL commands
 *
 * Provides class utilities, object iteration with filtering, reference
 * iteration, regex/wildcard matching, and DSL evaluation helpers.
 * All functions operate on nmo_cmd_ctx_t so they can be used from
 * both CLI commands and REPL commands.
 */

#ifndef NMO_CMD_CORE_H
#define NMO_CMD_CORE_H

#include "nmo_cmd_ctx.h"
#include "nmo.h"
#include "object/nmo_ref_graph.h"
#include "dsl/nmo_dsl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. Class utilities
 * ============================================================================ */

/**
 * @brief Get class name from class ID
 * @return Class name or NULL if not found
 */
const char *nmo_core_class_name(const nmo_cmd_ctx_t *c, nmo_class_id_t id);

/**
 * @brief Get class name, falling back to "Class#N" in caller buffer
 * @return Class name (never NULL if buf is valid)
 */
const char *nmo_core_class_name_or(const nmo_cmd_ctx_t *c, nmo_class_id_t id,
                                   char *buf, size_t sz);

/**
 * @brief Get class ID from class name
 * @return Class ID or 0 if not found
 */
nmo_class_id_t nmo_core_class_id(const nmo_cmd_ctx_t *c, const char *name);

/**
 * @brief Check if class derives from base
 */
bool nmo_core_class_derives(const nmo_cmd_ctx_t *c, nmo_class_id_t id,
                            nmo_class_id_t base);

/* ============================================================================
 * 2. Object lookup + pattern matching
 * ============================================================================ */

/**
 * @brief Find object by ID in session repository
 * @return Object pointer or NULL
 */
nmo_object_t *nmo_core_find_by_id(const nmo_cmd_ctx_t *c, nmo_object_id_t id);

/**
 * @brief Lightweight regex match (supports . * [] ^ $ and \-escapes)
 * @param text    Text to search in
 * @param pattern Regex pattern
 * @param icase   Case-insensitive matching
 */
bool nmo_core_regex_match(const char *text, const char *pattern, bool icase);

/* ============================================================================
 * 3. Object iteration with visitor pattern
 * ============================================================================ */

/**
 * @brief Visitor callback for object iteration
 * @return 0 to continue, non-zero to stop
 */
typedef int (*nmo_core_object_fn)(size_t index, nmo_object_t *obj,
                                  const nmo_cmd_ctx_t *c, void *user);

/**
 * @brief Filter criteria for object iteration
 */
typedef struct {
    nmo_class_id_t class_id;        /**< Filter by class (0 = any) */
    bool class_derived;             /**< Match derived classes too */
    const char *name_pattern;       /**< Wildcard match (*foo*, foo*) */
    const char *name_substr;        /**< Substring match */
    const char *name_regex;         /**< Regex match */
    bool regex_icase;               /**< Case-insensitive regex */
    nmo_dsl_program_t *dsl_filter;  /**< DSL expression filter */
    nmo_object_id_t object_id;      /**< Filter by object ID (0 = any) */
} nmo_core_object_filter_t;

/**
 * @brief Iteration result counters
 */
typedef struct {
    size_t total;                   /**< Total objects in session */
    size_t matched;                 /**< Objects passing all filters */
    size_t visited;                 /**< Objects visited (may be < matched if stopped early) */
} nmo_core_iter_result_t;

/**
 * @brief Iterate objects with optional filtering and visitor callback
 *
 * @param c       Command context
 * @param filter  Filter criteria (NULL = match all)
 * @param visitor Callback per matched object (NULL = count only)
 * @param user    User data passed to visitor
 * @param result  Output counters (NULL = don't collect)
 * @return NMO_CLI_EXIT_SUCCESS or NMO_CLI_EXIT_INTERNAL_ERROR
 */
int nmo_core_iter_objects(const nmo_cmd_ctx_t *c,
                          const nmo_core_object_filter_t *filter,
                          nmo_core_object_fn visitor, void *user,
                          nmo_core_iter_result_t *result);

/* ============================================================================
 * 4. Reference iteration
 * ============================================================================ */

#define NMO_CORE_REFS_OUT  0x1
#define NMO_CORE_REFS_IN   0x2
#define NMO_CORE_REFS_BOTH (NMO_CORE_REFS_OUT | NMO_CORE_REFS_IN)

/**
 * @brief Reference info passed to visitor
 */
typedef struct {
    const nmo_ref_edge_t *edge;     /**< Edge descriptor */
    bool is_incoming;               /**< true if incoming reference */
    nmo_object_t *peer;             /**< Peer object (may be NULL if broken) */
    const char *peer_name;          /**< Peer object name (may be NULL) */
    const char *peer_class_name;    /**< Peer class name (may be NULL) */
} nmo_core_ref_info_t;

/**
 * @brief Reference iteration result counters
 */
typedef struct {
    size_t outgoing;                /**< Outgoing edge count */
    size_t incoming;                /**< Incoming edge count */
} nmo_core_ref_result_t;

/**
 * @brief Visitor callback for reference iteration
 * @return 0 to continue, non-zero to stop
 */
typedef int (*nmo_core_ref_fn)(const nmo_core_ref_info_t *info,
                               const nmo_cmd_ctx_t *c, void *user);

/**
 * @brief Iterate references for an object
 *
 * @param c       Command context
 * @param obj_id  Object ID to enumerate refs for
 * @param dir     Direction mask (NMO_CORE_REFS_OUT, IN, or BOTH)
 * @param visitor Callback per reference (NULL = count only)
 * @param user    User data passed to visitor
 * @param result  Output counters (NULL = don't collect)
 * @return NMO_CLI_EXIT_SUCCESS or NMO_CLI_EXIT_INTERNAL_ERROR
 */
int nmo_core_iter_refs(const nmo_cmd_ctx_t *c,
                       nmo_object_id_t obj_id,
                       unsigned dir,
                       nmo_core_ref_fn visitor, void *user,
                       nmo_core_ref_result_t *result);

/* ============================================================================
 * 5. DSL evaluation helpers
 * ============================================================================ */

/**
 * @brief Set up a DSL eval context for an object
 * @return true on success, false if object has no chunk
 */
bool nmo_core_dsl_setup_ctx(const nmo_cmd_ctx_t *c, nmo_object_t *obj,
                            nmo_dsl_eval_context_t *out);

/**
 * @brief Evaluate a DSL expression against an object (compile + eval)
 */
nmo_status_t nmo_core_dsl_eval(const nmo_cmd_ctx_t *c, nmo_object_t *obj,
                               const char *expr, nmo_dsl_value_t *result);

/**
 * @brief Check if a DSL value is truthy
 */
bool nmo_core_dsl_is_truthy(const nmo_dsl_value_t *val);

/**
 * @brief Format a DSL value into a text buffer
 * @return true if formatted successfully
 */
bool nmo_core_dsl_format(const nmo_dsl_value_t *val, char *buf, size_t sz);

/**
 * @brief Print a DSL compile/eval error with source context and caret
 *
 * Reads the last error message (expected format "line:col: message"),
 * prints the error message then shows the relevant source line with
 * a caret (^) pointing to the error column.
 *
 * @param stream  Output stream for the error (typically stderr)
 * @param source  The original DSL source string
 * @param prefix  Error prefix (e.g. "Error: Failed to compile filter")
 */
void nmo_core_dsl_print_error(FILE *stream, const char *source,
                              const char *prefix);

/* ============================================================================
 * 7. Field mutation
 * ============================================================================ */

typedef struct nmo_field_set_entry {
    const char *field_name;
    const char *value_str;
} nmo_field_set_entry_t;

typedef struct nmo_field_set_result {
    size_t applied;
    size_t failed;
} nmo_field_set_result_t;

/**
 * @brief Set one or more fields on an object's typed state.
 *
 * For each entry, looks up the field by name in the object's type descriptor,
 * parses the value string via nmo_type_value_from_string, and writes it.
 * Prints old->new for each field.
 *
 * @return NMO_CLI_EXIT_SUCCESS on success
 */
int nmo_core_set_fields(
    nmo_cmd_ctx_t *c,
    nmo_object_id_t object_id,
    const nmo_field_set_entry_t *entries,
    size_t entry_count,
    bool dry_run,
    nmo_field_set_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_CORE_H */
