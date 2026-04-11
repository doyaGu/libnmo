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
 * - Core Layer: allocator, arena, error, logger, GUID, math, color, pool, refcount, containers
 * - IO Layer: file, memory, compressed, checksum, mmap, transactional IO
 * - Format Layer: headers, chunks, objects, managers, data, chunk pool, image
 * - Object Layer: class IDs, object types, schemas
 * - Type Layer: type system, dynamic types, operation system, builtin operations, string conversion
 * - DSL Layer: compile-once evaluate-many query and mutation language
 * - Session Layer: repository, ID remapping, object system, object index, parser, builder
 * - App Layer: context, session, plugin, comparison, runtime load, inspector, save pipeline, stats
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
 * // Load file (creates a session)
 * nmo_session_t *session = nmo_session_load(ctx, "file.nmo");
 * if (!session) {
 *     fprintf(stderr, "Error: failed to load file\n");
 * }
 *
 * // Clean up
 * nmo_session_destroy(session);
 * nmo_context_release(ctx);
 * @endcode
 */

// Common types
#include "nmo_types.h"

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
#include "core/nmo_hash_set.h"
#include "core/nmo_indexed_map.h"
#include "core/nmo_list.h"
#include "core/nmo_utils.h"
#include "core/nmo_hex.h"
#include "core/nmo_path.h"
#include "core/nmo_shared_library.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"
#include "core/nmo_pool.h"
#include "core/nmo_refcount.h"

// IO layer
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_memory.h"
#include "io/nmo_io_compressed.h"
#include "io/nmo_io_checksum.h"
#include "io/nmo_io_mmap.h"
#include "io/nmo_txn.h"

// Format layer
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_pool.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_image.h"
#include "format/nmo_image_codec.h"
#include "format/nmo_stb_adapter.h"
#include "format/nmo_data.h"

// Object layer
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_index.h"
#include "object/nmo_shadow_storage.h"
#include "object/nmo_object_system.h"

// Type layer
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_string.h"

// DSL layer
#include "dsl/nmo_dsl.h"

// Extension layer
#include "extension/nmo_extension_abi.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_loader.h"
#include "extension/nmo_extension_host.h"
#include "extension/nmo_extension_diagnostics.h"

// Session layer
#include "session/nmo_deserializer.h"
#include "format/nmo_id_remap.h"
#include "app/nmo_load.h"
#include "session/nmo_builder.h"
#include "session/nmo_object_system.h"
#include "session/nmo_runtime_graph.h"
#include "session/nmo_runtime_kernel.h"

// Session layer (context, session, serializer)
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_util.h"
#include "session/nmo_serializer.h"

// Behavior layer
#include "behavior/nmo_behavior_graph.h"

// App layer
#include "app/nmo_load.h"
#include "app/nmo_save.h"
#include "app/nmo_comparison.h"
#include "app/nmo_inspector.h"
#include "app/nmo_stats.h"
#include "app/nmo_chunk_index.h"
#include "app/nmo_json_stream.h"
#include "app/nmo_json_util.h"
#include "app/nmo_object_diff.h"
#include "app/nmo_object_hierarchy.h"
#include "app/nmo_object_summary.h"
#include "type/nmo_type_query.h"

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
