#ifndef NMO_PROJECT_INTERNAL_H
#define NMO_PROJECT_INTERNAL_H

#include "core/nmo_error.h"

typedef struct nmo_document nmo_document_t;
typedef struct nmo_project_plan nmo_project_plan_t;

nmo_status_t nmo_project_author_scenes(
    nmo_document_t *document,
    const nmo_project_plan_t *plan);

#endif /* NMO_PROJECT_INTERNAL_H */
