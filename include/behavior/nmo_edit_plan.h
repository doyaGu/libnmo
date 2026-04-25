/**
 * @file nmo_edit_plan.h
 * @brief Unified script edit operation plan and transaction executor.
 */

#ifndef NMO_EDIT_PLAN_H
#define NMO_EDIT_PLAN_H

#include "object/nmo_object_edit.h"
#include "runtime/nmo_workspace.h"
#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_edit_plan nmo_edit_plan_t;

typedef enum nmo_edit_op_kind {
    NMO_EDIT_OP_SET_PARAMETER_VALUE = 1,
    NMO_EDIT_OP_SET_PARAMETER_BYTES = 2
} nmo_edit_op_kind_t;

typedef struct nmo_edit_op {
    nmo_edit_op_kind_t kind;
    nmo_object_id_t primary_id;
    union {
        struct {
            const char *value;
            nmo_parameter_write_options_t options;
            bool has_options;
        } set_value;
        struct {
            const uint8_t *bytes;
            size_t byte_count;
            nmo_parameter_write_options_t options;
            bool has_options;
        } set_bytes;
    } data;
} nmo_edit_op_t;

typedef struct nmo_edit_executor_options {
    bool dry_run;
} nmo_edit_executor_options_t;

typedef struct nmo_edit_changed_object {
    nmo_object_id_t id;
    nmo_edit_op_kind_t cause;
} nmo_edit_changed_object_t;

typedef struct nmo_edit_operation_result {
    nmo_edit_op_kind_t kind;
    nmo_object_id_t primary_id;
    nmo_status_t status;
} nmo_edit_operation_result_t;

typedef struct nmo_edit_report {
    bool ok;
    bool dry_run;
    nmo_status_t status;
    size_t operation_count;
    nmo_edit_operation_result_t *operations;
    size_t changed_object_count;
    nmo_edit_changed_object_t *changed_objects;
} nmo_edit_report_t;

NMO_API nmo_status_t nmo_edit_plan_create(nmo_edit_plan_t **out_plan);
NMO_API void nmo_edit_plan_destroy(nmo_edit_plan_t *plan);
NMO_API size_t nmo_edit_plan_count(const nmo_edit_plan_t *plan);
NMO_API const nmo_edit_op_t *nmo_edit_plan_get(const nmo_edit_plan_t *plan, size_t index);

NMO_API nmo_status_t nmo_edit_plan_add_set_parameter_value(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options);

NMO_API nmo_status_t nmo_edit_plan_add_set_parameter_bytes(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options);

NMO_API nmo_edit_executor_options_t nmo_edit_executor_options_default(void);

NMO_API nmo_status_t nmo_edit_report_init(nmo_edit_report_t *report);
NMO_API void nmo_edit_report_dispose(nmo_edit_report_t *report);

NMO_API nmo_status_t nmo_edit_executor_execute(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EDIT_PLAN_H */
