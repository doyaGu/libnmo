/**
 * @file nmo_script_executor.h
 * @brief Shared single-session script automation executor
 */

#ifndef NMO_SCRIPT_EXECUTOR_H
#define NMO_SCRIPT_EXECUTOR_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "behavior/nmo_script_edit.h"
#include "lua/nmo_lua_runtime.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_load_options nmo_load_options_t;
typedef struct nmo_save_options nmo_save_options_t;
typedef struct nmo_script_executor nmo_script_executor_t;

/*
 * Shared executor for Lua/CLI script automation. It owns one session, one Lua
 * runtime, and one script-edit transaction for the duration of a batch run.
 */
#define NMO_SCRIPT_EXECUTOR_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_EXECUTOR_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef struct nmo_script_executor_options {
    const char *label;
    const nmo_load_options_t *load_options;
    const nmo_save_options_t *save_options;
    uint32_t validation_flags;
    bool dry_run;
} nmo_script_executor_options_t;

typedef nmo_status_t (*nmo_script_executor_action_fn)(
    nmo_script_executor_t *executor,
    void *user_data);

NMO_API nmo_script_executor_options_t nmo_script_executor_options_default(void);

NMO_API nmo_context_t *nmo_script_executor_context(nmo_script_executor_t *executor);
NMO_API nmo_session_t *nmo_script_executor_session(nmo_script_executor_t *executor);
NMO_API nmo_lua_runtime_t *nmo_script_executor_lua_runtime(nmo_script_executor_t *executor);
NMO_API nmo_script_edit_tx_t *nmo_script_executor_transaction(nmo_script_executor_t *executor);

NMO_API nmo_status_t nmo_script_executor_execute(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_script_executor_options_t *options,
    nmo_script_executor_action_fn action,
    void *user_data,
    nmo_script_edit_report_t *out_report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_EXECUTOR_H */
