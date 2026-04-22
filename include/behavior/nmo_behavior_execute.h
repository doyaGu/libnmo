#ifndef NMO_BEHAVIOR_EXECUTE_H
#define NMO_BEHAVIOR_EXECUTE_H

#include "behavior/nmo_script_executor.h"

typedef nmo_script_executor_options_t nmo_behavior_execute_options_t;
typedef nmo_script_executor_action_fn nmo_behavior_execute_action_fn;
typedef nmo_script_edit_report_t nmo_behavior_execute_result_t;

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_behavior_execute_options_t nmo_behavior_execute_options_default(void);

NMO_API nmo_status_t nmo_behavior_execute(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_behavior_execute_options_t *options,
    nmo_behavior_execute_action_fn action,
    void *user_data,
    nmo_behavior_execute_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_EXECUTE_H */
