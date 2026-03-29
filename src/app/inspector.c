/**
 * @file inspector.c
 * @brief Implementation of chunk inspection and debugging utilities
 *
 * Reference: CKStateChunk debugging helpers in reference implementation
 */

#include "app/nmo_inspector.h"
#include "app/nmo_ansi.h"
#include "app/nmo_hexdump.h"
#include "app/nmo_json_stream.h"
#include "core/nmo_error.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

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
        fprintf(stream, "%s", NMO_ANSI_RESET);
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
 * @brief Dump chunk recursively
 */
static nmo_status_t dump_chunk_recursive(
    const nmo_chunk_t *chunk,
    FILE *stream,
    const nmo_inspector_options_t *options,
    size_t depth
) {
    if (chunk == NULL || stream == NULL || options == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check depth limit */
    if (options->max_depth > 0 && depth >= options->max_depth) {
        print_indent(stream, depth);
        fprintf(stream, "(max depth reached)\n");
        return NMO_OK;
    }
    
    bool colorize = options->colorize;
    
    print_indent(stream, depth);
    print_colored(stream, colorize, NMO_ANSI_BOLD, "Chunk");
    fprintf(stream, " {\n");
    
    /* Chunk ID */
    uint32_t chunk_id = nmo_chunk_get_class_id(chunk);
    print_indent(stream, depth + 1);
    print_colored(stream, colorize, NMO_ANSI_YELLOW, "ID: ");
    fprintf(stream, "%u (0x%08x)\n", chunk_id, chunk_id);
    
    /* Data size */
    size_t data_size = 0;
    const uint8_t *data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
    print_indent(stream, depth + 1);
    print_colored(stream, colorize, NMO_ANSI_YELLOW, "Data Size: ");
    fprintf(stream, "%zu bytes\n", data_size);
    
    /* Data preview */
    if (options->level >= NMO_DUMP_DETAILED && data_size > 0) {
        print_indent(stream, depth + 1);
        print_colored(stream, colorize, NMO_ANSI_YELLOW, "Data Preview:\n");
        
        if (data != NULL) {
            size_t preview_size = (options->level >= NMO_DUMP_FULL) 
                                 ? data_size 
                                 : (data_size > 64 ? 64 : data_size);
            
            if (options->show_hex) {
                nmo_hexdump_options_t hd;
                nmo_hexdump_init_options(&hd);
                hd.colorize = colorize;
                hd.bytes_per_line = options->hex_bytes ? options->hex_bytes : 16;
                hd.group_size = hd.bytes_per_line / 2;
                hd.indent_spaces = (depth + 2) * 2;
                hd.ansi.offset = NMO_ANSI_CYAN;
                hd.ansi.hex = "";
                hd.ansi.ascii = "";
                hd.ansi.delim = "";
                hd.ansi.reset = NMO_ANSI_RESET;

                print_indent(stream, depth + 2);
                fprintf(stream, "(hexdump -C, first %zu bytes)\n", preview_size);
                nmo_hexdump_canonical(stream, data, preview_size, &hd);
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
            print_colored(stream, colorize, NMO_ANSI_YELLOW, "Sub-chunks: ");
            fprintf(stream, "%u\n", sub_count);
            
            for (uint32_t i = 0; i < sub_count; i++) {
                nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
                if (sub != NULL) {
                    print_indent(stream, depth + 1);
                    print_colored(stream, colorize, NMO_ANSI_CYAN, "[%u]\n", i);
                    dump_chunk_recursive(sub, stream, options, depth + 2);
                }
            }
        }
    }
    
    print_indent(stream, depth);
    fprintf(stream, "}\n");

    return NMO_OK;
}

nmo_status_t nmo_inspector_dump_chunk(
    const nmo_chunk_t *chunk,
    FILE *stream,
    const nmo_inspector_options_t *options
) {
    if (chunk == NULL || stream == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_inspector_options_t default_opts;
    if (options == NULL) {
        nmo_inspector_init_options(&default_opts);
        options = &default_opts;
    }

    return dump_chunk_recursive(chunk, stream, options, 0);
}

nmo_status_t nmo_inspector_validate_chunk(
    const nmo_chunk_t *chunk,
    nmo_chunk_validation_t *result
) {
    if (chunk == NULL || result == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
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
        return NMO_OK;
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
            return NMO_OK;
        }

        nmo_chunk_validation_t sub_result;
        if (nmo_inspector_validate_chunk(sub, &sub_result) != NMO_OK) {
            result->is_valid = false;
            result->sub_chunks_valid = false;
            result->error_count++;
            return NMO_OK;
        }

        if (!sub_result.is_valid) {
            result->is_valid = false;
            result->sub_chunks_valid = false;
            result->error_count += sub_result.error_count;
            result->warning_count += sub_result.warning_count;
            {
                const int prefix_len = snprintf(NULL, 0, "Sub-chunk %u validation failed: ", i);
                const size_t avail = (prefix_len >= 0 && (size_t)prefix_len < sizeof(result->error_message))
                    ? sizeof(result->error_message) - (size_t)prefix_len - 1u : 0u;
                snprintf(result->error_message, sizeof(result->error_message),
                        "Sub-chunk %u validation failed: %.*s", i, (int)avail,
                        sub_result.error_message);
            }
            return NMO_OK;
        }
    }

    return NMO_OK;
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

    nmo_hexdump_options_t hd;
    nmo_hexdump_init_options(&hd);
    hd.colorize = false;
    hd.bytes_per_line = bytes_per_line;
    hd.group_size = bytes_per_line / 2;
    hd.indent_spaces = 0;
    nmo_hexdump_canonical(stream, data, dump_size, &hd);
    
    return (int)dump_size;
}

nmo_status_t nmo_inspector_print_summary(
    const nmo_chunk_t *chunk,
    FILE *stream
) {
    if (chunk == NULL || stream == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
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
    return NMO_OK;
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

static nmo_status_t json_write_chunk(nmo_json_stream_t *writer,
                            const nmo_chunk_t *chunk,
                            bool include_data) {
    if (!writer || !chunk) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!nmo_json_stream_begin_object(writer)) {
        return NMO_ERR_CANT_WRITE_FILE;
    }

    size_t data_size = 0;
    const uint8_t *data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);

    if (!nmo_json_stream_key(writer, "class_id") ||
        !nmo_json_stream_value_uint(writer, (uint64_t)nmo_chunk_get_class_id(chunk))) {
        return NMO_ERR_CANT_WRITE_FILE;
    }

    if (!nmo_json_stream_key(writer, "data_size") ||
        !nmo_json_stream_value_uint(writer, (uint64_t)data_size)) {
        return NMO_ERR_CANT_WRITE_FILE;
    }

    if (!nmo_json_stream_key(writer, "id_count") ||
        !nmo_json_stream_value_uint(writer, (uint64_t)nmo_chunk_get_id_count(chunk))) {
        return NMO_ERR_CANT_WRITE_FILE;
    }

    if (nmo_chunk_is_compressed(chunk)) {
        if (!nmo_json_stream_key(writer, "compressed") ||
            !nmo_json_stream_value_bool(writer, true)) {
            return NMO_ERR_CANT_WRITE_FILE;
        }
    }

    if (include_data && data && data_size > 0) {
        if (!nmo_json_stream_key(writer, "data_hex") ||
            !nmo_json_stream_value_hex_bytes(writer, data, data_size, false)) {
            return NMO_ERR_CANT_WRITE_FILE;
        }
    }

    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
    if (sub_count > 0) {
        if (!nmo_json_stream_key(writer, "sub_chunks") ||
            !nmo_json_stream_begin_array(writer)) {
            return NMO_ERR_CANT_WRITE_FILE;
        }

        for (uint32_t i = 0; i < sub_count; ++i) {
            nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
            if (!sub) {
                continue;
            }
            if (json_write_chunk(writer, sub, include_data) != NMO_OK) {
                return NMO_ERR_CANT_WRITE_FILE;
            }
        }

        if (!nmo_json_stream_end_array(writer)) {
            return NMO_ERR_CANT_WRITE_FILE;
        }
    }

    if (!nmo_json_stream_end_object(writer)) {
        return NMO_ERR_CANT_WRITE_FILE;
    }

    return NMO_OK;
}

nmo_status_t nmo_inspector_export_json(
    const nmo_chunk_t *chunk,
    FILE *stream,
    bool include_data
) {
    if (chunk == NULL || stream == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_json_stream_t writer;
    nmo_json_stream_init(&writer, stream, true);

    if (json_write_chunk(&writer, chunk, include_data) != NMO_OK) {
        return NMO_ERR_CANT_WRITE_FILE;
    }
    if (fputc('\n', stream) == EOF) {
        return NMO_ERR_CANT_WRITE_FILE;
    }
    return NMO_OK;
}
