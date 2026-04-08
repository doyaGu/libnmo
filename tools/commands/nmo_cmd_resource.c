/**
 * @file nmo_cmd_resource.c
 * @brief CLI resource command group implementation
 */

#include "nmo_cmd_resource.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

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
    static const nmo_opt_def_t opts[] = {
        {"--sort", NULL, NMO_OPT_STRING, "Sort by: index (default), size, name"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *sort_by = vals[0].present ? vals[0].val.str : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(c.session, &count);

    /* Build index array for sorting */
    uint32_t *indices = NULL;
    if (count > 0) {
        indices = (uint32_t *)malloc(count * sizeof(uint32_t));
        if (!indices) {
            fprintf(stderr, "Error: Out of memory\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        for (uint32_t i = 0; i < count; ++i) {
            indices[i] = i;
        }
    }

    /* Sort indices if requested */
    if (sort_by && count > 1) {
        if (strcmp(sort_by, "size") == 0) {
            /* Selection sort by size descending */
            for (uint32_t i = 0; i < count - 1; ++i) {
                uint32_t max_idx = i;
                for (uint32_t j = i + 1; j < count; ++j) {
                    if (files[indices[j]].size > files[indices[max_idx]].size) {
                        max_idx = j;
                    }
                }
                if (max_idx != i) {
                    uint32_t tmp = indices[i];
                    indices[i] = indices[max_idx];
                    indices[max_idx] = tmp;
                }
            }
        } else if (strcmp(sort_by, "name") == 0) {
            /* Selection sort by name ascending */
            for (uint32_t i = 0; i < count - 1; ++i) {
                uint32_t min_idx = i;
                for (uint32_t j = i + 1; j < count; ++j) {
                    const char *a = files[indices[j]].name ? files[indices[j]].name : "";
                    const char *b = files[indices[min_idx]].name ? files[indices[min_idx]].name : "";
                    if (strcmp(a, b) < 0) {
                        min_idx = j;
                    }
                }
                if (min_idx != i) {
                    uint32_t tmp = indices[i];
                    indices[i] = indices[min_idx];
                    indices[min_idx] = tmp;
                }
            }
        }
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "count", count);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);

        for (uint32_t k = 0; k < count; ++k) {
            uint32_t i = indices[k];
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            add_resource_json(doc, item, &files[i], i, false);
            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "resources", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "resource.list");
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

        for (uint32_t k = 0; k < count; ++k) {
            uint32_t i = indices[k];

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

        fprintf(c.out, "Resources: %u\n\n", count);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    free(indices);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * resource show
 * ============================================================================ */

int nmo_cmd_resource_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--index", "-i", NMO_OPT_STRING, "Resource index"},
        {"--name",  "-n", NMO_OPT_STRING, "Resource name"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *index_str = vals[0].present ? vals[0].val.str : NULL;
    const char *name_str = vals[1].present ? vals[1].val.str : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* If no --index/--name, check first positional (excluding file) as shorthand */
    if (!index_str && !name_str && r.pos_count >= 2) {
        const char *first_pos = r.pos_args[0];
        uint32_t idx_tmp;
        if (nmo_tool_parse_u32_dec(first_pos, &idx_tmp)) {
            index_str = first_pos;
        } else {
            name_str = first_pos;
        }
    }

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(c.session, &count);

    const nmo_included_file_t *res = NULL;
    uint32_t res_index = 0;
    if (index_str) {
        uint32_t idx;
        if (!nmo_tool_parse_u32_dec(index_str, &idx)) {
            fprintf(stderr, "Error: Invalid index '%s'\n", index_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (idx >= count) {
            fprintf(stderr, "Error: Index %u out of range (count=%u)\n", idx, count);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
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
            fprintf(stderr, "Error: Resource '%s' not found\n", name_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    } else {
        fprintf(stderr, "Error: No resource specified\n");
        fprintf(stderr, "Usage: nmo resource show [--index <n> | --name <name>] <file>\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Load objects for owner resolution (best-effort) */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    (void)nmo_session_get_objects(c.session, &objects, &object_count);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
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

                const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
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

        nmo_cmd_ctx_json_end(&c, doc, data, "resource.show");
    } else {
        nmo_cli_print_heading(c.out, "Resource", c.colorize);

        char idx_buf[32];
        snprintf(idx_buf, sizeof(idx_buf), "%u", res_index);
        nmo_cli_print_kv(c.out, "Index", idx_buf, 12, c.colorize);
        nmo_cli_print_kv(c.out, "Name", (res->name && res->name[0]) ? res->name : "-", 12, c.colorize);

        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%u", res->size);
        nmo_cli_print_kv(c.out, "Size", size_buf, 12, c.colorize);

        char attr_buf[32];
        snprintf(attr_buf, sizeof(attr_buf), "0x%08X", res->attributes);
        nmo_cli_print_kv(c.out, "Attributes", attr_buf, 12, c.colorize);

        fprintf(c.out, "\nOwners (%zu):\n", res->owner_ids.count);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)res->owner_ids.data;
        for (size_t i = 0; i < res->owner_ids.count; ++i) {
            nmo_object_id_t oid = ids[i];
            nmo_object_t *o = find_object_by_id(objects, object_count, oid);
            if (!o) {
                fprintf(c.out, "  - %u\n", oid);
                continue;
            }

            nmo_class_id_t class_id = nmo_object_get_class_id(o);
            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
            const char *name = nmo_object_get_name(o);

            fprintf(c.out, "  - %u  %s  %s\n",
                    oid,
                    class_name ? class_name : "-",
                    (name && name[0]) ? name : "-");
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * resource extract
 * ============================================================================ */

int nmo_cmd_resource_extract(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--out-dir",   "-d", NMO_OPT_STRING, "Output directory"},
        {"--index",     "-i", NMO_OPT_STRING, "Resource index"},
        {"--name",      "-n", NMO_OPT_STRING, "Resource name"},
        {"--overwrite", NULL, NMO_OPT_FLAG,   "Overwrite existing files"},
    };
    nmo_opt_val_t vals[4];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 4, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *out_dir = vals[0].present ? vals[0].val.str : NULL;
    if (!out_dir || !*out_dir) {
        fprintf(stderr, "Error: Missing --out-dir\n");
        fprintf(stderr, "Usage: nmo resource extract --out-dir <dir> [--index <n> | --name <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *index_str = vals[1].present ? vals[1].val.str : NULL;
    const char *name_str = vals[2].present ? vals[2].val.str : NULL;
    const bool overwrite = vals[3].val.flag;

    char dir_err[256];
    if (ensure_dir_exists(out_dir, dir_err, sizeof(dir_err)) != 0) {
        fprintf(stderr, "Error: %s\n", dir_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(c.session, &count);

    uint32_t start = 0;
    uint32_t end = count;
    if (index_str) {
        uint32_t idx;
        if (!nmo_tool_parse_u32_dec(index_str, &idx) || idx >= count) {
            fprintf(stderr, "Error: Invalid --index '%s'\n", index_str ? index_str : "");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
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
            fprintf(stderr, "Error: Resource '%s' not found\n", name_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    uint32_t extracted = 0;
    uint32_t skipped = 0;
    uint32_t errors = 0;

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *entries = NULL;
    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, data, "out_dir", out_dir);
        entries = yyjson_mut_arr(doc);
    } else {
        fprintf(c.out, "Extracting resources to: %s\n", out_dir);
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
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "out_of_memory");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [%u] skipped: out of memory\n", i);
            }
            continue;
        }

        if (is_meta_only || !has_payload) {
            skipped++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", is_meta_only ? "metadata_only" : "no_payload");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [%u] %s -> skipped (%s)\n", i, safe_name, is_meta_only ? "metadata_only" : "no_payload");
            }
            free(path);
            continue;
        }

        if (!overwrite && file_exists(path)) {
            skipped++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "exists");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [%u] %s -> skipped (exists; use --overwrite)\n", i, safe_name);
            }
            free(path);
            continue;
        }

        FILE *f = fopen(path, "wb");
        if (!f) {
            errors++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "open_failed");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [%u] %s -> failed to open (%s)\n", i, safe_name, strerror(errno));
            }
            free(path);
            continue;
        }

        size_t written = fwrite(res->data, 1, (size_t)res->size, f);
        fclose(f);

        if (written != (size_t)res->size) {
            errors++;
            if (c.is_json) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, e, "index", i);
                nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
                yyjson_mut_obj_add_str(doc, e, "path", path);
                yyjson_mut_obj_add_uint(doc, e, "size", res->size);
                yyjson_mut_obj_add_bool(doc, e, "extracted", false);
                yyjson_mut_obj_add_str(doc, e, "reason", "write_failed");
                yyjson_mut_arr_add_val(entries, e);
            } else {
                fprintf(c.out, "  [%u] %s -> write failed\n", i, safe_name);
            }
            free(path);
            continue;
        }

        extracted++;
        if (c.is_json) {
            yyjson_mut_val *e = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, e, "index", i);
            nmo_cli_json_add_str_safe(doc, e, "name", res->name ? res->name : "");
            yyjson_mut_obj_add_str(doc, e, "path", path);
            yyjson_mut_obj_add_uint(doc, e, "size", res->size);
            yyjson_mut_obj_add_bool(doc, e, "extracted", true);
            yyjson_mut_arr_add_val(entries, e);
        } else {
            fprintf(c.out, "  [%u] %s (%u bytes)\n", i, safe_name, res->size);
        }
        free(path);
    }

    int exit_code = (errors > 0) ? NMO_CLI_EXIT_IO_ERROR : NMO_CLI_EXIT_SUCCESS;
    if (c.is_json) {
        yyjson_mut_obj_add_uint(doc, data, "extracted", extracted);
        yyjson_mut_obj_add_uint(doc, data, "skipped", skipped);
        yyjson_mut_obj_add_uint(doc, data, "errors", errors);
        yyjson_mut_obj_add_val(doc, data, "entries", entries);

        nmo_cmd_ctx_json_end(&c, doc, data, "resource.extract");
    } else {
        fprintf(c.out, "\nExtracted: %u, Skipped: %u, Errors: %u\n", extracted, skipped, errors);
    }

    return nmo_cmd_ctx_done(&c, exit_code);
}

