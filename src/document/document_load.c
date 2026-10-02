/**
 * @file document_load.c
 * @brief Loading a document and reading what the load recorded; the load
 *        pipeline is session/session_load.c
 */

#include "document/nmo_document_load.h"
#include "../runtime/runtime_internal.h"
#include "core/nmo_error.h"
#include <string.h>

nmo_status_t nmo_document_load_file(
    nmo_context_t *ctx,
    const char *path,
    const nmo_load_options_t *options,
    nmo_document_t **out_document)
{
    nmo_document_t *document = NULL;
    nmo_status_t status = NMO_OK;

    if (ctx == NULL || path == NULL || out_document == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_document = NULL;

    document = nmo_document_create(ctx);
    if (document == NULL) {
        return NMO_ERR_NOMEM;
    }

    status = nmo_document_internal_load_file(document, path, options);
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        return status;
    }

    *out_document = document;
    return NMO_OK;
}

const nmo_file_state_t *nmo_document_get_file_state(const nmo_document_t *document)
{
    return nmo_document_internal_file_state(document);
}

nmo_file_info_t nmo_document_get_file_info(const nmo_document_t *document)
{
    const nmo_file_state_t *file_state = nmo_document_get_file_state(document);
    nmo_file_info_t empty = {0};
    return file_state != NULL ? file_state->info : empty;
}

const nmo_header_t *nmo_document_get_header(const nmo_document_t *document)
{
    return nmo_document_internal_header(document);
}

int nmo_document_is_partial_load(const nmo_document_t *document)
{
    return nmo_document_internal_is_partial_load(document);
}

int nmo_document_has_materialized_load_state(const nmo_document_t *document)
{
    return nmo_document_internal_has_materialized_load_state(document);
}

nmo_status_t nmo_document_get_runtime_load_stats(
    const nmo_document_t *document,
    nmo_runtime_load_stats_t *out_stats)
{
    return nmo_document_internal_get_runtime_load_stats(document, out_stats);
}

const nmo_session_plugin_diagnostics_t *nmo_document_get_plugin_diagnostics(
    const nmo_document_t *document)
{
    return nmo_document_internal_plugin_diagnostics(document);
}
