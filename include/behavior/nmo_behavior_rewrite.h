/**
 * @file nmo_behavior_rewrite.h
 * @brief Safe behavior graph rewrite operations.
 */

#ifndef NMO_BEHAVIOR_REWRITE_H
#define NMO_BEHAVIOR_REWRITE_H

#include "core/nmo_guid.h"
#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

typedef struct nmo_behavior_replace_bb_desc {
    nmo_object_id_t behavior_id;
    nmo_guid_t block_guid;
    const char *name;
    uint32_t block_version;
    bool preserve_links;
    bool preserve_params;
} nmo_behavior_replace_bb_desc_t;

typedef struct nmo_behavior_rewrite_report {
    bool changed;
    bool eligible_leaf;
    nmo_object_id_t behavior_id;
    uint32_t before_flags;
    uint32_t after_flags;
    nmo_guid_t before_guid;
    nmo_guid_t after_guid;
    size_t sub_behavior_count;
    size_t sub_behavior_link_count;
    size_t operation_count;
    size_t preserved_inputs;
    size_t preserved_outputs;
    size_t preserved_in_parameters;
    size_t preserved_out_parameters;
    size_t preserved_local_parameters;
    size_t preserved_control_in;
    size_t preserved_control_out;
    size_t preserved_parameter_in;
    size_t preserved_parameter_out;
    const char *diagnostic_code;
    const char *diagnostic_message;
    size_t diagnostics_count;
} nmo_behavior_rewrite_report_t;

NMO_API nmo_status_t nmo_behavior_replace_bb(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_rewrite_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_REWRITE_H */
