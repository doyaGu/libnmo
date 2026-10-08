/**
 * @file nmo_cmd_script_analyze.c
 * @brief nmo script analyze: run a Lua analysis over the script models of files
 */

#include "nmo_cmd_script.h"

#include "../nmo_opt.h"
#include "nmo_lua.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"

/* The analyses bundled with the CLI (tools/lua/analyze/) */
static const char analyze_summary_text[] = {
#include "analyze/summary.lua.inc"
};
static const char analyze_messages_text[] = {
#include "analyze/messages.lua.inc"
};
static const char analyze_arrays_text[] = {
#include "analyze/arrays.lua.inc"
};
static const char analyze_interactions_text[] = {
#include "analyze/interactions.lua.inc"
};

typedef struct analyze_bundled {
    const char *name;
    const char *summary;
    const char *text;
    size_t size;
} analyze_bundled_t;

#define ANALYZE_BUNDLED(name, summary, text) {name, summary, text, sizeof(text) - 1}

static const analyze_bundled_t analyze_bundled[] = {
    ANALYZE_BUNDLED("summary", "What each script waits for, sends, reads, writes, and activates",
                    analyze_summary_text),
    ANALYZE_BUNDLED("messages", "Where each message goes: the receivers each send reaches",
                    analyze_messages_text),
    ANALYZE_BUNDLED("arrays", "Who reads and writes each data array, by column, across the files",
                    analyze_arrays_text),
    ANALYZE_BUNDLED("interactions",
                    "How scripts start each other: messages and script activation (--dot)",
                    analyze_interactions_text),
};

static const analyze_bundled_t *analyze_find_bundled(const char *name)
{
    for (size_t i = 0; i < sizeof(analyze_bundled) / sizeof(analyze_bundled[0]); i++) {
        if (strcmp(analyze_bundled[i].name, name) == 0) {
            return &analyze_bundled[i];
        }
    }
    return NULL;
}

static bool analyze_is_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    fclose(fp);
    return true;
}

/* print(), writing to the command output (upvalue 1) */
static int analyze_print(lua_State *L)
{
    FILE *out = (FILE *)lua_touserdata(L, lua_upvalueindex(1));
    int count = lua_gettop(L);
    for (int i = 1; i <= count; i++) {
        size_t length = 0;
        const char *text = luaL_tolstring(L, i, &length);
        if (i > 1) {
            fputc('\t', out);
        }
        fwrite(text, 1, length, out);
        lua_pop(L, 1);
    }
    fputc('\n', out);
    return 0;
}

/* The file name of `path`, without its directories. */
static const char *analyze_base_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

/* Open each file, push its model table into the global `models`, and close it again. */
static int analyze_load_models(lua_State *L, const char *const *files, size_t file_count,
                               const nmo_cli_global_opts_t *global)
{
    /* the files share the command output, which the caller opened */
    nmo_cli_global_opts_t file_global = *global;
    file_global.output_path = NULL;

    lua_createtable(L, (int)file_count, 0);
    for (size_t i = 0; i < file_count; i++) {
        nmo_cmd_ctx_t c;
        int rc = nmo_cmd_ctx_init_with_file(&c, files[i], &file_global);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            lua_pop(L, 1);
            return rc;
        }
        nmo_status_t status = nmo_lua_push_script_model(L, c.workspace);
        nmo_cmd_ctx_done(&c, 0);
        if (status != NMO_OK) {
            const char *message = nmo_last_error_message();
            fprintf(stderr, "Error: %s: %s\n", files[i],
                    message != NULL ? message : nmo_error_string(status));
            lua_pop(L, 1);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        lua_pushstring(L, files[i]);
        lua_setfield(L, -2, "path");
        lua_pushstring(L, analyze_base_name(files[i]));
        lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    lua_rawgeti(L, -1, 1);
    lua_setglobal(L, "model");
    lua_setglobal(L, "models");
    return NMO_CLI_EXIT_SUCCESS;
}

static int analyze_list(const nmo_cli_global_opts_t *global)
{
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    for (size_t i = 0; i < sizeof(analyze_bundled) / sizeof(analyze_bundled[0]); i++) {
        fprintf(c.out, "  %-14s %s\n", analyze_bundled[i].name, analyze_bundled[i].summary);
    }
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_script_analyze(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--list", "-l", NMO_OPT_FLAG, "List the bundled analyses"},
    };
    enum { OPT_LIST, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[512];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);

    /* the arguments after "--" are the script's */
    int split = argc;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            split = i;
            break;
        }
    }
    if (nmo_opt_parse(split, argv, opts, OPT_COUNT, &r) < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (nmo_opt_flag(&vals[OPT_LIST])) {
        return analyze_list(global);
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo script analyze <script.lua | analysis> <file>... "
                        "[-- <arg>...]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *script = r.pos_args[0];
    const analyze_bundled_t *bundled = NULL;
    if (!analyze_is_file(script)) {
        bundled = analyze_find_bundled(script);
        if (bundled == NULL) {
            fprintf(stderr, "Error: '%s' is neither a Lua file nor a bundled analysis "
                            "(see nmo script analyze --list)\n", script);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    nmo_cmd_ctx_t out;
    int rc = nmo_cmd_ctx_init_no_file(&out, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    if (runtime == NULL || nmo_lua_register_platform_bindings(runtime) != NMO_OK) {
        fprintf(stderr, "Error: Failed to create the Lua runtime\n");
        nmo_lua_runtime_destroy(runtime);
        return nmo_cmd_ctx_done(&out, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    lua_State *L = nmo_lua_runtime_state(runtime);
    lua_pushlightuserdata(L, out.out);
    lua_pushcclosure(L, analyze_print, 1);
    lua_setglobal(L, "print");

    rc = analyze_load_models(L, &r.pos_args[1], r.pos_count - 1, global);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        const char *const *script_args = (const char *const *)&argv[split + 1];
        size_t script_arg_count = split < argc ? (size_t)(argc - split - 1) : 0u;
        nmo_status_t status = bundled != NULL
            ? nmo_lua_runtime_execute_buffer(runtime, bundled->name, bundled->text,
                                             bundled->size, script_args, script_arg_count)
            : nmo_lua_runtime_execute_file(runtime, script, script_args, script_arg_count);
        fflush(out.out);
        if (status != NMO_OK) {
            const char *message = nmo_last_error_message();
            fprintf(stderr, "Error: %s\n", message != NULL ? message : nmo_error_string(status));
            rc = status == NMO_ERR_CANT_OPEN_FILE ? NMO_CLI_EXIT_IO_ERROR
                                                  : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }
    nmo_lua_runtime_destroy(runtime);
    return nmo_cmd_ctx_done(&out, rc);
}
