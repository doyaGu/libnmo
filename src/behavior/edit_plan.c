/**
 * @file edit_plan.c
 * @brief Unified edit plan storage and transaction executor.
 */

#include "behavior/nmo_edit_plan.h"

#include "object/nmo_value_writer.h"

#include <stdlib.h>
#include <string.h>

struct nmo_edit_plan {
    nmo_edit_op_t *ops;
    size_t count;
    size_t capacity;
};

static char *edit_plan_strdup(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    size_t len = strlen(text) + 1u;
    char *copy = (char *)malloc(len);
    if (copy != NULL) {
        memcpy(copy, text, len);
    }
    return copy;
}

static nmo_status_t edit_plan_reserve(nmo_edit_plan_t *plan, size_t needed)
{
    if (plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (needed <= plan->capacity) {
        return NMO_OK;
    }
    size_t new_capacity = plan->capacity == 0 ? 8u : plan->capacity * 2u;
    while (new_capacity < needed) {
        new_capacity *= 2u;
    }
    nmo_edit_op_t *new_ops =
        (nmo_edit_op_t *)realloc(plan->ops, new_capacity * sizeof(*new_ops));
    if (new_ops == NULL) {
        return NMO_ERR_NOMEM;
    }
    plan->ops = new_ops;
    plan->capacity = new_capacity;
    return NMO_OK;
}

static void edit_op_dispose(nmo_edit_op_t *op)
{
    if (op == NULL) {
        return;
    }
    if (op->kind == NMO_EDIT_OP_SET_PARAMETER_VALUE) {
        free((void *)op->data.set_value.value);
    } else if (op->kind == NMO_EDIT_OP_SET_PARAMETER_BYTES) {
        free((void *)op->data.set_bytes.bytes);
    }
    memset(op, 0, sizeof(*op));
}

nmo_status_t nmo_edit_plan_create(nmo_edit_plan_t **out_plan)
{
    if (out_plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_plan = (nmo_edit_plan_t *)calloc(1, sizeof(nmo_edit_plan_t));
    return *out_plan != NULL ? NMO_OK : NMO_ERR_NOMEM;
}

void nmo_edit_plan_destroy(nmo_edit_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    for (size_t i = 0; i < plan->count; i++) {
        edit_op_dispose(&plan->ops[i]);
    }
    free(plan->ops);
    free(plan);
}

size_t nmo_edit_plan_count(const nmo_edit_plan_t *plan)
{
    return plan != NULL ? plan->count : 0u;
}

const nmo_edit_op_t *nmo_edit_plan_get(const nmo_edit_plan_t *plan, size_t index)
{
    if (plan == NULL || index >= plan->count) {
        return NULL;
    }
    return &plan->ops[index];
}

nmo_status_t nmo_edit_plan_add_set_parameter_value(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    if (plan == NULL || parameter_id == 0 || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = NMO_EDIT_OP_SET_PARAMETER_VALUE;
    op->primary_id = parameter_id;
    op->data.set_value.value = edit_plan_strdup(value_str);
    if (op->data.set_value.value == NULL) {
        return NMO_ERR_NOMEM;
    }
    if (options != NULL) {
        op->data.set_value.options = *options;
        op->data.set_value.has_options = true;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_set_parameter_bytes(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    if (plan == NULL || parameter_id == 0 || (bytes == NULL && byte_count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = NMO_EDIT_OP_SET_PARAMETER_BYTES;
    op->primary_id = parameter_id;
    if (byte_count > 0) {
        uint8_t *copy = (uint8_t *)malloc(byte_count);
        if (copy == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(copy, bytes, byte_count);
        op->data.set_bytes.bytes = copy;
    }
    op->data.set_bytes.byte_count = byte_count;
    if (options != NULL) {
        op->data.set_bytes.options = *options;
        op->data.set_bytes.has_options = true;
    }
    plan->count++;
    return NMO_OK;
}

nmo_edit_executor_options_t nmo_edit_executor_options_default(void)
{
    nmo_edit_executor_options_t options = {0};
    return options;
}

nmo_status_t nmo_edit_report_init(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    return NMO_OK;
}

void nmo_edit_report_dispose(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return;
    }
    free(report->operations);
    free(report->changed_objects);
    memset(report, 0, sizeof(*report));
}

static nmo_status_t edit_report_prepare(nmo_edit_report_t *report,
                                        const nmo_edit_plan_t *plan,
                                        bool dry_run)
{
    if (report == NULL || plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_edit_report_dispose(report);
    report->dry_run = dry_run;
    report->operation_count = plan->count;
    if (plan->count == 0) {
        return NMO_OK;
    }
    report->operations =
        (nmo_edit_operation_result_t *)calloc(plan->count, sizeof(*report->operations));
    report->changed_objects =
        (nmo_edit_changed_object_t *)calloc(plan->count, sizeof(*report->changed_objects));
    if (report->operations == NULL || report->changed_objects == NULL) {
        nmo_edit_report_dispose(report);
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

static bool edit_report_has_changed_object(
    const nmo_edit_report_t *report,
    nmo_object_id_t id)
{
    for (size_t i = 0; i < report->changed_object_count; i++) {
        if (report->changed_objects[i].id == id) {
            return true;
        }
    }
    return false;
}

static void edit_report_note_changed_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause)
{
    if (report == NULL || report->changed_objects == NULL || id == 0 ||
        edit_report_has_changed_object(report, id)) {
        return;
    }
    report->changed_objects[report->changed_object_count++] =
        (nmo_edit_changed_object_t){.id = id, .cause = cause};
}

static nmo_status_t edit_executor_apply_op(
    nmo_workspace_edit_t *edit,
    const nmo_edit_op_t *op)
{
    if (edit == NULL || op == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        return nmo_value_writer_set_parameter_value(
            edit,
            op->primary_id,
            op->data.set_value.value,
            op->data.set_value.has_options ? &op->data.set_value.options : NULL);
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        return nmo_value_writer_set_parameter_bytes(
            edit,
            op->primary_id,
            op->data.set_bytes.bytes,
            op->data.set_bytes.byte_count,
            op->data.set_bytes.has_options ? &op->data.set_bytes.options : NULL);
    default:
        return NMO_ERR_NOT_SUPPORTED;
    }
}

nmo_status_t nmo_edit_executor_execute(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report)
{
    if (workspace == NULL || plan == NULL || report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_edit_executor_options_t effective =
        options != NULL ? *options : nmo_edit_executor_options_default();
    NMO_RETURN_IF_ERROR(edit_report_prepare(report, plan, effective.dry_run));

    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t rc = nmo_workspace_edit_begin(workspace, "edit plan", &edit);
    if (rc != NMO_OK) {
        report->status = rc;
        return rc;
    }

    for (size_t i = 0; i < plan->count; i++) {
        const nmo_edit_op_t *op = &plan->ops[i];
        nmo_status_t op_rc = edit_executor_apply_op(edit, op);
        report->operations[i] = (nmo_edit_operation_result_t){
            .kind = op->kind,
            .primary_id = op->primary_id,
            .status = op_rc,
        };
        if (op_rc != NMO_OK) {
            nmo_workspace_edit_rollback(edit);
            report->ok = false;
            report->status = op_rc;
            return op_rc;
        }
        edit_report_note_changed_object(report, op->primary_id, op->kind);
    }

    if (effective.dry_run) {
        nmo_workspace_edit_rollback(edit);
        report->ok = true;
        report->status = NMO_OK;
        return NMO_OK;
    }

    rc = nmo_workspace_edit_commit(edit);
    report->ok = rc == NMO_OK;
    report->status = rc;
    return rc;
}
