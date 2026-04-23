/**
 * @file nmo_cmd_behavior_show.c
 * @brief CLI behavior show command implementation
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "behavior/nmo_behavior_analyze.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operations.h"
#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_behavior_analyze.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void behavior_show_guid_to_string(nmo_guid_t guid, char *buf, size_t size) {
    if (!buf || size == 0) {
        return;
    }
    snprintf(buf, size, "%08X-%08X", guid.d1, guid.d2);
}

static void behavior_show_add_decoded_value_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry,
    const nmo_workspace_t *workspace)
{
    if (!doc || !item || !param || !param->has_state) {
        return;
    }

    char value_buf[256];
    if (nmo_behavior_param_value_to_string(param, registry, workspace,
                                  value_buf, sizeof(value_buf)) == NMO_OK &&
        value_buf[0] != '\0') {
        nmo_cli_json_add_str_safe(doc, item, "decoded_value", value_buf);
    }
}

static void behavior_show_add_source_chain_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    nmo_workspace_t *workspace,
    const nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    nmo_object_id_t param_id)
{
    if (!doc || !item || !workspace || !repo || param_id == 0) {
        return;
    }

    nmo_array_t chain;
    if (nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 8, NULL) != NMO_OK) {
        return;
    }

    if (nmo_behavior_analyze_trace_param_chain(workspace, param_id,
                                               &chain, 32) == NMO_OK &&
        chain.count > 0) {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        const nmo_behavior_trace_step_t *steps =
            (const nmo_behavior_trace_step_t *)chain.data;
        for (size_t i = 0; i < chain.count; i++) {
            yyjson_mut_val *step = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, step, "id", steps[i].id);
            nmo_cli_json_add_str_safe(doc, step, "name",
                                      resolve_name(repo, steps[i].id));
            nmo_object_t *obj =
                nmo_object_repository_find_by_id(repo, steps[i].id);
            nmo_guid_t type_guid = get_param_type_guid(obj);
            char guid_buf[24];
            behavior_show_guid_to_string(type_guid, guid_buf, sizeof(guid_buf));
            nmo_cli_json_add_str_safe(doc, step, "type_guid", guid_buf);
            nmo_cli_json_add_str_safe(doc, step, "type_name",
                                      resolve_type(registry, type_guid));
            bool is_shared = false;
            if (obj && nmo_object_get_class_id(obj) == NMO_CID_PARAMETERIN) {
                const nmo_parameterin_state_t *pin =
                    (const nmo_parameterin_state_t *)nmo_object_get_state(obj);
                is_shared = pin && pin->is_shared;
            }
            yyjson_mut_obj_add_bool(doc, step, "is_shared", is_shared);
            yyjson_mut_obj_add_uint(doc, step, "owner_id", steps[i].owner_id);
            nmo_cli_json_add_str_safe(doc, step, "owner_name",
                                      resolve_name(repo, steps[i].owner_id));
            yyjson_mut_arr_add_val(arr, step);
        }
        yyjson_mut_obj_add_val(doc, item, "source_chain", arr);
    }

    nmo_array_dispose(&chain);
}

static const char *behavior_show_operation_token(const char *op_name) {
    return (op_name && op_name[0]) ? op_name : "OP";
}

static const char *behavior_show_operation_name(
    const nmo_type_registry_t *registry,
    nmo_guid_t operation_guid)
{
    const char *name = nmo_type_registry_guid_to_name(registry, operation_guid);
    if (name && name[0]) {
        return name;
    }
    if (nmo_guid_equals(operation_guid, NMO_OP_GUID_ADD)) {
        return "Addition";
    }
    if (operation_guid.d1 == 0x556A69AFu &&
        operation_guid.d2 == 0x076E3F09u) {
        return "Get Length";
    }
    return "Unknown Operation";
}

static bool behavior_show_operation_is_copy_like(const char *op_name) {
    return op_name &&
        (strcmp(op_name, "Copy") == 0 ||
         strcmp(op_name, "Identity") == 0 ||
         strcmp(op_name, "Set") == 0);
}

static void behavior_show_format_operation(
    char *buf,
    size_t size,
    const char *op_name,
    const char *in1_name,
    bool has_in1,
    const char *in2_name,
    bool has_in2,
    const char *out_name,
    bool has_out,
    const char *out_type)
{
    if (!buf || size == 0) {
        return;
    }

    const char *op_token = behavior_show_operation_token(op_name);
    const char *in1 = (in1_name && in1_name[0]) ? in1_name : "[missing in1]";
    const char *in2 = (in2_name && in2_name[0]) ? in2_name : "[missing in2]";
    const char *out = (out_name && out_name[0]) ? out_name : "[missing out]";
    const char *otype = (out_type && out_type[0]) ? out_type : "?";

    if (behavior_show_operation_is_copy_like(op_name)) {
        snprintf(buf, size, "[%s] %s -> %s [COPY]  [%s]",
                 op_token, has_in1 ? in1 : "[missing in1]",
                 has_out ? out : "[missing out]", otype);
    } else if (has_in1 && !has_in2) {
        snprintf(buf, size, "[%s] %s %s -> %s  [%s]",
                 op_token, op_token, in1,
                 has_out ? out : "[missing out]", otype);
    } else {
        snprintf(buf, size, "[%s] %s %s %s -> %s  [%s]",
                 op_token,
                 has_in1 ? in1 : "[missing in1]",
                 op_token,
                 has_in2 ? in2 : "[missing in2]",
                 has_out ? out : "[missing out]",
                 otype);
    }
}

static void behavior_show_add_operation_param_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    const char *prefix,
    nmo_object_id_t param_id)
{
    if (!doc || !item || !repo || !prefix || param_id == 0) {
        return;
    }

    const char *id_key = NULL;
    const char *name_key = NULL;
    const char *guid_key = NULL;
    const char *type_key = NULL;
    if (strcmp(prefix, "in1") == 0) {
        id_key = "in1_id";
        name_key = "in1_name";
        guid_key = "in1_type_guid";
        type_key = "in1_type_name";
    } else if (strcmp(prefix, "in2") == 0) {
        id_key = "in2_id";
        name_key = "in2_name";
        guid_key = "in2_type_guid";
        type_key = "in2_type_name";
    } else if (strcmp(prefix, "out") == 0) {
        id_key = "out_id";
        name_key = "out_name";
        guid_key = "out_type_guid";
        type_key = "out_type_name";
    } else {
        return;
    }

    yyjson_mut_obj_add_uint(doc, item, id_key, param_id);
    nmo_cli_json_add_str_safe(doc, item, name_key, resolve_name(repo, param_id));

    nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
    nmo_guid_t type_guid = get_param_type_guid(param_obj);
    char guid_buf[24];
    behavior_show_guid_to_string(type_guid, guid_buf, sizeof(guid_buf));

    nmo_cli_json_add_str_safe(doc, item, guid_key, guid_buf);
    nmo_cli_json_add_str_safe(doc, item, type_key,
                              resolve_type(registry, type_guid));
}

typedef struct behavior_show_flow_source {
    nmo_object_id_t param_id;
    nmo_object_id_t owner_id;
    const char *owner_name;
    const char *param_name;
} behavior_show_flow_source_t;

static void behavior_show_add_data_flow_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    const nmo_behavior_state_t *bs,
    nmo_object_id_t behavior_id,
    const char *behavior_name)
{
    if (!doc || !data || !repo || !bs) {
        return;
    }

    behavior_show_flow_source_t sources[512];
    size_t source_count = 0;

    if (bs->local_parameters.data) {
        const nmo_object_id_t *ids =
            (const nmo_object_id_t *)bs->local_parameters.data;
        for (size_t i = 0; i < bs->local_parameters.count && source_count < 512; i++) {
            sources[source_count].param_id = ids[i];
            sources[source_count].owner_id = behavior_id;
            sources[source_count].owner_name =
                (behavior_name && behavior_name[0]) ? behavior_name : "(root)";
            sources[source_count].param_name = resolve_name(repo, ids[i]);
            source_count++;
        }
    }

    if (bs->sub_behaviors.data) {
        const nmo_object_id_t *sub_ids =
            (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sub_bs =
                (const nmo_behavior_state_t *)sub->state;
            const char *sub_name = nmo_object_get_name(sub);
            if (!sub_name || !sub_name[0]) sub_name = "(unnamed)";
            const nmo_object_id_t *pids =
                (const nmo_object_id_t *)sub_bs->out_parameters.data;
            for (size_t pi = 0; pi < sub_bs->out_parameters.count && source_count < 512; pi++) {
                sources[source_count].param_id = pids[pi];
                sources[source_count].owner_id = sub_ids[si];
                sources[source_count].owner_name = sub_name;
                sources[source_count].param_name = resolve_name(repo, pids[pi]);
                source_count++;
            }
        }
    }

    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (bs->sub_behaviors.data) {
        const nmo_object_id_t *sub_ids =
            (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sub_bs =
                (const nmo_behavior_state_t *)sub->state;
            const char *sub_name = nmo_object_get_name(sub);
            if (!sub_name || !sub_name[0]) sub_name = "(unnamed)";
            const nmo_object_id_t *pids =
                (const nmo_object_id_t *)sub_bs->in_parameters.data;
            for (size_t pi = 0; pi < sub_bs->in_parameters.count; pi++) {
                nmo_object_t *pin_obj = nmo_object_repository_find_by_id(repo, pids[pi]);
                if (!pin_obj || !pin_obj->state) continue;
                const nmo_parameterin_state_t *pin =
                    (const nmo_parameterin_state_t *)pin_obj->state;
                if (!pin || pin->source_id == 0) continue;

                const behavior_show_flow_source_t *src = NULL;
                for (size_t s = 0; s < source_count; s++) {
                    if (sources[s].param_id == pin->source_id) {
                        src = &sources[s];
                        break;
                    }
                }

                nmo_object_t *src_obj =
                    nmo_object_repository_find_by_id(repo, pin->source_id);
                const char *src_name = src ? src->param_name : resolve_name(repo, pin->source_id);
                const char *src_owner_name = src ? src->owner_name : "(external)";
                nmo_object_id_t src_owner_id = src ? src->owner_id : 0;
                nmo_guid_t type_guid = get_param_type_guid(pin_obj);
                char guid_buf[24];
                behavior_show_guid_to_string(type_guid, guid_buf, sizeof(guid_buf));

                yyjson_mut_val *flow = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, flow, "source_id", pin->source_id);
                nmo_cli_json_add_str_safe(doc, flow, "source_name",
                                          src_name ? src_name : "");
                yyjson_mut_obj_add_uint(doc, flow, "source_owner_id", src_owner_id);
                nmo_cli_json_add_str_safe(doc, flow, "source_owner_name",
                                          src_owner_name ? src_owner_name : "");
                yyjson_mut_obj_add_uint(doc, flow, "target_id", pids[pi]);
                nmo_cli_json_add_str_safe(doc, flow, "target_name",
                                          resolve_name(repo, pids[pi]));
                yyjson_mut_obj_add_uint(doc, flow, "target_owner_id", sub_ids[si]);
                nmo_cli_json_add_str_safe(doc, flow, "target_owner_name", sub_name);
                nmo_cli_json_add_str_safe(doc, flow, "type_guid", guid_buf);
                nmo_cli_json_add_str_safe(doc, flow, "type_name",
                                          resolve_type(registry, type_guid));
                yyjson_mut_obj_add_bool(doc, flow, "is_shared", pin->is_shared != 0);
                if (src_obj && nmo_object_get_class_id(src_obj) == NMO_CID_PARAMETERIN) {
                    yyjson_mut_obj_add_bool(doc, flow, "source_is_parameter_in", true);
                }
                yyjson_mut_arr_add_val(arr, flow);
            }
        }
    }

    yyjson_mut_obj_add_val(doc, data, "data_flow", arr);
}

int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--raw",  NULL, NMO_OPT_FLAG, "Show raw reflection (like object show)"},
        {"--json", "-j", NMO_OPT_FLAG, "JSON output"},
        {"--id",   "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_RAW, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool raw_mode = vals[OPT_RAW].present && vals[OPT_RAW].val.flag;
    if (raw_mode) {
        return nmo_cmd_object_show(argc, argv, global);
    }

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;
    if (!has_selector_opt && positional_id == NULL) {
        fprintf(stderr, "Usage: nmo behavior show [--id <id> | --name <name> | <id>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    if (nmo_session_ensure_behavior_acceleration(c.session) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .required_base_class = NMO_CID_BEHAVIOR,
        .selector_label = "Behavior",
        .type_label = "CKBehavior",
    };
    nmo_object_t *beh = NULL;
    nmo_object_id_t target_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &beh, &target_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo behavior show [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(beh);
    if (!bs) {
        fprintf(stderr, "Error: No state for behavior %u\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(beh);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", target_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");
        yyjson_mut_obj_add_uint(doc, data, "class_id",
                                nmo_object_get_class_id(beh));
        const char *cls_name = nmo_cli_class_name_from_id(
            c.ctx, nmo_object_get_class_id(beh));
        if (cls_name) {
            nmo_cli_json_add_str_safe(doc, data, "class_name", cls_name);
        }

        bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
        bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
        nmo_cli_json_add_str_safe(doc, data, "behavior_type",
                                  is_script ? "Script"
                                            : is_bb ? "BB" : "Graph");
        yyjson_mut_obj_add_uint(doc, data, "flags", bs->flags);
        yyjson_mut_obj_add_int(doc, data, "priority", bs->priority);
        yyjson_mut_obj_add_int(doc, data, "compatible_class_id",
                               bs->compatible_class_id);
        nmo_cmd_behavior_add_interface_diagnostics_json(doc, data, c.session);

        if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
            char guid_buf[24];
            snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                     bs->block_guid.d1, bs->block_guid.d2);
            nmo_cli_json_add_str_safe(doc, data, "bb_guid", guid_buf);
            yyjson_mut_obj_add_uint(doc, data, "bb_version",
                                    bs->block_version);
            const char *proto_name = nmo_behavior_registry_get_name(
                nmo_context_get_bb_registry(c.ctx), bs->block_guid);
            if (proto_name) {
                nmo_cli_json_add_str_safe(doc, data, "bb_proto_name",
                                          proto_name);
            }
        }

        if (bs->target_parameter_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "target_parameter_id",
                                    bs->target_parameter_id);
        }

        /* IO Ports: inputs */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->inputs.data;
            for (size_t i = 0; i < bs->inputs.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_cli_json_add_str_safe(doc, item, "name",
                                          resolve_name(repo, ids[i]));
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "inputs", arr);
        }

        /* IO Ports: outputs */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->outputs.data;
            for (size_t i = 0; i < bs->outputs.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_cli_json_add_str_safe(doc, item, "name",
                                          resolve_name(repo, ids[i]));
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "outputs", arr);
        }

        /* Input parameters */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->in_parameters.data;
            for (size_t i = 0; i < bs->in_parameters.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *p =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *pname = p ? nmo_object_get_name(p) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (pname && pname[0]) ? pname : "");
                nmo_guid_t tg = get_param_type_guid(p);
                nmo_cli_json_add_str_safe(doc, item, "type",
                                          resolve_type(c.registry, tg));
                if (p && nmo_object_get_class_id(p) == NMO_CID_PARAMETERIN) {
                    const nmo_parameterin_state_t *pin =
                        (const nmo_parameterin_state_t *)
                            nmo_object_get_state(p);
                    if (pin && pin->source_id != 0) {
                        yyjson_mut_obj_add_uint(doc, item, "source_id",
                                                pin->source_id);
                        if (pin->is_shared) {
                            yyjson_mut_obj_add_bool(doc, item, "is_shared",
                                                    true);
                        }
                    }
                }
                behavior_show_add_source_chain_json(doc, item, c.workspace,
                                                    c.registry,
                                                    repo, ids[i]);
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "input_parameters", arr);
        }

        /* Output parameters */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->out_parameters.data;
            for (size_t i = 0; i < bs->out_parameters.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *p =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *pname = p ? nmo_object_get_name(p) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (pname && pname[0]) ? pname : "");
                nmo_guid_t tg = get_param_type_guid(p);
                nmo_cli_json_add_str_safe(doc, item, "type",
                                          resolve_type(c.registry, tg));
                if (p && (nmo_object_get_class_id(p) == NMO_CID_PARAMETEROUT ||
                          nmo_object_get_class_id(p) == NMO_CID_PARAMETER)) {
                    behavior_show_add_decoded_value_json(
                        doc, item,
                        (const nmo_parameter_state_t *)nmo_object_get_state(p),
                        c.registry, c.workspace);
                }
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "output_parameters", arr);
        }

        /* Local parameters */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->local_parameters.data;
            for (size_t i = 0; i < bs->local_parameters.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *p =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *pname = p ? nmo_object_get_name(p) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (pname && pname[0]) ? pname : "");
                nmo_guid_t tg = get_param_type_guid(p);
                nmo_cli_json_add_str_safe(doc, item, "type",
                                          resolve_type(c.registry, tg));
                if (p && (nmo_object_get_class_id(p) == NMO_CID_PARAMETERLOCAL ||
                          nmo_object_get_class_id(p) == NMO_CID_PARAMETER)) {
                    behavior_show_add_decoded_value_json(
                        doc, item,
                        (const nmo_parameter_state_t *)nmo_object_get_state(p),
                        c.registry, c.workspace);
                }
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "local_parameters", arr);
        }

        /* Operations */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *op_ids =
                (const nmo_object_id_t *)bs->operations.data;
            for (size_t i = 0; i < bs->operations.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", op_ids[i]);
                nmo_object_t *op_obj =
                    nmo_object_repository_find_by_id(repo, op_ids[i]);
                if (op_obj && op_obj->state) {
                    const nmo_parameteroperation_state_t *op_state =
                        (const nmo_parameteroperation_state_t *)op_obj->state;
                    const char *op_name = behavior_show_operation_name(
                        c.registry, op_state->operation_guid);
                    char guid_buf[24];
                    behavior_show_guid_to_string(op_state->operation_guid,
                                                 guid_buf, sizeof(guid_buf));
                    nmo_cli_json_add_str_safe(doc, item, "operation_guid",
                                              guid_buf);
                    if (op_name) {
                        nmo_cli_json_add_str_safe(doc, item, "operation",
                                                  op_name);
                        nmo_cli_json_add_str_safe(doc, item, "operation_name",
                                                  op_name);
                    }
                    if (op_state->has_in1) {
                        behavior_show_add_operation_param_json(
                            doc, item, repo, c.registry, "in1",
                            op_state->in1_id);
                    }
                    if (op_state->has_in2) {
                        behavior_show_add_operation_param_json(
                            doc, item, repo, c.registry, "in2",
                            op_state->in2_id);
                    }
                    if (op_state->has_out) {
                        behavior_show_add_operation_param_json(
                            doc, item, repo, c.registry, "out",
                            op_state->out_id);
                    }
                }
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "operations", arr);
        }

        /* Sub-behaviors */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->sub_behaviors.data;
            for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *sub =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *sname = sub ? nmo_object_get_name(sub) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (sname && sname[0]) ? sname : "");
                if (sub && sub->state) {
                    const nmo_behavior_state_t *sub_bs =
                        (const nmo_behavior_state_t *)sub->state;
                    bool sub_bb =
                        (sub_bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
                    bool sub_script = (sub_bs->flags & CKBEHAVIOR_SCRIPT) != 0;
                    nmo_cli_json_add_str_safe(doc, item, "type",
                        sub_script ? "Script" : sub_bb ? "BB" : "Graph");
                    if (sub_bb && !nmo_guid_is_null(sub_bs->block_guid)) {
                        const char *proto = nmo_behavior_registry_get_name(
                            nmo_context_get_bb_registry(c.ctx),
                            sub_bs->block_guid);
                        if (proto) {
                            nmo_cli_json_add_str_safe(doc, item,
                                                      "proto_name", proto);
                        }
                    }
                }
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "sub_behaviors", arr);
        }

        /* Behavior links */
        {
            const nmo_behavior_index_t *bidx =
                nmo_session_get_behavior_index(c.session);
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->sub_behavior_links.data;
            for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
                nmo_object_t *link_obj =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                if (!link_obj || !link_obj->state) continue;
                const nmo_behaviorlink_state_t *ls =
                    (const nmo_behaviorlink_state_t *)link_obj->state;
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                yyjson_mut_obj_add_uint(doc, item, "in_io_id", ls->in_io_id);
                yyjson_mut_obj_add_uint(doc, item, "out_io_id",
                                        ls->out_io_id);
                if (bidx) {
                    const nmo_port_owner_t *sp =
                        nmo_behavior_index_find(bidx, ls->in_io_id);
                    const nmo_port_owner_t *tp =
                        nmo_behavior_index_find(bidx, ls->out_io_id);
                    if (sp) {
                        yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                                sp->owner_id);
                    }
                    if (tp) {
                        yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                                tp->owner_id);
                    }
                }
                yyjson_mut_obj_add_int(doc, item, "activation_delay",
                                       ls->activation_delay);
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "behavior_links", arr);
        }

        behavior_show_add_data_flow_json(doc, data, repo, c.registry, bs,
                                         target_id, name);

        if (bs->interface_data) {
            const nmo_interface_body_t *ibody = &bs->interface_data->script.body;
            yyjson_mut_val *comments_arr = yyjson_mut_arr(doc);
            for (size_t ci = 0; ci < ibody->comment_count; ci++) {
                const nmo_interface_comment_t *cm = &ibody->comments[ci];
                yyjson_mut_val *cobj = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, cobj, "index", ci);
                if (cm->text) nmo_cli_json_add_str_safe(doc, cobj, "text", cm->text);
                yyjson_mut_obj_add_real(doc, cobj, "left", (double)cm->left);
                yyjson_mut_obj_add_real(doc, cobj, "top", (double)cm->top);
                yyjson_mut_obj_add_real(doc, cobj, "right", (double)cm->right);
                yyjson_mut_obj_add_real(doc, cobj, "bottom", (double)cm->bottom);
                if (cm->style_flags)
                    yyjson_mut_obj_add_uint(doc, cobj, "style_flags", cm->style_flags);
                yyjson_mut_arr_add_val(comments_arr, cobj);
            }
            yyjson_mut_obj_add_val(doc, data, "comments", comments_arr);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.show");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* Text output: BB signature view */
    char heading[256];
    snprintf(heading, sizeof(heading), "Behavior #%u: %s",
             target_id, (name && name[0]) ? name : "(unnamed)");
    nmo_cli_print_heading(c.out, heading, c.colorize);

    /* Flags and identity */
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
    fprintf(c.out, "  Type: %s\n", is_script ? "Script" : is_bb ? "Building Block" : "Graph");
    if (bs->interface_data && (bs->interface_data->script.flags & NMO_INTERFACE_FLAG_FOLDED))
        fprintf(c.out, "  Layout: Folded\n");
    if (!bs->interface_data) {
        nmo_cmd_behavior_print_interface_diagnostics(c.out, c.session);
    }
    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {

        const char *proto_name = nmo_behavior_registry_get_name(nmo_context_get_bb_registry(c.ctx),bs->block_guid);
        if (proto_name) {
            fprintf(c.out, "  Prototype: %s  {%08X-%08X}  v%u\n",
                    proto_name, bs->block_guid.d1, bs->block_guid.d2, bs->block_version);
        } else {
            fprintf(c.out, "  GUID: {%08X-%08X}  Version: %u\n",
                    bs->block_guid.d1, bs->block_guid.d2, bs->block_version);
        }
    }
    if (bs->compatible_class_id > 0) {
        const char *cls = nmo_core_class_name(&c, (nmo_class_id_t)bs->compatible_class_id);
        fprintf(c.out, "  Target Class: %s (#%d)\n",
                cls ? cls : "?", bs->compatible_class_id);
    }

    /* IO Ports */
    if (bs->inputs.count > 0 || bs->outputs.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "IO Ports", c.colorize);
        const nmo_object_id_t *in_ids = (const nmo_object_id_t *)bs->inputs.data;
        for (size_t i = 0; i < bs->inputs.count; i++) {
            fprintf(c.out, "  bIn  %zu: %s\n", i, resolve_name(repo, in_ids[i]));
        }
        const nmo_object_id_t *out_ids = (const nmo_object_id_t *)bs->outputs.data;
        for (size_t i = 0; i < bs->outputs.count; i++) {
            fprintf(c.out, "  bOut %zu: %s\n", i, resolve_name(repo, out_ids[i]));
        }
    }

    /* Input Parameters */
    if (bs->in_parameters.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Input Parameters", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->in_parameters.data;
        for (size_t i = 0; i < bs->in_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *pname = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tname = resolve_type(c.registry, tg);
            fprintf(c.out, "  pIn  %zu: %-24s  [%s]", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
            /* Show source with shared chain tracing */
            if (p && nmo_object_get_class_id(p) == NMO_CID_PARAMETERIN) {
                const nmo_parameterin_state_t *pin =
                    (const nmo_parameterin_state_t *)nmo_object_get_state(p);
                if (pin && pin->source_id != 0) {
                    if (pin->is_shared) {
                        /* Trace shared chain to find direct source */
                        uint32_t shared_hops = 0;
                        nmo_object_id_t cur_id = pin->source_id;
                        while (shared_hops < 32) {
                            nmo_object_t *cur_obj = nmo_object_repository_find_by_id(repo, cur_id);
                            if (!cur_obj) break;
                            if (nmo_object_get_class_id(cur_obj) != NMO_CID_PARAMETERIN) break;
                            const nmo_parameterin_state_t *cur_pin =
                                (const nmo_parameterin_state_t *)nmo_object_get_state(cur_obj);
                            if (!cur_pin || cur_pin->source_id == 0) break;
                            if (!cur_pin->is_shared) {
                                /* Reached direct source */
                                cur_id = cur_pin->source_id;
                                shared_hops++;
                                break;
                            }
                            cur_id = cur_pin->source_id;
                            shared_hops++;
                        }
                        const char *final_name = resolve_name(repo, cur_id);
                        nmo_object_t *final_obj = nmo_object_repository_find_by_id(repo, cur_id);
                        nmo_guid_t final_tg = get_param_type_guid(final_obj);
                        const char *final_type = resolve_type(c.registry, final_tg);
                        fprintf(c.out, "  <- %s [%s] via %u shared link%s",
                                final_name ? final_name : "?",
                                final_type,
                                shared_hops,
                                shared_hops == 1 ? "" : "s");
                    } else {
                        const char *src = resolve_name(repo, pin->source_id);
                        fprintf(c.out, "  <- %s", src ? src : "?");
                    }
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Output Parameters */
    if (bs->out_parameters.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Output Parameters", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->out_parameters.data;
        for (size_t i = 0; i < bs->out_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *pname = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tname = resolve_type(c.registry, tg);
            fprintf(c.out, "  pOut %zu: %-24s  [%s]", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
            /* Decode value if available */
            if (p && (nmo_object_get_class_id(p) == NMO_CID_PARAMETEROUT ||
                      nmo_object_get_class_id(p) == NMO_CID_PARAMETER)) {
                const nmo_parameter_state_t *ps =
                    (const nmo_parameter_state_t *)nmo_object_get_state(p);
                if (ps && ps->has_state) {
                    char val_buf[256];
                    if (nmo_behavior_param_value_to_string(ps, c.registry, c.workspace,
                                                  val_buf, sizeof(val_buf)) == NMO_OK
                        && val_buf[0] != '\0') {
                        fprintf(c.out, " = %s", val_buf);
                    }
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Local Parameters */
    if (bs->local_parameters.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Local Parameters", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->local_parameters.data;
        for (size_t i = 0; i < bs->local_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *pname = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tname = resolve_type(c.registry, tg);
            fprintf(c.out, "  local %zu: %-24s  [%s]", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
            /* Decode value if available */
            if (p && (nmo_object_get_class_id(p) == NMO_CID_PARAMETERLOCAL ||
                      nmo_object_get_class_id(p) == NMO_CID_PARAMETER)) {
                const nmo_parameter_state_t *lps =
                    (const nmo_parameter_state_t *)nmo_object_get_state(p);
                if (lps && lps->has_state) {
                    char val_buf[256];
                    if (nmo_behavior_param_value_to_string(lps, c.registry, c.workspace,
                                                  val_buf, sizeof(val_buf)) == NMO_OK
                        && val_buf[0] != '\0') {
                        fprintf(c.out, " = %s", val_buf);
                    }
                }
            }
            if (bs->interface_data && bs->interface_data->script.body.has_params) {
                const nmo_interface_param_set_t *ips = &bs->interface_data->script.body.params;
                if (i < ips->local_count) {
                    const nmo_interface_param_t *ip = &ips->locals[i];
                    fprintf(c.out, "  grid=(%d,%d)", ip->h_pos, ip->v_pos);
                    if (ip->style & NMO_INTERFACE_PARAM_STYLE_COLLAPSED)
                        fprintf(c.out, " [collapsed]");
                    else if (ip->style & NMO_INTERFACE_PARAM_STYLE_NAMEVALUE)
                        fprintf(c.out, " [name+value]");
                    else if (ip->style & NMO_INTERFACE_PARAM_STYLE_VALUE)
                        fprintf(c.out, " [value]");
                    else if (ip->style & NMO_INTERFACE_PARAM_STYLE_NAME)
                        fprintf(c.out, " [name]");
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Operations */
    if (bs->operations.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Operations", c.colorize);
        const nmo_object_id_t *op_ids = (const nmo_object_id_t *)bs->operations.data;
        for (size_t i = 0; i < bs->operations.count; i++) {
            nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, op_ids[i]);
            if (!op_obj || !op_obj->state) {
                fprintf(c.out, "  pOp %zu: #%u (missing)\n", i, op_ids[i]);
                continue;
            }
            const nmo_parameteroperation_state_t *op_state =
                (const nmo_parameteroperation_state_t *)op_obj->state;
            const char *op_name = behavior_show_operation_name(
                c.registry, op_state->operation_guid);
            /* Resolve in1, in2, out names and types */
            const char *n1 = op_state->has_in1 ? resolve_name(repo, op_state->in1_id) : NULL;
            const char *n2 = op_state->has_in2 ? resolve_name(repo, op_state->in2_id) : NULL;
            const char *no = op_state->has_out ? resolve_name(repo, op_state->out_id) : NULL;
            /* Resolve result type from out parameter */
            const char *out_type = "?";
            if (op_state->has_out) {
                nmo_object_t *out_p = nmo_object_repository_find_by_id(repo, op_state->out_id);
                nmo_guid_t otg = get_param_type_guid(out_p);
                out_type = resolve_type(c.registry, otg);
            }
            char op_buf[512];
            behavior_show_format_operation(
                op_buf, sizeof(op_buf), op_name,
                n1, op_state->has_in1, n2, op_state->has_in2,
                no, op_state->has_out, out_type);
            fprintf(c.out, "  %s\n", op_buf);
        }
    }

    /* Sub-behaviors */
    if (bs->sub_behaviors.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Sub-Behaviors", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->sub_behaviors.data;

        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *sname = sub ? nmo_object_get_name(sub) : NULL;
            /* Resolve BB prototype name */
            const char *proto_name = NULL;
            if (sub && sub->state) {
                const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
                if ((sub_bs->flags & CKBEHAVIOR_BUILDINGBLOCK) && !nmo_guid_is_null(sub_bs->block_guid)) {
                    proto_name = nmo_behavior_registry_get_name(nmo_context_get_bb_registry(c.ctx),sub_bs->block_guid);
                }
            }
            if (proto_name) {
                fprintf(c.out, "  [%zu] #%u %s", i, ids[i], proto_name);
                if (sname && sname[0] && strcmp(sname, proto_name) != 0) {
                    fprintf(c.out, " (%s)", sname);
                }
            } else {
                fprintf(c.out, "  [%zu] #%u %s", i, ids[i],
                        (sname && sname[0]) ? sname : "(unnamed)");
            }
            {
                const nmo_interface_behavior_t *isub = find_interface_sub(bs->interface_data, ids[i]);
                if (isub) {
                    if (isub->flags & NMO_INTERFACE_FLAG_FOLDED)
                        fprintf(c.out, " [folded]");
                    if (isub->flags & NMO_INTERFACE_FLAG_HEADER_ONLY)
                        fprintf(c.out, " [header-only]");
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Behavior Links */
    if (bs->sub_behavior_links.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Execution Flow", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->sub_behavior_links.data;
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, ids[i]);
            if (!link_obj || !link_obj->state) continue;
            const nmo_behaviorlink_state_t *ls =
                (const nmo_behaviorlink_state_t *)link_obj->state;
            /* in_io_id = source (SDK naming is backwards), out_io_id = target */
            const nmo_behavior_index_t *bidx = nmo_session_get_behavior_index(c.session);
            nmo_object_id_t src_owner = 0, tgt_owner = 0;
            if (bidx) {
                const nmo_port_owner_t *sp = nmo_behavior_index_find(bidx, ls->in_io_id);
                const nmo_port_owner_t *tp = nmo_behavior_index_find(bidx, ls->out_io_id);
                if (sp) src_owner = sp->owner_id;
                if (tp) tgt_owner = tp->owner_id;
            }
            const char *so = (src_owner == 0 || src_owner == target_id) ? name : resolve_name(repo, src_owner);
            const char *to = (tgt_owner == 0 || tgt_owner == target_id) ? name : resolve_name(repo, tgt_owner);
            fprintf(c.out, "  %s.%s -> %s.%s",
                    (so && so[0]) ? so : "?", resolve_name(repo, ls->in_io_id),
                    (to && to[0]) ? to : "?", resolve_name(repo, ls->out_io_id));
            if (ls->activation_delay != 0) {
                fprintf(c.out, "  (delay: %d)", ls->activation_delay);
            }
            fprintf(c.out, "\n");
        }
    }

    /* Data Flow: trace pIn.source_id connections between sub-behaviors. */
    if (bs->sub_behaviors.count > 0) {
        /* Collect all sub-behavior pOut/pLocal -> owner name mapping */
        typedef struct { nmo_object_id_t param_id; const char *owner; const char *param_name; } flow_src_t;
        flow_src_t sources[512];
        size_t src_count = 0;

        /* Add parent's local parameters as sources */
        if (bs->local_parameters.data) {
            const nmo_object_id_t *lids = (const nmo_object_id_t *)bs->local_parameters.data;
            for (size_t i = 0; i < bs->local_parameters.count && src_count < 512; i++) {
                nmo_object_t *p = nmo_object_repository_find_by_id(repo, lids[i]);
                sources[src_count].param_id = lids[i];
                sources[src_count].owner = (name && name[0]) ? name : "(root)";
                sources[src_count].param_name = p ? nmo_object_get_name(p) : "?";
                src_count++;
            }
        }

        /* Add each sub-behavior's pOut as sources */
        const nmo_object_id_t *sub_ids = (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
            const char *sname = nmo_object_get_name(sub);
            if (!sname || !sname[0]) sname = "(unnamed)";

            if (sub_bs->out_parameters.data) {
                const nmo_object_id_t *pids = (const nmo_object_id_t *)sub_bs->out_parameters.data;
                for (size_t i = 0; i < sub_bs->out_parameters.count && src_count < 512; i++) {
                    nmo_object_t *p = nmo_object_repository_find_by_id(repo, pids[i]);
                    sources[src_count].param_id = pids[i];
                    sources[src_count].owner = sname;
                    sources[src_count].param_name = p ? nmo_object_get_name(p) : "?";
                    src_count++;
                }
            }
        }

        /* Now trace each sub-behavior's pIn.source_id to find data flow edges */
        size_t flow_count = 0;
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Data Flow", c.colorize);

        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
            const char *sname = nmo_object_get_name(sub);
            if (!sname || !sname[0]) sname = "(unnamed)";

            if (!sub_bs->in_parameters.data) continue;
            const nmo_object_id_t *pids = (const nmo_object_id_t *)sub_bs->in_parameters.data;
            for (size_t pi = 0; pi < sub_bs->in_parameters.count; pi++) {
                nmo_object_t *pin = nmo_object_repository_find_by_id(repo, pids[pi]);
                if (!pin || !pin->state) continue;
                const nmo_parameterin_state_t *ps = (const nmo_parameterin_state_t *)pin->state;
                if (ps->source_id == 0) continue;

                /* Find source in our map */
                const char *src_owner = NULL;
                const char *src_name = NULL;
                for (size_t s = 0; s < src_count; s++) {
                    if (sources[s].param_id == ps->source_id) {
                        src_owner = sources[s].owner;
                        src_name = sources[s].param_name;
                        break;
                    }
                }

                if (!src_owner) {
                    /* Source not in local scope -- might be external */
                    src_owner = "(external)";
                    nmo_object_t *src_obj = nmo_object_repository_find_by_id(repo, ps->source_id);
                    src_name = src_obj ? nmo_object_get_name(src_obj) : "?";
                }

                const char *pin_name = nmo_object_get_name(pin);
                nmo_guid_t tg = get_param_type_guid(pin);
                const char *tname = resolve_type(c.registry, tg);

                fprintf(c.out, "  %s.%s -> %s.%s  [%s]%s\n",
                        src_owner,
                        (src_name && src_name[0]) ? src_name : "?",
                        sname,
                        (pin_name && pin_name[0]) ? pin_name : "?",
                        tname,
                        ps->is_shared ? " (shared)" : "");
                flow_count++;
            }
        }

        if (flow_count == 0) {
            fprintf(c.out, "  (no parameter connections)\n");
        }
    }

    /* Interface: Comments (script body only) */
    if (bs->interface_data && bs->interface_data->script.body.comment_count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Comments", c.colorize);
        const nmo_interface_body_t *body = &bs->interface_data->script.body;
        for (size_t ci = 0; ci < body->comment_count; ci++) {
            const nmo_interface_comment_t *cm = &body->comments[ci];
            fprintf(c.out, "  [%zu] ", ci);
            if (cm->text && cm->text[0]) {
                size_t tlen = strlen(cm->text);
                if (tlen > 60)
                    fprintf(c.out, "\"%.57s...\"", cm->text);
                else
                    fprintf(c.out, "\"%s\"", cm->text);
            } else {
                fprintf(c.out, "(empty)");
            }
            fprintf(c.out, "  rect=(%.0f,%.0f,%.0f,%.0f)", cm->left, cm->top, cm->right, cm->bottom);
            if (cm->style_flags)
                fprintf(c.out, "  flags=0x%X", cm->style_flags);
            fprintf(c.out, "\n");
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
