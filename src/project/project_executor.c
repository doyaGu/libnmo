#include "project/nmo_project_executor.h"

#include "document/nmo_document.h"
#include "document/nmo_document_save.h"
#include "object/nmo_class_ids.h"
#include "project_internal.h"
#include "project/nmo_project_plan.h"
#include "runtime/nmo_context.h"
#include "../runtime/runtime_internal.h"

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

    nmo_object_id_t root_id = 0;
    status = nmo_document_internal_create_object(
        document,
        NMO_CID_OBJECT,
        nmo_project_plan_document_name(plan),
        (nmo_guid_t){0, 0},
        &root_id);
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_project_author_scenes(document, plan);
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_document_save_file(document, output_path, NULL);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    if (status != NMO_OK) {
        return status;
    }

    report->ok = true;
    NMO_RETURN_OK();
}
