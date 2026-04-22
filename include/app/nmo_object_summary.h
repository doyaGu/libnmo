/**
 * @file nmo_object_summary.h
 * @brief Object semantic summary system for CLI (Reflection-first design)
 *
 * This module provides automatic semantic summaries for ALL Virtools object types
 * using the type system's reflection capabilities. JSON object export uses a
 * separate semantic snapshot emitter whose default output is importable by
 * `object import -f json`. Key design principles:
 *
 * 1. REFLECTION-FIRST: Field traversal is the primary mechanism, not type-specific
 *    hardcoded handlers. All types with reflection get automatic summaries.
 *
 * 2. TYPE-AWARE FORMATTING: Uses GUID-aware rendering for colors (ARGB hex),
 *    enums, flags, and object refs.
 *
 * 3. BASE CLASS FLATTENING: Walks the inheritance hierarchy at render time,
 *    emitting fields from each class level with section headers.
 *
 * 4. DRY + SOLID: Single responsibility for each component, no code duplication.
 *
 * This header remains the advanced rendering/reporting surface. For stable
 * binding-facing structured summary data, prefer the result helpers in
 * nmo_report_result.h instead of modeling FILE* or yyjson output contexts.
 */

#ifndef NMO_OBJECT_SUMMARY_H
#define NMO_OBJECT_SUMMARY_H

#include "session/nmo_context.h"
#include "core/nmo_guid.h"
#include "format/nmo_object.h"
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
typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;

/*
 * Summary rendering stays public for advanced C and CLI/reporting consumers.
 * It is not the default binding-facing result contract.
 */
#define NMO_OBJECT_SUMMARY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_SUMMARY_RENDERING_API_TIER NMO_API_TIER_ADVANCED_C

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
    uint32_t array_preview_max;         ///< Max elements to show for text array previews (default: 16)
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
NMO_API nmo_summary_config_t nmo_summary_config_default(void);

/* ============================================================================
 * Main Summary API
 * ============================================================================ */

/**
 * @brief Initialize the summary system
 *
 * Reflection-first summaries do not require explicit setup.
 */
NMO_API void nmo_summary_init(void);

/**
 * @brief Generate complete semantic summary for an object
 *
 * This is the main entry point. It generates:
 * 1. Base metadata (file index, state size, parent, etc.)
 * 2. Reflection-based field dump (using type system)
 * 3. Optional projection sections when requested
 *
 * @param obj Object to summarize
 * @param out Output context
 * @return true if summary was generated
 */
NMO_API bool nmo_object_summary(nmo_object_t *obj, nmo_summary_output_t *out);

/**
 * @brief Generate summary with custom configuration
 *
 * @param obj Object to summarize
 * @param out Output context
 * @param config Summary configuration
 * @return true if summary was generated
 */
NMO_API bool nmo_object_summary_with_config(
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
 * If a selected path refers to a repeated field without an index, text output
 * includes an array preview (consistent with the normal Fields section). JSON
 * object export emits complete array snapshot items for importable output.
 *
 * @param obj Object to summarize
 * @param out Output context
 * @param paths Array of path strings
 * @param path_count Number of path strings
 * @return true if any selection item was emitted
 */
NMO_API bool nmo_object_summary_select(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const char *const *paths,
    size_t path_count);

/**
 * @brief Generate selected-path summary with custom configuration.
 */
NMO_API bool nmo_object_summary_select_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config,
    const char *const *paths,
    size_t path_count);

/**
 * @brief Check if a class has reflection available
 *
 * @param ctx Context with type registry
 * @param class_id Class ID to check
 * @return true if reflection data is available
 */
NMO_API bool nmo_summary_has_reflection(nmo_context_t *ctx, nmo_class_id_t class_id);

/* ============================================================================
 * Output Helper Functions
 * ============================================================================ */

/** @brief Add a section heading */
NMO_API void nmo_summary_add_section(nmo_summary_output_t *out, const char *title);

/** @brief Add a string field */
NMO_API void nmo_summary_add_string(nmo_summary_output_t *out, const char *key,
                                    const char *value, int label_width);

/** @brief Add a signed integer field */
NMO_API void nmo_summary_add_int(nmo_summary_output_t *out, const char *key,
                                 int64_t value, int label_width);

/** @brief Add an unsigned integer field */
NMO_API void nmo_summary_add_uint(nmo_summary_output_t *out, const char *key,
                                  uint64_t value, int label_width);

/** @brief Add a floating point field */
NMO_API void nmo_summary_add_float(nmo_summary_output_t *out, const char *key,
                                   double value, int label_width);

/** @brief Add a boolean field */
NMO_API void nmo_summary_add_bool(nmo_summary_output_t *out, const char *key,
                                  bool value, int label_width);

/** @brief Add an object reference field (with optional name) */
NMO_API void nmo_summary_add_object_ref(nmo_summary_output_t *out, const char *key,
                                        nmo_object_id_t id, const char *name, int label_width);

/** @brief Add a hex value field */
NMO_API void nmo_summary_add_hex(nmo_summary_output_t *out, const char *key,
                                 uint32_t value, int label_width);

/** @brief Add a vector3 (x, y, z) field */
NMO_API void nmo_summary_add_vector3(nmo_summary_output_t *out, const char *key,
                                     float x, float y, float z, int label_width);

/** @brief Add a color (ARGB) field */
NMO_API void nmo_summary_add_color(nmo_summary_output_t *out, const char *key,
                                   uint32_t argb, int label_width);

/** @brief Add a GUID field */
NMO_API void nmo_summary_add_guid(nmo_summary_output_t *out, const char *key,
                                  nmo_guid_t guid, int label_width);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_SUMMARY_H */
