#ifndef NMO_OBJECT_REFS_H
#define NMO_OBJECT_REFS_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "object/nmo_ref_query.h"
#include "object/nmo_ref_graph.h"

typedef struct nmo_object nmo_object_t;
typedef struct nmo_session nmo_document_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_object_refs_direction {
    NMO_OBJECT_REFS_OUTGOING = 1u << 0,
    NMO_OBJECT_REFS_INCOMING = 1u << 1,
    NMO_OBJECT_REFS_BOTH = (1u << 0) | (1u << 1)
} nmo_object_refs_direction_t;

typedef struct nmo_object_refs_edge {
    const nmo_ref_edge_t *edge;
    bool is_incoming;
    nmo_object_t *peer;
} nmo_object_refs_edge_t;

typedef struct nmo_object_refs_result {
    size_t outgoing;
    size_t incoming;
} nmo_object_refs_result_t;

typedef bool (*nmo_object_refs_edge_visitor_fn)(
    const nmo_object_refs_edge_t *edge,
    void *user_data);

NMO_API nmo_status_t nmo_object_refs_iterate(
    nmo_document_t *document,
    nmo_object_id_t object_id,
    nmo_object_refs_direction_t direction,
    nmo_object_refs_edge_visitor_fn visitor,
    void *user_data,
    nmo_object_refs_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_REFS_H */
