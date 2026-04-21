/**
 * @file nmo_script_edit.h
 * @brief Script edit transaction kernel.
 */

#ifndef NMO_SCRIPT_EDIT_H
#define NMO_SCRIPT_EDIT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_session_edit nmo_session_edit_t;
typedef struct nmo_script_edit_tx nmo_script_edit_tx_t;

typedef enum nmo_script_edit_validation_flags {
    NMO_SCRIPT_EDIT_VALIDATE_REFERENCES      = 1u << 0,
    NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX  = 1u << 1,
    NMO_SCRIPT_EDIT_VALIDATE_INTERFACE       = 1u << 2,
    NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY = 1u << 3
} nmo_script_edit_validation_flags_t;

typedef struct nmo_script_edit_report {
    size_t created_objects;
    size_t deleted_objects;
    size_t changed_objects;
    size_t moved_links;
    size_t rewired_parameters;
    size_t interface_changes;
    size_t warnings;
    size_t errors;
} nmo_script_edit_report_t;

NMO_API nmo_status_t nmo_script_edit_begin(nmo_context_t *ctx,
                                           nmo_session_t *session,
                                           const char *label,
                                           nmo_script_edit_tx_t **out_tx);

NMO_API nmo_session_edit_t *nmo_script_edit_session_edit(
    nmo_script_edit_tx_t *tx);

NMO_API void nmo_script_edit_mark(nmo_script_edit_tx_t *tx,
                                  uint32_t session_edit_flags);

NMO_API const nmo_script_edit_report_t *nmo_script_edit_report(
    const nmo_script_edit_tx_t *tx);

NMO_API nmo_status_t nmo_script_edit_validate(nmo_script_edit_tx_t *tx,
                                              uint32_t validation_flags);

NMO_API nmo_status_t nmo_script_edit_commit(nmo_script_edit_tx_t *tx);
NMO_API void nmo_script_edit_rollback(nmo_script_edit_tx_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_EDIT_H */
