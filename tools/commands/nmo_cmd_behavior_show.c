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
#include "runtime/nmo_context.h"
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

/* Everything the behavior show sections read. */
typedef struct behavior_show {
    nmo_cmd_ctx_t *c;
    nmo_object_repository_t *repo;
    nmo_object_t *beh;
    const nmo_behavior_state_t *bs;
    nmo_object_id_t target_id;
    const char *name;
} behavior_show_t;

/* Append `item` to `arr`, freeing it when building it failed. */
static bool behavior_show_add_item(nmo_cli_record_array_t *arr,
                                   nmo_cli_record_t *item,
                                   bool ok)
{
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(arr, item);
}

/* A JSON array that text shows as its item summaries only. */
static nmo_cli_record_array_t *behavior_show_array(nmo_cli_record_t *rec, const char *key)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    if (arr) {
        nmo_cli_record_array_omit_heading(arr);
    }
    return arr;
}

/* A text section heading, shown only when the section has entries. */
static bool behavior_show_heading(nmo_cli_record_t *rec, bool show, const char *title)
{
    return !show || nmo_cli_record_heading(rec, title);
}

static const char *behavior_show_kind(const nmo_behavior_state_t *bs)
{
    if (bs->flags & CKBEHAVIOR_SCRIPT) {
        return "Script";
    }
    return (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) ? "BB" : "Graph";
}

/* Text name of a parameter: "?" when missing, "(unnamed)" when unnamed. */
static const char *behavior_show_param_text_name(const nmo_object_t *p)
{
    if (!p) {
        return "?";
    }
    const char *pname = nmo_object_get_name(p);
    return (pname && pname[0]) ? pname : "(unnamed)";
}

/* JSON: index, id, name, and type of a parameter item. */
static bool behavior_show_param_fields(nmo_cli_record_t *item,
                                       size_t index,
                                       nmo_object_id_t id,
                                       const nmo_object_t *p,
                                       const char *type_name)
{
    const char *pname = p ? nmo_object_get_name(p) : NULL;
    return nmo_cli_record_uint(item, "index", NULL, (uint64_t)index) &&
           nmo_cli_record_uint(item, "id", NULL, id) &&
           nmo_cli_record_str(item, "name", NULL, (pname && pname[0]) ? pname : "") &&
           nmo_cli_record_str(item, "type", NULL, type_name);
}

/*
 * Decoded value of an output or local parameter whose class is `own_class` or
 * CKParameter, or NULL when it has none. JSON: "decoded_value" when non-empty.
 */
static char *behavior_show_decoded_value(const behavior_show_t *s,
                                         nmo_object_t *p,
                                         nmo_class_id_t own_class)
{
    if (!p) {
        return NULL;
    }
    nmo_class_id_t cid = nmo_object_get_class_id(p);
    if (cid != own_class && cid != NMO_CID_PARAMETER) {
        return NULL;
    }
    const nmo_parameter_state_t *ps = (const nmo_parameter_state_t *)nmo_object_get_state(p);
    if (!ps || !ps->has_state) {
        return NULL;
    }
    return nmo_core_param_value_dup(ps, s->c->registry, s->c->workspace);
}

/* JSON: "source_chain", the steps a ParameterIn's value is traced through. */
static bool behavior_show_add_source_chain(nmo_cli_record_t *item,
                                           const behavior_show_t *s,
                                           nmo_object_id_t param_id)
{
    if (!s->c->workspace || !s->repo || param_id == 0) {
        return true;
    }

    nmo_array_t chain;
    if (nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 8, NULL) != NMO_OK) {
        return true;
    }

    bool ok = true;
    if (nmo_behavior_analyze_trace_param_chain(s->c->workspace, param_id,
                                               &chain, 32) == NMO_OK &&
        chain.count > 0) {
        nmo_cli_record_array_t *arr = behavior_show_array(item, "source_chain");
        ok = arr != NULL;
        const nmo_behavior_trace_step_t *steps =
            (const nmo_behavior_trace_step_t *)chain.data;
        for (size_t i = 0; ok && i < chain.count; i++) {
            nmo_object_t *obj = nmo_object_repository_find_by_id(s->repo, steps[i].id);
            nmo_guid_t type_guid = get_param_type_guid(obj);
            bool is_shared = false;
            if (obj && nmo_object_get_class_id(obj) == NMO_CID_PARAMETERIN) {
                const nmo_parameterin_state_t *pin =
                    (const nmo_parameterin_state_t *)nmo_object_get_state(obj);
                is_shared = pin && pin->is_shared;
            }

            nmo_cli_record_t *step = nmo_cli_record_new();
            bool step_ok = step != NULL &&
                nmo_cli_record_uint(step, "id", NULL, steps[i].id) &&
                nmo_cli_record_str(step, "name", NULL, resolve_name(s->repo, steps[i].id)) &&
                nmo_cli_record_str_fmt(step, "type_guid", NULL, "%08X-%08X",
                                       type_guid.d1, type_guid.d2) &&
                nmo_cli_record_str(step, "type_name", NULL,
                                   resolve_type(s->c->registry, type_guid)) &&
                nmo_cli_record_bool(step, "is_shared", NULL, is_shared) &&
                nmo_cli_record_uint(step, "owner_id", NULL, steps[i].owner_id) &&
                nmo_cli_record_str(step, "owner_name", NULL,
                                   resolve_name(s->repo, steps[i].owner_id));
            ok = behavior_show_add_item(arr, step, step_ok);
        }
    }

    nmo_array_dispose(&chain);
    return ok;
}

/*
 * Text: where a ParameterIn reads from, "  <- <source>", or, through shared
 * links, "  <- <direct source> [<type>] via N shared link(s)". Heap string,
 * empty when the parameter has no source; NULL on OOM.
 */
static char *behavior_show_pin_source_text(const behavior_show_t *s, const nmo_object_t *p)
{
    if (!p || nmo_object_get_class_id(p) != NMO_CID_PARAMETERIN) {
        return nmo_tool_strdup_fmt("%s", "");
    }
    const nmo_parameterin_state_t *pin =
        (const nmo_parameterin_state_t *)nmo_object_get_state(p);
    const nmo_object_id_t source_id = nmo_parameterin_source_id(pin);
    if (source_id == 0) {
        return nmo_tool_strdup_fmt("%s", "");
    }
    if (!pin->is_shared) {
        const char *src = resolve_name(s->repo, source_id);
        return nmo_tool_strdup_fmt("  <- %s", src ? src : "?");
    }

    /* Trace the shared chain to the direct source */
    uint32_t shared_hops = 0;
    nmo_object_id_t cur_id = source_id;
    while (shared_hops < 32) {
        nmo_object_t *cur_obj = nmo_object_repository_find_by_id(s->repo, cur_id);
        if (!cur_obj) break;
        if (nmo_object_get_class_id(cur_obj) != NMO_CID_PARAMETERIN) break;
        const nmo_parameterin_state_t *cur_pin =
            (const nmo_parameterin_state_t *)nmo_object_get_state(cur_obj);
        const nmo_object_id_t next_id = nmo_parameterin_source_id(cur_pin);
        if (next_id == 0) break;
        if (!cur_pin->is_shared) {
            /* Reached direct source */
            cur_id = next_id;
            shared_hops++;
            break;
        }
        cur_id = next_id;
        shared_hops++;
    }
    const char *final_name = resolve_name(s->repo, cur_id);
    nmo_object_t *final_obj = nmo_object_repository_find_by_id(s->repo, cur_id);
    const char *final_type = resolve_type(s->c->registry, get_param_type_guid(final_obj));
    return nmo_tool_strdup_fmt("  <- %s [%s] via %u shared link%s",
                               final_name ? final_name : "?", final_type,
                               shared_hops, shared_hops == 1 ? "" : "s");
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

/* Text: one "  [op] in1 op in2 -> out  [type]" line. */
static bool behavior_show_operation_summary(
    nmo_cli_record_t *item,
    const char *op_name,
    const char *in1_name,
    bool has_in1,
    const char *in2_name,
    bool has_in2,
    const char *out_name,
    bool has_out,
    const char *out_type)
{
    const char *op_token = behavior_show_operation_token(op_name);
    const char *in1 = (in1_name && in1_name[0]) ? in1_name : "[missing in1]";
    const char *in2 = (in2_name && in2_name[0]) ? in2_name : "[missing in2]";
    const char *out = (out_name && out_name[0]) ? out_name : "[missing out]";
    const char *otype = (out_type && out_type[0]) ? out_type : "?";

    if (behavior_show_operation_is_copy_like(op_name)) {
        return nmo_cli_record_set_summary_fmt(
            item, "  [%s] %s -> %s [COPY]  [%s]",
            op_token, has_in1 ? in1 : "[missing in1]",
            has_out ? out : "[missing out]", otype);
    }
    if (has_in1 && !has_in2) {
        return nmo_cli_record_set_summary_fmt(
            item, "  [%s] %s %s -> %s  [%s]",
            op_token, op_token, in1,
            has_out ? out : "[missing out]", otype);
    }
    return nmo_cli_record_set_summary_fmt(
        item, "  [%s] %s %s %s -> %s  [%s]",
        op_token,
        has_in1 ? in1 : "[missing in1]",
        op_token,
        has_in2 ? in2 : "[missing in2]",
        has_out ? out : "[missing out]",
        otype);
}

/* JSON keys for one ParameterOperation operand. */
typedef struct behavior_show_operand_keys {
    const char *id;
    const char *name;
    const char *type_guid;
    const char *type_name;
} behavior_show_operand_keys_t;

static const behavior_show_operand_keys_t behavior_show_in1_keys = {
    "in1_id", "in1_name", "in1_type_guid", "in1_type_name"};
static const behavior_show_operand_keys_t behavior_show_in2_keys = {
    "in2_id", "in2_name", "in2_type_guid", "in2_type_name"};
static const behavior_show_operand_keys_t behavior_show_out_keys = {
    "out_id", "out_name", "out_type_guid", "out_type_name"};

/* JSON: id, name, and type of one operand. */
static bool behavior_show_add_operation_param(
    nmo_cli_record_t *item,
    const behavior_show_t *s,
    const behavior_show_operand_keys_t *keys,
    nmo_object_id_t param_id)
{
    if (!s->repo || param_id == 0) {
        return true;
    }

    nmo_object_t *param_obj = nmo_object_repository_find_by_id(s->repo, param_id);
    nmo_guid_t type_guid = get_param_type_guid(param_obj);
    return nmo_cli_record_uint(item, keys->id, NULL, param_id) &&
           nmo_cli_record_str(item, keys->name, NULL, resolve_name(s->repo, param_id)) &&
           nmo_cli_record_str_fmt(item, keys->type_guid, NULL, "%08X-%08X",
                                  type_guid.d1, type_guid.d2) &&
           nmo_cli_record_str(item, keys->type_name, NULL,
                              resolve_type(s->c->registry, type_guid));
}

/* Identity, flags, and prototype. */
static bool behavior_show_add_header(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    nmo_cmd_ctx_t *c = s->c;
    const char *name = s->name;
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
    nmo_class_id_t class_id = nmo_object_get_class_id(s->beh);
    const char *cls_name = nmo_cli_class_name_from_id(c->ctx, class_id);

    bool ok = nmo_cli_record_title_fmt(rec, "Behavior #%u: %s", s->target_id,
                                       (name && name[0]) ? name : "(unnamed)") &&
              nmo_cli_record_uint(rec, "id", NULL, s->target_id) &&
              nmo_cli_record_str(rec, "name", NULL, (name && name[0]) ? name : "") &&
              nmo_cli_record_uint(rec, "class_id", NULL, class_id);
    if (cls_name) {
        ok = ok && nmo_cli_record_str(rec, "class_name", NULL, cls_name);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "  Type: %s\n",
                                      is_script ? "Script" : is_bb ? "Building Block" : "Graph") &&
         nmo_cli_record_str(rec, "behavior_type", NULL, behavior_show_kind(bs)) &&
         nmo_cli_record_uint(rec, "flags", NULL, bs->flags) &&
         nmo_cli_record_int(rec, "priority", NULL, bs->priority) &&
         nmo_cli_record_int(rec, "compatible_class_id", NULL, bs->compatible_class_id);
    if (bs->interface_data && (bs->interface_data->script.flags & NMO_INTERFACE_FLAG_FOLDED)) {
        ok = ok && nmo_cli_record_raw(rec, "  Layout: Folded\n");
    }
    ok = ok && nmo_cmd_behavior_add_interface_diagnostics(rec, c->workspace,
                                                          !bs->interface_data);

    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
        const char *proto_name = nmo_behavior_registry_get_name(
            nmo_context_get_bb_registry(c->ctx), bs->block_guid);
        ok = ok && nmo_cli_record_str_fmt(rec, "bb_guid", NULL, "%08X-%08X",
                                          bs->block_guid.d1, bs->block_guid.d2) &&
             nmo_cli_record_uint(rec, "bb_version", NULL, bs->block_version);
        if (proto_name) {
            ok = ok && nmo_cli_record_str(rec, "bb_proto_name", NULL, proto_name) &&
                 nmo_cli_record_raw_fmt(rec, "  Prototype: %s  {%08X-%08X}  v%u\n",
                                        proto_name, bs->block_guid.d1, bs->block_guid.d2,
                                        bs->block_version);
        } else {
            ok = ok && nmo_cli_record_raw_fmt(rec, "  GUID: {%08X-%08X}  Version: %u\n",
                                              bs->block_guid.d1, bs->block_guid.d2,
                                              bs->block_version);
        }
    }
    if (bs->compatible_class_id > 0) {
        const char *cls = nmo_core_class_name(c, (nmo_class_id_t)bs->compatible_class_id);
        ok = ok && nmo_cli_record_raw_fmt(rec, "  Target Class: %s (#%d)\n",
                                          cls ? cls : "?", bs->compatible_class_id);
    }

    const nmo_object_id_t target_parameter_id = nmo_behavior_target_parameter_id(bs);
    if (target_parameter_id != 0) {
        ok = ok && nmo_cli_record_uint(rec, "target_parameter_id", NULL, target_parameter_id);
    }
    return ok;
}

/* "IO Ports": the behavior inputs ("bIn") then outputs ("bOut"). */
static bool behavior_show_add_io_ports(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    bool ok = behavior_show_heading(rec, bs->inputs.count > 0 || bs->outputs.count > 0,
                                    "IO Ports");
    const nmo_array_t *ports[2] = {&bs->inputs, &bs->outputs};
    static const char *const keys[2] = {"inputs", "outputs"};
    static const char *const tags[2] = {"bIn ", "bOut"};

    for (size_t k = 0; ok && k < 2; k++) {
        nmo_cli_record_array_t *arr = behavior_show_array(rec, keys[k]);
        ok = arr != NULL;
        for (size_t i = 0; ok && i < ports[k]->count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(ports[k], i);
            const char *port_name = resolve_name(s->repo, id);
            nmo_cli_record_t *item = nmo_cli_record_new();
            bool item_ok = item != NULL &&
                nmo_cli_record_uint(item, "index", NULL, (uint64_t)i) &&
                nmo_cli_record_uint(item, "id", NULL, id) &&
                nmo_cli_record_str(item, "name", NULL, port_name) &&
                nmo_cli_record_set_summary_fmt(item, "  %s %zu: %s", tags[k], i, port_name);
            ok = behavior_show_add_item(arr, item, item_ok);
        }
    }
    return ok;
}

/* "Input Parameters": each pIn with its type and source. */
static bool behavior_show_add_input_params(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    bool ok = behavior_show_heading(rec, bs->in_parameters.count > 0, "Input Parameters");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "input_parameters") : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < bs->in_parameters.count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->in_parameters, i);
        nmo_object_t *p = nmo_object_repository_find_by_id(s->repo, id);
        const char *tname = resolve_type(s->c->registry, get_param_type_guid(p));

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL && behavior_show_param_fields(item, i, id, p, tname);
        if (p && nmo_object_get_class_id(p) == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *pin =
                (const nmo_parameterin_state_t *)nmo_object_get_state(p);
            const nmo_object_id_t source_id = nmo_parameterin_source_id(pin);
            if (source_id != 0) {
                item_ok = item_ok && nmo_cli_record_uint(item, "source_id", NULL, source_id);
                if (pin->is_shared) {
                    item_ok = item_ok && nmo_cli_record_bool(item, "is_shared", NULL, true);
                }
            }
        }
        item_ok = item_ok && behavior_show_add_source_chain(item, s, id);

        char *source_text = item_ok ? behavior_show_pin_source_text(s, p) : NULL;
        item_ok = source_text != NULL &&
                  nmo_cli_record_set_summary_fmt(item, "  pIn  %zu: %-24s  [%s]%s", i,
                                                 behavior_show_param_text_name(p), tname,
                                                 source_text);
        free(source_text);
        ok = behavior_show_add_item(arr, item, item_ok);
    }
    return ok;
}

/* "Output Parameters": each pOut with its type and decoded value. */
static bool behavior_show_add_output_params(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    bool ok = behavior_show_heading(rec, bs->out_parameters.count > 0, "Output Parameters");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "output_parameters") : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < bs->out_parameters.count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->out_parameters, i);
        nmo_object_t *p = nmo_object_repository_find_by_id(s->repo, id);
        const char *tname = resolve_type(s->c->registry, get_param_type_guid(p));
        char *value = behavior_show_decoded_value(s, p, NMO_CID_PARAMETEROUT);
        bool has_value = value && value[0];

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL && behavior_show_param_fields(item, i, id, p, tname) &&
            nmo_cli_record_str_opt(item, "decoded_value", NULL, value, NULL) &&
            nmo_cli_record_set_summary_fmt(item, "  pOut %zu: %-24s  [%s]%s%s", i,
                                           behavior_show_param_text_name(p), tname,
                                           has_value ? " = " : "", has_value ? value : "");
        free(value);
        ok = behavior_show_add_item(arr, item, item_ok);
    }
    return ok;
}

/* "Local Parameters": each local with its type, value, and schematic slot. */
static bool behavior_show_add_local_params(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    const nmo_interface_param_set_t *ips =
        (bs->interface_data && bs->interface_data->script.body.has_params)
            ? &bs->interface_data->script.body.params
            : NULL;
    bool ok = behavior_show_heading(rec, bs->local_parameters.count > 0, "Local Parameters");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "local_parameters") : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < bs->local_parameters.count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->local_parameters, i);
        nmo_object_t *p = nmo_object_repository_find_by_id(s->repo, id);
        const char *tname = resolve_type(s->c->registry, get_param_type_guid(p));
        char *value = behavior_show_decoded_value(s, p, NMO_CID_PARAMETERLOCAL);
        bool has_value = value && value[0];
        const char *pname = behavior_show_param_text_name(p);

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL && behavior_show_param_fields(item, i, id, p, tname) &&
            nmo_cli_record_str_opt(item, "decoded_value", NULL, value, NULL);
        if (ips && i < ips->local_count) {
            const nmo_interface_param_t *ip = &ips->locals[i];
            const char *style = "";
            if (ip->style & NMO_INTERFACE_PARAM_STYLE_COLLAPSED)
                style = " [collapsed]";
            else if (ip->style & NMO_INTERFACE_PARAM_STYLE_NAMEVALUE)
                style = " [name+value]";
            else if (ip->style & NMO_INTERFACE_PARAM_STYLE_VALUE)
                style = " [value]";
            else if (ip->style & NMO_INTERFACE_PARAM_STYLE_NAME)
                style = " [name]";
            item_ok = item_ok && nmo_cli_record_set_summary_fmt(
                item, "  local %zu: %-24s  [%s]%s%s  grid=(%d,%d)%s", i, pname, tname,
                has_value ? " = " : "", has_value ? value : "",
                ip->h_pos, ip->v_pos, style);
        } else {
            item_ok = item_ok && nmo_cli_record_set_summary_fmt(
                item, "  local %zu: %-24s  [%s]%s%s", i, pname, tname,
                has_value ? " = " : "", has_value ? value : "");
        }
        free(value);
        ok = behavior_show_add_item(arr, item, item_ok);
    }
    return ok;
}

/* "Operations": each pOp as "in1 op in2 -> out". */
static bool behavior_show_add_operations(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    const nmo_type_registry_t *registry = s->c->registry;
    bool ok = behavior_show_heading(rec, bs->operations.count > 0, "Operations");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "operations") : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < bs->operations.count; i++) {
        nmo_object_id_t operation_id = nmo_behavior_ref_array_get_id(&bs->operations, i);
        nmo_object_t *op_obj = nmo_object_repository_find_by_id(s->repo, operation_id);

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL &&
            nmo_cli_record_uint(item, "index", NULL, (uint64_t)i) &&
            nmo_cli_record_uint(item, "id", NULL, operation_id);
        if (!op_obj || !op_obj->state) {
            item_ok = item_ok && nmo_cli_record_set_summary_fmt(
                item, "  pOp %zu: #%u (missing)", i, operation_id);
            ok = behavior_show_add_item(arr, item, item_ok);
            continue;
        }

        const nmo_parameteroperation_state_t *op_state =
            (const nmo_parameteroperation_state_t *)op_obj->state;
        const char *op_name = behavior_show_operation_name(registry, op_state->operation_guid);
        const nmo_object_id_t in1_id = nmo_parameteroperation_in1_id(op_state);
        const nmo_object_id_t in2_id = nmo_parameteroperation_in2_id(op_state);
        const nmo_object_id_t out_id = nmo_parameteroperation_out_id(op_state);
        item_ok = item_ok &&
            nmo_cli_record_str_fmt(item, "operation_guid", NULL, "%08X-%08X",
                                   op_state->operation_guid.d1,
                                   op_state->operation_guid.d2) &&
            nmo_cli_record_str(item, "operation", NULL, op_name) &&
            nmo_cli_record_str(item, "operation_name", NULL, op_name);
        if (op_state->has_in1) {
            item_ok = item_ok &&
                behavior_show_add_operation_param(item, s, &behavior_show_in1_keys, in1_id);
        }
        if (op_state->has_in2) {
            item_ok = item_ok &&
                behavior_show_add_operation_param(item, s, &behavior_show_in2_keys, in2_id);
        }
        if (op_state->has_out) {
            item_ok = item_ok &&
                behavior_show_add_operation_param(item, s, &behavior_show_out_keys, out_id);
        }

        /* Resolve result type from out parameter */
        const char *out_type = "?";
        if (op_state->has_out) {
            nmo_object_t *out_p = nmo_object_repository_find_by_id(s->repo, out_id);
            out_type = resolve_type(registry, get_param_type_guid(out_p));
        }
        item_ok = item_ok && behavior_show_operation_summary(
            item, op_name,
            op_state->has_in1 ? resolve_name(s->repo, in1_id) : NULL, op_state->has_in1,
            op_state->has_in2 ? resolve_name(s->repo, in2_id) : NULL, op_state->has_in2,
            op_state->has_out ? resolve_name(s->repo, out_id) : NULL, op_state->has_out,
            out_type);
        ok = behavior_show_add_item(arr, item, item_ok);
    }
    return ok;
}

/* "Sub-Behaviors": each child with its prototype and schematic flags. */
static bool behavior_show_add_sub_behaviors(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    bool ok = behavior_show_heading(rec, bs->sub_behaviors.count > 0, "Sub-Behaviors");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "sub_behaviors") : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < bs->sub_behaviors.count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->sub_behaviors, i);
        nmo_object_t *sub = nmo_object_repository_find_by_id(s->repo, id);
        const char *sname = sub ? nmo_object_get_name(sub) : NULL;
        bool named = sname && sname[0];

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL &&
            nmo_cli_record_uint(item, "index", NULL, (uint64_t)i) &&
            nmo_cli_record_uint(item, "id", NULL, id) &&
            nmo_cli_record_str(item, "name", NULL, named ? sname : "");

        /* Resolve BB prototype name */
        const char *proto_name = NULL;
        if (sub && sub->state) {
            const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
            item_ok = item_ok &&
                nmo_cli_record_str(item, "type", NULL, behavior_show_kind(sub_bs));
            if ((sub_bs->flags & CKBEHAVIOR_BUILDINGBLOCK) &&
                !nmo_guid_is_null(sub_bs->block_guid)) {
                proto_name = nmo_behavior_registry_get_name(
                    nmo_context_get_bb_registry(s->c->ctx), sub_bs->block_guid);
            }
            if (proto_name) {
                item_ok = item_ok && nmo_cli_record_str(item, "proto_name", NULL, proto_name);
            }
        }

        const char *label = proto_name ? proto_name : named ? sname : "(unnamed)";
        bool alias = proto_name && named && strcmp(sname, proto_name) != 0;
        const nmo_interface_behavior_t *isub = find_interface_sub(bs->interface_data, id);
        bool folded = isub && (isub->flags & NMO_INTERFACE_FLAG_FOLDED);
        bool header_only = isub && (isub->flags & NMO_INTERFACE_FLAG_HEADER_ONLY);
        item_ok = item_ok && nmo_cli_record_set_summary_fmt(
            item, "  [%zu] #%u %s%s%s%s%s%s", i, id, label,
            alias ? " (" : "", alias ? sname : "", alias ? ")" : "",
            folded ? " [folded]" : "", header_only ? " [header-only]" : "");
        ok = behavior_show_add_item(arr, item, item_ok);
    }
    return ok;
}

/* "Execution Flow": each behavior link as "owner.port -> owner.port". */
static bool behavior_show_add_links(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    const nmo_behavior_index_t *bidx = nmo_tool_owner_behavior_index(s->c->workspace);
    bool ok = behavior_show_heading(rec, bs->sub_behavior_links.count > 0, "Execution Flow");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "behavior_links") : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < bs->sub_behavior_links.count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->sub_behavior_links, i);
        nmo_object_t *link_obj = nmo_object_repository_find_by_id(s->repo, id);
        if (!link_obj || !link_obj->state) continue;
        const nmo_behaviorlink_state_t *ls = (const nmo_behaviorlink_state_t *)link_obj->state;
        /* in_io_id = source (SDK naming is backwards), out_io_id = target */
        const nmo_object_id_t in_io_id = nmo_behaviorlink_in_io_id(ls);
        const nmo_object_id_t out_io_id = nmo_behaviorlink_out_io_id(ls);
        const nmo_port_owner_t *sp = bidx ? nmo_behavior_index_find(bidx, in_io_id) : NULL;
        const nmo_port_owner_t *tp = bidx ? nmo_behavior_index_find(bidx, out_io_id) : NULL;
        nmo_object_id_t src_owner = sp ? sp->owner_id : 0;
        nmo_object_id_t tgt_owner = tp ? tp->owner_id : 0;

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL &&
            nmo_cli_record_uint(item, "id", NULL, id) &&
            nmo_cli_record_uint(item, "in_io_id", NULL, in_io_id) &&
            nmo_cli_record_uint(item, "out_io_id", NULL, out_io_id);
        if (sp) {
            item_ok = item_ok && nmo_cli_record_uint(item, "source_owner_id", NULL, src_owner);
        }
        if (tp) {
            item_ok = item_ok && nmo_cli_record_uint(item, "target_owner_id", NULL, tgt_owner);
        }
        item_ok = item_ok &&
            nmo_cli_record_int(item, "activation_delay", NULL, ls->activation_delay);

        const char *so = (src_owner == 0 || src_owner == s->target_id)
            ? s->name : resolve_name(s->repo, src_owner);
        const char *to = (tgt_owner == 0 || tgt_owner == s->target_id)
            ? s->name : resolve_name(s->repo, tgt_owner);
        if (ls->activation_delay != 0) {
            item_ok = item_ok && nmo_cli_record_set_summary_fmt(
                item, "  %s.%s -> %s.%s  (delay: %d)",
                (so && so[0]) ? so : "?", resolve_name(s->repo, in_io_id),
                (to && to[0]) ? to : "?", resolve_name(s->repo, out_io_id),
                ls->activation_delay);
        } else {
            item_ok = item_ok && nmo_cli_record_set_summary_fmt(
                item, "  %s.%s -> %s.%s",
                (so && so[0]) ? so : "?", resolve_name(s->repo, in_io_id),
                (to && to[0]) ? to : "?", resolve_name(s->repo, out_io_id));
        }
        ok = behavior_show_add_item(arr, item, item_ok);
    }
    return ok;
}

/* A parameter that can feed a sub-behavior input: a local or a sub-behavior pOut. */
typedef struct behavior_show_flow_source {
    nmo_object_id_t param_id;
    nmo_object_id_t owner_id;
    const char *owner_name;
    const char *json_name; /* resolve_name() */
    const char *text_name; /* object name, NULL or "?" when missing */
} behavior_show_flow_source_t;

typedef struct behavior_show_flow_sources {
    behavior_show_flow_source_t *items;
    size_t count;
    size_t capacity;
} behavior_show_flow_sources_t;

static bool behavior_show_flow_source_add(behavior_show_flow_sources_t *list,
                                          const behavior_show_t *s,
                                          nmo_object_id_t param_id,
                                          nmo_object_id_t owner_id,
                                          const char *owner_name)
{
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 64u;
        behavior_show_flow_source_t *new_items = (behavior_show_flow_source_t *)realloc(
            list->items, new_capacity * sizeof(*new_items));
        if (!new_items) {
            return false;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    nmo_object_t *p = nmo_object_repository_find_by_id(s->repo, param_id);
    behavior_show_flow_source_t *src = &list->items[list->count++];
    src->param_id = param_id;
    src->owner_id = owner_id;
    src->owner_name = owner_name;
    src->json_name = resolve_name(s->repo, param_id);
    src->text_name = p ? nmo_object_get_name(p) : "?";
    return true;
}

/* The parent's locals and every sub-behavior's pOut, in lookup order. */
static bool behavior_show_collect_flow_sources(behavior_show_flow_sources_t *list,
                                               const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    const char *root_name = (s->name && s->name[0]) ? s->name : "(root)";
    bool ok = true;
    for (size_t i = 0; ok && i < bs->local_parameters.count; i++) {
        ok = behavior_show_flow_source_add(
            list, s, nmo_behavior_ref_array_get_id(&bs->local_parameters, i),
            s->target_id, root_name);
    }
    for (size_t si = 0; ok && si < bs->sub_behaviors.count; si++) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(&bs->sub_behaviors, si);
        if (sub_id == 0) continue;
        nmo_object_t *sub = nmo_object_repository_find_by_id(s->repo, sub_id);
        if (!sub || !sub->state) continue;
        const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
        const char *sub_name = nmo_object_get_name(sub);
        if (!sub_name || !sub_name[0]) sub_name = "(unnamed)";
        for (size_t pi = 0; ok && pi < sub_bs->out_parameters.count; pi++) {
            ok = behavior_show_flow_source_add(
                list, s, nmo_behavior_ref_array_get_id(&sub_bs->out_parameters, pi),
                sub_id, sub_name);
        }
    }
    return ok;
}

/*
 * "Data Flow": every sub-behavior pIn with a source, as
 * "source owner.param -> sub.pin  [type]".
 */
static bool behavior_show_add_data_flow(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    const nmo_behavior_state_t *bs = s->bs;
    bool show_text = bs->sub_behaviors.count > 0;
    behavior_show_flow_sources_t sources = {0};
    bool ok = behavior_show_collect_flow_sources(&sources, s) &&
              behavior_show_heading(rec, show_text, "Data Flow");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "data_flow") : NULL;
    ok = arr != NULL;

    for (size_t si = 0; ok && si < bs->sub_behaviors.count; si++) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(&bs->sub_behaviors, si);
        if (sub_id == 0) continue;
        nmo_object_t *sub = nmo_object_repository_find_by_id(s->repo, sub_id);
        if (!sub || !sub->state) continue;
        const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
        const char *sub_name = nmo_object_get_name(sub);
        if (!sub_name || !sub_name[0]) sub_name = "(unnamed)";

        for (size_t pi = 0; ok && pi < sub_bs->in_parameters.count; pi++) {
            nmo_object_id_t param_id =
                nmo_behavior_ref_array_get_id(&sub_bs->in_parameters, pi);
            if (param_id == 0) continue;
            nmo_object_t *pin_obj = nmo_object_repository_find_by_id(s->repo, param_id);
            if (!pin_obj || !pin_obj->state) continue;
            const nmo_parameterin_state_t *pin =
                (const nmo_parameterin_state_t *)pin_obj->state;
            const nmo_object_id_t source_id = nmo_parameterin_source_id(pin);
            if (source_id == 0) continue;

            const behavior_show_flow_source_t *src = NULL;
            for (size_t k = 0; k < sources.count; k++) {
                if (sources.items[k].param_id == source_id) {
                    src = &sources.items[k];
                    break;
                }
            }

            /* A source outside the local scope is external */
            nmo_object_t *src_obj = nmo_object_repository_find_by_id(s->repo, source_id);
            const char *src_owner_name = src ? src->owner_name : "(external)";
            const char *src_json_name = src ? src->json_name : resolve_name(s->repo, source_id);
            const char *src_text_name = src ? src->text_name
                                            : src_obj ? nmo_object_get_name(src_obj) : "?";
            const char *pin_name = nmo_object_get_name(pin_obj);
            nmo_guid_t type_guid = get_param_type_guid(pin_obj);
            const char *tname = resolve_type(s->c->registry, type_guid);

            nmo_cli_record_t *flow = nmo_cli_record_new();
            bool flow_ok = flow != NULL &&
                nmo_cli_record_uint(flow, "source_id", NULL, source_id) &&
                nmo_cli_record_str(flow, "source_name", NULL, src_json_name) &&
                nmo_cli_record_uint(flow, "source_owner_id", NULL, src ? src->owner_id : 0) &&
                nmo_cli_record_str(flow, "source_owner_name", NULL, src_owner_name) &&
                nmo_cli_record_uint(flow, "target_id", NULL, param_id) &&
                nmo_cli_record_str(flow, "target_name", NULL, resolve_name(s->repo, param_id)) &&
                nmo_cli_record_uint(flow, "target_owner_id", NULL, sub_id) &&
                nmo_cli_record_str(flow, "target_owner_name", NULL, sub_name) &&
                nmo_cli_record_str_fmt(flow, "type_guid", NULL, "%08X-%08X",
                                       type_guid.d1, type_guid.d2) &&
                nmo_cli_record_str(flow, "type_name", NULL, tname) &&
                nmo_cli_record_bool(flow, "is_shared", NULL, pin->is_shared != 0);
            if (src_obj && nmo_object_get_class_id(src_obj) == NMO_CID_PARAMETERIN) {
                flow_ok = flow_ok &&
                    nmo_cli_record_bool(flow, "source_is_parameter_in", NULL, true);
            }
            flow_ok = flow_ok && nmo_cli_record_set_summary_fmt(
                flow, "  %s.%s -> %s.%s  [%s]%s",
                src_owner_name,
                (src_text_name && src_text_name[0]) ? src_text_name : "?",
                sub_name,
                (pin_name && pin_name[0]) ? pin_name : "?",
                tname,
                pin->is_shared ? " (shared)" : "");
            ok = behavior_show_add_item(arr, flow, flow_ok);
        }
    }
    free(sources.items);

    if (ok && show_text && nmo_cli_record_array_count(arr) == 0) {
        ok = nmo_cli_record_raw(rec, "  (no parameter connections)\n");
    }
    return ok;
}

/* "Comments": the script body's schematic comments. */
static bool behavior_show_add_comments(nmo_cli_record_t *rec, const behavior_show_t *s)
{
    if (!s->bs->interface_data) {
        return true;
    }
    const nmo_interface_body_t *body = &s->bs->interface_data->script.body;
    bool ok = behavior_show_heading(rec, body->comment_count > 0, "Comments");
    nmo_cli_record_array_t *arr = ok ? behavior_show_array(rec, "comments") : NULL;
    ok = arr != NULL;
    for (size_t ci = 0; ok && ci < body->comment_count; ci++) {
        const nmo_interface_comment_t *cm = &body->comments[ci];
        bool has_text = cm->text && cm->text[0];
        size_t tlen = has_text ? strlen(cm->text) : 0;

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL &&
            nmo_cli_record_uint(item, "index", NULL, ci);
        if (cm->text) {
            item_ok = item_ok && nmo_cli_record_str(item, "text", NULL, cm->text);
        }
        item_ok = item_ok &&
            nmo_cli_record_real(item, "left", NULL, (double)cm->left, NULL) &&
            nmo_cli_record_real(item, "top", NULL, (double)cm->top, NULL) &&
            nmo_cli_record_real(item, "right", NULL, (double)cm->right, NULL) &&
            nmo_cli_record_real(item, "bottom", NULL, (double)cm->bottom, NULL);
        if (cm->style_flags) {
            item_ok = item_ok && nmo_cli_record_uint(item, "style_flags", NULL, cm->style_flags);
        }

        /* Text longer than 60 bytes shows its first 57 and "..." */
        const char *quote = has_text ? "\"" : "";
        const char *body_text = has_text ? cm->text : "(empty)";
        int body_len = has_text ? (int)(tlen > 60 ? 57 : tlen) : 7;
        const char *tail = has_text ? (tlen > 60 ? "...\"" : "\"") : "";
        if (cm->style_flags) {
            item_ok = item_ok && nmo_cli_record_set_summary_fmt(
                item, "  [%zu] %s%.*s%s  rect=(%.0f,%.0f,%.0f,%.0f)  flags=0x%X", ci,
                quote, body_len, body_text, tail,
                cm->left, cm->top, cm->right, cm->bottom, cm->style_flags);
        } else {
            item_ok = item_ok && nmo_cli_record_set_summary_fmt(
                item, "  [%zu] %s%.*s%s  rect=(%.0f,%.0f,%.0f,%.0f)", ci,
                quote, body_len, body_text, tail,
                cm->left, cm->top, cm->right, cm->bottom);
        }
        ok = behavior_show_add_item(arr, item, item_ok);
    }
    return ok;
}

int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--raw",  NULL, NMO_OPT_FLAG, "Show raw reflection (like object show)"},
        NMO_OPT_DEF_JSON,
        {"--id",   "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_RAW, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool raw_mode = nmo_opt_flag(&vals[OPT_RAW]);
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

    if (nmo_tool_owner_ensure_behavior_acceleration(c.workspace) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
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

    behavior_show_t show = {
        .c = &c,
        .repo = repo,
        .beh = beh,
        .bs = bs,
        .target_id = target_id,
        .name = name,
    };
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              behavior_show_add_header(rec, &show) &&
              behavior_show_add_io_ports(rec, &show) &&
              behavior_show_add_input_params(rec, &show) &&
              behavior_show_add_output_params(rec, &show) &&
              behavior_show_add_local_params(rec, &show) &&
              behavior_show_add_operations(rec, &show) &&
              behavior_show_add_sub_behaviors(rec, &show) &&
              behavior_show_add_links(rec, &show) &&
              behavior_show_add_data_flow(rec, &show) &&
              behavior_show_add_comments(rec, &show);
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.show", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}
