/**
 * @file export_dot.c
 * @brief DOT (Graphviz) format export for reference graphs.
 */

#include "app/nmo_export_dot.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "type/nmo_type_query.h"
#include "core/nmo_arena.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* DOT escape                                                                */
/* ------------------------------------------------------------------------ */

void nmo_dot_escape_label(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) {
        if (dst && dst_size > 0) dst[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i]; i++) {
        bool need_escape = (src[i] == '"' || src[i] == '\\' || src[i] == '|' ||
                            src[i] == '{' || src[i] == '}' ||
                            src[i] == '<' || src[i] == '>');
        size_t need = need_escape ? 2 : 1;
        if (j + need >= dst_size) break; /* reserve 1 for NUL */
        if (need_escape) dst[j++] = '\\';
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

/* ------------------------------------------------------------------------ */
/* Edge color palette                                                        */
/* ------------------------------------------------------------------------ */

static const char *dot_edge_color(nmo_ref_kind_t kind) {
    static const char *colors[] = {
        [NMO_REF_KIND_UNKNOWN]       = "gray",
        [NMO_REF_KIND_HIERARCHY]     = "black",
        [NMO_REF_KIND_MESH]          = "blue",
        [NMO_REF_KIND_MATERIAL]      = "red",
        [NMO_REF_KIND_TEXTURE]       = "darkgreen",
        [NMO_REF_KIND_OWNER]         = "purple",
        [NMO_REF_KIND_BEHAVIOR_LINK] = "orange",
        [NMO_REF_KIND_PARAMETER]     = "brown",
        [NMO_REF_KIND_TARGET]        = "cyan4",
        [NMO_REF_KIND_GROUP_MEMBER]  = "magenta",
        [NMO_REF_KIND_SCENE]         = "darkgoldenrod",
        [NMO_REF_KIND_ANIMATION]     = "deeppink",
        [NMO_REF_KIND_PLACE]         = "darkolivegreen",
        [NMO_REF_KIND_SKIN_BONE]     = "chocolate",
        [NMO_REF_KIND_DATA_ARRAY]    = "navy",
        [NMO_REF_KIND_SCRIPT]        = "darkslategray",
    };
    if ((int)kind >= 0 && kind < NMO_REF_KIND_MAX)
        return colors[kind];
    return "gray";
}

/* ------------------------------------------------------------------------ */
/* Collect unique node IDs                                                   */
/* ------------------------------------------------------------------------ */

/* Binary search in sorted array. Returns index if found, or insertion point. */
static size_t bsearch_id(const nmo_object_id_t *arr, size_t count, nmo_object_id_t id) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < id) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Collect unique node IDs using sorted insertion — O(E * log N). */
static size_t collect_nodes(const nmo_ref_edge_t *edges, size_t edge_count,
                            nmo_object_id_t *out, size_t cap) {
    size_t count = 0;
    for (size_t i = 0; i < edge_count; ++i) {
        nmo_object_id_t ids[2] = { edges[i].from, edges[i].to };
        for (int k = 0; k < 2; ++k) {
            size_t pos = bsearch_id(out, count, ids[k]);
            if (pos < count && out[pos] == ids[k]) continue; /* duplicate */
            if (count >= cap) continue;
            /* Insert at pos, shift right */
            memmove(&out[pos + 1], &out[pos], (count - pos) * sizeof(nmo_object_id_t));
            out[pos] = ids[k];
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

nmo_status_t nmo_ref_graph_to_dot(
    nmo_ref_graph_t *graph,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    uint32_t kind_mask,
    nmo_arena_t *arena,
    FILE *out)
{
    if (!graph || !repo || !registry || !arena || !out)
        return NMO_ERR_INVALID_ARGUMENT;

    /* Get all edges */
    nmo_ref_edge_t *all_edges = NULL;
    size_t all_count = 0;
    nmo_status_t st = nmo_ref_graph_get_edges(graph, &all_edges, &all_count);
    if (st != NMO_OK)
        return st;

    /* Apply kind_mask filter if set */
    nmo_ref_edge_t *edges = all_edges;
    size_t edge_count = all_count;
    nmo_ref_edge_t *filtered = NULL;

    if (kind_mask != 0) {
        filtered = (nmo_ref_edge_t *)nmo_arena_alloc(
            arena, all_count * sizeof(nmo_ref_edge_t), _Alignof(nmo_ref_edge_t));
        if (!filtered && all_count > 0)
            return NMO_ERR_NOMEM;
        size_t fc = 0;
        for (size_t i = 0; i < all_count; ++i) {
            if ((int)all_edges[i].kind >= 0 &&
                all_edges[i].kind < NMO_REF_KIND_MAX &&
                (kind_mask & (1u << (unsigned)all_edges[i].kind))) {
                filtered[fc++] = all_edges[i];
            }
        }
        edges = filtered;
        edge_count = fc;
    }

    /* Collect unique node IDs */
    size_t node_cap = edge_count * 2 + 1;
    nmo_object_id_t *node_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, node_cap * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));
    size_t node_count = 0;
    if (node_ids) {
        node_count = collect_nodes(edges, edge_count, node_ids, node_cap);
    } else if (edge_count > 0) {
        return NMO_ERR_NOMEM;
    }

    /* Emit DOT header */
    fprintf(out, "digraph references {\n");
    fprintf(out, "    rankdir=LR;\n");
    fprintf(out, "    node [shape=record, fontsize=10];\n\n");

    /* Nodes */
    fprintf(out, "    // Nodes\n");
    for (size_t i = 0; i < node_count; ++i) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, node_ids[i]);
        const char *cls = "?";
        const char *name = "";
        char cbuf[32];
        if (obj) {
            const char *cn = nmo_type_query_class_name_from_id(
                registry, nmo_object_get_class_id(obj));
            if (cn) {
                cls = cn;
            } else {
                snprintf(cbuf, sizeof(cbuf), "Class#%u",
                         (unsigned)nmo_object_get_class_id(obj));
                cls = cbuf;
            }
            const char *n = nmo_object_get_name(obj);
            if (n && n[0]) name = n;
        }
        char esc_cls[64], esc_name[128];
        nmo_dot_escape_label(cls, esc_cls, sizeof(esc_cls));
        nmo_dot_escape_label(name, esc_name, sizeof(esc_name));
        fprintf(out, "    n%u [label=\"#%u|%s|%s\"];\n",
                node_ids[i], node_ids[i], esc_cls, esc_name);
    }

    /* Edges */
    fprintf(out, "\n    // Edges\n");
    for (size_t i = 0; i < edge_count; ++i) {
        fprintf(out, "    n%u -> n%u [label=\"%s\", color=\"%s\"];\n",
                edges[i].from, edges[i].to,
                nmo_ref_kind_name(edges[i].kind),
                dot_edge_color(edges[i].kind));
    }

    fprintf(out, "}\n");

    /* Arena-allocated buffers are freed when arena is reset/destroyed */
    return NMO_OK;
}
