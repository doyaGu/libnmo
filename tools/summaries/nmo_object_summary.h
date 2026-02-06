/**
 * @file nmo_object_summary.h
 * @brief Object semantic summary system for CLI (v2 - Reflection-first design)
 *
 * This module provides automatic semantic summaries for ALL Virtools object types
 * using the type system's reflection capabilities. Key design principles:
 *
 * 1. REFLECTION-FIRST: Field traversal is the primary mechanism, not type-specific
 *    hardcoded handlers. All types with reflection get automatic summaries.
 *
 * 2. TYPE-AWARE FORMATTING: Uses the type system's string conversion for proper
 *    enum names, flags formatting, etc.
 *
 * 3. PLUGGABLE ENRICHERS: Type-specific computed values (like "total faces" for
 *    mesh) are handled by small enricher functions, not full summary rewrites.
 *
 * 4. DRY + SOLID: Single responsibility for each component, no code duplication.
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
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* ============================================================================
 * Summary Output Context
 * ============================================================================ */

/**
 * @brief Summary output context
 *
 * Unified interface for both text and JSON output. The context carries all
 * state needed for name resolution, type lookups, and output formatting.
 */
typedef struct nmo_summary_output {
    /* Output targets */
    FILE *stream;                       ///< Output stream (for text)
    yyjson_mut_doc *json_doc;           ///< JSON document (for JSON output)
    yyjson_mut_val *json_data;          ///< JSON data object to add fields to
    bool is_json;                       ///< True if JSON output mode
    bool colorize;                      ///< True if colorized text output

    /* Context for resolution */
    nmo_context_t *ctx;                 ///< Context (provides type registry)
    nmo_session_t *session;             ///< Session (for object name resolution)
} nmo_summary_output_t;

/* ============================================================================
 * Summary Configuration
 * ============================================================================ */

/**
 * @brief Summary configuration options
 */
typedef struct nmo_summary_config {
    uint32_t array_preview_max;         ///< Max elements to show for arrays (default: 16)
    uint32_t text_preview_max;          ///< Max elements for text output (default: 8)
    uint32_t max_depth;                 ///< Max recursion depth for nested types (default: 2)
    bool show_field_metadata;           ///< Include field flags/semantic in output
    bool resolve_object_refs;           ///< Resolve object IDs to names
    bool format_enum_names;             ///< Format enums as names (not just values)
    bool format_flags_names;            ///< Format flags as names (not just values)
} nmo_summary_config_t;

/**
 * @brief Get default summary configuration
 */
nmo_summary_config_t nmo_summary_config_default(void);

/* ============================================================================
 * Enricher Interface (for type-specific computed values)
 * ============================================================================ */

/**
 * @brief Enricher callback signature
 *
 * Enrichers add computed/derived values that can't be obtained from raw
 * field reflection. They should NOT reimplement the full summary - just
 * add extra semantic information.
 *
 * @param obj Object to enrich
 * @param state Object state pointer
 * @param out Output context
 * @return true if enrichment was added
 */
typedef bool (*nmo_summary_enricher_fn)(
    nmo_object_t *obj,
    const void *state,
    nmo_summary_output_t *out);

/**
 * @brief Register an enricher for a class ID
 *
 * @param class_id Class ID to register for
 * @param enricher Enricher callback
 */
void nmo_summary_register_enricher(nmo_class_id_t class_id, nmo_summary_enricher_fn enricher);

/**
 * @brief Initialize built-in enrichers
 *
 * Registers enrichers for types that benefit from computed values
 * (e.g., mesh face count, behavior complexity metrics).
 */
void nmo_summary_init_builtin_enrichers(void);

/* ============================================================================
 * Main Summary API
 * ============================================================================ */

/**
 * @brief Initialize the summary system
 *
 * Must be called before using summary functions. Registers built-in enrichers.
 */
void nmo_summary_init(void);

/**
 * @brief Generate complete semantic summary for an object
 *
 * This is the main entry point. It generates:
 * 1. Base metadata (file index, state size, parent, etc.)
 * 2. Reflection-based field dump (using type system)
 * 3. Type-specific enrichments (if registered)
 *
 * @param obj Object to summarize
 * @param out Output context
 * @return true if summary was generated
 */
bool nmo_object_summary(nmo_object_t *obj, nmo_summary_output_t *out);

/**
 * @brief Generate summary with custom configuration
 *
 * @param obj Object to summarize
 * @param out Output context
 * @param config Summary configuration
 * @return true if summary was generated
 */
bool nmo_object_summary_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config);

/**
 * @brief Generate a summary for a set of selected field paths.
 *
 * Paths are reflection field paths using dot navigation and optional array
 * indexing, e.g.:
 *   - "vertices[0]"
 *   - "faces[3].material_group_idx"
 *   - "beobject.base.base.name"
 *
 * If a selected path refers to a repeated field without an index, the output
 * includes an array preview (consistent with the normal Fields section).
 *
 * @param obj Object to summarize
 * @param out Output context
 * @param paths Array of path strings
 * @param path_count Number of path strings
 * @return true if any selection item was emitted
 */
bool nmo_object_summary_select(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const char *const *paths,
    size_t path_count);

/**
 * @brief Generate selected-path summary with custom configuration.
 */
bool nmo_object_summary_select_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config,
    const char *const *paths,
    size_t path_count);

/**
 * @brief Evaluate one or more query expressions and emit results.
 *
 * Expressions are C-like and reflection-driven. See `include/dsl/nmo_dsl.h`.
 *
 * @param obj Object to query
 * @param out Output context
 * @param exprs Array of expression strings
 * @param expr_count Number of expressions
 * @return true if any result was emitted
 */
bool nmo_object_summary_expr(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const char *const *exprs,
    size_t expr_count);

/**
 * @brief Evaluate expressions with custom configuration.
 */
bool nmo_object_summary_expr_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config,
    const char *const *exprs,
    size_t expr_count);

/**
 * @brief Check if a class has reflection available
 *
 * @param ctx Context with type registry
 * @param class_id Class ID to check
 * @return true if reflection data is available
 */
bool nmo_summary_has_reflection(nmo_context_t *ctx, nmo_class_id_t class_id);

/**
 * @brief Check if a class has an enricher registered
 *
 * @param class_id Class ID to check
 * @return true if an enricher is registered
 */
bool nmo_summary_has_enricher(nmo_class_id_t class_id);

/* ============================================================================
 * Output Helper Functions
 * ============================================================================ */

/** @brief Add a section heading */
void nmo_summary_add_section(nmo_summary_output_t *out, const char *title);

/** @brief Add a string field */
void nmo_summary_add_string(nmo_summary_output_t *out, const char *key,
                            const char *value, int label_width);

/** @brief Add a signed integer field */
void nmo_summary_add_int(nmo_summary_output_t *out, const char *key,
                         int64_t value, int label_width);

/** @brief Add an unsigned integer field */
void nmo_summary_add_uint(nmo_summary_output_t *out, const char *key,
                          uint64_t value, int label_width);

/** @brief Add a floating point field */
void nmo_summary_add_float(nmo_summary_output_t *out, const char *key,
                           double value, int label_width);

/** @brief Add a boolean field */
void nmo_summary_add_bool(nmo_summary_output_t *out, const char *key,
                          bool value, int label_width);

/** @brief Add an object reference field (with optional name) */
void nmo_summary_add_object_ref(nmo_summary_output_t *out, const char *key,
                                nmo_object_id_t id, const char *name, int label_width);

/** @brief Add a hex value field */
void nmo_summary_add_hex(nmo_summary_output_t *out, const char *key,
                         uint32_t value, int label_width);

/** @brief Add a vector3 (x, y, z) field */
void nmo_summary_add_vector3(nmo_summary_output_t *out, const char *key,
                             float x, float y, float z, int label_width);

/** @brief Add a color (ARGB) field */
void nmo_summary_add_color(nmo_summary_output_t *out, const char *key,
                           uint32_t argb, int label_width);

/** @brief Add a GUID field */
void nmo_summary_add_guid(nmo_summary_output_t *out, const char *key,
                          nmo_guid_t guid, int label_width);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_SUMMARY_H */
