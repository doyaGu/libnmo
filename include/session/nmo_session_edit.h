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

#define NMO_SESSION_EDIT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SESSION_EDIT_TRANSACTION_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;
typedef struct nmo_session_edit nmo_session_edit_t;

typedef enum nmo_session_edit_flags {
    /** Object-local typed state changed. */
    NMO_SESSION_EDIT_OBJECT_STATE   = 1u << 0,
    /** Object references changed; invalidates the cached reference graph. */
    NMO_SESSION_EDIT_REFERENCES     = 1u << 1,
    /** Behavior graph membership changed; invalidates behavior index and reference graph. */
    NMO_SESSION_EDIT_BEHAVIOR_GRAPH = 1u << 2,
    /** Object names changed; invalidates name query state and rebuilds object indexes. */
    NMO_SESSION_EDIT_NAMES          = 1u << 3,
    /** Save-affecting resource state changed; no resource query cache exists yet. */
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
NMO_API nmo_status_t nmo_session_edit_snapshot_bytes(
    nmo_session_edit_t *edit,
    void *target,
    size_t size);
NMO_API nmo_status_t nmo_session_edit_track_created_object(
    nmo_session_edit_t *edit,
    nmo_object_id_t object_id);
NMO_API nmo_status_t nmo_session_edit_snapshot_object_chunk(
    nmo_session_edit_t *edit,
    nmo_object_id_t object_id);
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

NMO_API nmo_status_t nmo_session_edit_set_parameter_bytes(
    nmo_session_edit_t *edit,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count);

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

NMO_API nmo_status_t nmo_session_edit_mark_behavior_interface(
    nmo_session_edit_t *edit,
    nmo_object_id_t behavior_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_EDIT_H */
