#ifndef NMO_BEHAVIOR_QUERY_H
#define NMO_BEHAVIOR_QUERY_H

#include "document/nmo_document.h"
#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"

#include <stddef.h>

#define NMO_BEHAVIOR_QUERY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BEHAVIOR_QUERY_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_behavior_script_view {
    nmo_object_id_t script_id;
    nmo_object_id_t owner_id;
    const char *script_name;
    const char *owner_name;
    nmo_class_id_t owner_class_id;
} nmo_behavior_script_view_t;

NMO_API nmo_status_t nmo_behavior_query_count_scripts(
    nmo_document_t *document,
    size_t *out_count);

NMO_API nmo_status_t nmo_behavior_query_collect_scripts(
    nmo_document_t *document,
    nmo_array_t *out_scripts);

NMO_API nmo_status_t nmo_behavior_query_script_at(
    nmo_document_t *document,
    size_t index,
    nmo_behavior_script_view_t *out_view);

NMO_API nmo_status_t nmo_behavior_query_script_from_script_id(
    nmo_document_t *document,
    nmo_object_id_t script_id,
    nmo_behavior_script_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_QUERY_H */
