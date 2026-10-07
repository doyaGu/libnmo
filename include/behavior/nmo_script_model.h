/**
 * @file nmo_script_model.h
 * @brief Read-only semantic model of every behavior graph in a document.
 */

#ifndef NMO_SCRIPT_MODEL_H
#define NMO_SCRIPT_MODEL_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The script model is a snapshot of the behaviors of a document: every
 * script, the graphs no script holds (the roots of a behavior file), their
 * sub-behaviors, IOs, parameters, parameter operations, behavior links, and
 * the parameter reads and writes inside each graph. Building it walks the
 * document once; queries after that are lookups.
 *
 * Every array the model returns is owned by the model. The strings borrow
 * the document's object names and the registries' type names, so the model
 * is valid until the document is edited or destroyed.
 *
 * Nodes address their own items by ranges: node->first_param and
 * node->param_count index the array nmo_script_model_params() returns, and
 * likewise for the other ranges.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NMO_SCRIPT_MODEL_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_MODEL_API_TIER NMO_API_TIER_ADVANCED_C

typedef struct nmo_workspace nmo_workspace_t;
typedef struct nmo_script_model nmo_script_model_t;

typedef enum nmo_script_node_kind {
    NMO_SCRIPT_NODE_SCRIPT = 0,
    NMO_SCRIPT_NODE_GRAPH,
    NMO_SCRIPT_NODE_BUILDING_BLOCK,
} nmo_script_node_kind_t;

/* A behavior: a script, a graph, or a building block. */
typedef struct nmo_script_node {
    nmo_object_id_t id;
    nmo_script_node_kind_t kind;
    const char *name;                /* "" when unnamed */
    nmo_object_id_t parent_id;       /* the graph holding it; 0 for a root */
    nmo_object_id_t owner_object_id; /* the object whose script a root is; 0 if none */
    uint32_t depth;                  /* 0 for a root */
    uint32_t flags;                  /* CKBEHAVIOR_* */
    nmo_guid_t prototype_guid;       /* building blocks only */
    const char *prototype_name;      /* NULL when the prototype is unknown */
    nmo_guid_t operation_guid;       /* the operation an "Op" building block runs */
    const char *operation_name;      /* NULL unless it is an Op building block */
    size_t first_child, child_count; /* nmo_script_model_children() */
    size_t first_io, input_count, output_count; /* inputs, then outputs */
    size_t first_param, param_count; /* target, pIns, pOuts, locals */
    size_t first_operation, operation_count;
    size_t first_link, link_count;   /* the graph's behavior links */
    size_t first_data_edge, data_edge_count; /* the graph's parameter flows */
} nmo_script_node_t;

typedef struct nmo_script_io {
    nmo_object_id_t id;
    nmo_object_id_t node_id;
    bool is_output;
    uint32_t index;
    const char *name;
} nmo_script_io_t;

typedef enum nmo_script_param_role {
    NMO_SCRIPT_PARAM_TARGET = 0,
    NMO_SCRIPT_PARAM_INPUT,
    NMO_SCRIPT_PARAM_OUTPUT,
    NMO_SCRIPT_PARAM_LOCAL,
    NMO_SCRIPT_PARAM_OPERATION_INPUT,
    NMO_SCRIPT_PARAM_OPERATION_OUTPUT,
} nmo_script_param_role_t;

/*
 * A parameter a behavior or an operation holds. A parameter two behaviors
 * list (a folded graph exporting a sub-behavior's input) has an item under
 * each; nmo_script_model_find_param() returns the one of the owner the
 * parameter itself names.
 */
typedef struct nmo_script_param {
    nmo_object_id_t id;
    nmo_object_id_t owner_id;        /* the behavior, or the operation */
    nmo_script_param_role_t role;
    uint32_t index;                  /* position among its owner's same-role parameters */
    const char *name;                /* "" when unnamed */
    nmo_class_id_t class_id;
    nmo_guid_t type_guid;
    const char *type_name;           /* "?" when unknown */
    bool is_setting;                 /* a local holding a building block setting */
    nmo_object_id_t source_id;       /* inputs: the parameter read from; 0 if none */
    bool is_shared;                  /* inputs: the source is another input */
} nmo_script_param_t;

/* A parameter operation of a graph. */
typedef struct nmo_script_operation {
    nmo_object_id_t id;
    nmo_object_id_t graph_id;
    nmo_guid_t operation_guid;
    const char *operation_name;      /* "?" when unknown */
    nmo_object_id_t input1_id;       /* 0 when absent */
    nmo_object_id_t input2_id;
    nmo_object_id_t output_id;
    size_t first_param, param_count;
} nmo_script_operation_t;

/* A behavior link: an activation from one IO to another. */
typedef struct nmo_script_link {
    nmo_object_id_t id;
    nmo_object_id_t graph_id;
    nmo_object_id_t source_io_id;
    nmo_object_id_t target_io_id;
    nmo_object_id_t source_node_id;  /* 0 when the IO is not in the model */
    nmo_object_id_t target_node_id;
    int32_t activation_delay;
    int32_t initial_activation_delay;
} nmo_script_link_t;

/* How a graph reaches a parameter. */
typedef enum nmo_script_reach {
    NMO_SCRIPT_REACH_LOCAL = 0,        /* a local of the graph */
    NMO_SCRIPT_REACH_ANCESTOR_LOCAL,   /* a local of a graph holding it */
    NMO_SCRIPT_REACH_FOREIGN_LOCAL,    /* a local of any other behavior */
    NMO_SCRIPT_REACH_GRAPH_INPUT,      /* an input of the graph or of a graph holding it */
    NMO_SCRIPT_REACH_GRAPH_OUTPUT,     /* an output of the graph */
    NMO_SCRIPT_REACH_CHILD_INPUT,      /* an input of a sub-behavior */
    NMO_SCRIPT_REACH_CHILD_OUTPUT,     /* an output of a sub-behavior */
    NMO_SCRIPT_REACH_FOREIGN_INPUT,    /* an input of any other behavior */
    NMO_SCRIPT_REACH_FOREIGN_OUTPUT,   /* an output of any other behavior */
    NMO_SCRIPT_REACH_TARGET,           /* the target parameter of a behavior */
    NMO_SCRIPT_REACH_OPERATION_INPUT,  /* an input of a parameter operation */
    NMO_SCRIPT_REACH_OPERATION_OUTPUT, /* the output of a parameter operation */
    NMO_SCRIPT_REACH_EXTERNAL,         /* a parameter no behavior or operation holds */
} nmo_script_reach_t;

typedef enum nmo_script_data_kind {
    NMO_SCRIPT_DATA_READ = 0,  /* an input reads its source */
    NMO_SCRIPT_DATA_WRITE,     /* an output writes a parameter other than an input */
} nmo_script_data_kind_t;

/*
 * A parameter flow inside a graph, from `source_id` to `target_id`. The
 * owners are the behaviors or operations the edge was found under: a folded
 * graph can list a sub-behavior's input as its own, and the edge names the
 * holder it reads into.
 */
typedef struct nmo_script_data_edge {
    nmo_object_id_t graph_id;
    nmo_script_data_kind_t kind;
    nmo_object_id_t source_id;
    nmo_object_id_t target_id;
    nmo_object_id_t source_owner_id; /* 0 for an external source */
    nmo_object_id_t target_owner_id; /* 0 for an external target */
    nmo_script_reach_t source_reach;
    nmo_script_reach_t target_reach;
    nmo_guid_t type_guid;            /* the type of the input read, or the output written */
    bool is_shared;
} nmo_script_data_edge_t;

/** Build the model of the workspace's document. Destroy it with nmo_script_model_destroy(). */
NMO_API nmo_status_t nmo_script_model_build(nmo_workspace_t *workspace,
                                            nmo_script_model_t **out_model);
NMO_API void nmo_script_model_destroy(nmo_script_model_t *model);

NMO_API const nmo_script_node_t *nmo_script_model_nodes(const nmo_script_model_t *model,
                                                        size_t *out_count);
/** Root node ids (scripts and the graphs no script holds), in document order. */
NMO_API const nmo_object_id_t *nmo_script_model_roots(const nmo_script_model_t *model,
                                                      size_t *out_count);
/** Child node ids; a node's children are [first_child, first_child + child_count). */
NMO_API const nmo_object_id_t *nmo_script_model_children(const nmo_script_model_t *model,
                                                         size_t *out_count);
NMO_API const nmo_script_io_t *nmo_script_model_ios(const nmo_script_model_t *model,
                                                    size_t *out_count);
NMO_API const nmo_script_param_t *nmo_script_model_params(const nmo_script_model_t *model,
                                                          size_t *out_count);
NMO_API const nmo_script_operation_t *nmo_script_model_operations(
    const nmo_script_model_t *model, size_t *out_count);
NMO_API const nmo_script_link_t *nmo_script_model_links(const nmo_script_model_t *model,
                                                        size_t *out_count);
NMO_API const nmo_script_data_edge_t *nmo_script_model_data_edges(
    const nmo_script_model_t *model, size_t *out_count);

/* Lookups by object id; NULL when the model does not hold the object. */
NMO_API const nmo_script_node_t *nmo_script_model_find_node(const nmo_script_model_t *model,
                                                            nmo_object_id_t id);
NMO_API const nmo_script_io_t *nmo_script_model_find_io(const nmo_script_model_t *model,
                                                        nmo_object_id_t id);
NMO_API const nmo_script_param_t *nmo_script_model_find_param(const nmo_script_model_t *model,
                                                              nmo_object_id_t id);
NMO_API const nmo_script_operation_t *nmo_script_model_find_operation(
    const nmo_script_model_t *model, nmo_object_id_t id);

/**
 * The links leaving IO `io_id`, as `*out_count` pointers into
 * nmo_script_model_links(). NULL with a count of 0 when there are none.
 */
NMO_API const nmo_script_link_t *const *nmo_script_model_links_from_io(
    const nmo_script_model_t *model, nmo_object_id_t io_id, size_t *out_count);

/**
 * The data edges whose source is parameter `param_id` (the inputs reading
 * it and the parameters it writes), as pointers into
 * nmo_script_model_data_edges().
 */
NMO_API const nmo_script_data_edge_t *const *nmo_script_model_param_uses(
    const nmo_script_model_t *model, nmo_object_id_t param_id, size_t *out_count);

/** How graph `graph_id` reaches parameter `param_id`. */
NMO_API nmo_script_reach_t nmo_script_model_reach(const nmo_script_model_t *model,
                                                  nmo_object_id_t graph_id,
                                                  nmo_object_id_t param_id);

/** True when `ancestor_id` holds `node_id`, directly or through graphs. */
NMO_API bool nmo_script_model_is_ancestor(const nmo_script_model_t *model,
                                          nmo_object_id_t ancestor_id,
                                          nmo_object_id_t node_id);

/**
 * The decoded value of a parameter holding one (a local, an output, or a
 * plain parameter; not an input), into `buffer`. NMO_ERR_NOT_FOUND when the
 * parameter has no value.
 */
NMO_API nmo_status_t nmo_script_model_param_value(const nmo_script_model_t *model,
                                                  nmo_object_id_t param_id,
                                                  char *buffer,
                                                  size_t buffer_size);

/**
 * "Name#id" for a node, "Name(Operation)#id" for an Op building block, and
 * "Operation#id" for a parameter operation, into `buffer`. Returns the
 * length the full label needs, as snprintf() does.
 */
NMO_API size_t nmo_script_model_label(const nmo_script_model_t *model,
                                      nmo_object_id_t id,
                                      char *buffer,
                                      size_t buffer_size);

NMO_API const char *nmo_script_node_kind_name(nmo_script_node_kind_t kind);
NMO_API const char *nmo_script_param_role_name(nmo_script_param_role_t role);
/** "local", "ancestor local", "foreign local", "graph pIn", "pOut", ... */
NMO_API const char *nmo_script_reach_name(nmo_script_reach_t reach);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_MODEL_H */
