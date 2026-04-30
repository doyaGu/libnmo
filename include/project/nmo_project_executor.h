#ifndef NMO_PROJECT_EXECUTOR_H
#define NMO_PROJECT_EXECUTOR_H

#include "core/nmo_error.h"
#include "nmo_types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_project_plan nmo_project_plan_t;

typedef struct nmo_project_report {
    bool ok;
    bool dry_run;
    char *output_path;
} nmo_project_report_t;

NMO_API void nmo_project_report_init(nmo_project_report_t *report);
NMO_API void nmo_project_report_dispose(nmo_project_report_t *report);

NMO_API nmo_status_t nmo_project_executor_execute_to_file(
    const nmo_project_plan_t *plan,
    const char *output_path,
    nmo_project_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PROJECT_EXECUTOR_H */
