#include "project/nmo_project_executor.h"

#include "document/nmo_document.h"
#include "document/nmo_document_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "project_internal.h"
#include "project/nmo_project_plan.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

#include <stdlib.h>
#include <string.h>

static char *project_executor_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }

    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1u);
    return copy;
}

void nmo_project_report_init(nmo_project_report_t *report)
{
    if (!report) {
        return;
    }

    memset(report, 0, sizeof(*report));
    nmo_project_validation_report_init(&report->validation);
}

void nmo_project_report_dispose(nmo_project_report_t *report)
{
    if (!report) {
        return;
    }

    nmo_project_validation_report_dispose(&report->validation);
    free(report->output_path);
    memset(report, 0, sizeof(*report));
}

nmo_status_t nmo_project_executor_execute_to_file(
    const nmo_project_plan_t *plan,
    const char *output_path,
    nmo_project_report_t *report)
{
    if (!plan || !output_path || !report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan, output_path, and report are required");
    }

    report->ok = false;
    report->dry_run = false;
    nmo_project_validation_report_dispose(&report->validation);
    nmo_project_validation_report_init(&report->validation);
    free(report->output_path);
    report->output_path = project_executor_strdup(output_path);
    if (!report->output_path) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to copy project output path");
    }

    nmo_status_t status = nmo_project_validate_plan(plan, &report->validation);
    if (status != NMO_OK) {
        return status;
    }
    if (!report->validation.ok) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to create project execution context");
    }

    nmo_document_t *document = nmo_document_create(ctx);
    if (!document) {
        nmo_context_release(ctx);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to create generated document");
    }

    nmo_workspace_t *workspace = NULL;
    status = nmo_workspace_create(ctx, document, &workspace);
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    nmo_workspace_edit_t *edit = NULL;
    status = nmo_workspace_edit_begin(workspace, "project generation", &edit);
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    nmo_object_id_t root_id = 0;
    nmo_object_create_desc_t root = {
        .class_id = NMO_CID_OBJECT,
        .name = nmo_project_plan_document_name(plan),
        .type_guid = NMO_GUID_NULL,
    };
    status = nmo_object_edit_create(
        edit,
        &root,
        &root_id);
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_project_author_scenes(edit, plan);
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_workspace_edit_commit(edit);
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_document_save_file(document, output_path, NULL);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    if (status != NMO_OK) {
        return status;
    }

    report->ok = true;
    NMO_RETURN_OK();
}
