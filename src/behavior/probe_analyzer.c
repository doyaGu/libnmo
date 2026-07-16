#include "behavior/nmo_probe_analyzer.h"

#include "behavior/nmo_behavior_registry.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "type/nmo_type_guids.h"
#include "../runtime/runtime_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum probe_link_touch_mode {
    PROBE_LINK_TOUCH_ANY,
    PROBE_LINK_TOUCH_TO_IO_FIRST,
} probe_link_touch_mode_t;

void nmo_probe_selector_request_init(nmo_probe_selector_request_t *request)
{
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
    }
}

void nmo_probe_selector_result_init(nmo_probe_selector_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
}

void nmo_probe_analysis_dispose(nmo_probe_selector_result_t *result)
{
    if (result == NULL) {
        return;
    }
    free(result->candidates);
    memset(result, 0, sizeof(*result));
}

const char *nmo_probe_selector_mode_name(nmo_probe_selector_mode_t mode)
{
    switch (mode) {
    case NMO_PROBE_SELECTOR_MODE_AUTO:
        return "auto";
    case NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE:
        return "explicit_node";
    case NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK:
        return "explicit_link";
    case NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION:
        return "explicit_operation";
    case NMO_PROBE_SELECTOR_MODE_EXPLICIT_DATA_CELL:
        return "explicit_data_cell";
    case NMO_PROBE_SELECTOR_MODE_EXPLICIT:
        return "explicit";
    case NMO_PROBE_SELECTOR_MODE_UNSPECIFIED:
    default:
        return "";
    }
}

const char *nmo_probe_selector_status_name(nmo_probe_selector_status_t status)
{
    switch (status) {
    case NMO_PROBE_SELECTOR_STATUS_SELECTED:
        return "selected";
    case NMO_PROBE_SELECTOR_STATUS_NONE:
        return "none";
    case NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS:
        return "ambiguous";
    case NMO_PROBE_SELECTOR_STATUS_UNSAFE:
        return "unsafe";
    case NMO_PROBE_SELECTOR_STATUS_UNSPECIFIED:
    default:
        return "";
    }
}

const char *nmo_probe_candidate_role_name(nmo_probe_candidate_role_t role)
{
    switch (role) {
    case NMO_PROBE_CANDIDATE_MESSAGE:
        return "message";
    case NMO_PROBE_CANDIDATE_MESSAGE_SENDER:
        return "sender";
    case NMO_PROBE_CANDIDATE_MESSAGE_WAITER:
        return "waiter";
    case NMO_PROBE_CANDIDATE_MESSAGE_RECEIVER:
        return "receiver";
    case NMO_PROBE_CANDIDATE_DATA_WRITER:
        return "data_writer";
    case NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION:
        return "data_write_operation";
    case NMO_PROBE_CANDIDATE_DATA_WRITE_LINK:
        return "data_write_link";
    case NMO_PROBE_CANDIDATE_UNKNOWN:
    default:
        return "";
    }
}

static void probe_set_status(nmo_probe_selector_result_t *result,
                             nmo_probe_selector_mode_t mode,
                             nmo_probe_selector_status_t status,
                             const char *rejection_code)
{
    if (result == NULL) {
        return;
    }
    result->mode = mode;
    result->status = status;
    snprintf(result->rejection_code,
             sizeof(result->rejection_code),
             "%s",
             rejection_code != NULL ? rejection_code : "");
}

static nmo_status_t probe_reject(nmo_probe_selector_result_t *result,
                                 nmo_probe_selector_mode_t mode,
                                 nmo_probe_selector_status_t status,
                                 const char *rejection_code,
                                 const char *format,
                                 ...)
{
    probe_set_status(result, mode, status, rejection_code);
    if (result != NULL && format != NULL) {
        va_list args;
        va_start(args, format);
        vsnprintf(result->message, sizeof(result->message), format, args);
        va_end(args);
    }
    return NMO_ERR_INVALID_ARGUMENT;
}

static const nmo_behavior_state_t *probe_behavior_state_by_id(
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id)
{
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_BEHAVIOR
        ? (const nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
}

static bool probe_behavior_has_io(const nmo_behavior_state_t *state,
                                  nmo_object_id_t io_id)
{
    return state != NULL && io_id != 0u &&
           (nmo_behavior_ref_array_find(&state->inputs, io_id, NULL) ||
            nmo_behavior_ref_array_find(&state->outputs, io_id, NULL));
}

static bool probe_behavior_has_child_behavior(const nmo_behavior_state_t *state,
                                              nmo_object_id_t behavior_id)
{
    return state != NULL && behavior_id != 0u &&
           nmo_behavior_ref_array_find(
               &state->sub_behaviors, behavior_id, NULL);
}

static bool probe_behavior_has_parameter(const nmo_behavior_state_t *state,
                                         nmo_object_id_t parameter_id)
{
    return state != NULL && parameter_id != 0u &&
           (nmo_behavior_ref_array_find(
                &state->in_parameters, parameter_id, NULL) ||
            nmo_behavior_ref_array_find(
                &state->out_parameters, parameter_id, NULL) ||
            nmo_behavior_ref_array_find(
                &state->local_parameters, parameter_id, NULL) ||
            state->target_parameter_id == parameter_id);
}

static bool probe_link_touches_behavior(const nmo_behavior_state_t *behavior,
                                        const nmo_behaviorlink_state_t *link,
                                        bool to_io_only)
{
    if (behavior == NULL || link == NULL) {
        return false;
    }
    if (probe_behavior_has_io(behavior, link->out_io_id)) {
        return true;
    }
    return !to_io_only && probe_behavior_has_io(behavior, link->in_io_id);
}

static bool probe_text_starts_with_word_ci(const char *text,
                                           const char *needle)
{
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }
    while (*text == ' ' || *text == '\t' || *text == '_' || *text == '-') {
        ++text;
    }
    size_t needle_len = strlen(needle);
    for (size_t i = 0; i < needle_len; ++i) {
        char a = text[i];
        char b = needle[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    char next = text[needle_len];
    return next == '\0' || next == ' ' || next == '\t' || next == '_' ||
           next == '-';
}

static bool probe_is_message_behavior(nmo_context_t *ctx,
                                      const nmo_behavior_state_t *state)
{
    if (state == NULL ||
        (state->flags & CKBEHAVIOR_BUILDINGBLOCK) == 0u) {
        return false;
    }
    if ((state->flags & (CKBEHAVIOR_MESSAGESENDER |
                         CKBEHAVIOR_WAITSFORMESSAGE |
                         CKBEHAVIOR_MESSAGERECEIVER)) != 0u) {
        return true;
    }
    const nmo_behavior_proto_t *proto =
        ctx != NULL
            ? nmo_behavior_registry_find(nmo_context_get_bb_registry(ctx),
                                         state->block_guid)
            : NULL;
    return proto != NULL && proto->category != NULL &&
           strcmp(proto->category, "Logics/Message") == 0;
}

static nmo_probe_candidate_role_t probe_message_role(
    const nmo_behavior_state_t *state)
{
    if (state == NULL) {
        return NMO_PROBE_CANDIDATE_MESSAGE;
    }
    if ((state->flags & CKBEHAVIOR_MESSAGESENDER) != 0u) {
        return NMO_PROBE_CANDIDATE_MESSAGE_SENDER;
    }
    if ((state->flags & CKBEHAVIOR_WAITSFORMESSAGE) != 0u) {
        return NMO_PROBE_CANDIDATE_MESSAGE_WAITER;
    }
    if ((state->flags & CKBEHAVIOR_MESSAGERECEIVER) != 0u) {
        return NMO_PROBE_CANDIDATE_MESSAGE_RECEIVER;
    }
    return NMO_PROBE_CANDIDATE_MESSAGE;
}

static bool probe_is_data_write_behavior(nmo_context_t *ctx,
                                         const nmo_behavior_state_t *state)
{
    if (state == NULL ||
        (state->flags & CKBEHAVIOR_BUILDINGBLOCK) == 0u) {
        return false;
    }
    const nmo_behavior_proto_t *proto =
        ctx != NULL
            ? nmo_behavior_registry_find(nmo_context_get_bb_registry(ctx),
                                         state->block_guid)
            : NULL;
    if (proto == NULL || proto->category == NULL ||
        strcmp(proto->category, "Logics/Array") != 0 ||
        proto->name == NULL) {
        return false;
    }
    static const char *const write_verbs[] = {
        "add", "change", "clear", "insert", "move", "remove",
        "reverse", "set", "shuffle", "sort", "swap",
    };
    for (size_t i = 0; i < sizeof(write_verbs) / sizeof(write_verbs[0]); ++i) {
        if (probe_text_starts_with_word_ci(proto->name, write_verbs[i])) {
            return true;
        }
    }
    return false;
}

static bool probe_ensure_candidate_capacity(nmo_probe_selector_result_t *result)
{
    if (result == NULL) {
        return false;
    }
    if (result->candidate_count < result->candidate_capacity) {
        return true;
    }
    size_t new_capacity =
        result->candidate_capacity == 0u ? 8u : result->candidate_capacity * 2u;
    nmo_probe_selector_candidate_t *new_candidates =
        (nmo_probe_selector_candidate_t *)realloc(
            result->candidates,
            new_capacity * sizeof(*new_candidates));
    if (new_candidates == NULL) {
        return false;
    }
    memset(new_candidates + result->candidate_capacity,
           0,
           (new_capacity - result->candidate_capacity) *
               sizeof(*new_candidates));
    result->candidates = new_candidates;
    result->candidate_capacity = new_capacity;
    return true;
}

nmo_status_t nmo_probe_selector_result_add_candidate(
    nmo_probe_selector_result_t *result,
    const nmo_probe_selector_candidate_t *candidate)
{
    if (result == NULL || candidate == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!probe_ensure_candidate_capacity(result)) {
        return NMO_ERR_NOMEM;
    }
    result->candidates[result->candidate_count++] = *candidate;
    return NMO_OK;
}

static nmo_probe_selector_candidate_t *probe_add_candidate(
    nmo_context_t *ctx,
    nmo_probe_selector_result_t *result,
    nmo_object_id_t parent_id,
    nmo_object_id_t node_id,
    nmo_object_id_t link_id,
    nmo_object_id_t operation_id,
    const nmo_behavior_state_t *state,
    nmo_probe_candidate_role_t explicit_role)
{
    if (!probe_ensure_candidate_capacity(result)) {
        return NULL;
    }
    const nmo_behavior_proto_t *proto =
        ctx != NULL && state != NULL
            ? nmo_behavior_registry_find(nmo_context_get_bb_registry(ctx),
                                         state->block_guid)
            : NULL;
    size_t index = result->candidate_count++;
    memset(&result->candidates[index], 0, sizeof(result->candidates[index]));
    result->candidates[index].node_id = node_id;
    result->candidates[index].parent_id = parent_id;
    result->candidates[index].boundary_behavior_id = parent_id;
    result->candidates[index].link_id = link_id;
    result->candidates[index].operation_id = operation_id;
    result->candidates[index].confidence = 1.0;
    result->candidates[index].bb_guid =
        state != NULL ? state->block_guid : NMO_GUID_NULL;
    snprintf(result->candidates[index].proto_name,
             sizeof(result->candidates[index].proto_name),
             "%s",
             proto != NULL && proto->name != NULL ? proto->name : "");
    result->candidates[index].role =
        explicit_role != NMO_PROBE_CANDIDATE_UNKNOWN
            ? explicit_role
            : probe_message_role(state);
    return &result->candidates[index];
}

static nmo_guid_t probe_dataarray_column_type_guid(
    const nmo_dataarray_state_t *state,
    uint32_t col)
{
    if (state == NULL || state->column_formats == NULL ||
        col >= state->column_count) {
        return NMO_GUID_NULL;
    }
    const nmo_dataarray_column_format_t *format = &state->column_formats[col];
    if (!nmo_guid_is_null(format->parameter_type_guid)) {
        return format->parameter_type_guid;
    }
    switch (format->type) {
    case CKARRAYTYPE_INT:
        return CKPGUID_INT;
    case CKARRAYTYPE_FLOAT:
        return CKPGUID_FLOAT;
    case CKARRAYTYPE_STRING:
        return CKPGUID_STRING;
    case CKARRAYTYPE_OBJECT:
        return CKPGUID_OBJECT;
    case CKARRAYTYPE_PARAMETER:
        return CKPGUID_PARAMETEROUT;
    default:
        return NMO_GUID_NULL;
    }
}

static void probe_enrich_candidate_with_data_cell(
    nmo_object_repository_t *repo,
    const nmo_probe_selector_request_t *request,
    const nmo_parameteroperation_state_t *operation,
    nmo_probe_selector_candidate_t *candidate)
{
    if (candidate == NULL || request == NULL) {
        return;
    }
    if (request->dataarray_id != 0u) {
        candidate->dataarray_id = request->dataarray_id;
        nmo_object_t *dataarray =
            repo != NULL
                ? nmo_object_repository_find_by_id(repo, request->dataarray_id)
                : NULL;
        const nmo_dataarray_state_t *state =
            dataarray != NULL &&
                    nmo_object_get_class_id(dataarray) == NMO_CID_DATAARRAY
                ? (const nmo_dataarray_state_t *)nmo_object_get_state(dataarray)
                : NULL;
        if (request->has_data_cell) {
            candidate->column_type_guid =
                probe_dataarray_column_type_guid(state, request->col);
        }
    }
    if (operation != NULL) {
        if (operation->has_in1) {
            candidate->source_parameter_id = operation->in1_id;
        }
        if (operation->has_out) {
            candidate->value_parameter_id = operation->out_id;
        } else if (operation->has_in2) {
            candidate->value_parameter_id = operation->in2_id;
        }
    }
}

static nmo_guid_t probe_parameter_type_guid(nmo_object_repository_t *repo,
                                            nmo_object_id_t parameter_id)
{
    nmo_object_t *object =
        repo != NULL ? nmo_object_repository_find_by_id(repo, parameter_id)
                     : NULL;
    if (object == NULL) {
        return NMO_GUID_NULL;
    }
    switch (nmo_object_get_class_id(object)) {
    case NMO_CID_PARAMETERIN:
        return ((const nmo_parameterin_state_t *)nmo_object_get_state(object))
            ->type_guid;
    case NMO_CID_PARAMETEROUT:
        return ((const nmo_parameterout_state_t *)nmo_object_get_state(object))
            ->base.type_guid;
    case NMO_CID_PARAMETERLOCAL:
        return ((const nmo_parameterlocal_state_t *)nmo_object_get_state(object))
            ->base.type_guid;
    default:
        return NMO_GUID_NULL;
    }
}

static bool probe_data_write_candidate_type_matches(
    nmo_object_repository_t *repo,
    const nmo_probe_selector_candidate_t *candidate,
    const nmo_behavior_state_t *node)
{
    (void)node;
    if (repo == NULL || candidate == NULL ||
        nmo_guid_is_null(candidate->column_type_guid)) {
        return true;
    }
    if (candidate->value_parameter_id != 0u) {
        nmo_guid_t value_type =
            probe_parameter_type_guid(repo, candidate->value_parameter_id);
        return nmo_guid_is_null(value_type) ||
               nmo_guid_equals(value_type, candidate->column_type_guid);
    }
    return true;
}

static nmo_status_t probe_reject_type_mismatch(
    nmo_probe_selector_result_t *result,
    nmo_probe_selector_mode_t mode)
{
    return probe_reject(
        result,
        mode,
        NMO_PROBE_SELECTOR_STATUS_UNSAFE,
        "type_mismatch",
        "type_mismatch: debug probe data write-site value type does not match data array column type");
}

static void probe_append_id(char *buffer,
                            size_t buffer_size,
                            size_t *buffer_len,
                            size_t index,
                            nmo_object_id_t id)
{
    if (buffer == NULL || buffer_size == 0u || buffer_len == NULL ||
        *buffer_len >= buffer_size - 1u) {
        return;
    }
    int written = snprintf(buffer + *buffer_len,
                           buffer_size - *buffer_len,
                           "%s%u",
                           index == 0u ? "" : ",",
                           (unsigned)id);
    if (written <= 0) {
        return;
    }
    size_t append = (size_t)written;
    size_t available = buffer_size - *buffer_len;
    *buffer_len += append < available ? append : available - 1u;
}

static size_t probe_collect_touching_links(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    const nmo_behavior_state_t *target,
    bool to_io_only,
    nmo_object_id_t *out_selected_link_id,
    const nmo_behaviorlink_state_t **out_selected_link,
    char *candidate_ids,
    size_t candidate_ids_size)
{
    if (out_selected_link_id != NULL) {
        *out_selected_link_id = 0u;
    }
    if (out_selected_link != NULL) {
        *out_selected_link = NULL;
    }
    if (candidate_ids != NULL && candidate_ids_size > 0u) {
        candidate_ids[0] = '\0';
    }
    if (repo == NULL || parent == NULL || target == NULL) {
        return 0u;
    }

    size_t candidate_count = 0u;
    size_t candidate_ids_len = 0u;
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
        if (!probe_link_touches_behavior(target, link, to_io_only)) {
            continue;
        }
        probe_append_id(candidate_ids,
                        candidate_ids_size,
                        &candidate_ids_len,
                        candidate_count,
                        link_id);
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

static size_t probe_select_touching_links(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    const nmo_behavior_state_t *target,
    probe_link_touch_mode_t mode,
    nmo_object_id_t *out_selected_link_id,
    const nmo_behaviorlink_state_t **out_selected_link,
    char *candidate_ids,
    size_t candidate_ids_size)
{
    size_t count = probe_collect_touching_links(
        repo,
        parent,
        target,
        mode == PROBE_LINK_TOUCH_TO_IO_FIRST,
        out_selected_link_id,
        out_selected_link,
        candidate_ids,
        candidate_ids_size);
    if (count == 0u && mode == PROBE_LINK_TOUCH_TO_IO_FIRST) {
        count = probe_collect_touching_links(repo,
                                             parent,
                                             target,
                                             false,
                                             out_selected_link_id,
                                             out_selected_link,
                                             candidate_ids,
                                             candidate_ids_size);
    }
    return count;
}

static void probe_apply_selected_link(nmo_probe_selector_result_t *result,
                                      nmo_object_id_t link_id,
                                      const nmo_behaviorlink_state_t *link)
{
    if (result == NULL || link == NULL) {
        return;
    }
    result->selected_link_id = link_id;
    result->from_io_id = link->in_io_id;
    result->to_io_id = link->out_io_id;
    result->safe_insertion.selected = true;
    result->safe_insertion.selected_node_id = result->selected_node_id;
    result->safe_insertion.selected_operation_id =
        result->selected_operation_id;
    result->safe_insertion.selected_link_id = link_id;
    result->safe_insertion.remove_link_id = link_id;
    result->safe_insertion.insert_from_io_id = link->in_io_id;
    result->safe_insertion.insert_to_io_id = link->out_io_id;
    if (!result->has_delay && link->activation_delay > 0) {
        result->delay = (uint32_t)link->activation_delay;
        result->has_delay = true;
    }
    if (result->has_delay) {
        result->safe_insertion.has_preserved_delay = true;
        result->safe_insertion.preserved_delay = result->delay;
    }
}

static nmo_status_t probe_select_safe_link(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    const nmo_behavior_state_t *target,
    probe_link_touch_mode_t mode,
    nmo_probe_selector_result_t *result,
    nmo_probe_selector_mode_t result_mode,
    const char *error_prefix)
{
    char candidate_ids[256];
    nmo_object_id_t selected_link_id = 0u;
    const nmo_behaviorlink_state_t *selected_link = NULL;
    size_t candidate_count = probe_select_touching_links(repo,
                                                         parent,
                                                         target,
                                                         mode,
                                                         &selected_link_id,
                                                         &selected_link,
                                                         candidate_ids,
                                                         sizeof(candidate_ids));
    if (candidate_count != 1u || selected_link == NULL) {
        return probe_reject(
            result,
            result_mode,
            NMO_PROBE_SELECTOR_STATUS_UNSAFE,
            "unsafe_probe_insertion",
            "%s (candidate links: [%s])",
            error_prefix != NULL ? error_prefix
                                 : "debug probe automatic insertion is unsafe",
            candidate_ids);
    }
    probe_apply_selected_link(result, selected_link_id, selected_link);
    probe_set_status(result, result_mode, NMO_PROBE_SELECTOR_STATUS_SELECTED,
                     NULL);
    return NMO_OK;
}

static nmo_object_id_t probe_find_operation_owner_behavior(
    nmo_object_repository_t *repo,
    nmo_object_id_t operation_id,
    const nmo_parameteroperation_state_t *operation)
{
    if (repo == NULL || operation_id == 0u) {
        return 0u;
    }
    if (operation != NULL && operation->has_owner &&
        operation->owner_id != 0u) {
        nmo_object_t *owner =
            nmo_object_repository_find_by_id(repo, operation->owner_id);
        if (owner != NULL &&
            nmo_object_get_class_id(owner) == NMO_CID_BEHAVIOR) {
            return operation->owner_id;
        }
    }
    size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL ||
            nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
            continue;
        }
        const nmo_behavior_state_t *behavior =
            (const nmo_behavior_state_t *)nmo_object_get_state(object);
        if (behavior != NULL &&
            nmo_behavior_ref_array_find(
                &behavior->operations, operation_id, NULL)) {
            return nmo_object_get_id(object);
        }
    }
    return 0u;
}

static nmo_object_id_t probe_find_parameter_owner_behavior(
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id)
{
    if (repo == NULL || parameter_id == 0u) {
        return 0u;
    }
    size_t object_count = 0u;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    for (size_t i = 0; objects != NULL && i < object_count; ++i) {
        nmo_object_t *object = objects[i];
        if (object == NULL ||
            nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
            continue;
        }
        const nmo_behavior_state_t *behavior =
            (const nmo_behavior_state_t *)nmo_object_get_state(object);
        if (probe_behavior_has_parameter(behavior, parameter_id)) {
            return nmo_object_get_id(object);
        }
    }
    return 0u;
}

static void probe_add_related_behavior(nmo_object_id_t *ids,
                                       size_t *count,
                                       size_t capacity,
                                       nmo_object_id_t id)
{
    if (ids == NULL || count == NULL || id == 0u || *count >= capacity) {
        return;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (ids[i] == id) {
            return;
        }
    }
    ids[(*count)++] = id;
}

static void probe_collect_operation_parameter_behaviors(
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_object_id_t *ids,
    size_t *count,
    size_t capacity)
{
    nmo_object_id_t owner =
        probe_find_parameter_owner_behavior(repo, parameter_id);
    probe_add_related_behavior(ids, count, capacity, owner);

    nmo_object_t *parameter_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, parameter_id)
        : NULL;
    if (parameter_obj != NULL &&
        nmo_object_get_class_id(parameter_obj) == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *param_in =
            (const nmo_parameterin_state_t *)nmo_object_get_state(
                parameter_obj);
        if (param_in != NULL && param_in->source_id != 0u) {
            owner = probe_find_parameter_owner_behavior(repo,
                                                        param_in->source_id);
            probe_add_related_behavior(ids, count, capacity, owner);
        }
    }

    size_t object_count = repo != NULL
        ? nmo_object_repository_get_count(repo)
        : 0u;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL ||
            nmo_object_get_class_id(object) != NMO_CID_PARAMETERIN) {
            continue;
        }
        const nmo_parameterin_state_t *param_in =
            (const nmo_parameterin_state_t *)nmo_object_get_state(object);
        if (param_in == NULL || param_in->source_id != parameter_id) {
            continue;
        }
        owner = probe_find_parameter_owner_behavior(
            repo, nmo_object_get_id(object));
        probe_add_related_behavior(ids, count, capacity, owner);
    }

    if (parameter_obj != NULL &&
        nmo_object_get_class_id(parameter_obj) == NMO_CID_PARAMETEROUT) {
        const nmo_parameterout_state_t *param_out =
            (const nmo_parameterout_state_t *)nmo_object_get_state(
                parameter_obj);
        const nmo_object_id_t *destinations =
            param_out != NULL ? param_out->destination_ids : NULL;
        for (uint32_t i = 0;
             destinations != NULL && i < param_out->destination_count; ++i) {
            owner = probe_find_parameter_owner_behavior(repo, destinations[i]);
            probe_add_related_behavior(ids, count, capacity, owner);
        }
    }
}

static size_t probe_collect_operation_related_behaviors(
    nmo_object_repository_t *repo,
    nmo_object_id_t operation_id,
    const nmo_parameteroperation_state_t *operation,
    nmo_object_id_t *ids,
    size_t capacity)
{
    size_t count = 0u;
    if (repo == NULL || operation == NULL || ids == NULL || capacity == 0u) {
        return 0u;
    }
    probe_add_related_behavior(
        ids,
        &count,
        capacity,
        probe_find_operation_owner_behavior(repo, operation_id, operation));
    if (operation->has_in1) {
        probe_collect_operation_parameter_behaviors(
            repo, operation->in1_id, ids, &count, capacity);
    }
    if (operation->has_in2) {
        probe_collect_operation_parameter_behaviors(
            repo, operation->in2_id, ids, &count, capacity);
    }
    if (operation->has_out) {
        probe_collect_operation_parameter_behaviors(
            repo, operation->out_id, ids, &count, capacity);
    }
    return count;
}

static bool probe_link_touches_any_behavior(
    nmo_object_repository_t *repo,
    const nmo_behaviorlink_state_t *link,
    const nmo_object_id_t *behavior_ids,
    size_t behavior_count)
{
    if (repo == NULL || link == NULL || behavior_ids == NULL) {
        return false;
    }
    for (size_t i = 0; i < behavior_count; ++i) {
        const nmo_behavior_state_t *behavior =
            probe_behavior_state_by_id(repo, behavior_ids[i]);
        if (probe_link_touches_behavior(behavior, link, false)) {
            return true;
        }
    }
    return false;
}

static bool probe_explicit_endpoints_touch_operation_flow(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    const nmo_object_id_t *related_behaviors,
    size_t related_count)
{
    if (repo == NULL || parent == NULL || from_io_id == 0u ||
        to_io_id == 0u || related_behaviors == NULL ||
        related_count == 0u) {
        return false;
    }
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
        if (link == NULL || link->in_io_id != from_io_id ||
            link->out_io_id != to_io_id) {
            continue;
        }
        return probe_link_touches_any_behavior(repo,
                                               link,
                                               related_behaviors,
                                               related_count);
    }
    return false;
}

static size_t probe_collect_operation_touching_links(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    nmo_object_id_t operation_id,
    const nmo_parameteroperation_state_t *operation,
    nmo_object_id_t *out_selected_link_id,
    const nmo_behaviorlink_state_t **out_selected_link)
{
    if (out_selected_link_id != NULL) {
        *out_selected_link_id = 0u;
    }
    if (out_selected_link != NULL) {
        *out_selected_link = NULL;
    }
    if (repo == NULL || parent == NULL || operation == NULL) {
        return 0u;
    }

    nmo_object_id_t related_behaviors[32];
    size_t related_count = probe_collect_operation_related_behaviors(
        repo,
        operation_id,
        operation,
        related_behaviors,
        sizeof(related_behaviors) / sizeof(related_behaviors[0]));
    if (related_count == 0u) {
        return 0u;
    }

    size_t count = 0u;
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
        if (!probe_link_touches_any_behavior(
                repo, link, related_behaviors, related_count)) {
            continue;
        }
        if (out_selected_link_id != NULL) {
            *out_selected_link_id = link_id;
        }
        if (out_selected_link != NULL) {
            *out_selected_link = link;
        }
        ++count;
    }
    return count;
}

static nmo_status_t probe_analyze_message(nmo_context_t *ctx,
                                          nmo_object_repository_t *repo,
                                          const nmo_behavior_state_t *parent,
                                          const nmo_probe_selector_request_t *request,
                                          nmo_probe_selector_result_t *result)
{
    if (request->message_node_id != 0u) {
        nmo_probe_selector_mode_t mode =
            request->remove_link_id != 0u
                ? NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK
                : NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE;
        const nmo_behavior_state_t *message =
            probe_behavior_state_by_id(repo, request->message_node_id);
        if (message == NULL) {
            return probe_reject(result,
                                mode,
                                NMO_PROBE_SELECTOR_STATUS_NONE,
                                "message_node_not_found",
                                "debug probe message-node not found");
        }
        if (!probe_is_message_behavior(ctx, message)) {
            return probe_reject(
                result,
                mode,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "not_message_behavior",
                "debug probe message-node target is not a message behavior");
        }
        result->selected_node_id = request->message_node_id;
        result->safe_insertion.selected_node_id = request->message_node_id;
        probe_add_candidate(ctx,
                            result,
                            request->behavior_id,
                            request->message_node_id,
                            0u,
                            0u,
                            message,
                            NMO_PROBE_CANDIDATE_UNKNOWN);
        if (request->remove_link_id != 0u) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, request->remove_link_id);
            const nmo_behaviorlink_state_t *link =
                link_obj != NULL &&
                        nmo_object_get_class_id(link_obj) ==
                            NMO_CID_BEHAVIORLINK
                    ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(
                          link_obj)
                    : NULL;
            if (link == NULL ||
                !probe_link_touches_behavior(message, link, false)) {
                return probe_reject(
                    result,
                    mode,
                    NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                    "unsafe_probe_insertion",
                    "debug probe remove-link does not touch selected message-node");
            }
            if (parent == NULL ||
                !nmo_behavior_ref_array_find(&parent->sub_behavior_links,
                                             request->remove_link_id,
                                             NULL)) {
                return probe_reject(
                    result,
                    mode,
                    NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                    "cross_boundary_probe_link",
                    "debug probe remove-link is not in the selected behavior boundary");
            }
            probe_apply_selected_link(result, request->remove_link_id, link);
            probe_set_status(result, mode, NMO_PROBE_SELECTOR_STATUS_SELECTED,
                             NULL);
            return NMO_OK;
        }
        if (request->from_io_id != 0u || request->to_io_id != 0u) {
            if (!probe_behavior_has_io(message, request->from_io_id) &&
                !probe_behavior_has_io(message, request->to_io_id)) {
                return probe_reject(
                    result,
                    mode,
                    NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                    "unsafe_probe_insertion",
                    "debug probe IO endpoint does not touch selected message-node");
            }
            result->from_io_id = request->from_io_id;
            result->to_io_id = request->to_io_id;
            result->safe_insertion.selected = true;
            result->safe_insertion.selected_node_id = result->selected_node_id;
            result->safe_insertion.insert_from_io_id = request->from_io_id;
            result->safe_insertion.insert_to_io_id = request->to_io_id;
            probe_set_status(result, mode, NMO_PROBE_SELECTOR_STATUS_SELECTED,
                             NULL);
            return NMO_OK;
        }
        return probe_select_safe_link(
            repo,
            parent,
            message,
            PROBE_LINK_TOUCH_TO_IO_FIRST,
            result,
            mode,
            "debug probe automatic insertion is unsafe");
    }

    probe_set_status(result,
                     NMO_PROBE_SELECTOR_MODE_AUTO,
                     NMO_PROBE_SELECTOR_STATUS_UNSPECIFIED,
                     NULL);
    nmo_object_id_t selected_id = 0u;
    size_t candidate_count = 0u;
    char candidate_ids[256];
    size_t candidate_ids_len = 0u;
    candidate_ids[0] = '\0';
    for (size_t i = 0; parent != NULL && i < parent->sub_behaviors.count; ++i) {
        nmo_object_id_t child_id = nmo_behavior_ref_array_get_id(
            &parent->sub_behaviors, i);
        if (child_id == 0) continue;
        const nmo_behavior_state_t *child =
            probe_behavior_state_by_id(repo, child_id);
        if (!probe_is_message_behavior(ctx, child)) {
            continue;
        }
        probe_add_candidate(ctx,
                            result,
                            request->behavior_id,
                            child_id,
                            0u,
                            0u,
                            child,
                            NMO_PROBE_CANDIDATE_UNKNOWN);
        probe_append_id(candidate_ids,
                        sizeof(candidate_ids),
                        &candidate_ids_len,
                        candidate_count,
                        child_id);
        selected_id = child_id;
        ++candidate_count;
    }
    if (candidate_count == 0u) {
        return probe_reject(result,
                            NMO_PROBE_SELECTOR_MODE_AUTO,
                            NMO_PROBE_SELECTOR_STATUS_NONE,
                            "no_message_candidates",
                            "debug probe message selector found no message "
                            "candidates (candidates: [])");
    }
    if (candidate_count > 1u) {
        return probe_reject(
            result,
            NMO_PROBE_SELECTOR_MODE_AUTO,
            NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS,
            "ambiguous_message_candidates",
            "debug probe message selector is ambiguous (candidates: [%s])",
            candidate_ids);
    }
    const nmo_behavior_state_t *message =
        probe_behavior_state_by_id(repo, selected_id);
    result->selected_node_id = selected_id;
    result->safe_insertion.selected_node_id = selected_id;
    return probe_select_safe_link(repo,
                                  parent,
                                  message,
                                  PROBE_LINK_TOUCH_TO_IO_FIRST,
                                  result,
                                  NMO_PROBE_SELECTOR_MODE_AUTO,
                                  "debug probe automatic insertion is unsafe");
}

static nmo_status_t probe_analyze_data_cell(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent,
    const nmo_probe_selector_request_t *request,
    nmo_probe_selector_result_t *result)
{
    unsigned explicit_count = 0u;
    explicit_count += request->write_node_id != 0u ? 1u : 0u;
    explicit_count += request->write_operation_id != 0u ? 1u : 0u;
    explicit_count += request->write_link_id != 0u ? 1u : 0u;
    if (explicit_count > 1u) {
        return probe_reject(
            result,
            NMO_PROBE_SELECTOR_MODE_EXPLICIT,
            NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS,
            "ambiguous_write_site_selector",
            "debug probe data-cell write selector is ambiguous");
    }

    if (request->write_node_id != 0u) {
        const nmo_behavior_state_t *node =
            probe_behavior_state_by_id(repo, request->write_node_id);
        if (node == NULL) {
            return probe_reject(result,
                                NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE,
                                NMO_PROBE_SELECTOR_STATUS_NONE,
                                "write_node_not_found",
                                "debug probe write-node not found");
        }
        if (!probe_is_data_write_behavior(ctx, node)) {
            return probe_reject(
                result,
                NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "not_data_write_behavior",
                "debug probe write-node target is not a data write behavior");
        }
        result->selected_node_id = request->write_node_id;
        result->safe_insertion.selected_node_id = request->write_node_id;
        nmo_probe_selector_candidate_t *candidate =
            probe_add_candidate(ctx,
                                result,
                                request->behavior_id,
                                request->write_node_id,
                                0u,
                                0u,
                                node,
                                NMO_PROBE_CANDIDATE_DATA_WRITER);
        probe_enrich_candidate_with_data_cell(repo, request, NULL, candidate);
        if (!probe_data_write_candidate_type_matches(repo, candidate, node)) {
            return probe_reject_type_mismatch(
                result, NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE);
        }
        if (request->remove_link_id != 0u) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, request->remove_link_id);
            const nmo_behaviorlink_state_t *link =
                link_obj != NULL &&
                        nmo_object_get_class_id(link_obj) ==
                            NMO_CID_BEHAVIORLINK
                    ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(
                          link_obj)
                    : NULL;
            if (link == NULL ||
                !probe_link_touches_behavior(node, link, false)) {
                return probe_reject(
                    result,
                    NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE,
                    NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                    "unsafe_probe_insertion",
                    "debug probe remove-link does not touch selected write-node");
            }
            probe_apply_selected_link(result, request->remove_link_id, link);
            probe_set_status(result,
                             NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE,
                             NMO_PROBE_SELECTOR_STATUS_SELECTED,
                             NULL);
            return NMO_OK;
        }
        return probe_select_safe_link(
            repo,
            parent,
            node,
            PROBE_LINK_TOUCH_ANY,
            result,
            NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE,
            "unsafe_probe_insertion: debug probe automatic data write "
            "insertion is unsafe");
    }

    if (request->write_operation_id != 0u) {
        nmo_object_t *op_obj =
            nmo_object_repository_find_by_id(repo, request->write_operation_id);
        if (op_obj == NULL) {
            return probe_reject(result,
                                NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                                NMO_PROBE_SELECTOR_STATUS_NONE,
                                "write_operation_not_found",
                                "debug probe write-operation not found");
        }
        if (nmo_object_get_class_id(op_obj) != NMO_CID_PARAMETEROPERATION) {
            return probe_reject(
                result,
                NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "not_parameter_operation",
                "debug probe write-operation target is not a parameter operation");
        }
        const nmo_parameteroperation_state_t *operation =
            (const nmo_parameteroperation_state_t *)nmo_object_get_state(op_obj);
        if (request->remove_link_id == 0u && request->from_io_id == 0u &&
            request->to_io_id == 0u) {
            return probe_reject(
                result,
                NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "unsafe_probe_insertion",
                "debug probe write-operation requires --remove-link or explicit IO endpoints");
        }
        nmo_object_id_t operation_owner_id =
            probe_find_operation_owner_behavior(repo,
                                                request->write_operation_id,
                                                operation);
        if (operation_owner_id == 0u ||
            (operation_owner_id != request->behavior_id &&
             !probe_behavior_has_child_behavior(parent, operation_owner_id))) {
            return probe_reject(
                result,
                NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "unsafe_probe_insertion",
                "unsafe_probe_insertion: debug probe write-operation is "
                "outside the selected behavior boundary");
        }
        if (request->remove_link_id != 0u) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, request->remove_link_id);
            const nmo_behaviorlink_state_t *link =
                link_obj != NULL &&
                        nmo_object_get_class_id(link_obj) ==
                            NMO_CID_BEHAVIORLINK
                    ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(
                          link_obj)
                    : NULL;
            if (link == NULL || parent == NULL ||
                !nmo_behavior_ref_array_find(&parent->sub_behavior_links,
                                             request->remove_link_id,
                                             NULL)) {
                return probe_reject(
                    result,
                    NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                    NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                    "cross_boundary_probe_link",
                    "debug probe remove-link is not in the selected behavior boundary");
            }
            nmo_object_id_t related_behaviors[32];
            size_t related_count =
                probe_collect_operation_related_behaviors(
                    repo,
                    request->write_operation_id,
                    operation,
                    related_behaviors,
                    sizeof(related_behaviors) / sizeof(related_behaviors[0]));
            if (!probe_link_touches_any_behavior(
                    repo, link, related_behaviors, related_count)) {
                return probe_reject(
                    result,
                    NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                    NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                    "unsafe_probe_insertion",
                    "unsafe_probe_insertion: debug probe remove-link does not "
                    "touch selected write-operation data flow");
            }
            probe_apply_selected_link(result, request->remove_link_id, link);
            nmo_probe_selector_candidate_t *candidate =
                probe_add_candidate(ctx,
                                    result,
                                    request->behavior_id,
                                    0u,
                                    request->remove_link_id,
                                    request->write_operation_id,
                                    NULL,
                                    NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION);
            probe_enrich_candidate_with_data_cell(
                repo, request, operation, candidate);
            if (!probe_data_write_candidate_type_matches(
                    repo, candidate, NULL)) {
                return probe_reject_type_mismatch(
                    result, NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION);
            }
        } else {
            nmo_object_id_t related_behaviors[32];
            size_t related_count =
                probe_collect_operation_related_behaviors(
                    repo,
                    request->write_operation_id,
                    operation,
                    related_behaviors,
                    sizeof(related_behaviors) / sizeof(related_behaviors[0]));
            if (!probe_explicit_endpoints_touch_operation_flow(
                    repo,
                    parent,
                    request->from_io_id,
                    request->to_io_id,
                    related_behaviors,
                    related_count)) {
                return probe_reject(
                    result,
                    NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                    NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                    "unsafe_probe_insertion",
                    "unsafe_probe_insertion: debug probe explicit IO endpoints do not touch selected write-operation data flow");
            }
            result->from_io_id = request->from_io_id;
            result->to_io_id = request->to_io_id;
            result->safe_insertion.selected = true;
            result->safe_insertion.selected_operation_id =
                request->write_operation_id;
            result->safe_insertion.insert_from_io_id = request->from_io_id;
            result->safe_insertion.insert_to_io_id = request->to_io_id;
            nmo_probe_selector_candidate_t *candidate =
                probe_add_candidate(ctx,
                                    result,
                                    request->behavior_id,
                                    0u,
                                    0u,
                                    request->write_operation_id,
                                    NULL,
                                    NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION);
            probe_enrich_candidate_with_data_cell(
                repo, request, operation, candidate);
            if (!probe_data_write_candidate_type_matches(
                    repo, candidate, NULL)) {
                return probe_reject_type_mismatch(
                    result, NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION);
            }
        }
        result->selected_operation_id = request->write_operation_id;
        result->safe_insertion.selected_operation_id =
            request->write_operation_id;
        probe_set_status(result,
                         NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
                         NMO_PROBE_SELECTOR_STATUS_SELECTED,
                         NULL);
        return NMO_OK;
    }

    if (request->write_link_id != 0u) {
        if (request->remove_link_id != 0u &&
            request->remove_link_id != request->write_link_id) {
            return probe_reject(
                result,
                NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "conflicting_probe_links",
                "debug probe --write-link conflicts with --remove-link");
        }
        nmo_object_t *link_obj =
            nmo_object_repository_find_by_id(repo, request->write_link_id);
        const nmo_behaviorlink_state_t *link =
            link_obj != NULL &&
                    nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(
                      link_obj)
                : NULL;
        if (link_obj == NULL) {
            return probe_reject(result,
                                NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK,
                                NMO_PROBE_SELECTOR_STATUS_NONE,
                                "write_link_not_found",
                                "debug probe write-link not found");
        }
        if (link == NULL) {
            return probe_reject(
                result,
                NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "not_behavior_link",
                "debug probe write-link target is not a behavior link");
        }
        if (parent == NULL ||
            !nmo_behavior_ref_array_find(&parent->sub_behavior_links,
                                         request->write_link_id,
                                         NULL)) {
            return probe_reject(
                result,
                NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK,
                NMO_PROBE_SELECTOR_STATUS_UNSAFE,
                "cross_boundary_probe_link",
                "debug probe write-link is not in the selected behavior boundary");
        }
        probe_apply_selected_link(result, request->write_link_id, link);
        probe_set_status(result,
                         NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK,
                         NMO_PROBE_SELECTOR_STATUS_SELECTED,
                         NULL);
        return NMO_OK;
    }

    nmo_object_id_t selected_id = 0u;
    nmo_object_id_t selected_operation_id = 0u;
    nmo_object_id_t selected_link_id = 0u;
    const nmo_behaviorlink_state_t *selected_link = NULL;
    size_t candidate_count = 0u;
    char candidate_ids[256];
    size_t candidate_ids_len = 0u;
    candidate_ids[0] = '\0';
    for (size_t i = 0; parent != NULL && i < parent->sub_behaviors.count; ++i) {
        nmo_object_id_t child_id = nmo_behavior_ref_array_get_id(
            &parent->sub_behaviors, i);
        if (child_id == 0) continue;
        const nmo_behavior_state_t *child =
            probe_behavior_state_by_id(repo, child_id);
        if (!probe_is_data_write_behavior(ctx, child)) {
            continue;
        }
        nmo_probe_selector_candidate_t *candidate =
            probe_add_candidate(ctx,
                                result,
                                request->behavior_id,
                                child_id,
                                0u,
                                0u,
                                child,
                                NMO_PROBE_CANDIDATE_DATA_WRITER);
        probe_enrich_candidate_with_data_cell(repo, request, NULL, candidate);
        probe_append_id(candidate_ids,
                        sizeof(candidate_ids),
                        &candidate_ids_len,
                        candidate_count,
                        child_id);
        selected_id = child_id;
        ++candidate_count;
    }
    for (size_t i = 0; parent != NULL && i < parent->operations.count; ++i) {
        nmo_object_id_t operation_id = nmo_behavior_ref_array_get_id(
            &parent->operations, i);
        if (operation_id == 0) continue;
        nmo_object_t *op_obj =
            nmo_object_repository_find_by_id(repo, operation_id);
        const nmo_parameteroperation_state_t *operation =
            op_obj != NULL &&
                    nmo_object_get_class_id(op_obj) == NMO_CID_PARAMETEROPERATION
                ? (const nmo_parameteroperation_state_t *)nmo_object_get_state(
                      op_obj)
                : NULL;
        if (operation == NULL) {
            continue;
        }
        nmo_object_id_t operation_link_id = 0u;
        const nmo_behaviorlink_state_t *operation_link = NULL;
        size_t touching_count = probe_collect_operation_touching_links(
            repo,
            parent,
            operation_id,
            operation,
            &operation_link_id,
            &operation_link);
        if (touching_count == 0u) {
            continue;
        }
        nmo_probe_selector_candidate_t *candidate =
            probe_add_candidate(ctx,
                                result,
                                request->behavior_id,
                                0u,
                                operation_link_id,
                                operation_id,
                                NULL,
                                NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION);
        probe_enrich_candidate_with_data_cell(
            repo, request, operation, candidate);
        probe_append_id(candidate_ids,
                        sizeof(candidate_ids),
                        &candidate_ids_len,
                        candidate_count,
                        operation_id);
        selected_operation_id = operation_id;
        selected_link_id = operation_link_id;
        selected_link = operation_link;
        ++candidate_count;
    }
    if (candidate_count > 1u) {
        return probe_reject(
            result,
            NMO_PROBE_SELECTOR_MODE_AUTO,
            NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS,
            "ambiguous_data_write_candidates",
            "debug probe data-cell write selector is ambiguous (candidates: [%s])",
            candidate_ids);
    }
    if (candidate_count == 1u) {
        if (selected_operation_id != 0u && selected_link != NULL) {
            nmo_probe_selector_candidate_t *candidate =
                result->candidate_count > 0u
                    ? &result->candidates[result->candidate_count - 1u]
                    : NULL;
            if (!probe_data_write_candidate_type_matches(
                    repo, candidate, NULL)) {
                return probe_reject_type_mismatch(
                    result, NMO_PROBE_SELECTOR_MODE_AUTO);
            }
            result->selected_operation_id = selected_operation_id;
            result->safe_insertion.selected_operation_id =
                selected_operation_id;
            probe_apply_selected_link(result, selected_link_id, selected_link);
            probe_set_status(result,
                             NMO_PROBE_SELECTOR_MODE_AUTO,
                             NMO_PROBE_SELECTOR_STATUS_SELECTED,
                             NULL);
            return NMO_OK;
        }
        const nmo_behavior_state_t *writer =
            probe_behavior_state_by_id(repo, selected_id);
        nmo_probe_selector_candidate_t *candidate =
            result->candidate_count > 0u
                ? &result->candidates[result->candidate_count - 1u]
                : NULL;
        if (!probe_data_write_candidate_type_matches(repo, candidate, writer)) {
            return probe_reject_type_mismatch(
                result, NMO_PROBE_SELECTOR_MODE_AUTO);
        }
        result->selected_node_id = selected_id;
        result->safe_insertion.selected_node_id = selected_id;
        return probe_select_safe_link(
            repo,
            parent,
            writer,
            PROBE_LINK_TOUCH_ANY,
            result,
            NMO_PROBE_SELECTOR_MODE_AUTO,
            "unsafe_probe_insertion: debug probe automatic data write "
            "insertion is unsafe");
    }
    probe_set_status(result,
                     NMO_PROBE_SELECTOR_MODE_EXPLICIT_DATA_CELL,
                     NMO_PROBE_SELECTOR_STATUS_SELECTED,
                     NULL);
    return NMO_OK;
}

nmo_status_t nmo_probe_analyze_selector(
    nmo_workspace_t *workspace,
    const nmo_probe_selector_request_t *request,
    nmo_probe_selector_result_t *result)
{
    if (result != NULL) {
        nmo_probe_selector_result_init(result);
    }
    if (workspace == NULL || request == NULL || result == NULL ||
        request->behavior_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    result->has_delay = request->has_delay;
    result->delay = request->delay;

    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(workspace);
    const nmo_behavior_state_t *parent =
        probe_behavior_state_by_id(repo, request->behavior_id);
    if (parent == NULL) {
        return probe_reject(result,
                            NMO_PROBE_SELECTOR_MODE_UNSPECIFIED,
                            NMO_PROBE_SELECTOR_STATUS_NONE,
                            "behavior_not_found",
                            "debug probe behavior not found");
    }

    switch (request->kind) {
    case NMO_PROBE_SELECTOR_MESSAGE:
        return probe_analyze_message(ctx, repo, parent, request, result);
    case NMO_PROBE_SELECTOR_DATA_CELL_WRITE:
        return probe_analyze_data_cell(ctx, repo, parent, request, result);
    default:
        return probe_reject(result,
                            NMO_PROBE_SELECTOR_MODE_UNSPECIFIED,
                            NMO_PROBE_SELECTOR_STATUS_NONE,
                            "unsupported_probe_selector",
                            "unsupported probe selector kind");
    }
}
