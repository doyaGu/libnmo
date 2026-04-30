#ifndef NMO_PROJECT_VALIDATOR_H
#define NMO_PROJECT_VALIDATOR_H

#include "core/nmo_error.h"
#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_project_plan nmo_project_plan_t;

typedef struct nmo_project_validation_issue {
    char *code;
    char *message;
} nmo_project_validation_issue_t;

typedef struct nmo_project_validation_report {
    bool ok;
    nmo_project_validation_issue_t *issues;
    size_t issue_count;
    size_t issue_capacity;
} nmo_project_validation_report_t;

NMO_API void nmo_project_validation_report_init(
    nmo_project_validation_report_t *report);

NMO_API void nmo_project_validation_report_dispose(
    nmo_project_validation_report_t *report);

NMO_API bool nmo_project_validation_contains(
    const nmo_project_validation_report_t *report,
    const char *code);

NMO_API nmo_status_t nmo_project_validate_plan(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PROJECT_VALIDATOR_H */
