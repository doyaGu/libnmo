#include "behavior/nmo_behavior_execute.h"

#include "lua/nmo_lua_bindings.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

struct nmo_behavior_execution {
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_lua_runtime_t *runtime;
    nmo_script_edit_tx_t *tx;
    nmo_behavior_execute_options_t options;
};

static nmo_status_t nmo_behavior_execute_internal(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_behavior_execute_options_t *options,
    nmo_behavior_execute_action_fn action,
    void *user_data,
    nmo_edit_report_t *out_report);

static nmo_status_t behavior_execute_copy_report(
    const nmo_behavior_execution_t *execution,
    const char *output_path,
    nmo_status_t status,
    nmo_edit_report_t *out_report)
{
    const nmo_script_edit_report_t *legacy = NULL;

    if (out_report == NULL) {
        return NMO_OK;
    }

    out_report->ok = status == NMO_OK;
    out_report->status = status;
    out_report->dry_run = execution != NULL && execution->options.dry_run;
    out_report->validation.final_status = status;
    if (status == NMO_OK && output_path != NULL && !out_report->dry_run) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_set_output_path(
            out_report, output_path));
    }

    if (execution == NULL || execution->tx == NULL) {
        return NMO_OK;
    }

    legacy = nmo_script_edit_report(execution->tx);
    if (legacy == NULL) {
        return NMO_OK;
    }

    for (size_t i = 0; i < legacy->created_object_id_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_created_object(
            out_report,
            legacy->created_object_ids[i],
            0,
            "created"));
    }
    for (size_t i = 0; i < legacy->changed_object_id_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            out_report,
            legacy->changed_object_ids[i],
            0,
            "changed"));
    }
    return NMO_OK;
}

static void behavior_execute_destroy(nmo_behavior_execution_t *execution)
{
    if (execution == NULL) {
        return;
    }

    if (execution->tx != NULL) {
        nmo_script_edit_rollback(execution->tx);
        execution->tx = NULL;
    }
    if (execution->runtime != NULL) {
        nmo_lua_runtime_destroy(execution->runtime);
        execution->runtime = NULL;
    }
    if (execution->workspace != NULL) {
        nmo_workspace_destroy(execution->workspace);
        execution->workspace = NULL;
    }
    if (execution->document != NULL) {
        nmo_document_destroy(execution->document);
        execution->document = NULL;
    }

    free(execution);
}

static nmo_status_t behavior_execute_validate(
    nmo_behavior_execution_t *execution,
    uint32_t validation_flags)
{
    static const uint32_t ordered_flags[] = {
        NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY,
        NMO_SCRIPT_EDIT_VALIDATE_REFERENCES,
        NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX,
        NMO_SCRIPT_EDIT_VALIDATE_INTERFACE
    };

    if (execution == NULL || execution->tx == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Behavior execution has no active transaction");
    }

    for (size_t i = 0; i < sizeof(ordered_flags) / sizeof(ordered_flags[0]); ++i) {
        if ((validation_flags & ordered_flags[i]) == 0u) {
            continue;
        }

        nmo_status_t status = nmo_script_edit_validate(execution->tx, ordered_flags[i]);
        if (status != NMO_OK) {
            return status;
        }
    }

    NMO_RETURN_OK();
}

NMO_API nmo_behavior_execute_options_t nmo_behavior_execute_options_default(void)
{
    nmo_behavior_execute_options_t options;
    memset(&options, 0, sizeof(options));
    options.label = "behavior-execute";
    options.validation_flags =
        NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
        NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX |
        NMO_SCRIPT_EDIT_VALIDATE_INTERFACE |
        NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY;
    return options;
}

NMO_API nmo_context_t *nmo_behavior_execution_context(
    nmo_behavior_execution_t *execution)
{
    return execution != NULL ? execution->ctx : NULL;
}

NMO_API nmo_workspace_t *nmo_behavior_execution_workspace(
    nmo_behavior_execution_t *execution)
{
    return execution != NULL ? execution->workspace : NULL;
}

NMO_API nmo_lua_runtime_t *nmo_behavior_execution_lua_runtime(
    nmo_behavior_execution_t *execution)
{
    return execution != NULL ? execution->runtime : NULL;
}

NMO_API nmo_script_edit_tx_t *nmo_behavior_execution_transaction(
    nmo_behavior_execution_t *execution)
{
    return execution != NULL ? execution->tx : NULL;
}

NMO_API nmo_status_t nmo_behavior_execute(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_behavior_execute_options_t *options,
    nmo_behavior_execute_action_fn action,
    void *user_data,
    nmo_edit_report_t *out_report)
{
    return nmo_behavior_execute_internal(
        ctx, input_path, output_path, options, action, user_data, out_report);
}

static nmo_status_t nmo_behavior_execute_internal(
    nmo_context_t *ctx,
    const char *input_path,
    const char *output_path,
    const nmo_behavior_execute_options_t *options,
    nmo_behavior_execute_action_fn action,
    void *user_data,
    nmo_edit_report_t *out_report)
{
    nmo_behavior_execution_t *execution = NULL;
    nmo_behavior_execute_options_t resolved_options =
        nmo_behavior_execute_options_default();
    nmo_status_t status = NMO_OK;

    if (out_report != NULL) {
        nmo_edit_report_dispose(out_report);
        NMO_RETURN_IF_ERROR(nmo_edit_report_init(out_report));
    }

    if (ctx == NULL || input_path == NULL || action == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Behavior execute requires context, input path, and action");
    }
    if (options != NULL) {
        resolved_options = *options;
        if (resolved_options.label == NULL) {
            resolved_options.label = "behavior-execute";
        }
    }
    if (!resolved_options.dry_run && output_path == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Behavior execute requires output_path unless dry_run is enabled");
    }

    execution = (nmo_behavior_execution_t *)calloc(1, sizeof(*execution));
    if (execution == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate behavior execution");
    }

    execution->ctx = ctx;
    execution->options = resolved_options;

    execution->document = nmo_document_create(ctx);
    if (execution->document == NULL) {
        behavior_execute_destroy(execution);
        return NMO_ERR_NOMEM;
    }

    status = nmo_document_internal_load_file(
        execution->document, input_path, resolved_options.load_options);
    if (status != NMO_OK) {
        behavior_execute_destroy(execution);
        return status;
    }

    status = nmo_workspace_create(ctx, execution->document, &execution->workspace);
    if (status != NMO_OK) {
        behavior_execute_destroy(execution);
        return status;
    }

    status = nmo_workspace_internal_ensure_behavior_acceleration(execution->workspace);
    if (status != NMO_OK) {
        behavior_execute_destroy(execution);
        return status;
    }

    execution->runtime = nmo_lua_runtime_create();
    if (execution->runtime == NULL) {
        behavior_execute_destroy(execution);
        return NMO_ERR_NOMEM;
    }

    status = nmo_lua_register_platform_bindings(execution->runtime);
    if (status != NMO_OK) {
        behavior_execute_destroy(execution);
        return status;
    }

    status = nmo_script_edit_begin(
        execution->workspace, resolved_options.label, &execution->tx);
    if (status != NMO_OK) {
        behavior_execute_destroy(execution);
        return status;
    }

    status = action(execution, user_data);
    if (status != NMO_OK) {
        (void)behavior_execute_copy_report(
            execution, output_path, status, out_report);
        behavior_execute_destroy(execution);
        return status;
    }

    status = behavior_execute_validate(execution, resolved_options.validation_flags);
    if (status != NMO_OK) {
        (void)behavior_execute_copy_report(
            execution, output_path, status, out_report);
        behavior_execute_destroy(execution);
        return status;
    }

    status = behavior_execute_copy_report(
        execution, output_path, NMO_OK, out_report);
    if (status != NMO_OK) {
        behavior_execute_destroy(execution);
        return status;
    }

    if (resolved_options.dry_run) {
        nmo_script_edit_rollback(execution->tx);
        execution->tx = NULL;
        behavior_execute_destroy(execution);
        return NMO_OK;
    }

    status = nmo_script_edit_commit(execution->tx);
    execution->tx = NULL;
    if (status != NMO_OK) {
        behavior_execute_destroy(execution);
        return status;
    }

    status = nmo_document_save_file(
        execution->document, output_path, resolved_options.save_options);
    behavior_execute_destroy(execution);
    return status;
}
