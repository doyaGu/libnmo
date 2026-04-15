/**
 * @file nmo_session_edit.h
 * @brief Session-scoped edit transactions for object mutation.
 */
#ifndef NMO_SESSION_EDIT_H
#define NMO_SESSION_EDIT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;
typedef struct nmo_session_edit nmo_session_edit_t;

typedef enum nmo_session_edit_flags {
    NMO_SESSION_EDIT_OBJECT_STATE   = 1u << 0,
    NMO_SESSION_EDIT_REFERENCES     = 1u << 1,
    NMO_SESSION_EDIT_BEHAVIOR_GRAPH = 1u << 2,
    NMO_SESSION_EDIT_NAMES          = 1u << 3,
    NMO_SESSION_EDIT_RESOURCES      = 1u << 4
} nmo_session_edit_flags_t;

typedef struct nmo_session_field_edit {
    const char *field_name;
    const char *value_str;
} nmo_session_field_edit_t;

typedef struct nmo_session_field_edit_result {
    size_t applied;
    size_t failed;
} nmo_session_field_edit_result_t;

NMO_API nmo_status_t nmo_session_edit_begin(
    nmo_session_t *session,
    const char *label,
    nmo_session_edit_t **out_edit);

NMO_API nmo_status_t nmo_session_edit_commit(nmo_session_edit_t *edit);
NMO_API void nmo_session_edit_rollback(nmo_session_edit_t *edit);
NMO_API void *nmo_session_edit_alloc(nmo_session_edit_t *edit, size_t size, size_t align);
NMO_API void nmo_session_edit_mark(nmo_session_edit_t *edit, uint32_t flags);

NMO_API nmo_status_t nmo_session_apply_edit_flags(nmo_session_t *session, uint32_t flags);

NMO_API nmo_status_t nmo_session_edit_set_object_fields(
    nmo_session_edit_t *edit,
    nmo_object_id_t object_id,
    const nmo_session_field_edit_t *fields,
    size_t field_count,
    nmo_session_field_edit_result_t *out_result);

NMO_API nmo_status_t nmo_session_edit_rename_object(
    nmo_session_edit_t *edit,
    nmo_object_id_t object_id,
    const char *new_name);

NMO_API nmo_status_t nmo_session_edit_set_parameter_value(
    nmo_session_edit_t *edit,
    nmo_object_id_t parameter_id,
    const char *value_str);

NMO_API nmo_status_t nmo_session_edit_set_dataarray_cell(
    nmo_session_edit_t *edit,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value_str);

NMO_API nmo_status_t nmo_session_edit_add_behavior_link(
    nmo_session_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    int16_t activation_delay,
    nmo_object_id_t *out_link_id);

NMO_API nmo_status_t nmo_session_edit_remove_behavior_link(
    nmo_session_edit_t *edit,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_EDIT_H */
