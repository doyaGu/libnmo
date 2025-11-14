/**
 * @file nmo_io_stream.h
 * @brief Streaming reader/writer helpers for large Virtools files (Phase 8)
 */

#ifndef NMO_IO_STREAM_H
#define NMO_IO_STREAM_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to keep IO layer dependency-free */
struct nmo_file_header;
typedef struct nmo_file_header nmo_file_header_t;

struct nmo_header1;
typedef struct nmo_header1 nmo_header1_t;

struct nmo_object;
typedef struct nmo_object nmo_object_t;

struct nmo_manager_data;
typedef struct nmo_manager_data nmo_manager_data_t;

/** Opaque streaming reader handle */
typedef struct nmo_stream_reader nmo_stream_reader_t;

/** Opaque streaming writer handle */
typedef struct nmo_stream_writer nmo_stream_writer_t;

/**
 * @brief Reader configuration for streaming IO.
 */
typedef struct nmo_stream_reader_config {
    size_t buffer_size;          /**< Decompression buffer (bytes), default 64KB */
    nmo_allocator_t *allocator;  /**< Allocator for internal arena (optional) */
    nmo_arena_t *arena;          /**< External arena for metadata (optional, not owned) */
} nmo_stream_reader_config_t;

/**
 * @brief Create streaming reader tied to a file path.
 *
 * @param path File path to open (required)
 * @param config Optional configuration (NULL for defaults)
 * @return Reader handle or NULL on error
 */
NMO_API nmo_stream_reader_t *nmo_stream_reader_create(
    const char *path,
    const nmo_stream_reader_config_t *config);

/** Destroy reader and release any resources */
NMO_API void nmo_stream_reader_destroy(nmo_stream_reader_t *reader);

/** Get parsed file header (owned by reader) */
NMO_API const nmo_file_header_t *nmo_stream_reader_get_header(const nmo_stream_reader_t *reader);

/** Get parsed Header1 table (owned by reader) */
NMO_API const nmo_header1_t *nmo_stream_reader_get_header1(const nmo_stream_reader_t *reader);

/** Get manager data array parsed during initialization */
NMO_API const nmo_manager_data_t *nmo_stream_reader_get_managers(
    const nmo_stream_reader_t *reader,
    uint32_t *out_count);

/**
 * @brief Read the next object chunk as an nmo_object_t allocated from @p arena.
 *
 * @param reader Streaming reader
 * @param arena Arena used for the returned object (required)
 * @param out_object Output pointer for created object
 * @return NMO_OK on success, NMO_ERR_EOF when there are no more objects
 */
NMO_API nmo_result_t nmo_stream_reader_read_next_object(
    nmo_stream_reader_t *reader,
    nmo_arena_t *arena,
    nmo_object_t **out_object);

/** Skip the next object chunk without allocating it */
NMO_API nmo_result_t nmo_stream_reader_skip_object(nmo_stream_reader_t *reader);

/**
 * @brief Writer options controlling compression behavior.
 */
typedef struct nmo_stream_writer_options {
    const void *header1_data;          /**< Serialized Header1 blob (can be NULL) */
    size_t header1_size;               /**< Size of header1_data in bytes */
    size_t header1_uncompressed_size;  /**< Uncompressed Header1 size (0 if none) */
    size_t buffer_size;                /**< Compression buffer size (default 64KB) */
    int compression_level;             /**< Deflate level (0-9, default 6) */
    int compress_data;                 /**< Non-zero to compress data section */
} nmo_stream_writer_options_t;

/**
 * @brief Create streaming writer for incremental save.
 *
 * @param path Output file path (will be truncated)
 * @param header Template file header (copied internally)
 * @param options Optional writer options (NULL => defaults, no Header1)
 * @return Writer handle or NULL on failure
 */
NMO_API nmo_stream_writer_t *nmo_stream_writer_create(
    const char *path,
    const nmo_file_header_t *header,
    const nmo_stream_writer_options_t *options);

/**
 * @brief Append one object to the data section.
 *
 * @param writer Writer handle
 * @param object Object to serialize (chunk + metadata required)
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_stream_writer_write_object(
    nmo_stream_writer_t *writer,
    const nmo_object_t *object);

/** Finalize writer: flush compression stream and patch header */
NMO_API nmo_result_t nmo_stream_writer_finalize(nmo_stream_writer_t *writer);

/** Destroy writer (automatically finalizes if needed) */
NMO_API void nmo_stream_writer_destroy(nmo_stream_writer_t *writer);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_STREAM_H */
