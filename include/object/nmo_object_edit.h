#ifndef NMO_OBJECT_EDIT_H
#define NMO_OBJECT_EDIT_H

#include "runtime/nmo_workspace.h"

#include <stdbool.h>
#include <stdint.h>

#define NMO_OBJECT_IMPORT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_IMPORT_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_type_registry nmo_type_registry_t;

typedef struct nmo_session_field_edit {
    const char *field_name;
    const char *value_str;
} nmo_session_field_edit_t;

typedef struct nmo_session_field_edit_result {
    size_t applied;
    size_t failed;
} nmo_session_field_edit_result_t;

#define NMO_IMPORT_CREATE_MISSING  0x0001
#define NMO_IMPORT_DRY_RUN         0x0002

typedef struct nmo_import_result {
    size_t objects_updated;
    size_t objects_created;
    size_t fields_written;
    size_t fields_skipped;
    size_t errors;
} nmo_import_result_t;

typedef struct nmo_parameter_write_options {
    bool resize;
    bool create_missing_manager_entry;
} nmo_parameter_write_options_t;

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

NMO_API nmo_status_t nmo_object_edit_set_parameter_value(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str);

NMO_API nmo_status_t nmo_object_edit_set_parameter_value_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options);

NMO_API nmo_status_t nmo_object_edit_set_parameter_bytes(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count);

NMO_API nmo_status_t nmo_object_edit_set_parameter_bytes_ex(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options);

NMO_API nmo_status_t nmo_object_edit_set_dataarray_cell(
    nmo_workspace_edit_t *edit,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value_str);

NMO_API nmo_status_t nmo_object_edit_ensure_message_manager_entry(
    nmo_workspace_edit_t *edit,
    const char *name,
    uint32_t *out_value);

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
