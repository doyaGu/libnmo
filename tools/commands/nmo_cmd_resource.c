/**
 * @file nmo_cmd_resource.c
 * @brief CLI resource command group implementation
 */

#include "nmo_cmd_resource.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "session/nmo_session.h"
#include "app/nmo_save.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_error.h"
#include "format/nmo_stb_adapter.h"
#include "object/nmo_class_ids.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>

#ifdef _WIN32
#include <direct.h>
#define NMO_PATH_SEP '\\'
#else
#include <sys/stat.h>
#define NMO_PATH_SEP '/'
#endif

static int nmo_cmd_resource_extract_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

int nmo_cmd_resource_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: resource list|show|extract|info ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        uint32_t count = 0;
        nmo_included_file_t *files = nmo_session_get_included_files(ctx->session, &count);
        fprintf(ctx->out, "Resources: %u\n", count);
        for (uint32_t i = 0; i < count; i++) {
            fprintf(ctx->out, "  [%u] %s (%u bytes)\n", i,
                    files[i].name ? files[i].name : "(unnamed)",
                    files[i].size);
        }
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (strcmp(argv[0], "info") == 0 || strcmp(argv[0], "show") == 0 ||
        strcmp(argv[0], "s") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--index", "-i", NMO_OPT_UINT, "Resource index"},
            {"--name",  "-n", NMO_OPT_STRING, "Resource name"},
        };
        enum { OPT_INDEX, OPT_NAME, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        uint32_t count = 0;
        nmo_included_file_t *files = nmo_session_get_included_files(ctx->session, &count);
        const nmo_included_file_t *selected = NULL;
        uint32_t selected_index = 0;
        if (vals[OPT_INDEX].present) {
            selected_index = vals[OPT_INDEX].val.u;
            if (selected_index < count) {
                selected = &files[selected_index];
            }
        } else if (vals[OPT_NAME].present) {
            for (uint32_t i = 0; i < count; i++) {
                if (files[i].name && strcmp(files[i].name, vals[OPT_NAME].val.str) == 0) {
                    selected = &files[i];
                    selected_index = i;
                    break;
                }
            }
        }
        if (!selected) {
            fprintf(stderr, "Error: Resource not found\n");
            return NMO_CLI_EXIT_NOT_FOUND;
        }
        fprintf(ctx->out, "Resource #%u\n", selected_index);
        fprintf(ctx->out, "Name: %s\n", selected->name ? selected->name : "(unnamed)");
        fprintf(ctx->out, "Size: %u bytes\n", selected->size);
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (strcmp(argv[0], "extract") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_resource_extract_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported resource read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
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

static int ascii_lower(int ch) {
    return (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
}

static bool str_ends_with_ci(const char *text, const char *suffix) {
    if (text == NULL || suffix == NULL) {
        return false;
    }
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return false;
    }
    const char *tail = text + text_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (ascii_lower((unsigned char)tail[i]) != ascii_lower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

static bool resource_name_is_image_like(const char *name) {
    return str_ends_with_ci(name, ".bmp") ||
           str_ends_with_ci(name, ".png") ||
           str_ends_with_ci(name, ".jpg") ||
           str_ends_with_ci(name, ".jpeg") ||
           str_ends_with_ci(name, ".tga") ||
           str_ends_with_ci(name, ".pcx") ||
           str_ends_with_ci(name, ".dds");
}

static bool resource_has_texture_owner(const nmo_cmd_ctx_t *c, const nmo_included_file_t *res) {
    if (c == NULL || res == NULL || nmo_arena_array_is_empty(&res->owner_ids)) {
        return false;
    }

    const size_t owner_count = nmo_arena_array_size(&res->owner_ids);
    for (size_t i = 0; i < owner_count; ++i) {
        const nmo_object_id_t *owner_id =
            (const nmo_object_id_t *)nmo_arena_array_get(&res->owner_ids, i);
        if (owner_id == NULL || *owner_id == 0) {
            continue;
        }
        nmo_object_t *owner = nmo_core_find_by_id(c, *owner_id);
        if (owner != NULL &&
            nmo_core_class_derives(c, nmo_object_get_class_id(owner), NMO_CID_TEXTURE)) {
            return true;
        }
    }
    return false;
}

static bool resource_name_matches_texture_object(const nmo_cmd_ctx_t *c, const char *name) {
    if (c == NULL || name == NULL || *name == '\0') {
        return false;
    }

    nmo_object_t *obj = NULL;
    if (nmo_core_find_by_name(c, name, &obj) != NMO_CLI_EXIT_SUCCESS || obj == NULL) {
        return false;
    }

    return nmo_core_class_derives(c, nmo_object_get_class_id(obj), NMO_CID_TEXTURE);
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

/* ============================================================================
 * resource list
 * ============================================================================ */

int nmo_cmd_resource_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--sort", "-s", NMO_OPT_STRING, "Sort by: index (default), size, name"},
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

            nmo_object_t *o = nmo_core_find_by_id(&c, oid);
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
            nmo_object_t *o = nmo_core_find_by_id(&c, oid);
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

typedef struct resource_extract_args {
    const char *out_dir;
    const char *index_str;
    const char *name_str;
    bool overwrite;
} resource_extract_args_t;

static int resource_extract_parse(int argc, char **argv,
                                  bool expect_file_operand,
                                  resource_extract_args_t *args,
                                  const char *usage) {
    memset(args, 0, sizeof(*args));

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

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->out_dir = vals[0].present ? vals[0].val.str : NULL;
    args->index_str = vals[1].present ? vals[1].val.str : NULL;
    args->name_str = vals[2].present ? vals[2].val.str : NULL;
    args->overwrite = vals[3].present && vals[3].val.flag;

    if (!args->out_dir || !*args->out_dir) {
        fprintf(stderr, "Error: Missing --out-dir\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int resource_extract_run(nmo_cmd_ctx_t *ctx,
                                const resource_extract_args_t *args,
                                bool close_ctx) {
    nmo_cmd_ctx_t c = *ctx;

    char dir_err[256];
    if (ensure_dir_exists(args->out_dir, dir_err, sizeof(dir_err)) != 0) {
        fprintf(stderr, "Error: %s\n", dir_err);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR)
                         : NMO_CLI_EXIT_IO_ERROR;
    }

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(c.session, &count);

    uint32_t start = 0;
    uint32_t end = count;
    if (args->index_str) {
        uint32_t idx;
        if (!nmo_tool_parse_u32_dec(args->index_str, &idx) || idx >= count) {
            fprintf(stderr, "Error: Invalid --index '%s'\n",
                    args->index_str ? args->index_str : "");
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                             : NMO_CLI_EXIT_ARG_ERROR;
        }
        start = idx;
        end = idx + 1;
    } else if (args->name_str) {
        bool found = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (files[i].name && strcmp(files[i].name, args->name_str) == 0) {
                start = i;
                end = i + 1;
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Error: Resource '%s' not found\n", args->name_str);
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                             : NMO_CLI_EXIT_ARG_ERROR;
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
        yyjson_mut_obj_add_str(doc, data, "out_dir", args->out_dir);
        entries = yyjson_mut_arr(doc);
    } else {
        fprintf(c.out, "Extracting resources to: %s\n", args->out_dir);
    }

    for (uint32_t i = start; i < end; ++i) {
        const nmo_included_file_t *res = &files[i];
        const bool is_meta_only = (res->attributes & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) != 0;
        const bool has_payload = (res->data != NULL && res->size > 0);

        char safe_name[260];
        nmo_tool_sanitize_filename(safe_name, sizeof(safe_name), res->name, i);
        char *path = join_path(args->out_dir, safe_name);
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

        if (!args->overwrite && file_exists(path)) {
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

    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_resource_extract(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    resource_extract_args_t args;
    const char *usage =
        "nmo resource extract --out-dir <dir> [--index <n> | --name <name>] <file>";
    int rc = resource_extract_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return resource_extract_run(&c, &args, true);
}

static int nmo_cmd_resource_extract_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    resource_extract_args_t args;
    const char *usage =
        "resource extract --out-dir <dir> [--index <n> | --name <name>]";
    int rc = resource_extract_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    return resource_extract_run(ctx, &args, false);
}

/* ============================================================================
 * Helpers for write commands
 * ============================================================================ */

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *last = NULL;
    if (slash && bslash) {
        last = (slash > bslash) ? slash : bslash;
    } else if (slash) {
        last = slash;
    } else if (bslash) {
        last = bslash;
    }
    return last ? last + 1 : path;
}

static int read_file_to_memory(const char *path, uint8_t **out_data, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Cannot seek in '%s': %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "Error: Cannot get size of '%s': %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Cannot rewind '%s': %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (!data) {
        fprintf(stderr, "Error: Out of memory reading '%s'\n", path);
        fclose(f);
        return -1;
    }
    if (sz > 0) {
        size_t rd = fread(data, 1, (size_t)sz, f);
        if (rd != (size_t)sz) {
            fprintf(stderr, "Error: Short read on '%s'\n", path);
            free(data);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    *out_data = data;
    *out_size = (uint32_t)sz;
    return 0;
}

/* ============================================================================
 * resource import
 * ============================================================================ */

typedef struct resource_import_args {
    const char *res_name;
    const uint8_t *file_data;
    uint32_t file_size;
    nmo_object_id_t owner_ids[64];
    uint32_t owner_count;
    uint32_t new_index;
} resource_import_args_t;

static int resource_import_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    resource_import_args_t *args = (resource_import_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_included_file_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.owner_ids = args->owner_ids;
    meta.owner_count = args->owner_count;

    int add_rc = nmo_session_add_included_file_ex(
        c->session,
        args->res_name,
        args->file_data,
        args->file_size,
        &meta);
    if (add_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add resource: %s\n", nmo_error_string(add_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    uint32_t count = 0;
    (void)nmo_session_get_included_files(c->session, &count);
    args->new_index = count > 0 ? count - 1 : 0;
    return NMO_CLI_EXIT_SUCCESS;
}

static int resource_import_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    resource_import_args_t *args = (resource_import_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "index", args->new_index);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_str_safe(doc, data, "name", args->res_name);
        yyjson_mut_obj_add_uint(doc, data, "size", args->file_size);
        yyjson_mut_obj_add_uint(doc, data, "owner_count", args->owner_count);
        if (!dry_run && output_path) {
            yyjson_mut_obj_add_str(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, "resource.import");
    } else {
        fprintf(c->out, "%sImported resource:\n", dry_run ? "Dry run: " : "");
        char idx_buf[32];
        snprintf(idx_buf, sizeof(idx_buf), "%u", args->new_index);
        nmo_cli_print_kv(c->out, "Index", idx_buf, 12, c->colorize);
        nmo_cli_print_kv(c->out, "Name", args->res_name, 12, c->colorize);
        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%u", args->file_size);
        nmo_cli_print_kv(c->out, "Size", size_buf, 12, c->colorize);
        char own_buf[32];
        snprintf(own_buf, sizeof(own_buf), "%u", args->owner_count);
        nmo_cli_print_kv(c->out, "Owners", own_buf, 12, c->colorize);
        if (dry_run) {
            fprintf(c->out, "\n(dry run, no changes saved)\n");
        } else {
            fprintf(c->out, "\nSaved to: %s\n", output_path);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_resource_import(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file (required)"},
        {"--name",   "-n", NMO_OPT_STRING, "Resource name (default: basename of disk file)"},
        {"--owner",  NULL, NMO_OPT_STRING, "Owner object IDs (comma-separated)"},
        {"--dry-run", NULL, NMO_OPT_FLAG,  "Preview only, do not save"},
    };
    nmo_opt_val_t vals[4];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 4, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[0].present ? vals[0].val.str : NULL;
    bool dry_run = vals[3].present && vals[3].val.flag;
    if (!dry_run && (!output_path || !*output_path)) {
        fprintf(stderr, "Error: Missing --output\n");
        fprintf(stderr, "Usage: nmo resource import [-o <output>] [--dry-run] [--name <name>] [--owner <ids>] <disk-file> <nmo-file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *name_str = vals[1].present ? vals[1].val.str : NULL;
    const char *owner_str = vals[2].present ? vals[2].val.str : NULL;

    if (r.pos_count < 2) {
        fprintf(stderr, "Error: Expected <disk-file> <nmo-file>\n");
        fprintf(stderr, "Usage: nmo resource import -o <output> [--name <name>] [--owner <ids>] <disk-file> <nmo-file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *disk_file = r.pos_args[0];
    const char *nmo_file = r.pos_args[r.pos_count - 1];

    /* Read disk file */
    uint8_t *file_data = NULL;
    uint32_t file_size = 0;
    if (read_file_to_memory(disk_file, &file_data, &file_size) != 0) {
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Resource name */
    const char *res_name = name_str ? name_str : path_basename(disk_file);

    resource_import_args_t args = {
        .res_name = res_name,
        .file_data = file_data,
        .file_size = file_size,
    };

    /* Parse owner IDs */
    if (owner_str && *owner_str) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", owner_str);
        char *tok = strtok(buf, ",");
        while (tok && args.owner_count < 64) {
            uint32_t id;
            if (!nmo_tool_parse_u32_dec(tok, &id)) {
                fprintf(stderr, "Error: Invalid owner ID '%s'\n", tok);
                free(file_data);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args.owner_ids[args.owner_count++] = id;
            tok = strtok(NULL, ",");
        }
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "resource.import",
        .output_required_unless_dry_run = true,
    };
    int rc = nmo_cli_run_write_command(
        nmo_file,
        output_path,
        dry_run,
        global,
        &spec,
        resource_import_mutate,
        resource_import_report,
        &args);
    free(file_data);
    return rc;
}

/* ============================================================================
 * resource replace
 * ============================================================================ */

typedef struct resource_replace_args {
    const char *index_str;
    const char *name_str;
    const uint8_t *file_data;
    uint32_t file_size;
    uint32_t res_index;
    char res_name[256];
    uint32_t old_size;
    bool warn_texture_bitmap;
} resource_replace_args_t;

static int resource_replace_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    resource_replace_args_t *args = (resource_replace_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(c->session, &count);

    uint32_t res_index = 0;
    const nmo_included_file_t *res = NULL;

    if (args->index_str != NULL) {
        uint32_t idx;
        if (!nmo_tool_parse_u32_dec(args->index_str, &idx)) {
            fprintf(stderr, "Error: Invalid index '%s'\n", args->index_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (idx >= count) {
            fprintf(stderr, "Error: Index %u out of range (count=%u)\n", idx, count);
            return NMO_CLI_EXIT_NOT_FOUND;
        }
        res_index = idx;
        res = &files[idx];
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            if (files[i].name && strcmp(files[i].name, args->name_str) == 0) {
                res_index = i;
                res = &files[i];
                break;
            }
        }
        if (res == NULL) {
            fprintf(stderr, "Error: Resource '%s' not found\n", args->name_str);
            return NMO_CLI_EXIT_NOT_FOUND;
        }
    }

    args->res_index = res_index;
    args->old_size = res->size;
    snprintf(args->res_name, sizeof(args->res_name), "%s", res->name ? res->name : "");
    args->warn_texture_bitmap =
        resource_name_is_image_like(args->res_name) &&
        (resource_has_texture_owner(c, res) ||
         resource_name_matches_texture_object(c, args->res_name));

    int rep_rc = nmo_session_replace_included_file(
        c->session,
        res_index,
        args->file_data,
        args->file_size);
    if (rep_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to replace resource: %s\n", nmo_error_string(rep_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int resource_replace_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    resource_replace_args_t *args = (resource_replace_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "index", args->res_index);
        nmo_cli_json_add_str_safe(doc, data, "name", args->res_name);
        yyjson_mut_obj_add_uint(doc, data, "old_size", args->old_size);
        yyjson_mut_obj_add_uint(doc, data, "new_size", args->file_size);
        if (args->warn_texture_bitmap) {
            yyjson_mut_obj_add_str(
                doc,
                data,
                "warning",
                "resource replace updated the included resource payload, not CKTexture bitmap data; use texture replace for texture objects");
        }
        if (!dry_run && output_path) {
            yyjson_mut_obj_add_str(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, "resource.replace");
    } else {
        fprintf(c->out, "%sReplaced resource:\n", dry_run ? "Dry run: " : "");
        char idx_buf[32];
        snprintf(idx_buf, sizeof(idx_buf), "%u", args->res_index);
        nmo_cli_print_kv(c->out, "Index", idx_buf, 12, c->colorize);
        nmo_cli_print_kv(c->out, "Name", args->res_name[0] ? args->res_name : "-", 12, c->colorize);
        char old_buf[32];
        snprintf(old_buf, sizeof(old_buf), "%u -> %u", args->old_size, args->file_size);
        nmo_cli_print_kv(c->out, "Size", old_buf, 12, c->colorize);
        if (args->warn_texture_bitmap) {
            fprintf(c->out,
                    "\nWarning: resource replace updated the included resource payload, "
                    "not CKTexture bitmap data. Use `texture replace` for texture objects.\n");
        }
        if (dry_run) {
            fprintf(c->out, "\n(dry run, no changes saved)\n");
        } else {
            fprintf(c->out, "\nSaved to: %s\n", output_path);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_resource_replace(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file (required)"},
        {"--index",  "-i", NMO_OPT_STRING, "Resource index"},
        {"--name",   "-n", NMO_OPT_STRING, "Resource name"},
        {"--dry-run", NULL, NMO_OPT_FLAG,  "Preview only, do not save"},
    };
    nmo_opt_val_t vals[4];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 4, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[0].present ? vals[0].val.str : NULL;
    bool dry_run = vals[3].present && vals[3].val.flag;
    if (!dry_run && (!output_path || !*output_path)) {
        fprintf(stderr, "Error: Missing --output\n");
        fprintf(stderr, "Usage: nmo resource replace [-o <output>] [--dry-run] [--index <n> | --name <name>] <disk-file> <nmo-file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *index_str = vals[1].present ? vals[1].val.str : NULL;
    const char *name_str = vals[2].present ? vals[2].val.str : NULL;

    if (!index_str && !name_str) {
        fprintf(stderr, "Error: Specify --index or --name to identify the resource\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (r.pos_count < 2) {
        fprintf(stderr, "Error: Expected <disk-file> <nmo-file>\n");
        fprintf(stderr, "Usage: nmo resource replace -o <output> [--dry-run] [--index <n> | --name <name>] <disk-file> <nmo-file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *disk_file = r.pos_args[0];
    const char *nmo_file = r.pos_args[r.pos_count - 1];

    /* Read disk file */
    uint8_t *file_data = NULL;
    uint32_t file_size = 0;
    if (read_file_to_memory(disk_file, &file_data, &file_size) != 0) {
        return NMO_CLI_EXIT_IO_ERROR;
    }

    resource_replace_args_t args = {
        .index_str = index_str,
        .name_str = name_str,
        .file_data = file_data,
        .file_size = file_size,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "resource.replace",
        .output_required_unless_dry_run = true,
    };
    int rc = nmo_cli_run_write_command(
        nmo_file,
        output_path,
        dry_run,
        global,
        &spec,
        resource_replace_mutate,
        resource_replace_report,
        &args);
    free(file_data);
    return rc;
}

/* ============================================================================
 * resource remove
 * ============================================================================ */

typedef struct resource_remove_args {
    const char *index_str;
    const char *name_str;
    uint32_t res_index;
    char res_name[256];
    uint32_t res_size;
    uint32_t res_owner_count;
    uint32_t before_count;
    uint32_t after_count;
} resource_remove_args_t;

static int resource_remove_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    resource_remove_args_t *args = (resource_remove_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(c->session, &count);

    uint32_t res_index = 0;
    const nmo_included_file_t *res = NULL;

    if (args->index_str != NULL) {
        uint32_t idx;
        if (!nmo_tool_parse_u32_dec(args->index_str, &idx)) {
            fprintf(stderr, "Error: Invalid index '%s'\n", args->index_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (idx >= count) {
            fprintf(stderr, "Error: Index %u out of range (count=%u)\n", idx, count);
            return NMO_CLI_EXIT_NOT_FOUND;
        }
        res_index = idx;
        res = &files[idx];
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            if (files[i].name && strcmp(files[i].name, args->name_str) == 0) {
                res_index = i;
                res = &files[i];
                break;
            }
        }
        if (res == NULL) {
            fprintf(stderr, "Error: Resource '%s' not found\n", args->name_str);
            return NMO_CLI_EXIT_NOT_FOUND;
        }
    }

    args->res_index = res_index;
    snprintf(args->res_name, sizeof(args->res_name), "%s", res->name ? res->name : "");
    args->res_size = res->size;
    args->res_owner_count = (uint32_t)res->owner_ids.count;
    args->before_count = count;
    args->after_count = count;

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    int rm_rc = nmo_session_remove_included_file(c->session, res_index);
    if (rm_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove resource: %s\n", nmo_error_string(rm_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    uint32_t new_count = 0;
    (void)nmo_session_get_included_files(c->session, &new_count);
    args->after_count = new_count;
    return NMO_CLI_EXIT_SUCCESS;
}

static int resource_remove_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    resource_remove_args_t *args = (resource_remove_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (dry_run) {
        if (c->is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_bool(doc, data, "dry_run", true);
            yyjson_mut_obj_add_uint(doc, data, "index", args->res_index);
            nmo_cli_json_add_str_safe(doc, data, "name", args->res_name);
            yyjson_mut_obj_add_uint(doc, data, "size", args->res_size);
            yyjson_mut_obj_add_uint(doc, data, "owner_count", args->res_owner_count);
            yyjson_mut_obj_add_uint(doc, data, "total_count", args->before_count);
            nmo_cmd_ctx_json_end(c, doc, data, "resource.remove");
        } else {
            fprintf(c->out, "Would remove resource:\n");
            char idx_buf[32];
            snprintf(idx_buf, sizeof(idx_buf), "%u", args->res_index);
            nmo_cli_print_kv(c->out, "Index", idx_buf, 12, c->colorize);
            nmo_cli_print_kv(c->out, "Name", args->res_name[0] ? args->res_name : "-", 12, c->colorize);
            char size_buf[32];
            snprintf(size_buf, sizeof(size_buf), "%u", args->res_size);
            nmo_cli_print_kv(c->out, "Size", size_buf, 12, c->colorize);
            char own_buf[32];
            snprintf(own_buf, sizeof(own_buf), "%u", args->res_owner_count);
            nmo_cli_print_kv(c->out, "Owners", own_buf, 12, c->colorize);
            fprintf(c->out, "\n(dry run, no changes made)\n");
        }
        return NMO_CLI_EXIT_SUCCESS;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "index", args->res_index);
        nmo_cli_json_add_str_safe(doc, data, "name", args->res_name);
        yyjson_mut_obj_add_uint(doc, data, "size", args->res_size);
        yyjson_mut_obj_add_uint(doc, data, "before_count", args->before_count);
        yyjson_mut_obj_add_uint(doc, data, "after_count", args->after_count);
        yyjson_mut_obj_add_str(doc, data, "output", output_path);
        nmo_cmd_ctx_json_end(c, doc, data, "resource.remove");
    } else {
        fprintf(c->out, "Removed resource:\n");
        char idx_buf[32];
        snprintf(idx_buf, sizeof(idx_buf), "%u", args->res_index);
        nmo_cli_print_kv(c->out, "Index", idx_buf, 12, c->colorize);
        nmo_cli_print_kv(c->out, "Name", args->res_name[0] ? args->res_name : "-", 12, c->colorize);
        char cnt_buf[32];
        snprintf(cnt_buf, sizeof(cnt_buf), "%u -> %u", args->before_count, args->after_count);
        nmo_cli_print_kv(c->out, "Count", cnt_buf, 12, c->colorize);
        fprintf(c->out, "\nSaved to: %s\n", output_path);
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_resource_remove(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--index",   "-i", NMO_OPT_STRING, "Resource index"},
        {"--name",    "-n", NMO_OPT_STRING, "Resource name"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview only, do not save"},
    };
    nmo_opt_val_t vals[4];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 4, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[0].present ? vals[0].val.str : NULL;
    const char *index_str = vals[1].present ? vals[1].val.str : NULL;
    const char *name_str = vals[2].present ? vals[2].val.str : NULL;
    const bool dry_run = vals[3].val.flag;

    if (!dry_run && output_path != NULL && !*output_path) {
        fprintf(stderr, "Error: Missing --output (required unless --dry-run)\n");
        fprintf(stderr, "Usage: nmo resource remove -o <output> [--index <n> | --name <name>] [--dry-run] <nmo-file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!index_str && !name_str) {
        fprintf(stderr, "Error: Specify --index or --name to identify the resource\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (r.pos_count < 1) {
        fprintf(stderr, "Error: Expected <nmo-file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *nmo_file = r.pos_args[r.pos_count - 1];

    resource_remove_args_t args = {
        .index_str = index_str,
        .name_str = name_str,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "resource.remove",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        nmo_file,
        output_path,
        dry_run,
        global,
        &spec,
        resource_remove_mutate,
        resource_remove_report,
        &args);
}

/* ============================================================================
 * resource info
 * ============================================================================ */

static const char *detect_format(const uint8_t *data, uint32_t size) {
    if (size >= 5 && data[0] == 'C' && data[1] == 'K') {
        return "CK";
    }
    if (size >= 8 &&
        data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
        data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
        return "PNG";
    }
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        return "BMP";
    }
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "JPEG";
    }
    if (size >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ') {
        return "DDS";
    }
    if (size >= 12 &&
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
        return "WAV";
    }
    return "unknown";
}

int nmo_cmd_resource_info(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--index", "-i", NMO_OPT_STRING, "Resource index (in NMO)"},
        {"--name",  "-n", NMO_OPT_STRING, "Resource name (in NMO)"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *index_str = vals[0].present ? vals[0].val.str : NULL;
    const char *name_str = vals[1].present ? vals[1].val.str : NULL;

    bool from_nmo = (index_str != NULL || name_str != NULL);

    const uint8_t *payload = NULL;
    uint32_t payload_size = 0;
    uint8_t *file_data = NULL;
    const char *res_name = NULL;
    nmo_cmd_ctx_t c;
    bool ctx_opened = false;

    if (from_nmo) {
        /* Open NMO, find resource */
        int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
        if (rc) return rc;
        ctx_opened = true;

        uint32_t count = 0;
        nmo_included_file_t *files = nmo_session_get_included_files(c.session, &count);
        const nmo_included_file_t *res = NULL;

        if (index_str) {
            uint32_t idx;
            if (!nmo_tool_parse_u32_dec(index_str, &idx)) {
                fprintf(stderr, "Error: Invalid index '%s'\n", index_str);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
            }
            if (idx >= count) {
                fprintf(stderr, "Error: Index %u out of range (count=%u)\n", idx, count);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
            }
            res = &files[idx];
        } else {
            for (uint32_t i = 0; i < count; ++i) {
                if (files[i].name && strcmp(files[i].name, name_str) == 0) {
                    res = &files[i];
                    break;
                }
            }
            if (!res) {
                fprintf(stderr, "Error: Resource '%s' not found\n", name_str);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
            }
        }

        payload = (const uint8_t *)res->data;
        payload_size = res->size;
        res_name = res->name;
    } else {
        /* Read external file */
        if (r.pos_count < 1) {
            fprintf(stderr, "Error: Expected <file>\n");
            fprintf(stderr, "Usage: nmo resource info [--index <n> | --name <name>] <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        const char *disk_path = r.pos_args[0];
        uint32_t fsize = 0;
        if (read_file_to_memory(disk_path, &file_data, &fsize) != 0) {
            return NMO_CLI_EXIT_IO_ERROR;
        }
        payload = file_data;
        payload_size = fsize;
        res_name = path_basename(disk_path);

        /* Open a minimal context for output formatting */
        int rc = nmo_cmd_ctx_init_no_file(&c, global);
        if (rc) {
            free(file_data);
            return rc;
        }
        ctx_opened = true;
    }

    /* Detect format */
    const char *format = "unknown";
    char ck_sig[8];
    ck_sig[0] = '\0';

    if (payload && payload_size > 0) {
        format = detect_format(payload, payload_size);
        if (strcmp(format, "CK") == 0 && payload_size >= 5) {
            snprintf(ck_sig, sizeof(ck_sig), "CK%.3s", (const char *)payload + 2);
        }
    }

    /* Try to get image dimensions for supported formats */
    int img_width = 0;
    int img_height = 0;
    int img_channels = 0;
    bool has_dims = false;

    if (payload && payload_size > 0 &&
        (strcmp(format, "PNG") == 0 || strcmp(format, "BMP") == 0 ||
         strcmp(format, "JPEG") == 0)) {
        nmo_arena_t *tmp_arena = nmo_arena_create(NULL, 0);
        if (tmp_arena) {
            uint8_t *decoded = nmo_stbi_load_from_memory(
                tmp_arena, payload, (int)payload_size,
                &img_width, &img_height, &img_channels, 0);
            if (decoded) {
                has_dims = true;
            }
            nmo_arena_destroy(tmp_arena);
        }
    }

    /* Output */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        if (res_name) {
            nmo_cli_json_add_str_safe(doc, data, "name", res_name);
        }
        yyjson_mut_obj_add_uint(doc, data, "size", payload_size);
        yyjson_mut_obj_add_str(doc, data, "format", format);
        if (ck_sig[0]) {
            yyjson_mut_obj_add_str(doc, data, "ck_signature", ck_sig);
        }
        if (has_dims) {
            yyjson_mut_obj_add_int(doc, data, "width", img_width);
            yyjson_mut_obj_add_int(doc, data, "height", img_height);
            yyjson_mut_obj_add_int(doc, data, "channels", img_channels);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "resource.info");
    } else {
        nmo_cli_print_heading(c.out, "Resource Info", c.colorize);

        if (res_name) {
            nmo_cli_print_kv(c.out, "Name", res_name, 12, c.colorize);
        }
        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%u", payload_size);
        nmo_cli_print_kv(c.out, "Size", size_buf, 12, c.colorize);
        nmo_cli_print_kv(c.out, "Format", format, 12, c.colorize);
        if (ck_sig[0]) {
            nmo_cli_print_kv(c.out, "CK Signature", ck_sig, 12, c.colorize);
        }
        if (has_dims) {
            char dim_buf[64];
            snprintf(dim_buf, sizeof(dim_buf), "%dx%d (%d channels)", img_width, img_height, img_channels);
            nmo_cli_print_kv(c.out, "Dimensions", dim_buf, 12, c.colorize);
        }
    }

    free(file_data);
    if (ctx_opened) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

