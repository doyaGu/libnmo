/**
 * @file nmo_cmd_extension.c
 * @brief CLI extension command group implementation
 */

#include "nmo_cmd_extension.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_cli_record.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "object/nmo_context.h"
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

/* ============================================================================
 * extension list
 * ============================================================================ */

/* One registered plugin for extension list. */
static bool extension_build_record(const nmo_extension_plugin_info_t *p,
                                   nmo_cli_record_t *rec)
{
    bool dynamic = (p->flags & NMO_EXTENSION_FLAG_DYNAMIC) != 0;
    bool initialized = (p->flags & NMO_EXTENSION_FLAG_INITIALIZED) != 0;

    bool ok = nmo_cli_record_guid(rec, "guid", "GUID", p->guid) &&
              nmo_cli_record_str(rec, "name", NULL, p->name ? p->name : "") &&
              nmo_cli_record_text(rec, "Name", p->name ? p->name : "(unnamed)") &&
              nmo_cli_record_uint(rec, "version", "Version", (uint64_t)p->version) &&
              nmo_cli_record_str(rec, "category", "Category",
                                 plugin_category_to_string(p->category)) &&
              nmo_cli_record_uint(rec, "flags", NULL, (uint64_t)p->flags) &&
              nmo_cli_record_bool(rec, "dynamic", NULL, dynamic) &&
              nmo_cli_record_bool(rec, "initialized", NULL, initialized) &&
              nmo_cli_record_text_fmt(rec, "Flags", "%s%s%s",
                                      dynamic ? "Dynamic" : "",
                                      (dynamic && initialized) ? ", " : "",
                                      initialized ? "Initialized" : (dynamic ? "" : "None")) &&
              nmo_cli_record_uint(rec, "manager_count", NULL, (uint64_t)p->manager_count) &&
              nmo_cli_record_uint(rec, "type_count", NULL, (uint64_t)p->type_count) &&
              nmo_cli_record_text_fmt(rec, "Mgr/Type", "%zu/%zu", p->manager_count, p->type_count);
    if (ok && p->library_path) {
        ok = nmo_cli_record_str(rec, "library_path", NULL, p->library_path);
    }
    return ok;
}

/* Text columns: GUID, Name, Version, Category, Flags, Mgr/Type. */
static const nmo_cli_table_col_t extension_list_columns[] = {
    { "GUID",         NMO_CLI_ALIGN_LEFT,   36, 0 },
    { "Name",         NMO_CLI_ALIGN_LEFT,   20, 0 },
    { "Version",      NMO_CLI_ALIGN_RIGHT,   8, 0 },
    { "Category",     NMO_CLI_ALIGN_LEFT,   12, 0 },
    { "Flags",        NMO_CLI_ALIGN_LEFT,   16, 0 },
    { "Mgr/Type",     NMO_CLI_ALIGN_RIGHT,   8, 0 },
};

static nmo_cli_record_t *extension_list_record_new(
    const nmo_extension_plugin_info_t *plugins,
    size_t count)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_title(rec, "Registered Extensions") &&
              nmo_cli_record_raw(rec, "\n") &&
              nmo_cli_record_uint(rec, "plugin_count", NULL, (uint64_t)count);
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "plugins", NULL) : NULL;
    ok = ok && arr != NULL;
    if (ok && count > 0) {
        ok = nmo_cli_record_array_set_table(
            arr, extension_list_columns,
            sizeof(extension_list_columns) / sizeof(extension_list_columns[0]));
    }
    for (size_t i = 0; ok && i < count; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && extension_build_record(&plugins[i], item);
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(arr, item);
        }
    }
    if (ok) {
        ok = count == 0
            ? nmo_cli_record_raw(rec, "No extensions loaded.\n")
            : nmo_cli_record_raw_fmt(rec, "\nTotal: %zu extension(s)\n", count);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

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

    nmo_cli_record_t *rec = extension_list_record_new(plugins, count);
    rc = nmo_cmd_ctx_emit_record(&c, rec, "extension.list", 0, c.colorize);
    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * extension load
 * ============================================================================ */

/* Newly loaded plugins are assumed to be at the end of the registry list. */
static nmo_cli_record_t *extension_load_record_new(
    const char *dll_path,
    size_t loaded_count,
    const nmo_extension_plugin_info_t *plugins,
    size_t first,
    size_t total_count)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_title(rec, "Extension Load Result") &&
              nmo_cli_record_raw(rec, "\n") &&
              nmo_cli_record_str(rec, "library_path", "Library", dll_path) &&
              nmo_cli_record_bool(rec, "success", NULL, true) &&
              nmo_cli_record_uint(rec, "loaded_count", "Loaded", (uint64_t)loaded_count);
    if (ok && loaded_count > 0) {
        ok = nmo_cli_record_raw(rec, "\nLoaded plugins:\n");
    }
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "plugins", NULL) : NULL;
    ok = ok && arr != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(arr);
        nmo_cli_record_array_inline_items(arr);
    }
    for (size_t i = first; ok && i < total_count; ++i) {
        const nmo_extension_plugin_info_t *p = &plugins[i];
        char guid_str[NMO_GUID_STRING_SIZE];
        nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL &&
             nmo_cli_record_str(item, "guid", NULL, guid_str) &&
             nmo_cli_record_str(item, "name", NULL, p->name ? p->name : "") &&
             nmo_cli_record_uint(item, "version", NULL, (uint64_t)p->version);
        if (ok && loaded_count > 0) {
            ok = nmo_cli_record_raw_fmt(item, "  - %s (%s, version %u)\n",
                                        p->name ? p->name : "(unnamed)",
                                        guid_str, p->version);
        }
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(arr, item);
        }
    }
    ok = ok && nmo_cli_record_raw(rec, "\nStatus: SUCCESS\n");
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

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

    size_t total_count = 0;
    const nmo_extension_plugin_info_t *plugins =
        nmo_extension_registry_list(registry, &total_count);
    nmo_cli_record_t *rec = extension_load_record_new(
        dll_path, loaded_count, plugins, count_before, total_count);
    c.file_path = dll_path;
    rc = nmo_cmd_ctx_emit_record(&c, rec, "extension.load", 12, c.colorize);
    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * extension info
 * ============================================================================ */

static const nmo_cli_table_col_t extension_info_columns[] = {
    { "GUID",         NMO_CLI_ALIGN_LEFT,   36, 0 },
    { "Category",     NMO_CLI_ALIGN_LEFT,   12, 0 },
    { "Req Ver",      NMO_CLI_ALIGN_RIGHT,   8, 0 },
    { "Resolved Ver", NMO_CLI_ALIGN_RIGHT,  12, 0 },
    { "Status",       NMO_CLI_ALIGN_LEFT,   20, 0 },
};

static bool extension_info_build_item(const nmo_tool_plugin_dependency_status_t *p,
                                      nmo_cli_record_t *item)
{
    const char *status;
    if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) {
        status = "MISSING";
    } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
        status = "VERSION_TOO_OLD";
    } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
        status = "MANAGER_UNAVAIL";
    } else {
        status = "OK";
    }

    bool ok = nmo_cli_record_guid(item, "guid", "GUID", p->guid) &&
              nmo_cli_record_str(item, "category", "Category",
                                 plugin_category_to_string(p->category)) &&
              nmo_cli_record_uint(item, "required_version", "Req Ver",
                                  (uint64_t)p->required_version) &&
              nmo_cli_record_uint(item, "resolved_version", "Resolved Ver",
                                  (uint64_t)p->resolved_version);
    if (ok && p->resolved_version == 0) {
        ok = nmo_cli_record_set_text(item, "-");
    }
    if (ok && p->resolved_name) {
        ok = nmo_cli_record_str(item, "resolved_name", NULL, p->resolved_name);
    }
    ok = ok &&
         nmo_cli_record_bool(item, "missing", NULL,
             (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) != 0) &&
         nmo_cli_record_bool(item, "version_too_old", NULL,
             (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) != 0) &&
         nmo_cli_record_bool(item, "manager_unavailable", NULL,
             (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) != 0);
    if (ok && p->resolved_name && p->resolved_name[0] != '\0') {
        ok = nmo_cli_record_text_fmt(item, "Status", "%s (%s)", status, p->resolved_name);
    } else if (ok) {
        ok = nmo_cli_record_text(item, "Status", status);
    }
    return ok;
}

static nmo_cli_record_t *extension_info_record_new(
    const char *file_path,
    const nmo_tool_plugin_diagnostics_t *diag)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_title(rec, "Extension Metadata") &&
              nmo_cli_record_str_opt(rec, "file", "File", file_path, "") &&
              nmo_cli_record_raw(rec, "\n") &&
              nmo_cli_record_uint(rec, "plugin_count", "Plugin Count",
                                  (uint64_t)diag->entry_count) &&
              nmo_cli_record_uint(rec, "missing_count", "Missing",
                                  (uint64_t)diag->missing_count) &&
              nmo_cli_record_uint(rec, "outdated_count", "Outdated",
                                  (uint64_t)diag->outdated_count) &&
              nmo_cli_record_bool(rec, "extension_registry_available", NULL,
                                  diag->extension_registry_available != 0) &&
              nmo_cli_record_text(rec, "Registry",
                                  diag->extension_registry_available
                                      ? "Available" : "Not Available");
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "plugins", NULL) : NULL;
    ok = ok && arr != NULL;
    if (ok && diag->entry_count > 0) {
        ok = nmo_cli_record_array_set_heading(arr, "Plugin Dependencies:\n") &&
             nmo_cli_record_array_set_table(
                 arr, extension_info_columns,
                 sizeof(extension_info_columns) / sizeof(extension_info_columns[0]));
    }
    for (size_t i = 0; ok && i < diag->entry_count; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && extension_info_build_item(&diag->entries[i], item);
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(arr, item);
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static int extension_info_run(nmo_cmd_ctx_t *c)
{
    /* Get plugin diagnostics */
    const nmo_tool_plugin_diagnostics_t *diag =
        nmo_document_get_plugin_diagnostics(c->document);
    if (!diag) {
        fprintf(stderr, "Error: No plugin diagnostics available\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return nmo_cmd_ctx_emit_record(c, extension_info_record_new(c->file_path, diag),
                                   "extension.info", 16, c->colorize);
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

/* Issue lines are shown in text only when some dependency is missing or outdated. */
static bool extension_check_build_issue(const nmo_tool_plugin_dependency_status_t *p,
                                        bool show_text,
                                        nmo_cli_record_t *item)
{
    char guid_str[NMO_GUID_STRING_SIZE];
    nmo_guid_format(p->guid, guid_str, sizeof(guid_str));
    const char *category = plugin_category_to_string(p->category);

    bool ok = nmo_cli_record_str(item, "guid", NULL, guid_str) &&
              nmo_cli_record_str(item, "category", NULL, category) &&
              nmo_cli_record_uint(item, "required_version", NULL,
                                  (uint64_t)p->required_version);
    if (ok && show_text) {
        ok = nmo_cli_record_raw_fmt(item, "  - %s (%s)\n", guid_str, category);
    }
    if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) {
        ok = ok && nmo_cli_record_str(item, "issue", NULL, "missing");
        if (ok && show_text) {
            ok = nmo_cli_record_raw_fmt(item, "    Status: MISSING (required version %u)\n",
                                        p->required_version);
        }
    } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
        ok = ok && nmo_cli_record_str(item, "issue", NULL, "version_too_old") &&
             nmo_cli_record_uint(item, "resolved_version", NULL,
                                 (uint64_t)p->resolved_version);
        if (ok && show_text) {
            ok = nmo_cli_record_raw_fmt(item, "    Status: VERSION_TOO_OLD (required: %u, found: %u)\n",
                                        p->required_version, p->resolved_version);
        }
    } else if (p->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
        ok = ok && nmo_cli_record_str(item, "issue", NULL, "manager_unavailable");
        if (ok && show_text) {
            ok = nmo_cli_record_raw(item, "    Status: MANAGER_UNAVAILABLE\n");
        }
    }
    if (ok && show_text && p->resolved_name && p->resolved_name[0] != '\0') {
        ok = nmo_cli_record_raw_fmt(item, "    Name: %s\n", p->resolved_name);
    }
    return ok;
}

static nmo_cli_record_t *extension_check_record_new(
    const char *file_path,
    const nmo_tool_plugin_diagnostics_t *diag,
    bool has_issues)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_title(rec, "Plugin Dependency Check") &&
              nmo_cli_record_str_opt(rec, "file", "File", file_path, "") &&
              nmo_cli_record_raw(rec, "\n") &&
              nmo_cli_record_bool(rec, "all_dependencies_satisfied", NULL, !has_issues) &&
              nmo_cli_record_uint(rec, "total_dependencies", "Total",
                                  (uint64_t)diag->entry_count) &&
              nmo_cli_record_uint(rec, "missing_count", "Missing",
                                  (uint64_t)diag->missing_count) &&
              nmo_cli_record_uint(rec, "outdated_count", "Outdated",
                                  (uint64_t)diag->outdated_count);
    if (ok && has_issues) {
        ok = nmo_cli_record_raw(rec, "\nIssues Found:\n\n");
    }
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "issues", NULL) : NULL;
    ok = ok && arr != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(arr);
        nmo_cli_record_array_inline_items(arr);
    }
    for (size_t i = 0; ok && i < diag->entry_count; ++i) {
        const nmo_tool_plugin_dependency_status_t *p = &diag->entries[i];
        if (p->status_flags == 0) {
            continue;
        }
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && extension_check_build_issue(p, has_issues, item);
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(arr, item);
        }
    }
    ok = ok && nmo_cli_record_raw(rec, has_issues
                                           ? "\nResult: ISSUES FOUND\n"
                                           : "\nResult: ALL DEPENDENCIES SATISFIED\n");
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

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

    int rc = nmo_cmd_ctx_emit_record(
        c, extension_check_record_new(c->file_path, diag, has_issues),
        "extension.check", 16, c->colorize);
    return rc != NMO_CLI_EXIT_SUCCESS ? rc : exit_code;
}

int nmo_cmd_extension_check(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = extension_check_run(&c, global ? global->strict_mode : false);
    return nmo_cmd_ctx_done(&c, rc);
}

