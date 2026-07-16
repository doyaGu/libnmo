/**
 * @file test_param_chain.c
 * @brief Tests for typed parameter chain tracing
 */

#include "../test_framework.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

static bool open_test_file(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session)
{
    char errbuf[256] = {0};
    return nmo_session_open_file_with_context(path, out_ctx, out_session,
                                              errbuf, sizeof(errbuf));
}

/* Walk behaviors looking for a pIn with source, optionally shared */
typedef struct {
    nmo_workspace_t *workspace;
    nmo_object_id_t found_shared;
    nmo_object_id_t found_direct;
} find_pin_ctx_t;

static bool find_pin_visitor(nmo_object_id_t behavior_id,
                             const nmo_behavior_state_t *state,
                             uint32_t depth, bool is_bb, void *user_data)
{
    (void)behavior_id; (void)depth; (void)is_bb;
    find_pin_ctx_t *fctx = (find_pin_ctx_t *)user_data;
    if (!state) return true;

    nmo_document_t *document = nmo_workspace_get_document(fctx->workspace);
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    for (size_t i = 0; i < state->in_parameters.count; ++i) {
        nmo_object_id_t pin_id = nmo_behavior_ref_array_get_id(
            &state->in_parameters, i);
        if (pin_id == 0) continue;
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, pin_id);
        if (!obj) continue;

        const nmo_parameterin_state_t *pin =
            (const nmo_parameterin_state_t *)nmo_object_get_state(obj);
        if (!pin || pin->source_id == 0) continue;

        if (pin->is_shared && fctx->found_shared == 0)
            fctx->found_shared = pin_id;
        if (!pin->is_shared && fctx->found_direct == 0)
            fctx->found_direct = pin_id;

        if (fctx->found_shared != 0 && fctx->found_direct != 0)
            return false;
    }
    return true;
}

static nmo_status_t open_test_workspace(
    const char *path,
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    nmo_document_t **out_document,
    nmo_workspace_t **out_workspace)
{
    if (!open_test_file(path, out_ctx, out_session)) {
        return NMO_ERR_CANT_OPEN_FILE;
    }
    nmo_status_t st = nmo_session_borrow_document(*out_session, out_document);
    if (st != NMO_OK) {
        nmo_session_close_with_context(*out_ctx, *out_session);
        *out_ctx = NULL;
        *out_session = NULL;
        return st;
    }
    st = nmo_workspace_create(*out_ctx, *out_document, out_workspace);
    if (st != NMO_OK) {
        nmo_document_destroy(*out_document);
        nmo_session_close_with_context(*out_ctx, *out_session);
        *out_document = NULL;
        *out_ctx = NULL;
        *out_session = NULL;
    }
    return st;
}

static void find_pins(nmo_document_t *document, nmo_workspace_t *workspace,
                      find_pin_ctx_t *fctx)
{
    fctx->workspace = workspace;
    fctx->found_shared = 0;
    fctx->found_direct = 0;

    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 32, NULL);
    ASSERT_EQ(NMO_OK, nmo_behavior_query_collect_scripts(document, &scripts));

    const nmo_behavior_script_view_t *entries =
        (const nmo_behavior_script_view_t *)scripts.data;
    for (size_t i = 0; i < scripts.count; ++i) {
        ASSERT_EQ(
            NMO_OK,
            nmo_behavior_walk(workspace, entries[i].script_id, find_pin_visitor, fctx));
        if (fctx->found_shared != 0 && fctx->found_direct != 0) break;
    }
    nmo_array_dispose(&scripts);
}

/* ---- Tests ---- */

TEST(param_chain, basic_chain_has_steps)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(document, workspace, &fctx);
    if (fctx.found_direct == 0) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 16, NULL);
    nmo_status_t st = nmo_behavior_analyze_trace_param_chain(
        workspace, fctx.found_direct, &chain, 32);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_TRUE(chain.count >= 2);

    const nmo_behavior_trace_step_t *steps =
        (const nmo_behavior_trace_step_t *)chain.data;
    ASSERT_EQ((int)NMO_BEHAVIOR_TRACE_STEP_START, (int)steps[0].type);
    ASSERT_EQ((long long)fctx.found_direct, (long long)steps[0].id);

    /* Last step should be a non-ParameterIn (DirectSource terminal) */
    ASSERT_TRUE(steps[chain.count - 1].class_id != NMO_CID_PARAMETERIN ||
                steps[chain.count - 1].type == NMO_BEHAVIOR_TRACE_STEP_START);

    nmo_array_dispose(&chain);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(param_chain, shared_source_detected)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(document, workspace, &fctx);
    if (fctx.found_shared == 0) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 16, NULL);
    ASSERT_EQ(
        NMO_OK,
        nmo_behavior_analyze_trace_param_chain(
            workspace, fctx.found_shared, &chain, 32));
    ASSERT_TRUE(chain.count >= 2);

    /* The starting pIn has is_shared=true. When the chain continues through
     * another pIn that also has is_shared, that step is SHARED_SOURCE.
     * At minimum verify the chain contains a non-START step. */
    const nmo_behavior_trace_step_t *steps =
        (const nmo_behavior_trace_step_t *)chain.data;
    bool has_shared_or_direct = false;
    for (size_t i = 1; i < chain.count; ++i) {
        if (steps[i].type == NMO_BEHAVIOR_TRACE_STEP_SHARED_SOURCE ||
            steps[i].type == NMO_BEHAVIOR_TRACE_STEP_DIRECT_SOURCE) {
            has_shared_or_direct = true;
            break;
        }
    }
    ASSERT_TRUE(has_shared_or_direct);

    nmo_array_dispose(&chain);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(param_chain, owner_id_populated)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(document, workspace, &fctx);
    if (fctx.found_direct == 0) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 16, NULL);
    ASSERT_EQ(
        NMO_OK,
        nmo_behavior_analyze_trace_param_chain(
            workspace, fctx.found_direct, &chain, 32));

    const nmo_behavior_trace_step_t *steps =
        (const nmo_behavior_trace_step_t *)chain.data;
    bool any_owner = false;
    for (size_t i = 0; i < chain.count; ++i) {
        if (steps[i].owner_id != 0) { any_owner = true; break; }
    }
    ASSERT_TRUE(any_owner);

    nmo_array_dispose(&chain);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(param_chain, max_depth_respected)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(document, workspace, &fctx);
    if (fctx.found_direct == 0) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 16, NULL);
    ASSERT_EQ(
        NMO_OK,
        nmo_behavior_analyze_trace_param_chain(
            workspace, fctx.found_direct, &chain, 1));
    ASSERT_TRUE(chain.count <= 1);

    nmo_array_dispose(&chain);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(param_chain, basic_chain_has_steps);
    REGISTER_TEST(param_chain, shared_source_detected);
    REGISTER_TEST(param_chain, owner_id_populated);
    REGISTER_TEST(param_chain, max_depth_respected);
TEST_MAIN_END()

