/**
 * @file nmo_edit_plan_json.h
 * @brief JSON serialization helpers for unified script edit plans.
 */

#ifndef NMO_EDIT_PLAN_JSON_H
#define NMO_EDIT_PLAN_JSON_H

#include "behavior/nmo_edit_plan.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_edit_plan_manifest_json_write(
    const nmo_edit_plan_t *plan,
    const char *input_path,
    const char *output_path,
    char **out_json);

NMO_API void nmo_edit_plan_manifest_json_free(char *json);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EDIT_PLAN_JSON_H */
