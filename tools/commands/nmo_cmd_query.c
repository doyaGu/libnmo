/**
 * @file nmo_cmd_query.c
 * @brief CLI query command group implementation (DSL integration)
 */

#include "nmo_cmd_query.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "dsl/nmo_dsl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Helper: Format DSL value for output
 * ============================================================================ */

/**
 * Format a DSL value into a buffer for text output.
 * Returns false if the value type is not displayable in text mode.
 */
static bool format_dsl_value(const nmo_dsl_value_t *value, char *buf, size_t buf_size) {
    if (!value || !buf || buf_size == 0) {
        return false;
    }

    switch (value->kind) {
        case NMO_DSL_VALUE_NULL:
            snprintf(buf, buf_size, "null");
            return true;

        case NMO_DSL_VALUE_BOOL:
            snprintf(buf, buf_size, "%s", value->as.b ? "true" : "false");
            return true;

        case NMO_DSL_VALUE_INT:
            snprintf(buf, buf_size, "%lld", (long long)value->as.i);
            return true;

        case NMO_DSL_VALUE_UINT:
            snprintf(buf, buf_size, "%llu", (unsigned long long)value->as.u);
            return true;

        case NMO_DSL_VALUE_REAL:
            snprintf(buf, buf_size, "%g", value->as.r);
            return true;

        case NMO_DSL_VALUE_STRING:
            if (value->as.s) {
                snprintf(buf, buf_size, "\"%s\"", value->as.s);
            } else {
                snprintf(buf, buf_size, "\"\"");
            }
            return true;

        case NMO_DSL_VALUE_BYREF:
            snprintf(buf, buf_size, "<byref:%p>", value->as.byref.ptr);
            return true;

        case NMO_DSL_VALUE_OBJECT:
            snprintf(buf, buf_size, "<object:%p>", value->as.object.instance);
            return true;

        case NMO_DSL_VALUE_SEQ: {
            uint64_t count = nmo_dsl_seq_count(value->as.seq);
            snprintf(buf, buf_size, "<seq:%llu>", (unsigned long long)count);
            return true;
        }

        case NMO_DSL_VALUE_TYPE:
            snprintf(buf, buf_size, "<type>");
            return true;

        default:
            snprintf(buf, buf_size, "<unknown>");
            return false;
    }
}

/**
 * Add a DSL value to a JSON document.
 */
static void add_dsl_value_to_json(yyjson_mut_doc *doc, yyjson_mut_val *parent,
                                   const char *key, const nmo_dsl_value_t *value) {
    if (!value) {
        yyjson_mut_obj_add_null(doc, parent, key);
        return;
    }

    switch (value->kind) {
        case NMO_DSL_VALUE_NULL:
            yyjson_mut_obj_add_null(doc, parent, key);
            break;

        case NMO_DSL_VALUE_BOOL:
            yyjson_mut_obj_add_bool(doc, parent, key, value->as.b);
            break;

        case NMO_DSL_VALUE_INT:
            yyjson_mut_obj_add_sint(doc, parent, key, value->as.i);
            break;

        case NMO_DSL_VALUE_UINT:
            yyjson_mut_obj_add_uint(doc, parent, key, value->as.u);
            break;

        case NMO_DSL_VALUE_REAL:
            yyjson_mut_obj_add_real(doc, parent, key, value->as.r);
            break;

        case NMO_DSL_VALUE_STRING:
            if (value->as.s) {
                yyjson_mut_obj_add_str(doc, parent, key, value->as.s);
            } else {
                yyjson_mut_obj_add_str(doc, parent, key, "");
            }
            break;

        case NMO_DSL_VALUE_BYREF:
        case NMO_DSL_VALUE_OBJECT:
        case NMO_DSL_VALUE_TYPE: {
            char buf[64];
            format_dsl_value(value, buf, sizeof(buf));
            yyjson_mut_obj_add_str(doc, parent, key, buf);
            break;
        }

        case NMO_DSL_VALUE_SEQ: {
            uint64_t count = nmo_dsl_seq_count(value->as.seq);
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (uint64_t i = 0; i < count; i++) {
                nmo_dsl_value_t elem = {0};
                if (nmo_dsl_seq_get(value->as.seq, i, &elem)) {
                    /* Recursively add sequence elements */
                    switch (elem.kind) {
                        case NMO_DSL_VALUE_BOOL:
                            yyjson_mut_arr_add_bool(doc, arr, elem.as.b);
                            break;
                        case NMO_DSL_VALUE_INT:
                            yyjson_mut_arr_add_sint(doc, arr, elem.as.i);
                            break;
                        case NMO_DSL_VALUE_UINT:
                            yyjson_mut_arr_add_uint(doc, arr, elem.as.u);
                            break;
                        case NMO_DSL_VALUE_REAL:
                            yyjson_mut_arr_add_real(doc, arr, elem.as.r);
                            break;
                        case NMO_DSL_VALUE_STRING:
                            yyjson_mut_arr_add_str(doc, arr, elem.as.s ? elem.as.s : "");
                            break;
                        default: {
                            char elem_buf[64];
                            format_dsl_value(&elem, elem_buf, sizeof(elem_buf));
                            yyjson_mut_arr_add_str(doc, arr, elem_buf);
                            break;
                        }
                    }
                }
            }
            yyjson_mut_obj_add_val(doc, parent, key, arr);
            break;
        }

        default: {
            char buf[64];
            format_dsl_value(value, buf, sizeof(buf));
            yyjson_mut_obj_add_str(doc, parent, key, buf);
            break;
        }
    }
}

/**
 * Read file contents into a malloc'd buffer.
 * Returns NULL on error. Caller must free.
 */
static char *read_file_contents(const char *path, char *errbuf, size_t errbuf_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(errbuf, errbuf_size, "Failed to open file: %s", path);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        snprintf(errbuf, errbuf_size, "Failed to seek file: %s", path);
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0) {
        snprintf(errbuf, errbuf_size, "Failed to get file size: %s", path);
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        snprintf(errbuf, errbuf_size, "Failed to rewind file: %s", path);
        fclose(f);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        snprintf(errbuf, errbuf_size, "Out of memory");
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    if ((long)nread != size) {
        snprintf(errbuf, errbuf_size, "Failed to read file: %s", path);
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}

/* ============================================================================
 * query eval
 * ============================================================================ */

int nmo_cmd_query_eval(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Parse arguments: nmo query eval "<expression>" <file>
     * or: nmo query eval --expr "<expression>" <file>
     * or: nmo query eval --stdin <file>
     */
    const char *expr_str = NULL;
    const char *file_path = NULL;
    bool use_stdin = nmo_tool_has_flag(argc, argv, "--stdin", NULL);

    const char *expr_opt = nmo_tool_find_opt_value(argc, argv, "--expr", "-e");
    if (expr_opt) {
        expr_str = expr_opt;
        file_path = nmo_tool_find_file_arg_last(argc, argv);
    } else if (use_stdin) {
        file_path = nmo_tool_find_file_arg_last(argc, argv);
    } else {
        /* First positional is expression, second is file */
        const char *args[2];
        size_t arg_count = nmo_tool_find_file_args(argc, argv, args, 2);
        if (arg_count >= 1) {
            expr_str = args[0];
        }
        if (arg_count >= 2) {
            file_path = args[1];
        }
    }

    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo query eval \"<expression>\" <file>\n");
        fprintf(stderr, "       nmo query eval --expr \"<expression>\" <file>\n");
        fprintf(stderr, "       nmo query eval --stdin <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Read expression from stdin if requested */
    char *stdin_buffer = NULL;
    if (use_stdin) {
        size_t capacity = 4096;
        size_t size = 0;
        stdin_buffer = (char *)malloc(capacity);
        if (!stdin_buffer) {
            fprintf(stderr, "Error: Out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        int c;
        while ((c = fgetc(stdin)) != EOF) {
            if (size + 1 >= capacity) {
                capacity *= 2;
                char *new_buf = (char *)realloc(stdin_buffer, capacity);
                if (!new_buf) {
                    free(stdin_buffer);
                    fprintf(stderr, "Error: Out of memory\n");
                    return NMO_CLI_EXIT_INTERNAL_ERROR;
                }
                stdin_buffer = new_buf;
            }
            stdin_buffer[size++] = (char)c;
        }
        stdin_buffer[size] = '\0';
        expr_str = stdin_buffer;
    }

    if (!expr_str || expr_str[0] == '\0') {
        free(stdin_buffer);
        fprintf(stderr, "Error: No expression specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        free(stdin_buffer);
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Set up eval context */
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_dsl_eval_context_t eval_ctx;
    memset(&eval_ctx, 0, sizeof(eval_ctx));
    eval_ctx.registry = registry;
    eval_ctx.ops = NULL;
    eval_ctx.root_type = NULL;
    eval_ctx.root_instance = NULL;
    eval_ctx.current_type = NULL;
    eval_ctx.current_instance = repo;

    /* Evaluate expression */
    nmo_dsl_value_t result = {0};
    nmo_status_t status = nmo_dsl_eval_one(registry, &eval_ctx, expr_str, &result);

    if (status != NMO_OK) {
        free(stdin_buffer);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: DSL evaluation failed: %s\n", nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output result */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        free(stdin_buffer);
        nmo_dsl_value_destroy(&result);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        add_dsl_value_to_json(doc, data, "result", &result);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "query.eval", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        char value_buf[256];
        format_dsl_value(&result, value_buf, sizeof(value_buf));
        fprintf(out, "%s\n", value_buf);
    }

    free(stdin_buffer);
    nmo_dsl_value_destroy(&result);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * query script
 * ============================================================================ */

int nmo_cmd_query_script(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Usage: nmo query script <script.nmodsl> <file> [-o <output>] */
    const char *args[2];
    size_t arg_count = nmo_tool_find_file_args(argc, argv, args, 2);

    if (arg_count < 2) {
        fprintf(stderr, "Error: Missing arguments\n");
        fprintf(stderr, "Usage: nmo query script <script.nmodsl> <file> [-o <output>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *script_path = args[0];
    const char *file_path = args[1];
    const char *output_path = nmo_tool_find_opt_value(argc, argv, "-o", "--output");

    /* Read script file */
    char errbuf[256];
    char *script_source = read_file_contents(script_path, errbuf, sizeof(errbuf));
    if (!script_source) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        free(script_source);
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Compile script */
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_dsl_compile_options_t compile_opts = { .mode = NMO_DSL_MODE_SCRIPT, .flags = 0 };
    nmo_dsl_program_t *program = NULL;

    nmo_status_t status = nmo_dsl_compile(registry, NULL, script_source, &compile_opts, &program);
    if (status != NMO_OK) {
        free(script_source);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to compile script: %s\n", nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    free(script_source);

    /* Set up eval context */
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_dsl_eval_context_t eval_ctx;
    memset(&eval_ctx, 0, sizeof(eval_ctx));
    eval_ctx.registry = registry;
    eval_ctx.ops = NULL;
    eval_ctx.root_type = NULL;
    eval_ctx.root_instance = repo;
    eval_ctx.current_type = NULL;
    eval_ctx.current_instance = repo;

    /* Execute script */
    nmo_dsl_value_t result = {0};
    status = nmo_dsl_exec(program, &eval_ctx, &result);

    nmo_dsl_program_destroy(program);

    if (status != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to execute script: %s\n", nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Save session if output path specified */
    if (output_path) {
        status = nmo_session_save(session, output_path);
        if (status != NMO_OK) {
            nmo_dsl_value_destroy(&result);
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Failed to save session: %s\n", nmo_error_string(status));
            return NMO_CLI_EXIT_IO_ERROR;
        }
    }

    /* Output result */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_dsl_value_destroy(&result);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        add_dsl_value_to_json(doc, data, "result", &result);
        if (output_path) {
            yyjson_mut_obj_add_str(doc, data, "saved_to", output_path);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "query.script", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        char value_buf[256];
        format_dsl_value(&result, value_buf, sizeof(value_buf));
        fprintf(out, "Result: %s\n", value_buf);
        if (output_path) {
            fprintf(out, "Saved to: %s\n", output_path);
        }
    }

    nmo_dsl_value_destroy(&result);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * query schema
 * ============================================================================ */

int nmo_cmd_query_schema(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Usage: nmo query schema <schema.nmodsl> */
    const char *schema_path = nmo_tool_find_file_arg(argc, argv);

    if (!schema_path) {
        fprintf(stderr, "Error: No schema file specified\n");
        fprintf(stderr, "Usage: nmo query schema <schema.nmodsl>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Read schema file */
    char errbuf[256];
    char *schema_source = read_file_contents(schema_path, errbuf, sizeof(errbuf));
    if (!schema_source) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Create context (no session needed for schema-only) */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        free(schema_source);
        fprintf(stderr, "Error: Failed to create context\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    /* Compile schema */
    nmo_dsl_compile_options_t compile_opts = { .mode = NMO_DSL_MODE_SCHEMA, .flags = 0 };
    nmo_dsl_program_t *program = NULL;

    nmo_status_t status = nmo_dsl_compile(registry, NULL, schema_source, &compile_opts, &program);
    if (status != NMO_OK) {
        free(schema_source);
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Failed to compile schema: %s\n", nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    free(schema_source);

    /* Apply schema */
    status = nmo_dsl_apply_schema(registry, program);

    nmo_dsl_program_destroy(program);

    if (status != NMO_OK) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Failed to apply schema: %s\n", nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output result */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "status", "applied");

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "query.schema", schema_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        fprintf(out, "Schema applied successfully\n");
    }

    nmo_context_release(ctx);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * query module
 * ============================================================================ */

int nmo_cmd_query_module(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Usage: nmo query module <module.nmodsl> <file> [-o <output>] */
    const char *args[2];
    size_t arg_count = nmo_tool_find_file_args(argc, argv, args, 2);

    if (arg_count < 2) {
        fprintf(stderr, "Error: Missing arguments\n");
        fprintf(stderr, "Usage: nmo query module <module.nmodsl> <file> [-o <output>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *module_path = args[0];
    const char *file_path = args[1];
    const char *output_path = nmo_tool_find_opt_value(argc, argv, "-o", "--output");

    /* Read module file */
    char errbuf[256];
    char *module_source = read_file_contents(module_path, errbuf, sizeof(errbuf));
    if (!module_source) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        free(module_source);
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Compile module */
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_dsl_compile_options_t compile_opts = { .mode = NMO_DSL_MODE_MODULE, .flags = 0 };
    nmo_dsl_program_t *program = NULL;

    nmo_status_t status = nmo_dsl_compile(registry, NULL, module_source, &compile_opts, &program);
    if (status != NMO_OK) {
        free(module_source);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to compile module: %s\n", nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    free(module_source);

    /* Set up eval context */
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_dsl_eval_context_t eval_ctx;
    memset(&eval_ctx, 0, sizeof(eval_ctx));
    eval_ctx.registry = registry;
    eval_ctx.ops = NULL;
    eval_ctx.root_type = NULL;
    eval_ctx.root_instance = repo;
    eval_ctx.current_type = NULL;
    eval_ctx.current_instance = repo;

    /* Run module */
    nmo_dsl_value_t result = {0};
    status = nmo_dsl_run_module(registry, program, &eval_ctx, NULL, &result);

    nmo_dsl_program_destroy(program);

    if (status != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to run module: %s\n", nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Save session if output path specified */
    if (output_path) {
        status = nmo_session_save(session, output_path);
        if (status != NMO_OK) {
            nmo_dsl_value_destroy(&result);
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Failed to save session: %s\n", nmo_error_string(status));
            return NMO_CLI_EXIT_IO_ERROR;
        }
    }

    /* Output result */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_dsl_value_destroy(&result);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        add_dsl_value_to_json(doc, data, "result", &result);
        if (output_path) {
            yyjson_mut_obj_add_str(doc, data, "saved_to", output_path);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "query.module", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        char value_buf[256];
        format_dsl_value(&result, value_buf, sizeof(value_buf));
        fprintf(out, "Result: %s\n", value_buf);
        if (output_path) {
            fprintf(out, "Saved to: %s\n", output_path);
        }
    }

    nmo_dsl_value_destroy(&result);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}
