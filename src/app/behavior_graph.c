#include "app/nmo_behavior_graph.h"

#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "app/nmo_type_query.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_types.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct nmo_behavior_graph_io_owner {
    nmo_object_id_t io_id;
    nmo_object_id_t behavior_id;
    const char *behavior_name;
    bool is_output;
} nmo_behavior_graph_io_owner_t;

static bool is_behavior_class(nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return false;
    }
    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR) ? true : false;
}

static bool add_unique_id(nmo_object_id_t **ids, size_t *count, size_t *cap, nmo_object_id_t id) {
    if (id == 0 || !ids || !count || !cap) {
        return true;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*ids)[i] == id) {
            return true;
        }
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_object_id_t *new_ids = (nmo_object_id_t *)realloc(*ids, new_cap * sizeof(*new_ids));
        if (!new_ids) {
            return false;
        }
        *ids = new_ids;
        *cap = new_cap;
    }
    (*ids)[(*count)++] = id;
    return true;
}

static bool add_io_owner(nmo_behavior_graph_io_owner_t **owners,
                         size_t *count,
                         size_t *cap,
                         nmo_object_id_t io_id,
                         nmo_object_t *behavior,
                         bool is_output) {
    if (io_id == 0 || !owners || !count || !cap || !behavior) {
        return true;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*owners)[i].io_id == io_id) {
            return true;
        }
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_behavior_graph_io_owner_t *new_arr =
            (nmo_behavior_graph_io_owner_t *)realloc(*owners, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return false;
        }
        *owners = new_arr;
        *cap = new_cap;
    }
    (*owners)[*count] = (nmo_behavior_graph_io_owner_t){
        .io_id = io_id,
        .behavior_id = nmo_object_get_id(behavior),
        .behavior_name = nmo_object_get_name(behavior),
        .is_output = is_output,
    };
    (*count)++;
    return true;
}

static const nmo_behavior_graph_io_owner_t *find_io_owner(const nmo_behavior_graph_io_owner_t *owners,
                                                          size_t count,
                                                          nmo_object_id_t io_id) {
    if (!owners || io_id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (owners[i].io_id == io_id) {
            return &owners[i];
        }
    }
    return NULL;
}

static bool collect_behavior_ids(const nmo_behavior_state_t *state,
                                 nmo_object_id_t root_id,
                                 nmo_object_id_t **out_ids,
                                 size_t *out_count) {
    if (!state || !out_ids || !out_count) {
        return false;
    }

    size_t cap = 0;
    size_t count = 0;
    nmo_object_id_t *ids = NULL;

    if (!add_unique_id(&ids, &count, &cap, root_id)) {
        free(ids);
        return false;
    }

    const nmo_object_id_t *sub_ids = NMO_ARRAY_DATA(nmo_object_id_t, &state->sub_behaviors);
    for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
        if (!add_unique_id(&ids, &count, &cap, sub_ids[i])) {
            free(ids);
            return false;
        }
    }

    *out_ids = ids;
    *out_count = count;
    return true;
}

static bool collect_behavior_io_owners(nmo_type_registry_t *registry,
                                       nmo_object_repository_t *repo,
                                       const nmo_object_id_t *behavior_ids,
                                       size_t behavior_count,
                                       nmo_behavior_graph_io_owner_t **out_owners,
                                       size_t *out_owner_count) {
    if (!registry || !repo || !out_owners || !out_owner_count) {
        return false;
    }

    nmo_behavior_graph_io_owner_t *owners = NULL;
    size_t count = 0;
    size_t cap = 0;

    for (size_t i = 0; i < behavior_count; ++i) {
        nmo_object_t *behavior = nmo_object_repository_find_by_id(repo, behavior_ids[i]);
        if (!behavior) {
            continue;
        }
        if (!nmo_type_query_object_is_derived_from_guid(registry, behavior, CKPGUID_BEHAVIOR)) {
            continue;
        }

        const nmo_behavior_state_t *state =
            (const nmo_behavior_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                registry, behavior, CKPGUID_BEHAVIOR);
        if (!state) {
            continue;
        }

        const nmo_object_id_t *inputs = NMO_ARRAY_DATA(nmo_object_id_t, &state->inputs);
        const nmo_object_id_t *outputs = NMO_ARRAY_DATA(nmo_object_id_t, &state->outputs);

        for (size_t j = 0; j < state->inputs.count; ++j) {
            if (!add_io_owner(&owners, &count, &cap, inputs[j], behavior, false)) {
                free(owners);
                return false;
            }
        }

        for (size_t j = 0; j < state->outputs.count; ++j) {
            if (!add_io_owner(&owners, &count, &cap, outputs[j], behavior, true)) {
                free(owners);
                return false;
            }
        }
    }

    *out_owners = owners;
    *out_owner_count = count;
    return true;
}

static bool add_graph_node(nmo_behavior_graph_node_t **nodes,
                           size_t *count,
                           size_t *cap,
                           nmo_object_id_t id,
                           const char *kind,
                           const char *name,
                           bool owns_name,
                           nmo_class_id_t class_id,
                           const char *class_name) {
    if (!nodes || !count || !cap || id == 0) {
        return true;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*nodes)[i].id == id) {
            if (kind && (*nodes)[i].kind && strcmp((*nodes)[i].kind, "unknown") == 0) {
                (*nodes)[i].kind = kind;
            }
            if (class_id != 0 && (*nodes)[i].class_id == 0) {
                (*nodes)[i].class_id = class_id;
                (*nodes)[i].class_name = class_name;
            }
            if (name && name[0] && (!(*nodes)[i].name || !(*nodes)[i].name[0])) {
                (*nodes)[i].name = name;
                (*nodes)[i].owns_name = owns_name;
            } else if (owns_name && name && name[0] && (*nodes)[i].owns_name) {
                free((void *)((*nodes)[i].name));
                (*nodes)[i].name = name;
                (*nodes)[i].owns_name = owns_name;
            }
            return true;
        }
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_behavior_graph_node_t *new_nodes =
            (nmo_behavior_graph_node_t *)realloc(*nodes, new_cap * sizeof(*new_nodes));
        if (!new_nodes) {
            return false;
        }
        *nodes = new_nodes;
        *cap = new_cap;
    }
    (*nodes)[*count] = (nmo_behavior_graph_node_t){
        .id = id,
        .kind = kind,
        .name = name,
        .owns_name = owns_name,
        .class_id = class_id,
        .class_name = class_name,
    };
    (*count)++;
    return true;
}

static bool add_graph_edge(nmo_behavior_graph_edge_t **edges,
                           size_t *count,
                           size_t *cap,
                           nmo_object_id_t link_id,
                           nmo_object_id_t from_id,
                           nmo_object_id_t to_id,
                           const char *kind,
                           const char *field_path,
                           nmo_object_id_t in_io_id,
                           nmo_object_id_t out_io_id,
                           int32_t activation_delay,
                           int32_t initial_activation_delay,
                           bool is_shared) {
    if (!edges || !count || !cap || from_id == 0 || to_id == 0) {
        return true;
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_behavior_graph_edge_t *new_edges =
            (nmo_behavior_graph_edge_t *)realloc(*edges, new_cap * sizeof(*new_edges));
        if (!new_edges) {
            return false;
        }
        *edges = new_edges;
        *cap = new_cap;
    }
    (*edges)[*count] = (nmo_behavior_graph_edge_t){
        .link_id = link_id,
        .from_id = from_id,
        .to_id = to_id,
        .kind = kind,
        .field_path = field_path,
        .in_io_id = in_io_id,
        .out_io_id = out_io_id,
        .activation_delay = activation_delay,
        .initial_activation_delay = initial_activation_delay,
        .is_shared = is_shared,
    };
    (*count)++;
    return true;
}

static bool add_graph_node_from_object(nmo_behavior_graph_node_t **nodes,
                                       size_t *node_count,
                                       size_t *node_cap,
                                       nmo_object_repository_t *repo,
                                       nmo_context_t *ctx,
                                       nmo_object_id_t id,
                                       const char *kind,
                                       const char *missing_prefix,
                                       size_t *missing_count) {
    if (!repo || !ctx || id == 0) {
        return true;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) {
        if (missing_count) {
            (*missing_count)++;
        }
        char label_buf[64];
        snprintf(label_buf, sizeof(label_buf), "%s %u", missing_prefix, id);
        size_t label_len = strlen(label_buf);
        char *label_copy = (char *)malloc(label_len + 1);
        if (!label_copy) {
            return false;
        }
        memcpy(label_copy, label_buf, label_len + 1);
        if (!add_graph_node(nodes, node_count, node_cap, id, kind, label_copy, true, 0, NULL)) {
            free(label_copy);
            return false;
        }
        return true;
    }

    const char *name = nmo_object_get_name(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *class_name = nmo_type_query_class_name_from_id(ctx, class_id);
    const char *label = (name && name[0]) ? name : class_name;
    return add_graph_node(nodes, node_count, node_cap, id, kind, label, false, class_id, class_name);
}

static bool add_parameter_edge(nmo_object_id_t **param_ids,
                               size_t *param_count,
                               size_t *param_cap,
                               nmo_behavior_graph_node_t **nodes,
                               size_t *node_count,
                               size_t *node_cap,
                               nmo_behavior_graph_edge_t **edges,
                               size_t *edge_count,
                               size_t *edge_cap,
                               nmo_object_repository_t *repo,
                               nmo_context_t *ctx,
                               nmo_object_id_t param_id,
                               nmo_object_id_t from_id,
                               nmo_object_id_t to_id,
                               const char *edge_kind,
                               const char *field_path,
                               size_t *missing_nodes,
                               bool is_shared) {
    if (!add_unique_id(param_ids, param_count, param_cap, param_id)) {
        return false;
    }
    if (!add_graph_node_from_object(nodes, node_count, node_cap,
                                    repo, ctx, param_id, "parameter",
                                    "Param", missing_nodes)) {
        return false;
    }
    if (!add_graph_edge(edges, edge_count, edge_cap,
                        0, from_id, to_id, edge_kind, field_path,
                        0, 0, 0, 0, is_shared)) {
        return false;
    }
    return true;
}

static void free_graph_nodes(nmo_behavior_graph_node_t *nodes, size_t count) {
    if (!nodes) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (nodes[i].owns_name) {
            free((void *)nodes[i].name);
        }
    }
    free(nodes);
}

bool nmo_behavior_graph_build(nmo_context_t *ctx,
                              nmo_session_t *session,
                              nmo_object_id_t behavior_id,
                              nmo_behavior_graph_t *out_graph) {
    if (!ctx || !session || behavior_id == 0 || !out_graph) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid behavior graph arguments");
        return false;
    }

    memset(out_graph, 0, sizeof(*out_graph));

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!registry || !repo) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Missing registry or repository");
        return false;
    }

    nmo_object_t *behavior = nmo_object_repository_find_by_id(repo, behavior_id);
    if (!behavior) {
        NMO_SET_LAST_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Behavior %u not found", behavior_id);
        return false;
    }
    if (!is_behavior_class(registry, nmo_object_get_class_id(behavior))) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Object %u is not a behavior", behavior_id);
        return false;
    }

    const nmo_behavior_state_t *behavior_state =
        (const nmo_behavior_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            registry, behavior, CKPGUID_BEHAVIOR);
    if (!behavior_state) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Behavior state unavailable");
        return false;
    }

    nmo_object_id_t *behavior_ids = NULL;
    size_t behavior_count = 0;
    nmo_behavior_graph_io_owner_t *io_owners = NULL;
    size_t io_owner_count = 0;
    nmo_behavior_graph_node_t *nodes = NULL;
    size_t node_count = 0;
    size_t node_cap = 0;
    nmo_behavior_graph_edge_t *edges = NULL;
    size_t edge_count = 0;
    size_t edge_cap = 0;
    nmo_object_id_t *parameter_ids = NULL;
    size_t parameter_count = 0;
    size_t parameter_cap = 0;
    size_t broken_links = 0;
    size_t missing_nodes = 0;

    if (!collect_behavior_ids(behavior_state, behavior_id, &behavior_ids, &behavior_count)) {
        goto fail_nomem;
    }
    if (!collect_behavior_io_owners(registry, repo, behavior_ids, behavior_count, &io_owners, &io_owner_count)) {
        goto fail_nomem;
    }

    for (size_t i = 0; i < behavior_count; ++i) {
        if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                        repo, ctx, behavior_ids[i], "behavior",
                                        "Behavior", &missing_nodes)) {
            goto fail_nomem;
        }
    }

    const nmo_object_id_t *link_ids = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->sub_behavior_links);
    size_t link_count = behavior_state->sub_behavior_links.count;
    for (size_t i = 0; i < link_count; ++i) {
        nmo_object_id_t link_id = link_ids[i];
        if (link_id == 0) {
            continue;
        }

        nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
        const nmo_behaviorlink_state_t *link_state = NULL;
        if (link_obj && nmo_type_query_object_is_derived_from_guid(registry, link_obj, CKPGUID_BEHAVIORLINK)) {
            link_state = (const nmo_behaviorlink_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                registry, link_obj, CKPGUID_BEHAVIORLINK);
        }

        if (!link_state) {
            broken_links++;
            continue;
        }

        const nmo_behavior_graph_io_owner_t *in_owner =
            find_io_owner(io_owners, io_owner_count, link_state->in_io_id);
        const nmo_behavior_graph_io_owner_t *out_owner =
            find_io_owner(io_owners, io_owner_count, link_state->out_io_id);

        if (in_owner && out_owner) {
            if (!add_graph_edge(&edges, &edge_count, &edge_cap, link_id,
                                out_owner->behavior_id, in_owner->behavior_id,
                                "behavior_link", "sub_behavior_links",
                                link_state->in_io_id, link_state->out_io_id,
                                link_state->activation_delay, link_state->initial_activation_delay, false)) {
                goto fail_nomem;
            }
        } else if (link_state->in_io_id != 0 && link_state->out_io_id != 0) {
            if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                            repo, ctx, link_state->in_io_id, "io", "IO", &missing_nodes)) {
                goto fail_nomem;
            }
            if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                            repo, ctx, link_state->out_io_id, "io", "IO", &missing_nodes)) {
                goto fail_nomem;
            }
            if (!add_graph_edge(&edges, &edge_count, &edge_cap, link_id,
                                link_state->out_io_id, link_state->in_io_id,
                                "io_link", "sub_behavior_links",
                                link_state->in_io_id, link_state->out_io_id, 0, 0, false)) {
                goto fail_nomem;
            }
        }
    }

    const nmo_object_id_t *in_params = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->in_parameters);
    for (size_t i = 0; i < behavior_state->in_parameters.count; ++i) {
        if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                &nodes, &node_count, &node_cap,
                                &edges, &edge_count, &edge_cap,
                                repo, ctx, in_params[i], behavior_id, in_params[i],
                                "param_in", "in_parameters", &missing_nodes, false)) {
            goto fail_nomem;
        }
    }

    const nmo_object_id_t *out_params = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->out_parameters);
    for (size_t i = 0; i < behavior_state->out_parameters.count; ++i) {
        if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                &nodes, &node_count, &node_cap,
                                &edges, &edge_count, &edge_cap,
                                repo, ctx, out_params[i], behavior_id, out_params[i],
                                "param_out", "out_parameters", &missing_nodes, false)) {
            goto fail_nomem;
        }
    }

    const nmo_object_id_t *local_params = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->local_parameters);
    for (size_t i = 0; i < behavior_state->local_parameters.count; ++i) {
        if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                &nodes, &node_count, &node_cap,
                                &edges, &edge_count, &edge_cap,
                                repo, ctx, local_params[i], behavior_id, local_params[i],
                                "param_local", "local_parameters", &missing_nodes, false)) {
            goto fail_nomem;
        }
    }

    const nmo_object_id_t *ops = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->operations);
    for (size_t i = 0; i < behavior_state->operations.count; ++i) {
        nmo_object_id_t op_id = ops[i];
        if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                        repo, ctx, op_id, "operation", "Operation", &missing_nodes)) {
            goto fail_nomem;
        }

        nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, op_id);
        if (!op_obj || !nmo_type_query_object_is_derived_from_guid(registry, op_obj, CKPGUID_PARAMETEROPERATION)) {
            continue;
        }

        const nmo_parameteroperation_state_t *op_state =
            (const nmo_parameteroperation_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                registry, op_obj, CKPGUID_PARAMETEROPERATION);
        if (!op_state) {
            continue;
        }

        if (op_state->has_in1 && op_state->in1_id != 0) {
            if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                    &nodes, &node_count, &node_cap,
                                    &edges, &edge_count, &edge_cap,
                                    repo, ctx, op_state->in1_id, op_state->in1_id, op_id,
                                    "op_in1", "in1_id", &missing_nodes, false)) {
                goto fail_nomem;
            }
        }

        if (op_state->has_in2 && op_state->in2_id != 0) {
            if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                    &nodes, &node_count, &node_cap,
                                    &edges, &edge_count, &edge_cap,
                                    repo, ctx, op_state->in2_id, op_state->in2_id, op_id,
                                    "op_in2", "in2_id", &missing_nodes, false)) {
                goto fail_nomem;
            }
        }

        if (op_state->has_out && op_state->out_id != 0) {
            if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                    &nodes, &node_count, &node_cap,
                                    &edges, &edge_count, &edge_cap,
                                    repo, ctx, op_state->out_id, op_id, op_state->out_id,
                                    "op_out", "out_id", &missing_nodes, false)) {
                goto fail_nomem;
            }
        }
    }

    for (size_t i = 0; i < parameter_count; ++i) {
        nmo_object_id_t param_id = parameter_ids[i];
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
        if (!param_obj) {
            continue;
        }

        if (nmo_type_query_object_is_derived_from_guid(registry, param_obj, CKPGUID_PARAMETEROUT)) {
            const nmo_parameterout_state_t *out_state =
                (const nmo_parameterout_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, param_obj, CKPGUID_PARAMETEROUT);
            if (out_state && out_state->destination_ids && out_state->destination_count > 0) {
                for (uint32_t d = 0; d < out_state->destination_count; ++d) {
                    nmo_object_id_t dest_id = out_state->destination_ids[d];
                    if (dest_id == 0) {
                        continue;
                    }
                    if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                            &nodes, &node_count, &node_cap,
                                            &edges, &edge_count, &edge_cap,
                                            repo, ctx, dest_id, param_id, dest_id,
                                            "param_dest", "destination_ids",
                                            &missing_nodes, false)) {
                        goto fail_nomem;
                    }
                }
            }
        }

        if (nmo_type_query_object_is_derived_from_guid(registry, param_obj, CKPGUID_PARAMETERIN)) {
            const nmo_parameterin_state_t *in_state =
                (const nmo_parameterin_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, param_obj, CKPGUID_PARAMETERIN);
            if (in_state && in_state->source_id != 0) {
                if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                        &nodes, &node_count, &node_cap,
                                        &edges, &edge_count, &edge_cap,
                                        repo, ctx, in_state->source_id, in_state->source_id, param_id,
                                        "param_source", "source_id",
                                        &missing_nodes, in_state->is_shared != 0)) {
                    goto fail_nomem;
                }
            }
        }
    }

    out_graph->behavior_id = behavior_id;
    out_graph->behavior_name = nmo_object_get_name(behavior);
    out_graph->behavior_class_id = nmo_object_get_class_id(behavior);
    out_graph->behavior_class_name = nmo_type_query_class_name_from_id(ctx, out_graph->behavior_class_id);
    out_graph->nodes = nodes;
    out_graph->node_count = node_count;
    out_graph->edges = edges;
    out_graph->edge_count = edge_count;
    out_graph->broken_links = broken_links;
    out_graph->missing_nodes = missing_nodes;

    free(io_owners);
    free(behavior_ids);
    free(parameter_ids);
    nmo_last_error_clear();
    return true;

fail_nomem:
    free(io_owners);
    free(behavior_ids);
    free(parameter_ids);
    free(edges);
    free_graph_nodes(nodes, node_count);
    NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Out of memory while building behavior graph");
    return false;
}

void nmo_behavior_graph_free(nmo_behavior_graph_t *graph) {
    if (!graph) {
        return;
    }
    free(graph->edges);
    free_graph_nodes(graph->nodes, graph->node_count);
    memset(graph, 0, sizeof(*graph));
}
