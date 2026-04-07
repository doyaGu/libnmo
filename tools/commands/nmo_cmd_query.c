/**
 * @file nmo_cmd_query.c
 * @brief CLI query command group implementation (DSL integration)
 */

#include "nmo_cmd_query.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "dsl/nmo_dsl.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
            nmo_core_dsl_format(value, buf, sizeof(buf));
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
                            nmo_core_dsl_format(&elem, elem_buf, sizeof(elem_buf));
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
            nmo_core_dsl_format(value, buf, sizeof(buf));
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
        /* Collect positional args, skipping known --key value pairs */
        const char *positionals[3];
        size_t pos_count = 0;
        for (int i = 1; i < argc && pos_count < 3; ++i) {
            if (argv[i][0] == '-') {
                /* Skip options that consume a value */
                if (strcmp(argv[i], "--object") == 0 ||
                    strcmp(argv[i], "--expr") == 0 ||
                    strcmp(argv[i], "--format") == 0 ||
                    strcmp(argv[i], "-e") == 0 ||
                    strcmp(argv[i], "-o") == 0 ||
                    strcmp(argv[i], "-f") == 0) {
                    ++i; /* skip value */
                }
                continue;
            }
            positionals[pos_count++] = argv[i];
        }
        if (pos_count >= 1) {
            expr_str = positionals[0];
        }
        if (pos_count >= 2) {
            file_path = positionals[1];
        }
    }

    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo query eval \"<expression>\" <file>\n");
        fprintf(stderr, "       nmo query eval --expr \"<expression>\" <file>\n");
        fprintf(stderr, "       nmo query eval --stdin <file>\n");
        fprintf(stderr, "       nmo query eval --object <id|name> \"<expression>\" <file>\n");
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

    /* Check for --object flag to set type context */
    const char *obj_opt = nmo_tool_find_opt_value(argc, argv, "--object", NULL);
    nmo_object_t *target_obj = NULL;
    if (obj_opt) {
        /* Try as numeric ID first, then as name */
        char *end = NULL;
        long id = strtol(obj_opt, &end, 10);
        if (end && end != obj_opt && *end == '\0' && id > 0) {
            target_obj = nmo_object_repository_find_by_id(repo, (nmo_object_id_t)id);
        }
        if (!target_obj) {
            target_obj = nmo_object_repository_find_by_name(repo, obj_opt);
        }
        if (!target_obj) {
            free(stdin_buffer);
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Object not found: %s\n", obj_opt);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        /* Resolve type descriptor for this object */
        nmo_guid_t type_guid = nmo_object_get_type_guid(target_obj);
        const nmo_type_descriptor_t *obj_type = NULL;
        if (!nmo_guid_is_null(type_guid)) {
            obj_type = nmo_type_registry_find_by_guid(registry, type_guid);
        }
        if (!obj_type) {
            obj_type = nmo_type_registry_find_by_class_id_inherited(
                registry, nmo_object_get_class_id(target_obj));
        }

        if (obj_type && nmo_type_has_reflection(obj_type)) {
            const void *state = nmo_object_get_state(target_obj);
            eval_ctx.root_type = obj_type;
            eval_ctx.root_instance = (void *)state;
            eval_ctx.current_type = obj_type;
            eval_ctx.current_instance = state;
        } else {
            free(stdin_buffer);
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Object '%s' has no reflection data\n", obj_opt);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    } else {
        eval_ctx.root_type = NULL;
        eval_ctx.root_instance = NULL;
        eval_ctx.current_type = NULL;
        eval_ctx.current_instance = repo;
    }

    /* Evaluate expression */
    nmo_dsl_value_t result = {0};
    nmo_status_t status = nmo_dsl_eval_one(registry, &eval_ctx, expr_str, &result);

    if (status != NMO_OK) {
        free(stdin_buffer);
        /* Capture detailed error before closing session (which may clear it) */
        char detail[256];
        size_t detail_len = nmo_last_error_message_copy(detail, sizeof(detail));
        nmo_tool_close_session(ctx, session);
        if (detail_len > 0) {
            fprintf(stderr, "Error: %s\n", detail);
        } else {
            fprintf(stderr, "Error: DSL evaluation failed: %s\n", nmo_error_string(status));
        }
        if (!obj_opt) {
            fprintf(stderr, "Hint: Use --object <id|name> to evaluate in the context of an object\n");
        }
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output result */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        free(stdin_buffer);
        nmo_dsl_value_destroy(&result);
        nmo_tool_close_session(ctx, session);
        return out_rc;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        add_dsl_value_to_json(doc, data, "result", &result);

        nmo_cli_json_write_enveloped_and_free(doc, data, "query.eval", file_path, c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        char value_buf[256];
        nmo_core_dsl_format(&result, value_buf, sizeof(value_buf));
        fprintf(c.out, "%s\n", value_buf);
    }

    free(stdin_buffer);
    nmo_dsl_value_destroy(&result);
    nmo_tool_close_session(ctx, session);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_dsl_value_destroy(&result);
        nmo_tool_close_session(ctx, session);
        return out_rc;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        add_dsl_value_to_json(doc, data, "result", &result);
        if (output_path) {
            yyjson_mut_obj_add_str(doc, data, "saved_to", output_path);
        }

        nmo_cli_json_write_enveloped_and_free(doc, data, "query.script", file_path, c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        char value_buf[256];
        nmo_core_dsl_format(&result, value_buf, sizeof(value_buf));
        fprintf(c.out, "Result: %s\n", value_buf);
        if (output_path) {
            fprintf(c.out, "Saved to: %s\n", output_path);
        }
    }

    nmo_dsl_value_destroy(&result);
    nmo_tool_close_session(ctx, session);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_context_release(ctx);
        return out_rc;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "status", "applied");

        nmo_cli_json_write_enveloped_and_free(doc, data, "query.schema", schema_path, c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        fprintf(c.out, "Schema applied successfully\n");
    }

    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_dsl_value_destroy(&result);
        nmo_tool_close_session(ctx, session);
        return out_rc;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        add_dsl_value_to_json(doc, data, "result", &result);
        if (output_path) {
            yyjson_mut_obj_add_str(doc, data, "saved_to", output_path);
        }

        nmo_cli_json_write_enveloped_and_free(doc, data, "query.module", file_path, c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        char value_buf[256];
        nmo_core_dsl_format(&result, value_buf, sizeof(value_buf));
        fprintf(c.out, "Result: %s\n", value_buf);
        if (output_path) {
            fprintf(c.out, "Saved to: %s\n", output_path);
        }
    }

    nmo_dsl_value_destroy(&result);
    nmo_tool_close_session(ctx, session);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

