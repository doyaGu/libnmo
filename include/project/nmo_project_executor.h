#ifndef NMO_PROJECT_EXECUTOR_H
#define NMO_PROJECT_EXECUTOR_H

#include "core/nmo_error.h"
#include "nmo_types.h"
#include "project/nmo_project_validator.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_project_plan nmo_project_plan_t;

typedef struct nmo_project_report_name_list {
    char **names;
    size_t count;
} nmo_project_report_name_list_t;

typedef struct nmo_project_report_diff {
    nmo_project_report_name_list_t created;
} nmo_project_report_diff_t;

typedef struct nmo_project_report_object_evidence {
    uint32_t plan_handle;
    nmo_object_id_t object_id;
    nmo_class_id_t class_id;
    char *name;
} nmo_project_report_object_evidence_t;

typedef struct nmo_project_report_asset_binding_evidence {
    char *owner_name;
    char *asset_name;
    char *kind;
} nmo_project_report_asset_binding_evidence_t;

typedef struct nmo_project_report_material_texture_slot_evidence {
    char *material_name;
    uint32_t slot;
    char *texture_name;
    char *source_path;
} nmo_project_report_material_texture_slot_evidence_t;

typedef struct nmo_project_report_script_evidence {
    char *name;
    size_t step_count;
    bool validation_ok;
} nmo_project_report_script_evidence_t;

typedef struct nmo_project_report_evidence {
    nmo_project_report_object_evidence_t *objects;
    size_t object_count;
    nmo_project_report_asset_binding_evidence_t *asset_bindings;
    size_t asset_binding_count;
    nmo_project_report_material_texture_slot_evidence_t *material_texture_slots;
    size_t material_texture_slot_count;
    nmo_project_report_script_evidence_t *scripts;
    size_t script_count;
    bool post_load_checked;
    bool post_load_ok;
} nmo_project_report_evidence_t;

typedef struct nmo_project_report {
    bool ok;
    bool dry_run;
    nmo_project_report_diff_t document_diff;
    nmo_project_report_diff_t scene_diff;
    nmo_project_report_diff_t object_diff;
    nmo_project_report_diff_t asset_diff;
    nmo_project_report_diff_t script_diff;
    nmo_project_report_diff_t manager_diff;
    nmo_project_validation_report_t validation;
    nmo_project_report_evidence_t evidence;
    char *output_path;
} nmo_project_report_t;

NMO_API void nmo_project_report_init(nmo_project_report_t *report);
NMO_API void nmo_project_report_dispose(nmo_project_report_t *report);

NMO_API bool nmo_project_report_diff_has_created_scene(
    const nmo_project_report_t *report,
    const char *name);

NMO_API bool nmo_project_report_diff_has_created_object(
    const nmo_project_report_t *report,
    const char *name);

NMO_API bool nmo_project_report_diff_has_created_asset(
    const nmo_project_report_t *report,
    const char *name);

NMO_API nmo_status_t nmo_project_executor_execute_dry_run(
    const nmo_project_plan_t *plan,
    nmo_project_report_t *report);

NMO_API nmo_status_t nmo_project_executor_execute_to_file(
    const nmo_project_plan_t *plan,
    const char *output_path,
    nmo_project_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PROJECT_EXECUTOR_H */
