#include "test_framework.h"

#include "behavior/nmo_script_model.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_document.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "../../src/runtime/runtime_internal.h"

#include <stdint.h>
#include <string.h>

static nmo_object_id_t test_create_object(nmo_session_t *session,
                                          nmo_class_id_t class_id,
                                          const char *name)
{
    nmo_object_id_t id = 0;
    if (nmo_session_create_object(session, class_id, name, (nmo_guid_t){0, 0}, &id, NULL) !=
        NMO_OK) {
        return 0;
    }
    return id;
}

static void *test_state(nmo_session_t *session, nmo_object_id_t id)
{
    nmo_object_t *object = nmo_object_repository_find_by_id(nmo_session_get_repository(session), id);
    return object != NULL ? nmo_object_get_state(object) : NULL;
}

static void test_append_id(nmo_array_t *array, nmo_object_id_t id)
{
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(array, id, NULL));
}

static nmo_status_t test_open_workspace(const char *path,
                                        nmo_context_t **out_ctx,
                                        nmo_session_t **out_session,
                                        nmo_document_t **out_document,
                                        nmo_workspace_t **out_workspace)
{
    char errbuf[256] = {0};
    if (!nmo_session_open_file_with_context(path, out_ctx, out_session, errbuf,
                                            sizeof(errbuf))) {
        return NMO_ERR_CANT_OPEN_FILE;
    }
    nmo_status_t st = nmo_session_borrow_document(*out_session, out_document);
    if (st == NMO_OK) {
        st = nmo_workspace_create(*out_ctx, *out_document, out_workspace);
    }
    return st;
}

static void test_close_workspace(nmo_context_t *ctx, nmo_session_t *session,
                                 nmo_document_t *document, nmo_workspace_t *workspace)
{
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

/* The data edge of graph `graph_id` from `source_id` to `target_id`, or NULL. */
static const nmo_script_data_edge_t *test_find_edge(const nmo_script_model_t *model,
                                                    nmo_object_id_t graph_id,
                                                    nmo_object_id_t source_id,
                                                    nmo_object_id_t target_id)
{
    const nmo_script_node_t *graph = nmo_script_model_find_node(model, graph_id);
    size_t count = 0;
    const nmo_script_data_edge_t *edges = nmo_script_model_data_edges(model, &count);
    for (size_t i = 0; graph != NULL && i < graph->data_edge_count; i++) {
        const nmo_script_data_edge_t *edge = &edges[graph->first_data_edge + i];
        if (edge->source_id == source_id && edge->target_id == target_id) {
            return edge;
        }
    }
    return NULL;
}

TEST(script_model, models_a_graph_no_script_holds) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t graph = test_create_object(session, NMO_CID_BEHAVIOR, "Graph");
    nmo_object_id_t child = test_create_object(session, NMO_CID_BEHAVIOR, "Child");
    nmo_object_id_t graph_in = test_create_object(session, NMO_CID_BEHAVIORIO, "Start");
    nmo_object_id_t child_in = test_create_object(session, NMO_CID_BEHAVIORIO, "In");
    nmo_object_id_t child_out = test_create_object(session, NMO_CID_BEHAVIORIO, "Out");
    nmo_object_id_t local = test_create_object(session, NMO_CID_PARAMETERLOCAL, "Speed");
    nmo_object_id_t pin = test_create_object(session, NMO_CID_PARAMETERIN, "Value");
    nmo_object_id_t link = test_create_object(session, NMO_CID_BEHAVIORLINK, "Link");
    ASSERT_TRUE(graph && child && graph_in && child_in && child_out && local && pin && link);

    nmo_behavior_state_t *graph_state = (nmo_behavior_state_t *)test_state(session, graph);
    nmo_behavior_state_t *child_state = (nmo_behavior_state_t *)test_state(session, child);
    nmo_behaviorlink_state_t *link_state = (nmo_behaviorlink_state_t *)test_state(session, link);
    nmo_parameterin_state_t *pin_state = (nmo_parameterin_state_t *)test_state(session, pin);
    ASSERT_NOT_NULL(graph_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(link_state);
    ASSERT_NOT_NULL(pin_state);

    child_state->flags |= CKBEHAVIOR_BUILDINGBLOCK;
    test_append_id(&graph_state->sub_behaviors, child);
    test_append_id(&graph_state->inputs, graph_in);
    test_append_id(&graph_state->local_parameters, local);
    test_append_id(&graph_state->sub_behavior_links, link);
    test_append_id(&child_state->inputs, child_in);
    test_append_id(&child_state->outputs, child_out);
    test_append_id(&child_state->in_parameters, pin);
    nmo_parameterin_set_source_id(pin_state, local);
    nmo_behaviorlink_set_in_io_id(link_state, graph_in);
    nmo_behaviorlink_set_out_io_id(link_state, child_in);
    link_state->activation_delay = 2;

    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_script_model_t *model = NULL;
    ASSERT_EQ(NMO_OK, nmo_script_model_build(workspace, &model));

    /* The graph is a root although no object holds it as a script */
    size_t root_count = 0;
    const nmo_object_id_t *roots = nmo_script_model_roots(model, &root_count);
    ASSERT_EQ(1u, root_count);
    ASSERT_EQ(graph, roots[0]);

    const nmo_script_node_t *g = nmo_script_model_find_node(model, graph);
    const nmo_script_node_t *c = nmo_script_model_find_node(model, child);
    ASSERT_NOT_NULL(g);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(NMO_SCRIPT_NODE_GRAPH, g->kind);
    ASSERT_EQ(NMO_SCRIPT_NODE_BUILDING_BLOCK, c->kind);
    ASSERT_EQ(graph, c->parent_id);
    ASSERT_EQ(1u, c->depth);
    ASSERT_EQ(1u, g->child_count);
    ASSERT_EQ(1u, c->input_count);
    ASSERT_EQ(1u, c->output_count);
    ASSERT_TRUE(nmo_script_model_is_ancestor(model, graph, child));
    ASSERT_FALSE(nmo_script_model_is_ancestor(model, child, graph));

    /* The link and its endpoints' nodes */
    size_t link_count = 0;
    const nmo_script_link_t *const *from_start =
        nmo_script_model_links_from_io(model, graph_in, &link_count);
    ASSERT_EQ(1u, link_count);
    ASSERT_EQ(link, from_start[0]->id);
    ASSERT_EQ(graph, from_start[0]->source_node_id);
    ASSERT_EQ(child, from_start[0]->target_node_id);
    ASSERT_EQ(2, from_start[0]->activation_delay);
    nmo_script_model_links_from_io(model, child_out, &link_count);
    ASSERT_EQ(0u, link_count);

    /* The child's input reads the graph's local */
    const nmo_script_param_t *p = nmo_script_model_find_param(model, pin);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(NMO_SCRIPT_PARAM_INPUT, p->role);
    ASSERT_EQ(child, p->owner_id);
    ASSERT_EQ(local, p->source_id);
    ASSERT_EQ(NMO_SCRIPT_REACH_LOCAL, nmo_script_model_reach(model, graph, local));
    ASSERT_EQ(NMO_SCRIPT_REACH_ANCESTOR_LOCAL, nmo_script_model_reach(model, child, local));
    const nmo_script_data_edge_t *read = test_find_edge(model, graph, local, pin);
    ASSERT_NOT_NULL(read);
    ASSERT_EQ(NMO_SCRIPT_DATA_READ, read->kind);
    ASSERT_EQ(graph, read->source_owner_id);
    ASSERT_EQ(child, read->target_owner_id);
    ASSERT_EQ(NMO_SCRIPT_REACH_LOCAL, read->source_reach);
    ASSERT_EQ(NMO_SCRIPT_REACH_CHILD_INPUT, read->target_reach);
    size_t use_count = 0;
    const nmo_script_data_edge_t *const *uses =
        nmo_script_model_param_uses(model, local, &use_count);
    ASSERT_EQ(1u, use_count);
    ASSERT_TRUE(uses[0] == read);

    char label[64];
    nmo_script_model_label(model, child, label, sizeof(label));
    char expected[64];
    snprintf(expected, sizeof(expected), "Child#%u", (unsigned)child);
    ASSERT_STR_EQ(expected, label);
    ASSERT_STR_EQ("local", nmo_script_reach_name(read->source_reach));

    nmo_script_model_destroy(model);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(script_model, models_ballance_base_scripts) {
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, test_open_workspace(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                                          &ctx, &session, &document, &workspace));
    nmo_script_model_t *model = NULL;
    ASSERT_EQ(NMO_OK, nmo_script_model_build(workspace, &model));

    /* Loading_Manager #363 is a script; Register & Activate Init_Script #237 a graph in it */
    const nmo_script_node_t *script = nmo_script_model_find_node(model, 363);
    const nmo_script_node_t *graph = nmo_script_model_find_node(model, 237);
    ASSERT_NOT_NULL(script);
    ASSERT_NOT_NULL(graph);
    ASSERT_EQ(NMO_SCRIPT_NODE_SCRIPT, script->kind);
    ASSERT_TRUE(script->owner_object_id != 0);
    ASSERT_EQ(NMO_SCRIPT_NODE_GRAPH, graph->kind);
    ASSERT_EQ(363u, graph->parent_id);

    /* Op #160 runs Subtraction; its name needs the Virtools data the CLI loads */
    const nmo_script_node_t *op = nmo_script_model_find_node(model, 160);
    ASSERT_NOT_NULL(op);
    ASSERT_NOT_NULL(op->operation_name);
    ASSERT_EQ(0x67641171u, op->operation_guid.d1);
    ASSERT_EQ(0x6499077Au, op->operation_guid.d2);
    char label[64];
    nmo_script_model_label(model, 160, label, sizeof(label));
    ASSERT_TRUE(strncmp(label, "Op(", 3) == 0);

    /* A sub-behavior output writes a local: TT List ScreenModes #411 into Screen Modes #660 */
    const nmo_script_data_edge_t *write = test_find_edge(model, 660, 410, 409);
    ASSERT_NOT_NULL(write);
    ASSERT_EQ(NMO_SCRIPT_DATA_WRITE, write->kind);
    ASSERT_EQ(411u, write->source_owner_id);
    ASSERT_EQ(NMO_SCRIPT_REACH_CHILD_OUTPUT, write->source_reach);
    ASSERT_EQ(NMO_SCRIPT_REACH_LOCAL, write->target_reach);

    /* Every graph's data edges stay inside its range */
    size_t node_count = 0, edge_count = 0;
    const nmo_script_node_t *nodes = nmo_script_model_nodes(model, &node_count);
    const nmo_script_data_edge_t *edges = nmo_script_model_data_edges(model, &edge_count);
    size_t total = 0;
    for (size_t i = 0; i < node_count; i++) {
        for (size_t e = 0; e < nodes[i].data_edge_count; e++) {
            ASSERT_EQ(nodes[i].id, edges[nodes[i].first_data_edge + e].graph_id);
        }
        total += nodes[i].data_edge_count;
    }
    ASSERT_EQ(edge_count, total);

    char value[64];
    ASSERT_EQ(NMO_OK, nmo_script_model_param_value(model, write->target_id, value,
                                                   sizeof(value)));

    nmo_script_model_destroy(model);
    test_close_workspace(ctx, session, document, workspace);
}

TEST(script_model, names_the_holder_of_an_exported_input) {
    TEST_REQUIRE_FIXTURE("Ballance/Menu.nmo");
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, test_open_workspace(NMO_TEST_DATA_FILE("Ballance/Menu.nmo"),
                                          &ctx, &session, &document, &workspace));
    nmo_script_model_t *model = NULL;
    ASSERT_EQ(NMO_OK, nmo_script_model_build(workspace, &model));

    /*
     * Folded graph Secure Key #464 lists Key Event #434's input #432 as its own.
     * Read in Keyboard #541 it is the graph's input; inside #464, Key Event's.
     */
    const nmo_script_param_t *pin = nmo_script_model_find_param(model, 432);
    ASSERT_NOT_NULL(pin);
    const nmo_script_data_edge_t *outer = test_find_edge(model, 541, pin->source_id, 432);
    const nmo_script_data_edge_t *inner = test_find_edge(model, 464, pin->source_id, 432);
    ASSERT_NOT_NULL(outer);
    ASSERT_NOT_NULL(inner);
    ASSERT_EQ(464u, outer->target_owner_id);
    ASSERT_EQ(434u, inner->target_owner_id);
    ASSERT_EQ(NMO_SCRIPT_REACH_CHILD_INPUT, inner->target_reach);
    ASSERT_EQ(NMO_SCRIPT_REACH_ANCESTOR_LOCAL, inner->source_reach);

    nmo_script_model_destroy(model);
    test_close_workspace(ctx, session, document, workspace);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_model, models_a_graph_no_script_holds);
    REGISTER_TEST(script_model, models_ballance_base_scripts);
    REGISTER_TEST(script_model, names_the_holder_of_an_exported_input);
TEST_MAIN_END()
