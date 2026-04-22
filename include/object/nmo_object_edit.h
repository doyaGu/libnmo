#ifndef NMO_OBJECT_EDIT_H
#define NMO_OBJECT_EDIT_H

#include "runtime/nmo_workspace.h"
#include "app/nmo_object_import.h"
#include "session/nmo_session_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_object_edit_set_fields(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_session_field_edit_t *fields,
    size_t field_count,
    nmo_session_field_edit_result_t *out_result);

NMO_API nmo_status_t nmo_object_edit_rename(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t object_id,
    const char *new_name);

NMO_API nmo_status_t nmo_object_edit_import_json(
    nmo_workspace_t *workspace,
    const char *json_data,
    size_t json_size,
    uint32_t flags,
    nmo_import_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_EDIT_H */
