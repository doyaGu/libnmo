#ifndef NMO_DOCUMENT_LOAD_H
#define NMO_DOCUMENT_LOAD_H

#include "document/nmo_document.h"
#include "app/nmo_load.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_document_load_file(
    nmo_context_t *ctx,
    const char *path,
    nmo_document_t **out_document);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_LOAD_H */
