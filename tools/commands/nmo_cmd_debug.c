/**
 * @file nmo_cmd_debug.c
 * @brief CLI debug command group implementation (non-interactive diagnostics)
 */

#include "nmo_cmd_debug.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_json.h"
#include "../nmo_cli_output.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_tool_common.h"
#include "../nmo_tool_session.h"
#include "../nmo_opt.h"

#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_behavior_registry.h"
#include "nmo.h"
#include "document/nmo_document_stats.h"
#include "document/nmo_document_save.h"
#include "runtime/nmo_context.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

typedef struct nmo_debug_chunks_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *chunks;
    nmo_cli_table_t *table;
    size_t chunk_count;
} nmo_debug_chunks_data_t;

typedef struct nmo_debug_probe_args {
    const char *kind;
    nmo_object_id_t behavior_id;
    nmo_object_id_t remove_link_id;
    nmo_object_id_t from_io_id;
    nmo_object_id_t to_io_id;
    nmo_object_id_t message_node_id;
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
        args->from_io_id = link_state->in_io_id;
    }
    if (args->to_io_id == 0u) {
        args->to_io_id = link_state->out_io_id;
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
    size_t object_count = 0u;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    for (size_t i = 0; objects != NULL && i < object_count; ++i) {
        nmo_object_t *object = objects[i];
        if (object == NULL ||
            nmo_object_get_class_id(object) != NMO_CID_BEHAVIORLINK) {
            continue;
        }
        const nmo_behaviorlink_state_t *state =
            (const nmo_behaviorlink_state_t *)nmo_object_get_state(object);
        if (state != NULL &&
            state->in_io_id == wanted->in_io_id &&
            state->out_io_id == wanted->out_io_id &&
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

static bool debug_probe_behavior_has_io(const nmo_behavior_state_t *state,
                                        nmo_object_id_t io_id)
{
    return state != NULL && io_id != 0u &&
           (nmo_array_find(&state->inputs, &io_id, NULL) != 0 ||
            nmo_array_find(&state->outputs, &io_id, NULL) != 0);
}

static nmo_status_t debug_probe_validate_targets(
    nmo_cmd_ctx_t *ctx,
    const nmo_debug_probe_args_t *args,
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
        if (args->remove_link_id != 0u) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, args->remove_link_id);
            const nmo_behaviorlink_state_t *link_state = link_obj != NULL &&
                nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                    ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(
                          link_obj)
                    : NULL;
            if (link_state == NULL ||
                (!debug_probe_behavior_has_io(message_state,
                                              link_state->in_io_id) &&
                 !debug_probe_behavior_has_io(message_state,
                                              link_state->out_io_id))) {
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
        strcmp(args->kind, "message-logger") != 0 ||
        args->message_node_id != 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    nmo_object_t *behavior_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, args->behavior_id)
        : NULL;
    const nmo_behavior_state_t *behavior =
        behavior_obj != NULL &&
                nmo_object_get_class_id(behavior_obj) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(behavior_obj)
            : NULL;
    if (behavior == NULL) {
        return NMO_OK;
    }

    nmo_object_id_t selected_id = 0u;
    size_t candidate_count = 0u;
    for (size_t i = 0; i < behavior->sub_behaviors.count; ++i) {
        nmo_object_id_t child_id =
            ((const nmo_object_id_t *)behavior->sub_behaviors.data)[i];
        nmo_object_t *child_obj =
            nmo_object_repository_find_by_id(repo, child_id);
        const nmo_behavior_state_t *child =
            child_obj != NULL &&
                    nmo_object_get_class_id(child_obj) == NMO_CID_BEHAVIOR
                ? (const nmo_behavior_state_t *)nmo_object_get_state(child_obj)
                : NULL;
        if (!debug_probe_is_message_behavior(ctx, child)) {
            continue;
        }
        selected_id = child_id;
        ++candidate_count;
    }

    if (candidate_count == 1u) {
        args->message_node_id = selected_id;
        return NMO_OK;
    }
    if (candidate_count == 0u) {
        NMO_RETURN_ERROR(
            NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR,
            "debug probe message selector found no message candidates");
    }
    NMO_RETURN_ERROR(
        NMO_ERR_INVALID_ARGUMENT,
        NMO_SEVERITY_ERROR,
        "debug probe message selector is ambiguous");
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
    char errbuf[256];
    if (!nmo_tool_open_document(output_path,
                                &saved_ctx,
                                &saved_document,
                                &saved_workspace,
                                errbuf,
                                sizeof(errbuf))) {
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
    char data_cell_text[128];

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
        status = debug_probe_analyze_message_selector(ctx, args);
    }
    if (status == NMO_OK) {
        status = debug_probe_validate_targets(ctx, args, spec);
    }
    if (status == NMO_OK) {
        status = nmo_edit_plan_create(&plan);
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
        snprintf(data_cell_text, sizeof(data_cell_text),
                 "dataarray:%u[%u,%u]",
                 (unsigned)args->dataarray_id,
                 (unsigned)args->data_row,
                 (unsigned)args->data_col);
        probe_text = data_cell_text;
    }
    if (status == NMO_OK && probe_text != NULL && spec->text_handle != NULL) {
        status = nmo_edit_plan_add_set_parameter_value_from_handle(
            plan, node_op_index, spec->text_handle, probe_text, NULL);
    }
    if (status == NMO_OK && spec->connects_parameter &&
        args->parameter_id != 0u && spec->text_handle != NULL) {
        status = nmo_edit_plan_add_connect_parameter_to_handle(
            plan, args->parameter_id, node_op_index, spec->text_handle);
    }
    if (status == NMO_OK && args->from_io_id != 0u) {
        status = nmo_edit_plan_add_behavior_link_to_handle(
            plan,
            args->behavior_id,
            args->from_io_id,
            node_op_index,
            spec->input_handle,
            args->has_delay ? args->delay : 0u);
    }
    if (status == NMO_OK && args->to_io_id != 0u) {
        status = nmo_edit_plan_add_behavior_link_from_handle(
            plan,
            args->behavior_id,
            node_op_index,
            spec->output_handle,
            args->to_io_id,
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
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: debug probe failed: %s\n",
                (message != NULL && message[0] != '\0')
                    ? message
                    : nmo_error_string(status));
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

    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        if (!dry_run && output_path != NULL) {
            (void)debug_probe_reconcile_saved_link_ids(
                ctx, output_path, args);
        }
        if (!dry_run && output_path != NULL && args->report.output_path == NULL) {
            (void)nmo_edit_report_set_output_path(&args->report, output_path);
        }
        nmo_cli_edit_report_add_schema_v2_json(
            doc, data, &args->report, dry_run);
        nmo_cli_json_add_str_safe(doc, data, "probe_kind", args->kind);
        yyjson_mut_obj_add_uint(doc, data, "behavior_id",
                                (uint64_t)args->behavior_id);
        if (args->message_node_id != 0u) {
            yyjson_mut_obj_add_uint(doc, data, "message_node_id",
                                    (uint64_t)args->message_node_id);
            nmo_cli_json_add_str_safe(
                doc, data, "probe_selector", "message_flow");
        }
        int rc = nmo_cmd_ctx_json_end(ctx, doc, data, "debug.probe");
        nmo_edit_report_dispose(&args->report);
        return rc;
    }

    fprintf(ctx->out, "%sInjected %zu debug probe operation(s)\n",
            dry_run ? "[dry-run] " : "",
            args->report.operation_count);
    if (!dry_run && output_path != NULL) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    nmo_edit_report_dispose(&args->report);
    return NMO_CLI_EXIT_SUCCESS;
}

static void debug_add_load_phase_stats_json(yyjson_mut_doc *doc,
                                            yyjson_mut_val *data,
                                            const nmo_load_perf_stats_t *stats) {
    if (doc == NULL || data == NULL || stats == NULL) {
        return;
    }

    yyjson_mut_val *phase_stats = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, phase_stats, "packed_header1_bytes", (uint64_t)stats->packed_header1_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "unpacked_header1_bytes", (uint64_t)stats->unpacked_header1_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "packed_data_bytes", (uint64_t)stats->packed_data_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "unpacked_data_bytes", (uint64_t)stats->unpacked_data_bytes);

    yyjson_mut_val *phases = yyjson_mut_obj(doc);
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, entry, "calls", phase->calls);
        yyjson_mut_obj_add_real(doc, entry, "milliseconds", phase->milliseconds);
        yyjson_mut_obj_add_val(doc, phases, nmo_load_perf_phase_name((nmo_load_perf_phase_t)i), entry);
    }
    yyjson_mut_obj_add_val(doc, phase_stats, "phases", phases);
    yyjson_mut_obj_add_val(doc, data, "phase_stats", phase_stats);
}

static void debug_print_load_phase_stats(FILE *out, const nmo_load_perf_stats_t *stats) {
    if (out == NULL || stats == NULL) {
        return;
    }

    fprintf(out, "\nPhase Timings:\n");
    fprintf(out, "  %-28s %8s %12s\n", "phase", "calls", "ms");
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        fprintf(out, "  %-28s %8llu %12.3f\n",
                nmo_load_perf_phase_name((nmo_load_perf_phase_t)i),
                (unsigned long long)phase->calls,
                phase->milliseconds);
    }

    fprintf(out, "\nSection Bytes:\n");
    fprintf(out, "  Header1: packed=%zu unpacked=%zu\n",
            stats->packed_header1_bytes,
            stats->unpacked_header1_bytes);
    fprintf(out, "  Data:    packed=%zu unpacked=%zu\n",
            stats->packed_data_bytes,
            stats->unpacked_data_bytes);
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

    nmo_debug_chunks_data_t *data = (nmo_debug_chunks_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        return 0;
    }

    if (data->doc && data->chunks) {
        yyjson_mut_val *cv = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, cv, "id", nmo_object_get_id(obj));
        yyjson_mut_obj_add_uint(data->doc, cv, "class_id", chunk->class_id);
        yyjson_mut_obj_add_uint(data->doc, cv, "data_size",
                                (uint64_t)nmo_chunk_get_data_size(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "compressed_size",
                                (uint64_t)chunk->compressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "options", chunk->chunk_options);

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
        if (class_name) {
            yyjson_mut_obj_add_str(data->doc, cv, "class_name", class_name);
        }

        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(data->doc, cv, "name", name);
        }

        yyjson_mut_arr_add_val(data->chunks, cv);
    } else if (data->table) {
        char oid[16], cid[16], dsz[16], csz[16];
        char opt_buf[64];
        char opt_cell[96];
        snprintf(oid, sizeof(oid), "%u", nmo_object_get_id(obj));
        snprintf(cid, sizeof(cid), "%u", chunk->class_id);
        snprintf(dsz, sizeof(dsz), "%zu", nmo_chunk_get_data_size(chunk));
        snprintf(csz, sizeof(csz), "%zu", chunk->compressed_size);

        const char *opt = nmo_cli_chunk_options_to_string(chunk->chunk_options,
            opt_buf, sizeof(opt_buf));
        if (chunk->chunk_options == 0) {
            snprintf(opt_cell, sizeof(opt_cell), "-");
        } else {
            snprintf(opt_cell, sizeof(opt_cell), "%s (0x%04X)", opt, chunk->chunk_options);
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);

        const char *cells[] = {oid, cid, class_name ? class_name : "-", dsz, csz, opt_cell};
        (void)nmo_cli_table_add_row(data->table, cells, 6);
    }

    data->chunk_count++;
    return 0;
}

typedef struct nmo_debug_objects_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *objects;
    nmo_cli_table_t *table;
} nmo_debug_objects_data_t;

static int debug_objects_object(size_t index, nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c, void *user)
{
    nmo_debug_objects_data_t *data = (nmo_debug_objects_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    if (data->doc && data->objects) {
        yyjson_mut_val *o = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, o, "index", (uint64_t)index);
        yyjson_mut_obj_add_uint(data->doc, o, "id", nmo_object_get_id(obj));
        yyjson_mut_obj_add_uint(data->doc, o, "class_id", nmo_object_get_class_id(obj));
        yyjson_mut_obj_add_uint(data->doc, o, "flags", nmo_object_get_flags(obj));

        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(data->doc, o, "name", name);
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(data->doc, o, "class_name", class_name);
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        yyjson_mut_obj_add_bool(data->doc, o, "has_chunk", chunk != NULL);
        if (chunk) {
            yyjson_mut_obj_add_uint(data->doc, o, "chunk_size",
                                    (uint64_t)nmo_chunk_get_data_size(chunk));
        }

        yyjson_mut_arr_add_val(data->objects, o);
    } else if (data->table) {
        char idx[24], id[16], flags[16], chunk_sz[24];
        snprintf(idx, sizeof(idx), "%zu", index);
        snprintf(id, sizeof(id), "%u", nmo_object_get_id(obj));
        snprintf(flags, sizeof(flags), "0x%08X", nmo_object_get_flags(obj));

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (chunk) {
            snprintf(chunk_sz, sizeof(chunk_sz), "%zu", nmo_chunk_get_data_size(chunk));
        } else {
            snprintf(chunk_sz, sizeof(chunk_sz), "-");
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
        const char *name = nmo_object_get_name(obj);

        const char *cells[] = {
            idx, id, flags,
            class_name ? class_name : "-",
            (name && name[0]) ? name : "-",
            chunk_sz
        };
        (void)nmo_cli_table_add_row(data->table, cells, 6);
    }

    return 0;
}

typedef struct nmo_debug_export_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *objects;
    bool include_data;
    size_t max_bytes;
} nmo_debug_export_data_t;

static int debug_export_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    nmo_debug_export_data_t *data = (nmo_debug_export_data_t *)user;
    if (!data || !data->doc || !data->objects || !obj) {
        return 0;
    }

    yyjson_mut_val *o = yyjson_mut_obj(data->doc);
    yyjson_mut_obj_add_uint(data->doc, o, "index", (uint64_t)index);
    yyjson_mut_obj_add_uint(data->doc, o, "id", nmo_object_get_id(obj));
    yyjson_mut_obj_add_uint(data->doc, o, "class_id", nmo_object_get_class_id(obj));
    yyjson_mut_obj_add_uint(data->doc, o, "flags", nmo_object_get_flags(obj));

    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) {
        nmo_cli_json_add_str_safe(data->doc, o, "name", name);
    }

    const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
    if (class_name) {
        yyjson_mut_obj_add_str(data->doc, o, "class_name", class_name);
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (chunk) {
        yyjson_mut_val *cv = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, cv, "class_id", chunk->class_id);
        yyjson_mut_obj_add_uint(data->doc, cv, "data_size",
                                (uint64_t)nmo_chunk_get_data_size(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "compressed_size",
                                (uint64_t)chunk->compressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "uncompressed_size",
                                (uint64_t)chunk->uncompressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "options", (uint64_t)chunk->chunk_options);
        yyjson_mut_obj_add_uint(data->doc, cv, "id_count",
                                (uint64_t)nmo_chunk_get_id_count(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "subchunk_count",
                                (uint64_t)nmo_chunk_get_sub_chunk_count(chunk));

        if (data->include_data) {
            size_t data_size = 0;
            const uint8_t *chunk_data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
            (void)nmo_cli_json_add_data_hex(data->doc, cv, chunk_data,
                                            data_size, data->max_bytes, false);
        }

        yyjson_mut_obj_add_val(data->doc, o, "chunk", cv);
    }

    yyjson_mut_arr_add_val(data->objects, o);
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_str(doc, data, "profile", debug_load_profile_name(profile));
        yyjson_mut_obj_add_bool(doc, data, "stats_available", has_stats);

        if (has_stats) {
            yyjson_mut_obj_add_uint(doc, data, "total_objects", (uint64_t)stats.total_objects);

            yyjson_mut_val *refs = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, refs, "total", stats.references.total);
            yyjson_mut_obj_add_uint(doc, refs, "resolved", stats.references.resolved);
            yyjson_mut_obj_add_uint(doc, refs, "unresolved", stats.references.unresolved);
            yyjson_mut_obj_add_uint(doc, refs, "ambiguous", stats.references.ambiguous);
            yyjson_mut_obj_add_val(doc, data, "references", refs);

            yyjson_mut_val *idx = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, idx, "class_entries", (uint64_t)stats.indexes.class_entries);
            yyjson_mut_obj_add_uint(doc, idx, "name_entries", (uint64_t)stats.indexes.name_entries);
            yyjson_mut_obj_add_uint(doc, idx, "guid_entries", (uint64_t)stats.indexes.guid_entries);
            yyjson_mut_obj_add_uint(doc, idx, "memory_usage", (uint64_t)stats.indexes.memory_usage);
            yyjson_mut_obj_add_val(doc, data, "indexes", idx);

            yyjson_mut_obj_add_uint(doc, data, "manager_errors", stats.manager_errors);
        }
        debug_add_load_phase_stats_json(doc, data, phase_stats);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.load-phases");
    } else {
        nmo_cli_print_heading(c->out, "Load Phases", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 16, c->colorize);
        nmo_cli_print_kv(c->out, "Profile", debug_load_profile_name(profile), 16, c->colorize);

        if (!has_stats) {
            fprintf(c->out, "\nLoad statistics unavailable\n");
        } else {
            char buf[64];
            fprintf(c->out, "\n");

            snprintf(buf, sizeof(buf), "%zu", stats.total_objects);
            nmo_cli_print_kv(c->out, "Total Objects", buf, 16, c->colorize);

            fprintf(c->out, "\nReferences:\n");
            snprintf(buf, sizeof(buf), "%u", stats.references.total);
            nmo_cli_print_kv(c->out, "  Total", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.resolved);
            nmo_cli_print_kv(c->out, "  Resolved", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.unresolved);
            nmo_cli_print_kv(c->out, "  Unresolved", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.ambiguous);
            nmo_cli_print_kv(c->out, "  Ambiguous", buf, 14, c->colorize);

            fprintf(c->out, "\nIndexes:\n");
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.class_entries);
            nmo_cli_print_kv(c->out, "  Classes", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.name_entries);
            nmo_cli_print_kv(c->out, "  Names", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.guid_entries);
            nmo_cli_print_kv(c->out, "  GUIDs", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", stats.indexes.memory_usage);
            nmo_cli_print_kv(c->out, "  Memory", buf, 14, c->colorize);

            fprintf(c->out, "\n");
            snprintf(buf, sizeof(buf), "%u", stats.manager_errors);
            nmo_cli_print_kv(c->out, "Manager Errors", buf, 16, c->colorize);
        }
        debug_print_load_phase_stats(c->out, phase_stats);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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

    char errbuf[256];
    if (!nmo_tool_open_document_opts(c.file_path, &load_opts,
                                     &c.ctx, &c.document, &c.workspace,
                                     errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
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
    int rc = NMO_CLI_EXIT_SUCCESS;
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *chunks = yyjson_mut_arr(doc);
        nmo_debug_chunks_data_t chunks_data = {
            .doc = doc,
            .chunks = chunks,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_chunks_object,
                                       &chunks_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_uint(doc, data, "chunk_count",
                                (uint64_t)chunks_data.chunk_count);
        yyjson_mut_obj_add_val(doc, data, "chunks", chunks);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.chunks");
    } else {
        nmo_cli_print_heading(c->out, "Chunk Debug Info", c->colorize);

        static const nmo_cli_table_col_t columns[] = {
            {"ObjectID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"ClassID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
            {"DataSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"PackSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Options", NMO_CLI_ALIGN_LEFT, 8, 32},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        nmo_debug_chunks_data_t chunks_data = {
            .table = &table,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_chunks_object,
                                       &chunks_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(c->out, "Chunks: %zu (from %zu objects)\n\n",
                chunks_data.chunk_count, result.matched);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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
    int rc = NMO_CLI_EXIT_SUCCESS;
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *objs = yyjson_mut_arr(doc);
        nmo_debug_objects_data_t objects_data = {
            .doc = doc,
            .objects = objs,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_objects_object,
                                       &objects_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_val(doc, data, "objects", objs);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.objects");
    } else {
        nmo_cli_print_heading(c->out, "Object Debug Info", c->colorize);

        static const nmo_cli_table_col_t columns[] = {
            {"Idx", NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Flags", NMO_CLI_ALIGN_RIGHT, 10, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 40},
            {"Chunk", NMO_CLI_ALIGN_RIGHT, 8, 0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        nmo_debug_objects_data_t objects_data = {
            .table = &table,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_objects_object,
                                       &objects_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(c->out, "Objects: %zu\n\n", result.matched);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
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
    yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
    yyjson_mut_obj_add_bool(doc, data, "include_data", include_data);
    yyjson_mut_obj_add_uint(doc, data, "max_bytes", (uint64_t)max_bytes);

    yyjson_mut_val *objs = yyjson_mut_arr(doc);
    nmo_debug_export_data_t export_data = {
        .doc = doc,
        .objects = objs,
        .include_data = include_data,
        .max_bytes = max_bytes,
    };
    nmo_core_iter_result_t result = {0};
    int rc = nmo_core_object_query_run(c, NULL, debug_export_object,
                                       &export_data, &result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        yyjson_mut_doc_free(doc);
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
    yyjson_mut_obj_add_val(doc, data, "objects", objs);
    nmo_cmd_ctx_json_end(c, doc, data, "debug.export");

    if (!c->is_json && c->global && c->global->output_path) {
        fprintf(stdout, "Exported %zu objects to %s\n", result.matched, c->global->output_path);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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

