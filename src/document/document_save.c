/**
 * @file document_save.c
 * @brief Saving a document; the save pipeline is session/serializer.c
 */

#include "document/nmo_document_save.h"
#include "../runtime/runtime_internal.h"

nmo_status_t nmo_document_save_file(
    nmo_document_t *document,
    const char *path,
    const nmo_save_options_t *options)
{
    if (document == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_document_internal_save_file(document, path, options);
}
