/**
 * @file nmo_cmd_behavior.c
 * @brief CLI behavior command group implementation
 */

#include "nmo_cmd_behavior.h"

#include "nmo_cmd_object.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"

#include "nmo.h"
#include "object/nmo_class_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_file_arg_last(int argc, char **argv) {
    const char *last_non_opt = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            last_non_opt = argv[i];
        }
    }
    return last_non_opt;
}

int nmo_cmd_behavior_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo behavior list <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    size_t behavior_count = 0;

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            if (!nmo_cli_class_is_derived_from(ctx, class_id, NMO_CID_BEHAVIOR)) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "name", name);
            }

            yyjson_mut_arr_add_val(arr, item);
            behavior_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)behavior_count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "behavior.list", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);
            if (!nmo_cli_class_is_derived_from(ctx, class_id, NMO_CID_BEHAVIOR)) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-",
            };
            nmo_cli_table_add_row(&table, cells, 3);
            behavior_count++;
        }

        fprintf(out, "Behaviors: %zu\n\n", behavior_count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Reuse object show output contract. */
    return nmo_cmd_object_show(argc, argv, global);
}

typedef struct {
    nmo_class_id_t class_id;
    size_t count;
} nmo_cli_class_count_t;

static int class_count_cmp_desc(const void *a, const void *b) {
    const nmo_cli_class_count_t *aa = (const nmo_cli_class_count_t *)a;
    const nmo_cli_class_count_t *bb = (const nmo_cli_class_count_t *)b;
    if (aa->count != bb->count) {
        return (aa->count < bb->count) ? 1 : -1;
    }
    if (aa->class_id != bb->class_id) {
        return (aa->class_id < bb->class_id) ? -1 : 1;
    }
    return 0;
}

int nmo_cmd_behavior_stats(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo behavior stats <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_class_count_t *by_class = NULL;
    size_t by_class_count = 0;
    size_t by_class_cap = 0;
    size_t total = 0;

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_class_id_t class_id = nmo_object_get_class_id(obj);
        if (!nmo_cli_class_is_derived_from(ctx, class_id, NMO_CID_BEHAVIOR)) {
            continue;
        }
        total++;

        bool found = false;
        for (size_t j = 0; j < by_class_count; ++j) {
            if (by_class[j].class_id == class_id) {
                by_class[j].count++;
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }

        if (by_class_count == by_class_cap) {
            size_t new_cap = (by_class_cap == 0) ? 8 : (by_class_cap * 2);
            nmo_cli_class_count_t *new_arr = (nmo_cli_class_count_t *)realloc(by_class, new_cap * sizeof(*by_class));
            if (!new_arr) {
                free(by_class);
                nmo_tool_close_session(ctx, session);
                fprintf(stderr, "Error: Out of memory\n");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
            by_class = new_arr;
            by_class_cap = new_cap;
        }
        by_class[by_class_count++] = (nmo_cli_class_count_t){.class_id = class_id, .count = 1};
    }

    qsort(by_class, by_class_count, sizeof(*by_class), class_count_cmp_desc);

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        free(by_class);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total", (uint64_t)total);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < by_class_count; ++i) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "class_id", (uint64_t)by_class[i].class_id);
            yyjson_mut_obj_add_uint(doc, item, "count", (uint64_t)by_class[i].count);

            const char *class_name = nmo_cli_class_name_from_id(ctx, by_class[i].class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "by_class", arr);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "behavior.stats", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Behavior Statistics", colorize);
        fprintf(out, "\n");

        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", total);
        nmo_cli_print_kv(out, "Total", buf, 16, colorize);
        fprintf(out, "\n");

        static const nmo_cli_table_col_t columns[] = {
            {"Class ID", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 32},
            {"Count", NMO_CLI_ALIGN_RIGHT, 5, 0},
        };
        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < by_class_count; ++i) {
            char class_id_buf[16];
            char count_buf[16];
            snprintf(class_id_buf, sizeof(class_id_buf), "%u", (unsigned)by_class[i].class_id);
            snprintf(count_buf, sizeof(count_buf), "%zu", by_class[i].count);

            const char *class_name = nmo_cli_class_name_from_id(ctx, by_class[i].class_id);
            const char *cells[] = {
                class_id_buf,
                class_name ? class_name : "-",
                count_buf,
            };
            nmo_cli_table_add_row(&table, cells, 3);
        }

        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    free(by_class);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}
