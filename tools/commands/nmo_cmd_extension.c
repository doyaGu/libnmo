/**
 * @file nmo_cmd_extension.c
 * @brief CLI extension command group implementation
 */

#include "nmo_cmd_extension.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "runtime/nmo_context.h"
#include "extension/nmo_extension_registry.h"
#include "core/nmo_guid.h"
#include <stdio.h>
#include <string.h>

static int extension_info_run(nmo_cmd_ctx_t *c);
static int extension_check_run(nmo_cmd_ctx_t *c, bool strict_mode);

int nmo_cmd_extension_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: extension info|check ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "info") == 0 || strcmp(argv[0], "i") == 0) {
        return extension_info_run(ctx);
    }
    if (strcmp(argv[0], "check") == 0 || strcmp(argv[0], "ch") == 0) {
        return extension_check_run(ctx, false);
    }

    fprintf(stderr, "Unsupported extension read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/* Helper: Convert plugin category enum to string */
static const char *plugin_category_to_string(nmo_plugin_category_t category) {
    switch (category) {
        case NMO_PLUGIN_BITMAP_READER:     return "BitmapReader";
        case NMO_PLUGIN_SOUND_READER:      return "SoundReader";
        case NMO_PLUGIN_MODEL_READER:      return "ModelReader";
        case NMO_PLUGIN_MANAGER_DLL:       return "Manager";
        case NMO_PLUGIN_BEHAVIOR_DLL:      return "Behavior";
        case NMO_PLUGIN_RENDER_DLL:        return "Render";
        case NMO_PLUGIN_MOVIE_READER:      return "MovieReader";
        case NMO_PLUGIN_EXTENSION_DLL:     return "Extension";
        case NMO_PLUGIN_CUSTOM_DLL:        return "Custom";
        default:                           return "Unknown";
    }
}

/* Helper: Format plugin flags as string */
static void format_plugin_flags(uint32_t flags, char *buf, size_t buf_size) {
    if (buf_size == 0) {
        return;
    }

    buf[0] = '\0';
    bool first = true;

    if (flags & NMO_EXTENSION_FLAG_DYNAMIC) {
        if (!first) {
            strncat(buf, ", ", buf_size - strlen(buf) - 1);
        }
        strncat(buf, "Dynamic", buf_size - strlen(buf) - 1);
        first = false;
    }

    if (flags & NMO_EXTENSION_FLAG_INITIALIZED) {
        if (!first) {
            strncat(buf, ", ", buf_size - strlen(buf) - 1);
        }
        strncat(buf, "Initialized", buf_size - strlen(buf) - 1);
        first = false;
    }

    if (first) {
        strncat(buf, "None", buf_size - strlen(buf) - 1);
    }
}

/* ============================================================================
 * extension list
 * ============================================================================ */

int nmo_cmd_extension_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    (void)argc;
    (void)argv;

    /* Create context to access extension registry */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create context\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get extension registry */
    nmo_extension_registry_t *registry = nmo_context_get_extension_registry(ctx);
    if (!registry) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Extension registry not available\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get list of plugins */
    size_t count = 0;
    const nmo_extension_plugin_info_t *plugins = nmo_extension_registry_list(registry, &count);

    /* Setup output stream */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) {
        nmo_context_release(ctx);
        return rc;
    }

    /* Output in requested format */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "plugin_count", (uint64_t)count);

        yyjson_mut_val *plugins_arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < count; ++i) {
            const nmo_extension_plugin_info_t *p = &plugins[i];
            yyjson_mut_val *plugin_obj = yyjson_mut_obj(doc);

            char guid_str[64];
            nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
            yyjson_mut_obj_add_str(doc, plugin_obj, "guid", guid_str);
            nmo_cli_json_add_str_safe(doc, plugin_obj, "name", p->name ? p->name : "");
            yyjson_mut_obj_add_uint(doc, plugin_obj, "version", (uint64_t)p->version);
            yyjson_mut_obj_add_str(doc, plugin_obj, "category", plugin_category_to_string(p->category));
            yyjson_mut_obj_add_uint(doc, plugin_obj, "flags", (uint64_t)p->flags);
            yyjson_mut_obj_add_bool(doc, plugin_obj, "dynamic", (p->flags & NMO_EXTENSION_FLAG_DYNAMIC) != 0);
            yyjson_mut_obj_add_bool(doc, plugin_obj, "initialized", (p->flags & NMO_EXTENSION_FLAG_INITIALIZED) != 0);
            yyjson_mut_obj_add_uint(doc, plugin_obj, "manager_count", (uint64_t)p->manager_count);
            yyjson_mut_obj_add_uint(doc, plugin_obj, "type_count", (uint64_t)p->type_count);

            if (p->library_path) {
                nmo_cli_json_add_str_safe(doc, plugin_obj, "library_path", p->library_path);
            }

            yyjson_mut_arr_append(plugins_arr, plugin_obj);
        }
        yyjson_mut_obj_add_val(doc, data, "plugins", plugins_arr);

        nmo_cli_json_write_enveloped_and_free(doc, data, "extension.list", NULL, c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "Registered Extensions", c.colorize);
        fprintf(c.out, "\n");

        if (count == 0) {
            fprintf(c.out, "No extensions loaded.\n");
        } else {
            /* Build table */
            nmo_cli_table_col_t columns[] = {
                { "GUID",         NMO_CLI_ALIGN_LEFT,   36, 0 },
                { "Name",         NMO_CLI_ALIGN_LEFT,   20, 0 },
                { "Version",      NMO_CLI_ALIGN_RIGHT,   8, 0 },
                { "Category",     NMO_CLI_ALIGN_LEFT,   12, 0 },
                { "Flags",        NMO_CLI_ALIGN_LEFT,   16, 0 },
                { "Mgr/Type",     NMO_CLI_ALIGN_RIGHT,   8, 0 },
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

            for (size_t i = 0; i < count; ++i) {
                const nmo_extension_plugin_info_t *p = &plugins[i];

                char guid_str[64];
                char version_str[16];
                char flags_str[64];
                char counts_str[16];

                nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
                snprintf(version_str, sizeof(version_str), "%u", p->version);
                format_plugin_flags(p->flags, flags_str, sizeof(flags_str));
                snprintf(counts_str, sizeof(counts_str), "%zu/%zu",
                         p->manager_count, p->type_count);

                const char *cells[] = {
                    guid_str,
                    p->name ? p->name : "(unnamed)",
                    version_str,
                    plugin_category_to_string(p->category),
                    flags_str,
                    counts_str
                };

                nmo_cli_table_add_row(&table, cells, sizeof(cells) / sizeof(cells[0]));
            }

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);

            fprintf(c.out, "\nTotal: %zu extension(s)\n", count);
        }
    }

    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * extension load
 * ============================================================================ */

int nmo_cmd_extension_load(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *dll_path = nmo_tool_find_file_arg(argc, argv);
    if (!dll_path) {
        fprintf(stderr, "Error: No DLL path specified\n");
        fprintf(stderr, "Usage: nmo extension load <path.dll>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Create context */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create context\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get extension registry */
    nmo_extension_registry_t *registry = nmo_context_get_extension_registry(ctx);
    if (!registry) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Extension registry not available\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get count before loading */
    size_t count_before = nmo_extension_registry_get_count(registry);

    /* Load library */
    nmo_status_t status = nmo_extension_registry_load_library(registry, dll_path, NULL);
    if (status != NMO_OK) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Failed to load extension from '%s': error code %d\n",
                dll_path, status);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get count after loading */
    size_t count_after = nmo_extension_registry_get_count(registry);
    size_t loaded_count = count_after - count_before;

    /* Setup output stream */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) {
        nmo_context_release(ctx);
        return rc;
    }

    /* Output in requested format */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        nmo_cli_json_add_str_safe(doc, data, "library_path", dll_path);
        yyjson_mut_obj_add_bool(doc, data, "success", true);
        yyjson_mut_obj_add_uint(doc, data, "loaded_count", (uint64_t)loaded_count);

        /* List newly loaded plugins */
        yyjson_mut_val *plugins_arr = yyjson_mut_arr(doc);
        size_t total_count = 0;
        const nmo_extension_plugin_info_t *plugins = nmo_extension_registry_list(registry, &total_count);

        /* Assume newly loaded plugins are at the end */
        for (size_t i = count_before; i < total_count; ++i) {
            const nmo_extension_plugin_info_t *p = &plugins[i];
            yyjson_mut_val *plugin_obj = yyjson_mut_obj(doc);

            char guid_str[64];
            nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
            yyjson_mut_obj_add_str(doc, plugin_obj, "guid", guid_str);
            nmo_cli_json_add_str_safe(doc, plugin_obj, "name", p->name ? p->name : "");
            yyjson_mut_obj_add_uint(doc, plugin_obj, "version", (uint64_t)p->version);

            yyjson_mut_arr_append(plugins_arr, plugin_obj);
        }
        yyjson_mut_obj_add_val(doc, data, "plugins", plugins_arr);

        nmo_cli_json_write_enveloped_and_free(doc, data, "extension.load", dll_path, c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "Extension Load Result", c.colorize);
        fprintf(c.out, "\n");
        nmo_cli_print_kv(c.out, "Library", dll_path, 12, c.colorize);

        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%zu", loaded_count);
        nmo_cli_print_kv(c.out, "Loaded", count_str, 12, c.colorize);

        if (loaded_count > 0) {
            fprintf(c.out, "\nLoaded plugins:\n");

            size_t total_count = 0;
            const nmo_extension_plugin_info_t *plugins = nmo_extension_registry_list(registry, &total_count);

            for (size_t i = count_before; i < total_count; ++i) {
                const nmo_extension_plugin_info_t *p = &plugins[i];
                char guid_str[64];
                nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
                fprintf(c.out, "  - %s (%s, version %u)\n",
                        p->name ? p->name : "(unnamed)",
                        guid_str,
                        p->version);
            }
        }

        fprintf(c.out, "\nStatus: SUCCESS\n");
    }

    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * extension info
 * ============================================================================ */

static int extension_info_run(nmo_cmd_ctx_t *c)
{
    /* Get plugin diagnostics */
    const nmo_tool_plugin_diagnostics_t *diag =
        nmo_document_get_plugin_diagnostics(c->document);
    if (!diag) {
        fprintf(stderr, "Error: No plugin diagnostics available\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output in requested format */
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_uint(doc, data, "plugin_count", (uint64_t)diag->entry_count);
        yyjson_mut_obj_add_uint(doc, data, "missing_count", (uint64_t)diag->missing_count);
        yyjson_mut_obj_add_uint(doc, data, "outdated_count", (uint64_t)diag->outdated_count);
        yyjson_mut_obj_add_bool(doc, data, "extension_registry_available", diag->extension_registry_available != 0);

        yyjson_mut_val *plugins_arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < diag->entry_count; ++i) {
            const nmo_tool_plugin_dependency_status_t *p = &diag->entries[i];
            yyjson_mut_val *plugin_obj = yyjson_mut_obj(doc);

            char guid_str[64];
            nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
            yyjson_mut_obj_add_str(doc, plugin_obj, "guid", guid_str);
            yyjson_mut_obj_add_str(doc, plugin_obj, "category", plugin_category_to_string(p->category));
            yyjson_mut_obj_add_uint(doc, plugin_obj, "required_version", (uint64_t)p->required_version);
            yyjson_mut_obj_add_uint(doc, plugin_obj, "resolved_version", (uint64_t)p->resolved_version);

            if (p->resolved_name) {
                nmo_cli_json_add_str_safe(doc, plugin_obj, "resolved_name", p->resolved_name);
            }

            yyjson_mut_obj_add_bool(doc, plugin_obj, "missing",
                (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) != 0);
            yyjson_mut_obj_add_bool(doc, plugin_obj, "version_too_old",
                (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) != 0);
            yyjson_mut_obj_add_bool(doc, plugin_obj, "manager_unavailable",
                (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) != 0);

            yyjson_mut_arr_append(plugins_arr, plugin_obj);
        }
        yyjson_mut_obj_add_val(doc, data, "plugins", plugins_arr);

        nmo_cmd_ctx_json_end(c, doc, data, "extension.info");
    } else {
        /* Text output */
        nmo_cli_print_heading(c->out, "Extension Metadata", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 16, c->colorize);
        fprintf(c->out, "\n");

        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%zu", diag->entry_count);
        nmo_cli_print_kv(c->out, "Plugin Count", count_str, 16, c->colorize);

        snprintf(count_str, sizeof(count_str), "%zu", diag->missing_count);
        nmo_cli_print_kv(c->out, "Missing", count_str, 16, c->colorize);

        snprintf(count_str, sizeof(count_str), "%zu", diag->outdated_count);
        nmo_cli_print_kv(c->out, "Outdated", count_str, 16, c->colorize);

        nmo_cli_print_kv(c->out, "Registry", diag->extension_registry_available ? "Available" : "Not Available", 16, c->colorize);

        if (diag->entry_count > 0) {
            fprintf(c->out, "\nPlugin Dependencies:\n\n");

            /* Build table */
            nmo_cli_table_col_t columns[] = {
                { "GUID",         NMO_CLI_ALIGN_LEFT,   36, 0 },
                { "Category",     NMO_CLI_ALIGN_LEFT,   12, 0 },
                { "Req Ver",      NMO_CLI_ALIGN_RIGHT,   8, 0 },
                { "Resolved Ver", NMO_CLI_ALIGN_RIGHT,  12, 0 },
                { "Status",       NMO_CLI_ALIGN_LEFT,   20, 0 },
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

            for (size_t i = 0; i < diag->entry_count; ++i) {
                const nmo_tool_plugin_dependency_status_t *p = &diag->entries[i];

                char guid_str[64];
                char req_ver_str[16];
                char res_ver_str[16];
                char status_str[64];

                nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
                snprintf(req_ver_str, sizeof(req_ver_str), "%u", p->required_version);

                if (p->resolved_version != 0) {
                    snprintf(res_ver_str, sizeof(res_ver_str), "%u", p->resolved_version);
                } else {
                    snprintf(res_ver_str, sizeof(res_ver_str), "-");
                }

                status_str[0] = '\0';
                if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) {
                    strncat(status_str, "MISSING", sizeof(status_str) - strlen(status_str) - 1);
                } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
                    strncat(status_str, "VERSION_TOO_OLD", sizeof(status_str) - strlen(status_str) - 1);
                } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
                    strncat(status_str, "MANAGER_UNAVAIL", sizeof(status_str) - strlen(status_str) - 1);
                } else {
                    strncat(status_str, "OK", sizeof(status_str) - strlen(status_str) - 1);
                }

                if (p->resolved_name && p->resolved_name[0] != '\0') {
                    size_t len = strlen(status_str);
                    snprintf(status_str + len, sizeof(status_str) - len, " (%s)", p->resolved_name);
                }

                const char *cells[] = {
                    guid_str,
                    plugin_category_to_string(p->category),
                    req_ver_str,
                    res_ver_str,
                    status_str
                };

                nmo_cli_table_add_row(&table, cells, sizeof(cells) / sizeof(cells[0]));
            }

            nmo_cli_table_print(&table, c->out, c->colorize);
            nmo_cli_table_free(&table);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_extension_info(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = extension_info_run(&c);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * extension check
 * ============================================================================ */

static int extension_check_run(nmo_cmd_ctx_t *c, bool strict_mode)
{
    /* Get plugin diagnostics */
    const nmo_tool_plugin_diagnostics_t *diag =
        nmo_document_get_plugin_diagnostics(c->document);
    if (!diag) {
        fprintf(stderr, "Error: No plugin diagnostics available\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Determine exit code based on diagnostics */
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    bool has_issues = (diag->missing_count > 0 || diag->outdated_count > 0);

    if (has_issues && strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    /* Output in requested format */
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_bool(doc, data, "all_dependencies_satisfied", !has_issues);
        yyjson_mut_obj_add_uint(doc, data, "total_dependencies", (uint64_t)diag->entry_count);
        yyjson_mut_obj_add_uint(doc, data, "missing_count", (uint64_t)diag->missing_count);
        yyjson_mut_obj_add_uint(doc, data, "outdated_count", (uint64_t)diag->outdated_count);

        /* List issues */
        yyjson_mut_val *issues_arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < diag->entry_count; ++i) {
            const nmo_tool_plugin_dependency_status_t *p = &diag->entries[i];

            if (p->status_flags != 0) {
                yyjson_mut_val *issue_obj = yyjson_mut_obj(doc);

                char guid_str[64];
                nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
                yyjson_mut_obj_add_str(doc, issue_obj, "guid", guid_str);
                yyjson_mut_obj_add_str(doc, issue_obj, "category", plugin_category_to_string(p->category));
                yyjson_mut_obj_add_uint(doc, issue_obj, "required_version", (uint64_t)p->required_version);

                if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) {
                    yyjson_mut_obj_add_str(doc, issue_obj, "issue", "missing");
                } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
                    yyjson_mut_obj_add_str(doc, issue_obj, "issue", "version_too_old");
                    yyjson_mut_obj_add_uint(doc, issue_obj, "resolved_version", (uint64_t)p->resolved_version);
                } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
                    yyjson_mut_obj_add_str(doc, issue_obj, "issue", "manager_unavailable");
                }

                yyjson_mut_arr_append(issues_arr, issue_obj);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "issues", issues_arr);

        nmo_cmd_ctx_json_end(c, doc, data, "extension.check");
    } else {
        /* Text output */
        nmo_cli_print_heading(c->out, "Plugin Dependency Check", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 16, c->colorize);
        fprintf(c->out, "\n");

        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%zu", diag->entry_count);
        nmo_cli_print_kv(c->out, "Total", count_str, 16, c->colorize);

        snprintf(count_str, sizeof(count_str), "%zu", diag->missing_count);
        nmo_cli_print_kv(c->out, "Missing", count_str, 16, c->colorize);

        snprintf(count_str, sizeof(count_str), "%zu", diag->outdated_count);
        nmo_cli_print_kv(c->out, "Outdated", count_str, 16, c->colorize);

        if (has_issues) {
            fprintf(c->out, "\nIssues Found:\n\n");

            for (size_t i = 0; i < diag->entry_count; ++i) {
                const nmo_tool_plugin_dependency_status_t *p = &diag->entries[i];

                if (p->status_flags != 0) {
                    char guid_str[64];
                    nmo_guid_format(p->guid, guid_str, sizeof(guid_str));

                    fprintf(c->out, "  - %s (%s)\n", guid_str, plugin_category_to_string(p->category));

                    if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) {
                        fprintf(c->out, "    Status: MISSING (required version %u)\n", p->required_version);
                    } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
                        fprintf(c->out, "    Status: VERSION_TOO_OLD (required: %u, found: %u)\n",
                                p->required_version, p->resolved_version);
                    } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
                        fprintf(c->out, "    Status: MANAGER_UNAVAILABLE\n");
                    }

                    if (p->resolved_name && p->resolved_name[0] != '\0') {
                        fprintf(c->out, "    Name: %s\n", p->resolved_name);
                    }
                }
            }

            fprintf(c->out, "\nResult: ISSUES FOUND\n");
        } else {
            fprintf(c->out, "\nResult: ALL DEPENDENCIES SATISFIED\n");
        }
    }

    return exit_code;
}

int nmo_cmd_extension_check(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = extension_check_run(&c, global ? global->strict_mode : false);
    return nmo_cmd_ctx_done(&c, rc);
}

