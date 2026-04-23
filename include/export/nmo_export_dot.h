#ifndef NMO_EXPORT_DOT_OWNER_H
#define NMO_EXPORT_DOT_OWNER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define NMO_EXPORT_DOT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_EXPORT_DOT_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_ref_graph nmo_ref_graph_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_arena nmo_arena_t;

NMO_API nmo_status_t nmo_ref_graph_to_dot(
    nmo_ref_graph_t *graph,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    uint32_t kind_mask,
    nmo_arena_t *arena,
    FILE *out);

NMO_API void nmo_dot_escape_label(const char *src, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXPORT_DOT_OWNER_H */
