#ifndef NMO_BEHAVIOR_EXECUTE_H
#define NMO_BEHAVIOR_EXECUTE_H

#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_script_edit.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "lua/nmo_lua_runtime.h"
#include "runtime/nmo_workspace.h"

#include <stdbool.h>

#define NMO_BEHAVIOR_EXECUTE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BEHAVIOR_EXECUTE_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_behavior_execution nmo_behavior_execution_t;

typedef struct nmo_behavior_execute_options {
    const char *label;
    const nmo_load_options_t *load_options;
    const nmo_save_options_t *save_options;
    uint32_t validation_flags;
    bool dry_run;
} nmo_behavior_execute_options_t;

typedef nmo_status_t (*nmo_behavior_execute_action_fn)(
    nmo_behavior_execution_t *execution,
    void *user_data);

typedef nmo_script_edit_report_t nmo_behavior_execute_result_t;

NMO_API nmo_behavior_execute_options_t nmo_behavior_execute_options_default(void);

NMO_API nmo_context_t *nmo_behavior_execution_context(
    nmo_behavior_execution_t *execution);
NMO_API nmo_workspace_t *nmo_behavior_execution_workspace(
    nmo_behavior_execution_t *execution);
NMO_API nmo_lua_runtime_t *nmo_behavior_execution_lua_runtime(
    nmo_behavior_execution_t *execution);
NMO_API nmo_script_edit_tx_t *nmo_behavior_execution_transaction(
    nmo_behavior_execution_t *execution);

NMO_API nmo_status_t nmo_behavior_execute(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_behavior_execute_options_t *options,
    nmo_behavior_execute_action_fn action,
    void *user_data,
    nmo_behavior_execute_result_t *out_result);

NMO_API nmo_status_t nmo_behavior_execute_v2(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_behavior_execute_options_t *options,
    nmo_behavior_execute_action_fn action,
    void *user_data,
    nmo_edit_report_t *out_report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_EXECUTE_H */
