/**
 * @file stats.c
 * @brief Implementation of file statistics collection
 * 
 * Reference: CKFile::WriteStats in reference/src/CKFile.cpp
 */

#include "app/nmo_stats.h"
#include "app/nmo_json_stream.h"
#include "app/nmo_session.h"
#include "session/nmo_session_internal.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/nmo_object_repository.h"
#include <string.h>
#include <time.h>
#include <stdarg.h>

/**
 * @brief Collect object statistics
 */
static void collect_object_stats(
    nmo_object_repository_t *repo,
    nmo_file_stats_t *stats
) {
    memset(&stats->objects, 0, sizeof(stats->objects));
    
    size_t count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &count);
    
    stats->objects.total_count = count;
    
    for (size_t i = 0; i < count; i++) {
        nmo_class_id_t class_id = objects[i]->class_id;
        
        if (class_id < 256) {
            stats->objects.by_class[class_id]++;
            
            if (class_id > stats->objects.max_class_id) {
                stats->objects.max_class_id = class_id;
            }
        }
    }
    
    /* Count unique classes */
    for (size_t i = 0; i < 256; i++) {
        if (stats->objects.by_class[i] > 0) {
            stats->objects.unique_classes++;
        }
    }
}

/**
 * @brief Collect memory statistics
 */
static void collect_memory_stats(
    nmo_object_repository_t *repo,
    nmo_file_stats_t *stats
) {
    memset(&stats->memory, 0, sizeof(stats->memory));
    memset(&stats->chunks, 0, sizeof(stats->chunks));
    
    size_t count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &count);
    
    size_t total_chunk_data = 0;
    size_t total_chunk_overhead = 0;
    size_t compressed_count = 0;
    size_t max_chunk_size = 0;
    size_t total_chunks = 0;
    
    for (size_t i = 0; i < count; i++) {
        nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
        if (chunk == NULL) {
            continue;
        }
        
        total_chunks++;
        
        /* Get chunk data size */
        size_t chunk_size = 0;
        nmo_chunk_get_data(chunk, &chunk_size);
        total_chunk_data += chunk_size;
        
        /* Estimate overhead (header, identifiers, etc.) */
        total_chunk_overhead += sizeof(nmo_chunk_t);
        
        if (chunk_size > max_chunk_size) {
            max_chunk_size = chunk_size;
        }
        
        /* Check if compressed */
        if (nmo_chunk_is_compressed(chunk)) {
            compressed_count++;
        }
    }
    
    stats->memory.chunk_data_size = total_chunk_data;
    stats->memory.chunk_overhead = total_chunk_overhead;
    stats->memory.data_size = total_chunk_data + total_chunk_overhead;
    
    /* Header size estimation (basic approximation) */
    stats->memory.header_size = 1024; /* Typical header size */
    stats->memory.total_size = stats->memory.header_size + stats->memory.data_size;
    
    /* Compression ratio */
    if (stats->memory.data_size > 0) {
        stats->memory.compression_ratio = 
            (stats->memory.chunk_data_size * 100) / stats->memory.data_size;
    }
    
    /* Chunk statistics */
    stats->chunks.total_chunks = total_chunks;
    stats->chunks.compressed_chunks = compressed_count;
    stats->chunks.max_chunk_size = max_chunk_size;
    if (total_chunks > 0) {
        stats->chunks.avg_chunk_size = total_chunk_data / total_chunks;
    }
}

/**
 * @brief Collect reference statistics
 */
static void collect_reference_stats(
    nmo_session_t *session,
    nmo_object_repository_t *repo,
    nmo_file_stats_t *stats
) {
    (void)repo;
    memset(&stats->references, 0, sizeof(stats->references));

    if (session == NULL) {
        return;
    }

    nmo_runtime_load_stats_t finish_stats = {0};
    if (nmo_session_get_runtime_load_stats(session, &finish_stats) == NMO_OK) {
        stats->references.total_references = finish_stats.references.total;
        stats->references.resolved = finish_stats.references.resolved;
        stats->references.unresolved = finish_stats.references.unresolved;
        return;
    }

    nmo_reference_resolver_t *resolver = nmo_session_get_reference_resolver(session);
    if (resolver == NULL) {
        return;
    }

    nmo_reference_stats_t resolver_stats = {0};
    if (nmo_reference_resolver_get_stats(resolver, &resolver_stats) != NMO_OK) {
        return;
    }

    stats->references.total_references = resolver_stats.total_count;
    stats->references.resolved = resolver_stats.resolved_count;
    stats->references.unresolved = resolver_stats.unresolved_count;
}

int nmo_stats_collect(
    nmo_session_t *session,
    nmo_file_stats_t *out_stats
) {
    if (session == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    memset(out_stats, 0, sizeof(nmo_file_stats_t));
    
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    
    /* Collect different categories of statistics */
    collect_object_stats(repo, out_stats);
    collect_memory_stats(repo, out_stats);
    collect_reference_stats(session, repo, out_stats);
    
    /* Performance stats would be collected during load/save operations */
    /* For now, leave them at zero */
    
    return NMO_OK;
}

void nmo_stats_print(
    const nmo_file_stats_t *stats,
    FILE *output
) {
    if (stats == NULL || output == NULL) {
        return;
    }
    
    fprintf(output, "=== File Statistics ===\n\n");
    
    /* Object statistics */
    fprintf(output, "Objects:\n");
    fprintf(output, "  Total:          %zu\n", stats->objects.total_count);
    fprintf(output, "  Unique Classes: %zu\n", stats->objects.unique_classes);
    fprintf(output, "  Max Class ID:   %zu\n", stats->objects.max_class_id);
    
    /* Show class distribution */
    fprintf(output, "\n  Distribution by Class:\n");
    for (size_t i = 0; i <= stats->objects.max_class_id && i < 256; i++) {
        if (stats->objects.by_class[i] > 0) {
            fprintf(output, "    Class %3zu: %5zu objects\n", 
                   i, stats->objects.by_class[i]);
        }
    }
    
    /* Memory statistics */
    fprintf(output, "\nMemory:\n");
    fprintf(output, "  Total Size:      %zu bytes (%.2f KB)\n", 
           stats->memory.total_size,
           stats->memory.total_size / 1024.0);
    fprintf(output, "  Header Size:     %zu bytes\n", stats->memory.header_size);
    fprintf(output, "  Data Size:       %zu bytes (%.2f KB)\n",
           stats->memory.data_size,
           stats->memory.data_size / 1024.0);
    fprintf(output, "  Chunk Data:      %zu bytes\n", stats->memory.chunk_data_size);
    fprintf(output, "  Chunk Overhead:  %zu bytes\n", stats->memory.chunk_overhead);
    fprintf(output, "  Compression:     %zu%%\n", stats->memory.compression_ratio);
    
    /* Chunk statistics */
    fprintf(output, "\nChunks:\n");
    fprintf(output, "  Total Chunks:    %zu\n", stats->chunks.total_chunks);
    fprintf(output, "  Compressed:      %zu (%.1f%%)\n",
           stats->chunks.compressed_chunks,
           stats->chunks.total_chunks > 0 
               ? (stats->chunks.compressed_chunks * 100.0) / stats->chunks.total_chunks
               : 0.0);
    fprintf(output, "  Max Size:        %zu bytes\n", stats->chunks.max_chunk_size);
    fprintf(output, "  Avg Size:        %zu bytes\n", stats->chunks.avg_chunk_size);
    
    /* Performance statistics */
    if (stats->performance.load_time_ms > 0) {
        fprintf(output, "\nPerformance:\n");
        fprintf(output, "  Load Time:       %.2f ms\n", stats->performance.load_time_ms);
        fprintf(output, "  Parse Time:      %.2f ms\n", stats->performance.parse_time_ms);
        fprintf(output, "  Remap Time:      %.2f ms\n", stats->performance.remap_time_ms);
    }
    
    /* Reference statistics */
    if (stats->references.total_references > 0) {
        fprintf(output, "\nReferences:\n");
        fprintf(output, "  Total:           %zu\n", stats->references.total_references);
        fprintf(output, "  Resolved:        %zu\n", stats->references.resolved);
        fprintf(output, "  Unresolved:      %zu\n", stats->references.unresolved);
    }
}

void nmo_stats_print_summary(
    const nmo_file_stats_t *stats,
    FILE *output
) {
    if (stats == NULL || output == NULL) {
        return;
    }
    
    fprintf(output, 
           "Objects: %zu | Classes: %zu | Size: %.2f KB | Chunks: %zu (%.1f%% compressed)\n",
           stats->objects.total_count,
           stats->objects.unique_classes,
           stats->memory.total_size / 1024.0,
           stats->chunks.total_chunks,
           stats->chunks.total_chunks > 0
               ? (stats->chunks.compressed_chunks * 100.0) / stats->chunks.total_chunks
               : 0.0);
}

int nmo_stats_export_json(
    const nmo_file_stats_t *stats,
    const char *output_path
) {
    if (stats == NULL || output_path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        return -1;
    }

    nmo_json_stream_t js;
    nmo_json_stream_init(&js, out, true);

#define JSON_TRY(expr) do { if (!(expr)) goto fail; } while (0)

    JSON_TRY(nmo_json_stream_begin_object(&js));

    JSON_TRY(nmo_json_stream_key(&js, "objects"));
    JSON_TRY(nmo_json_stream_begin_object(&js));
    JSON_TRY(nmo_json_stream_key(&js, "total_count"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->objects.total_count));
    JSON_TRY(nmo_json_stream_key(&js, "unique_classes"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->objects.unique_classes));
    JSON_TRY(nmo_json_stream_key(&js, "max_class_id"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->objects.max_class_id));
    JSON_TRY(nmo_json_stream_key(&js, "by_class"));
    JSON_TRY(nmo_json_stream_begin_array(&js));
    for (size_t i = 0; i <= stats->objects.max_class_id && i < 256; ++i) {
        if (stats->objects.by_class[i] == 0) {
            continue;
        }
        JSON_TRY(nmo_json_stream_begin_object(&js));
        JSON_TRY(nmo_json_stream_key(&js, "class_id"));
        JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)i));
        JSON_TRY(nmo_json_stream_key(&js, "count"));
        JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->objects.by_class[i]));
        JSON_TRY(nmo_json_stream_end_object(&js));
    }
    JSON_TRY(nmo_json_stream_end_array(&js));
    JSON_TRY(nmo_json_stream_end_object(&js));

    JSON_TRY(nmo_json_stream_key(&js, "memory"));
    JSON_TRY(nmo_json_stream_begin_object(&js));
    JSON_TRY(nmo_json_stream_key(&js, "total_size"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->memory.total_size));
    JSON_TRY(nmo_json_stream_key(&js, "header_size"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->memory.header_size));
    JSON_TRY(nmo_json_stream_key(&js, "data_size"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->memory.data_size));
    JSON_TRY(nmo_json_stream_key(&js, "chunk_data_size"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->memory.chunk_data_size));
    JSON_TRY(nmo_json_stream_key(&js, "chunk_overhead"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->memory.chunk_overhead));
    JSON_TRY(nmo_json_stream_key(&js, "compression_ratio"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->memory.compression_ratio));
    JSON_TRY(nmo_json_stream_end_object(&js));

    JSON_TRY(nmo_json_stream_key(&js, "chunks"));
    JSON_TRY(nmo_json_stream_begin_object(&js));
    JSON_TRY(nmo_json_stream_key(&js, "total_chunks"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->chunks.total_chunks));
    JSON_TRY(nmo_json_stream_key(&js, "compressed_chunks"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->chunks.compressed_chunks));
    JSON_TRY(nmo_json_stream_key(&js, "max_chunk_size"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->chunks.max_chunk_size));
    JSON_TRY(nmo_json_stream_key(&js, "avg_chunk_size"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->chunks.avg_chunk_size));
    JSON_TRY(nmo_json_stream_end_object(&js));

    JSON_TRY(nmo_json_stream_key(&js, "performance"));
    JSON_TRY(nmo_json_stream_begin_object(&js));
    JSON_TRY(nmo_json_stream_key(&js, "load_time_ms"));
    JSON_TRY(nmo_json_stream_value_real(&js, stats->performance.load_time_ms));
    JSON_TRY(nmo_json_stream_key(&js, "parse_time_ms"));
    JSON_TRY(nmo_json_stream_value_real(&js, stats->performance.parse_time_ms));
    JSON_TRY(nmo_json_stream_key(&js, "remap_time_ms"));
    JSON_TRY(nmo_json_stream_value_real(&js, stats->performance.remap_time_ms));
    JSON_TRY(nmo_json_stream_end_object(&js));

    JSON_TRY(nmo_json_stream_key(&js, "references"));
    JSON_TRY(nmo_json_stream_begin_object(&js));
    JSON_TRY(nmo_json_stream_key(&js, "total_references"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->references.total_references));
    JSON_TRY(nmo_json_stream_key(&js, "resolved"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->references.resolved));
    JSON_TRY(nmo_json_stream_key(&js, "unresolved"));
    JSON_TRY(nmo_json_stream_value_uint(&js, (uint64_t)stats->references.unresolved));
    JSON_TRY(nmo_json_stream_end_object(&js));

    JSON_TRY(nmo_json_stream_end_object(&js));
    if (fputc('\n', out) == EOF) {
        goto fail;
    }

#undef JSON_TRY

    if (fclose(out) != 0) {
        return -1;
    }
    return 0;

fail:
    (void)fclose(out);
    return -1;
}
