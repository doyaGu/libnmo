#ifndef NMO_DOCUMENT_LOAD_H
#define NMO_DOCUMENT_LOAD_H

#include "runtime/nmo_document.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "format/nmo_file_state.h"

#include <stddef.h>

#define NMO_LOAD_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LOAD_WORKFLOW_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif
typedef struct nmo_load_options nmo_load_options_t;
typedef struct nmo_plugin_dep nmo_plugin_dep_t;
typedef struct nmo_header nmo_header_t;
typedef struct nmo_manager_data nmo_manager_data_t;

NMO_API nmo_status_t nmo_document_load_file(
    nmo_context_t *ctx,
    const char *path,
    const nmo_load_options_t *options,
    nmo_document_t **out_document);
NMO_API const nmo_file_state_t *nmo_document_get_file_state(
    const nmo_document_t *document);
NMO_API nmo_file_info_t nmo_document_get_file_info(const nmo_document_t *document);
NMO_API const nmo_header_t *nmo_document_get_header(const nmo_document_t *document);
NMO_API int nmo_document_is_partial_load(const nmo_document_t *document);
NMO_API int nmo_document_has_materialized_load_state(const nmo_document_t *document);
NMO_API nmo_status_t nmo_document_get_runtime_load_stats(
    const nmo_document_t *document,
    nmo_runtime_load_stats_t *out_stats);
NMO_API const nmo_session_plugin_diagnostics_t *nmo_document_get_plugin_diagnostics(
    const nmo_document_t *document);
/**
 * @brief Parse the state of one object of a document loaded with
 *        NMO_LOAD_PROFILE_OBJECTS.
 *
 * The schema reads the object's chunk and the object's own dependency hooks
 * run, as a full load does for it. The session-wide steps of a full load do
 * not run (handing managers their data, fitting layer cells to their grid,
 * indexing behaviors), so the state lacks what those steps would add to it.
 * An object that already has a state, or has no chunk, is left as it is.
 *
 * @return NMO_OK; NMO_ERR_NOT_FOUND for an unknown id; NMO_ERR_NOT_SUPPORTED
 *         when no schema reads the object's class; NMO_ERR_INVALID_STATE when
 *         the document was not loaded with NMO_LOAD_PROFILE_OBJECTS; or the
 *         schema's error, with the object left without a state.
 */
NMO_API nmo_status_t nmo_document_load_object_state(
    nmo_document_t *document,
    nmo_object_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_LOAD_H */
