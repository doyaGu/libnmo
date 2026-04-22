#ifndef NMO_DOCUMENT_SAVE_H
#define NMO_DOCUMENT_SAVE_H

#include "document/nmo_document.h"
#include "app/nmo_save.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_document_save_file(
    nmo_document_t *document,
    const char *path,
    const nmo_save_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_SAVE_H */
