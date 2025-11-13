/**
 * @file inspector.c
 * @brief Implementation of chunk inspection and debugging utilities
 * 
 * Reference: CKStateChunk debugging helpers in reference implementation
 */

#include "app/nmo_inspector.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_parser.h"
#include "yyjson.h"
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ANSI color codes */
#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"

void nmo_inspector_init_options(nmo_inspector_options_t *options) {
    if (options == NULL) return;
    
    memset(options, 0, sizeof(nmo_inspector_options_t));
    options->level = NMO_DUMP_NORMAL;
    options->show_hex = false;
    options->show_sub_chunks = true;
    options->validate = false;
    options->colorize = false;
    options->max_depth = 0;  /* Unlimited */
    options->hex_bytes = 16;
}

/**
 * @brief Helper to print with optional color
 */
static void print_colored(
    FILE *stream,
    bool colorize,
    const char *color,
    const char *format,
    ...
) {
    va_list args;
    va_start(args, format);
    
    if (colorize) {
        fprintf(stream, "%s", color);
    }
    vfprintf(stream, format, args);
    if (colorize) {
        fprintf(stream, "%s", ANSI_RESET);
    }
    
    va_end(args);
}

/**
 * @brief Print indentation
 */
static void print_indent(FILE *stream, size_t depth) {
    for (size_t i = 0; i < depth; i++) {
        fprintf(stream, "  ");
    }
}

/**
 * @brief Dump chunk data as hex
 */
static void dump_hex_data(
    FILE *stream,
    const uint8_t *data,
    size_t size,
    size_t bytes_per_line,
    bool colorize
) {
    for (size_t offset = 0; offset < size; offset += bytes_per_line) {
        /* Offset */
        print_colored(stream, colorize, ANSI_CYAN, "    %08zx: ", offset);
        
        /* Hex bytes */
        size_t line_bytes = (offset + bytes_per_line <= size) 
                           ? bytes_per_line 
                           : (size - offset);
        
        for (size_t i = 0; i < bytes_per_line; i++) {
            if (i < line_bytes) {
                fprintf(stream, "%02x ", data[offset + i]);
            } else {
                fprintf(stream, "   ");
            }
            
            if (i == bytes_per_line / 2 - 1) {
                fprintf(stream, " ");
            }
        }
        
        /* ASCII representation */
        fprintf(stream, " |");
        for (size_t i = 0; i < line_bytes; i++) {
            uint8_t c = data[offset + i];
            fprintf(stream, "%c", isprint(c) ? c : '.');
        }
        fprintf(stream, "|\n");
    }
}

/**
 * @brief Dump chunk recursively
 */
static int dump_chunk_recursive(
    const nmo_chunk_t *chunk,
    FILE *stream,
    const nmo_inspector_options_t *options,
    size_t depth
) {
    if (chunk == NULL || stream == NULL || options == NULL) {
        return -1;
    }
    
    /* Check depth limit */
    if (options->max_depth > 0 && depth >= options->max_depth) {
        print_indent(stream, depth);
        fprintf(stream, "(max depth reached)\n");
        return 0;
    }
    
    bool colorize = options->colorize;
    
    print_indent(stream, depth);
    print_colored(stream, colorize, ANSI_BOLD, "Chunk");
    fprintf(stream, " {\n");
    
    /* Chunk ID */
    uint32_t chunk_id = nmo_chunk_get_class_id(chunk);
    print_indent(stream, depth + 1);
    print_colored(stream, colorize, ANSI_YELLOW, "ID: ");
    fprintf(stream, "%u (0x%08x)\n", chunk_id, chunk_id);
    
    /* Data size */
    size_t data_size = 0;
    const uint8_t *data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
    print_indent(stream, depth + 1);
    print_colored(stream, colorize, ANSI_YELLOW, "Data Size: ");
    fprintf(stream, "%zu bytes\n", data_size);
    
    /* Data preview */
    if (options->level >= NMO_DUMP_DETAILED && data_size > 0) {
        print_indent(stream, depth + 1);
        print_colored(stream, colorize, ANSI_YELLOW, "Data Preview:\n");
        
        if (data != NULL) {
            size_t preview_size = (options->level >= NMO_DUMP_FULL) 
                                 ? data_size 
                                 : (data_size > 64 ? 64 : data_size);
            
            if (options->show_hex) {
                dump_hex_data(stream, data, preview_size, options->hex_bytes, colorize);
            } else {
                /* Simple preview */
                print_indent(stream, depth + 2);
                fprintf(stream, "(first %zu bytes): ", preview_size);
                for (size_t i = 0; i < preview_size && i < 32; i++) {
                    fprintf(stream, "%02x ", data[i]);
                }
                if (preview_size > 32) {
                    fprintf(stream, "...");
                }
                fprintf(stream, "\n");
            }
            
            if (preview_size < data_size) {
                print_indent(stream, depth + 2);
                fprintf(stream, "(%zu more bytes not shown)\n", 
                       data_size - preview_size);
            }
        }
    }
    
    /* Sub-chunks */
    if (options->show_sub_chunks) {
        uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
        if (sub_count > 0) {
            print_indent(stream, depth + 1);
            print_colored(stream, colorize, ANSI_YELLOW, "Sub-chunks: ");
            fprintf(stream, "%u\n", sub_count);
            
            for (uint32_t i = 0; i < sub_count; i++) {
                nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
                if (sub != NULL) {
                    print_indent(stream, depth + 1);
                    print_colored(stream, colorize, ANSI_CYAN, "[%u]\n", i);
                    dump_chunk_recursive(sub, stream, options, depth + 2);
                }
            }
        }
    }
    
    print_indent(stream, depth);
    fprintf(stream, "}\n");
    
    return 0;
}

int nmo_inspector_dump_chunk(
    const nmo_chunk_t *chunk,
    FILE *stream,
    const nmo_inspector_options_t *options
) {
    if (chunk == NULL || stream == NULL) {
        return -1;
    }
    
    nmo_inspector_options_t default_opts;
    if (options == NULL) {
        nmo_inspector_init_options(&default_opts);
        options = &default_opts;
    }
    
    return dump_chunk_recursive(chunk, stream, options, 0);
}

int nmo_inspector_validate_chunk(
    const nmo_chunk_t *chunk,
    nmo_chunk_validation_t *result
) {
    if (chunk == NULL || result == NULL) {
        return -1;
    }
    
    memset(result, 0, sizeof(nmo_chunk_validation_t));
    result->is_valid = true;
    result->header_valid = true;
    result->identifiers_valid = true;
    result->data_valid = true;
    result->sub_chunks_valid = true;
    
    /* Check data region */
    size_t data_size = 0;
    const void *data = nmo_chunk_get_data(chunk, &data_size);
    
    if (data_size > 0 && data == NULL) {
        result->is_valid = false;
        result->data_valid = false;
        result->error_count++;
        snprintf(result->error_message, sizeof(result->error_message),
                "Data size is %zu but data pointer is NULL", data_size);
        return 0;
    }
    
    /* Validate sub-chunks recursively */
    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
    for (uint32_t i = 0; i < sub_count; i++) {
        nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
        if (sub == NULL) {
            result->is_valid = false;
            result->sub_chunks_valid = false;
            result->error_count++;
            snprintf(result->error_message, sizeof(result->error_message),
                    "Sub-chunk %u is NULL", i);
            return 0;
        }
        
        nmo_chunk_validation_t sub_result;
        if (nmo_inspector_validate_chunk(sub, &sub_result) != 0) {
            result->is_valid = false;
            result->sub_chunks_valid = false;
            result->error_count++;
            return 0;
        }
        
        if (!sub_result.is_valid) {
            result->is_valid = false;
            result->sub_chunks_valid = false;
            result->error_count += sub_result.error_count;
            result->warning_count += sub_result.warning_count;
            snprintf(result->error_message, sizeof(result->error_message),
                    "Sub-chunk %u validation failed: %s", i, sub_result.error_message);
            return 0;
        }
    }
    
    return 0;
}

int nmo_inspector_hex_dump(
    const nmo_chunk_t *chunk,
    FILE *stream,
    size_t max_bytes,
    size_t bytes_per_line
) {
    if (chunk == NULL || stream == NULL) {
        return -1;
    }
    
    if (bytes_per_line == 0) {
        bytes_per_line = 16;
    }
    
    size_t data_size = 0;
    const uint8_t *data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
    
    if (data_size == 0 || data == NULL) {
        fprintf(stream, "(no data)\n");
        return 0;
    }
    
    size_t dump_size = (max_bytes > 0 && max_bytes < data_size) 
                      ? max_bytes 
                      : data_size;
    
    dump_hex_data(stream, data, dump_size, bytes_per_line, false);
    
    return (int)dump_size;
}

int nmo_inspector_print_summary(
    const nmo_chunk_t *chunk,
    FILE *stream
) {
    if (chunk == NULL || stream == NULL) {
        return -1;
    }
    
    nmo_class_id_t class_id = nmo_chunk_get_class_id(chunk);
    size_t data_size = 0;
    nmo_chunk_get_data(chunk, &data_size);
    size_t id_count = nmo_chunk_get_id_count(chunk);
    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
    
    fprintf(stream, "Class=%d IDs=[%zu] Data=%zu bytes Sub=[%u]",
           class_id, id_count, data_size, sub_count);
    
    if (nmo_chunk_is_compressed(chunk)) {
        fprintf(stream, " [COMPRESSED]");
    }
    
    fprintf(stream, "\n");
    return 0;
}

int nmo_inspector_compare_chunks(
    const nmo_chunk_t *chunk1,
    const nmo_chunk_t *chunk2,
    FILE *stream
) {
    if (chunk1 == NULL || chunk2 == NULL || stream == NULL) {
        return -1;
    }
    
    int differences = 0;
    
    /* Compare chunk IDs */
    uint32_t id1 = nmo_chunk_get_class_id(chunk1);
    uint32_t id2 = nmo_chunk_get_class_id(chunk2);
    if (id1 != id2) {
        fprintf(stream, "Chunk ID differs: %u vs %u\n", id1, id2);
        differences++;
    }
    
    /* Compare data sizes */
    size_t size1 = 0;
    size_t size2 = 0;
    nmo_chunk_get_data(chunk1, &size1);
    nmo_chunk_get_data(chunk2, &size2);
    if (size1 != size2) {
        fprintf(stream, "Data size differs: %zu vs %zu\n", size1, size2);
        differences++;
    }
    
    /* Compare data content */
    if (size1 == size2 && size1 > 0) {
        size_t temp_size1 = 0, temp_size2 = 0;
        const uint8_t *data1 = (const uint8_t *)nmo_chunk_get_data(chunk1, &temp_size1);
        const uint8_t *data2 = (const uint8_t *)nmo_chunk_get_data(chunk2, &temp_size2);
        
        if (data1 != NULL && data2 != NULL) {
            if (memcmp(data1, data2, size1) != 0) {
                fprintf(stream, "Data content differs\n");
                differences++;
                
                /* Find first difference */
                for (size_t i = 0; i < size1; i++) {
                    if (data1[i] != data2[i]) {
                        fprintf(stream, "  First difference at offset %zu: 0x%02x vs 0x%02x\n",
                               i, data1[i], data2[i]);
                        break;
                    }
                }
            }
        }
    }
    
    /* Compare ID counts */
    size_t id_count1 = nmo_chunk_get_id_count(chunk1);
    size_t id_count2 = nmo_chunk_get_id_count(chunk2);
    if (id_count1 != id_count2) {
        fprintf(stream, "ID count differs: %zu vs %zu\n", id_count1, id_count2);
        differences++;
    }
    
    /* Compare sub-chunk counts */
    uint32_t sub_count1 = nmo_chunk_get_sub_chunk_count(chunk1);
    uint32_t sub_count2 = nmo_chunk_get_sub_chunk_count(chunk2);
    if (sub_count1 != sub_count2) {
        fprintf(stream, "Sub-chunk count differs: %u vs %u\n", sub_count1, sub_count2);
        differences++;
    }
    
    if (differences == 0) {
        fprintf(stream, "Chunks are identical\n");
        return 0;
    }
    
    return 1;
}

/* Helper to convert chunk to JSON recursively */
static yyjson_mut_val* chunk_to_json(
    yyjson_mut_doc *doc,
    const nmo_chunk_t *chunk,
    bool include_data
) {
    if (!chunk) return yyjson_mut_null(doc);
    
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    
    /* Basic info */
    nmo_class_id_t class_id = nmo_chunk_get_class_id(chunk);
    yyjson_mut_obj_add_uint(doc, obj, "class_id", class_id);
    
    size_t data_size = 0;
    nmo_chunk_get_data(chunk, &data_size);
    yyjson_mut_obj_add_uint(doc, obj, "data_size", data_size);
    
    size_t id_count = nmo_chunk_get_id_count(chunk);
    yyjson_mut_obj_add_uint(doc, obj, "id_count", id_count);
    
    if (nmo_chunk_is_compressed(chunk)) {
        yyjson_mut_obj_add_bool(doc, obj, "compressed", true);
    }
    
    /* Data (hex-encoded if requested) */
    if (include_data && data_size > 0) {
        size_t temp_size = 0;
        const uint8_t *data = (const uint8_t *)nmo_chunk_get_data(chunk, &temp_size);
        if (data != NULL) {
            char *hex = (char*)malloc(data_size * 2 + 1);
            if (hex) {
                for (size_t i = 0; i < data_size; i++) {
                    sprintf(hex + i * 2, "%02x", data[i]);
                }
                hex[data_size * 2] = '\0';
                yyjson_mut_obj_add_strcpy(doc, obj, "data_hex", hex);
                free(hex);
            }
        }
    }
    
    /* Sub-chunks */
    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
    if (sub_count > 0) {
        yyjson_mut_val *sub_arr = yyjson_mut_arr(doc);
        for (uint32_t i = 0; i < sub_count; i++) {
            nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
            if (sub != NULL) {
                yyjson_mut_val *sub_json = chunk_to_json(doc, sub, include_data);
                yyjson_mut_arr_append(sub_arr, sub_json);
            }
        }
        yyjson_mut_obj_add_val(doc, obj, "sub_chunks", sub_arr);
    }
    
    return obj;
}

int nmo_inspector_export_json(
    const nmo_chunk_t *chunk,
    FILE *stream,
    bool include_data
) {
    if (chunk == NULL || stream == NULL) {
        return -1;
    }
    
    /* Create JSON document */
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return -1;
    
    /* Convert chunk to JSON */
    yyjson_mut_val *root = chunk_to_json(doc, chunk, include_data);
    yyjson_mut_doc_set_root(doc, root);
    
    /* Write to stream */
    yyjson_write_flag flg = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
    char *json = yyjson_mut_write(doc, flg, NULL);
    if (json) {
        fputs(json, stream);
        free(json);
    }
    
    yyjson_mut_doc_free(doc);
    return json ? 0 : -1;
}
