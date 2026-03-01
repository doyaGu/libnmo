/**
 * @file nmo_cmd_resource.c
 * @brief CLI resource command group implementation
 */

#include "nmo_cmd_resource.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_session.h"
#include "object/nmo_object_repository.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define NMO_PATH_SEP '\\'
#else
#include <sys/stat.h>
#define NMO_PATH_SEP '/'
#endif

static const char *find_positional_arg_excluding_file(int argc, char **argv, const char *file_path) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            /* Skip option and its value if it looks like an option with arg */
            if ((strcmp(argv[i], "--index") == 0 || strcmp(argv[i], "-i") == 0 ||
                 strcmp(argv[i], "--name") == 0 || strcmp(argv[i], "-n") == 0 ||
                 strcmp(argv[i], "--out-dir") == 0 || strcmp(argv[i], "-d") == 0) &&
                i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (file_path && strcmp(argv[i], file_path) == 0) {
            continue;
        }
        return argv[i];
    }
    return NULL;
}

static int ensure_dir_exists(const char *dir_path, char *errbuf, size_t errbuf_size) {
    if (!dir_path || !*dir_path) {
        return -1;
    }

#ifdef _WIN32
    if (_mkdir(dir_path) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
#else
    if (mkdir(dir_path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
#endif

    if (errbuf && errbuf_size > 0) {
        snprintf(errbuf, errbuf_size, "Failed to create directory '%s' (%s)", dir_path, strerror(errno));
    }
    return -1;
}

static char *join_path(const char *dir, const char *file) {
    if (!dir || !file) {
        return NULL;
    }
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    size_t need_sep = (dir_len > 0 && dir[dir_len - 1] != (char)NMO_PATH_SEP) ? 1u : 0u;
    size_t total = dir_len + need_sep + file_len + 1u;
    char *out = (char *)malloc(total);
    if (!out) {
        return NULL;
    }
    memcpy(out, dir, dir_len);
    size_t pos = dir_len;
    if (need_sep) {
        out[pos++] = (char)NMO_PATH_SEP;
    }
    memcpy(out + pos, file, file_len);
    out[pos + file_len] = '\0';
    return out;
}

static bool file_exists(const char *path) {
    if (!path || !*path) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

static void add_resource_json(yyjson_mut_doc *doc, yyjson_mut_val *obj, const nmo_included_file_t *res,
                              uint32_t index, bool include_owners) {
    yyjson_mut_obj_add_uint(doc, obj, "index", index);
    if (res->name) {
        nmo_cli_json_add_str_safe(doc, obj, "name", res->name);
    }
    yyjson_mut_obj_add_uint(doc, obj, "size", res->size);
    yyjson_mut_obj_add_uint(doc, obj, "attributes", res->attributes);
    yyjson_mut_obj_add_bool(doc, obj, "borrowed", (res->attributes & NMO_INCLUDED_FILE_ATTR_BORROWED) != 0);
    yyjson_mut_obj_add_bool(doc, obj, "metadata_only",
                            (res->attributes & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) != 0);

    uint32_t owner_count = (uint32_t)res->owner_ids.count;
    yyjson_mut_obj_add_uint(doc, obj, "owner_count", owner_count);

    if (include_owners) {
        yyjson_mut_val *owners = yyjson_mut_arr(doc);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)res->owner_ids.data;
        for (uint32_t i = 0; i < owner_count; ++i) {
            yyjson_mut_arr_add_uint(doc, owners, ids[i]);
        }
        yyjson_mut_obj_add_val(doc, obj, "owner_ids", owners);
    }
}

static nmo_object_t *find_object_by_id(nmo_object_t **objects, size_t object_count, nmo_object_id_t id) {
    if (!objects) {
        return NULL;
    }
    for (size_t i = 0; i < object_count; ++i) {
        if (objects[i] && nmo_object_get_id(objects[i]) == id) {
            return objects[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * resource list
 * ============================================================================ */

int nmo_cmd_resource_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo resource list <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(session, &count);

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

        yyjson_mut_obj_add_uint(doc, data, "count", count);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);

        for (uint32_t i = 0; i < count; ++i) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            add_resource_json(doc, item, &files[i], i, false);
            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "resources", arr);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "resource.list", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"Index", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 60},
            {"Size", NMO_CLI_ALIGN_RIGHT, 10, 0},
            {"Owners", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"Flags", NMO_CLI_ALIGN_LEFT, 10, 0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (uint32_t i = 0; i < count; ++i) {
            char idx_buf[16];
            snprintf(idx_buf, sizeof(idx_buf), "%u", i);

            const char *name = (files[i].name && files[i].name[0]) ? files[i].name : "-";

            char size_buf[32];
            snprintf(size_buf, sizeof(size_buf), "%u", files[i].size);

            char owner_buf[16];
            snprintf(owner_buf, sizeof(owner_buf), "%zu", files[i].owner_ids.count);

            char flags_buf[64];
            flags_buf[0] = '\0';
            bool first = true;
            if (files[i].attributes & NMO_INCLUDED_FILE_ATTR_BORROWED) {
                snprintf(flags_buf + strlen(flags_buf), sizeof(flags_buf) - strlen(flags_buf), "%sBORROWED",
                         first ? "" : "|");
                first = false;
            }
            if (files[i].attributes & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) {
                snprintf(flags_buf + strlen(flags_buf), sizeof(flags_buf) - strlen(flags_buf), "%sMETA",
                         first ? "" : "|");
                first = false;
            }
            if (first) {
                snprintf(flags_buf, sizeof(flags_buf), "-");
            }

            const char *cells[] = {idx_buf, name, size_buf, owner_buf, flags_buf};
            nmo_cli_table_add_row(&table, cells, 5);
        }

        fprintf(out, "Resources: %u\n\n", count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * resource show
 * ============================================================================ */

int nmo_cmd_resource_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo resource show [--index <n> | --name <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *index_str = nmo_tool_find_opt_value(argc, argv, "--index", "-i");
    const char *name_str = nmo_tool_find_opt_value(argc, argv, "--name", "-n");

    const char *pos = find_positional_arg_excluding_file(argc, argv, file_path);
    if (!index_str && !name_str && pos) {
        uint32_t idx_tmp;
        if (nmo_tool_parse_u32_dec(pos, &idx_tmp)) {
            index_str = pos;
        } else {
            name_str = pos;
        }
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(session, &count);

    const nmo_included_file_t *res = NULL;
    uint32_t res_index = 0;
    if (index_str) {
        uint32_t idx;
        if (!nmo_tool_parse_u32_dec(index_str, &idx)) {
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Invalid index '%s'\n", index_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (idx >= count) {
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Index %u out of range (count=%u)\n", idx, count);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        res = &files[idx];
        res_index = idx;
    } else if (name_str) {
        for (uint32_t i = 0; i < count; ++i) {
            if (files[i].name && strcmp(files[i].name, name_str) == 0) {
                res = &files[i];
                res_index = i;
                break;
            }
        }
        if (!res) {
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Resource '%s' not found\n", name_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: No resource specified\n");
        fprintf(stderr, "Usage: nmo resource show [--index <n> | --name <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    /* Load objects for owner resolution (best-effort) */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    (void)nmo_session_get_objects(session, &objects, &object_count);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        add_resource_json(doc, obj, res, res_index, true);
        yyjson_mut_obj_add_val(doc, data, "resource", obj);

        yyjson_mut_val *owners = yyjson_mut_arr(doc);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)res->owner_ids.data;
        for (size_t i = 0; i < res->owner_ids.count; ++i) {
            yyjson_mut_val *owner = yyjson_mut_obj(doc);
            nmo_object_id_t oid = ids[i];
            yyjson_mut_obj_add_uint(doc, owner, "id", oid);

            nmo_object_t *o = find_object_by_id(objects, object_count, oid);
            if (o) {
                nmo_class_id_t class_id = nmo_object_get_class_id(o);
                yyjson_mut_obj_add_uint(doc, owner, "class_id", class_id);

                const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
                if (class_name) {
                    yyjson_mut_obj_add_str(doc, owner, "class_name", class_name);
                }

                const char *name = nmo_object_get_name(o);
                if (name && name[0]) {
                    nmo_cli_json_add_str_safe(doc, owner, "name", name);
                }
            }

            yyjson_mut_arr_add_val(owners, owner);
        }
        yyjson_mut_obj_add_val(doc, data, "owners", owners);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "resource.show", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Resource", colorize);

        char idx_buf[32];
        snprintf(idx_buf, sizeof(idx_buf), "%u", res_index);
        nmo_cli_print_kv(out, "Index", idx_buf, 12, colorize);
        nmo_cli_print_kv(out, "Name", (res->name && res->name[0]) ? res->name : "-", 12, colorize);

        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%u", res->size);
        nmo_cli_print_kv(out, "Size", size_buf, 12, colorize);

        char attr_buf[32];
        snprintf(attr_buf, sizeof(attr_buf), "0x%08X", res->attributes);
        nmo_cli_print_kv(out, "Attributes", attr_buf, 12, colorize);

        fprintf(out, "\nOwners (%zu):\n", res->owner_ids.count);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)res->owner_ids.data;
        for (size_t i = 0; i < res->owner_ids.count; ++i) {
            nmo_object_id_t oid = ids[i];
            nmo_object_t *o = find_object_by_id(objects, object_count, oid);
            if (!o) {
                fprintf(out, "  - %u\n", oid);
                continue;
            }

            nmo_class_id_t class_id = nmo_object_get_class_id(o);
            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            const char *name = nmo_object_get_name(o);

            fprintf(out, "  - %u  %s  %s\n",
                    oid,
                    class_name ? class_name : "-",
                    (name && name[0]) ? name : "-");
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * resource extract
 * ============================================================================ */

int nmo_cmd_resource_extract(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo resource extract --out-dir <dir> [--index <n> | --name <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *out_dir = nmo_tool_find_opt_value(argc, argv, "--out-dir", "-d");
    if (!out_dir || !*out_dir) {
        fprintf(stderr, "Error: Missing --out-dir\n");
        fprintf(stderr, "Usage: nmo resource extract --out-dir <dir> [--index <n> | --name <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *index_str = nmo_tool_find_opt_value(argc, argv, "--index", "-i");
    const char *name_str = nmo_tool_find_opt_value(argc, argv, "--name", "-n");
    const bool overwrite = nmo_tool_has_flag(argc, argv, "--overwrite", NULL);

    char dir_err[256];
    if (ensure_dir_exists(out_dir, dir_err, sizeof(dir_err)) != 0) {
        fprintf(stderr, "Error: %s\n", dir_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(session, &count);

    uint32_t start = 0;
    uint32_t end = count;
    if (index_str) {
        uint32_t idx;
        if (!nmo_tool_parse_u32_dec(index_str, &idx) || idx >= count) {
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Invalid --index '%s'\n", index_str ? index_str : "");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        start = idx;
        end = idx + 1;
    } else if (name_str) {
        bool found = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (files[i].name && strcmp(files[i].name, name_str) == 0) {
                start = i;
                end = i + 1;
                found = true;
                break;
            }
        }
        if (!found) {
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Resource '%s' not found\n", name_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    uint32_t extracted = 0;
    uint32_t skipped = 0;
    uint32_t errors = 0;

    bool want_json = (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *entries = NULL;
    if (want_json) {
        doc = nmo_cli_json_create_doc();
        data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, data, "out_dir", out_dir);
        entries = yyjson_mut_arr(doc);
    } else {
        fprintf(out, "Extracting resources to: %s\n", out_dir);
    }

    for (uint32_t i = start; i < end; ++i) {
        const nmo_included_file_t *res = &files[i];
        const bool is_meta_only = (res->attributes & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) != 0;
        const bool has_payload = (res->data != NULL && res->size > 0);

        char safe_name[260];
        nmo_tool_sanitize_filename(safe_name, sizeof(safe_name), res->name, i);
        char *path = join_path(out_dir, safe_name);
        if (!path) {
            errors++;
            if (want_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "out_of_memory");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(out, "  [%u] skipped: out of memory\n", i);
            }
            continue;
        }

        if (is_meta_only || !has_payload) {
            skipped++;
            if (want_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", is_meta_only ? "metadata_only" : "no_payload");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(out, "  [%u] %s -> skipped (%s)\n", i, safe_name, is_meta_only ? "metadata_only" : "no_payload");
            }
            free(path);
            continue;
        }

        if (!overwrite && file_exists(path)) {
            skipped++;
            if (want_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "exists");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(out, "  [%u] %s -> skipped (exists; use --overwrite)\n", i, safe_name);
            }
            free(path);
            continue;
        }

        FILE *f = fopen(path, "wb");
        if (!f) {
            errors++;
            if (want_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "open_failed");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(out, "  [%u] %s -> failed to open (%s)\n", i, safe_name, strerror(errno));
            }
            free(path);
            continue;
        }

        size_t written = fwrite(res->data, 1, (size_t)res->size, f);
        fclose(f);

        if (written != (size_t)res->size) {
            errors++;
            if (want_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "write_failed");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(out, "  [%u] %s -> write failed\n", i, safe_name);
            }
            free(path);
            continue;
        }

        extracted++;
        if (want_json) {
            yyjson_mut_val *e = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, e, "index", i);
            nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
            yyjson_mut_obj_add_str(doc, e, "path", path);
            yyjson_mut_obj_add_uint(doc, e, "size", res->size);
            yyjson_mut_obj_add_bool(doc, e, "extracted", true);
            yyjson_mut_arr_add_val(entries, e);
        } else {
            fprintf(out, "  [%u] %s (%u bytes)\n", i, safe_name, res->size);
        }
        free(path);
    }

    int exit_code = (errors > 0) ? NMO_CLI_EXIT_IO_ERROR : NMO_CLI_EXIT_SUCCESS;
    if (want_json) {
        yyjson_mut_obj_add_uint(doc, data, "extracted", extracted);
        yyjson_mut_obj_add_uint(doc, data, "skipped", skipped);
        yyjson_mut_obj_add_uint(doc, data, "errors", errors);
        yyjson_mut_obj_add_val(doc, data, "entries", entries);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "resource.extract", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        fprintf(out, "\nExtracted: %u, Skipped: %u, Errors: %u\n", extracted, skipped, errors);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return exit_code;
}
