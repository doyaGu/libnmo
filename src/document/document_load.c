/**
 * @file load.c
 * @brief High-level file load wrapper
 *
 * Thin wrapper that selects IO and delegates to the phased
 * deserializer API in session/deserializer.c.
 */

#include "document/nmo_document_load.h"
#include "../runtime/runtime_internal.h"
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_mmap.h"
#include "format/nmo_header.h"
#include "core/nmo_logger.h"
#include "core/nmo_error.h"
#include <string.h>

static nmo_load_perf_stats_t *load_perf_sink_from_options(
    const nmo_load_options_t *opts,
    nmo_load_perf_stats_t *local_stats)
{
    if (opts == NULL || !opts->collect_perf_stats) {
        return NULL;
    }
    return (opts->perf_stats != NULL) ? opts->perf_stats : local_stats;
}

static uint64_t load_perf_begin(nmo_load_perf_stats_t *stats) {
    return (stats != NULL) ? nmo_perf_now_ticks() : 0u;
}

static void load_perf_end(nmo_load_perf_stats_t *stats,
                          nmo_load_perf_phase_t phase,
                          uint64_t start_ticks) {
    if (stats == NULL) {
        return;
    }
    uint64_t end_ticks = nmo_perf_now_ticks();
    nmo_load_perf_stats_record(stats, phase, nmo_perf_elapsed_ms(start_ticks, end_ticks));
}

static int nmo_load_profile_is_valid(nmo_load_profile_t profile) {
    return profile == NMO_LOAD_PROFILE_FULL ||
           profile == NMO_LOAD_PROFILE_METADATA ||
           profile == NMO_LOAD_PROFILE_HEADER_ONLY;
}

/**
 * @brief Detect if a file uses compression by reading its header
 *
 * @param path File path to inspect
 * @return 1 if compressed, 0 if uncompressed, -1 on error
 */
static nmo_status_t nmo_detect_file_compression(const char *path,
                                                int *out_is_compressed) {
    if (path == NULL || out_is_compressed == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        return NMO_ERR_FILE_NOT_FOUND;
    }

    /* Parse the file header to check compression flags */
    nmo_file_header_t header;
    nmo_status_t result = nmo_file_header_parse(io, &header);
    nmo_io_close(io);

    if (result != NMO_OK) {
        return result;
    }

    /* Check if any compression is enabled */
    const uint32_t compression_mask =
        NMO_FILE_WRITE_CHUNK_COMPRESSED_OLD |
        NMO_FILE_WRITE_WHOLE_COMPRESSED;
    int is_compressed = (header.file_write_mode & compression_mask) != 0;

    /* Also check if header1 is compressed (pack_size != unpack_size) */
    if (header.hdr1_pack_size != header.hdr1_unpack_size) {
        is_compressed = 1;
    }

    /* And data section compression */
    if (header.data_pack_size != header.data_unpack_size) {
        is_compressed = 1;
    }

    *out_is_compressed = is_compressed;
    return NMO_OK;
}

/**
 * @brief Load file with automatic IO selection
 *
 * Detects compression and uses mmap for uncompressed files when
 * supported, falling back to standard file IO otherwise.
 */
nmo_status_t nmo_load_file(nmo_session_t *session,
                           const char *path,
                           const nmo_load_options_t *opts) {
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Use defaults if no options provided */
    nmo_load_options_t local_opts;
    if (opts == NULL) {
        local_opts = nmo_load_options_default();
        opts = &local_opts;
    }
    nmo_load_perf_stats_t local_perf_stats = {0};
    if (opts->collect_perf_stats && opts->perf_stats == NULL) {
        local_opts = *opts;
        local_opts.perf_stats = &local_perf_stats;
        opts = &local_opts;
    }
    if (!nmo_load_profile_is_valid(opts->profile)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (opts->profile != NMO_LOAD_PROFILE_FULL) {
        if (nmo_session_has_materialized_load_state(session)) {
            return NMO_ERR_INVALID_STATE;
        }
    }
    nmo_load_perf_stats_t *perf_stats = load_perf_sink_from_options(opts, &local_perf_stats);

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_logger_t *logger = nmo_context_get_logger(ctx);

    /* Select best IO path automatically (mmap for uncompressed, fallback to standard) */
    uint64_t open_detect_start = load_perf_begin(perf_stats);
    int is_compressed = 0;
    nmo_status_t detect_result =
        nmo_detect_file_compression(path, &is_compressed);
    if (detect_result != NMO_OK) {
        load_perf_end(perf_stats, NMO_LOAD_PERF_OPEN_DETECT, open_detect_start);
        nmo_log(logger, NMO_LOG_ERROR,
                "Failed to inspect file header for compression: %s", path);
        return detect_result;
    }

    nmo_io_interface_t *io = NULL;

    if (!is_compressed && nmo_io_mmap_supported()) {
        nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file (mmap): %s", path);
        io = nmo_mmap_io_open(path);
        if (io == NULL) {
            nmo_log(logger, NMO_LOG_WARN,
                    "Failed to open mmap for file, falling back to standard IO: %s", path);
        }
    }

    if (io == NULL) {
        nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file: %s", path);
        io = nmo_file_io_open(path, NMO_IO_READ);
        if (io == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to open file: %s", path);
            load_perf_end(perf_stats, NMO_LOAD_PERF_OPEN_DETECT, open_detect_start);
            return NMO_ERR_FILE_NOT_FOUND;
        }
    }
    load_perf_end(perf_stats, NMO_LOAD_PERF_OPEN_DETECT, open_detect_start);

    /* Run the phased deserializer pipeline */
    nmo_deserializer_t *ds = nmo_deserializer_create(session, io, opts);
    if (ds == NULL) {
        nmo_io_close(io);
        return NMO_ERR_NOMEM;
    }

    nmo_status_t st = nmo_deserializer_parse_header(ds);
    if (st == NMO_OK && opts->profile != NMO_LOAD_PROFILE_FULL) {
        nmo_deserializer_destroy(ds);
        return NMO_OK;
    }
    if (st == NMO_OK) {
        st = nmo_deserializer_parse_objects(ds);
    }
    if (st == NMO_OK) {
        st = nmo_deserializer_finalize(ds);
    }

    nmo_deserializer_destroy(ds);
    return st;
}

nmo_status_t nmo_document_load_file(
    nmo_context_t *ctx,
    const char *path,
    const nmo_load_options_t *options,
    nmo_document_t **out_document)
{
    nmo_document_t *document = NULL;
    nmo_status_t status = NMO_OK;

    if (ctx == NULL || path == NULL || out_document == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_document = NULL;

    document = nmo_document_create(ctx);
    if (document == NULL) {
        return NMO_ERR_NOMEM;
    }

    status = nmo_document_internal_load_file(document, path, options);
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        return status;
    }

    *out_document = document;
    return NMO_OK;
}

const nmo_file_state_t *nmo_document_get_file_state(const nmo_document_t *document)
{
    return nmo_document_internal_file_state(document);
}

nmo_file_info_t nmo_document_get_file_info(const nmo_document_t *document)
{
    const nmo_file_state_t *file_state = nmo_document_get_file_state(document);
    nmo_file_info_t empty = {0};
    return file_state != NULL ? file_state->info : empty;
}

const nmo_header_t *nmo_document_get_header(const nmo_document_t *document)
{
    return nmo_document_internal_header(document);
}

int nmo_document_is_partial_load(const nmo_document_t *document)
{
    return nmo_document_internal_is_partial_load(document);
}

int nmo_document_has_materialized_load_state(const nmo_document_t *document)
{
    return nmo_document_internal_has_materialized_load_state(document);
}

nmo_status_t nmo_document_get_runtime_load_stats(
    const nmo_document_t *document,
    nmo_runtime_load_stats_t *out_stats)
{
    return nmo_document_internal_get_runtime_load_stats(document, out_stats);
}

const nmo_session_plugin_diagnostics_t *nmo_document_get_plugin_diagnostics(
    const nmo_document_t *document)
{
    return nmo_document_internal_plugin_diagnostics(document);
}
