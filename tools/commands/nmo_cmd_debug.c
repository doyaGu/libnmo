/**
 * @file nmo_cmd_debug.c
 * @brief CLI debug command group implementation (non-interactive diagnostics)
 */

#include "nmo_cmd_debug.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_tool_common.h"
#include "../nmo_tool_session.h"
#include "../nmo_opt.h"

#include "edit/nmo_edit_plan.h"
#include "extension/nmo_behavior_registry.h"
#include "edit/nmo_probe_analyzer.h"
#include "nmo.h"
#include "document/nmo_document_stats.h"
#include "document/nmo_document_save.h"
#include "runtime/nmo_context.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* Records built while iterating objects, before the report around them. */
typedef struct debug_item_list {
    nmo_cli_record_t **items;
    size_t count;
    size_t capacity;
    bool out_of_memory;
} debug_item_list_t;

typedef struct nmo_debug_probe_args {
    const char *kind;
    nmo_object_id_t behavior_id;
    nmo_object_id_t remove_link_id;
    nmo_object_id_t from_io_id;
    nmo_object_id_t to_io_id;
    nmo_object_id_t message_node_id;
    nmo_object_id_t write_node_id;
    nmo_object_id_t write_operation_id;
    nmo_object_id_t write_link_id;
    nmo_object_id_t parameter_id;
    nmo_object_id_t dataarray_id;
    uint32_t data_row;
    uint32_t data_col;
    bool has_data_row;
    bool has_data_col;
    uint32_t delay;
    bool has_delay;
    const char *name;
    const char *text;
    const char *selector_mode;          /**< static name; NULL or "" when unset */
    const char *selector_status;        /**< static name; NULL or "" when unset */
    char *selector_rejection_code;      /**< malloc'd; NULL or "" when unset */
    nmo_object_id_t selector_selected_node_id;
    nmo_object_id_t selector_selected_link_id;
    nmo_object_id_t selector_selected_operation_id;
    struct debug_probe_selector_candidate {
        nmo_object_id_t node_id;
        nmo_object_id_t parent_id;
        nmo_object_id_t boundary_behavior_id;
        nmo_object_id_t link_id;
        nmo_object_id_t operation_id;
        nmo_object_id_t from_io_id;
        nmo_object_id_t to_io_id;
        bool has_delay;
        uint32_t delay;
        nmo_object_id_t source_parameter_id;
        nmo_object_id_t value_parameter_id;
        nmo_object_id_t dataarray_id;
        nmo_guid_t column_type_guid;
        double confidence;
        nmo_guid_t bb_guid;
        char *proto_name;               /**< malloc'd; NULL when unset */
        const char *role;               /**< static role name */
        char *rejection_code;           /**< malloc'd; NULL or "" when unset */
    } selector_candidates[64];
    size_t selector_candidate_count;
    nmo_probe_safe_insertion_t selector_safe_insertion;
    bool has_selector_analysis;
    nmo_probe_selector_result_t selector_analysis;
    nmo_edit_report_t report;
} nmo_debug_probe_args_t;

typedef struct debug_probe_kind_spec {
    const char *kind;
    nmo_guid_t bb_guid;
    const char *input_handle;
    const char *output_handle;
    const char *text_handle;
    bool connects_parameter;
    bool logs_data_cell;
} debug_probe_kind_spec_t;

static const debug_probe_kind_spec_t debug_probe_kind_specs[] = {
    {
        "2d-text",
        NMO_GUID_INIT(0x055B29FEu, 0x662D5CA0u),
        "input:On",
        "output:Exit On",
        "input_param:Text",
        false,
        false,
    },
    {
        "console",
        NMO_GUID_INIT(0x18655B3Fu, 0x68291DC3u),
        "input:In",
        "output:Out",
        "input_param:String",
        false,
        false,
    },
    {
        "debug-output",
        NMO_GUID_INIT(0x18655B3Fu, 0x68291DC3u),
        "input:In",
        "output:Out",
        "input_param:String",
        false,
        false,
    },
    {
        "message-logger",
        NMO_GUID_INIT(0x18655B3Fu, 0x68291DC3u),
        "input:In",
        "output:Out",
        "input_param:String",
        false,
        false,
    },
    {
        "parameter-logger",
        NMO_GUID_INIT(0x18655B3Fu, 0x68291DC3u),
        "input:In",
        "output:Out",
        "input_param:String",
        true,
        false,
    },
    {
        "data-cell-logger",
        NMO_GUID_INIT(0x18655B3Fu, 0x68291DC3u),
        "input:In",
        "output:Out",
        "input_param:String",
        false,
        true,
    },
    {
        "control-marker",
        NMO_GUID_INIT(0x302561C4u, 0x0D282980u),
        "input:In 0",
        "output:Out 0",
        NULL,
        false,
        false,
    },
};

static const debug_probe_kind_spec_t *debug_probe_find_kind(const char *kind);
static bool debug_probe_parse_u32_arg(const char *text, uint32_t *out_value);
static nmo_status_t debug_probe_infer_removed_link_endpoints(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args);
static nmo_status_t debug_probe_reconcile_saved_link_ids(
    nmo_cmd_ctx_t *ctx,
    const char *output_path,
    nmo_debug_probe_args_t *args);
static bool debug_probe_add_selector_diagnostics(
    nmo_cli_record_t *rec,
    const nmo_debug_probe_args_t *args);

/*
 * Emit a record built by this file. An incomplete record (`ok` false) is an
 * internal error.
 */
static int debug_emit(nmo_cmd_ctx_t *c,
                      nmo_cli_record_t *rec,
                      bool ok,
                      const char *cmd_name,
                      int key_width)
{
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(c, rec, cmd_name, key_width, c->colorize);
}

typedef struct debug_probe_report_json_ctx {
    const nmo_edit_report_t *report;
    bool dry_run;
} debug_probe_report_json_ctx_t;

static bool debug_probe_edit_report_json(yyjson_mut_doc *doc,
                                         yyjson_mut_val *obj,
                                         const void *data)
{
    const debug_probe_report_json_ctx_t *ctx =
        (const debug_probe_report_json_ctx_t *)data;
    nmo_cli_edit_report_add_schema_v2_json(doc, obj, ctx->report, ctx->dry_run);
    return true;
}

static void debug_item_list_push(debug_item_list_t *list,
                                 nmo_cli_record_t *item,
                                 bool ok)
{
    if (!ok) {
        nmo_cli_record_free(item);
        list->out_of_memory = true;
        return;
    }
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2u : 64u;
        nmo_cli_record_t **grown = (nmo_cli_record_t **)realloc(
            list->items, capacity * sizeof(*grown));
        if (!grown) {
            nmo_cli_record_free(item);
            list->out_of_memory = true;
            return;
        }
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count++] = item;
}

/*
 * Move every item into a new `key` array on `rec`, printed in text as a table
 * with `columns`. The list is emptied either way; `rec` NULL just frees it.
 */
static bool debug_item_list_to_array(debug_item_list_t *list,
                                     nmo_cli_record_t *rec,
                                     const char *key,
                                     const nmo_cli_table_col_t *columns,
                                     size_t column_count)
{
    nmo_cli_record_array_t *array =
        rec ? nmo_cli_record_array(rec, key, NULL) : NULL;
    bool ok = array != NULL && !list->out_of_memory;
    if (ok && columns) {
        ok = nmo_cli_record_array_set_table(array, columns, column_count);
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (ok) {
            ok = nmo_cli_record_array_add(array, list->items[i]);
        } else {
            nmo_cli_record_free(list->items[i]);
        }
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
    return ok;
}

static int debug_probe_parse(int argc,
                             char **argv,
                             nmo_debug_probe_args_t *args,
                             const char **out_input_path,
                             const char **out_output_path,
                             bool *out_dry_run)
{
    if (argc < 2 || argv == NULL || args == NULL || out_input_path == NULL ||
        out_output_path == NULL || out_dry_run == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    memset(args, 0, sizeof(*args));
    args->kind = argv[1];
    args->name = "nmo debug probe";
    *out_input_path = NULL;
    *out_output_path = NULL;
    *out_dry_run = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--behavior") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --behavior '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->behavior_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--remove-link") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --remove-link '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->remove_link_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--from-io") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --from-io '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->from_io_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--to-io") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --to-io '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->to_io_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--message-node") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --message-node '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->message_node_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--write-node") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --write-node '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->write_node_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--write-operation") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --write-operation '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->write_operation_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--write-link") == 0 && i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --write-link '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->write_link_id = (nmo_object_id_t)parsed;
        } else if ((strcmp(argv[i], "--parameter") == 0 ||
                    strcmp(argv[i], "--source-param") == 0) &&
                   i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --parameter '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->parameter_id = (nmo_object_id_t)parsed;
        } else if ((strcmp(argv[i], "--dataarray") == 0 ||
                    strcmp(argv[i], "--data-array") == 0) &&
                   i + 1 < argc) {
            uint32_t parsed = 0u;
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &parsed)) {
                fprintf(stderr, "Error: Invalid --dataarray '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->dataarray_id = (nmo_object_id_t)parsed;
        } else if (strcmp(argv[i], "--row") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &args->data_row)) {
                fprintf(stderr, "Error: Invalid --row '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->has_data_row = true;
        } else if (strcmp(argv[i], "--col") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &args->data_col)) {
                fprintf(stderr, "Error: Invalid --col '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->has_data_col = true;
        } else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (!debug_probe_parse_u32_arg(value, &args->delay)) {
                fprintf(stderr, "Error: Invalid --delay '%s'\n", value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            args->has_delay = true;
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            args->name = argv[++i];
        } else if (strcmp(argv[i], "--text") == 0 && i + 1 < argc) {
            args->text = argv[++i];
        } else if ((strcmp(argv[i], "-o") == 0 ||
                    strcmp(argv[i], "--output") == 0) &&
                   i + 1 < argc) {
            *out_output_path = argv[++i];
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            *out_dry_run = true;
        } else if (argv[i][0] != '-') {
            *out_input_path = argv[i];
        } else {
            fprintf(stderr, "Error: Unsupported debug probe option '%s'\n",
                    argv[i]);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    const debug_probe_kind_spec_t *spec = debug_probe_find_kind(args->kind);
    if (spec == NULL) {
        fprintf(stderr, "Error: Unsupported debug probe kind '%s'\n",
                args->kind);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->text != NULL && spec->text_handle == NULL) {
        fprintf(stderr,
                "Error: --text is only supported for 2d-text, console, and "
                "debug-output/message/data-cell logger probes\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->message_node_id != 0u &&
        strcmp(args->kind, "message-logger") != 0) {
        fprintf(stderr,
                "Error: --message-node is only supported for message-logger probes\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if ((args->write_node_id != 0u ||
         args->write_operation_id != 0u ||
         args->write_link_id != 0u) &&
        !spec->logs_data_cell) {
        fprintf(stderr,
                "Error: --write-node/--write-operation/--write-link are only supported for data-cell-logger probes\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->parameter_id != 0u && !spec->connects_parameter) {
        fprintf(stderr,
                "Error: --parameter is only supported for parameter-logger probes\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (spec->connects_parameter && args->parameter_id == 0u) {
        fprintf(stderr,
                "Error: parameter-logger requires --parameter <id>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (spec->connects_parameter && args->text != NULL) {
        fprintf(stderr,
                "Error: parameter-logger uses --parameter, not --text\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->dataarray_id != 0u && !spec->logs_data_cell) {
        fprintf(stderr,
                "Error: --dataarray is only supported for data-cell-logger probes\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if ((args->has_data_row || args->has_data_col) && !spec->logs_data_cell) {
        fprintf(stderr,
                "Error: --row/--col are only supported for data-cell-logger probes\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (spec->logs_data_cell &&
        (args->dataarray_id == 0u || !args->has_data_row ||
         !args->has_data_col)) {
        fprintf(stderr,
                "Error: data-cell-logger requires --dataarray <id> --row <n> --col <n>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if ((args->from_io_id != 0u && spec->input_handle == NULL) ||
        (args->to_io_id != 0u && spec->output_handle == NULL)) {
        fprintf(stderr, "Error: Probe kind '%s' has no known control IO handles\n",
                args->kind);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->has_delay && args->from_io_id == 0u &&
        args->to_io_id == 0u && args->remove_link_id == 0u) {
        fprintf(stderr,
                "Error: --delay requires --from-io, --to-io, or --remove-link\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->behavior_id == 0u || *out_input_path == NULL) {
        fprintf(stderr,
                "Usage: nmo debug probe 2d-text|console|debug-output|message-logger|parameter-logger|data-cell-logger|control-marker "
                "--behavior <id> [--remove-link <id>] [--from-io <id>] [--to-io <id>] "
                "[--parameter <id>] [--dataarray <id> --row <n> --col <n>] "
                "[--write-node <id>|--write-operation <id>|--write-link <id>] "
                "[--delay <n>] [--name <name>] [--text <text>] [--dry-run] <file> "
                "-o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static bool debug_probe_parse_u32_arg(const char *text, uint32_t *out_value)
{
    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE ||
        value > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

static const debug_probe_kind_spec_t *debug_probe_find_kind(const char *kind)
{
    if (kind == NULL) {
        return NULL;
    }
    for (size_t i = 0;
         i < sizeof(debug_probe_kind_specs) / sizeof(debug_probe_kind_specs[0]);
         ++i) {
        if (strcmp(debug_probe_kind_specs[i].kind, kind) == 0) {
            return &debug_probe_kind_specs[i];
        }
    }
    return NULL;
}

static nmo_status_t debug_probe_infer_removed_link_endpoints(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args)
{
    if (ctx == NULL || args == NULL || args->remove_link_id == 0u ||
        (args->from_io_id != 0u && args->to_io_id != 0u)) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    nmo_object_t *link_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, args->remove_link_id)
        : NULL;
    const nmo_behaviorlink_state_t *link_state = link_obj != NULL
        ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
        : NULL;
    if (link_state == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    if (args->from_io_id == 0u) {
        args->from_io_id = nmo_behaviorlink_in_io_id(link_state);
    }
    if (args->to_io_id == 0u) {
        args->to_io_id = nmo_behaviorlink_out_io_id(link_state);
    }
    if (!args->has_delay && link_state->activation_delay > 0) {
        args->delay = (uint32_t)link_state->activation_delay;
        args->has_delay = true;
    }
    return NMO_OK;
}

static nmo_object_id_t debug_probe_find_matching_link(
    nmo_object_repository_t *repo,
    const nmo_behaviorlink_state_t *wanted)
{
    if (repo == NULL || wanted == NULL) {
        return 0u;
    }
    size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL ||
            nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
            continue;
        }
        const nmo_behaviorlink_state_t *state =
            (const nmo_behaviorlink_state_t *)nmo_object_get_state(object);
        if (state != NULL &&
            nmo_behaviorlink_in_io_id(state) ==
                nmo_behaviorlink_in_io_id(wanted) &&
            nmo_behaviorlink_out_io_id(state) ==
                nmo_behaviorlink_out_io_id(wanted) &&
            state->activation_delay == wanted->activation_delay &&
            state->initial_activation_delay == wanted->initial_activation_delay) {
            return nmo_object_get_id(object);
        }
    }
    return 0u;
}

static bool debug_probe_is_parameter_reference_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

static bool debug_probe_is_message_behavior(
    const nmo_cmd_ctx_t *ctx,
    const nmo_behavior_state_t *state)
{
    if (state == NULL ||
        (state->flags & CKBEHAVIOR_BUILDINGBLOCK) == 0u) {
        return false;
    }
    const uint32_t message_flags =
        CKBEHAVIOR_WAITSFORMESSAGE |
        CKBEHAVIOR_MESSAGESENDER |
        CKBEHAVIOR_MESSAGERECEIVER;
    if ((state->flags & message_flags) != 0u) {
        return true;
    }
    const nmo_behavior_proto_t *proto =
        ctx != NULL && ctx->ctx != NULL
            ? nmo_behavior_registry_find(
                  nmo_context_get_bb_registry(ctx->ctx),
                  state->block_guid)
            : NULL;
    return proto != NULL && proto->category != NULL &&
           strcmp(proto->category, "Logics/Message") == 0;
}

static const char *debug_probe_message_role(const nmo_behavior_state_t *state)
{
    if (state == NULL) {
        return "message";
    }
    if ((state->flags & CKBEHAVIOR_MESSAGESENDER) != 0u) {
        return "sender";
    }
    if ((state->flags & CKBEHAVIOR_WAITSFORMESSAGE) != 0u) {
        return "waiter";
    }
    if ((state->flags & CKBEHAVIOR_MESSAGERECEIVER) != 0u) {
        return "receiver";
    }
    return "message";
}

static bool debug_probe_has_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

/* Release the candidate strings and reset the candidate count. */
static void debug_probe_selector_clear_candidates(nmo_debug_probe_args_t *args)
{
    for (size_t i = 0; i < args->selector_candidate_count; ++i) {
        free(args->selector_candidates[i].proto_name);
        args->selector_candidates[i].proto_name = NULL;
        free(args->selector_candidates[i].rejection_code);
        args->selector_candidates[i].rejection_code = NULL;
        args->selector_candidates[i].role = NULL;
    }
    args->selector_candidate_count = 0u;
}

/* Release every string the selector diagnostics own. */
static void debug_probe_selector_dispose_strings(nmo_debug_probe_args_t *args)
{
    if (args == NULL) {
        return;
    }
    debug_probe_selector_clear_candidates(args);
    free(args->selector_rejection_code);
    args->selector_rejection_code = NULL;
}

/*
 * `mode` and `status` must be static strings (selector names or literals);
 * `rejection_code` is copied because it comes from a transient result.
 */
static void debug_probe_selector_set_mode_status(
    nmo_debug_probe_args_t *args,
    const char *mode,
    const char *status,
    const char *rejection_code)
{
    if (args == NULL) {
        return;
    }
    args->selector_mode = mode != NULL ? mode : "";
    args->selector_status = status != NULL ? status : "";
    char *copy = nmo_tool_strdup(rejection_code != NULL ? rejection_code : "");
    if (copy != NULL) {
        free(args->selector_rejection_code);
        args->selector_rejection_code = copy;
    }
}

static void debug_probe_selector_add_candidate(
    const nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args,
    nmo_object_id_t parent_id,
    nmo_object_id_t node_id,
    const nmo_behavior_state_t *state,
    const char *role_override)
{
    if (args == NULL ||
        args->selector_candidate_count >=
            sizeof(args->selector_candidates) /
                sizeof(args->selector_candidates[0])) {
        return;
    }
    const nmo_behavior_proto_t *proto =
        ctx != NULL && ctx->ctx != NULL && state != NULL
            ? nmo_behavior_registry_find(
                  nmo_context_get_bb_registry(ctx->ctx),
                  state->block_guid)
            : NULL;
    size_t index = args->selector_candidate_count++;
    args->selector_candidates[index].node_id = node_id;
    args->selector_candidates[index].parent_id = parent_id;
    args->selector_candidates[index].bb_guid =
        state != NULL ? state->block_guid : NMO_GUID_NULL;
    free(args->selector_candidates[index].proto_name);
    args->selector_candidates[index].proto_name = nmo_tool_strdup(
        proto != NULL && proto->name != NULL ? proto->name : "");
    args->selector_candidates[index].role =
        role_override != NULL ? role_override : debug_probe_message_role(state);
    free(args->selector_candidates[index].rejection_code);
    args->selector_candidates[index].rejection_code = NULL;
}

static bool debug_probe_link_touches_behavior(
    const nmo_behavior_state_t *behavior,
    const nmo_behaviorlink_state_t *link,
    bool to_io_only);

static bool debug_probe_behavior_has_io(const nmo_behavior_state_t *state,
                                        nmo_object_id_t io_id)
{
    return state != NULL && io_id != 0u &&
           (nmo_behavior_ref_array_find(&state->inputs, io_id, NULL) ||
            nmo_behavior_ref_array_find(&state->outputs, io_id, NULL));
}

typedef enum debug_probe_link_touch_mode {
    DEBUG_PROBE_LINK_TOUCH_ANY,
    DEBUG_PROBE_LINK_TOUCH_TO_IO_FIRST,
} debug_probe_link_touch_mode_t;

static bool debug_probe_link_touches_behavior(
    const nmo_behavior_state_t *behavior,
    const nmo_behaviorlink_state_t *link,
    bool to_io_only)
{
    if (behavior == NULL || link == NULL) {
        return false;
    }
    if (debug_probe_behavior_has_io(
            behavior, nmo_behaviorlink_out_io_id(link))) {
        return true;
    }
    return !to_io_only &&
           debug_probe_behavior_has_io(
               behavior, nmo_behaviorlink_in_io_id(link));
}

/*
 * Append ",<id>" (or "<id>" for the first entry) to the malloc'd list in
 * *list, growing it to the exact length. The list stays unchanged on OOM.
 */
static void debug_probe_append_id(char **list, size_t index, nmo_object_id_t id)
{
    if (list == NULL) {
        return;
    }
    char *grown = nmo_tool_strdup_fmt("%s%s%u",
                                      *list != NULL ? *list : "",
                                      index == 0u ? "" : ",",
                                      (unsigned)id);
    if (grown != NULL) {
        free(*list);
        *list = grown;
    }
}

/*
 * `candidate_ids`, when non-NULL, receives a malloc'd comma-separated list of
 * the touching link IDs (NULL when there are none); the caller frees it.
 */
static size_t debug_probe_collect_touching_links(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    const nmo_behavior_state_t *target,
    bool to_io_only,
    nmo_object_id_t *out_selected_link_id,
    const nmo_behaviorlink_state_t **out_selected_link,
    char **candidate_ids)
{
    if (out_selected_link_id != NULL) {
        *out_selected_link_id = 0u;
    }
    if (out_selected_link != NULL) {
        *out_selected_link = NULL;
    }
    if (candidate_ids != NULL) {
        free(*candidate_ids);
        *candidate_ids = NULL;
    }
    if (repo == NULL || parent == NULL || target == NULL) {
        return 0u;
    }

    size_t candidate_count = 0u;
    for (size_t i = 0; i < parent->sub_behavior_links.count; ++i) {
        nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
            &parent->sub_behavior_links, i);
        if (link_id == 0) continue;
        nmo_object_t *link_obj =
            nmo_object_repository_find_by_id(repo, link_id);
        const nmo_behaviorlink_state_t *link =
            link_obj != NULL &&
                    nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(
                      link_obj)
                : NULL;
        if (!debug_probe_link_touches_behavior(target, link, to_io_only)) {
            continue;
        }
        debug_probe_append_id(candidate_ids, candidate_count, link_id);
        if (out_selected_link_id != NULL) {
            *out_selected_link_id = link_id;
        }
        if (out_selected_link != NULL) {
            *out_selected_link = link;
        }
        ++candidate_count;
    }
    return candidate_count;
}

static size_t debug_probe_select_touching_links(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    const nmo_behavior_state_t *target,
    debug_probe_link_touch_mode_t mode,
    nmo_object_id_t *out_selected_link_id,
    const nmo_behaviorlink_state_t **out_selected_link,
    char **candidate_ids)
{
    size_t count = debug_probe_collect_touching_links(
        repo,
        parent,
        target,
        mode == DEBUG_PROBE_LINK_TOUCH_TO_IO_FIRST,
        out_selected_link_id,
        out_selected_link,
        candidate_ids);
    if (count == 0u && mode == DEBUG_PROBE_LINK_TOUCH_TO_IO_FIRST) {
        count = debug_probe_collect_touching_links(
            repo,
            parent,
            target,
            false,
            out_selected_link_id,
            out_selected_link,
            candidate_ids);
    }
    return count;
}

static void debug_probe_apply_selector_result(
    nmo_debug_probe_args_t *args,
    const nmo_probe_selector_result_t *result)
{
    if (args == NULL || result == NULL) {
        return;
    }
    debug_probe_selector_set_mode_status(
        args,
        nmo_probe_selector_mode_name(result->mode),
        nmo_probe_selector_status_name(result->status),
        result->rejection_code[0] != '\0' ? result->rejection_code : NULL);
    args->selector_selected_node_id = result->selected_node_id;
    args->selector_selected_link_id = result->selected_link_id;
    args->selector_selected_operation_id = result->selected_operation_id;
    args->selector_safe_insertion = result->safe_insertion;
    nmo_probe_analysis_dispose(&args->selector_analysis);
    args->selector_analysis = *result;
    args->selector_analysis.candidates = NULL;
    args->selector_analysis.candidate_count = 0u;
    args->selector_analysis.candidate_capacity = 0u;
    args->has_selector_analysis = true;
    debug_probe_selector_clear_candidates(args);
    for (size_t i = 0;
         i < result->candidate_count &&
         i < sizeof(args->selector_candidates) /
                 sizeof(args->selector_candidates[0]);
         ++i) {
        (void)nmo_probe_selector_result_add_candidate(
            &args->selector_analysis, &result->candidates[i]);
        args->selector_candidates[i].node_id = result->candidates[i].node_id;
        args->selector_candidates[i].parent_id =
            result->candidates[i].parent_id;
        args->selector_candidates[i].boundary_behavior_id =
            result->candidates[i].boundary_behavior_id;
        args->selector_candidates[i].link_id = result->candidates[i].link_id;
        args->selector_candidates[i].operation_id =
            result->candidates[i].operation_id;
        args->selector_candidates[i].from_io_id =
            result->candidates[i].from_io_id;
        args->selector_candidates[i].to_io_id =
            result->candidates[i].to_io_id;
        args->selector_candidates[i].has_delay =
            result->candidates[i].has_delay;
        args->selector_candidates[i].delay = result->candidates[i].delay;
        args->selector_candidates[i].source_parameter_id =
            result->candidates[i].source_parameter_id;
        args->selector_candidates[i].value_parameter_id =
            result->candidates[i].value_parameter_id;
        args->selector_candidates[i].dataarray_id =
            result->candidates[i].dataarray_id;
        args->selector_candidates[i].column_type_guid =
            result->candidates[i].column_type_guid;
        args->selector_candidates[i].confidence =
            result->candidates[i].confidence;
        args->selector_candidates[i].bb_guid = result->candidates[i].bb_guid;
        args->selector_candidates[i].proto_name =
            nmo_tool_strdup(result->candidates[i].proto_name);
        args->selector_candidates[i].role =
            nmo_probe_candidate_role_name(result->candidates[i].role);
        args->selector_candidates[i].rejection_code =
            nmo_tool_strdup(result->candidates[i].rejection_code);
        ++args->selector_candidate_count;
    }
    if (result->selected_link_id != 0u && args->remove_link_id == 0u) {
        args->remove_link_id = result->selected_link_id;
    }
    if (result->from_io_id != 0u && args->from_io_id == 0u) {
        args->from_io_id = result->from_io_id;
    }
    if (result->to_io_id != 0u && args->to_io_id == 0u) {
        args->to_io_id = result->to_io_id;
    }
    if (result->has_delay && !args->has_delay) {
        args->delay = result->delay;
        args->has_delay = true;
    }
    if (strcmp(args->kind, "message-logger") == 0 &&
        result->selected_node_id != 0u &&
        args->message_node_id == 0u) {
        args->message_node_id = result->selected_node_id;
    }
    if (strcmp(args->kind, "data-cell-logger") == 0) {
        if (result->selected_node_id != 0u && args->write_node_id == 0u) {
            args->write_node_id = result->selected_node_id;
        }
        if (result->selected_operation_id != 0u &&
            args->write_operation_id == 0u) {
            args->write_operation_id = result->selected_operation_id;
        }
    }
}

static nmo_status_t debug_probe_analyze_core_selector(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args,
    nmo_probe_selector_kind_t kind)
{
    if (ctx == NULL || args == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_probe_selector_request_t request;
    nmo_probe_selector_result_t result;
    nmo_probe_selector_request_init(&request);
    nmo_probe_selector_result_init(&result);
    request.kind = kind;
    request.behavior_id = args->behavior_id;
    request.dataarray_id = args->dataarray_id;
    request.row = args->data_row;
    request.col = args->data_col;
    request.has_data_cell = args->has_data_row && args->has_data_col;
    request.message_node_id = args->message_node_id;
    request.write_node_id = args->write_node_id;
    request.write_operation_id = args->write_operation_id;
    request.write_link_id = args->write_link_id;
    request.remove_link_id = args->remove_link_id;
    request.from_io_id = args->from_io_id;
    request.to_io_id = args->to_io_id;
    request.has_delay = args->has_delay;
    request.delay = args->delay;

    nmo_status_t status =
        nmo_probe_analyze_selector(ctx->workspace, &request, &result);
    debug_probe_apply_selector_result(args, &result);
    if (status != NMO_OK) {
        char message[sizeof(result.message)];
        snprintf(message,
                 sizeof(message),
                 "%s",
                 result.message[0] != '\0' ? result.message
                                           : "debug probe selector failed");
        nmo_probe_analysis_dispose(&result);
        NMO_RETURN_ERROR(
            status,
            NMO_SEVERITY_ERROR,
            "%s",
            message);
    }
    nmo_probe_analysis_dispose(&result);
    return NMO_OK;
}

static nmo_status_t debug_probe_validate_targets(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args,
    const debug_probe_kind_spec_t *spec)
{
    if (ctx == NULL || args == NULL || spec == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *behavior =
        nmo_object_repository_find_by_id(repo, args->behavior_id);
    if (behavior == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "debug probe behavior not found");
    }
    if (nmo_object_get_class_id(behavior) != NMO_CID_BEHAVIOR) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "debug probe behavior target is not a behavior");
    }

    if (spec->connects_parameter) {
        nmo_object_t *parameter =
            nmo_object_repository_find_by_id(repo, args->parameter_id);
        if (parameter == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "debug probe parameter not found");
        }
        if (!debug_probe_is_parameter_reference_class(
                nmo_object_get_class_id(parameter))) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "debug probe parameter target is not a parameter");
        }
    }

    if (args->message_node_id != 0u) {
        if (!debug_probe_has_text(args->selector_mode)) {
            debug_probe_selector_set_mode_status(
                args,
                args->remove_link_id != 0u ? "explicit_link" : "explicit_node",
                "selected",
                NULL);
        } else if (!debug_probe_has_text(args->selector_status)) {
            args->selector_status = "selected";
        }
        nmo_object_t *message_node =
            nmo_object_repository_find_by_id(repo, args->message_node_id);
        if (message_node == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "debug probe message-node not found");
        }
        if (nmo_object_get_class_id(message_node) != NMO_CID_BEHAVIOR) {
            NMO_RETURN_ERROR(
                NMO_ERR_INVALID_ARGUMENT,
                NMO_SEVERITY_ERROR,
                "debug probe message-node target is not a behavior");
        }
        const nmo_behavior_state_t *message_state =
            (const nmo_behavior_state_t *)nmo_object_get_state(message_node);
        if (!debug_probe_is_message_behavior(ctx, message_state)) {
            NMO_RETURN_ERROR(
                NMO_ERR_INVALID_ARGUMENT,
                NMO_SEVERITY_ERROR,
                "debug probe message-node target is not a message behavior");
        }
        args->selector_selected_node_id = args->message_node_id;
        if (args->remove_link_id != 0u) {
            args->selector_selected_link_id = args->remove_link_id;
        }
        if (args->selector_candidate_count == 0u) {
            debug_probe_selector_add_candidate(
                ctx,
                args,
                args->behavior_id,
                args->message_node_id,
                message_state,
                NULL);
        }
        if (args->remove_link_id != 0u) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, args->remove_link_id);
            const nmo_behaviorlink_state_t *link_state = link_obj != NULL &&
                nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                    ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(
                          link_obj)
                    : NULL;
            if (link_state == NULL ||
                (!debug_probe_behavior_has_io(
                     message_state, nmo_behaviorlink_in_io_id(link_state)) &&
                 !debug_probe_behavior_has_io(
                     message_state, nmo_behaviorlink_out_io_id(link_state)))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_ARGUMENT,
                    NMO_SEVERITY_ERROR,
                    "debug probe remove-link does not touch selected message-node");
            }
        } else if ((args->from_io_id != 0u || args->to_io_id != 0u) &&
                   !debug_probe_behavior_has_io(message_state,
                                                args->from_io_id) &&
                   !debug_probe_behavior_has_io(message_state,
                                                args->to_io_id)) {
            NMO_RETURN_ERROR(
                NMO_ERR_INVALID_ARGUMENT,
                NMO_SEVERITY_ERROR,
                "debug probe IO endpoint does not touch selected message-node");
        }
    }

    if (spec->logs_data_cell) {
        nmo_object_t *dataarray =
            nmo_object_repository_find_by_id(repo, args->dataarray_id);
        if (dataarray == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "debug probe data array not found");
        }
        if (nmo_object_get_class_id(dataarray) != NMO_CID_DATAARRAY) {
            NMO_RETURN_ERROR(
                NMO_ERR_INVALID_ARGUMENT,
                NMO_SEVERITY_ERROR,
                "debug probe data array target is not a CKDataArray");
        }
        const nmo_dataarray_state_t *state =
            (const nmo_dataarray_state_t *)nmo_object_get_state(dataarray);
        if (state == NULL ||
            args->data_row >= state->row_count ||
            args->data_col >= state->column_count ||
            state->rows == NULL ||
            args->data_col >= state->rows[args->data_row].column_count) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT,
                             NMO_SEVERITY_ERROR,
                             "debug probe data cell is out of range");
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t debug_probe_analyze_message_selector(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args)
{
    if (ctx == NULL || args == NULL ||
        strcmp(args->kind, "message-logger") != 0) {
        return NMO_OK;
    }
    return debug_probe_analyze_core_selector(
        ctx, args, NMO_PROBE_SELECTOR_MESSAGE);
}

static nmo_status_t debug_probe_analyze_data_cell_selector(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args)
{
    if (ctx == NULL || args == NULL ||
        strcmp(args->kind, "data-cell-logger") != 0) {
        return NMO_OK;
    }
    return debug_probe_analyze_core_selector(
        ctx, args, NMO_PROBE_SELECTOR_DATA_CELL_WRITE);
}

static nmo_status_t debug_probe_select_data_write_link(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args)
{
    if (ctx == NULL || args == NULL ||
        strcmp(args->kind, "data-cell-logger") != 0 ||
        args->write_node_id == 0u ||
        args->remove_link_id != 0u ||
        args->from_io_id != 0u ||
        args->to_io_id != 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    nmo_object_t *parent_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, args->behavior_id)
        : NULL;
    const nmo_behavior_state_t *parent =
        parent_obj != NULL &&
                nmo_object_get_class_id(parent_obj) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(parent_obj)
            : NULL;
    nmo_object_t *writer_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, args->write_node_id)
        : NULL;
    const nmo_behavior_state_t *writer =
        writer_obj != NULL &&
                nmo_object_get_class_id(writer_obj) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(writer_obj)
            : NULL;
    if (parent == NULL || writer == NULL) {
        return NMO_OK;
    }

    char *candidate_ids = NULL;
    nmo_object_id_t selected_link_id = 0u;
    const nmo_behaviorlink_state_t *selected_link = NULL;
    size_t candidate_count = debug_probe_select_touching_links(
        repo,
        parent,
        writer,
        DEBUG_PROBE_LINK_TOUCH_ANY,
        &selected_link_id,
        &selected_link,
        &candidate_ids);

    if (candidate_count != 1u || selected_link == NULL) {
        debug_probe_selector_set_mode_status(
            args, "explicit_node", "unsafe", "unsafe_probe_insertion");
        nmo_last_error_setf(
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            __FILE__,
            __LINE__,
            "unsafe_probe_insertion: debug probe automatic data write insertion is unsafe (candidate links: [%s])",
            candidate_ids != NULL ? candidate_ids : "");
        free(candidate_ids);
        return NMO_ERR_INVALID_ARGUMENT;
    }
    free(candidate_ids);

    args->remove_link_id = selected_link_id;
    args->from_io_id = nmo_behaviorlink_in_io_id(selected_link);
    args->to_io_id = nmo_behaviorlink_out_io_id(selected_link);
    args->selector_selected_link_id = selected_link_id;
    if (!args->has_delay && selected_link->activation_delay > 0) {
        args->delay = (uint32_t)selected_link->activation_delay;
        args->has_delay = true;
    }
    debug_probe_selector_set_mode_status(
        args, "explicit_node", "selected", NULL);
    return NMO_OK;
}

static nmo_status_t debug_probe_select_message_link(
    nmo_cmd_ctx_t *ctx,
    nmo_debug_probe_args_t *args)
{
    if (ctx == NULL || args == NULL ||
        strcmp(args->kind, "message-logger") != 0 ||
        args->message_node_id == 0u ||
        args->remove_link_id != 0u ||
        args->from_io_id != 0u ||
        args->to_io_id != 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    nmo_object_t *parent_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, args->behavior_id)
        : NULL;
    const nmo_behavior_state_t *parent =
        parent_obj != NULL &&
                nmo_object_get_class_id(parent_obj) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(parent_obj)
            : NULL;
    nmo_object_t *message_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, args->message_node_id)
        : NULL;
    const nmo_behavior_state_t *message =
        message_obj != NULL &&
                nmo_object_get_class_id(message_obj) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(message_obj)
            : NULL;
    if (parent == NULL || message == NULL) {
        return NMO_OK;
    }

    char *candidate_ids = NULL;
    nmo_object_id_t selected_link_id = 0u;
    const nmo_behaviorlink_state_t *selected_link = NULL;
    size_t candidate_count = debug_probe_select_touching_links(
        repo,
        parent,
        message,
        DEBUG_PROBE_LINK_TOUCH_TO_IO_FIRST,
        &selected_link_id,
        &selected_link,
        &candidate_ids);

    if (candidate_count != 1u || selected_link == NULL) {
        debug_probe_selector_set_mode_status(
            args,
            debug_probe_has_text(args->selector_mode)
                ? args->selector_mode
                : "explicit_node",
            "unsafe",
            "unsafe_probe_insertion");
        nmo_last_error_setf(
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            __FILE__,
            __LINE__,
            "debug probe automatic insertion is unsafe (candidate links: [%s])",
            candidate_ids != NULL ? candidate_ids : "");
        free(candidate_ids);
        return NMO_ERR_INVALID_ARGUMENT;
    }
    free(candidate_ids);

    args->remove_link_id = selected_link_id;
    args->from_io_id = nmo_behaviorlink_in_io_id(selected_link);
    args->to_io_id = nmo_behaviorlink_out_io_id(selected_link);
    args->selector_selected_link_id = selected_link_id;
    if (!args->has_delay && selected_link->activation_delay > 0) {
        args->delay = (uint32_t)selected_link->activation_delay;
        args->has_delay = true;
    }
    debug_probe_selector_set_mode_status(
        args,
        debug_probe_has_text(args->selector_mode)
            ? args->selector_mode
            : "explicit_node",
        "selected",
        NULL);
    return NMO_OK;
}

static void debug_probe_replace_report_id(nmo_edit_report_t *report,
                                          nmo_object_id_t old_id,
                                          nmo_object_id_t new_id)
{
    if (report == NULL || old_id == 0u || new_id == 0u || old_id == new_id) {
        return;
    }
    for (size_t i = 0; i < report->operation_count; ++i) {
        nmo_edit_operation_result_t *operation = &report->operations[i];
        if (operation->result_id == old_id) {
            operation->result_id = new_id;
        }
        for (size_t j = 0; j < operation->handle_count; ++j) {
            if (operation->handles[j].id == old_id) {
                operation->handles[j].id = new_id;
            }
        }
    }
    for (size_t i = 0; i < report->created_object_count; ++i) {
        if (report->created_objects[i].id == old_id) {
            report->created_objects[i].id = new_id;
        }
    }
}

static nmo_status_t debug_probe_reconcile_saved_link_ids(
    nmo_cmd_ctx_t *ctx,
    const char *output_path,
    nmo_debug_probe_args_t *args)
{
    if (ctx == NULL || output_path == NULL || args == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *saved_ctx = NULL;
    nmo_document_t *saved_document = NULL;
    nmo_workspace_t *saved_workspace = NULL;
    if (!nmo_tool_open_document(output_path,
                                &saved_ctx,
                                &saved_document,
                                &saved_workspace,
                                NULL)) {
        return NMO_ERR_CANT_OPEN_FILE;
    }

    nmo_object_repository_t *runtime_repo =
        nmo_tool_owner_repository(ctx->workspace);
    nmo_object_repository_t *saved_repo =
        nmo_tool_owner_repository(saved_workspace);
    for (size_t i = 0; i < args->report.operation_count; ++i) {
        nmo_edit_operation_result_t *operation = &args->report.operations[i];
        if (operation->kind != NMO_EDIT_OP_ADD_BEHAVIOR_LINK ||
            operation->result_id == 0u ||
            (saved_repo != NULL &&
             nmo_object_repository_find_by_id(saved_repo,
                                              operation->result_id) != NULL)) {
            continue;
        }
        nmo_object_t *runtime_link = runtime_repo != NULL
            ? nmo_object_repository_find_by_id(runtime_repo,
                                               operation->result_id)
            : NULL;
        const nmo_behaviorlink_state_t *runtime_state = runtime_link != NULL
            ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(runtime_link)
            : NULL;
        nmo_object_id_t saved_id =
            debug_probe_find_matching_link(saved_repo, runtime_state);
        if (saved_id != 0u) {
            debug_probe_replace_report_id(
                &args->report, operation->result_id, saved_id);
        }
    }

    nmo_tool_close_document(saved_ctx, saved_document, saved_workspace);
    return NMO_OK;
}

static int debug_probe_mutate(nmo_cmd_ctx_t *ctx,
                              bool dry_run,
                              const char *output_path,
                              void *user_data)
{
    (void)output_path;
    nmo_debug_probe_args_t *args = (nmo_debug_probe_args_t *)user_data;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = NMO_OK;
    size_t node_op_index = 0u;
    char *data_cell_text = NULL;

    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    const debug_probe_kind_spec_t *spec = debug_probe_find_kind(args->kind);
    if (spec == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_edit_report_init(&args->report);
    status = debug_probe_infer_removed_link_endpoints(ctx, args);
    if (status == NMO_OK) {
        status = debug_probe_validate_targets(ctx, args, spec);
    }
    if (status == NMO_OK) {
        status = debug_probe_analyze_message_selector(ctx, args);
    }
    if (status == NMO_OK) {
        status = debug_probe_analyze_data_cell_selector(ctx, args);
    }
    if (status == NMO_OK) {
        status = debug_probe_select_message_link(ctx, args);
    }
    if (status == NMO_OK) {
        status = debug_probe_select_data_write_link(ctx, args);
    }
    if (status == NMO_OK && args->remove_link_id != 0u) {
        status = debug_probe_validate_targets(ctx, args, spec);
    }
    if (status == NMO_OK) {
        status = nmo_edit_plan_create(&plan);
    }
    if (status == NMO_OK && args->has_selector_analysis) {
        status = nmo_edit_plan_set_probe_selector_analysis(
            plan, &args->selector_analysis);
    }
    if (status == NMO_OK && args->remove_link_id != 0u) {
        status = nmo_edit_plan_add_remove_behavior_link(
            plan, args->behavior_id, args->remove_link_id);
    }
    if (status == NMO_OK) {
        node_op_index = nmo_edit_plan_count(plan);
    }
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_node(
            plan, args->behavior_id, spec->bb_guid, args->name);
    }
    const char *probe_text = args->text;
    if (status == NMO_OK && spec->logs_data_cell && probe_text == NULL) {
        data_cell_text = nmo_tool_strdup_fmt("dataarray:%u[%u,%u]",
                                             (unsigned)args->dataarray_id,
                                             (unsigned)args->data_row,
                                             (unsigned)args->data_col);
        if (data_cell_text == NULL) {
            status = NMO_ERR_NOMEM;
        }
        probe_text = data_cell_text;
    }
    if (status == NMO_OK && probe_text != NULL && spec->text_handle != NULL) {
        nmo_edit_handle_ref_t text_ref = {
            .has_ref = true,
            .operation_index = node_op_index,
            .handle_name = spec->text_handle,
        };
        status = nmo_edit_plan_add_set_parameter_value(
            plan, 0u, &text_ref, probe_text, NULL);
    }
    if (status == NMO_OK && spec->connects_parameter &&
        args->parameter_id != 0u && spec->text_handle != NULL) {
        nmo_edit_handle_ref_t text_ref = {
            .has_ref = true,
            .operation_index = node_op_index,
            .handle_name = spec->text_handle,
        };
        status = nmo_edit_plan_add_connect_parameter(
            plan, args->parameter_id, 0u, &text_ref);
    }
    if (status == NMO_OK && args->from_io_id != 0u) {
        nmo_edit_handle_ref_t input_ref = {
            .has_ref = true,
            .operation_index = node_op_index,
            .handle_name = spec->input_handle,
        };
        status = nmo_edit_plan_add_behavior_link(
            plan,
            args->behavior_id,
            args->from_io_id,
            NULL,
            0u,
            &input_ref,
            args->has_delay ? args->delay : 0u);
    }
    if (status == NMO_OK && args->to_io_id != 0u) {
        nmo_edit_handle_ref_t output_ref = {
            .has_ref = true,
            .operation_index = node_op_index,
            .handle_name = spec->output_handle,
        };
        status = nmo_edit_plan_add_behavior_link(
            plan,
            args->behavior_id,
            0u,
            &output_ref,
            args->to_io_id,
            NULL,
            (args->from_io_id == 0u && args->has_delay) ? args->delay : 0u);
    }
    if (status == NMO_OK) {
        nmo_edit_executor_options_t options =
            nmo_edit_executor_options_default();
        options.dry_run = dry_run;
        status = nmo_edit_executor_execute(
            ctx->workspace, plan, &options, &args->report);
    }
    nmo_edit_plan_destroy(plan);
    free(data_cell_text);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: debug probe failed: %s\n",
                (message != NULL && message[0] != '\0')
                    ? message
                    : nmo_error_string(status));
        nmo_probe_analysis_dispose(&args->selector_analysis);
        debug_probe_selector_dispose_strings(args);
        return status == NMO_ERR_INVALID_ARGUMENT || status == NMO_ERR_NOT_FOUND
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_probe_report(nmo_cmd_ctx_t *ctx,
                              bool dry_run,
                              const char *output_path,
                              void *user_data)
{
    nmo_debug_probe_args_t *args = (nmo_debug_probe_args_t *)user_data;
    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    const debug_probe_kind_spec_t *spec = debug_probe_find_kind(args->kind);

    if (ctx->is_json && !dry_run && output_path != NULL) {
        (void)debug_probe_reconcile_saved_link_ids(ctx, output_path, args);
        if (args->report.output_path == NULL) {
            (void)nmo_edit_report_set_output_path(&args->report, output_path);
        }
    }

    const debug_probe_report_json_ctx_t report_json = {
        .report = &args->report,
        .dry_run = dry_run,
    };
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_json(rec, debug_probe_edit_report_json,
                                   &report_json);
    if (args->kind != NULL) {
        ok = ok && nmo_cli_record_str(rec, "probe_kind", NULL, args->kind);
    } else {
        ok = ok && nmo_cli_record_null(rec, "probe_kind", NULL, NULL);
    }
    ok = ok && nmo_cli_record_uint(rec, "behavior_id", NULL, args->behavior_id);
    if (args->message_node_id != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "message_node_id", NULL,
                                       args->message_node_id);
        ok = ok && nmo_cli_record_str(rec, "probe_selector", NULL,
                                      "message_flow");
    } else if (spec != NULL && spec->logs_data_cell) {
        ok = ok && nmo_cli_record_str(rec, "probe_selector", NULL,
                                      "data_cell_write");
    }
    if (!args->report.has_probe_selector_analysis) {
        ok = ok && debug_probe_add_selector_diagnostics(rec, args);
    }
    ok = ok && nmo_cli_record_raw_fmt(
        rec, "%sInjected %zu debug probe operation(s)\n",
        dry_run ? "[dry-run] " : "", args->report.operation_count);
    if (!dry_run && output_path != NULL) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
    }

    int rc = debug_emit(ctx, rec, ok, "debug.probe", 0);
    nmo_edit_report_dispose(&args->report);
    nmo_probe_analysis_dispose(&args->selector_analysis);
    debug_probe_selector_dispose_strings(args);
    return rc;
}
/* Add `key` as an unsigned integer unless `value` is zero. */
static bool debug_add_uint_nonzero(nmo_cli_record_t *rec, const char *key,
                                   uint64_t value)
{
    return value == 0u || nmo_cli_record_uint(rec, key, NULL, value);
}

static bool debug_probe_add_selector_candidate(
    nmo_cli_record_array_t *candidates,
    const struct debug_probe_selector_candidate *candidate)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_uint(item, "node_id", NULL, candidate->node_id);
    ok = ok && nmo_cli_record_uint(item, "parent_id", NULL,
                                   candidate->parent_id);
    ok = ok && nmo_cli_record_uint(item, "boundary_behavior_id", NULL,
                                   candidate->boundary_behavior_id);
    ok = ok && debug_add_uint_nonzero(item, "link_id", candidate->link_id);
    ok = ok && debug_add_uint_nonzero(item, "operation_id",
                                      candidate->operation_id);
    ok = ok && debug_add_uint_nonzero(item, "from_io_id",
                                      candidate->from_io_id);
    ok = ok && debug_add_uint_nonzero(item, "to_io_id", candidate->to_io_id);
    if (candidate->has_delay) {
        ok = ok && nmo_cli_record_uint(item, "delay", NULL, candidate->delay);
    }
    ok = ok && debug_add_uint_nonzero(item, "source_parameter_id",
                                      candidate->source_parameter_id);
    ok = ok && debug_add_uint_nonzero(item, "value_parameter_id",
                                      candidate->value_parameter_id);
    ok = ok && debug_add_uint_nonzero(item, "dataarray_id",
                                      candidate->dataarray_id);
    if (!nmo_guid_is_null(candidate->column_type_guid)) {
        ok = ok && nmo_cli_record_guid(item, "column_type_guid", NULL,
                                       candidate->column_type_guid);
    }
    ok = ok && nmo_cli_record_real(item, "confidence", NULL,
                                   candidate->confidence, NULL);
    ok = ok && nmo_cli_record_guid(item, "bb_guid", NULL, candidate->bb_guid);
    ok = ok && nmo_cli_record_str(item, "proto_name", NULL,
                                  candidate->proto_name);
    ok = ok && nmo_cli_record_str(item, "role", NULL, candidate->role);
    if (debug_probe_has_text(candidate->rejection_code)) {
        ok = ok && nmo_cli_record_str(item, "rejection_code", NULL,
                                      candidate->rejection_code);
    }
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(candidates, item);
}

static bool debug_probe_add_safe_insertion(
    nmo_cli_record_t *diag,
    const nmo_probe_safe_insertion_t *safe_insertion)
{
    nmo_cli_record_t *safe = nmo_cli_record_object(diag, "safe_insertion");
    bool ok = safe != NULL;
    ok = ok && nmo_cli_record_bool(safe, "selected", NULL, true);
    ok = ok && debug_add_uint_nonzero(safe, "selected_node_id",
                                      safe_insertion->selected_node_id);
    ok = ok && debug_add_uint_nonzero(safe, "selected_link_id",
                                      safe_insertion->selected_link_id);
    ok = ok && debug_add_uint_nonzero(safe, "selected_operation_id",
                                      safe_insertion->selected_operation_id);
    ok = ok && debug_add_uint_nonzero(safe, "remove_link_id",
                                      safe_insertion->remove_link_id);
    ok = ok && debug_add_uint_nonzero(safe, "insert_from_io_id",
                                      safe_insertion->insert_from_io_id);
    ok = ok && debug_add_uint_nonzero(safe, "insert_to_io_id",
                                      safe_insertion->insert_to_io_id);
    if (safe_insertion->has_preserved_delay) {
        ok = ok && nmo_cli_record_uint(safe, "preserved_delay", NULL,
                                       safe_insertion->preserved_delay);
    }
    return ok;
}

/* JSON-only "probe_selector_diagnostics", when a selector mode was recorded. */
static bool debug_probe_add_selector_diagnostics(
    nmo_cli_record_t *rec,
    const nmo_debug_probe_args_t *args)
{
    if (!debug_probe_has_text(args->selector_mode)) {
        return true;
    }
    nmo_cli_record_t *diag =
        nmo_cli_record_object(rec, "probe_selector_diagnostics");
    bool ok = diag != NULL;
    ok = ok && nmo_cli_record_str(diag, "mode", NULL, args->selector_mode);
    ok = ok && nmo_cli_record_str(diag, "status", NULL, args->selector_status);
    nmo_cli_record_array_t *candidates =
        ok ? nmo_cli_record_array(diag, "candidates", NULL) : NULL;
    ok = candidates != NULL;
    for (size_t i = 0; ok && i < args->selector_candidate_count; ++i) {
        ok = debug_probe_add_selector_candidate(
            candidates, &args->selector_candidates[i]);
    }
    ok = ok && debug_add_uint_nonzero(diag, "selected_node_id",
                                      args->selector_selected_node_id);
    ok = ok && debug_add_uint_nonzero(diag, "selected_link_id",
                                      args->selector_selected_link_id);
    ok = ok && debug_add_uint_nonzero(diag, "selected_operation_id",
                                      args->selector_selected_operation_id);
    if (debug_probe_has_text(args->selector_rejection_code)) {
        ok = ok && nmo_cli_record_str(diag, "rejection_code", NULL,
                                      args->selector_rejection_code);
    }
    if (args->selector_safe_insertion.selected) {
        ok = ok && debug_probe_add_safe_insertion(
            diag, &args->selector_safe_insertion);
    }
    return ok;
}
static bool debug_add_load_phase_stats(nmo_cli_record_t *rec,
                                       const nmo_load_perf_stats_t *stats)
{
    nmo_cli_record_t *phase_stats = nmo_cli_record_object(rec, "phase_stats");
    bool ok = phase_stats != NULL;
    ok = ok && nmo_cli_record_uint(phase_stats, "packed_header1_bytes", NULL,
                                   stats->packed_header1_bytes);
    ok = ok && nmo_cli_record_uint(phase_stats, "unpacked_header1_bytes", NULL,
                                   stats->unpacked_header1_bytes);
    ok = ok && nmo_cli_record_uint(phase_stats, "packed_data_bytes", NULL,
                                   stats->packed_data_bytes);
    ok = ok && nmo_cli_record_uint(phase_stats, "unpacked_data_bytes", NULL,
                                   stats->unpacked_data_bytes);
    ok = ok && nmo_cli_record_raw_fmt(phase_stats,
                                      "\nPhase Timings:\n  %-28s %8s %12s\n",
                                      "phase", "calls", "ms");

    nmo_cli_record_t *phases =
        ok ? nmo_cli_record_object(phase_stats, "phases") : NULL;
    ok = phases != NULL;
    for (int i = 0; ok && i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        const char *name = nmo_load_perf_phase_name((nmo_load_perf_phase_t)i);
        nmo_cli_record_t *entry = nmo_cli_record_object(phases, name);
        ok = entry != NULL;
        ok = ok && nmo_cli_record_uint(entry, "calls", NULL, phase->calls);
        ok = ok && nmo_cli_record_real(entry, "milliseconds", NULL,
                                       phase->milliseconds, NULL);
        ok = ok && nmo_cli_record_raw_fmt(entry, "  %-28s %8llu %12.3f\n",
                                          name,
                                          (unsigned long long)phase->calls,
                                          phase->milliseconds);
    }

    ok = ok && nmo_cli_record_raw_fmt(phase_stats,
                                      "\nSection Bytes:\n"
                                      "  Header1: packed=%zu unpacked=%zu\n"
                                      "  Data:    packed=%zu unpacked=%zu\n",
                                      stats->packed_header1_bytes,
                                      stats->unpacked_header1_bytes,
                                      stats->packed_data_bytes,
                                      stats->unpacked_data_bytes);
    return ok;
}

static bool debug_load_profile_from_arg(const char *arg, nmo_load_profile_t *out_profile) {
    if (arg == NULL || out_profile == NULL) {
        return false;
    }
    if (strcmp(arg, "full") == 0) {
        *out_profile = NMO_LOAD_PROFILE_FULL;
        return true;
    }
    if (strcmp(arg, "metadata") == 0) {
        *out_profile = NMO_LOAD_PROFILE_METADATA;
        return true;
    }
    if (strcmp(arg, "header") == 0 || strcmp(arg, "header-only") == 0) {
        *out_profile = NMO_LOAD_PROFILE_HEADER_ONLY;
        return true;
    }
    return false;
}

static const char *debug_load_profile_name(nmo_load_profile_t profile) {
    switch (profile) {
        case NMO_LOAD_PROFILE_FULL:
            return "full";
        case NMO_LOAD_PROFILE_METADATA:
            return "metadata";
        case NMO_LOAD_PROFILE_HEADER_ONLY:
            return "header-only";
        default:
            return "unknown";
    }
}

static int debug_parse_load_profile(int argc, char **argv,
                                    nmo_load_profile_t *profile)
{
    *profile = NMO_LOAD_PROFILE_FULL;
    for (int i = 0; i < argc; i++) {
        const char *value = NULL;
        if (strncmp(argv[i], "--profile=", 10) == 0) {
            value = argv[i] + 10;
        } else if (strncmp(argv[i], "--load-profile=", 15) == 0) {
            value = argv[i] + 15;
        }

        if (value != NULL && !debug_load_profile_from_arg(value, profile)) {
            fprintf(stderr, "Error: Invalid load profile '%s'\n", value);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_chunks_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    debug_item_list_t *items = (debug_item_list_t *)user;
    if (!items || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        return 0;
    }

    const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_uint(item, "id", "ObjectID", nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_uint(item, "class_id", "ClassID", chunk->class_id);
    ok = ok && nmo_cli_record_text(item, "Class", class_name ? class_name : "-");
    ok = ok && nmo_cli_record_uint(item, "data_size", "DataSize",
                                   nmo_chunk_get_data_size(chunk));
    ok = ok && nmo_cli_record_uint(item, "compressed_size", "PackSize",
                                   chunk->compressed_size);
    ok = ok && nmo_cli_record_uint(item, "options", "Options",
                                   chunk->chunk_options);
    if (ok && chunk->chunk_options == 0) {
        ok = nmo_cli_record_set_text(item, "-");
    } else if (ok) {
        char *opt = nmo_cli_chunk_options_dup(chunk->chunk_options);
        ok = nmo_cli_record_set_text_fmt(item, "%s (0x%04X)", opt ? opt : "",
                                         chunk->chunk_options);
        free(opt);
    }
    if (class_name) {
        ok = ok && nmo_cli_record_str(item, "class_name", NULL, class_name);
    }
    ok = ok && nmo_cli_record_str_opt(item, "name", NULL,
                                      nmo_object_get_name(obj), NULL);
    debug_item_list_push(items, item, ok);
    return 0;
}

static int debug_objects_object(size_t index, nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c, void *user)
{
    debug_item_list_t *items = (debug_item_list_t *)user;
    if (!items || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
    const char *name = nmo_object_get_name(obj);

    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_uint(item, "index", "Idx", index);
    ok = ok && nmo_cli_record_uint(item, "id", "ID", nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_uint(item, "class_id", NULL,
                                   nmo_object_get_class_id(obj));
    ok = ok && nmo_cli_record_uint(item, "flags", "Flags",
                                   nmo_object_get_flags(obj));
    ok = ok && nmo_cli_record_set_text_fmt(item, "0x%08X",
                                           nmo_object_get_flags(obj));
    ok = ok && nmo_cli_record_str_opt(item, "name", NULL, name, NULL);
    if (class_name) {
        ok = ok && nmo_cli_record_str(item, "class_name", NULL, class_name);
    }
    ok = ok && nmo_cli_record_text(item, "Class", class_name ? class_name : "-");
    ok = ok && nmo_cli_record_text(item, "Name", (name && name[0]) ? name : "-");
    ok = ok && nmo_cli_record_bool(item, "has_chunk", NULL, chunk != NULL);
    if (chunk) {
        ok = ok && nmo_cli_record_uint(item, "chunk_size", "Chunk",
                                       nmo_chunk_get_data_size(chunk));
    } else {
        ok = ok && nmo_cli_record_text(item, "Chunk", "-");
    }
    debug_item_list_push(items, item, ok);
    return 0;
}

typedef struct nmo_debug_export_data {
    debug_item_list_t items;
    bool include_data;
    size_t max_bytes;
} nmo_debug_export_data_t;

static int debug_export_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    nmo_debug_export_data_t *data = (nmo_debug_export_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_uint(item, "index", NULL, index);
    ok = ok && nmo_cli_record_uint(item, "id", NULL, nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_uint(item, "class_id", NULL,
                                   nmo_object_get_class_id(obj));
    ok = ok && nmo_cli_record_uint(item, "flags", NULL,
                                   nmo_object_get_flags(obj));
    ok = ok && nmo_cli_record_str_opt(item, "name", NULL,
                                      nmo_object_get_name(obj), NULL);

    const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
    if (class_name) {
        ok = ok && nmo_cli_record_str(item, "class_name", NULL, class_name);
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (chunk) {
        nmo_cli_record_t *cv = ok ? nmo_cli_record_object(item, "chunk") : NULL;
        ok = cv != NULL;
        ok = ok && nmo_cli_record_uint(cv, "class_id", NULL, chunk->class_id);
        ok = ok && nmo_cli_record_uint(cv, "data_size", NULL,
                                       nmo_chunk_get_data_size(chunk));
        ok = ok && nmo_cli_record_uint(cv, "compressed_size", NULL,
                                       chunk->compressed_size);
        ok = ok && nmo_cli_record_uint(cv, "uncompressed_size", NULL,
                                       chunk->uncompressed_size);
        ok = ok && nmo_cli_record_uint(cv, "options", NULL,
                                       chunk->chunk_options);
        ok = ok && nmo_cli_record_uint(cv, "id_count", NULL,
                                       nmo_chunk_get_id_count(chunk));
        ok = ok && nmo_cli_record_uint(cv, "subchunk_count", NULL,
                                       nmo_chunk_get_sub_chunk_count(chunk));

        if (data->include_data) {
            size_t data_size = 0;
            const void *chunk_data = nmo_chunk_get_data(chunk, &data_size);
            ok = ok && nmo_cli_record_hex_bytes(cv, NULL, chunk_data,
                                                data_size, data->max_bytes);
        }
    }

    debug_item_list_push(&data->items, item, ok);
    return 0;
}

/* ============================================================================
 * debug load-phases
 * ============================================================================ */

static int debug_load_phases_run_in_ctx(nmo_cmd_ctx_t *c,
                                        nmo_load_profile_t profile,
                                        const nmo_load_perf_stats_t *phase_stats,
                                        bool close_ctx)
{
    nmo_load_perf_stats_t empty_phase_stats;
    if (!phase_stats) {
        nmo_load_perf_stats_reset(&empty_phase_stats);
        phase_stats = &empty_phase_stats;
    }

    /* Get finish loading stats */
    nmo_runtime_load_stats_t stats;
    bool has_stats = (nmo_document_get_runtime_load_stats(c->document, &stats) == NMO_OK);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "Load Phases");
    ok = ok && nmo_cli_record_str(rec, "file", "File", c->file_path);
    ok = ok && nmo_cli_record_str(rec, "profile", "Profile",
                                  debug_load_profile_name(profile));
    ok = ok && nmo_cli_record_bool(rec, "stats_available", NULL, has_stats);

    if (!has_stats) {
        ok = ok && nmo_cli_record_raw(rec, "\nLoad statistics unavailable\n");
    } else {
        ok = ok && nmo_cli_record_raw(rec, "\n");
        ok = ok && nmo_cli_record_uint(rec, "total_objects", "Total Objects",
                                       stats.total_objects);

        ok = ok && nmo_cli_record_raw(rec, "\nReferences:\n");
        nmo_cli_record_t *refs = ok ? nmo_cli_record_object(rec, "references") : NULL;
        ok = refs != NULL;
        if (ok) {
            nmo_cli_record_set_key_width(refs, 14);
        }
        ok = ok && nmo_cli_record_uint(refs, "total", "  Total",
                                       stats.references.total);
        ok = ok && nmo_cli_record_uint(refs, "resolved", "  Resolved",
                                       stats.references.resolved);
        ok = ok && nmo_cli_record_uint(refs, "unresolved", "  Unresolved",
                                       stats.references.unresolved);
        ok = ok && nmo_cli_record_uint(refs, "ambiguous", "  Ambiguous",
                                       stats.references.ambiguous);

        ok = ok && nmo_cli_record_raw(rec, "\nIndexes:\n");
        nmo_cli_record_t *idx = ok ? nmo_cli_record_object(rec, "indexes") : NULL;
        ok = idx != NULL;
        if (ok) {
            nmo_cli_record_set_key_width(idx, 14);
        }
        ok = ok && nmo_cli_record_uint(idx, "class_entries", "  Classes",
                                       stats.indexes.class_entries);
        ok = ok && nmo_cli_record_uint(idx, "name_entries", "  Names",
                                       stats.indexes.name_entries);
        ok = ok && nmo_cli_record_uint(idx, "guid_entries", "  GUIDs",
                                       stats.indexes.guid_entries);
        ok = ok && nmo_cli_record_uint(idx, "memory_usage", "  Memory",
                                       stats.indexes.memory_usage);
        ok = ok && nmo_cli_record_set_text_fmt(idx, "%zu bytes",
                                               stats.indexes.memory_usage);

        ok = ok && nmo_cli_record_raw(rec, "\n");
        ok = ok && nmo_cli_record_uint(rec, "manager_errors", "Manager Errors",
                                       stats.manager_errors);
    }
    ok = ok && debug_add_load_phase_stats(rec, phase_stats);

    int rc = debug_emit(c, rec, ok, "debug.load-phases", 16);
    return close_ctx ? nmo_cmd_ctx_done(c, rc) : rc;
}

int nmo_cmd_debug_load_phases(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    nmo_load_profile_t profile;
    rc = debug_parse_load_profile(argc, argv, &profile);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    c.file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!c.file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_load_perf_stats_t phase_stats;
    nmo_load_perf_stats_reset(&phase_stats);

    nmo_load_options_t load_opts = nmo_load_options_default();
    load_opts.profile = profile;
    load_opts.collect_perf_stats = true;
    load_opts.perf_stats = &phase_stats;

    char *open_error = NULL;
    if (!nmo_tool_open_document_opts(c.file_path, &load_opts,
                                     &c.ctx, &c.document, &c.workspace,
                                     &open_error)) {
        fprintf(stderr, "Error: %s\n", open_error ? open_error : "Failed to open file");
        free(open_error);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }
    c.owns_document = true;
    c.registry = nmo_context_get_type_registry(c.ctx);
    return debug_load_phases_run_in_ctx(&c, profile, &phase_stats, true);
}

static int nmo_cmd_debug_load_phases_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    nmo_load_profile_t profile;
    int rc = debug_parse_load_profile(argc, argv, &profile);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    return debug_load_phases_run_in_ctx(ctx, profile, NULL, false);
}

/* ============================================================================
 * debug chunks - Iterate objects to list chunk debug info
 * ============================================================================ */

static int debug_chunks_run_in_ctx(nmo_cmd_ctx_t *c, bool close_ctx)
{
    static const nmo_cli_table_col_t columns[] = {
        {"ObjectID", NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"ClassID", NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
        {"DataSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
        {"PackSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
        {"Options", NMO_CLI_ALIGN_LEFT, 8, 32},
    };

    debug_item_list_t items = {0};
    nmo_core_iter_result_t result = {0};
    int rc = nmo_core_object_query_run(c, NULL, debug_chunks_object,
                                       &items, &result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        (void)debug_item_list_to_array(&items, NULL, NULL, NULL, 0);
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t chunk_count = items.count;
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "Chunk Debug Info");
    ok = ok && nmo_cli_record_uint(rec, "object_count", NULL, result.matched);
    ok = ok && nmo_cli_record_uint(rec, "chunk_count", NULL, chunk_count);
    ok = ok && nmo_cli_record_raw_fmt(rec, "Chunks: %zu (from %zu objects)\n\n",
                                      chunk_count, result.matched);
    ok = debug_item_list_to_array(&items, ok ? rec : NULL, "chunks", columns,
                                  sizeof(columns) / sizeof(columns[0]));

    rc = debug_emit(c, rec, ok, "debug.chunks", 0);
    return close_ctx ? nmo_cmd_ctx_done(c, rc) : rc;
}

int nmo_cmd_debug_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_chunks_run_in_ctx(&c, true);
}

static int nmo_cmd_debug_chunks_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return debug_chunks_run_in_ctx(ctx, false);
}

/* ============================================================================
 * debug objects
 * ============================================================================ */

static int debug_objects_run_in_ctx(nmo_cmd_ctx_t *c, bool close_ctx)
{
    static const nmo_cli_table_col_t columns[] = {
        {"Idx", NMO_CLI_ALIGN_RIGHT, 4, 0},
        {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"Flags", NMO_CLI_ALIGN_RIGHT, 10, 0},
        {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
        {"Name", NMO_CLI_ALIGN_LEFT, 20, 40},
        {"Chunk", NMO_CLI_ALIGN_RIGHT, 8, 0},
    };

    debug_item_list_t items = {0};
    nmo_core_iter_result_t result = {0};
    int rc = nmo_core_object_query_run(c, NULL, debug_objects_object,
                                       &items, &result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        (void)debug_item_list_to_array(&items, NULL, NULL, NULL, 0);
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "Object Debug Info");
    ok = ok && nmo_cli_record_uint(rec, "object_count", NULL, result.matched);
    ok = ok && nmo_cli_record_raw_fmt(rec, "Objects: %zu\n\n", result.matched);
    ok = debug_item_list_to_array(&items, ok ? rec : NULL, "objects", columns,
                                  sizeof(columns) / sizeof(columns[0]));

    rc = debug_emit(c, rec, ok, "debug.objects", 0);
    return close_ctx ? nmo_cmd_ctx_done(c, rc) : rc;
}

int nmo_cmd_debug_objects(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_objects_run_in_ctx(&c, true);
}

static int nmo_cmd_debug_objects_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return debug_objects_run_in_ctx(ctx, false);
}

/* ============================================================================
 * debug export
 * ============================================================================ */

static int debug_export_parse(int argc, char **argv,
                              bool *include_data,
                              size_t *max_bytes)
{
    static const nmo_opt_def_t opts[] = {
        {"--data",      "--include-data", NMO_OPT_FLAG, "Include chunk data"},
        {"--max-bytes", NULL,             NMO_OPT_UINT, "Max bytes for data dump (default: 4096)"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    *include_data = vals[0].val.flag;
    *max_bytes = vals[1].present ? (size_t)vals[1].val.u : 4096;
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_export_run_in_ctx(nmo_cmd_ctx_t *c,
                                   bool include_data,
                                   size_t max_bytes,
                                   bool close_ctx)
{
    nmo_debug_export_data_t export_data = {
        .include_data = include_data,
        .max_bytes = max_bytes,
    };
    nmo_core_iter_result_t result = {0};
    int rc = nmo_core_object_query_run(c, NULL, debug_export_object,
                                       &export_data, &result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        (void)debug_item_list_to_array(&export_data.items, NULL, NULL, NULL, 0);
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_str(rec, "file", NULL, c->file_path);
    ok = ok && nmo_cli_record_bool(rec, "include_data", NULL, include_data);
    ok = ok && nmo_cli_record_uint(rec, "max_bytes", NULL, max_bytes);
    ok = ok && nmo_cli_record_uint(rec, "object_count", NULL, result.matched);
    ok = debug_item_list_to_array(&export_data.items, ok ? rec : NULL,
                                  "objects", NULL, 0);

    /* The snapshot is JSON in every output format. */
    nmo_cmd_ctx_t json_ctx = *c;
    json_ctx.is_json = true;
    rc = debug_emit(&json_ctx, rec, ok, "debug.export", 0);

    if (rc == NMO_CLI_EXIT_SUCCESS && !c->is_json && c->global &&
        c->global->output_path) {
        fprintf(stdout, "Exported %zu objects to %s\n", result.matched, c->global->output_path);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, rc) : rc;
}

int nmo_cmd_debug_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    bool include_data = false;
    size_t max_bytes = 4096;
    int rc = debug_export_parse(argc, argv, &include_data, &max_bytes);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_export_run_in_ctx(&c, include_data, max_bytes, true);
}

int nmo_cmd_debug_probe(int argc,
                        char **argv,
                        const nmo_cli_global_opts_t *global)
{
    nmo_debug_probe_args_t args;
    const char *input_path = NULL;
    const char *output_path = NULL;
    bool dry_run = false;
    int rc = debug_probe_parse(
        argc, argv, &args, &input_path, &output_path, &dry_run);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "debug.probe",
        .output_required_unless_dry_run = true,
        .should_save = NULL,
    };
    return nmo_cli_run_write_command(
        input_path,
        output_path,
        dry_run,
        global,
        &spec,
        debug_probe_mutate,
        debug_probe_report,
        &args);
}

static int nmo_cmd_debug_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    bool include_data = false;
    size_t max_bytes = 4096;
    int rc = debug_export_parse(argc, argv, &include_data, &max_bytes);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    return debug_export_run_in_ctx(ctx, include_data, max_bytes, false);
}

int nmo_cmd_debug_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: debug load-phases|chunks|objects|export|probe ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "load-phases") == 0 || strcmp(argv[0], "lp") == 0) {
        return nmo_cmd_debug_load_phases_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "chunks") == 0 || strcmp(argv[0], "ch") == 0) {
        return nmo_cmd_debug_chunks_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "objects") == 0 || strcmp(argv[0], "obj") == 0) {
        return nmo_cmd_debug_objects_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "export") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_debug_export_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported debug read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

