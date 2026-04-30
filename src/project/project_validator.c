#include "project/nmo_project_validator.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_project_plan.h"

#include <stdlib.h>
#include <string.h>

static char *project_validation_strdup(const char *src)
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

static void project_validation_clear(nmo_project_validation_report_t *report)
{
    if (!report) {
        return;
    }

    for (size_t i = 0; i < report->issue_count; ++i) {
        free(report->issues[i].code);
        free(report->issues[i].message);
    }
    free(report->issues);
    report->issues = NULL;
    report->issue_count = 0u;
    report->issue_capacity = 0u;
    report->ok = false;
}

static nmo_status_t project_validation_add_issue(
    nmo_project_validation_report_t *report,
    const char *code,
    const char *message)
{
    if (!report || !code) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "validation report and issue code are required");
    }

    if (report->issue_count == report->issue_capacity) {
        size_t new_capacity = report->issue_capacity ? report->issue_capacity * 2u : 4u;
        nmo_project_validation_issue_t *new_issues =
            (nmo_project_validation_issue_t *)realloc(
                report->issues,
                new_capacity * sizeof(*new_issues));
        if (!new_issues) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "failed to allocate project validation issue");
        }
        memset(new_issues + report->issue_capacity,
               0,
               (new_capacity - report->issue_capacity) * sizeof(*new_issues));
        report->issues = new_issues;
        report->issue_capacity = new_capacity;
    }

    nmo_project_validation_issue_t *issue = &report->issues[report->issue_count];
    issue->code = project_validation_strdup(code);
    issue->message = project_validation_strdup(message ? message : code);
    if (!issue->code || !issue->message) {
        free(issue->code);
        free(issue->message);
        memset(issue, 0, sizeof(*issue));
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to copy project validation issue");
    }

    report->issue_count++;
    NMO_RETURN_OK();
}

void nmo_project_validation_report_init(nmo_project_validation_report_t *report)
{
    if (!report) {
        return;
    }
    memset(report, 0, sizeof(*report));
}

void nmo_project_validation_report_dispose(nmo_project_validation_report_t *report)
{
    if (!report) {
        return;
    }
    project_validation_clear(report);
}

bool nmo_project_validation_contains(
    const nmo_project_validation_report_t *report,
    const char *code)
{
    if (!report || !code) {
        return false;
    }

    for (size_t i = 0; i < report->issue_count; ++i) {
        if (report->issues[i].code && strcmp(report->issues[i].code, code) == 0) {
            return true;
        }
    }

    return false;
}

static bool project_validation_has_scene_handle(
    const nmo_project_plan_t *plan,
    uint32_t handle)
{
    size_t scene_count = nmo_project_plan_scene_count(plan);
    for (size_t i = 0; i < scene_count; ++i) {
        nmo_project_scene_desc_t scene = {0};
        if (nmo_project_plan_get_scene(plan, i, &scene) == NMO_OK &&
            scene.handle == handle) {
            return true;
        }
    }
    return false;
}

static bool project_validation_has_object_handle(
    const nmo_project_plan_t *plan,
    uint32_t handle)
{
    size_t object_count = nmo_project_plan_object_count(plan);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        if (nmo_project_plan_get_object(plan, i, &object) == NMO_OK &&
            object.handle == handle) {
            return true;
        }
    }
    return false;
}

static nmo_status_t project_validation_check_duplicate_scenes(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    size_t scene_count = nmo_project_plan_scene_count(plan);
    for (size_t i = 0; i < scene_count; ++i) {
        nmo_project_scene_desc_t lhs = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_scene(plan, i, &lhs));
        for (size_t j = i + 1u; j < scene_count; ++j) {
            nmo_project_scene_desc_t rhs = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_scene(plan, j, &rhs));
            if (lhs.handle == rhs.handle) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue(
                    report,
                    "duplicate_scene_handle",
                    "Project scene handles must be unique"));
            }
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t project_validation_check_objects(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    size_t object_count = nmo_project_plan_object_count(plan);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_project_object_desc_t object = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, i, &object));

        if (object.class_id == 0u ||
            object.class_id == NMO_CLASS_ID_INVALID ||
            object.class_id > NMO_CID_MAXCLASSID) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "invalid_object_class",
                "Project object class must be a valid CKObject class ID"));
        }
        if (object.scene_handle != 0u &&
            !project_validation_has_scene_handle(plan, object.scene_handle)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "missing_scene",
                "Project object references a missing scene handle"));
        }
        if (object.parent_handle != 0u &&
            !project_validation_has_object_handle(plan, object.parent_handle)) {
            NMO_RETURN_IF_ERROR(project_validation_add_issue(
                report,
                "missing_parent",
                "Project object references a missing parent object handle"));
        }

        for (size_t j = i + 1u; j < object_count; ++j) {
            nmo_project_object_desc_t other = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, j, &other));
            if (object.handle == other.handle) {
                NMO_RETURN_IF_ERROR(project_validation_add_issue(
                    report,
                    "duplicate_object_handle",
                    "Project object handles must be unique"));
            }
        }
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_validate_plan(
    const nmo_project_plan_t *plan,
    nmo_project_validation_report_t *report)
{
    if (!plan || !report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and validation report are required");
    }

    project_validation_clear(report);

    const char *document_name = nmo_project_plan_document_name(plan);
    if (!document_name || document_name[0] == '\0') {
        NMO_RETURN_IF_ERROR(project_validation_add_issue(
            report,
            "missing_document_name",
            "Project plan requires a document name"));
    }

    NMO_RETURN_IF_ERROR(project_validation_check_duplicate_scenes(plan, report));
    NMO_RETURN_IF_ERROR(project_validation_check_objects(plan, report));

    report->ok = report->issue_count == 0u;
    NMO_RETURN_OK();
}
