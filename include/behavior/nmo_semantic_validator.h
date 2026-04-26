/**
 * @file nmo_semantic_validator.h
 * @brief Shared semantic risk collection for Virtools script edits.
 */

#ifndef NMO_SEMANTIC_VALIDATOR_H
#define NMO_SEMANTIC_VALIDATOR_H

#include "behavior/nmo_behavior_analyze.h"
#include "runtime/nmo_workspace.h"

#include <stddef.h>

#define NMO_SEMANTIC_VALIDATOR_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SEMANTIC_VALIDATOR_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_edit_plan nmo_edit_plan_t;

typedef enum nmo_behavior_semantic_risk_severity {
    NMO_BEHAVIOR_SEMANTIC_RISK_SAFE = 0,
    NMO_BEHAVIOR_SEMANTIC_RISK_WARN = 1,
    NMO_BEHAVIOR_SEMANTIC_RISK_REJECT = 2,
} nmo_behavior_semantic_risk_severity_t;

typedef struct nmo_behavior_semantic_risk {
    nmo_behavior_semantic_risk_severity_t severity;
    const char *code;
    const char *message;
    nmo_object_id_t object_id;
} nmo_behavior_semantic_risk_t;

NMO_API nmo_status_t nmo_semantic_validate_boundary(
    nmo_workspace_t *workspace,
    const nmo_behavior_boundary_t *boundary,
    const nmo_object_id_t *node_ids,
    size_t node_count,
    nmo_behavior_semantic_risk_t **out_risks,
    size_t *out_risk_count);

NMO_API nmo_status_t nmo_semantic_validate_edit_plan(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    nmo_behavior_semantic_risk_t **out_risks,
    size_t *out_risk_count);

NMO_API void nmo_semantic_risks_free(
    nmo_behavior_semantic_risk_t *risks);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SEMANTIC_VALIDATOR_H */
