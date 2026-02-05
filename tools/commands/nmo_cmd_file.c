/**
 * @file nmo_cmd_file.c
 * @brief CLI file command group implementation
 */

#include "nmo_cmd_file.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"

#include "nmo.h"
#include "app/nmo_stats.h"
#include "format/nmo_header.h"

#include <stdio.h>
#include <string.h>

/**
 * Find first non-option argument (the file path)
 */
static const char *find_file_arg(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            return argv[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * file info
 * ============================================================================ */

int nmo_cmd_file_info(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo file info <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get file info */
    nmo_file_info_t info = nmo_session_get_file_info(session);

    /* Output */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", file_path);
        yyjson_mut_obj_add_uint(doc, data, "object_count", info.object_count);
        yyjson_mut_obj_add_uint(doc, data, "manager_count", info.manager_count);
        yyjson_mut_obj_add_uint(doc, data, "ck_version", info.ck_version);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "file.info", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "File Info", colorize);
        nmo_cli_print_kv(out, "File", file_path, 14, colorize);

        char buf[64];
        snprintf(buf, sizeof(buf), "%u", info.object_count);
        nmo_cli_print_kv(out, "Objects", buf, 14, colorize);

        snprintf(buf, sizeof(buf), "%u", info.manager_count);
        nmo_cli_print_kv(out, "Managers", buf, 14, colorize);

        snprintf(buf, sizeof(buf), "0x%08X", info.ck_version);
        nmo_cli_print_kv(out, "CK Version", buf, 14, colorize);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * file header
 * ============================================================================ */

int nmo_cmd_file_header(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo file header <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get header - cast from opaque nmo_header_t to public nmo_file_header_t */
    const nmo_file_header_t *header = (const nmo_file_header_t *)nmo_session_get_header(session);
    if (!header) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get file header\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        char sig_buf[9];
        memcpy(sig_buf, header->signature, 8);
        sig_buf[8] = '\0';
        yyjson_mut_obj_add_str(doc, data, "signature", sig_buf);
        yyjson_mut_obj_add_uint(doc, data, "file_version", header->file_version);
        yyjson_mut_obj_add_uint(doc, data, "file_version2", header->file_version2);
        yyjson_mut_obj_add_uint(doc, data, "ck_version", header->ck_version);
        yyjson_mut_obj_add_uint(doc, data, "crc", header->crc);
        yyjson_mut_obj_add_uint(doc, data, "file_write_mode", header->file_write_mode);
        yyjson_mut_obj_add_uint(doc, data, "hdr1_pack_size", header->hdr1_pack_size);
        if (header->file_version >= 5) {
            yyjson_mut_obj_add_uint(doc, data, "data_pack_size", header->data_pack_size);
            yyjson_mut_obj_add_uint(doc, data, "data_unpack_size", header->data_unpack_size);
            yyjson_mut_obj_add_uint(doc, data, "object_count", header->object_count);
            yyjson_mut_obj_add_uint(doc, data, "manager_count", header->manager_count);
            yyjson_mut_obj_add_uint(doc, data, "max_id_saved", header->max_id_saved);
            yyjson_mut_obj_add_uint(doc, data, "product_version", header->product_version);
            yyjson_mut_obj_add_uint(doc, data, "product_build", header->product_build);
            yyjson_mut_obj_add_uint(doc, data, "hdr1_unpack_size", header->hdr1_unpack_size);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "file.header", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "File Header", colorize);

        char sig_buf[9];
        memcpy(sig_buf, header->signature, 8);
        sig_buf[8] = '\0';
        nmo_cli_print_kv(out, "Signature", sig_buf, 18, colorize);

        char buf[64];
        snprintf(buf, sizeof(buf), "%u (secondary %u)", header->file_version, header->file_version2);
        nmo_cli_print_kv(out, "File Version", buf, 18, colorize);

        snprintf(buf, sizeof(buf), "0x%08X", header->ck_version);
        nmo_cli_print_kv(out, "CK Version", buf, 18, colorize);

        snprintf(buf, sizeof(buf), "0x%08X", header->crc);
        nmo_cli_print_kv(out, "CRC", buf, 18, colorize);

        snprintf(buf, sizeof(buf), "0x%X", header->file_write_mode);
        nmo_cli_print_kv(out, "Write Mode", buf, 18, colorize);

        snprintf(buf, sizeof(buf), "%u bytes", header->hdr1_pack_size);
        nmo_cli_print_kv(out, "Header1 Packed", buf, 18, colorize);

        if (header->file_version >= 5) {
            snprintf(buf, sizeof(buf), "%u bytes", header->data_pack_size);
            nmo_cli_print_kv(out, "Data Packed", buf, 18, colorize);

            snprintf(buf, sizeof(buf), "%u bytes", header->data_unpack_size);
            nmo_cli_print_kv(out, "Data Unpacked", buf, 18, colorize);

            snprintf(buf, sizeof(buf), "%u", header->object_count);
            nmo_cli_print_kv(out, "Objects", buf, 18, colorize);

            snprintf(buf, sizeof(buf), "%u", header->manager_count);
            nmo_cli_print_kv(out, "Managers", buf, 18, colorize);

            snprintf(buf, sizeof(buf), "%u", header->max_id_saved);
            nmo_cli_print_kv(out, "Max ID Saved", buf, 18, colorize);

            snprintf(buf, sizeof(buf), "%u / %u", header->product_version, header->product_build);
            nmo_cli_print_kv(out, "Product Ver/Build", buf, 18, colorize);
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * file stats
 * ============================================================================ */

int nmo_cmd_file_stats(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo file stats <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Collect stats */
    nmo_file_stats_t stats;
    if (nmo_stats_collect(session, &stats) != 0) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to collect statistics\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Objects stats */
        yyjson_mut_val *obj_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, obj_stats, "total", stats.objects.total_count);
        yyjson_mut_obj_add_uint(doc, obj_stats, "unique_classes", (uint64_t)stats.objects.unique_classes);
        yyjson_mut_obj_add_val(doc, data, "objects", obj_stats);

        /* Chunks stats */
        yyjson_mut_val *chunk_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, chunk_stats, "total", stats.chunks.total_chunks);
        yyjson_mut_obj_add_uint(doc, chunk_stats, "compressed", stats.chunks.compressed_chunks);
        yyjson_mut_obj_add_uint(doc, chunk_stats, "max_size", stats.chunks.max_chunk_size);
        yyjson_mut_obj_add_val(doc, data, "chunks", chunk_stats);

        /* Memory stats */
        yyjson_mut_val *mem_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, mem_stats, "total_size", stats.memory.total_size);
        yyjson_mut_obj_add_uint(doc, mem_stats, "chunk_data_size", stats.memory.chunk_data_size);
        yyjson_mut_obj_add_val(doc, data, "memory", mem_stats);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "file.stats", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "File Statistics", colorize);
        fprintf(out, "\n");

        nmo_cli_print_heading(out, "Objects", colorize);
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", stats.objects.total_count);
        nmo_cli_print_kv(out, "Total", buf, 16, colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.objects.unique_classes);
        nmo_cli_print_kv(out, "Unique Classes", buf, 16, colorize);
        fprintf(out, "\n");

        nmo_cli_print_heading(out, "Chunks", colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.total_chunks);
        nmo_cli_print_kv(out, "Total", buf, 16, colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.compressed_chunks);
        nmo_cli_print_kv(out, "Compressed", buf, 16, colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.max_chunk_size);
        nmo_cli_print_kv(out, "Max Size", buf, 16, colorize);
        fprintf(out, "\n");

        nmo_cli_print_heading(out, "Memory", colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.total_size);
        nmo_cli_print_kv(out, "Total Size", buf, 16, colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.chunk_data_size);
        nmo_cli_print_kv(out, "Chunk Data", buf, 16, colorize);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * file plugins
 * ============================================================================ */

int nmo_cmd_file_plugins(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo file plugins <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get plugin diagnostics */
    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(session);
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_bool(doc, data, "extension_registry_available",
                                diag ? diag->extension_registry_available : false);
        yyjson_mut_obj_add_uint(doc, data, "missing_count", diag ? diag->missing_count : 0);
        yyjson_mut_obj_add_uint(doc, data, "outdated_count", diag ? diag->outdated_count : 0);
        yyjson_mut_obj_add_uint(doc, data, "entry_count", diag ? diag->entry_count : 0);

        /* Plugin entries */
        yyjson_mut_val *entries = yyjson_mut_arr(doc);
        if (diag && diag->entries) {
            for (size_t i = 0; i < diag->entry_count; ++i) {
                const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                yyjson_mut_val *entry = yyjson_mut_obj(doc);

                char guid_buf[64];
                nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                yyjson_mut_obj_add_str(doc, entry, "guid", guid_buf);
                if (e->resolved_name) {
                    yyjson_mut_obj_add_str(doc, entry, "name", e->resolved_name);
                }
                yyjson_mut_obj_add_uint(doc, entry, "status_flags", e->status_flags);

                yyjson_mut_arr_add_val(entries, entry);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "entries", entries);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "file.plugins", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Plugin Dependencies", colorize);

        if (!diag) {
            fprintf(out, "Plugin diagnostics unavailable\n");
        } else {
            char buf[64];
            nmo_cli_print_kv(out, "Registry Available",
                            diag->extension_registry_available ? "yes" : "no", 18, colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->missing_count);
            nmo_cli_print_kv(out, "Missing", buf, 18, colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->outdated_count);
            nmo_cli_print_kv(out, "Outdated", buf, 18, colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->entry_count);
            nmo_cli_print_kv(out, "Total Entries", buf, 18, colorize);

            if (diag->entries && diag->entry_count > 0) {
                fprintf(out, "\nEntries:\n");
                for (size_t i = 0; i < diag->entry_count; ++i) {
                    const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                    char guid_buf[64];
                    nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                    fprintf(out, "  %s", guid_buf);
                    if (e->resolved_name) {
                        fprintf(out, " (%s)", e->resolved_name);
                    }
                    if (e->status_flags) {
                        fprintf(out, " [flags=0x%X]", e->status_flags);
                    }
                    fprintf(out, "\n");
                }
            }
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}
