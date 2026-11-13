/**
 * @file document_export.c
 * @brief Writing a document in other formats
 */

#include "document/nmo_document_export.h"
#include "object/nmo_context.h"

nmo_status_t nmo_document_export_gltf(
    nmo_document_t *document,
    const char *path,
    const nmo_gltf_export_options_t *options,
    nmo_gltf_export_stats_t *out_stats)
{
    if (document == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_context_t *ctx = nmo_document_get_context(document);
    return nmo_export_gltf(nmo_document_get_repository(document),
                           ctx != NULL ? nmo_context_get_type_registry(ctx) : NULL,
                           path, options, out_stats);
}
