/**
 * @file nmo_cmd_parameter.c
 * @brief CLI parameter command group implementation
 */

#include "nmo_cmd_parameter.h"

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "behavior/nmo_param_value.h"
#include "object/nmo_object_types.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_object_repository.h"
#include "app/nmo_save.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static int is_parameter_class(const nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return 0;
    }

    if (nmo_type_registry_is_class_derived_from(
            registry, (uint32_t)class_id, (uint32_t)NMO_CID_PARAMETER)) {
        return 1;
    }

    return class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

/**
 * @brief Allocate and format a parameter value string with dynamic sizing
 * @return Allocated string (caller must free), or NULL on allocation failure
 */
static char *format_parameter_value(const nmo_parameter_state_t *pstate,
                                     nmo_type_registry_t *registry,
                                     nmo_session_t *session,
                                     size_t *out_len) {
    if (!pstate) {
        return NULL;
    }

    size_t buf_size = 8192;
    char *buffer = (char *)malloc(buf_size);
    if (!buffer) {
        return NULL;
    }

    size_t needed = nmo_param_value_to_string(pstate, registry, session, buffer, buf_size);

    /* If truncated, reallocate and retry */
    if (needed >= buf_size) {
        size_t new_size = needed + 1;
        char *new_buffer = (char *)realloc(buffer, new_size);
        if (!new_buffer) {
            free(buffer);
            return NULL;
        }
        buffer = new_buffer;
        buf_size = new_size;
        needed = nmo_param_value_to_string(pstate, registry, session, buffer, buf_size);
    }

    if (out_len) {
        *out_len = needed;
    }
    return buffer;
}

/**
 * @brief Format hex dump of parameter buffer data (up to max_bytes)
 */
static void format_hex_dump(const uint8_t *data, size_t len, size_t max_bytes,
                             char *out_buf, size_t out_size) {
    if (!data || len == 0 || !out_buf || out_size == 0) {
        if (out_buf && out_size > 0) {
            out_buf[0] = '\0';
        }
        return;
    }

    size_t display_len = (max_bytes > 0 && len > max_bytes) ? max_bytes : len;
    size_t pos = 0;

    for (size_t i = 0; i < display_len && pos + 3 < out_size; ++i) {
        snprintf(out_buf + pos, out_size - pos, "%02x ", data[i]);
        pos += 3;
    }

    if (display_len < len && pos + 10 < out_size) {
        snprintf(out_buf + pos, out_size - pos, "... (%zu)", len);
    } else if (pos > 0 && out_buf[pos - 1] == ' ') {
        out_buf[pos - 1] = '\0';
    }
}

/* ---- parameter list: per-file handler for batch mode ---- */

static int parameter_list_single(const char *file_path,
                                 const nmo_cli_global_opts_t *global,
                                 void *user_data,
                                 yyjson_mut_doc *doc,
                                 yyjson_mut_val *data)
{
    const nmo_tool_text_output_ctx_t *text_ctx =
        (const nmo_tool_text_output_ctx_t *)user_data;

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t param_count = 0;

    if (doc && data) {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);
            if (!is_parameter_class(registry, class_id)) continue;

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            if (class_name) nmo_cli_json_add_str_safe(doc, item, "class_name", class_name);

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) nmo_cli_json_add_str_safe(doc, item, "name", name);

            yyjson_mut_arr_add_val(arr, item);
            param_count++;
        }
        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)param_count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);
    } else {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = text_ctx ? text_ctx->colorize : false;

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
            if (!is_parameter_class(registry, class_id)) continue;

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
            param_count++;
        }

        fprintf(out, "Parameters: %zu\n\n", param_count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    (void)global;
    nmo_tool_close_session(ctx, session);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_parameter_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Batch mode */
    if (global->batch_mode) {
        const char *paths[256];
        size_t count = nmo_tool_find_file_args(argc, argv, paths, 256);
        if (count == 0) {
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch parameter list <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return nmo_tool_batch_run(paths, count, global, "parameter.list",
                                  parameter_list_single, NULL);
    }

    /* Single file mode */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    if (!c.registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    size_t param_count = 0;

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            if (!is_parameter_class(c.registry, class_id)) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "name", name);
            }

            yyjson_mut_arr_add_val(arr, item);
            param_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)param_count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "parameter.list");
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
            if (!is_parameter_class(c.registry, class_id)) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-",
            };
            nmo_cli_table_add_row(&table, cells, 3);
            param_count++;
        }

        fprintf(c.out, "Parameters: %zu\n\n", param_count);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_parameter_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *id_str = NULL;
    const char *file_path = NULL;

    int non_opt = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            non_opt++;
            if (non_opt == 1) id_str = argv[i];
            else if (non_opt == 2) file_path = argv[i];
        }
    }
    if (!id_str || !file_path) {
        fprintf(stderr, "Usage: nmo parameter show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id;
    if (!nmo_tool_parse_u32(id_str, &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *obj = repo ? nmo_object_repository_find_by_id(repo, object_id) : NULL;
    if (!obj) {
        fprintf(stderr, "Error: Object #%u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_parameter_class(c.registry, cid)) {
        /* Fall back to generic object show for non-parameter objects */
        nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
        return nmo_cmd_object_show(argc, argv, global);
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_cli_class_name_from_id(c.ctx, cid);

    /* Get parameter base state via explicit struct access for each derived type */
    const nmo_parameter_state_t *pstate = NULL;
    {
        const void *raw_state = nmo_object_get_state(obj);
        if (raw_state) {
            if (cid == NMO_CID_PARAMETER) {
                pstate = (const nmo_parameter_state_t *)raw_state;
            } else if (cid == NMO_CID_PARAMETEROUT) {
                pstate = &((const nmo_parameterout_state_t *)raw_state)->base;
            } else if (cid == NMO_CID_PARAMETERLOCAL) {
                pstate = &((const nmo_parameterlocal_state_t *)raw_state)->base;
            }
            /* ParameterIn and ParameterOperation have different base chains */
        }
    }

    /* Dynamic value buffer - only for classes with valid Parameter state */
    char *value_buf = NULL;
    if (pstate && c.registry && cid != NMO_CID_PARAMETERIN) {
        value_buf = format_parameter_value(pstate, (nmo_type_registry_t *)c.registry, c.session, NULL);
    }

    /* Summary buffer with dynamic allocation */
    size_t summary_size = 8192;
    char *summary_buf = (char *)malloc(summary_size);
    if (!summary_buf) {
        free(value_buf);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    summary_buf[0] = '\0';

    if (pstate && c.registry && cid != NMO_CID_PARAMETERIN) {
        nmo_param_value_format_summary(pstate, (nmo_type_registry_t *)c.registry, c.session,
                                       summary_buf, summary_size);
    }

    /* Get owner and specialized info based on class */
    nmo_object_id_t owner_id = 0;
    nmo_object_id_t source_id = 0;
    uint32_t destination_count = 0;
    uint8_t is_shared = 0;
    nmo_guid_t type_guid = NMO_GUID_NULL;

    const void *state = nmo_object_get_state(obj);
    if (state) {
        if (cid == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *pin_state =
                (const nmo_parameterin_state_t *)state;
            owner_id = pin_state->owner_id;
            source_id = pin_state->source_id;
            is_shared = pin_state->is_shared;
            type_guid = pin_state->type_guid;
        } else if (cid == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *pout_state =
                (const nmo_parameterout_state_t *)state;
            owner_id = pout_state->owner_id;
            destination_count = pout_state->destination_count;
            /* ParameterOut inherits from Parameter, so pstate is valid */
        } else if (cid == NMO_CID_PARAMETERLOCAL) {
            const nmo_parameterlocal_state_t *plocal_state =
                (const nmo_parameterlocal_state_t *)state;
            owner_id = plocal_state->owner_id;
            /* ParameterLocal inherits from Parameter, so pstate is valid */
        }
    }

    /* For ParameterIn, get the type from its GUID */
    const char *type_name_override = NULL;
    if (cid == NMO_CID_PARAMETERIN && c.registry && !nmo_guid_is_null(type_guid)) {
        type_name_override = nmo_type_registry_guid_to_name((nmo_type_registry_t *)c.registry, type_guid);
    }

    /* Hex dump for small buffers */
    char hex_dump[512];
    hex_dump[0] = '\0';
    if (pstate && pstate->mode == CKPARAM_MODE_BUFFER &&
        pstate->buffer_data.data != NULL &&
        pstate->buffer_data.count > 0 && pstate->buffer_data.count < 64) {
        format_hex_dump((const uint8_t *)pstate->buffer_data.data,
                        pstate->buffer_data.count, 64, hex_dump, sizeof(hex_dump));
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        yyjson_mut_obj_add_uint(doc, data, "class_id", cid);
        if (class_name) yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        if (name && name[0]) nmo_cli_json_add_str_safe(doc, data, "name", name);

        /* Type information */
        const char *type_name_display = type_name_override;
        if (!type_name_display && pstate && cid != NMO_CID_PARAMETERLOCAL) {
            type_name_display = nmo_param_value_type_name(pstate, (nmo_type_registry_t *)c.registry);
        }
        if (type_name_display) {
            yyjson_mut_obj_add_str(doc, data, "type", type_name_display);
        }

        if (pstate) {
            yyjson_mut_obj_add_str(doc, data, "mode",
                                   nmo_param_mode_to_string(pstate->mode));
            if (value_buf && value_buf[0]) {
                nmo_cli_json_add_str_safe(doc, data, "value", value_buf);
            }
            if (pstate->buffer_data.data) {
                yyjson_mut_obj_add_uint(doc, data, "buffer_size",
                                        (uint64_t)pstate->buffer_data.count);
            }

            if (hex_dump[0]) {
                yyjson_mut_obj_add_str(doc, data, "hex", hex_dump);
            }
        }

        if (owner_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "owner_id", owner_id);
        }
        if (source_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "source_id", source_id);
            yyjson_mut_obj_add_bool(doc, data, "is_shared", is_shared != 0);
        }
        if (destination_count > 0) {
            yyjson_mut_obj_add_uint(doc, data, "destination_count", destination_count);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "parameter.show");
    } else {
        fprintf(c.out, "Parameter #%u\n", object_id);
        fprintf(c.out, "  Class: %s (CID %u)\n", class_name ? class_name : "?", cid);
        if (name && name[0]) fprintf(c.out, "  Name:  %s\n", name);

        /* Type information */
        const char *type_name_display = type_name_override;
        if (!type_name_display && pstate && cid != NMO_CID_PARAMETERLOCAL) {
            type_name_display = nmo_param_value_type_name(pstate, (nmo_type_registry_t *)c.registry);
        }
        if (type_name_display) {
            fprintf(c.out, "  Type:  %s\n", type_name_display);
        }

        if (pstate) {
            fprintf(c.out, "  Mode:  %s\n", nmo_param_mode_to_string(pstate->mode));
        }

        if (owner_id != 0) {
            nmo_object_t *owner_obj = repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
            if (owner_obj) {
                const char *owner_name = nmo_object_get_name(owner_obj);
                fprintf(c.out, "  Owner: #%u", owner_id);
                if (owner_name && owner_name[0]) {
                    fprintf(c.out, " (%s)", owner_name);
                }
                fprintf(c.out, "\n");
            } else {
                fprintf(c.out, "  Owner: #%u\n", owner_id);
            }
        }

        if (source_id != 0) {
            fprintf(c.out, "  Source: #%u%s\n", source_id, is_shared ? " (shared)" : " (direct)");
        }

        if (destination_count > 0) {
            fprintf(c.out, "  Destinations: %u\n", destination_count);
        }

        if (summary_buf && summary_buf[0]) {
            fprintf(c.out, "  Value: %s\n", summary_buf);
        }

        if (pstate && pstate->buffer_data.data) {
            fprintf(c.out, "  Buffer: %zu bytes\n", pstate->buffer_data.count);
        }

        if (hex_dump[0]) {
            fprintf(c.out, "  Hex:   %s\n", hex_dump);
        }
    }

    free(value_buf);
    free(summary_buf);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/**
 * @brief Dump parameter details with decoded value
 */
static void dump_parameter_details(nmo_object_t *obj,
                                    nmo_context_t *ctx,
                                    nmo_session_t *session,
                                    FILE *out) {
    if (!obj || !ctx || !out) {
        return;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t object_id = nmo_object_get_id(obj);
    nmo_class_id_t cid = nmo_object_get_class_id(obj);

    /* Skip ParameterLocal for now due to state access issues */
    if (cid == NMO_CID_PARAMETERLOCAL) {
        return;
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, cid);

    fprintf(out, "=== Parameter #%u ===\n", object_id);
    fprintf(out, "Class: %s (CID %u)\n", class_name ? class_name : "?", cid);
    if (name && name[0]) {
        fprintf(out, "Name:  %s\n", name);
    }

    const void *state = nmo_object_get_state(obj);
    if (!state) {
        fprintf(out, "No state available\n\n");
        return;
    }

    /* Parameter state is only valid for Parameter, ParameterLocal, ParameterOut */
    const nmo_parameter_state_t *pstate = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;
    const char *mode_str = "none";

    if (cid == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *pin_state = (const nmo_parameterin_state_t *)state;
        type_guid = pin_state->type_guid;
    } else if (cid != NMO_CID_PARAMETEROPERATION) {
        pstate = (const nmo_parameter_state_t *)state;
        type_guid = pstate->type_guid;
        mode_str = nmo_param_mode_to_string(pstate->mode);
    }

    const char *type_name = NULL;
    if (cid == NMO_CID_PARAMETERIN && !nmo_guid_is_null(type_guid)) {
        type_name = nmo_type_registry_guid_to_name(registry, type_guid);
    } else if (pstate) {
        type_name = nmo_param_value_type_name(pstate, registry);
    }

    if (type_name) {
        fprintf(out, "Type:  %s\n", type_name);
    }

    if (!nmo_guid_is_null(type_guid)) {
        char guid_str[64];
        nmo_guid_format(type_guid, guid_str, sizeof(guid_str));
        fprintf(out, "GUID:  %s\n", guid_str);
    }

    fprintf(out, "Mode:  %s\n", mode_str);

    /* Owner and specialized info */
    nmo_object_id_t owner_id = 0;
    nmo_object_id_t source_id = 0;
    uint32_t destination_count = 0;
    uint8_t is_shared = 0;

    if (cid == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *pin_state = (const nmo_parameterin_state_t *)state;
        owner_id = pin_state->owner_id;
        source_id = pin_state->source_id;
        is_shared = pin_state->is_shared;
    } else if (cid == NMO_CID_PARAMETEROUT) {
        const nmo_parameterout_state_t *pout_state = (const nmo_parameterout_state_t *)state;
        owner_id = pout_state->owner_id;
        destination_count = pout_state->destination_count;
    } else if (cid == NMO_CID_PARAMETERLOCAL) {
        const nmo_parameterlocal_state_t *plocal_state = (const nmo_parameterlocal_state_t *)state;
        owner_id = plocal_state->owner_id;
    }

    if (owner_id != 0) {
        nmo_object_t *owner_obj = repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
        if (owner_obj) {
            const char *owner_name = nmo_object_get_name(owner_obj);
            fprintf(out, "Owner: #%u", owner_id);
            if (owner_name && owner_name[0]) {
                fprintf(out, " (%s)", owner_name);
            }
            fprintf(out, "\n");
        } else {
            fprintf(out, "Owner: #%u\n", owner_id);
        }
    }

    if (source_id != 0) {
        fprintf(out, "Source: #%u%s\n", source_id, is_shared ? " (shared)" : " (direct)");
    }

    if (destination_count > 0) {
        fprintf(out, "Destinations: %u\n", destination_count);
    }

    /* Decoded value - only available for Parameter-derived classes */
    if (pstate) {
        char *value_buf = format_parameter_value(pstate, registry, session, NULL);
        if (value_buf) {
            fprintf(out, "Value: %s\n", value_buf);
            free(value_buf);
        }

        if (pstate->buffer_data.data) {
            fprintf(out, "Buffer: %zu bytes\n", pstate->buffer_data.count);
        }

        /* Raw hex dump (first 64 bytes) */
        if (pstate->mode == CKPARAM_MODE_BUFFER && pstate->buffer_data.data && pstate->buffer_data.count > 0) {
            fprintf(out, "Hex:   ");
            const uint8_t *data = (const uint8_t *)pstate->buffer_data.data;
            size_t display_len = (pstate->buffer_data.count > 64) ? 64 : pstate->buffer_data.count;
            for (size_t i = 0; i < display_len; ++i) {
                fprintf(out, "%02x ", data[i]);
                if ((i + 1) % 16 == 0 && i + 1 < display_len) {
                    fprintf(out, "\n       ");
                }
            }
            if (pstate->buffer_data.count > 64) {
                fprintf(out, "\n       ... (%zu bytes total)", pstate->buffer_data.count);
            }
            fprintf(out, "\n");
        }
    }

    fprintf(out, "\n");
}

int nmo_cmd_parameter_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--all",  "-a", NMO_OPT_FLAG,   "Dump all parameters"},
        {"--type", NULL, NMO_OPT_STRING, "Filter by type GUID"},
        {"--json", "-j", NMO_OPT_FLAG,   "JSON output"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool dump_all = vals[0].val.flag;
    const char *type_guid_str = vals[1].present ? vals[1].val.str : NULL;
    const char *id_str = NULL;

    if (dump_all) {
        if (r.pos_count < 1) {
            fprintf(stderr, "Usage: nmo parameter dump [--all] [--type <guid>] <id> <file>\n");
            fprintf(stderr, "       nmo parameter dump --all [--type <guid>] <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count < 2) {
            fprintf(stderr, "Usage: nmo parameter dump [--all] [--type <guid>] <id> <file>\n");
            fprintf(stderr, "       nmo parameter dump --all [--type <guid>] <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        id_str = r.pos_args[0];
    }

    uint32_t object_id = 0;
    if (!dump_all) {
        if (!id_str || !nmo_tool_parse_u32(id_str, &object_id)) {
            fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str ? id_str : "(null)");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    nmo_guid_t filter_guid = NMO_GUID_NULL;
    bool has_filter = false;
    if (type_guid_str) {
        filter_guid = nmo_guid_parse(type_guid_str);
        if (nmo_guid_is_null(filter_guid)) {
            fprintf(stderr, "Error: Invalid GUID '%s'\n", type_guid_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        has_filter = true;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

    /* JSON setup */
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *jdata = NULL;
    yyjson_mut_val *jarr = NULL;
    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        jdata = yyjson_mut_obj(doc);
        jarr = yyjson_mut_arr(doc);
    }

    size_t dump_count = 0;

    /* Collect objects to dump */
    nmo_object_t **targets = NULL;
    size_t target_count = 0;

    if (dump_all) {
        nmo_object_t **objects = NULL;
        size_t object_count = 0;
        if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
            fprintf(stderr, "Error: Failed to get objects\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        targets = objects;
        target_count = object_count;
    } else {
        nmo_object_t *obj = repo ? nmo_object_repository_find_by_id(repo, object_id) : NULL;
        if (!obj) {
            fprintf(stderr, "Error: Object #%u not found\n", object_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        nmo_class_id_t cid = nmo_object_get_class_id(obj);
        if (!is_parameter_class(c.registry, cid)) {
            fprintf(stderr, "Error: Object #%u is not a parameter (class %u)\n", object_id, cid);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        targets = &obj;
        target_count = 1;
    }

    for (size_t i = 0; i < target_count; ++i) {
        nmo_object_t *obj = targets[i];
        nmo_class_id_t class_id = nmo_object_get_class_id(obj);

        if (dump_all && !is_parameter_class(c.registry, class_id)) {
            continue;
        }

        /* Apply type GUID filter if specified */
        if (has_filter) {
            const void *st = nmo_object_get_state(obj);
            nmo_guid_t obj_type_guid = NMO_GUID_NULL;

            if (class_id == NMO_CID_PARAMETERIN) {
                const nmo_parameterin_state_t *pin = (const nmo_parameterin_state_t *)st;
                if (pin) obj_type_guid = pin->type_guid;
            } else if (st) {
                const nmo_parameter_state_t *pst = (const nmo_parameter_state_t *)st;
                obj_type_guid = pst->type_guid;
            }

            if (!nmo_guid_equals(obj_type_guid, filter_guid)) {
                if (!dump_all) {
                    fprintf(stderr, "Error: Parameter #%u type does not match filter\n", object_id);
                    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
                }
                continue;
            }
        }

        if (c.is_json) {
            /* Emit JSON object per parameter — mirrors parameter show JSON */
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_object_id_t oid = nmo_object_get_id(obj);
            yyjson_mut_obj_add_uint(doc, item, "id", oid);
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);
            const char *cn = nmo_cli_class_name_from_id(c.ctx, class_id);
            if (cn) nmo_cli_json_add_str_safe(doc, item, "class_name", cn);
            const char *nm = nmo_object_get_name(obj);
            if (nm && nm[0]) nmo_cli_json_add_str_safe(doc, item, "name", nm);

            const void *state = nmo_object_get_state(obj);
            if (state) {
                nmo_guid_t tg = NMO_GUID_NULL;
                const nmo_parameter_state_t *pstate = NULL;
                nmo_object_id_t owner_id = 0;
                nmo_object_id_t source_id = 0;
                uint8_t is_shared = 0;
                uint32_t dest_count = 0;

                if (class_id == NMO_CID_PARAMETERIN) {
                    const nmo_parameterin_state_t *pin = (const nmo_parameterin_state_t *)state;
                    tg = pin->type_guid;
                    owner_id = pin->owner_id;
                    source_id = pin->source_id;
                    is_shared = pin->is_shared;
                } else if (class_id == NMO_CID_PARAMETEROUT) {
                    const nmo_parameterout_state_t *pout = (const nmo_parameterout_state_t *)state;
                    pstate = (const nmo_parameter_state_t *)state;
                    tg = pstate->type_guid;
                    owner_id = pout->owner_id;
                    dest_count = pout->destination_count;
                } else if (class_id == NMO_CID_PARAMETERLOCAL) {
                    const nmo_parameterlocal_state_t *ploc = (const nmo_parameterlocal_state_t *)state;
                    pstate = (const nmo_parameter_state_t *)state;
                    tg = pstate->type_guid;
                    owner_id = ploc->owner_id;
                } else if (class_id != NMO_CID_PARAMETEROPERATION) {
                    pstate = (const nmo_parameter_state_t *)state;
                    tg = pstate->type_guid;
                }

                const char *tn = NULL;
                if (class_id == NMO_CID_PARAMETERIN && !nmo_guid_is_null(tg)) {
                    tn = nmo_type_registry_guid_to_name(c.registry, tg);
                } else if (pstate) {
                    tn = nmo_param_value_type_name(pstate, c.registry);
                }
                if (tn) nmo_cli_json_add_str_safe(doc, item, "type_name", tn);

                if (!nmo_guid_is_null(tg)) {
                    char gbuf[64];
                    nmo_guid_format(tg, gbuf, sizeof(gbuf));
                    nmo_cli_json_add_str_safe(doc, item, "type_guid", gbuf);
                }

                if (pstate) {
                    nmo_cli_json_add_str_safe(doc, item, "mode",
                        nmo_param_mode_to_string(pstate->mode));
                    char *vbuf = format_parameter_value(pstate, (nmo_type_registry_t *)c.registry, c.session, NULL);
                    if (vbuf) {
                        nmo_cli_json_add_str_safe(doc, item, "value", vbuf);
                        free(vbuf);
                    }
                    if (pstate->buffer_data.data) {
                        yyjson_mut_obj_add_uint(doc, item, "buffer_size",
                            (uint64_t)pstate->buffer_data.count);
                    }
                }

                if (owner_id) yyjson_mut_obj_add_uint(doc, item, "owner_id", owner_id);
                if (source_id) {
                    yyjson_mut_obj_add_uint(doc, item, "source_id", source_id);
                    yyjson_mut_obj_add_bool(doc, item, "is_shared", is_shared != 0);
                }
                if (dest_count) yyjson_mut_obj_add_uint(doc, item, "destination_count", dest_count);
            }

            yyjson_mut_arr_add_val(jarr, item);
        } else {
            dump_parameter_details(obj, c.ctx, c.session, c.out);
        }
        dump_count++;
    }

    if (c.is_json) {
        yyjson_mut_obj_add_uint(doc, jdata, "count", (uint64_t)dump_count);
        yyjson_mut_obj_add_val(doc, jdata, "parameters", jarr);
        nmo_cmd_ctx_json_end(&c, doc, jdata, "parameter.dump");
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * parameter set - Set a parameter value and save
 *
 *   nmo parameter set <param-id> <value> <file> -o <output>
 *   nmo parameter set --owner <beh-id> --name "Speed" <value> <file> -o <output>
 *   nmo parameter set --owner <beh-id> --index 2 <value> <file> -o <output>
 *   nmo parameter set --hex <param-id> <hex-value> <file> -o <output>
 *   nmo parameter set --dry-run <param-id> <value> <file>
 * ============================================================================ */

/**
 * @brief Get mutable parameter base state from an object by class ID.
 *
 * Returns NULL for ParameterIn and ParameterOperation (no buffer data).
 */
static nmo_parameter_state_t *get_mutable_pstate(nmo_object_t *obj) {
    void *raw = nmo_object_get_state(obj);
    if (!raw) return NULL;

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (cid == NMO_CID_PARAMETER) {
        return (nmo_parameter_state_t *)raw;
    } else if (cid == NMO_CID_PARAMETEROUT) {
        return &((nmo_parameterout_state_t *)raw)->base;
    } else if (cid == NMO_CID_PARAMETERLOCAL) {
        return &((nmo_parameterlocal_state_t *)raw)->base;
    }
    return NULL;
}

/**
 * @brief Parse a hex string into a byte buffer.
 * Accepts: "4142ff" or "41 42 ff" (spaces optional).
 * @return Number of bytes written, or (size_t)-1 on error.
 */
static size_t parse_hex_bytes(const char *hex_str, uint8_t *out, size_t out_cap) {
    size_t written = 0;
    const char *p = hex_str;

    while (*p) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) {
            return (size_t)-1;
        }
        if (written >= out_cap) {
            return (size_t)-1;
        }

        unsigned int byte_val = 0;
        if (sscanf(p, "%2x", &byte_val) != 1) {
            return (size_t)-1;
        }
        out[written++] = (uint8_t)byte_val;
        p += 2;
    }
    return written;
}

/**
 * @brief Find a parameter by owner behavior + name.
 *
 * Searches the behavior's in_parameters, out_parameters, and local_parameters
 * arrays.  ParameterIn objects are skipped (no buffer data).
 */
static nmo_object_t *find_param_by_owner_name(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    nmo_object_t *owner_obj,
    const char *param_name)
{
    const void *owner_state = nmo_object_get_state(owner_obj);
    if (!owner_state) return NULL;

    const nmo_behavior_state_t *bstate = (const nmo_behavior_state_t *)owner_state;

    const nmo_array_t *arrays[] = {
        &bstate->in_parameters,
        &bstate->out_parameters,
        &bstate->local_parameters,
    };

    for (int a = 0; a < 3; a++) {
        const nmo_array_t *arr = arrays[a];
        if (!arr->data || arr->count == 0) continue;
        const nmo_object_id_t *ids = (const nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_t *pobj = nmo_object_repository_find_by_id(repo, ids[i]);
            if (!pobj) continue;

            nmo_class_id_t pcid = nmo_object_get_class_id(pobj);
            if (pcid == NMO_CID_PARAMETERIN) continue;
            if (!is_parameter_class(registry, pcid)) continue;

            const char *pname = nmo_object_get_name(pobj);
            if (pname && strcmp(pname, param_name) == 0) {
                return pobj;
            }
        }
    }
    return NULL;
}

/**
 * @brief Find a parameter by owner behavior + flat index.
 *
 * Index counts across out_parameters then local_parameters (skips
 * in_parameters since they have no buffer data).
 */
static nmo_object_t *find_param_by_owner_index(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    nmo_object_t *owner_obj,
    uint32_t flat_index)
{
    const void *owner_state = nmo_object_get_state(owner_obj);
    if (!owner_state) return NULL;

    const nmo_behavior_state_t *bstate = (const nmo_behavior_state_t *)owner_state;

    const nmo_array_t *arrays[] = {
        &bstate->out_parameters,
        &bstate->local_parameters,
    };

    uint32_t running = 0;
    for (int a = 0; a < 2; a++) {
        const nmo_array_t *arr = arrays[a];
        if (!arr->data || arr->count == 0) continue;
        const nmo_object_id_t *ids = (const nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_t *pobj = nmo_object_repository_find_by_id(repo, ids[i]);
            if (!pobj) continue;
            nmo_class_id_t pcid = nmo_object_get_class_id(pobj);
            if (!is_parameter_class(registry, pcid)) continue;
            if (pcid == NMO_CID_PARAMETERIN) continue;

            if (running == flat_index) {
                return pobj;
            }
            running++;
        }
    }
    return NULL;
}

int nmo_cmd_parameter_set(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--owner",   "-b", NMO_OPT_STRING, "Owner behavior/object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Parameter name within owner"},
        {"--index",   "-i", NMO_OPT_UINT,   "Parameter index within owner"},
        {"--hex",     NULL, NMO_OPT_FLAG,   "Value is raw hex bytes"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Show old/new without saving"},
    };
    enum { OPT_OUTPUT, OPT_OWNER, OPT_NAME, OPT_INDEX, OPT_HEX, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *owner_str   = vals[OPT_OWNER].present  ? vals[OPT_OWNER].val.str  : NULL;
    const char *name_str    = vals[OPT_NAME].present   ? vals[OPT_NAME].val.str   : NULL;
    bool has_index          = vals[OPT_INDEX].present;
    uint32_t param_index    = has_index ? vals[OPT_INDEX].val.u : 0;
    bool hex_mode           = vals[OPT_HEX].present && vals[OPT_HEX].val.flag;
    bool dry_run            = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Determine positional args layout */
    bool owner_mode = (owner_str != NULL);
    const char *id_str = NULL;
    const char *value_str = NULL;
    const char *file_path = NULL;

    if (owner_mode) {
        /* --owner <beh-id> [--name|--index] <value> <file> */
        if (r.pos_count < 2) {
            fprintf(stderr, "Usage: nmo parameter set --owner <beh-id> "
                    "[--name <name> | --index <n>] <value> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        value_str = r.pos_args[0];
        file_path = r.pos_args[r.pos_count - 1];
    } else {
        /* <param-id> <value> <file> */
        if (r.pos_count < 3) {
            fprintf(stderr, "Usage: nmo parameter set <param-id> <value> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        id_str    = r.pos_args[0];
        value_str = r.pos_args[1];
        file_path = r.pos_args[r.pos_count - 1];
    }

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    if (!c.registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *param_obj = NULL;

    /* ---- Locate parameter ---- */
    if (owner_mode) {
        uint32_t owner_id;
        if (!nmo_tool_parse_u32(owner_str, &owner_id)) {
            fprintf(stderr, "Error: Invalid owner ID '%s'\n", owner_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        nmo_object_t *owner_obj = repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
        if (!owner_obj) {
            fprintf(stderr, "Error: Owner object #%u not found\n", owner_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
        }

        if (name_str) {
            param_obj = find_param_by_owner_name(repo, c.registry, owner_obj, name_str);
            if (!param_obj) {
                fprintf(stderr, "Error: No parameter named '%s' in owner #%u\n",
                        name_str, owner_id);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
            }
        } else if (has_index) {
            param_obj = find_param_by_owner_index(repo, c.registry, owner_obj, param_index);
            if (!param_obj) {
                fprintf(stderr, "Error: Parameter index %u out of range in owner #%u\n",
                        param_index, owner_id);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
            }
        } else {
            fprintf(stderr, "Error: --owner requires --name or --index\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    } else {
        /* Direct ID mode */
        uint32_t param_id;
        if (!nmo_tool_parse_u32(id_str, &param_id)) {
            fprintf(stderr, "Error: Invalid parameter ID '%s'\n", id_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        param_obj = repo ? nmo_object_repository_find_by_id(repo, param_id) : NULL;
        if (!param_obj) {
            fprintf(stderr, "Error: Object #%u not found\n", param_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
        }
        nmo_class_id_t cid = nmo_object_get_class_id(param_obj);
        if (!is_parameter_class(c.registry, cid)) {
            fprintf(stderr, "Error: Object #%u is not a parameter (class %u)\n",
                    param_id, cid);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (cid == NMO_CID_PARAMETERIN) {
            fprintf(stderr, "Error: ParameterIn objects have no buffer data to set\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    /* ---- Get mutable parameter state ---- */
    nmo_parameter_state_t *pstate = get_mutable_pstate(param_obj);
    if (!pstate) {
        fprintf(stderr, "Error: Cannot access parameter state\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* ---- Format old value ---- */
    char *old_value_str = format_parameter_value(pstate,
        (nmo_type_registry_t *)c.registry, c.session, NULL);

    const char *param_name = nmo_object_get_name(param_obj);
    const char *type_name = nmo_param_value_type_name(pstate,
        (nmo_type_registry_t *)c.registry);

    /* ---- Parse and write new value ---- */
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    if (hex_mode) {
        /* Raw hex mode: write bytes directly into buffer */
        if (pstate->mode != CKPARAM_MODE_BUFFER) {
            fprintf(stderr, "Error: --hex only supported for MODE_BUFFER parameters\n");
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        size_t max_len = strlen(value_str) / 2 + 1;
        uint8_t *hex_buf = (uint8_t *)malloc(max_len);
        if (!hex_buf) {
            fprintf(stderr, "Error: Out of memory\n");
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        size_t hex_len = parse_hex_bytes(value_str, hex_buf, max_len);
        if (hex_len == (size_t)-1) {
            fprintf(stderr, "Error: Invalid hex string '%s'\n", value_str);
            free(hex_buf);
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        if (pstate->buffer_data.data && hex_len <= pstate->buffer_data.count) {
            memcpy(pstate->buffer_data.data, hex_buf, hex_len);
            if (hex_len < pstate->buffer_data.count) {
                memset((uint8_t *)pstate->buffer_data.data + hex_len, 0,
                       pstate->buffer_data.count - hex_len);
            }
        } else if (pstate->buffer_data.data && hex_len > pstate->buffer_data.count) {
            fprintf(stderr, "Error: Hex data (%zu bytes) exceeds buffer size (%zu bytes)\n",
                    hex_len, pstate->buffer_data.count);
            free(hex_buf);
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        } else {
            fprintf(stderr, "Error: No buffer data in parameter\n");
            free(hex_buf);
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        free(hex_buf);
    } else if (pstate->mode == CKPARAM_MODE_BUFFER) {
        /* Look up type descriptor for string parsing */
        const nmo_type_descriptor_t *type_desc =
            nmo_type_registry_find_by_guid(c.registry, pstate->type_guid);
        if (!type_desc) {
            fprintf(stderr, "Error: Unknown parameter type\n");
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        if (!pstate->buffer_data.data || pstate->buffer_data.count == 0) {
            fprintf(stderr, "Error: Parameter has no buffer data\n");
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        size_t buf_size = type_desc->size > 0 ? type_desc->size : pstate->buffer_data.count;
        uint8_t *tmp_buf = (uint8_t *)calloc(1, buf_size);
        if (!tmp_buf) {
            fprintf(stderr, "Error: Out of memory\n");
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        nmo_status_t parse_rc = nmo_type_value_from_string(
            tmp_buf, type_desc, c.registry, value_str);
        if (parse_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to parse '%s' as %s: %s\n",
                    value_str,
                    type_name ? type_name : "unknown",
                    nmo_error_string(parse_rc));
            free(tmp_buf);
            free(old_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        size_t copy_len = buf_size < pstate->buffer_data.count
                        ? buf_size : pstate->buffer_data.count;
        memcpy(pstate->buffer_data.data, tmp_buf, copy_len);
        free(tmp_buf);
    } else if (pstate->mode == CKPARAM_MODE_OBJECT) {
        /* Parse as object reference: #<id> or name */
        if (value_str[0] == '#') {
            uint32_t ref_id;
            if (!nmo_tool_parse_u32(value_str + 1, &ref_id)) {
                fprintf(stderr, "Error: Invalid object ID '%s'\n", value_str);
                free(old_value_str);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
            }
            pstate->object_id = ref_id;
        } else {
            nmo_object_t *ref_obj = nmo_object_repository_find_by_name(repo, value_str);
            if (!ref_obj) {
                uint32_t ref_id;
                if (nmo_tool_parse_u32(value_str, &ref_id)) {
                    pstate->object_id = ref_id;
                } else {
                    fprintf(stderr, "Error: Object '%s' not found\n", value_str);
                    free(old_value_str);
                    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
                }
            } else {
                pstate->object_id = nmo_object_get_id(ref_obj);
            }
        }
    } else {
        fprintf(stderr, "Error: Unsupported parameter mode '%s'\n",
                nmo_param_mode_to_string(pstate->mode));
        free(old_value_str);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* ---- Format new value ---- */
    char *new_value_str = format_parameter_value(pstate,
        (nmo_type_registry_t *)c.registry, c.session, NULL);

    /* ---- Output ---- */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            free(old_value_str);
            free(new_value_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "id", nmo_object_get_id(param_obj));
        if (param_name && param_name[0])
            nmo_cli_json_add_str_safe(doc, data, "name", param_name);
        if (type_name)
            nmo_cli_json_add_str_safe(doc, data, "type", type_name);
        nmo_cli_json_add_str_safe(doc, data, "mode",
            nmo_param_mode_to_string(pstate->mode));
        if (old_value_str)
            nmo_cli_json_add_str_safe(doc, data, "old_value", old_value_str);
        if (new_value_str)
            nmo_cli_json_add_str_safe(doc, data, "new_value", new_value_str);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "parameter.set");
    } else {
        fprintf(c.out, "Parameter #%u", nmo_object_get_id(param_obj));
        if (param_name && param_name[0])
            fprintf(c.out, " (%s)", param_name);
        fprintf(c.out, "\n");
        if (type_name)
            fprintf(c.out, "  Type:  %s\n", type_name);
        fprintf(c.out, "  Old:   %s\n", old_value_str ? old_value_str : "(none)");
        fprintf(c.out, "  New:   %s\n", new_value_str ? new_value_str : "(none)");

        if (dry_run) {
            fprintf(c.out, "  (dry run - not saved)\n");
        }
    }

    /* ---- Save ---- */
    if (!dry_run && output_path) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
            exit_code = NMO_CLI_EXIT_IO_ERROR;
        } else if (!c.is_json) {
            fprintf(c.out, "Saved to: %s\n", output_path);
        }
    }

    free(old_value_str);
    free(new_value_str);
    return nmo_cmd_ctx_done(&c, exit_code);
}

