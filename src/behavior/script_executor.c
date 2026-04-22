#include "behavior/nmo_script_executor.h"
#include "behavior/nmo_behavior_execute.h"

#include "app/nmo_save.h"
#include "lua/nmo_lua_bindings.h"
#include "session/nmo_session.h"

#include <stdlib.h>
#include <string.h>

struct nmo_script_executor {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_lua_runtime_t *runtime;
    nmo_script_edit_tx_t *tx;
    nmo_script_executor_options_t options;
};

static void nmo_script_executor_clear_report(nmo_script_edit_report_t *report)
{
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
    }
}

static void nmo_script_executor_copy_report(const nmo_script_executor_t *executor,
                                            nmo_script_edit_report_t *out_report)
{
    const nmo_script_edit_report_t *report = NULL;

    if (executor == NULL || executor->tx == NULL || out_report == NULL) {
        return;
    }

    report = nmo_script_edit_report(executor->tx);
    if (report != NULL) {
        *out_report = *report;
    }
}

static void nmo_script_executor_destroy(nmo_script_executor_t *executor)
{
    if (executor == NULL) {
        return;
    }

    if (executor->tx != NULL) {
        nmo_script_edit_rollback(executor->tx);
        executor->tx = NULL;
    }
    if (executor->runtime != NULL) {
        nmo_lua_runtime_destroy(executor->runtime);
        executor->runtime = NULL;
    }
    if (executor->session != NULL) {
        nmo_session_destroy(executor->session);
        executor->session = NULL;
    }

    free(executor);
}

static nmo_status_t nmo_script_executor_validate(
    nmo_script_executor_t *executor,
    uint32_t validation_flags)
{
    static const uint32_t ordered_flags[] = {
        NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY,
        NMO_SCRIPT_EDIT_VALIDATE_REFERENCES,
        NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX,
        NMO_SCRIPT_EDIT_VALIDATE_INTERFACE
    };
    size_t i = 0;

    if (executor == NULL || executor->tx == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Script executor has no active transaction");
    }

    for (i = 0; i < sizeof(ordered_flags) / sizeof(ordered_flags[0]); ++i) {
        if ((validation_flags & ordered_flags[i]) == 0u) {
            continue;
        }

        nmo_status_t status = nmo_script_edit_validate(executor->tx, ordered_flags[i]);
        if (status != NMO_OK) {
            return status;
        }
    }

    NMO_RETURN_OK();
}

NMO_API nmo_script_executor_options_t nmo_script_executor_options_default(void)
{
    nmo_script_executor_options_t options;
    memset(&options, 0, sizeof(options));
    options.label = "script-executor";
    options.validation_flags =
        NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
        NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX |
        NMO_SCRIPT_EDIT_VALIDATE_INTERFACE |
        NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY;
    return options;
}

NMO_API nmo_behavior_execute_options_t nmo_behavior_execute_options_default(void)
{
    return nmo_script_executor_options_default();
}

NMO_API nmo_context_t *nmo_script_executor_context(nmo_script_executor_t *executor)
{
    return executor != NULL ? executor->ctx : NULL;
}

NMO_API nmo_session_t *nmo_script_executor_session(nmo_script_executor_t *executor)
{
    return executor != NULL ? executor->session : NULL;
}

NMO_API nmo_lua_runtime_t *nmo_script_executor_lua_runtime(nmo_script_executor_t *executor)
{
    return executor != NULL ? executor->runtime : NULL;
}

NMO_API nmo_script_edit_tx_t *nmo_script_executor_transaction(nmo_script_executor_t *executor)
{
    return executor != NULL ? executor->tx : NULL;
}

NMO_API nmo_status_t nmo_script_executor_execute(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_script_executor_options_t *options,
    nmo_script_executor_action_fn action,
    void *user_data,
    nmo_script_edit_report_t *out_report)
{
    nmo_script_executor_t *executor = NULL;
    nmo_script_executor_options_t resolved_options =
        nmo_script_executor_options_default();
    nmo_status_t status = NMO_OK;

    nmo_script_executor_clear_report(out_report);

    if (ctx == NULL || input_path == NULL || action == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Script executor requires context, input path, and action");
    }
    if (options != NULL) {
        resolved_options = *options;
        if (resolved_options.label == NULL) {
            resolved_options.label = "script-executor";
        }
    }
    if (!resolved_options.dry_run && output_path == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Script executor requires output_path unless dry_run is enabled");
    }

    executor = (nmo_script_executor_t *)calloc(1, sizeof(*executor));
    if (executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate script executor");
    }

    executor->ctx = ctx;
    executor->options = resolved_options;

    executor->session = nmo_session_create(ctx);
    if (executor->session == NULL) {
        nmo_script_executor_destroy(executor);
        return NMO_ERR_NOMEM;
    }

    status = nmo_session_load_file(executor->session,
                                   input_path,
                                   resolved_options.load_options,
                                   NULL);
    if (status != NMO_OK) {
        nmo_script_executor_destroy(executor);
        return status;
    }

    status = nmo_session_ensure_behavior_acceleration(executor->session);
    if (status != NMO_OK) {
        nmo_script_executor_destroy(executor);
        return status;
    }

    executor->runtime = nmo_lua_runtime_create();
    if (executor->runtime == NULL) {
        nmo_script_executor_destroy(executor);
        return NMO_ERR_NOMEM;
    }

    status = nmo_lua_register_platform_bindings(executor->runtime);
    if (status != NMO_OK) {
        nmo_script_executor_destroy(executor);
        return status;
    }

    status = nmo_script_edit_begin(ctx,
                                   executor->session,
                                   resolved_options.label,
                                   &executor->tx);
    if (status != NMO_OK) {
        nmo_script_executor_destroy(executor);
        return status;
    }

    status = action(executor, user_data);
    if (status != NMO_OK) {
        nmo_script_executor_destroy(executor);
        return status;
    }

    status = nmo_script_executor_validate(executor, resolved_options.validation_flags);
    if (status != NMO_OK) {
        nmo_script_executor_destroy(executor);
        return status;
    }

    nmo_script_executor_copy_report(executor, out_report);

    if (resolved_options.dry_run) {
        nmo_script_edit_rollback(executor->tx);
        executor->tx = NULL;
        nmo_script_executor_destroy(executor);
        return NMO_OK;
    }

    status = nmo_script_edit_commit(executor->tx);
    executor->tx = NULL;
    if (status != NMO_OK) {
        nmo_script_executor_destroy(executor);
        return status;
    }

    status = nmo_save_file(executor->session,
                           output_path,
                           resolved_options.save_options);
    nmo_script_executor_destroy(executor);
    return status;
}

NMO_API nmo_status_t nmo_behavior_execute(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_behavior_execute_options_t *options,
    nmo_behavior_execute_action_fn action,
    void *user_data,
    nmo_behavior_execute_result_t *out_result)
{
    return nmo_script_executor_execute(ctx,
                                       input_path,
                                       output_path,
                                       options,
                                       action,
                                       user_data,
                                       out_result);
}
