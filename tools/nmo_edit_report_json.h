#ifndef NMO_EDIT_REPORT_JSON_H
#define NMO_EDIT_REPORT_JSON_H

#include "behavior/nmo_edit_plan.h"
#include "yyjson.h"

#include <stddef.h>

const char *nmo_cli_edit_report_op_kind_string(nmo_edit_op_kind_t kind);

void nmo_cli_edit_report_add_operations_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report);

void nmo_cli_edit_report_add_impact_array_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *name,
    const nmo_edit_object_impact_t *items,
    size_t count);

void nmo_cli_edit_report_add_semantic_risks_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report);

void nmo_cli_edit_report_add_validation_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report);

void nmo_cli_edit_report_add_diff_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report);

void nmo_cli_edit_report_add_diff_counts_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    size_t changed_object_count,
    size_t created_object_count,
    size_t deleted_object_count,
    size_t semantic_risk_count);

#endif /* NMO_EDIT_REPORT_JSON_H */
