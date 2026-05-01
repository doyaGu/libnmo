#include "project/nmo_project_executor.h"

#include "document/nmo_document.h"
#include "document/nmo_document_file_state.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "project/nmo_asset_plan.h"
#include "project_internal.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_script_authoring.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

#include <stdio.h>
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

static void project_report_name_list_dispose(nmo_project_report_name_list_t *list)
{
    if (!list) {
        return;
    }
    for (size_t i = 0u; i < list->count; ++i) {
        free(list->names[i]);
    }
    free(list->names);
    memset(list, 0, sizeof(*list));
}

static void project_report_diff_dispose(nmo_project_report_diff_t *diff)
{
    if (!diff) {
        return;
    }
    project_report_name_list_dispose(&diff->created);
}

static void project_report_dispose_diffs(nmo_project_report_t *report)
{
    if (!report) {
        return;
    }
    project_report_diff_dispose(&report->document_diff);
    project_report_diff_dispose(&report->scene_diff);
    project_report_diff_dispose(&report->object_diff);
    project_report_diff_dispose(&report->asset_diff);
    project_report_diff_dispose(&report->script_diff);
    project_report_diff_dispose(&report->manager_diff);
}

static nmo_status_t project_report_add_created(
    nmo_project_report_diff_t *diff,
    const char *name)
{
    if (!diff || !name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "report diff and created name are required");
    }

    char *copy = project_executor_strdup(name);
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project report name");
    }

    size_t next_count = diff->created.count + 1u;
    char **next_names =
        (char **)realloc(diff->created.names, next_count * sizeof(*next_names));
    if (!next_names) {
        free(copy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project report names");
    }

    diff->created.names = next_names;
    diff->created.names[diff->created.count] = copy;
    diff->created.count = next_count;
    NMO_RETURN_OK();
}

static bool project_report_created_contains(
    const nmo_project_report_diff_t *diff,
    const char *name)
{
    if (!diff || !name) {
        return false;
    }
    for (size_t i = 0u; i < diff->created.count; ++i) {
        if (diff->created.names[i] &&
            strcmp(diff->created.names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static nmo_status_t project_report_reset_for_execute(
    nmo_project_report_t *report)
{
    if (!report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "project report is required");
    }

    report->ok = false;
    report->dry_run = false;
    project_report_dispose_diffs(report);
    nmo_project_validation_report_dispose(&report->validation);
    nmo_project_validation_report_init(&report->validation);
    free(report->output_path);
    report->output_path = NULL;
    NMO_RETURN_OK();
}

static nmo_status_t project_report_populate_diff(
    nmo_project_report_t *report,
    const nmo_project_plan_t *plan)
{
    const char *document_name = nmo_project_plan_document_name(plan);
    if (document_name) {
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->document_diff,
            document_name));
    }

    for (size_t i = 0u; i < nmo_project_plan_scene_count(plan); ++i) {
        nmo_project_scene_desc_t scene = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_scene(plan, i, &scene));
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->scene_diff,
            scene.name));
    }

    for (size_t i = 0u; i < nmo_project_plan_object_count(plan); ++i) {
        nmo_project_object_desc_t object = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, i, &object));
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->object_diff,
            object.name));
    }

    for (size_t i = 0u; i < nmo_project_plan_asset_count(plan); ++i) {
        nmo_project_asset_desc_t asset = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_asset(plan, i, &asset));

        nmo_project_object_desc_t object = {0};
        bool found = false;
        for (size_t j = 0u; j < nmo_project_plan_object_count(plan); ++j) {
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_object(plan, j, &object));
            if (object.handle == asset.object_handle) {
                found = true;
                break;
            }
        }
        if (!found || !object.name) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "asset target object handle not found");
        }

        char asset_name[256];
        if (asset.has_primitive_mesh || asset.has_external_mesh) {
            int len = snprintf(asset_name, sizeof(asset_name), "%s_Mesh", object.name);
            if (len < 0 || (size_t)len >= sizeof(asset_name)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "generated mesh asset name is too long");
            }
            NMO_RETURN_IF_ERROR(project_report_add_created(
                &report->asset_diff,
                asset_name));
        }
        if (asset.has_material_color || asset.has_material_texture) {
            int len = snprintf(asset_name, sizeof(asset_name), "%s_Material", object.name);
            if (len < 0 || (size_t)len >= sizeof(asset_name)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "generated material asset name is too long");
            }
            NMO_RETURN_IF_ERROR(project_report_add_created(
                &report->asset_diff,
                asset_name));
        }
        if (asset.has_material_texture) {
            int len = snprintf(asset_name, sizeof(asset_name), "%s_Texture", object.name);
            if (len < 0 || (size_t)len >= sizeof(asset_name)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "generated texture asset name is too long");
            }
            NMO_RETURN_IF_ERROR(project_report_add_created(
                &report->asset_diff,
                asset_name));
        }
        size_t obj_material_count =
            nmo_project_plan_obj_material_count(plan, asset.object_handle);
        for (size_t material_index = 0u;
             material_index < obj_material_count;
             ++material_index) {
            nmo_project_material_spec_t material = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_obj_material(
                plan,
                asset.object_handle,
                material_index,
                &material));
            int len = snprintf(
                asset_name,
                sizeof(asset_name),
                "%s_%s_Material",
                object.name,
                material.obj_material_name);
            if (len < 0 || (size_t)len >= sizeof(asset_name)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "generated OBJ material asset name is too long");
            }
            NMO_RETURN_IF_ERROR(project_report_add_created(
                &report->asset_diff,
                asset_name));
            if (material.has_texture) {
                len = snprintf(
                    asset_name,
                    sizeof(asset_name),
                    "%s_%s_Texture",
                    object.name,
                    material.obj_material_name);
                if (len < 0 || (size_t)len >= sizeof(asset_name)) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                     "generated OBJ material texture asset name is too long");
                }
                NMO_RETURN_IF_ERROR(project_report_add_created(
                    &report->asset_diff,
                    asset_name));
            }
        }
    }

    for (size_t i = 0u; i < nmo_project_plan_script_count(plan); ++i) {
        nmo_project_script_desc_t script = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_script(plan, i, &script));
        NMO_RETURN_IF_ERROR(project_report_add_created(
            &report->script_diff,
            script.name));
    }

    NMO_RETURN_OK();
}

static nmo_status_t project_report_validate_and_populate(
    const nmo_project_plan_t *plan,
    nmo_project_report_t *report)
{
    nmo_status_t status = nmo_project_validate_plan(plan, &report->validation);
    if (status != NMO_OK) {
        return status;
    }
    if (!report->validation.ok) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    return project_report_populate_diff(report, plan);
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

    project_report_dispose_diffs(report);
    nmo_project_validation_report_dispose(&report->validation);
    free(report->output_path);
    memset(report, 0, sizeof(*report));
}

bool nmo_project_report_diff_has_created_scene(
    const nmo_project_report_t *report,
    const char *name)
{
    return report && project_report_created_contains(&report->scene_diff, name);
}

bool nmo_project_report_diff_has_created_object(
    const nmo_project_report_t *report,
    const char *name)
{
    return report && project_report_created_contains(&report->object_diff, name);
}

bool nmo_project_report_diff_has_created_asset(
    const nmo_project_report_t *report,
    const char *name)
{
    return report && project_report_created_contains(&report->asset_diff, name);
}

nmo_status_t nmo_project_executor_execute_dry_run(
    const nmo_project_plan_t *plan,
    nmo_project_report_t *report)
{
    if (!plan || !report) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "plan and report are required");
    }

    NMO_RETURN_IF_ERROR(project_report_reset_for_execute(report));
    report->dry_run = true;
    nmo_status_t status = project_report_validate_and_populate(plan, report);
    if (status != NMO_OK) {
        return status;
    }
    report->ok = true;
    NMO_RETURN_OK();
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

    NMO_RETURN_IF_ERROR(project_report_reset_for_execute(report));
    report->output_path = project_executor_strdup(output_path);
    if (!report->output_path) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to copy project output path");
    }

    nmo_status_t status = project_report_validate_and_populate(plan, report);
    if (status != NMO_OK) {
        return status;
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
    status = nmo_document_set_file_info(
        document,
        &(nmo_file_info_t){
            .file_version = 9u,
        });
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
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

    nmo_project_runtime_object_t *objects = NULL;
    size_t object_count = 0u;
    status = nmo_project_author_scenes(edit, plan, &objects, &object_count);
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_project_author_assets(edit, plan, objects, object_count);
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        free(objects);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_workspace_edit_commit(edit);
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        free(objects);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    status = nmo_project_author_scripts(workspace, plan, objects, object_count);
    free(objects);
    objects = NULL;
    if (status != NMO_OK) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return status;
    }

    size_t output_len = strlen(output_path);
    char *temp_path = (char *)malloc(output_len + 5u);
    if (!temp_path) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate project temp output path");
    }
    memcpy(temp_path, output_path, output_len);
    memcpy(temp_path + output_len, ".tmp", 5u);

    remove(temp_path);
    status = nmo_document_save_file(document, temp_path, NULL);
    if (status == NMO_OK) {
        nmo_document_t *loaded = NULL;
        status = nmo_document_load_file(ctx, temp_path, NULL, &loaded);
        nmo_document_destroy(loaded);
    }
    if (status == NMO_OK) {
        remove(output_path);
        if (rename(temp_path, output_path) != 0) {
            status = NMO_ERR_CANT_WRITE_FILE;
            NMO_SET_LAST_ERROR(status, NMO_SEVERITY_ERROR,
                               "failed to publish generated project output: %s",
                               output_path);
        }
    }
    if (status != NMO_OK) {
        remove(temp_path);
    }
    free(temp_path);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    if (status != NMO_OK) {
        return status;
    }

    report->ok = true;
    NMO_RETURN_OK();
}
