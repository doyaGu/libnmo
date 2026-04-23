#ifndef NMO_DOCUMENT_H
#define NMO_DOCUMENT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_document nmo_document_t;
typedef struct nmo_object_repository nmo_object_repository_t;

NMO_API nmo_document_t *nmo_document_create(nmo_context_t *ctx);
NMO_API void nmo_document_destroy(nmo_document_t *document);

NMO_API nmo_context_t *nmo_document_get_context(const nmo_document_t *document);
NMO_API nmo_object_repository_t *nmo_document_get_repository(
    const nmo_document_t *document);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_H */
