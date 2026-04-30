#include "project_internal.h"

#include "behavior/nmo_edit_plan.h"
#include "core/nmo_guid.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "project/nmo_script_authoring.h"
#include "runtime/nmo_workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nmo_status_t project_find_runtime_object(
    const nmo_project_runtime_object_t *objects,
    size_t object_count,
    uint32_t plan_handle,
    nmo_object_id_t *out_object_id)
{
    if (!objects || !out_object_id || plan_handle == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "runtime object map and output object id are required");
    }

    *out_object_id = 0u;
    for (size_t i = 0u; i < object_count; ++i) {
        if (objects[i].plan_handle == plan_handle) {
            *out_object_id = objects[i].object_id;
            NMO_RETURN_OK();
        }
    }

    NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                     "project object handle has no generated runtime object");
}

static char *project_script_make_debug_name(const char *script_name)
{
    const char *suffix = "_DebugOutput";
    size_t name_len = script_name ? strlen(script_name) : 0u;
    size_t suffix_len = strlen(suffix);
    char *name = (char *)malloc(name_len + suffix_len + 1u);
    if (!name) {
        return NULL;
    }
    if (name_len != 0u) {
        memcpy(name, script_name, name_len);
    }
    memcpy(name + name_len, suffix, suffix_len + 1u);
    return name;
}

static nmo_status_t project_script_add_debug_output(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id,
    const char *script_name,
    const char *message)
{
    nmo_edit_plan_t *edit_plan = NULL;
    nmo_edit_report_t edit_report;
    char *node_name = NULL;
    nmo_status_t status = nmo_edit_report_init(&edit_report);
    if (status != NMO_OK) {
        return status;
    }

    node_name = project_script_make_debug_name(script_name);
    if (!node_name) {
        nmo_edit_report_dispose(&edit_report);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "failed to allocate debug output node name");
    }

    status = nmo_edit_plan_create(&edit_plan);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_node(
            edit_plan,
            behavior_id,
            NMO_GUID(0x18655B3Fu, 0x68291DC3u),
            node_name);
    }
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_set_parameter_value_from_handle(
            edit_plan,
            0u,
            "input_param:String",
            message,
            NULL);
    }
    if (status == NMO_OK) {
        status = nmo_edit_executor_execute(workspace, edit_plan, NULL, &edit_report);
    }

    nmo_edit_plan_destroy(edit_plan);
    nmo_edit_report_dispose(&edit_report);
    free(node_name);
    return status;
}

static nmo_status_t project_script_create_root(
    nmo_workspace_t *workspace,
    nmo_object_id_t owner_id,
    const char *name,
    nmo_object_id_t *out_behavior_id)
{
    nmo_workspace_edit_t *edit = NULL;
    nmo_object_id_t behavior_id = 0u;
    nmo_status_t status =
        nmo_workspace_edit_begin(workspace, "project script binding", &edit);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_BEHAVIOR,
            .name = name,
            .type_guid = NMO_GUID_NULL,
        },
        &behavior_id);
    if (status == NMO_OK) {
        status = nmo_object_edit_bind_script(edit, owner_id, behavior_id);
    }
    if (status != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        return status;
    }

    status = nmo_workspace_edit_commit(edit);
    if (status != NMO_OK) {
        return status;
    }

    if (out_behavior_id) {
        *out_behavior_id = behavior_id;
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_project_author_scripts(
    nmo_workspace_t *workspace,
    const nmo_project_plan_t *plan,
    const nmo_project_runtime_object_t *objects,
    size_t object_count)
{
    if (!workspace || !plan) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "workspace and plan are required");
    }

    size_t script_count = nmo_project_plan_script_count(plan);
    for (size_t i = 0u; i < script_count; ++i) {
        nmo_project_script_desc_t script = {0};
        nmo_object_id_t owner_id = 0u;
        nmo_object_id_t behavior_id = 0u;
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_script(plan, i, &script));
        NMO_RETURN_IF_ERROR(project_find_runtime_object(
            objects,
            object_count,
            script.object_handle,
            &owner_id));
        NMO_RETURN_IF_ERROR(project_script_create_root(
            workspace,
            owner_id,
            script.name,
            &behavior_id));

        for (size_t j = 0u; j < script.step_count; ++j) {
            nmo_project_script_step_desc_t step = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_script_step(
                plan,
                script.handle,
                j,
                &step));
            switch (step.kind) {
            case NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT:
                NMO_RETURN_IF_ERROR(project_script_add_debug_output(
                    workspace,
                    behavior_id,
                    script.name,
                    step.message));
                break;
            default:
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "unsupported project script step");
            }
        }
    }

    NMO_RETURN_OK();
}
