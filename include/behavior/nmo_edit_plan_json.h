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

typedef struct nmo_edit_plan_manifest {
    char *input_path;
    char *output_path;
    nmo_edit_plan_t *plan;
} nmo_edit_plan_manifest_t;

NMO_API nmo_status_t nmo_edit_plan_json_write(
    const nmo_edit_plan_t *plan,
    char **out_json);

NMO_API nmo_status_t nmo_edit_plan_json_read(
    const char *json,
    size_t json_len,
    nmo_edit_plan_t **out_plan);

NMO_API nmo_status_t nmo_edit_plan_manifest_json_write(
    const nmo_edit_plan_t *plan,
    const char *input_path,
    const char *output_path,
    char **out_json);

NMO_API nmo_status_t nmo_edit_plan_manifest_json_read(
    const char *json,
    size_t json_len,
    nmo_edit_plan_manifest_t *out_manifest);

NMO_API nmo_status_t nmo_edit_plan_manifest_json_read_file(
    const char *path,
    nmo_edit_plan_manifest_t *out_manifest);

NMO_API void nmo_edit_plan_manifest_dispose(
    nmo_edit_plan_manifest_t *manifest);

NMO_API void nmo_edit_plan_manifest_json_free(char *json);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EDIT_PLAN_JSON_H */
