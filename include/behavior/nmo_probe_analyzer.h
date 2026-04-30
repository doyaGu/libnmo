#ifndef NMO_BEHAVIOR_PROBE_ANALYZER_H
#define NMO_BEHAVIOR_PROBE_ANALYZER_H

#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "nmo_types.h"

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_workspace nmo_workspace_t;

typedef enum nmo_probe_selector_kind {
    NMO_PROBE_SELECTOR_MESSAGE = 1,
    NMO_PROBE_SELECTOR_DATA_CELL_WRITE = 2
} nmo_probe_selector_kind_t;

typedef enum nmo_probe_selector_mode {
    NMO_PROBE_SELECTOR_MODE_UNSPECIFIED = 0,
    NMO_PROBE_SELECTOR_MODE_AUTO,
    NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE,
    NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK,
    NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION,
    NMO_PROBE_SELECTOR_MODE_EXPLICIT_DATA_CELL,
    NMO_PROBE_SELECTOR_MODE_EXPLICIT
} nmo_probe_selector_mode_t;

typedef enum nmo_probe_selector_status {
    NMO_PROBE_SELECTOR_STATUS_UNSPECIFIED = 0,
    NMO_PROBE_SELECTOR_STATUS_SELECTED,
    NMO_PROBE_SELECTOR_STATUS_NONE,
    NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS,
    NMO_PROBE_SELECTOR_STATUS_UNSAFE
} nmo_probe_selector_status_t;

typedef enum nmo_probe_selector_safety_policy {
    NMO_PROBE_SELECTOR_SAFETY_DEFAULT = 0,
    NMO_PROBE_SELECTOR_SAFETY_CONSERVATIVE,
    NMO_PROBE_SELECTOR_SAFETY_EXPLICIT_ONLY
} nmo_probe_selector_safety_policy_t;

typedef enum nmo_probe_selector_policy {
    NMO_PROBE_SELECTOR_POLICY_DEFAULT = 0,
    NMO_PROBE_SELECTOR_POLICY_AUTO,
    NMO_PROBE_SELECTOR_POLICY_EXPLICIT
} nmo_probe_selector_policy_t;

typedef enum nmo_probe_candidate_role {
    NMO_PROBE_CANDIDATE_UNKNOWN = 0,
    NMO_PROBE_CANDIDATE_MESSAGE,
    NMO_PROBE_CANDIDATE_MESSAGE_SENDER,
    NMO_PROBE_CANDIDATE_MESSAGE_WAITER,
    NMO_PROBE_CANDIDATE_MESSAGE_RECEIVER,
    NMO_PROBE_CANDIDATE_DATA_WRITER,
    NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION,
    NMO_PROBE_CANDIDATE_DATA_WRITE_LINK
} nmo_probe_candidate_role_t;

typedef struct nmo_probe_selector_candidate {
    nmo_object_id_t node_id;
    nmo_object_id_t parent_id;
    nmo_object_id_t boundary_behavior_id;
    nmo_object_id_t link_id;
    nmo_object_id_t operation_id;
    nmo_object_id_t from_io_id;
    nmo_object_id_t to_io_id;
    bool has_delay;
    uint32_t delay;
    nmo_object_id_t source_parameter_id;
    nmo_object_id_t value_parameter_id;
    nmo_object_id_t dataarray_id;
    nmo_guid_t column_type_guid;
    double confidence;
    nmo_guid_t bb_guid;
    char proto_name[96];
    nmo_probe_candidate_role_t role;
    char rejection_code[64];
} nmo_probe_selector_candidate_t;

typedef struct nmo_probe_selector_request {
    nmo_probe_selector_kind_t kind;
    nmo_object_id_t behavior_id;
    nmo_object_id_t dataarray_id;
    uint32_t row;
    uint32_t col;
    bool has_data_cell;
    nmo_object_id_t message_node_id;
    nmo_object_id_t write_node_id;
    nmo_object_id_t write_operation_id;
    nmo_object_id_t write_link_id;
    nmo_object_id_t remove_link_id;
    nmo_object_id_t from_io_id;
    nmo_object_id_t to_io_id;
    bool has_delay;
    uint32_t delay;
    nmo_probe_selector_safety_policy_t safety_policy;
    nmo_probe_selector_policy_t selector_policy;
} nmo_probe_selector_request_t;

typedef struct nmo_probe_safe_insertion {
    bool selected;
    nmo_object_id_t selected_node_id;
    nmo_object_id_t selected_link_id;
    nmo_object_id_t selected_operation_id;
    nmo_object_id_t remove_link_id;
    nmo_object_id_t insert_from_io_id;
    nmo_object_id_t insert_to_io_id;
    bool has_preserved_delay;
    uint32_t preserved_delay;
} nmo_probe_safe_insertion_t;

typedef struct nmo_probe_selector_result {
    nmo_probe_selector_mode_t mode;
    nmo_probe_selector_status_t status;
    char rejection_code[64];
    char message[256];
    nmo_object_id_t selected_node_id;
    nmo_object_id_t selected_link_id;
    nmo_object_id_t selected_operation_id;
    nmo_object_id_t from_io_id;
    nmo_object_id_t to_io_id;
    bool has_delay;
    uint32_t delay;
    nmo_probe_safe_insertion_t safe_insertion;
    nmo_probe_selector_candidate_t *candidates;
    size_t candidate_count;
    size_t candidate_capacity;
} nmo_probe_selector_result_t;

NMO_API void nmo_probe_selector_request_init(
    nmo_probe_selector_request_t *request);
NMO_API void nmo_probe_selector_result_init(
    nmo_probe_selector_result_t *result);

NMO_API const char *nmo_probe_selector_mode_name(
    nmo_probe_selector_mode_t mode);
NMO_API const char *nmo_probe_selector_status_name(
    nmo_probe_selector_status_t status);
NMO_API const char *nmo_probe_candidate_role_name(
    nmo_probe_candidate_role_t role);
NMO_API void nmo_probe_analysis_dispose(
    nmo_probe_selector_result_t *result);

NMO_API nmo_status_t nmo_probe_analyze_selector(
    nmo_workspace_t *workspace,
    const nmo_probe_selector_request_t *request,
    nmo_probe_selector_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_PROBE_ANALYZER_H */
