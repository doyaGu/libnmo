/**
 * @file stats.c
 * @brief Implementation of file statistics collection
 * 
 * Reference: CKFile::WriteStats in reference/src/CKFile.cpp
 */

#include "document/nmo_document_stats.h"
#include "export/nmo_export_json.h"
#include "export/nmo_export_text.h"
#include "../runtime/runtime_internal.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "yyjson.h"
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
    
    size_t count = nmo_object_repository_get_count(repo);
    
    stats->objects.total_count = count;
    
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }
        nmo_class_id_t class_id = object->class_id;
        
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
    
    size_t count = nmo_object_repository_get_count(repo);
    
    size_t total_chunk_data = 0;
    size_t total_chunk_overhead = 0;
    size_t compressed_count = 0;
    size_t max_chunk_size = 0;
    size_t total_chunks = 0;
    
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }
        nmo_chunk_t *chunk = nmo_object_get_chunk(object);
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
    nmo_document_t *document,
    nmo_object_repository_t *repo,
    nmo_file_stats_t *stats
) {
    (void)repo;
    memset(&stats->references, 0, sizeof(stats->references));

    if (document == NULL) {
        return;
    }

    nmo_runtime_load_stats_t finish_stats = {0};
    if (nmo_document_internal_get_runtime_load_stats(document, &finish_stats) == NMO_OK) {
        stats->references.total_references = finish_stats.references.total;
        stats->references.resolved = finish_stats.references.resolved;
        stats->references.unresolved = finish_stats.references.unresolved;
        return;
    }

}

nmo_status_t nmo_stats_collect(
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
    nmo_runtime_load_stats_t finish_stats = {0};
    if (nmo_session_get_runtime_load_stats(session, &finish_stats) == NMO_OK) {
        out_stats->references.total_references = finish_stats.references.total;
        out_stats->references.resolved = finish_stats.references.resolved;
        out_stats->references.unresolved = finish_stats.references.unresolved;
    }
    
    /* Performance stats would be collected during load/save operations */
    /* For now, leave them at zero */
    
    return NMO_OK;
}

nmo_status_t nmo_document_stats_collect(
    nmo_document_t *document,
    nmo_file_stats_t *out_stats
) {
    if (document == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_stats, 0, sizeof(nmo_file_stats_t));

    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    collect_object_stats(repo, out_stats);
    collect_memory_stats(repo, out_stats);
    collect_reference_stats(document, repo, out_stats);
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

void nmo_export_text_document_stats(
    const nmo_file_stats_t *stats,
    FILE *stream
) {
    nmo_stats_print(stats, stream);
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

void nmo_export_text_document_stats_summary(
    const nmo_file_stats_t *stats,
    FILE *stream
) {
    nmo_stats_print_summary(stats, stream);
}

nmo_status_t nmo_stats_export_json(
    const nmo_file_stats_t *stats,
    const char *output_path
) {
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *root = NULL;
    yyjson_mut_val *section = NULL;
    yyjson_mut_val *by_class = NULL;
    yyjson_write_flag flags =
        YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END;

    if (stats == NULL || output_path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return -1;
    }

    root = yyjson_mut_obj(doc);
    if (!root) {
        yyjson_mut_doc_free(doc);
        return -1;
    }
    yyjson_mut_doc_set_root(doc, root);

    section = yyjson_mut_obj(doc);
    by_class = yyjson_mut_arr(doc);
    if (!section || !by_class) {
        yyjson_mut_doc_free(doc);
        return -1;
    }
    yyjson_mut_obj_add_uint(doc, section, "total_count",
                            (uint64_t)stats->objects.total_count);
    yyjson_mut_obj_add_uint(doc, section, "unique_classes",
                            (uint64_t)stats->objects.unique_classes);
    yyjson_mut_obj_add_uint(doc, section, "max_class_id",
                            (uint64_t)stats->objects.max_class_id);
    for (size_t i = 0; i <= stats->objects.max_class_id && i < 256; ++i) {
        yyjson_mut_val *entry = NULL;
        if (stats->objects.by_class[i] == 0) {
            continue;
        }
        entry = yyjson_mut_obj(doc);
        if (!entry) {
            yyjson_mut_doc_free(doc);
            return -1;
        }
        yyjson_mut_obj_add_uint(doc, entry, "class_id", (uint64_t)i);
        yyjson_mut_obj_add_uint(doc, entry, "count",
                                (uint64_t)stats->objects.by_class[i]);
        yyjson_mut_arr_add_val(by_class, entry);
    }
    yyjson_mut_obj_add_val(doc, section, "by_class", by_class);
    yyjson_mut_obj_add_val(doc, root, "objects", section);

    section = yyjson_mut_obj(doc);
    if (!section) {
        yyjson_mut_doc_free(doc);
        return -1;
    }
    yyjson_mut_obj_add_uint(doc, section, "total_size",
                            (uint64_t)stats->memory.total_size);
    yyjson_mut_obj_add_uint(doc, section, "header_size",
                            (uint64_t)stats->memory.header_size);
    yyjson_mut_obj_add_uint(doc, section, "data_size",
                            (uint64_t)stats->memory.data_size);
    yyjson_mut_obj_add_uint(doc, section, "chunk_data_size",
                            (uint64_t)stats->memory.chunk_data_size);
    yyjson_mut_obj_add_uint(doc, section, "chunk_overhead",
                            (uint64_t)stats->memory.chunk_overhead);
    yyjson_mut_obj_add_uint(doc, section, "compression_ratio",
                            (uint64_t)stats->memory.compression_ratio);
    yyjson_mut_obj_add_val(doc, root, "memory", section);

    section = yyjson_mut_obj(doc);
    if (!section) {
        yyjson_mut_doc_free(doc);
        return -1;
    }
    yyjson_mut_obj_add_uint(doc, section, "total_chunks",
                            (uint64_t)stats->chunks.total_chunks);
    yyjson_mut_obj_add_uint(doc, section, "compressed_chunks",
                            (uint64_t)stats->chunks.compressed_chunks);
    yyjson_mut_obj_add_uint(doc, section, "max_chunk_size",
                            (uint64_t)stats->chunks.max_chunk_size);
    yyjson_mut_obj_add_uint(doc, section, "avg_chunk_size",
                            (uint64_t)stats->chunks.avg_chunk_size);
    yyjson_mut_obj_add_val(doc, root, "chunks", section);

    section = yyjson_mut_obj(doc);
    if (!section) {
        yyjson_mut_doc_free(doc);
        return -1;
    }
    yyjson_mut_obj_add_real(doc, section, "load_time_ms",
                            stats->performance.load_time_ms);
    yyjson_mut_obj_add_real(doc, section, "parse_time_ms",
                            stats->performance.parse_time_ms);
    yyjson_mut_obj_add_real(doc, section, "remap_time_ms",
                            stats->performance.remap_time_ms);
    yyjson_mut_obj_add_val(doc, root, "performance", section);

    section = yyjson_mut_obj(doc);
    if (!section) {
        yyjson_mut_doc_free(doc);
        return -1;
    }
    yyjson_mut_obj_add_uint(doc, section, "total_references",
                            (uint64_t)stats->references.total_references);
    yyjson_mut_obj_add_uint(doc, section, "resolved",
                            (uint64_t)stats->references.resolved);
    yyjson_mut_obj_add_uint(doc, section, "unresolved",
                            (uint64_t)stats->references.unresolved);
    yyjson_mut_obj_add_val(doc, root, "references", section);

    if (!yyjson_mut_write_file(output_path, doc, flags, NULL, NULL)) {
        yyjson_mut_doc_free(doc);
        return -1;
    }

    yyjson_mut_doc_free(doc);
    return 0;
}

nmo_status_t nmo_export_json_document_stats(
    const nmo_file_stats_t *stats,
    const char *output_path
) {
    return nmo_stats_export_json(stats, output_path);
}

