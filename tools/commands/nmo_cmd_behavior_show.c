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
#include "behavior/nmo_behavior_index.h"
#include "session/nmo_context.h"
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
#include "behavior/nmo_bb_registry.h"
#include "behavior/nmo_param_value.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--raw",  NULL, NMO_OPT_FLAG, "Show raw reflection (like object show)"},
        {"--json", "-j", NMO_OPT_FLAG, "JSON output"},
    };
    enum { OPT_RAW, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool raw_mode = vals[OPT_RAW].present && vals[OPT_RAW].val.flag;
    if (raw_mode) {
        return nmo_cmd_object_show(argc, argv, global);
    }

    const char *id_str = r.pos_count > 0 ? r.pos_args[0] : NULL;
    if (!id_str) {
        fprintf(stderr, "Usage: nmo behavior show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t target_id;
    if (!nmo_tool_parse_u32(id_str, &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, target_id);
    if (!beh) {
        fprintf(stderr, "Error: Object %u not found\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (!is_behavior_class(c.registry, nmo_object_get_class_id(beh))) {
        fprintf(stderr, "Error: Object %u is not a CKBehavior\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
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

        if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
            char guid_buf[24];
            snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                     bs->block_guid.d1, bs->block_guid.d2);
            nmo_cli_json_add_str_safe(doc, data, "bb_guid", guid_buf);
            yyjson_mut_obj_add_uint(doc, data, "bb_version",
                                    bs->block_version);
            const char *proto_name = nmo_bb_registry_get_name(
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
                    const char *op_name = nmo_type_registry_guid_to_name(
                        c.registry, op_state->operation_guid);
                    if (op_name) {
                        nmo_cli_json_add_str_safe(doc, item, "operation",
                                                  op_name);
                    }
                    if (op_state->has_in1) {
                        yyjson_mut_obj_add_uint(doc, item, "in1_id",
                                                op_state->in1_id);
                    }
                    if (op_state->has_in2) {
                        yyjson_mut_obj_add_uint(doc, item, "in2_id",
                                                op_state->in2_id);
                    }
                    if (op_state->has_out) {
                        yyjson_mut_obj_add_uint(doc, item, "out_id",
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
                        const char *proto = nmo_bb_registry_get_name(
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

        if (bs->interface_data) {
            const nmo_interface_body_t *ibody = &bs->interface_data->script.body;
            yyjson_mut_val *comments_arr = yyjson_mut_arr(doc);
            for (size_t ci = 0; ci < ibody->comment_count; ci++) {
                const nmo_interface_comment_t *cm = &ibody->comments[ci];
                yyjson_mut_val *cobj = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, cobj, "index", ci);
                if (cm->text) yyjson_mut_obj_add_str(doc, cobj, "text", cm->text);
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
    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {

        const char *proto_name = nmo_bb_registry_get_name(nmo_context_get_bb_registry(c.ctx),bs->block_guid);
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
                    if (nmo_param_value_to_string(ps, c.registry, c.session,
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
                    if (nmo_param_value_to_string(lps, c.registry, c.session,
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
            const char *op_name = nmo_type_registry_guid_to_name(
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
            /* Display as: [OP] in1 + in2 -> out [Type] */
            fprintf(c.out, "  [%s] ", op_name ? op_name : "?");
            if (n1) fprintf(c.out, "%s", n1);
            if (n1 && n2) fprintf(c.out, " + ");
            if (n2) fprintf(c.out, "%s", n2);
            if (no) fprintf(c.out, " -> %s", no);
            fprintf(c.out, "  [%s]\n", out_type);
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
                    proto_name = nmo_bb_registry_get_name(nmo_context_get_bb_registry(c.ctx),sub_bs->block_guid);
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
