/**
 * @file nmo_cmd_behavior_internal.h
 * @brief Shared helpers for behavior command split files
 */

#ifndef NMO_CMD_BEHAVIOR_INTERNAL_H
#define NMO_CMD_BEHAVIOR_INTERNAL_H

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_record.h"
#include "format/nmo_interface_chunk.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "type/nmo_type_system.h"
#include "behavior/nmo_behavior_analyze.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Behavior flag constants (enum values from nmo_object_enum_defs.h) */
#include "object/nmo_object_enum_defs.h"

/**
 * @brief Check if a class ID is CKBehavior or derived from it.
 */
int is_behavior_class(const nmo_type_registry_t *registry, nmo_class_id_t class_id);

/**
 * @brief Resolve an object ID to its name.
 * Returns "(none)" for 0, "(missing)" for unknown, "(unnamed)" for empty.
 */
const char *resolve_name(nmo_object_repository_t *repo, nmo_object_id_t id);

/**
 * @brief Resolve a parameter type GUID to a human-readable name.
 */
const char *resolve_type(const nmo_type_registry_t *reg, nmo_guid_t guid);

/**
 * @brief Get the parameter type GUID from any parameter object.
 */
nmo_guid_t get_param_type_guid(nmo_object_t *obj);

/**
 * @brief Find a sub-behavior entry in interface data by behavior ID.
 */
const nmo_interface_behavior_t *find_interface_sub(
    const nmo_interface_data_t *idata, nmo_object_id_t behavior_id);

/**
 * @brief Find the position of a behavior in interface data.
 */
bool find_interface_position(const nmo_interface_data_t *idata,
                             nmo_object_id_t behavior_id,
                             float *out_x, float *out_y);

/**
 * @brief Find the position of an operation in interface data.
 */
bool find_operation_position(const nmo_interface_data_t *idata,
                             nmo_object_id_t op_id,
                             float *out_x, float *out_y);

/**
 * @brief Find a link entry in interface data by link ID.
 */
const nmo_interface_link_t *find_interface_link(
    const nmo_interface_data_t *idata, nmo_object_id_t link_id);

void nmo_cmd_behavior_add_interface_diagnostics_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    nmo_workspace_t *workspace);

void nmo_cmd_behavior_print_interface_diagnostics(
    FILE *out,
    nmo_workspace_t *workspace);

/*
 * JSON: the interface_available / interface_parse fields. Text (when
 * `show_text`): the failed-parse summary line, if an interface parse failed.
 */
bool nmo_cmd_behavior_add_interface_diagnostics(nmo_cli_record_t *rec,
                                                nmo_workspace_t *workspace,
                                                bool show_text);

/* What the behavior flow helpers read. `index` may be NULL (no owners known). */
typedef struct nmo_cmd_behavior_flow_ctx {
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *registry;
    nmo_workspace_t *workspace;
    const nmo_behavior_index_t *index;
} nmo_cmd_behavior_flow_ctx_t;

/* The graph that holds `behavior_id` as a sub-behavior, or 0. */
nmo_object_id_t nmo_cmd_behavior_parent_id(const nmo_cmd_behavior_flow_ctx_t *f,
                                           nmo_object_id_t behavior_id);

/* The operation name of an "Op" building block, or NULL for any other behavior. */
const char *nmo_cmd_behavior_op_block_name(const nmo_cmd_behavior_flow_ctx_t *f,
                                           nmo_object_id_t behavior_id);

/* "Name#id", or "Name(Operation)#id" for an Op building block. Heap string. */
char *nmo_cmd_behavior_node_label_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                      nmo_object_id_t behavior_id);

/* "Owner#id.Port" for a behavior IO. Heap string. */
char *nmo_cmd_behavior_io_label_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                    nmo_object_id_t io_id);

/*
 * A parameter as seen from inside a graph: its owner, how the graph reaches
 * it (`kind`: "local", "ancestor local", "foreign local", "pOut", "graph pOut",
 * "graph pIn", "target", "operation", "external", ...), its decoded value for
 * the kinds that hold one, and a one-line `text` such as
 * "Gameplay_Ingame#3128.ActiveBall (ancestor local = (null))".
 */
typedef struct nmo_cmd_behavior_param_ref {
    nmo_object_id_t param_id;
    nmo_object_id_t owner_id; /* 0 when no behavior or operation owns it */
    const char *kind;
    char *value;              /* heap, may be NULL */
    char *text;               /* heap */
} nmo_cmd_behavior_param_ref_t;

bool nmo_cmd_behavior_resolve_param(const nmo_cmd_behavior_flow_ctx_t *f,
                                    nmo_object_id_t graph_id,
                                    nmo_object_id_t param_id,
                                    nmo_cmd_behavior_param_ref_t *out);
void nmo_cmd_behavior_param_ref_dispose(nmo_cmd_behavior_param_ref_t *ref);

/*
 * The data flow of graph `graph_id` into `arr`: every read of a sub-behavior
 * or operation input, target parameter included, and every write of a
 * sub-behavior or operation output into a parameter other than an input.
 * Items get a text summary only when `with_text`.
 */
bool nmo_cmd_behavior_add_data_flow_items(nmo_cli_record_array_t *arr,
                                          const nmo_cmd_behavior_flow_ctx_t *f,
                                          nmo_object_id_t graph_id,
                                          bool with_text);

int nmo_cmd_behavior_graph_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_behavior_graph_boundary_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_behavior_fold_candidates_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_behavior_interface_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_BEHAVIOR_INTERNAL_H */
