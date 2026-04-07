/**
 * @file nmo_cmd_parameter.c
 * @brief CLI parameter command group implementation
 */

#include "nmo_cmd_parameter.h"

#include "nmo_cmd_object.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_context.h"
#include "app/nmo_param_value.h"
#include "object/nmo_object_types.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int is_parameter_class(nmo_type_registry_t *registry, nmo_class_id_t class_id) {
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

int nmo_cmd_parameter_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo parameter list <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Type registry unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
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

    size_t param_count = 0;

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            if (!is_parameter_class(registry, class_id)) {
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
            param_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)param_count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cli_json_write_enveloped_and_free(doc, data, "parameter.list", file_path, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
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
            if (!is_parameter_class(registry, class_id)) {
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
            param_count++;
        }

        fprintf(out, "Parameters: %zu\n\n", param_count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
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

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *obj = repo ? nmo_object_repository_find_by_id(repo, object_id) : NULL;
    if (!obj) {
        fprintf(stderr, "Error: Object #%u not found\n", object_id);
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_parameter_class(registry, cid)) {
        /* Fall back to generic object show for non-parameter objects */
        nmo_tool_close_session(ctx, session);
        return nmo_cmd_object_show(argc, argv, global);
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, cid);

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
    if (pstate && registry && cid != NMO_CID_PARAMETERIN) {
        value_buf = format_parameter_value(pstate, registry, session, NULL);
    }

    /* Summary buffer with dynamic allocation */
    size_t summary_size = 8192;
    char *summary_buf = (char *)malloc(summary_size);
    if (!summary_buf) {
        free(value_buf);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    summary_buf[0] = '\0';

    if (pstate && registry && cid != NMO_CID_PARAMETERIN) {
        nmo_param_value_format_summary(pstate, registry, session,
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
    if (cid == NMO_CID_PARAMETERIN && registry && !nmo_guid_is_null(type_guid)) {
        type_name_override = nmo_type_registry_guid_to_name(registry, type_guid);
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

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        yyjson_mut_obj_add_uint(doc, data, "class_id", cid);
        if (class_name) yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        if (name && name[0]) nmo_cli_json_add_str_safe(doc, data, "name", name);

        /* Type information */
        const char *type_name_display = type_name_override;
        if (!type_name_display && pstate && cid != NMO_CID_PARAMETERLOCAL) {
            type_name_display = nmo_param_value_type_name(pstate, registry);
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

        nmo_cli_json_write_enveloped_and_free(doc, data, "parameter.show", file_path,
            out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        fprintf(out, "Parameter #%u\n", object_id);
        fprintf(out, "  Class: %s (CID %u)\n", class_name ? class_name : "?", cid);
        if (name && name[0]) fprintf(out, "  Name:  %s\n", name);

        /* Type information */
        const char *type_name_display = type_name_override;
        if (!type_name_display && pstate && cid != NMO_CID_PARAMETERLOCAL) {
            type_name_display = nmo_param_value_type_name(pstate, registry);
        }
        if (type_name_display) {
            fprintf(out, "  Type:  %s\n", type_name_display);
        }

        if (pstate) {
            fprintf(out, "  Mode:  %s\n", nmo_param_mode_to_string(pstate->mode));
        }

        if (owner_id != 0) {
            nmo_object_t *owner_obj = repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
            if (owner_obj) {
                const char *owner_name = nmo_object_get_name(owner_obj);
                fprintf(out, "  Owner: #%u", owner_id);
                if (owner_name && owner_name[0]) {
                    fprintf(out, " (%s)", owner_name);
                }
                fprintf(out, "\n");
            } else {
                fprintf(out, "  Owner: #%u\n", owner_id);
            }
        }

        if (source_id != 0) {
            fprintf(out, "  Source: #%u%s\n", source_id, is_shared ? " (shared)" : " (direct)");
        }

        if (destination_count > 0) {
            fprintf(out, "  Destinations: %u\n", destination_count);
        }

        if (summary_buf && summary_buf[0]) {
            fprintf(out, "  Value: %s\n", summary_buf);
        }

        if (pstate && pstate->buffer_data.data) {
            fprintf(out, "  Buffer: %zu bytes\n", pstate->buffer_data.count);
        }

        if (hex_dump[0]) {
            fprintf(out, "  Hex:   %s\n", hex_dump);
        }
    }

    free(value_buf);
    free(summary_buf);

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
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
    bool dump_all = false;
    const char *type_guid_str = NULL;
    const char *id_str = NULL;
    const char *file_path = NULL;

    /* Parse arguments */
    int non_opt = 0;
    for (int i = 1; i < argc; ++i) {
        if (nmo_tool_streq_ci(argv[i], "--all")) {
            dump_all = true;
        } else if (nmo_tool_streq_ci(argv[i], "--type") && i + 1 < argc) {
            type_guid_str = argv[++i];
        } else if (argv[i][0] != '-') {
            non_opt++;
            if (dump_all) {
                if (non_opt == 1) file_path = argv[i];
            } else {
                if (non_opt == 1) id_str = argv[i];
                else if (non_opt == 2) file_path = argv[i];
            }
        }
    }

    if (!file_path) {
        fprintf(stderr, "Usage: nmo parameter dump [--all] [--type <guid>] <id> <file>\n");
        fprintf(stderr, "       nmo parameter dump --all [--type <guid>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
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

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    if (dump_all) {
        nmo_object_t **objects = NULL;
        size_t object_count = 0;
        if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
            nmo_tool_close_session(ctx, session);
            nmo_cli_close_output_stream(global, out);
            fprintf(stderr, "Error: Failed to get objects\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            if (!is_parameter_class(registry, class_id)) {
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
                    continue;
                }
            }

            dump_parameter_details(obj, ctx, session, out);
        }
    } else {
        nmo_object_t *obj = repo ? nmo_object_repository_find_by_id(repo, object_id) : NULL;
        if (!obj) {
            fprintf(stderr, "Error: Object #%u not found\n", object_id);
            nmo_tool_close_session(ctx, session);
            nmo_cli_close_output_stream(global, out);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        nmo_class_id_t cid = nmo_object_get_class_id(obj);
        if (!is_parameter_class(registry, cid)) {
            fprintf(stderr, "Error: Object #%u is not a parameter (class %u)\n", object_id, cid);
            nmo_tool_close_session(ctx, session);
            nmo_cli_close_output_stream(global, out);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        /* Apply type GUID filter if specified */
        if (has_filter) {
            const void *st = nmo_object_get_state(obj);
            nmo_guid_t obj_type_guid = NMO_GUID_NULL;

            if (cid == NMO_CID_PARAMETERIN) {
                const nmo_parameterin_state_t *pin = (const nmo_parameterin_state_t *)st;
                if (pin) obj_type_guid = pin->type_guid;
            } else if (st) {
                const nmo_parameter_state_t *pst = (const nmo_parameter_state_t *)st;
                obj_type_guid = pst->type_guid;
            }

            if (!nmo_guid_equals(obj_type_guid, filter_guid)) {
                fprintf(stderr, "Error: Parameter #%u type does not match filter\n", object_id);
                nmo_tool_close_session(ctx, session);
                nmo_cli_close_output_stream(global, out);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        }

        dump_parameter_details(obj, ctx, session, out);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

