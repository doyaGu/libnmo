#ifndef NMO_SCRIPT_AUTHORING_H
#define NMO_SCRIPT_AUTHORING_H

#include "project/nmo_project_plan.h"

#include <stddef.h>
#include <stdint.h>

#define NMO_SCRIPT_AUTHORING_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_AUTHORING_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_project_script_step_kind {
    NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT = 1,
    NMO_PROJECT_SCRIPT_STEP_ON_START_DEBUG_OUTPUT = 2
} nmo_project_script_step_kind_t;

typedef struct nmo_project_script_desc {
    uint32_t handle;
    uint32_t object_handle;
    const char *name;
    size_t step_count;
} nmo_project_script_desc_t;

typedef struct nmo_project_script_step_desc {
    nmo_project_script_step_kind_t kind;
    const char *message;
} nmo_project_script_step_desc_t;

NMO_API nmo_status_t nmo_project_plan_add_object_script(
    nmo_project_plan_t *plan,
    uint32_t object_handle,
    const char *name,
    uint32_t *out_script_handle);

NMO_API size_t nmo_project_plan_script_count(
    const nmo_project_plan_t *plan);

NMO_API nmo_status_t nmo_project_plan_get_script(
    const nmo_project_plan_t *plan,
    size_t index,
    nmo_project_script_desc_t *out_script);

NMO_API nmo_status_t nmo_project_plan_get_script_step(
    const nmo_project_plan_t *plan,
    uint32_t script_handle,
    size_t index,
    nmo_project_script_step_desc_t *out_step);

NMO_API nmo_status_t nmo_project_plan_script_add_debug_output(
    nmo_project_plan_t *plan,
    uint32_t script_handle,
    const char *message);

NMO_API nmo_status_t nmo_project_plan_script_add_on_start_debug_output(
    nmo_project_plan_t *plan,
    uint32_t script_handle,
    const char *message);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_AUTHORING_H */
