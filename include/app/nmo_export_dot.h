/**
 * @file nmo_export_dot.h
 * @brief DOT (Graphviz) format export for reference graphs.
 */

#ifndef NMO_EXPORT_DOT_H
#define NMO_EXPORT_DOT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_ref_graph nmo_ref_graph_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/**
 * @brief Export reference graph as DOT digraph.
 *
 * Emits a Graphviz-compatible digraph with labeled nodes (id|class|name)
 * and colored edges labeled by reference kind.
 *
 * @param graph     Reference graph (required)
 * @param repo      Object repository for node names/classes (required)
 * @param registry  Type registry for class name lookup (required)
 * @param kind_mask Bitmask of nmo_ref_kind_t to include (0 = all)
 * @param out       Output stream (required)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_ref_graph_to_dot(
    nmo_ref_graph_t *graph,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    uint32_t kind_mask,
    FILE *out);

/**
 * @brief Escape a string for use in DOT record labels.
 *
 * Backslash-escapes characters special in DOT record syntax:
 * " \\ | { } < >
 *
 * @param src      Source string (required)
 * @param dst      Destination buffer (required)
 * @param dst_size Size of destination buffer
 */
NMO_API void nmo_dot_escape_label(const char *src, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXPORT_DOT_H */
