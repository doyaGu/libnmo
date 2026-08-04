#ifndef NMO_H
#define NMO_H

/**
 * @file nmo.h
 * @brief Main public header for libnmo - Virtools file format library
 *
 * libnmo is a C library for reading and writing Virtools file formats
 * (.nmo/.cmo/.vmo) with full compatibility with the original Virtools runtime.
 *
 * Architecture:
 * - Core Layer: allocator, arena, error, logger, GUID, math, color, refcount, containers
 * - IO Layer: file, memory, mmap, transactional IO
 * - Format Layer: headers, chunks, objects, managers, data, chunk pool, image
 * - Canonical Model: context -> document -> workspace -> object/behavior/chunk -> export
 * - Type/Extension/Lua Layers: type system, plugins, scripting bindings
 *
 * Basic usage:
 * @code
 * // Create context
 * nmo_context_desc_t desc = {
 *     .allocator = NULL,  // Use default
 *     .logger = nmo_logger_stderr(),
 *     .thread_pool_size = 4
 * };
 * nmo_context_t *ctx = nmo_context_create(&desc);
 * nmo_context_enable_logging(ctx, 1); // Optional: enable libnmo logs
 *
 * // Load file into a document
 * nmo_document_t *document = NULL;
 * if (nmo_document_load_file(ctx, "file.nmo", &document) != NMO_OK) {
 *     fprintf(stderr, "Error: failed to load file\n");
 * }
 *
 * // Create a workspace when you need mutation/runtime services
 * nmo_workspace_t *workspace = NULL;
 * if (nmo_workspace_create(ctx, document, &workspace) != NMO_OK) {
 *     fprintf(stderr, "Error: failed to create workspace\n");
 * }
 *
 * // Clean up
 * nmo_workspace_destroy(workspace);
 * nmo_document_destroy(document);
 * nmo_context_release(ctx);
 * @endcode
 */

// Common types
#include "nmo_types.h"

/*
 * nmo.h is a convenience umbrella, not a promise that every transitively
 * included header belongs to the same stable binding-facing tier.
 */
#define NMO_UMBRELLA_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_MIXED_TIER

// Core layer
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include "core/nmo_guid.h"
#include "core/nmo_array.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_bit_array.h"
#include "core/nmo_string.h"
#include "core/nmo_hash.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_indexed_map.h"
#include "core/nmo_utils.h"
#include "core/nmo_hex.h"
#include "core/nmo_path.h"
#include "core/nmo_shared_library.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"
#include "core/nmo_refcount.h"

// IO layer
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_memory.h"
#include "io/nmo_io_mmap.h"
#include "io/nmo_txn.h"

// Format layer
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_pool.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_image.h"
#include "format/nmo_image_codec.h"
#include "format/nmo_stb_adapter.h"
#include "format/nmo_data.h"
#include "format/nmo_interface_view.h"

// Object layer
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_ref.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_summary.h"
#include "object/nmo_object_diff.h"

// Type layer
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_view.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_string.h"

// Extension layer
#include "extension/nmo_extension_abi.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_loader.h"
#include "extension/nmo_extension_host.h"

// Reorganization owner headers
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "document/nmo_document.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_file_state.h"
#include "document/nmo_document_save.h"
#include "document/nmo_document_perf_stats.h"
#include "document/nmo_document_stats.h"
#include "document/nmo_document_compare.h"
#include "chunk/nmo_chunk_index.h"
#include "chunk/nmo_chunk_inspect.h"
#include "object/nmo_object_refs.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_asset_edit.h"
#include "object/nmo_entity_edit.h"
#include "object/nmo_scene_edit.h"
#include "object/nmo_animation_edit.h"
#include "object/nmo_sound_edit.h"
#include "object/nmo_object_hierarchy.h"
#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_behavior_query.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_execute.h"
#include "behavior/nmo_edit_plan_json.h"
#include "behavior/nmo_probe_analyzer.h"
#include "export/nmo_export_text.h"
#include "export/nmo_export_json.h"
#include "export/nmo_export_dot.h"
#include "export/nmo_ansi.h"
#include "export/nmo_hexdump.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
#include "project/nmo_project_validator.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_manifest_json.h"

// Additional type helpers
#include "type/nmo_type_query.h"

// Lua platform layer
#include "lua/nmo_lua_module.h"
#include "lua/nmo_lua_runtime.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get library version string
 * @return Version string (e.g., "1.0.0")
 * @ownership static
 */
NMO_API const char *nmo_version(void);

/**
 * @brief Get library version as integer
 * @return Version as (major << 16) | (minor << 8) | patch
 */
NMO_API uint32_t nmo_version_int(void);

#ifdef __cplusplus
}
#endif

#endif // NMO_H
