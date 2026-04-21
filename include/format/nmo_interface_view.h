/**
 * @file nmo_interface_view.h
 * @brief Stable read-only summaries for behavior interface data
 */

#ifndef NMO_INTERFACE_VIEW_H
#define NMO_INTERFACE_VIEW_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/*
 * Stable read-only facade over parsed interface data. This header is intended
 * for binding-facing consumers that need interface facts without depending on
 * parsed struct layout or arena-owned nested arrays.
 */
#define NMO_INTERFACE_VIEW_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_INTERFACE_VIEW_READ_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef struct nmo_interface_body_view {
    bool has_body;
    size_t link_count;
    size_t operation_count;
    size_t comment_count;
    size_t local_param_count;
    size_t shared_param_count;
    bool has_params;
    bool has_graph_io;
    bool has_links_section;
    bool has_operations_section;
    bool has_comments_section;
    bool has_unknown_flag_section;
    int32_t unknown_flag;
    size_t inward_input_count;
    size_t outward_input_count;
    size_t inward_output_count;
    size_t outward_output_count;
} nmo_interface_body_view_t;

typedef struct nmo_interface_view {
    nmo_object_id_t owner_behavior_id;
    nmo_object_id_t behavior_id;
    bool is_root;
    uint32_t version;
    uint32_t format_flags;
    uint32_t flags;
    uint32_t depth;
    size_t sub_behavior_count;
    bool extra_present;
    size_t extra_entry_count;
    bool has_snapshot;
    nmo_interface_body_view_t body;
} nmo_interface_view_t;

NMO_API nmo_status_t nmo_interface_view_from_behavior(
    nmo_session_t *session,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view);

NMO_API nmo_status_t nmo_interface_view_find_behavior(
    nmo_session_t *session,
    nmo_object_id_t owner_behavior_id,
    nmo_object_id_t behavior_id,
    nmo_interface_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INTERFACE_VIEW_H */
