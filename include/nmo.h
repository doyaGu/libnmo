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
 * - Core Layer: allocator, arena, error, logger, GUID
 * - IO Layer: file, memory, compressed, checksum, transactional IO
 * - Format Layer: headers, chunks, objects, managers
 * - Schema Layer: registry, validation, migration
 * - Object Layer: repository, ID remapping
 * - Session Layer: context, session, parser, builder
 *
 * Basic usage:
 * @code
 * // Create context
 * nmo_context* ctx = nmo_context_create(&(nmo_context_desc){
 *     .allocator = NULL,  // Use default
 *     .logger = nmo_logger_stderr(),
 *     .thread_pool_size = 4
 * });
 *
 * // Register built-in schemas
 * nmo_schema_registry_add_builtin(nmo_context_get_schema_registry(ctx));
 *
 * // Create session
 * nmo_session* session = nmo_session_create(ctx);
 *
 * // Load file
 * nmo_result result = nmo_load_file(session, "file.nmo", NMO_LOAD_DEFAULT);
 * if (result.code != NMO_OK) {
 *     fprintf(stderr, "Error: %s\n", result.error->message);
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
#include "core/nmo_buffer.h"
#include "core/nmo_hash.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_indexed_map.h"

// IO layer
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_memory.h"
#include "io/nmo_io_compressed.h"
#include "io/nmo_io_checksum.h"
#include "io/nmo_io_stream.h"
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
#include "format/nmo_manager_registry.h"
#include "format/nmo_image.h"
#include "format/nmo_image_codec.h"
#include "format/nmo_stb_adapter.h"

// Schema layer
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_validator.h"
#include "schema/nmo_migrator.h"

// Session layer
#include "session/nmo_object_repository.h"
#include "session/nmo_load_session.h"
#include "format/nmo_id_remap.h"
#include "session/nmo_parser.h"
#include "session/nmo_builder.h"

// App layer
#include "app/nmo_context.h"
#include "app/nmo_session.h"

// Built-in schemas
#include "nmo_builtin_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get library version string
 * @return Version string (e.g., "1.0.0")
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
